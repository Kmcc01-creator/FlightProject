// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Vulkan external fence support for io_uring integration

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_LINUX

// Forward declare Vulkan types to avoid header pollution
typedef struct VkDevice_T* VkDevice;
typedef struct VkFence_T* VkFence;
typedef uint64_t VkDeviceSize;

namespace Flight::IoUring
{

/**
 * FVulkanExternalFence
 *
 * Creates a Vulkan fence that can be exported to a sync fd for io_uring polling.
 * This bypasses UE's fence pool to create fences with VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT.
 *
 * Requirements:
 * - VK_KHR_external_fence extension (device)
 * - VK_KHR_external_fence_fd extension (device)
 * - These are core in Vulkan 1.1+
 *
 * Usage:
 * 1. Create fence before GPU work
 * 2. Submit work with fence
 * 3. Export fence to sync fd
 * 4. Poll sync fd via io_uring
 * 5. Reset and reuse or destroy
 */
class FLIGHTPROJECT_API FVulkanExternalFence
{
public:
	FVulkanExternalFence();
	~FVulkanExternalFence();

	// Non-copyable
	FVulkanExternalFence(const FVulkanExternalFence&) = delete;
	FVulkanExternalFence& operator=(const FVulkanExternalFence&) = delete;

	// Movable
	FVulkanExternalFence(FVulkanExternalFence&& Other) noexcept;
	FVulkanExternalFence& operator=(FVulkanExternalFence&& Other) noexcept;

	/**
	 * Initialize fence for the given Vulkan device
	 *
	 * @param InDevice Vulkan device handle
	 * @return true on success
	 */
	bool Initialize(VkDevice InDevice);

	/**
	 * Initialize using the current RHI Vulkan device
	 * @return true on success
	 */
	bool InitializeFromRHI();

	/** Check if fence is valid */
	bool IsValid() const { return Fence != nullptr; }

	/** Get raw Vulkan fence handle */
	VkFence GetHandle() const { return Fence; }

	/** Get Vulkan device */
	VkDevice GetDevice() const { return Device; }

	/**
	 * Export fence to sync fd
	 *
	 * The fence must be in signaled state or pending signal.
	 * After export, the sync fd becomes the owner of the fence state.
	 *
	 * @param OutSyncFd Receives the sync fd
	 * @return true on success
	 */
	bool ExportToSyncFd(int32& OutSyncFd);

	/**
	 * Reset the fence for reuse
	 *
	 * @return true on success
	 */
	bool Reset();

	/**
	 * Wait for fence to signal (blocking)
	 *
	 * @param TimeoutNs Timeout in nanoseconds (UINT64_MAX for infinite)
	 * @return true if signaled, false on timeout or error
	 */
	bool Wait(uint64 TimeoutNs = UINT64_MAX);

	/**
	 * Check if fence has signaled (non-blocking)
	 *
	 * @return true if signaled
	 */
	bool IsSignaled() const;

	/** Get last Vulkan error code */
	int32 GetLastError() const { return LastError; }

	/** Destroy fence and release resources */
	void Destroy();

private:
	VkDevice Device = nullptr;
	VkFence Fence = nullptr;
	int32 LastError = 0;

	// Vulkan function pointers (loaded dynamically)
	static void* GetFenceFdKHR;
	static bool bFunctionsLoaded;

	static bool LoadFunctions(VkDevice InDevice);
};

/**
 * FExternalFencePool
 *
 * Pool of exportable fences for efficient reuse.
 */
class FLIGHTPROJECT_API FExternalFencePool
{
public:
	FExternalFencePool();
	~FExternalFencePool();

	/**
	 * Initialize pool
	 *
	 * @param InDevice Vulkan device
	 * @param PoolSize Initial pool size
	 * @return true on success
	 */
	bool Initialize(VkDevice InDevice, uint32 PoolSize);

	/**
	 * Initialize using current RHI device
	 */
	bool InitializeFromRHI(uint32 PoolSize);

	/** Shutdown and release all fences */
	void Shutdown();

	/**
	 * Acquire a fence from pool
	 * @return Fence or nullptr if pool exhausted
	 */
	FVulkanExternalFence* Acquire();

	/**
	 * Release fence back to pool
	 */
	void Release(FVulkanExternalFence* Fence);

	/** Get available fence count */
	uint32 GetAvailableCount() const;

private:
	VkDevice Device = nullptr;
	TArray<TUniquePtr<FVulkanExternalFence>> AllFences;
	TArray<FVulkanExternalFence*> AvailableFences;
	FCriticalSection Lock;
};

/**
 * Check if external fence fd export is supported
 *
 * @return true if VK_KHR_external_fence_fd is available
 */
FLIGHTPROJECT_API bool IsExternalFenceFdSupported();

/**
 * Get the Vulkan device handle from RHI
 *
 * @return VkDevice or nullptr if not available
 */
FLIGHTPROJECT_API VkDevice GetVulkanDevice();

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
