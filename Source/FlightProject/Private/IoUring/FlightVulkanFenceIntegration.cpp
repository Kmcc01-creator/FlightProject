// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#include "IoUring/FlightVulkanFenceIntegration.h"
#include "IoUring/FlightIoRing.h"

#if PLATFORM_LINUX

#include <unistd.h>
#include <poll.h>

// RHI headers for accessing Vulkan device
#include "RHI.h"
#include "DynamicRHI.h"

// VULKAN_RHI_AVAILABLE is defined by the build system in Definitions.h

DEFINE_LOG_CATEGORY_STATIC(LogFlightVulkanFence, Log, All);

namespace Flight::IoUring
{

// ============================================================================
// FVulkanFenceExport
// ============================================================================

FVulkanFenceExport::FVulkanFenceExport()
	: SyncFd(-1)
	, bPollRegistered(false)
{
}

FVulkanFenceExport::~FVulkanFenceExport()
{
	Close();
}

FVulkanFenceExport::FVulkanFenceExport(FVulkanFenceExport&& Other) noexcept
	: SyncFd(Other.SyncFd)
	, bPollRegistered(Other.bPollRegistered)
{
	Other.SyncFd = -1;
	Other.bPollRegistered = false;
}

FVulkanFenceExport& FVulkanFenceExport::operator=(FVulkanFenceExport&& Other) noexcept
{
	if (this != &Other)
	{
		Close();
		SyncFd = Other.SyncFd;
		bPollRegistered = Other.bPollRegistered;
		Other.SyncFd = -1;
		Other.bPollRegistered = false;
	}
	return *this;
}

bool FVulkanFenceExport::ImportSyncFd(int32 InSyncFd)
{
	if (InSyncFd < 0)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Invalid sync fd: %d"), InSyncFd);
		return false;
	}

	Close();
	SyncFd = InSyncFd;
	bPollRegistered = false;

	UE_LOG(LogFlightVulkanFence, Verbose, TEXT("Imported sync fd: %d"), SyncFd);
	return true;
}

bool FVulkanFenceExport::RegisterPoll(FRing& Ring, uint64 UserData)
{
	if (!IsValid())
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Cannot register poll: no valid sync fd"));
		return false;
	}

	if (!Ring.IsValid())
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Cannot register poll: ring not valid"));
		return false;
	}

	FSqe* Sqe = Ring.GetSqe();
	if (!Sqe)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Cannot register poll: SQ full"));
		return false;
	}

	// Poll for POLLIN - sync fd becomes readable when fence signals
	Sqe->PrepPollAdd(SyncFd, POLLIN, UserData);

	int32 Submitted = Ring.Submit();
	if (Submitted < 0)
	{
		UE_LOG(LogFlightVulkanFence, Warning,
			TEXT("Failed to submit poll: error %d"), Ring.GetLastError());
		return false;
	}

	bPollRegistered = true;
	UE_LOG(LogFlightVulkanFence, Verbose,
		TEXT("Registered poll for sync fd %d with user_data %llu"), SyncFd, UserData);
	return true;
}

void FVulkanFenceExport::Close()
{
	if (SyncFd >= 0)
	{
		close(SyncFd);
		SyncFd = -1;
	}
	bPollRegistered = false;
}

// ============================================================================
// FVulkanFencePool
// ============================================================================

FVulkanFencePool::FVulkanFencePool()
	: Capacity(0)
{
}

FVulkanFencePool::~FVulkanFencePool()
{
	Shutdown();
}

bool FVulkanFencePool::Initialize(uint32 PoolSize)
{
	FScopeLock Lock(&PoolLock);

	if (Capacity > 0)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Pool already initialized"));
		return false;
	}

	// TODO: Actual Vulkan fence creation with export flags
	// For now, just reserve space - actual fences created on demand
	// when we have RHI integration

	Capacity = PoolSize;
	AvailableFences.Reserve(PoolSize);

	UE_LOG(LogFlightVulkanFence, Log,
		TEXT("Fence pool initialized with capacity %d (fences created on demand)"), PoolSize);
	return true;
}

void FVulkanFencePool::Shutdown()
{
	FScopeLock Lock(&PoolLock);

	// TODO: Destroy actual Vulkan fences
	// For now, just clear the arrays

	if (InFlightFences.Num() > 0)
	{
		UE_LOG(LogFlightVulkanFence, Warning,
			TEXT("Shutting down with %d fences still in flight"), InFlightFences.Num());
	}

	AvailableFences.Empty();
	InFlightFences.Empty();
	Capacity = 0;
}

void* FVulkanFencePool::AcquireFence()
{
	FScopeLock Lock(&PoolLock);

	if (AvailableFences.Num() > 0)
	{
		void* Fence = AvailableFences.Pop(EAllowShrinking::No);
		InFlightFences.Add(Fence);
		return Fence;
	}

	// TODO: Create new fence if under capacity
	// For now, return nullptr to indicate pool exhausted

	UE_LOG(LogFlightVulkanFence, Warning, TEXT("Fence pool exhausted"));
	return nullptr;
}

void FVulkanFencePool::ReleaseFence(void* Fence)
{
	FScopeLock Lock(&PoolLock);

	if (InFlightFences.Remove(Fence) > 0)
	{
		// TODO: Reset fence before returning to pool
		AvailableFences.Add(Fence);
	}
	else
	{
		UE_LOG(LogFlightVulkanFence, Warning,
			TEXT("Attempted to release fence not in pool: %p"), Fence);
	}
}

uint32 FVulkanFencePool::GetAvailableCount() const
{
	FScopeLock Lock(&const_cast<FVulkanFencePool*>(this)->PoolLock);
	return AvailableFences.Num();
}

// ============================================================================
// Utility Functions
// ============================================================================

bool IsVulkanExternalFenceFdSupported()
{
#if defined(VULKAN_RHI_AVAILABLE)
	// TODO: Query actual Vulkan device capabilities
	// Check for VK_KHR_external_fence_fd extension

	// For now, assume supported on Linux with Vulkan
	// Real implementation needs to check:
	// 1. vkGetPhysicalDeviceExternalFenceProperties
	// 2. VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT support
	return true;
#else
	return false;
#endif
}

bool ExportVulkanFenceToSyncFd(void* VkDevice, void* VkFence, int32& OutSyncFd)
{
	OutSyncFd = -1;

#if defined(VULKAN_RHI_AVAILABLE)
	// TODO: Implement actual Vulkan fence export
	//
	// VkFenceGetFdInfoKHR GetFdInfo = {};
	// GetFdInfo.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
	// GetFdInfo.fence = static_cast<VkFence>(VkFence);
	// GetFdInfo.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
	//
	// VkResult Result = vkGetFenceFdKHR(
	//     static_cast<VkDevice>(VkDevice),
	//     &GetFdInfo,
	//     &OutSyncFd
	// );
	//
	// return Result == VK_SUCCESS;

	UE_LOG(LogFlightVulkanFence, Warning,
		TEXT("ExportVulkanFenceToSyncFd: Not yet implemented - requires RHI integration"));
	return false;
#else
	UE_LOG(LogFlightVulkanFence, Warning,
		TEXT("ExportVulkanFenceToSyncFd: Vulkan RHI not available"));
	return false;
#endif
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
