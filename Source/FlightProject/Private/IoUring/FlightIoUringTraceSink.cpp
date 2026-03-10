// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "IoUring/FlightIoUringTraceSink.h"

#if PLATFORM_LINUX

#include "HAL/RunnableThread.h"
#include "HAL/Thread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/Event.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>

namespace Flight::IoUring
{

FFlightIoUringTraceSink* FFlightIoUringTraceSink::Instance = nullptr;

FFlightIoUringTraceSink::FFlightIoUringTraceSink()
{
	WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FFlightIoUringTraceSink::~FFlightIoUringTraceSink()
{
	Stop();
	if (WakeEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		WakeEvent = nullptr;
	}
	Instance = nullptr;
}

bool FFlightIoUringTraceSink::Start(const FString& FilePath)
{
	if (bIsActive) return true;

	if (!IoRing.Initialize(256, SetupFlags::CoopTaskrun))
	{
		return false;
	}

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(FilePath);
	FString Directory = FPaths::GetPath(AbsolutePath);
	IFileManager::Get().MakeDirectory(*Directory, true);

	FileFd = open(TCHAR_TO_ANSI(*AbsolutePath), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (FileFd < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TraceSink: Failed to open file %s (errno=%d)"), *AbsolutePath, errno);
		return false;
	}

	FileOffset.store(0);
	ActiveWriters.store(0);
	OutstandingWrites.store(0);
	bStopRequested.store(false);

	Slabs = MakeUnique<FSlab[]>(NUM_SLABS);
	TArray<iovec> Iovecs;
	Iovecs.AddUninitialized(NUM_SLABS);

	for (uint32 i = 0; i < NUM_SLABS; ++i)
	{
		Slabs[i].Used = 0;
		Slabs[i].bInFlight.store(false);
		Slabs[i].Index = i;
		
		Iovecs[i].iov_base = Slabs[i].Data;
		Iovecs[i].iov_len = SLAB_SIZE;
	}

	if (!IoRing.RegisterBuffers(Iovecs.GetData(), NUM_SLABS))
	{
		UE_LOG(LogTemp, Warning, TEXT("TraceSink: Failed to register io_uring buffers, falling back to standard WRITEV"));
	}

	bShouldExit = false;
	CompletionThread = FRunnableThread::Create(this, TEXT("FlightIoUringTraceCompletion"), 0, TPri_AboveNormal);
	
	FPlatformProcess::Sleep(0.01f);

	Instance = this;
	bIsActive = UE::Trace::RelayTo(reinterpret_cast<UPTRINT>(this), &TraceWrite, &TraceClose);
	if (!bIsActive)
	{
		UE_LOG(LogTemp, Error, TEXT("TraceSink: UE::Trace::RelayTo failed"));
	}
	
	return bIsActive;
}

void FFlightIoUringTraceSink::Stop()
{
	// We don't early return on !bIsActive because we might need to clean up the thread/ring
	// that was left behind by TraceClose.
	
	// 1. Signal shutdown to producers
	bStopRequested.store(true);
	
	// 2. Stop accepting new Unreal Trace data
	UE::Trace::Stop();

	// 3. Wait for active writers to finish copying data to slabs
	while (ActiveWriters.load(std::memory_order_acquire) > 0)
	{
		FPlatformProcess::Sleep(0.001f);
	}

	// 4. Signal background thread to finish and exit
	bShouldExit = true;
	if (WakeEvent) WakeEvent->Trigger();

	if (CompletionThread)
	{
		// Graceful join (not Kill)
		CompletionThread->WaitForCompletion();
		delete CompletionThread;
		CompletionThread = nullptr;
	}

	// 5. Final low-level cleanup
	IoRing.UnregisterBuffers();
	IoRing.Cleanup();
	
	if (FileFd >= 0)
	{
		close(FileFd);
		FileFd = -1;
	}

	bIsActive = false;
}

uint32 FFlightIoUringTraceSink::Run()
{
	TQueue<FSlab*, EQueueMode::Spsc> RetryQueue;

	while (true)
	{
		bool bWorkDone = false;

		// A. Process Retries
		FSlab* RetrySlab = nullptr;
		while (RetryQueue.Peek(RetrySlab))
		{
			FSqe* Sqe = IoRing.GetSqe();
			if (!Sqe) break;

			uint64 Offset = FileOffset.fetch_add(RetrySlab->Used, std::memory_order_relaxed);
			Sqe->PrepWriteFixed(FileFd, RetrySlab->Data, RetrySlab->Used, Offset, (uint16)RetrySlab->Index, RetrySlab->Index);
			
			RetryQueue.Dequeue(RetrySlab);
			OutstandingWrites.fetch_add(1, std::memory_order_release);
			bWorkDone = true;
		}

		// B. Pump new write requests
		FWriteRequest Request;
		while (WriteQueue.Dequeue(Request))
		{
			FSlab* Slab = Request.Slab;
			FSqe* Sqe = IoRing.GetSqe();
			if (Sqe)
			{
				uint64 Offset = FileOffset.fetch_add(Slab->Used, std::memory_order_relaxed);
				Sqe->PrepWriteFixed(FileFd, Slab->Data, Slab->Used, Offset, (uint16)Slab->Index, Slab->Index);
				OutstandingWrites.fetch_add(1, std::memory_order_release);
				bWorkDone = true;
			}
			else
			{
				RetryQueue.Enqueue(Slab);
			}
		}

		if (bWorkDone)
		{
			IoRing.Submit();
		}

		// C. Process completions
		while (const FCqe* Cqe = IoRing.PeekCqe())
		{
			uint32 Index = static_cast<uint32>(Cqe->UserData);
			if (Index < NUM_SLABS)
			{
				Slabs[Index].Used = 0;
				Slabs[Index].bInFlight.store(false, std::memory_order_release);
			}
			IoRing.CqAdvance(1);
			OutstandingWrites.fetch_sub(1, std::memory_order_release);
			bWorkDone = true;
		}

		// Exit Condition: Stop requested AND all queues/writes are empty
		if (bShouldExit && WriteQueue.IsEmpty() && RetryQueue.IsEmpty() && OutstandingWrites.load(std::memory_order_acquire) == 0)
		{
			break;
		}

		if (!bWorkDone && !bShouldExit)
		{
			WakeEvent->Wait(10); // Sleep up to 10ms if no work
		}
	}
	return 0;
}

bool FFlightIoUringTraceSink::TraceWrite(UPTRINT Handle, const void* Data, uint32 Size)
{
	FFlightIoUringTraceSink* Sink = reinterpret_cast<FFlightIoUringTraceSink*>(Handle);
	if (!Sink || Sink->bStopRequested.load(std::memory_order_acquire)) return false;

	// Refcount to prevent shutdown race
	Sink->ActiveWriters.fetch_add(1, std::memory_order_release);
	ON_SCOPE_EXIT {
		Sink->ActiveWriters.fetch_sub(1, std::memory_order_release);
	};

	const uint8* Src = static_cast<const uint8*>(Data);
	uint32 Remaining = Size;

	while (Remaining > 0)
	{
		uint32 Index = Sink->CurrentSlabIndex.load(std::memory_order_acquire);
		FSlab& Slab = Sink->Slabs[Index];

		if (Slab.bInFlight.load(std::memory_order_acquire))
		{
			FPlatformProcess::Sleep(0.0001f);
			if (Sink->bStopRequested.load(std::memory_order_acquire)) return false;
			continue;
		}

		uint32 Space = SLAB_SIZE - Slab.Used;
		uint32 ToCopy = FMath::Min(Remaining, Space);

		FMemory::Memcpy(Slab.Data + Slab.Used, Src, ToCopy);
		Slab.Used += ToCopy;
		Src += ToCopy;
		Remaining -= ToCopy;

		if (Slab.Used >= SLAB_SIZE / 2 || Remaining == 0)
		{
			Slab.bInFlight.store(true, std::memory_order_release);
			Sink->WriteQueue.Enqueue({ &Slab });
			if (Sink->WakeEvent) Sink->WakeEvent->Trigger();
			
			Sink->CurrentSlabIndex.store((Index + 1) % NUM_SLABS, std::memory_order_release);
		}
	}

	return true;
}

void FFlightIoUringTraceSink::TraceClose(UPTRINT Handle)
{
	FFlightIoUringTraceSink* Sink = reinterpret_cast<FFlightIoUringTraceSink*>(Handle);
	if (Sink)
	{
		// Don't set bIsActive = false here yet, let Stop() handle the final state.
		// Just signal that the relay has ended.
		Sink->bStopRequested.store(true);
	}
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
