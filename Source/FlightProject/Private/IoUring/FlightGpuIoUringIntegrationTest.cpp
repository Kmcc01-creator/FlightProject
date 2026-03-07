// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Full GPU to Executor integration test

#include "IoUring/FlightGpuIoUringIntegrationTest.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "IoUring/FlightGpuIoUringBridge.h"
#include "IoUring/FlightVulkanExternalFence.h"
#include "IoUring/FlightExportableSemaphore.h"
#include "Core/FlightAsyncExecutor.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include <poll.h>
#include <unistd.h>
#include <sys/eventfd.h>
#endif

using namespace Flight::Async;

DEFINE_LOG_CATEGORY_STATIC(LogFlightIntegrationTest, Log, All);

UFlightGpuIoUringIntegrationTest::UFlightGpuIoUringIntegrationTest()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UFlightGpuIoUringIntegrationTest::BeginPlay()
{
	Super::BeginPlay();

	IoUringSubsystem = GetWorld()->GetSubsystem<UFlightIoUringSubsystem>();
	GpuBridge = GetWorld()->GetSubsystem<UFlightGpuIoUringBridge>();

	// Run tests
	RunTests();
}

void UFlightGpuIoUringIntegrationTest::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Poll for completions
	if (IoUringSubsystem)
	{
		IoUringSubsystem->ProcessCompletions();
	}
	if (GpuBridge)
	{
		GpuBridge->PollCompletions();
	}
}

void UFlightGpuIoUringIntegrationTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PrintTestSummary();
	Super::EndPlay(EndPlayReason);
}

void UFlightGpuIoUringIntegrationTest::RunTests()
{
	UE_LOG(LogFlightIntegrationTest, Log, TEXT(""));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("========================================"));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  GPU-Executor Integration Test Suite"));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("========================================"));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT(""));

	// Reset counters
	TotalTests = 0;
	PassedTests = 0;
	FailedTests = 0;
	SkippedTests = 0;

	// Run test sequence
	TestIoUringAvailable();
	TestExternalFenceSupport();
	TestExportableSemaphoreSupport();
	TestFenceExport();
	TestFencePoll();
	TestGpuCompletionSignal();

	// Optional stress test
	if (bEnableStressTest)
	{
		UE_LOG(LogFlightIntegrationTest, Log, TEXT(""));
		UE_LOG(LogFlightIntegrationTest, Log, TEXT("--- Stress Test: %d iterations ---"),
			StressTestIterations);

		StressTestPending = StressTestIterations;
		StressTestCompleted = 0;
		StressTestTotalTime = 0.0;
		StressTestMinTime = DBL_MAX;
		StressTestMaxTime = 0.0;

		if (GpuBridge && GpuBridge->IsAvailable())
		{
			for (int32 i = 0; i < StressTestIterations; ++i)
			{
				int64 TrackingId = 2000000 + i;
				double StartTime = FPlatformTime::Seconds();

				GpuBridge->SignalGpuCompletion(TrackingId,
					[this, i, StartTime]()
					{
						double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
						OnComputeComplete(i, ElapsedMs, true);
					});
			}
		}
		else
		{
			UE_LOG(LogFlightIntegrationTest, Warning,
				TEXT("Stress test skipped - Bridge not available"));
			StressTestPending = 0;
		}
	}
}

bool UFlightGpuIoUringIntegrationTest::TestIoUringAvailable()
{
	++TotalTests;
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("Test 1: Async Executor Available"));

	if (!IoUringSubsystem)
	{
		UE_LOG(LogFlightIntegrationTest, Error, TEXT("  FAIL: Subsystem not found"));
		++FailedTests;
		return false;
	}

	if (!IoUringSubsystem->IsAvailable())
	{
		UE_LOG(LogFlightIntegrationTest, Error,
			TEXT("  FAIL: Subsystem not available (error: %d)"),
			IoUringSubsystem->GetLastError());
		++FailedTests;
		return false;
	}

	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  PASS: Executor available (%s)"), 
		IoUringSubsystem->GetExecutor()->GetBackendName());
	++PassedTests;
	return true;
}

