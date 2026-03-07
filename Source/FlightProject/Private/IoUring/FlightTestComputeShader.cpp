// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU completion tracking for io_uring integration
//
// NOTE: Compute shader implementations have moved to the FlightGpuCompute plugin.
// See: Plugins/FlightGpuCompute/Source/FlightGpuCompute/Private/FlightTestComputeShader.cpp

#include "IoUring/FlightTestComputeShader.h"

#if PLATFORM_LINUX
#include <sys/eventfd.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightGpuTracker, Log, All);

// ============================================================================
// GPU Completion Tracker
// ============================================================================

FFlightGpuCompletionTracker::FFlightGpuCompletionTracker()
{
#if PLATFORM_LINUX
	// Create eventfd for io_uring integration
	EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (EventFd < 0)
	{
		UE_LOG(LogFlightGpuTracker, Warning,
			TEXT("Failed to create eventfd for GPU completion tracking"));
	}
#endif
}

FFlightGpuCompletionTracker::~FFlightGpuCompletionTracker()
{
#if PLATFORM_LINUX
	if (EventFd >= 0)
	{
		close(EventFd);
		EventFd = -1;
	}
#endif
}

uint64 FFlightGpuCompletionTracker::TrackFence(FRHIGPUFence* FenceRHI, TFunction<void()> Callback)
{
	FScopeLock ScopeLock(&Lock);

	FTrackedFence Entry;
	Entry.Id = NextId++;
	Entry.Fence = FenceRHI;
	Entry.Callback = MoveTemp(Callback);
	Entry.StartTime = FPlatformTime::Seconds();

	TrackedFences.Add(MoveTemp(Entry));

	UE_LOG(LogFlightGpuTracker, Verbose,
		TEXT("Tracking GPU fence %llu"), Entry.Id);

	return Entry.Id;
}

void FFlightGpuCompletionTracker::CancelTracking(uint64 TrackingId)
{
	FScopeLock ScopeLock(&Lock);

	TrackedFences.RemoveAll([TrackingId](const FTrackedFence& Entry)
	{
		return Entry.Id == TrackingId;
	});
}

int32 FFlightGpuCompletionTracker::Poll()
{
	FScopeLock ScopeLock(&Lock);

	int32 CompletedCount = 0;
	TArray<TFunction<void()>> CallbacksToInvoke;

	// Check each tracked fence
	for (int32 i = TrackedFences.Num() - 1; i >= 0; --i)
	{
		FTrackedFence& Entry = TrackedFences[i];

		if (Entry.Fence.IsValid() && Entry.Fence->Poll())
		{
			double ElapsedMs = (FPlatformTime::Seconds() - Entry.StartTime) * 1000.0;
			UE_LOG(LogFlightGpuTracker, Verbose,
				TEXT("GPU fence %llu completed in %.2f ms"), Entry.Id, ElapsedMs);

			CallbacksToInvoke.Add(MoveTemp(Entry.Callback));
			TrackedFences.RemoveAt(i);
			++CompletedCount;
		}
	}

	// Signal eventfd if any fences completed
	if (CompletedCount > 0)
	{
		SignalEventFd();
	}

	// Invoke callbacks outside the lock
	Lock.Unlock();
	for (auto& Callback : CallbacksToInvoke)
	{
		Callback();
	}
	Lock.Lock();

	return CompletedCount;
}

void FFlightGpuCompletionTracker::SignalEventFd()
{
#if PLATFORM_LINUX
	if (EventFd >= 0)
	{
		uint64 Value = 1;
		ssize_t Written = write(EventFd, &Value, sizeof(Value));
		if (Written != sizeof(Value))
		{
			UE_LOG(LogFlightGpuTracker, Warning,
				TEXT("Failed to signal eventfd"));
		}
	}
#endif
}
