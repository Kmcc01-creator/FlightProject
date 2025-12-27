// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU to io_uring integration bridge

#include "IoUring/FlightGpuIoUringBridge.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "IoUring/FlightTestComputeShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "GlobalShader.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightGpuBridge, Log, All);

UFlightGpuIoUringBridge::UFlightGpuIoUringBridge()
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
	UE_LOG(LogFlightGpuBridge, Log, TEXT("Initializing GPU-io_uring bridge..."));

	// Create eventfd for GPU completion signaling
	EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
	if (EventFd < 0)
	{
		UE_LOG(LogFlightGpuBridge, Error,
			TEXT("Failed to create eventfd: %d"), errno);
		return;
	}

	// Get io_uring subsystem (may not be ready yet)
	IoUringSubsystem = Collection.InitializeDependency<UFlightIoUringSubsystem>();

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
		TEXT("GPU-io_uring bridge initialized (eventfd=%d)"), EventFd);
#endif
}

void UFlightGpuIoUringBridge::Deinitialize()
{
#if PLATFORM_LINUX
	UE_LOG(LogFlightGpuBridge, Log,
		TEXT("GPU bridge stats: %lld submissions, %lld completions, %d pending"),
		TotalSubmissions, TotalCompletions, PendingWork.Num());

	// Clean up pending work
	for (auto& Work : PendingWork)
	{
		if (Work.Readback)
		{
			delete Work.Readback;
		}
	}
	PendingWork.Empty();

	if (EventFd >= 0)
	{
		close(EventFd);
		EventFd = -1;
	}
#endif

	Super::Deinitialize();
}

bool UFlightGpuIoUringBridge::IsAvailable() const
{
#if PLATFORM_LINUX
	return EventFd >= 0;
#else
	return false;
#endif
}

#if PLATFORM_LINUX
void UFlightGpuIoUringBridge::SetupIoUringIntegration()
{
	if (!IoUringSubsystem || !IoUringSubsystem->IsAvailable())
	{
		return;
	}

	// Register multishot poll on our eventfd
	// This stays active and fires each time eventfd is signaled
	uint64 UserData = reinterpret_cast<uint64>(this) | 0x8000000000000000ULL;

	IoUringSubsystem->RegisterCallback(UserData,
		[this](int32 Result, uint32 Flags) { OnEventFdReadable(Result, Flags); });

	if (IoUringSubsystem->SubmitPoll(EventFd, POLLIN, UserData, true /* multishot */))
	{
		UE_LOG(LogFlightGpuBridge, Log,
			TEXT("Registered io_uring multishot poll on eventfd"));
	}
	else
	{
		UE_LOG(LogFlightGpuBridge, Warning,
			TEXT("Failed to register io_uring poll - using polling fallback"));
	}
}

void UFlightGpuIoUringBridge::OnEventFdReadable(int32 Result, uint32 Flags)
{
	if (Result > 0)
	{
		// Drain the eventfd
		uint64 Value = 0;
		while (read(EventFd, &Value, sizeof(Value)) > 0)
		{
			// Each read corresponds to a GPU completion
		}

		UE_LOG(LogFlightGpuBridge, Verbose,
			TEXT("io_uring notified of GPU completion"));
	}
}
#endif

int64 UFlightGpuIoUringBridge::SubmitTestCompute(int32 BufferSize, int32 TestValue)
{
	// NOTE: Compute shader is disabled because game modules load too late for
	// shader registration. To enable, move shaders to a plugin with
	// LoadingPhase::PostConfigInit. See FlightTestComputeShader.cpp.
	UE_LOG(LogFlightGpuBridge, Warning,
		TEXT("SubmitTestCompute unavailable - shader registration disabled in game modules"));
	return 0;

#if 0 // DISABLED - DispatchFlightTestCompute not available
	if (BufferSize <= 0)
	{
		UE_LOG(LogFlightGpuBridge, Warning, TEXT("Invalid buffer size: %d"), BufferSize);
		return 0;
	}

	int64 TrackingId = NextId++;
	++TotalSubmissions;

	UE_LOG(LogFlightGpuBridge, Log,
		TEXT("Submitting compute work %lld: size=%d, value=%d"),
		TrackingId, BufferSize, TestValue);

	// Enqueue RDG work on render thread
	ENQUEUE_RENDER_COMMAND(FlightTestCompute)(
		[this, TrackingId, BufferSize, TestValue](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			// Dispatch compute shader
			FRDGBufferRef OutputBuffer = DispatchFlightTestCompute(
				GraphBuilder,
				static_cast<uint32>(BufferSize),
				static_cast<uint32>(TestValue));

			// Setup readback
			FRHIGPUBufferReadback* Readback = new FRHIGPUBufferReadback(TEXT("FlightTestReadback"));

			// Add readback pass
			AddEnqueueCopyPass(GraphBuilder, Readback, OutputBuffer, 0);

			// Execute graph
			GraphBuilder.Execute();

			// Create fence for tracking completion
			FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("FlightTestFence"));
			RHICmdList.WriteGPUFence(Fence);

			// Queue work tracking on game thread
			AsyncTask(ENamedThreads::GameThread, [this, TrackingId, Fence, Readback, BufferSize]()
			{
				FPendingGpuWork Work;
				Work.Id = TrackingId;
				Work.Fence = Fence;
				Work.SubmitTime = FPlatformTime::Seconds();
				Work.Readback = Readback;
				Work.ResultBuffer.SetNumUninitialized(BufferSize);

				PendingWork.Add(MoveTemp(Work));
			});
		});

	return TrackingId;