bool UFlightGpuIoUringIntegrationTest::TestExternalFenceSupport()
{
	++TotalTests;
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("Test 2: Vulkan External Fence Support"));

#if PLATFORM_LINUX
	using namespace Flight::IoUring;

	VkDevice Device = GetVulkanDevice();
	if (!Device)
	{
		UE_LOG(LogFlightIntegrationTest, Warning,
			TEXT("  SKIP: Not running Vulkan RHI"));
		++SkippedTests;
		return false;
	}

	if (!IsExternalFenceFdSupported())
	{
		UE_LOG(LogFlightIntegrationTest, Warning,
			TEXT("  SKIP: VK_KHR_external_fence_fd not available"));
		++SkippedTests;
		return false;
	}

	UE_LOG(LogFlightIntegrationTest, Log,
		TEXT("  PASS: External fence fd supported"));
	++PassedTests;
	return true;
#else
	UE_LOG(LogFlightIntegrationTest, Warning, TEXT("  SKIP: Not Linux"));
	++SkippedTests;
	return false;
#endif
}

bool UFlightGpuIoUringIntegrationTest::TestExportableSemaphoreSupport()
{
	++TotalTests;
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("Test 3: Vulkan External Semaphore Support"));

#if PLATFORM_LINUX
	using namespace Flight::IoUring;

	if (!FExportableSemaphore::IsExtensionAvailable())
	{
		UE_LOG(LogFlightIntegrationTest, Warning,
			TEXT("  SKIP: VK_KHR_external_semaphore_fd not available"));
		++SkippedTests;
		return false;
	}

	// Try to create an exportable semaphore
	TUniquePtr<FExportableSemaphore> Semaphore = FExportableSemaphore::Create();
	if (!Semaphore)
	{
		UE_LOG(LogFlightIntegrationTest, Error,
			TEXT("  FAIL: Could not create exportable semaphore"));
		++FailedTests;
		return false;
	}

	UE_LOG(LogFlightIntegrationTest, Log,
		TEXT("  PASS: Exportable binary semaphore created (handle=0x%p)"),
		Semaphore->GetHandle());
	++PassedTests;
	return true;
#else
	UE_LOG(LogFlightIntegrationTest, Warning, TEXT("  SKIP: Not Linux"));
	++SkippedTests;
	return false;
#endif
}

bool UFlightGpuIoUringIntegrationTest::TestFenceExport()
{
	++TotalTests;
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("Test 4: Fence Export to Sync Fd"));

#if PLATFORM_LINUX
	using namespace Flight::IoUring;

	if (!IsExternalFenceFdSupported())
	{
		UE_LOG(LogFlightIntegrationTest, Warning,
			TEXT("  SKIP: External fence not supported"));
		++SkippedTests;
		return false;
	}

	// Create exportable fence
	FVulkanExternalFence Fence;
	if (!Fence.InitializeFromRHI())
	{
		UE_LOG(LogFlightIntegrationTest, Error,
			TEXT("  FAIL: Could not create fence (error: %d)"),
			Fence.GetLastError());
		++FailedTests;
		return false;
	}

	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  Created fence: %p"), Fence.GetHandle());

	UE_LOG(LogFlightIntegrationTest, Log,
		TEXT("  PASS: Fence creation successful"));
	++PassedTests;
	return true;
#else
	UE_LOG(LogFlightIntegrationTest, Warning, TEXT("  SKIP: Not Linux"));
	++SkippedTests;
	return false;
#endif
}

bool UFlightGpuIoUringIntegrationTest::TestFencePoll()
{
	++TotalTests;
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("Test 5: Executor EventWait Integration"));

	if (!IoUringSubsystem || !IoUringSubsystem->IsAvailable())
	{
		UE_LOG(LogFlightIntegrationTest, Warning,
			TEXT("  SKIP: Executor not available"));
		++SkippedTests;
		return false;
	}

