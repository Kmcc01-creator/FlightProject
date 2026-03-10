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
 * Status of a sync_fd export operation
 */
enum class EExportStatus : uint8
{
	Success,
	AlreadySignaled,
	Error
};

/**
 * Result of a sync_fd export operation
 */
struct FExportResult
{
	EExportStatus Status;
	int32 Fd = -1;
	int32 ErrorCode = 0;
};

/**
 * Wraps a Vulkan BINARY semaphore created with VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
 * for export to io_uring.
 */
class FLIGHTPROJECT_API FExportableSemaphore
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
	 * @return FExportResult containing the FD and status.
	 */
	FExportResult ExportSyncFd();

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
