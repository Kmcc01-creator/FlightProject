// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU to io_uring integration bridge

#include "IoUring/FlightGpuIoUringBridge.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "IoUring/FlightExportableSemaphore.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightAsyncExecutor.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include "IVulkanDynamicRHI.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#endif

using namespace Flight::Functional;
using namespace Flight::Async;

DEFINE_LOG_CATEGORY_STATIC(LogFlightGpuBridge, Log, All);

UFlightGpuIoUringBridge::UFlightGpuIoUringBridge()
{
}

UFlightGpuIoUringBridge::~UFlightGpuIoUringBridge()
{
	// Destructor defined here for TUniquePtr<FExportableSemaphore> in FPendingSyncState
}

bool UFlightGpuIoUringBridge::ShouldCreateSubsystem(UObject* Outer) const
{
#if PLATFORM_LINUX
	return true;
#else
	return false;
#endif
}

void UFlightGpuIoUringBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if PLATFORM_LINUX
	UE_LOG(LogFlightGpuBridge, Log, TEXT("Initializing GPU-io_uring bridge..."));

	// Get io_uring subsystem first
	IoUringSubsystem = Collection.InitializeDependency<UFlightIoUringSubsystem>();

	// Check if exportable semaphores are available
	bExportableSemaphoresAvailable = Flight::IoUring::FExportableSemaphore::IsExtensionAvailable();

	if (bExportableSemaphoresAvailable)
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("VK_KHR_external_semaphore_fd available - zero-syscall path enabled"));
	}
	else
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("VK_KHR_external_semaphore_fd not available - using eventfd fallback"));

		// Fallback: Create eventfd for GPU completion signaling
		EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
		if (EventFd < 0)
		{
			UE_LOG(LogFlightGpuBridge, Error,
				TEXT("Failed to create eventfd: %d"), errno);
			return;
		}

		if (IoUringSubsystem && IoUringSubsystem->IsAvailable())
		{
			SetupIoUringIntegration();
		}
		else
		{
			UE_LOG(LogFlightGpuBridge, Warning,
				TEXT("io_uring subsystem not available - using polling fallback"));
		}

		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("GPU-io_uring bridge initialized with EVENTFD fallback (eventfd=%d)"), EventFd);
	}
#endif
}

void UFlightGpuIoUringBridge::Deinitialize()
{
#if PLATFORM_LINUX
	UE_LOG(LogFlightGpuBridge, Log,
		TEXT("GPU bridge stats: %lld submissions, %lld completions, %d pending (mode=%s)"),
		TotalSubmissions.load(), TotalCompletions.load(), GetPendingCount(),
		bExportableSemaphoresAvailable ? TEXT("ExportableSemaphore") : TEXT("EventFd"));

	// Clean up all pending sync states
	{
		FScopeLock Lock(&PendingSyncStatesMutex);
		for (auto& Pair : PendingSyncStates)
		{
			if (Pair.Value)
			{
				CloseSyncFdIfValid(Pair.Value->SyncFd);
				// Semaphore destroyed by TUniquePtr
			}
		}
		PendingSyncStates.Empty();
	}

	if (EventFd >= 0)
	{
		close(EventFd);
		EventFd = -1;
	}

	IoUringSubsystem = nullptr;
#endif

	Super::Deinitialize();
}

bool UFlightGpuIoUringBridge::IsAvailable() const
{
#if PLATFORM_LINUX
	return bExportableSemaphoresAvailable || EventFd >= 0;
#else
	return false;
#endif
}

int32 UFlightGpuIoUringBridge::GetPendingCount() const
{
#if PLATFORM_LINUX
	FScopeLock Lock(&const_cast<UFlightGpuIoUringBridge*>(this)->PendingSyncStatesMutex);
	return PendingSyncStates.Num();
#else
	return 0;
#endif
}

#if PLATFORM_LINUX
void UFlightGpuIoUringBridge::CloseSyncFdIfValid(int32& Fd)
{
	if (Fd >= 0)
	{
		close(Fd);
		Fd = -1;
	}
}

