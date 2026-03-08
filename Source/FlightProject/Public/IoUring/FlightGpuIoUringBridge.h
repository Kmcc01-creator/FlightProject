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
#include "IoUring/FlightExportableSemaphore.h"
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
 * Architecture (when VK_KHR_external_semaphore_fd is available):
 * 1. Game thread submits compute work via RDG
 * 2. After GraphBuilder.Execute(), call SignalExportableSemaphoreForWork()
 * 3. RHIRunOnQueue creates binary semaphore, waits on UE's timeline, signals binary
 * 4. Export sync_fd from binary semaphore
 * 5. io_uring POLL_ADD on sync_fd
 * 6. CQE arrives when GPU work completes - zero syscalls in steady state
 *
 * Each pending GPU job gets its own binary semaphore and sync_fd (per Vulkan spec,
 * SYNC_FD can only be exported from binary semaphores, not timeline).
 *
 * Fallback (when extension unavailable):
 * - Uses eventfd + polling as placeholder
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
	 * Creates a binary semaphore that waits on UE's timeline and exports sync_fd.
	 *
	 * @param TrackingId Unique ID for this GPU work
	 * @param OnComplete Called on game thread when GPU work completes
	 * @param OnFailure Called on game thread when signaling setup fails
	 * @return True if completion signaling was registered, false on immediate setup failure
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
	// Atomic counters - incremented from io_uring worker thread, read from game thread
	std::atomic<int64> TotalSubmissions{0};
	std::atomic<int64> TotalCompletions{0};

#if PLATFORM_LINUX
	/**
	 * Per-job synchronization state.
	 * Each pending GPU job gets its own binary semaphore and sync_fd.
	 */
	struct FPendingSyncState
	{
		int64 TrackingId = 0;
		TUniquePtr<Flight::IoUring::FExportableSemaphore> Semaphore;
		int32 SyncFd = -1;
		uint64 IoUringUserData = 0;
		TFunction<void()> CompletionCallback;
		TFunction<void()> FailureCallback;
		double SubmitTime = 0.0;
	};

	// Map of TrackingId -> pending sync state
	TMap<int64, TSharedPtr<FPendingSyncState>> PendingSyncStates;
	FCriticalSection PendingSyncStatesMutex;

	int32 EventFd = -1;
	UFlightIoUringSubsystem* IoUringSubsystem = nullptr;
	bool bExportableSemaphoresAvailable = false;

	// Counter for generating unique io_uring user data
	std::atomic<uint64> NextUserDataId{1};

	void SetupIoUringIntegration();
	void OnEventFdReadable(int32 Result, uint32 Flags);
	void OnSyncFdReadable(int64 TrackingId, int32 Result, uint32 Flags);
	void HandleSyncFailure(int64 TrackingId, const TCHAR* Reason);

	/**
	 * Clean up a pending sync state (close fd, remove from map).
	 * Must be called with PendingSyncStatesMutex held.
	 */
	void CleanupPendingState_Locked(int64 TrackingId);

	/**
	 * Close sync_fd if valid and reset to -1.
	 */
	static void CloseSyncFdIfValid(int32& Fd);
#endif
};
