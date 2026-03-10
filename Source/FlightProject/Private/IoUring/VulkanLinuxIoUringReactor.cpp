// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "IoUring/VulkanLinuxIoUringReactor.h"

#if PLATFORM_LINUX

#include <poll.h>
#include <unistd.h>
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"

namespace Flight::IoUring
{

FVulkanIoUringReactor::FVulkanIoUringReactor() = default;

FVulkanIoUringReactor::~FVulkanIoUringReactor()
{
	Shutdown();
}

bool FVulkanIoUringReactor::Initialize(uint32 Entries)
{
	uint32 Flags = SetupFlags::SingleIssuer | SetupFlags::CoopTaskrun;
	return Ring.Initialize(Entries, Flags);
}

void FVulkanIoUringReactor::Shutdown()
{
	if (bShutdownRequested.exchange(true))
	{
		return;
	}

	// 1. Wait for active producers to finish their export/enqueue block
	while (ActiveProducers.load(std::memory_order_acquire) > 0)
	{
		FPlatformProcess::Sleep(0.001f);
	}

	// 2. Continuous pump until RequestQueue and PendingStates are empty
	while (true)
	{
		Tick();
		
		FScopeLock Lock(&MapCS);
		if (RequestQueue.IsEmpty() && PendingStates.IsEmpty())
		{
			break;
		}
		
		FPlatformProcess::Sleep(0.01f);
	}
	
	Ring.Cleanup();
}

bool FVulkanIoUringReactor::ArmSyncPoint(
	VkQueue Queue,
	VkSemaphore Timeline,
	uint64 Value,
	uint64 TrackingId,
	TFunction<void(ESyncResult, int32)> Callback)
{
	ActiveProducers.fetch_add(1, std::memory_order_release);
	
	ON_SCOPE_EXIT {
		ActiveProducers.fetch_sub(1, std::memory_order_release);
	};

	if (!IsAvailable() || bShutdownRequested.load(std::memory_order_acquire))
	{
		return false;
	}

	auto State = MakeShared<FSyncFdState>(TrackingId);
	State->Callback = MoveTemp(Callback);
	State->BinarySemaphore = FExportableSemaphore::Create();

	if (!State->BinarySemaphore) return false;

	if (!State->BinarySemaphore->SignalAfterTimelineValue(Queue, Timeline, Value))
	{
		return false;
	}

	FExportResult Export = State->BinarySemaphore->ExportSyncFd();
	if (Export.Status == EExportStatus::Error)
	{
		return false;
	}

	State->SyncFd = Export.Fd;
	State->Phase.store(FSyncFdState::EPhase::Exported, std::memory_order_release);

	RequestQueue.Enqueue(FSyncRequest(State));
	return true;
}

int32 FVulkanIoUringReactor::Tick()
{
	if (!IsAvailable()) return 0;
	
	TArray<TSharedPtr<FSyncFdState>> SubmittedThisTick;
	FSyncRequest Request;
	
	// A. Drain Request Queue
	while (RequestQueue.Dequeue(Request))
	{
		TSharedPtr<FSyncFdState> State = Request.State;
		if (!State.IsValid()) continue;

		if (bShutdownRequested.load(std::memory_order_acquire))
		{
			FinalizeState(State.ToSharedRef(), ESyncResult::Canceled, -1);
			continue;
		}

		if (State->SyncFd == -1) // AlreadySignaled path
		{
			FinalizeState(State.ToSharedRef(), ESyncResult::Success, 0);
			continue;
		}

		FSqe* Sqe = Ring.GetSqe();
		if (!Sqe)
		{
			RequestQueue.Enqueue(FSyncRequest(State));
			break;
		}

		uint64 UserData = NextUserData.fetch_add(1, std::memory_order_relaxed);
		State->UserData = UserData;
		
		{
			FScopeLock Lock(&MapCS);
			PendingStates.Add(UserData, State.ToSharedRef());
		}

		Sqe->PrepPollAdd(State->SyncFd, POLLIN, UserData);
		State->bPollArmed.store(true, std::memory_order_release);
		State->Phase.store(FSyncFdState::EPhase::Armed, std::memory_order_release);
		
		SubmittedThisTick.Add(State);
	}

	// B. Batch Submit
	if (SubmittedThisTick.Num() > 0)
	{
		int32 Ret = Ring.Submit();
		if (Ret < 0)
		{
			int32 RealErr = Ring.GetLastError();
			for (auto& State : SubmittedThisTick)
			{
				{
					FScopeLock Lock(&MapCS);
					PendingStates.Remove(State->UserData);
				}
				FinalizeState(State.ToSharedRef(), ESyncResult::Error, RealErr);
			}
		}
		else
		{
			OutstandingArmedOps.fetch_add(SubmittedThisTick.Num(), std::memory_order_release);
		}
	}

	// C. Process Completions
	return Ring.ForEachCqe([this](const FCqe& Cqe) {
		HandleCompletion(Cqe);
	});
}

void FVulkanIoUringReactor::HandleCompletion(const FCqe& Cqe)
{
	TSharedPtr<FSyncFdState> State = nullptr;
	{
		FScopeLock Lock(&MapCS);
		TSharedRef<FSyncFdState>* Found = PendingStates.Find(Cqe.UserData);
		if (Found)
		{
			State = *Found;
			PendingStates.Remove(Cqe.UserData);
		}
	}

	if (!State.IsValid()) return;

	OutstandingArmedOps.fetch_sub(1, std::memory_order_release);
	State->Phase.store(FSyncFdState::EPhase::Completed, std::memory_order_release);

	if (Cqe.Res >= 0)
	{
		FinalizeState(State.ToSharedRef(), ESyncResult::Success, 0);
	}
	else if (Cqe.Res == -ECANCELED)
	{
		FinalizeState(State.ToSharedRef(), ESyncResult::Canceled, Cqe.Res);
	}
	else
	{
		FinalizeState(State.ToSharedRef(), ESyncResult::Error, Cqe.Res);
	}
}

void FVulkanIoUringReactor::FinalizeState(TSharedRef<FSyncFdState> State, ESyncResult Result, int32 ErrorCode)
{
	if (State->bTerminal.exchange(true))
	{
		return;
	}

	State->Phase.store(FSyncFdState::EPhase::Finalized, std::memory_order_release);

	if (State->Callback)
	{
		State->Callback(Result, ErrorCode);
		State->Callback = nullptr;
	}
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
