// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU to io_uring integration bridge

#include "IoUring/FlightGpuIoUringBridge.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightAsyncExecutor.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include "IVulkanDynamicRHI.h"
#include <sys/eventfd.h>
#include <unistd.h>
#endif

using namespace Flight::Functional;
using namespace Flight::Async;

DEFINE_LOG_CATEGORY_STATIC(LogFlightGpuBridge, Log, All);

UFlightGpuIoUringBridge::UFlightGpuIoUringBridge()
{
}

UFlightGpuIoUringBridge::~UFlightGpuIoUringBridge()
{
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
	UE_LOG(LogFlightGpuBridge, Log, TEXT("Initializing GPU-io_uring bridge with Managed Reactor..."));

	// Get io_uring subsystem first
	IoUringSubsystem = Collection.InitializeDependency<UFlightIoUringSubsystem>();

	// Initialize centralized reactor
	if (!Reactor.Initialize(128))
	{
		UE_LOG(LogFlightGpuBridge, Error, TEXT("Failed to initialize Vulkan io_uring reactor"));
	}

	// Check if exportable semaphores are available
	bExportableSemaphoresAvailable = Flight::IoUring::FExportableSemaphore::IsExtensionAvailable();

	if (bExportableSemaphoresAvailable)
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("VK_KHR_external_semaphore_fd available - Zero-syscall Managed Reactor path enabled"));
	}
	else
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("VK_KHR_external_semaphore_fd not available - using eventfd fallback"));

		EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
		if (EventFd >= 0)
		{
			SetupIoUringIntegration();
		}
	}
#endif
}

void UFlightGpuIoUringBridge::Deinitialize()
{
#if PLATFORM_LINUX
	UE_LOG(LogFlightGpuBridge, Log,
		TEXT("GPU bridge stats: %lld submissions, %lld completions (mode=ManagedReactor)"),
		TotalSubmissions.load(), TotalCompletions.load());

	Reactor.Shutdown();

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
	return bExportableSemaphoresAvailable && Reactor.IsAvailable();
#else
	return false;
#endif
}

int32 UFlightGpuIoUringBridge::GetPendingCount() const
{
	// Pending count is now managed by the Reactor
	return 0; 
}

#if PLATFORM_LINUX
void UFlightGpuIoUringBridge::SetupIoUringIntegration()
{
	if (!IoUringSubsystem || !IoUringSubsystem->IsAvailable() || EventFd < 0)
	{
		return;
	}

	IFlightAsyncExecutor* Executor = IoUringSubsystem->GetExecutor();
	if (!Executor) return;

	Executor->SubmitEventWait(FAsyncHandle::FromLinuxFd(EventFd), [this](const FAsyncResult& Result) {
		OnEventFdReadable(Result.ErrorCode, Result.Flags);
	});
}

void UFlightGpuIoUringBridge::OnEventFdReadable(int32 Result, uint32 Flags)
{
	if (Result >= 0)
	{
		uint64 Value = 0;
		while (read(EventFd, &Value, sizeof(Value)) > 0) {}

		// Re-register
		if (IoUringSubsystem && IoUringSubsystem->GetExecutor())
		{
			IoUringSubsystem->GetExecutor()->SubmitEventWait(FAsyncHandle::FromLinuxFd(EventFd), [this](const FAsyncResult& Res) {
				OnEventFdReadable(Res.ErrorCode, Res.Flags);
			});
		}
	}
}

// Timeline info tuple for structured binding
struct FTimelineInfo
{
	VkSemaphore Semaphore;
	uint64 Value;
};

bool UFlightGpuIoUringBridge::SignalGpuCompletion(
	int64 TrackingId,
	TFunction<void()> OnComplete,
	TFunction<void()> OnFailure)
{
	if (!IsAvailable())
	{
		if (OnFailure) OnFailure();
		return false;
	}

	IVulkanDynamicRHI* VulkanRHI = GetIVulkanDynamicRHI();
	if (!VulkanRHI)
	{
		if (OnFailure) OnFailure();
		return false;
	}

	FTimelineInfo Info{};
	if (!VulkanRHI->RHIGetGraphicsQueueTimelineSemaphoreInfo(&Info.Semaphore, &Info.Value))
	{
		if (OnFailure) OnFailure();
		return false;
	}

	TotalSubmissions.fetch_add(1, std::memory_order_relaxed);

	TWeakObjectPtr<UFlightGpuIoUringBridge> WeakThis(this);

	VulkanRHI->RHIRunOnQueue(EVulkanRHIRunOnQueueType::Graphics,
		[WeakThis, Info, TrackingId, OnComplete = MoveTemp(OnComplete), OnFailure = MoveTemp(OnFailure)](VkQueue Queue) mutable
		{
			UFlightGpuIoUringBridge* Bridge = WeakThis.Get();
			if (!Bridge) return;

			bool bArmed = Bridge->Reactor.ArmSyncPoint(
				Queue,
				Info.Semaphore,
				Info.Value,
				TrackingId,
				[WeakThis, OnComplete = MoveTemp(OnComplete), OnFailure = MoveTemp(OnFailure)](Flight::IoUring::ESyncResult Result, int32 ErrorCode) mutable
				{
					AsyncTask(ENamedThreads::GameThread, [WeakThis, Result, OnComplete = MoveTemp(OnComplete), OnFailure = MoveTemp(OnFailure)]() mutable
					{
						UFlightGpuIoUringBridge* B = WeakThis.Get();
						if (!B) return;

						if (Result == Flight::IoUring::ESyncResult::Success)
						{
							B->TotalCompletions.fetch_add(1, std::memory_order_relaxed);
							if (OnComplete) OnComplete();
						}
						else
						{
							if (OnFailure) OnFailure();
						}
					});
				}
			);

			if (!bArmed)
			{
				AsyncTask(ENamedThreads::GameThread, [OnFailure = MoveTemp(OnFailure)]() {
					if (OnFailure) OnFailure();
				});
			}
		},
		false /* don't wait */);

	return true;
}
#endif

int32 UFlightGpuIoUringBridge::PollCompletions()
{
#if PLATFORM_LINUX
	int32 Processed = 0;
	Processed += Reactor.Tick();
	
	if (IoUringSubsystem)
	{
		Processed += IoUringSubsystem->ProcessCompletions();
	}
	return Processed;
#endif
	return 0;
}

void UFlightGpuIoUringBridge::RunIntegrationTest()
{
	if (!IsAvailable()) return;

	static int64 TestIdCounter = 2000000;
	int64 TestId = TestIdCounter++;

	UE_LOG(LogFlightGpuBridge, Log, TEXT("Running Managed Reactor Test ID: %lld"), TestId);

	SignalGpuCompletion(TestId, [TestId]()
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("Managed Reactor test PASSED: Received completion for TrackingId=%lld"), TestId);
	});
}
