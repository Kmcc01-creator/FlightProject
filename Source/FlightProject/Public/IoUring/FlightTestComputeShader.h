// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU completion tracking for io_uring integration
//
// NOTE: Compute shader classes have moved to the FlightGpuCompute plugin.
// Use #include "FlightTestComputeShader.h" from FlightGpuCompute for:
//   - FFlightTestComputeShader
//   - FFlightReductionComputeShader
//   - DispatchFlightTestCompute()
//
// This file retains FFlightGpuCompletionTracker which doesn't need PostConfigInit.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

/**
 * FFlightGpuCompletionTracker
 *
 * Tracks GPU work completion and bridges to io_uring notification.
 * Uses RHI fence polling with optional eventfd signaling.
 *
 * NOTE: For zero-syscall GPU completion notification via exportable semaphores,
 * use UFlightGpuIoUringBridge::SignalGpuCompletion() instead.
 */
class FLIGHTPROJECT_API FFlightGpuCompletionTracker
{
public:
	FFlightGpuCompletionTracker();
	~FFlightGpuCompletionTracker();

	/**
	 * Track a GPU fence for completion
	 *
	 * @param FenceRHI The RHI fence to track
	 * @param Callback Called when fence signals (on game thread)
	 * @return Tracking ID for cancellation
	 */
	uint64 TrackFence(FRHIGPUFence* FenceRHI, TFunction<void()> Callback);

	/**
	 * Cancel fence tracking
	 */
	void CancelTracking(uint64 TrackingId);

	/**
	 * Poll tracked fences (call from game thread tick)
	 * @return Number of fences that signaled
	 */
	int32 Poll();

	/** Get eventfd for io_uring integration (signaled when any fence completes) */
	int32 GetEventFd() const { return EventFd; }

private:
	struct FTrackedFence
	{
		uint64 Id;
		FGPUFenceRHIRef Fence;
		TFunction<void()> Callback;
		double StartTime;
	};

	TArray<FTrackedFence> TrackedFences;
	uint64 NextId = 1;
	int32 EventFd = -1;
	FCriticalSection Lock;

	void SignalEventFd();
};