void UFlightGpuIoUringBridge::CleanupPendingState_Locked(int64 TrackingId)
{
	TSharedPtr<FPendingSyncState>* StatePtr = PendingSyncStates.Find(TrackingId);
	if (StatePtr && *StatePtr)
	{
		CloseSyncFdIfValid((*StatePtr)->SyncFd);
	}
	PendingSyncStates.Remove(TrackingId);
}

void UFlightGpuIoUringBridge::SetupIoUringIntegration()
{
	if (!IoUringSubsystem || !IoUringSubsystem->IsAvailable())
	{
		return;
	}

	IFlightAsyncExecutor* Executor = IoUringSubsystem->GetExecutor();
	if (!Executor) return;

	// Register multishot poll on our eventfd (for fallback path)
	FAsyncHandle Handle = FAsyncHandle::FromLinuxFd(EventFd);

	// Note: Generic executor currently doesn't support multishot poll directly in the interface yet,
	// but we can use SubmitEventWait for now.
	bool bSubmitted = Executor->SubmitEventWait(Handle, [this](const FAsyncResult& Result) {
		OnEventFdReadable(Result.ErrorCode, Result.Flags);
	});

	if (bSubmitted)
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("Registered executor wait on eventfd"));
	}
	else
	{
		UE_LOG(LogFlightGpuBridge, Warning,
			TEXT("Failed to register executor wait - using polling fallback"));
	}
}

void UFlightGpuIoUringBridge::OnEventFdReadable(int32 Result, uint32 Flags)
{
	if (Result >= 0)
	{
		// Drain the eventfd
		uint64 Value = 0;
		while (read(EventFd, &Value, sizeof(Value)) > 0)
		{
			// Each read corresponds to a GPU completion
		}

		UE_LOG(LogFlightGpuBridge, Verbose,
			TEXT("Executor notified of GPU completion (eventfd path)"));
			
		// Re-register if needed (SubmitEventWait is currently single-shot in our refactor)
		if (IoUringSubsystem && IoUringSubsystem->GetExecutor())
		{
			IoUringSubsystem->GetExecutor()->SubmitEventWait(FAsyncHandle::FromLinuxFd(EventFd), [this](const FAsyncResult& Res) {
				OnEventFdReadable(Res.ErrorCode, Res.Flags);
			});
		}
	}
}

void UFlightGpuIoUringBridge::OnSyncFdReadable(int64 TrackingId, int32 Result, uint32 Flags)
{
	// NOTE: This method is called from io_uring worker thread, NOT game thread!
	// Must marshal callback invocation to game thread.

	TFunction<void()> Callback;
	double ElapsedMs = 0.0;

	{
		FScopeLock Lock(&PendingSyncStatesMutex);

		TSharedPtr<FPendingSyncState>* StatePtr = PendingSyncStates.Find(TrackingId);
		if (!StatePtr || !*StatePtr)
		{
			UE_LOG(LogFlightGpuBridge, Warning,
				TEXT("OnSyncFdReadable: No pending state for TrackingId %lld"), TrackingId);
			return;
		}

		FPendingSyncState& State = **StatePtr;

		if (Result >= 0)
		{
			ElapsedMs = (FPlatformTime::Seconds() - State.SubmitTime) * 1000.0;

			UE_LOG(LogFlightGpuBridge, Log,
				TEXT("GPU work %lld completed via SYNC_FD in %.2f ms (zero-syscall path!)"),
				TrackingId, ElapsedMs);

			TotalCompletions.fetch_add(1, std::memory_order_relaxed);
			Callback = MoveTemp(State.Callback);
		}
		else
		{
			UE_LOG(LogFlightGpuBridge, Warning,
				TEXT("OnSyncFdReadable: Error for TrackingId %lld, Result=%d"), TrackingId, Result);
		}

		// Clean up (closes sync_fd, removes from map)
		CleanupPendingState_Locked(TrackingId);
	}

	// Marshal callback to game thread - callbacks expect game thread context
	if (Callback)
	{
		AsyncTask(ENamedThreads::GameThread, MoveTemp(Callback));
	}
}

// Timeline info tuple for structured binding
struct FTimelineInfo
{
	VkSemaphore Semaphore;
	uint64 Value;
};

