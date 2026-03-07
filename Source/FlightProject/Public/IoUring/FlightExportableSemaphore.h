// Copyright Kelly Rey Wilson. All Rights Reserved.
// Exportable binary semaphore for io_uring GPU sync integration

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_LINUX

// Forward declarations to avoid Vulkan header in public header
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;

namespace Flight::IoUring
{

/**
 * Wraps a Vulkan BINARY semaphore created with VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
 * for export to io_uring.
 *
 * NOTE: Per Vulkan spec, SYNC_FD handles can ONLY be exported from binary semaphores,
 * not timeline semaphores. This class creates a binary semaphore that waits on UE's
 * timeline semaphore and signals itself, then exports the sync_fd.
 *
 * Each instance is single-use:
 *   1. Create instance after RHI init
 *   2. Call SignalAfterTimelineValue() on queue submission thread
 *   3. Call ExportSyncFd() to get fd for io_uring POLL_ADD
 *   4. When CQE fires, destroy the instance (closes semaphore)
 *
 * For multiple pending GPU jobs, create multiple FExportableSemaphore instances.
 */
class FExportableSemaphore
{
public:
	FExportableSemaphore();
	~FExportableSemaphore();

	// Non-copyable, non-movable (owns Vulkan resource)
	FExportableSemaphore(const FExportableSemaphore&) = delete;
	FExportableSemaphore& operator=(const FExportableSemaphore&) = delete;
	FExportableSemaphore(FExportableSemaphore&&) = delete;
	FExportableSemaphore& operator=(FExportableSemaphore&&) = delete;

	/** Check if semaphore was successfully created with export capability */
	bool IsValid() const { return Semaphore != nullptr; }

	/** Check if the external semaphore extension is available (call once at startup) */
	static bool IsExtensionAvailable();

	/**
	 * Create and initialize a new exportable binary semaphore.
	 * Returns nullptr if extension not available or creation fails.
	 */
	static TUniquePtr<FExportableSemaphore> Create();

	/**
	 * Signal this binary semaphore after waiting on a timeline semaphore value.
	 * Must be called from submission thread (via RHIRunOnQueue).
	 *
	 * @param Queue The Vulkan queue to submit on
	 * @param WaitSemaphore Timeline semaphore to wait on (UE's queue semaphore)
	 * @param WaitValue Timeline value to wait for
	 * @return true on success, false on error
	 */
	bool SignalAfterTimelineValue(VkQueue Queue, VkSemaphore WaitSemaphore, uint64 WaitValue);

	/**
	 * Export a sync_fd that becomes readable when this semaphore is signaled.
	 *
	 * IMPORTANT: After exporting, this semaphore instance should be considered
	 * "consumed" - the sync_fd now owns the signal state. The semaphore handle
	 * remains valid until destruction, but should not be used for further signaling.
	 *
	 * @return File descriptor (>=0) or -1 on error. Caller must close() when done.
	 */
	int32 ExportSyncFd();

	/** Check if sync_fd has already been exported */
	bool HasExported() const { return bHasExported; }

	/** Get the raw VkSemaphore handle (for debugging) */
	VkSemaphore GetHandle() const { return Semaphore; }

private:
	/** Private constructor - use Create() factory method */
	bool Initialize();

	VkDevice Device = nullptr;
	VkSemaphore Semaphore = nullptr;
	bool bHasExported = false;
	bool bHasSignaled = false;

	// Cached function pointers (static - shared across instances)
	static void* FnGetSemaphoreFd;
	static void* FnCreateSemaphore;
	static void* FnDestroySemaphore;
	static void* FnQueueSubmit;
	static bool bFunctionPointersLoaded;

	static bool LoadFunctionPointers();
};

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
