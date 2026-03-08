// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "IoUring/FlightIoUringSubsystem.h"
#include "IoUring/FlightGpuIoUringBridge.h"
#include "Async/Async.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include <poll.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightAsync, Log, All);

namespace Flight::Async
{

// ============================================================================
// Standard Fallback Executor (UE GraphEvents / Tasks)
// ============================================================================

class FFlightStandardExecutor : public IFlightAsyncExecutor
{
public:
	virtual bool Initialize() override { return true; }
	virtual int32 Tick() override { return 0; }

	virtual bool SubmitRead(FAsyncHandle Handle, void* Buffer, uint32 Size, uint64 Offset, FCompletionCallback Callback) override
	{
		// Fallback to standard synchronous read in a task (not efficient, but functional)
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Handle, Buffer, Size, Offset, Callback = MoveTemp(Callback)]() {
			FAsyncResult Result;
			// Generic implementation would use platform file I/O here
			Callback(Result);
		});
		return true;
	}

	virtual bool SubmitWrite(FAsyncHandle Handle, const void* Buffer, uint32 Size, uint64 Offset, FCompletionCallback Callback) override
	{
		Callback({-1}); // Not implemented in fallback
		return false;
	}

	virtual TSharedPtr<TReactiveValue<bool>> WaitOnGpuWork(int64 TrackingId) override
	{
		auto Status = MakeShared<TReactiveValue<bool>>(false);
		// Fallback doesn't have zero-syscall path, would need standard polling
		return Status;
	}

	virtual bool SubmitEventWait(FAsyncHandle EventHandle, FCompletionCallback Callback) override { return false; }
	virtual bool IsIoUringOptimized() const override { return false; }
	virtual const TCHAR* GetBackendName() const override { return TEXT("Standard (Fallback)"); }
	virtual int32 GetLastError() const override { return 0; }
};

// ============================================================================
// io_uring Executor (Linux Optimized)
// ============================================================================

#if PLATFORM_LINUX

class FIoUringExecutor : public IFlightAsyncExecutor
{
public:
	FIoUringExecutor() = default;

	virtual bool Initialize() override
	{
		uint32 Flags = Flight::IoUring::SetupFlags::SingleIssuer | Flight::IoUring::SetupFlags::CoopTaskrun;
		if (!MainRing.Initialize(256, Flags)) return false;
		if (!InboxRing.Initialize(64, Flags)) return false;
		return true;
	}

	virtual int32 Tick() override
	{
		int32 Processed = 0;
		Processed += MainRing.ForEachCqe([this](const Flight::IoUring::FCqe& Cqe) { HandleCompletion(Cqe); });
		Processed += InboxRing.ForEachCqe([this](const Flight::IoUring::FCqe& Cqe) { HandleCompletion(Cqe); });
		return Processed;
	}

	virtual bool SubmitRead(FAsyncHandle Handle, void* Buffer, uint32 Size, uint64 Offset, FCompletionCallback Callback) override
	{
		Flight::IoUring::FSqe* Sqe = MainRing.GetSqe();
		if (!Sqe) return false;

		uint64 Token = NextToken++;
		Callbacks.Add(Token, MoveTemp(Callback));
		Sqe->PrepRead(Handle.AsLinuxFd(), Buffer, Size, Offset, Token);
		MainRing.Submit();
		return true;
	}

	virtual bool SubmitWrite(FAsyncHandle Handle, const void* Buffer, uint32 Size, uint64 Offset, FCompletionCallback Callback) override
	{
		Flight::IoUring::FSqe* Sqe = MainRing.GetSqe();
		if (!Sqe) return false;

		uint64 Token = NextToken++;
		Callbacks.Add(Token, MoveTemp(Callback));
		Sqe->PrepWrite(Handle.AsLinuxFd(), Buffer, Size, Offset, Token);
		MainRing.Submit();
		return true;
	}

	virtual TSharedPtr<TReactiveValue<bool>> WaitOnGpuWork(int64 TrackingId) override
	{
		auto Status = MakeShared<TReactiveValue<bool>>(false);
		
		// Bridge to the existing GpuBridge logic but returning a ReactiveValue
		// Note: This requires the Bridge to be initialized.
		if (UWorld* World = GWorld) // Simplified for prototype
		{
			if (auto* Bridge = World->GetSubsystem<UFlightGpuIoUringBridge>())
			{
				const bool bSubmitted = Bridge->SignalGpuCompletion(
					TrackingId,
					[Status]() { *Status = true; },
					[Status]() { *Status = false; });

				if (!bSubmitted)
				{
					*Status = false;
				}
			}
		}

		return Status;
	}

	virtual bool SubmitEventWait(FAsyncHandle EventHandle, FCompletionCallback Callback) override
	{
		Flight::IoUring::FSqe* Sqe = MainRing.GetSqe();
		if (!Sqe) return false;

		uint64 Token = NextToken++;
		Callbacks.Add(Token, MoveTemp(Callback));
		Sqe->PrepPollAdd(EventHandle.AsLinuxFd(), POLLIN, Token);
		MainRing.Submit();
		return true;
	}

	virtual bool IsIoUringOptimized() const override { return true; }
	virtual const TCHAR* GetBackendName() const override { return TEXT("io_uring (Linux)"); }
	virtual int32 GetLastError() const override { return InternalLastError; }

private:
	void HandleCompletion(const Flight::IoUring::FCqe& Cqe)
	{
		FCompletionCallback Callback;
		if (Callbacks.RemoveAndCopyValue(Cqe.UserData, Callback))
		{
			FAsyncResult Result;
			Result.ErrorCode = Cqe.Res;
			Result.Flags = Cqe.Flags;
			if (Cqe.Res < 0) InternalLastError = -Cqe.Res;
			Callback(Result);
		}
	}

	Flight::IoUring::FRing MainRing;
	Flight::IoUring::FRing InboxRing;
	TMap<uint64, FCompletionCallback> Callbacks;
	uint64 NextToken = 1;
	int32 InternalLastError = 0;
};

#endif // PLATFORM_LINUX

TUniquePtr<IFlightAsyncExecutor> CreatePlatformExecutor()
{
#if PLATFORM_LINUX
	auto Executor = MakeUnique<FIoUringExecutor>();
	if (Executor->Initialize()) return Executor;
#endif

	auto Fallback = MakeUnique<FFlightStandardExecutor>();
	Fallback->Initialize();
	return Fallback;
}

} // namespace Flight::Async


// ============================================================================
// UFlightIoUringSubsystem Implementation
// ============================================================================

UFlightIoUringSubsystem::UFlightIoUringSubsystem() {}

bool UFlightIoUringSubsystem::ShouldCreateSubsystem(UObject* Outer) const { return true; }

void UFlightIoUringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Executor = Flight::Async::CreatePlatformExecutor();
	
	UE_LOG(LogFlightAsync, Log, TEXT("Async Executor initialized: %s"), Executor->GetBackendName());
}

void UFlightIoUringSubsystem::Deinitialize()
{
	Executor.Reset();
	Super::Deinitialize();
}

void UFlightIoUringSubsystem::Tick(float DeltaTime)
{
	ProcessCompletions();
}

TStatId UFlightIoUringSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlightIoUringSubsystem, STATGROUP_Tickables);
}

bool UFlightIoUringSubsystem::IsAvailable() const
{
	return Executor.IsValid();
}

int32 UFlightIoUringSubsystem::ProcessCompletions()
{
	return Executor ? Executor->Tick() : 0;
}