void UFlightGpuIoUringBridge::SignalGpuCompletion(int64 TrackingId, TFunction<void()> Callback)
{
	// Use heterogeneous validation chain - each step produces its own type!
	auto ValidationResult = Validate<FString>()
		.Then([this]() -> TResult<bool, FString>
		{
			if (!bExportableSemaphoresAvailable)
			{
				return Err<bool>(FString(TEXT("Exportable semaphores not available")));
			}
			return Ok(true);
		})
		.Then([](bool) -> TResult<IVulkanDynamicRHI*, FString>
		{
			IVulkanDynamicRHI* VulkanRHI = GetIVulkanDynamicRHI();
			if (!VulkanRHI)
			{
				return Err<IVulkanDynamicRHI*>(FString(TEXT("Vulkan RHI not available")));
			}
			return Ok(VulkanRHI);
		})
		.Then([](bool, IVulkanDynamicRHI* VulkanRHI) -> TResult<FTimelineInfo, FString>
		{
			FTimelineInfo Info{};
			bool bHasTimeline = VulkanRHI->RHIGetGraphicsQueueTimelineSemaphoreInfo(
				&Info.Semaphore, &Info.Value);
			if (!bHasTimeline)
			{
				return Err<FTimelineInfo>(FString(TEXT("Timeline semaphores not available")));
			}
			return Ok(Info);
		})
		.Then([TrackingId](bool, IVulkanDynamicRHI*, FTimelineInfo)
			-> TResult<TUniquePtr<Flight::IoUring::FExportableSemaphore>, FString>
		{
			auto Semaphore = Flight::IoUring::FExportableSemaphore::Create();
			if (!Semaphore)
			{
				return Err<TUniquePtr<Flight::IoUring::FExportableSemaphore>>(FString::Printf(
					TEXT("Failed to create exportable semaphore for TrackingId %lld"), TrackingId));
			}
			return Ok(MoveTemp(Semaphore));
		})
		.Finish();

	// Handle validation failure
	if (ValidationResult.IsErr())
	{
		UE_LOG(LogFlightGpuBridge, Warning, TEXT("SignalGpuCompletion: %s"), *ValidationResult.GetError());
		return;
	}

	// Structured binding! Each step's result is now a separate variable.
	auto [_, VulkanRHI, TimelineInfo, Semaphore] = MoveTemp(ValidationResult).Unwrap();

	// Create pending state and add to map
	TSharedPtr<FPendingSyncState> State = MakeShared<FPendingSyncState>();
	State->TrackingId = TrackingId;
	State->Semaphore = MoveTemp(Semaphore);
	State->Callback = MoveTemp(Callback);
	State->SubmitTime = FPlatformTime::Seconds();
	State->IoUringUserData = NextUserDataId.fetch_add(1, std::memory_order_relaxed);

	{
		FScopeLock Lock(&PendingSyncStatesMutex);

		// Check for duplicate TrackingId - clean up old entry to prevent leaks
		if (PendingSyncStates.Contains(TrackingId))
		{
			UE_LOG(LogFlightGpuBridge, Warning,
				TEXT("SignalGpuCompletion: Duplicate TrackingId %lld - cleaning up old entry"),
				TrackingId);
			CleanupPendingState_Locked(TrackingId);
		}

		PendingSyncStates.Add(TrackingId, State);
	}

	TotalSubmissions.fetch_add(1, std::memory_order_relaxed);

	// Use TWeakObjectPtr to guard against subsystem destruction
	TWeakObjectPtr<UFlightGpuIoUringBridge> WeakThis(this);

	// Capture shared state pointer (not raw) for thread safety
	TSharedPtr<FPendingSyncState> SharedState = State;

	// Capture timeline info for the async lambda
	VkSemaphore UeTimelineSemaphore = TimelineInfo.Semaphore;
	uint64 UeTimelineValue = TimelineInfo.Value;

	VulkanRHI->RHIRunOnQueue(EVulkanRHIRunOnQueueType::Graphics,
		[WeakThis, SharedState, UeTimelineSemaphore, UeTimelineValue](VkQueue Queue)
		{
			UE_LOG(LogFlightGpuBridge, Log, TEXT("SignalGpuCompletion[Queue]: Waiting for TimelineValue %llu on Semaphore %p for TrackingId %lld"), 
				UeTimelineValue, (void*)UeTimelineSemaphore, SharedState->TrackingId);

			// Signal our binary semaphore after waiting on UE's timeline value
			bool bSignaled = SharedState->Semaphore->SignalAfterTimelineValue(
				Queue, UeTimelineSemaphore, UeTimelineValue);

			if (!bSignaled)
			{
				UE_LOG(LogFlightGpuBridge, Error, TEXT("SignalGpuCompletion[Queue]: SignalAfterTimelineValue FAILED for TrackingId %lld"), 
					SharedState->TrackingId);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, TrackingId = SharedState->TrackingId]()
				{
					if (UFlightGpuIoUringBridge* Bridge = WeakThis.Get())
					{
						FScopeLock Lock(&Bridge->PendingSyncStatesMutex);
						Bridge->CleanupPendingState_Locked(TrackingId);
					}
				});
				return;
			}

			// Export sync_fd
			int32 SyncFd = SharedState->Semaphore->ExportSyncFd();
			if (SyncFd < 0)
			{
				UE_LOG(LogFlightGpuBridge, Error, TEXT("SignalGpuCompletion[Queue]: ExportSyncFd FAILED for TrackingId %lld"), 
					SharedState->TrackingId);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, TrackingId = SharedState->TrackingId]()
				{
					if (UFlightGpuIoUringBridge* Bridge = WeakThis.Get())
					{
						FScopeLock Lock(&Bridge->PendingSyncStatesMutex);
						Bridge->CleanupPendingState_Locked(TrackingId);
					}
				});
				return;
			}

			// Register with executor on game thread
			UE_LOG(LogFlightGpuBridge, Log, TEXT("SignalGpuCompletion[Queue]: Exported SyncFd %d for TrackingId %lld. Registering with executor."), 
				SyncFd, SharedState->TrackingId);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, SharedState, SyncFd]()
				{
					UFlightGpuIoUringBridge* Bridge = WeakThis.Get();
					if (!Bridge)
					{
						close(SyncFd);
						return;
					}

					int64 TrackingId = SharedState->TrackingId;

					// Store sync_fd in state
					{
						FScopeLock Lock(&Bridge->PendingSyncStatesMutex);
						TSharedPtr<FPendingSyncState>* StatePtr = Bridge->PendingSyncStates.Find(TrackingId);
						if (!StatePtr || !*StatePtr)
						{
							close(SyncFd);
							return;
						}
						(*StatePtr)->SyncFd = SyncFd;
					}

					if (!Bridge->IoUringSubsystem || !Bridge->IoUringSubsystem->IsAvailable())
					{
						FScopeLock Lock(&Bridge->PendingSyncStatesMutex);
						Bridge->CleanupPendingState_Locked(TrackingId);
						close(SyncFd);
						return;
					}

					IFlightAsyncExecutor* Executor = Bridge->IoUringSubsystem->GetExecutor();
					if (!Executor)
					{
						close(SyncFd);
						return;
					}

					// Submit wait to executor
					Executor->SubmitEventWait(FAsyncHandle::FromLinuxFd(SyncFd), 
						[WeakThis, TrackingId](const FAsyncResult& Res)
						{
							if (UFlightGpuIoUringBridge* B = WeakThis.Get())
							{
								B->OnSyncFdReadable(TrackingId, Res.ErrorCode, Res.Flags);
							}
						});
				});
		},
		false /* don't wait for submission */);
}
#endif

int32 UFlightGpuIoUringBridge::PollCompletions()
{
#if PLATFORM_LINUX
	if (IoUringSubsystem)
	{
		IoUringSubsystem->ProcessCompletions();
	}
#endif

	return 0;
}

void UFlightGpuIoUringBridge::RunIntegrationTest()
{
	if (!IsAvailable()) return;

	static int64 TestIdCounter = 1000000;
	int64 TestId = TestIdCounter++;

	SignalGpuCompletion(TestId, [TestId]()
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("Integration test PASSED: Received completion callback for TrackingId=%lld"), TestId);
	});
}
