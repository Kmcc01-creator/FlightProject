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
#include "IoUring/VulkanLinuxIoUringReactor.h"
#endif

/**
 * UFlightGpuIoUringBridge
 *
 * Bridges GPU compute work completion to io_uring notifications.
 * Enables game thread to be woken by io_uring when GPU work completes.
 *
 * Architecture:
 * 1. Game thread submits compute work via RDG
 * 2. SignalGpuCompletion() is called with a unique tracking ID
 * 3. FVulkanIoUringReactor manages the "Signal Binary -> Export FD -> Poll io_uring" pipeline
 * 4. Completion is handled asynchronously and marshaled back to the game thread
 */
UCLASS()
class FLIGHTPROJECT_API UFlightGpuIoUringBridge : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFlightGpuIoUringBridge();
	virtual ~UFlightGpuIoUringBridge();

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Check if bridge is operational */
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	bool IsAvailable() const;

	/** Check if the zero-syscall path (exportable semaphores) is active */
	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	bool IsUsingExportableSemaphores() const { return bExportableSemaphoresAvailable; }

	/**
	 * Signal GPU completion for a tracking ID.
	 * Call this after GraphBuilder.Execute() returns.
	 * Uses the centralized reactor for safe synchronization.
	 *
	 * @param TrackingId Unique ID for this GPU work
	 * @param OnComplete Called on game thread when GPU work completes
	 * @param OnFailure Called on game thread when signaling setup fails
	 * @return True if completion signaling was registered, false on setup failure
	 */
	bool SignalGpuCompletion(
		int64 TrackingId,
		TFunction<void()> OnComplete,
		TFunction<void()> OnFailure = TFunction<void()>());

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
	int32 GetPendingCount() const;

	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int64 GetTotalSubmissions() const { return TotalSubmissions.load(std::memory_order_relaxed); }

	UFUNCTION(BlueprintCallable, Category = "Flight|GpuBridge")
	int64 GetTotalCompletions() const { return TotalCompletions.load(std::memory_order_relaxed); }

private:
	// Atomic counters
	std::atomic<int64> TotalSubmissions{0};
	std::atomic<int64> TotalCompletions{0};

#if PLATFORM_LINUX
	Flight::IoUring::FVulkanIoUringReactor Reactor;
	
	UFlightIoUringSubsystem* IoUringSubsystem = nullptr;
	bool bExportableSemaphoresAvailable = false;
	int32 EventFd = -1;

	void SetupIoUringIntegration();
	void OnEventFdReadable(int32 Result, uint32 Flags);
#endif
};
