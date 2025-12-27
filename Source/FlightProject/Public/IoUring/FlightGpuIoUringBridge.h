// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU to io_uring integration bridge

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RenderGraphBuilder.h"
#include "FlightGpuIoUringBridge.generated.h"

class UFlightIoUringSubsystem;
class FRHIGPUBufferReadback;

#if PLATFORM_LINUX
namespace Flight::IoUring
{
	class FRing;
}
#endif

/**
 * UFlightGpuIoUringBridge
 *
 * Bridges GPU compute work completion to io_uring notifications.
 * Enables game thread to be woken by io_uring when GPU work completes.
 *
 * Architecture:
 * 1. Game thread submits compute work via RDG
 * 2. RDG executes and returns a GPU fence
 * 3. Bridge tracks fence, signals eventfd when complete
 * 4. io_uring polls eventfd, wakes game thread
 * 5. Game thread reads back results
 *
 * Future optimization path:
 * - Export Vulkan fence directly via VK_KHR_external_fence_fd
 * - Poll fence fd directly (no intermediate eventfd)
 * - Zero-copy buffer access via external memory
 */
UCLASS()
class FLIGHTPROJECT_API UFlightGpuIoUringBridge : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFlightGpuIoUringBridge();

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Check if bridge is operational */
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	bool IsAvailable() const;

	/**
	 * Submit test compute shader and track completion
	 *
	 * @param BufferSize Number of uint32 elements
	 * @param TestValue Value to write to buffer
	 * @param Callback Called when GPU work completes with result buffer
	 * @return Tracking ID (0 on failure)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int64 SubmitTestCompute(int32 BufferSize, int32 TestValue);

	/**
	 * Register callback for compute completion
	 *
	 * @param TrackingId ID from SubmitTestCompute
	 * @param Callback Called with result data
	 */
	void RegisterCompletionCallback(int64 TrackingId,
		TFunction<void(TArrayView<uint32> Results, double ElapsedMs)> Callback);

	/**
	 * Poll for GPU completions (call each frame)
	 * @return Number of completions processed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int32 PollCompletions();

	/** Run integration test */
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	void RunIntegrationTest();

	// Statistics
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int32 GetPendingCount() const { return PendingWork.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int64 GetTotalSubmissions() const { return TotalSubmissions; }

	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int64 GetTotalCompletions() const { return TotalCompletions; }

private:
	struct FPendingGpuWork
	{
		int64 Id;
		FGPUFenceRHIRef Fence;
		TFunction<void(TArrayView<uint32>, double)> Callback;
		double SubmitTime;
		TArray<uint32> ResultBuffer;
		FRHIGPUBufferReadback* Readback;
	};

	TArray<FPendingGpuWork> PendingWork;
	int64 NextId = 1;
	int64 TotalSubmissions = 0;
	int64 TotalCompletions = 0;

#if PLATFORM_LINUX
	int32 EventFd = -1;
	UFlightIoUringSubsystem* IoUringSubsystem = nullptr;

	void SetupIoUringIntegration();
	void OnEventFdReadable(int32 Result, uint32 Flags);
#endif

	void HandleCompletion(FPendingGpuWork& Work);
};
