// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_LINUX

namespace Flight::IoUring
{

class FRing;

/**
 * FVulkanFenceExport
 *
 * Exports a Vulkan fence to an eventfd for io_uring polling.
 * Uses VK_KHR_external_fence_fd extension.
 *
 * Workflow:
 * 1. Create Vulkan fence with EXTERNAL_FENCE_FD export capability
 * 2. Export fence to eventfd (sync fd)
 * 3. Poll eventfd via io_uring POLL_ADD
 * 4. On completion, eventfd signals and wakes io_uring
 * 5. Game thread processes completion and reads back GPU buffer
 *
 * Requirements:
 * - VK_KHR_external_fence_fd extension enabled
 * - Vulkan 1.1+ or VK_KHR_external_fence_capabilities
 *
 * Note: This is a thin wrapper - the actual Vulkan fence creation
 * happens in the rendering code. This class just manages the
 * eventfd export and io_uring integration.
 */
class FLIGHTPROJECT_API FVulkanFenceExport
{
public:
	FVulkanFenceExport();
	~FVulkanFenceExport();

	// Non-copyable
	FVulkanFenceExport(const FVulkanFenceExport&) = delete;
	FVulkanFenceExport& operator=(const FVulkanFenceExport&) = delete;

	// Movable
	FVulkanFenceExport(FVulkanFenceExport&& Other) noexcept;
	FVulkanFenceExport& operator=(FVulkanFenceExport&& Other) noexcept;

	/**
	 * Import an exported sync fd from Vulkan fence
	 *
	 * Call vkGetFenceFdKHR first to export the fence, then pass
	 * the resulting fd here.
	 *
	 * @param SyncFd The sync fd from vkGetFenceFdKHR
	 * @return true on success
	 */
	bool ImportSyncFd(int32 SyncFd);

	/**
	 * Register for poll notification via io_uring
	 *
	 * @param Ring The io_uring ring to use for polling
	 * @param UserData User data returned in completion
	 * @return true if poll was submitted
	 */
	bool RegisterPoll(FRing& Ring, uint64 UserData);

	/** Check if we have a valid sync fd */
	bool IsValid() const { return SyncFd >= 0; }

	/** Get the sync fd (for manual operations) */
	int32 GetSyncFd() const { return SyncFd; }

	/** Close the sync fd and reset state */
	void Close();

private:
	int32 SyncFd = -1;
	bool bPollRegistered = false;
};

/**
 * FVulkanFencePool
 *
 * Pool of exportable Vulkan fences for efficient reuse.
 * Each fence can be exported to eventfd for io_uring polling.
 *
 * Design:
 * - Pre-allocate fences with external export capability
 * - Track which fences are in flight vs available
 * - Return fences to pool on completion
 *
 * TODO: This is a placeholder - actual implementation requires
 * RHI integration to create fences with proper export flags.
 */
class FLIGHTPROJECT_API FVulkanFencePool
{
public:
	FVulkanFencePool();
	~FVulkanFencePool();

	/**
	 * Initialize the pool
	 * @param PoolSize Number of fences to pre-allocate
	 * @return true on success
	 */
	bool Initialize(uint32 PoolSize);

	/** Shutdown and release all fences */
	void Shutdown();

	/**
	 * Acquire a fence from the pool
	 * @return Fence handle, or nullptr if pool exhausted
	 */
	void* AcquireFence();

	/**
	 * Return a fence to the pool
	 * @param Fence The fence to return
	 */
	void ReleaseFence(void* Fence);

	/** Get number of available fences */
	uint32 GetAvailableCount() const;

	/** Get total pool capacity */
	uint32 GetCapacity() const { return Capacity; }

private:
	TArray<void*> AvailableFences;
	TSet<void*> InFlightFences;
	uint32 Capacity = 0;
	FCriticalSection PoolLock;
};

/**
 * Check if Vulkan external fence export is supported
 *
 * Queries the Vulkan device for VK_KHR_external_fence_fd support.
 * Should be called early to determine if this integration path is viable.
 *
 * @return true if external fence fd export is supported
 */
FLIGHTPROJECT_API bool IsVulkanExternalFenceFdSupported();

/**
 * Export a Vulkan fence to sync fd
 *
 * Wrapper around vkGetFenceFdKHR with proper error handling.
 * The fence must have been created with VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT.
 *
 * @param VkDevice The Vulkan device handle
 * @param VkFence The fence to export
 * @param OutSyncFd Receives the exported sync fd
 * @return true on success
 */
FLIGHTPROJECT_API bool ExportVulkanFenceToSyncFd(void* VkDevice, void* VkFence, int32& OutSyncFd);

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
