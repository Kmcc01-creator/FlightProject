// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightIoRing.h"
#include "FlightIoUringSubsystem.generated.h"

#if PLATFORM_LINUX

namespace Flight::IoUring
{
	struct FCompletionCallback;
}

#endif

/**
 * UFlightIoUringSubsystem
 *
 * World subsystem managing io_uring rings for async I/O operations.
 * Designed for experimental integration with Mass Entity and Vulkan.
 *
 * Architecture (inspired by m2/systems):
 * - Main ring: General purpose async I/O (file, network prep)
 * - Inbox ring: Receives MSG_RING messages from worker threads
 * - Per-thread worker rings: Heavy I/O (spawned on demand)
 *
 * Thread model:
 * - Game thread owns main ring and inbox ring
 * - Worker threads own their rings, communicate via MSG_RING
 * - No mutexes on hot paths - only atomic ring operations
 *
 * Vulkan integration path:
 * - Export Vulkan fence to eventfd (VK_KHR_external_fence_fd)
 * - Poll eventfd via io_uring POLL_ADD
 * - Completion wakes game thread for buffer readback
 */
UCLASS()
class FLIGHTPROJECT_API UFlightIoUringSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFlightIoUringSubsystem();

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Check if io_uring is available and initialized */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring")
	bool IsAvailable() const;

	/** Get last error code */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring")
	int32 GetLastError() const;

	/**
	 * Process pending completions (call from game thread tick)
	 * @return Number of completions processed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring")
	int32 ProcessCompletions();

#if PLATFORM_LINUX

	// ========== Native API (C++ only) ==========

	/** Get main ring for game thread operations */
	Flight::IoUring::FRing* GetMainRing() { return MainRing.IsValid() ? &MainRing : nullptr; }

	/** Get inbox ring fd for MSG_RING targeting */
	int32 GetInboxFd() const { return InboxRing.IsValid() ? InboxRing.GetFd() : -1; }

	/**
	 * Submit async read operation
	 *
	 * @param Fd File descriptor to read from
	 * @param Buffer Destination buffer (must remain valid until completion)
	 * @param Size Bytes to read
	 * @param Offset File offset
	 * @param UserData Identifier returned in completion
	 * @return true if submitted successfully
	 */
	bool SubmitRead(int32 Fd, void* Buffer, uint32 Size, uint64 Offset, uint64 UserData);

	/**
	 * Submit async write operation
	 */
	bool SubmitWrite(int32 Fd, const void* Buffer, uint32 Size, uint64 Offset, uint64 UserData);

	/**
	 * Submit poll operation (useful for eventfd/Vulkan fence)
	 *
	 * @param Fd File descriptor to poll
	 * @param PollMask POLLIN, POLLOUT, etc.
	 * @param UserData Identifier returned in completion
	 * @param bMultishot If true, stays registered until explicitly removed
	 * @return true if submitted successfully
	 */
	bool SubmitPoll(int32 Fd, uint32 PollMask, uint64 UserData, bool bMultishot = false);

	/**
	 * Register completion callback for specific user_data value
	 *
	 * @param UserData User data to match
	 * @param Callback Function to call on completion
	 */
	void RegisterCallback(uint64 UserData, TFunction<void(int32 Result, uint32 Flags)> Callback);

	/** Unregister callback */
	void UnregisterCallback(uint64 UserData);

private:
	// Main ring for game thread I/O
	Flight::IoUring::FRing MainRing;

	// Inbox ring for receiving MSG_RING from workers
	Flight::IoUring::FRing InboxRing;

	// Completion callbacks keyed by user_data
	TMap<uint64, TFunction<void(int32 Result, uint32 Flags)>> Callbacks;

	// Error state
	int32 LastError = 0;

	// Statistics
	uint64 TotalSubmissions = 0;
	uint64 TotalCompletions = 0;

	// Ring configuration
	static constexpr uint32 MainRingSize = 256;
	static constexpr uint32 InboxRingSize = 64;

	// Internal helpers
	void HandleCompletion(const Flight::IoUring::FCqe& Cqe);

#endif // PLATFORM_LINUX
};