#if PLATFORM_LINUX
	// Test eventfd wait via executor
	int TestFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (TestFd < 0)
	{
		UE_LOG(LogFlightIntegrationTest, Error,
			TEXT("  FAIL: Could not create eventfd"));
		++FailedTests;
		return false;
	}

	FAsyncHandle Handle = FAsyncHandle::FromLinuxFd(TestFd);
	bool bRes = IoUringSubsystem->GetExecutor()->SubmitEventWait(Handle, [TestFd](const FAsyncResult& Result) {
		close(TestFd);
	});

	if (bRes) {
		uint64 V = 1;
		write(TestFd, &V, 8);
		UE_LOG(LogFlightIntegrationTest, Log, TEXT("  PASS: EventWait submitted"));
		++PassedTests;
	} else {
		UE_LOG(LogFlightIntegrationTest, Error, TEXT("  FAIL: EventWait submission failed"));
		++FailedTests;
		close(TestFd);
	}
	return bRes;
#else
	UE_LOG(LogFlightIntegrationTest, Warning, TEXT("  SKIP: Poll test only on Linux currently"));
	++SkippedTests;
	return false;
#endif
}

bool UFlightGpuIoUringIntegrationTest::TestGpuCompletionSignal()
{
	++TotalTests;
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("Test 6: GPU Completion Signal via Executor"));

	if (!GpuBridge || !GpuBridge->IsAvailable())
	{
		UE_LOG(LogFlightIntegrationTest, Warning,
			TEXT("  SKIP: GPU bridge not available"));
		++SkippedTests;
		return false;
	}

	// Test the completion path
	static int64 TestIdCounter = 1000000;
	int64 TrackingId = TestIdCounter++;

	GpuBridge->SignalGpuCompletion(TrackingId,
		[TrackingId]()
		{
			UE_LOG(LogFlightIntegrationTest, Log,
				TEXT("  Completion callback received for TrackingId=%lld"),
				TrackingId);
		});

	UE_LOG(LogFlightIntegrationTest, Log,
		TEXT("  PASS: GPU completion signal submitted (TrackingId=%lld)"), TrackingId);

	++PassedTests;
	return true;
}

void UFlightGpuIoUringIntegrationTest::OnComputeComplete(int32 TestIndex,
	double ElapsedMs, bool bSuccess)
{
	--StressTestPending;
	++StressTestCompleted;
	StressTestTotalTime += ElapsedMs;
	StressTestMinTime = FMath::Min(StressTestMinTime, ElapsedMs);
	StressTestMaxTime = FMath::Max(StressTestMaxTime, ElapsedMs);

	if (StressTestPending == 0)
	{
		// All stress tests complete
		double AvgTime = StressTestTotalTime / StressTestCompleted;
		UE_LOG(LogFlightIntegrationTest, Log, TEXT(""));
		UE_LOG(LogFlightIntegrationTest, Log,
			TEXT("Stress Test Complete:"));
		UE_LOG(LogFlightIntegrationTest, Log,
			TEXT("  Iterations: %d"), StressTestCompleted);
		UE_LOG(LogFlightIntegrationTest, Log,
			TEXT("  Total time: %.2f ms"), StressTestTotalTime);
		UE_LOG(LogFlightIntegrationTest, Log,
			TEXT("  Avg time:   %.2f ms"), AvgTime);
		UE_LOG(LogFlightIntegrationTest, Log,
			TEXT("  Throughput: %.1f ops/sec"), 1000.0 / AvgTime);
	}
}

void UFlightGpuIoUringIntegrationTest::PrintTestSummary()
{
	UE_LOG(LogFlightIntegrationTest, Log, TEXT(""));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("========================================"));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  Test Summary"));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("========================================"));
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  Total:   %d"), TotalTests);
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  Passed:  %d"), PassedTests);
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  Failed:  %d"), FailedTests);
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("  Skipped: %d"), SkippedTests);
	UE_LOG(LogFlightIntegrationTest, Log, TEXT("========================================"));

	if (FailedTests == 0 && PassedTests > 0)
	{
		UE_LOG(LogFlightIntegrationTest, Log, TEXT("  ALL TESTS PASSED"));
	}
	else if (FailedTests > 0)
	{
		UE_LOG(LogFlightIntegrationTest, Error, TEXT("  SOME TESTS FAILED"));
	}
}