#endif
}

void UFlightGpuIoUringBridge::RegisterCompletionCallback(int64 TrackingId,
	TFunction<void(TArrayView<uint32> Results, double ElapsedMs)> Callback)
{
	for (auto& Work : PendingWork)
	{
		if (Work.Id == TrackingId)
		{
			Work.Callback = MoveTemp(Callback);
			return;
		}
	}

	UE_LOG(LogFlightGpuBridge, Warning,
		TEXT("No pending work found for tracking ID %lld"), TrackingId);
}

int32 UFlightGpuIoUringBridge::PollCompletions()
{
	int32 CompletedCount = 0;

	for (int32 i = PendingWork.Num() - 1; i >= 0; --i)
	{
		FPendingGpuWork& Work = PendingWork[i];

		// Check if readback is ready
		if (Work.Readback && Work.Readback->IsReady())
		{
			HandleCompletion(Work);
			++CompletedCount;
			++TotalCompletions;

			// Signal eventfd for io_uring notification
#if PLATFORM_LINUX
			if (EventFd >= 0)
			{
				uint64 Value = 1;
				write(EventFd, &Value, sizeof(Value));
			}
#endif

			// Cleanup
			delete Work.Readback;
			PendingWork.RemoveAt(i);
		}
	}

	// Also process io_uring completions
#if PLATFORM_LINUX
	if (IoUringSubsystem)
	{
		IoUringSubsystem->ProcessCompletions();
	}
#endif

	return CompletedCount;
}

void UFlightGpuIoUringBridge::HandleCompletion(FPendingGpuWork& Work)
{
	double ElapsedMs = (FPlatformTime::Seconds() - Work.SubmitTime) * 1000.0;

	UE_LOG(LogFlightGpuBridge, Log,
		TEXT("GPU work %lld completed in %.2f ms"), Work.Id, ElapsedMs);

	// Copy data from readback buffer
	if (Work.Readback)
	{
		const uint32* Data = static_cast<const uint32*>(
			Work.Readback->Lock(Work.ResultBuffer.Num() * sizeof(uint32)));

		if (Data)
		{
			FMemory::Memcpy(Work.ResultBuffer.GetData(), Data,
				Work.ResultBuffer.Num() * sizeof(uint32));
			Work.Readback->Unlock();
		}
	}

	// Invoke callback
	if (Work.Callback)
	{
		Work.Callback(Work.ResultBuffer, ElapsedMs);
	}
}

void UFlightGpuIoUringBridge::RunIntegrationTest()
{
	UE_LOG(LogFlightGpuBridge, Log, TEXT("=== GPU-io_uring Integration Test ==="));

	constexpr int32 TestBufferSize = 1024;
	constexpr int32 TestValue = 42;

	int64 TrackingId = SubmitTestCompute(TestBufferSize, TestValue);
	if (TrackingId == 0)
	{
		UE_LOG(LogFlightGpuBridge, Error, TEXT("Failed to submit test compute"));
		return;
	}

	RegisterCompletionCallback(TrackingId,
		[TestValue](TArrayView<uint32> Results, double ElapsedMs)
		{
			UE_LOG(LogFlightGpuBridge, Log,
				TEXT("Integration test completed in %.2f ms"), ElapsedMs);

			// Verify results
			bool bSuccess = true;
			for (int32 i = 0; i < FMath::Min(Results.Num(), 10); ++i)
			{
				uint32 Expected = TestValue + i;
				if (Results[i] != Expected)
				{
					UE_LOG(LogFlightGpuBridge, Error,
						TEXT("  Result[%d] = %u, expected %u"), i, Results[i], Expected);
					bSuccess = false;
				}
			}

			if (bSuccess)
			{
				UE_LOG(LogFlightGpuBridge, Log,
					TEXT("  PASS: First 10 values verified correctly"));
				UE_LOG(LogFlightGpuBridge, Log,
					TEXT("  Sample: [%u, %u, %u, %u, ...]"),
					Results[0], Results[1], Results[2], Results[3]);
			}
			else
			{
				UE_LOG(LogFlightGpuBridge, Error, TEXT("  FAIL: Result verification failed"));
			}
		});

	UE_LOG(LogFlightGpuBridge, Log,
		TEXT("Test submitted (ID=%lld), poll PollCompletions() each frame"), TrackingId);
}
