// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Vulkan external fence support for io_uring integration

#include "IoUring/FlightVulkanExternalFence.h"

#if PLATFORM_LINUX

#include "RHI.h"
#include "DynamicRHI.h"

// Include Vulkan headers for types and function pointer typedefs
#include <vulkan/vulkan.h>
#include <unistd.h>
#include <dlfcn.h>

DEFINE_LOG_CATEGORY_STATIC(LogFlightVulkanFence, Log, All);

namespace Flight::IoUring
{

// ============================================================================
// Vulkan Function Loader - loads functions dynamically at runtime
// ============================================================================

struct FVulkanFunctionLoader
{
	// Library handle
	void* VulkanLib = nullptr;

	// Core functions loaded from library
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
	PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;

	// Device functions loaded via vkGetDeviceProcAddr
	PFN_vkCreateFence CreateFence = nullptr;
	PFN_vkDestroyFence DestroyFence = nullptr;
	PFN_vkResetFences ResetFences = nullptr;
	PFN_vkWaitForFences WaitForFences = nullptr;
	PFN_vkGetFenceStatus GetFenceStatus = nullptr;
	PFN_vkGetFenceFdKHR GetFenceFdKHR = nullptr;

	bool bInitialized = false;
	bool bDeviceFunctionsLoaded = false;

	bool Initialize()
	{
		if (bInitialized)
		{
			return VulkanLib != nullptr;
		}

		bInitialized = true;

		// Try to load Vulkan library
		VulkanLib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
		if (!VulkanLib)
		{
			VulkanLib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
		}

		if (!VulkanLib)
		{
			UE_LOG(LogFlightVulkanFence, Warning,
				TEXT("Failed to load Vulkan library: %s"), UTF8_TO_TCHAR(dlerror()));
			return false;
		}

		// Load core functions
		GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			dlsym(VulkanLib, "vkGetInstanceProcAddr"));
		GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
			dlsym(VulkanLib, "vkGetDeviceProcAddr"));

		if (!GetInstanceProcAddr || !GetDeviceProcAddr)
		{
			UE_LOG(LogFlightVulkanFence, Warning,
				TEXT("Failed to load Vulkan proc addr functions"));
			dlclose(VulkanLib);
			VulkanLib = nullptr;
			return false;
		}

		UE_LOG(LogFlightVulkanFence, Log,
			TEXT("Vulkan library loaded successfully"));
		return true;
	}

	bool LoadDeviceFunctions(VkDevice Device)
	{
		if (bDeviceFunctionsLoaded)
		{
			return CreateFence != nullptr;
		}

		if (!VulkanLib || !GetDeviceProcAddr || !Device)
		{
			return false;
		}

		bDeviceFunctionsLoaded = true;

		CreateFence = reinterpret_cast<PFN_vkCreateFence>(
			GetDeviceProcAddr(Device, "vkCreateFence"));
		DestroyFence = reinterpret_cast<PFN_vkDestroyFence>(
			GetDeviceProcAddr(Device, "vkDestroyFence"));
		ResetFences = reinterpret_cast<PFN_vkResetFences>(
			GetDeviceProcAddr(Device, "vkResetFences"));
		WaitForFences = reinterpret_cast<PFN_vkWaitForFences>(
			GetDeviceProcAddr(Device, "vkWaitForFences"));
		GetFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(
			GetDeviceProcAddr(Device, "vkGetFenceStatus"));
		GetFenceFdKHR = reinterpret_cast<PFN_vkGetFenceFdKHR>(
			GetDeviceProcAddr(Device, "vkGetFenceFdKHR"));

		if (!CreateFence || !DestroyFence || !ResetFences ||
			!WaitForFences || !GetFenceStatus)
		{
			UE_LOG(LogFlightVulkanFence, Warning,
				TEXT("Failed to load core Vulkan fence functions"));
			return false;
		}

		if (!GetFenceFdKHR)
		{
			UE_LOG(LogFlightVulkanFence, Warning,
				TEXT("vkGetFenceFdKHR not available - VK_KHR_external_fence_fd may not be enabled"));
		}

		UE_LOG(LogFlightVulkanFence, Log,
			TEXT("Vulkan device functions loaded (GetFenceFdKHR: %s)"),
			GetFenceFdKHR ? TEXT("available") : TEXT("unavailable"));
		return true;
	}

	void Shutdown()
	{
		if (VulkanLib)
		{
			dlclose(VulkanLib);
			VulkanLib = nullptr;
		}
		GetInstanceProcAddr = nullptr;
		GetDeviceProcAddr = nullptr;
		CreateFence = nullptr;
		DestroyFence = nullptr;
		ResetFences = nullptr;
		WaitForFences = nullptr;
		GetFenceStatus = nullptr;
		GetFenceFdKHR = nullptr;
		bInitialized = false;
		bDeviceFunctionsLoaded = false;
	}
};

// Global function loader instance
static FVulkanFunctionLoader GVulkanLoader;

// Static function pointer storage for backwards compatibility
void* FVulkanExternalFence::GetFenceFdKHR = nullptr;
bool FVulkanExternalFence::bFunctionsLoaded = false;

// ============================================================================
// Helper Functions
// ============================================================================

VkDevice GetVulkanDevice()
{
	if (!GDynamicRHI)
	{
		return nullptr;
	}

	// Check if we're running Vulkan RHI
	if (GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::Vulkan)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Not running Vulkan RHI"));
		return nullptr;
	}

	// Use GetNativeDevice to get the VkDevice handle
	void* NativeDevice = GDynamicRHI->RHIGetNativeDevice();
	if (!NativeDevice)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Could not get native Vulkan device"));
		return nullptr;
	}

	return static_cast<VkDevice>(NativeDevice);
}

bool IsExternalFenceFdSupported()
{
	if (!GVulkanLoader.Initialize())
	{
		return false;
	}

	VkDevice Device = GetVulkanDevice();
	if (!Device)
	{
		return false;
	}

	if (!GVulkanLoader.LoadDeviceFunctions(Device))
	{
		return false;
	}

	return GVulkanLoader.GetFenceFdKHR != nullptr;
}

// ============================================================================
// FVulkanExternalFence
// ============================================================================

FVulkanExternalFence::FVulkanExternalFence()
	: Device(nullptr)
	, Fence(nullptr)
	, LastError(0)
{
}

FVulkanExternalFence::~FVulkanExternalFence()
{
	Destroy();
}

FVulkanExternalFence::FVulkanExternalFence(FVulkanExternalFence&& Other) noexcept
	: Device(Other.Device)
	, Fence(Other.Fence)
	, LastError(Other.LastError)
{
	Other.Device = nullptr;
	Other.Fence = nullptr;
}

FVulkanExternalFence& FVulkanExternalFence::operator=(FVulkanExternalFence&& Other) noexcept
{
	if (this != &Other)
	{
		Destroy();
		Device = Other.Device;
		Fence = Other.Fence;
		LastError = Other.LastError;
		Other.Device = nullptr;
		Other.Fence = nullptr;
	}
	return *this;
}

bool FVulkanExternalFence::LoadFunctions(VkDevice InDevice)
{
	if (bFunctionsLoaded)
	{
		return GetFenceFdKHR != nullptr;
	}

	if (!GVulkanLoader.Initialize())
	{
		return false;
	}

	if (!GVulkanLoader.LoadDeviceFunctions(InDevice))
	{
		return false;
	}

	GetFenceFdKHR = reinterpret_cast<void*>(GVulkanLoader.GetFenceFdKHR);
	bFunctionsLoaded = true;
	return GetFenceFdKHR != nullptr;
}

bool FVulkanExternalFence::Initialize(VkDevice InDevice)
{
	if (Fence)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Fence already initialized"));
		return false;
	}

	if (!InDevice)
	{
		UE_LOG(LogFlightVulkanFence, Error, TEXT("Invalid Vulkan device"));
		return false;
	}

	Device = InDevice;

	// Load function pointers
	if (!LoadFunctions(Device))
	{
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("Failed to load external fence functions"));
		return false;
	}

	if (!GVulkanLoader.CreateFence)
	{
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("vkCreateFence not loaded"));
		return false;
	}

	// Create fence with external export capability
	VkExportFenceCreateInfo ExportInfo = {};
	ExportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
	ExportInfo.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

	VkFenceCreateInfo CreateInfo = {};
	CreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	CreateInfo.pNext = &ExportInfo;
	CreateInfo.flags = 0;  // Not signaled initially

	VkResult Result = GVulkanLoader.CreateFence(Device, &CreateInfo, nullptr, &Fence);
	if (Result != VK_SUCCESS)
	{
		LastError = static_cast<int32>(Result);
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("vkCreateFence failed: %d"), Result);
		return false;
	}

	UE_LOG(LogFlightVulkanFence, Verbose,
		TEXT("Created external fence: %p"), Fence);
	return true;
}

bool FVulkanExternalFence::InitializeFromRHI()
{
	VkDevice RHIDevice = GetVulkanDevice();
	if (!RHIDevice)
	{
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("Failed to get Vulkan device from RHI"));
		return false;
	}

	return Initialize(RHIDevice);
}

bool FVulkanExternalFence::ExportToSyncFd(int32& OutSyncFd)
{
	OutSyncFd = -1;

	if (!Fence || !Device)
	{
		UE_LOG(LogFlightVulkanFence, Error, TEXT("Fence not initialized"));
		return false;
	}

	if (!GVulkanLoader.GetFenceFdKHR)
	{
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("vkGetFenceFdKHR not available"));
		return false;
	}

	VkFenceGetFdInfoKHR GetFdInfo = {};
	GetFdInfo.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
	GetFdInfo.fence = Fence;
	GetFdInfo.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

	int Fd = -1;
	VkResult Result = GVulkanLoader.GetFenceFdKHR(Device, &GetFdInfo, &Fd);
	if (Result != VK_SUCCESS)
	{
		LastError = static_cast<int32>(Result);
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("vkGetFenceFdKHR failed: %d"), Result);
		return false;
	}

	OutSyncFd = Fd;
	UE_LOG(LogFlightVulkanFence, Verbose,
		TEXT("Exported fence to sync fd: %d"), Fd);
	return true;
}

bool FVulkanExternalFence::Reset()
{
	if (!Fence || !Device)
	{
		return false;
	}

	if (!GVulkanLoader.ResetFences)
	{
		return false;
	}

	VkResult Result = GVulkanLoader.ResetFences(Device, 1, &Fence);
	if (Result != VK_SUCCESS)
	{
		LastError = static_cast<int32>(Result);
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("vkResetFences failed: %d"), Result);
		return false;
	}

	return true;
}

bool FVulkanExternalFence::Wait(uint64 TimeoutNs)
{
	if (!Fence || !Device)
	{
		return false;
	}

	if (!GVulkanLoader.WaitForFences)
	{
		return false;
	}

	VkResult Result = GVulkanLoader.WaitForFences(Device, 1, &Fence, VK_TRUE, TimeoutNs);
	if (Result == VK_SUCCESS)
	{
		return true;
	}
	else if (Result == VK_TIMEOUT)
	{
		return false;
	}
	else
	{
		LastError = static_cast<int32>(Result);
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("vkWaitForFences failed: %d"), Result);
		return false;
	}
}

bool FVulkanExternalFence::IsSignaled() const
{
	if (!Fence || !Device)
	{
		return false;
	}

	if (!GVulkanLoader.GetFenceStatus)
	{
		return false;
	}

	VkResult Result = GVulkanLoader.GetFenceStatus(Device, Fence);
	return Result == VK_SUCCESS;
}

void FVulkanExternalFence::Destroy()
{
	if (Fence && Device && GVulkanLoader.DestroyFence)
	{
		GVulkanLoader.DestroyFence(Device, Fence, nullptr);
		UE_LOG(LogFlightVulkanFence, Verbose,
			TEXT("Destroyed fence: %p"), Fence);
	}
	Fence = nullptr;
	Device = nullptr;
}

// ============================================================================
// FExternalFencePool
// ============================================================================

FExternalFencePool::FExternalFencePool()
	: Device(nullptr)
{
}

FExternalFencePool::~FExternalFencePool()
{
	Shutdown();
}

bool FExternalFencePool::Initialize(VkDevice InDevice, uint32 PoolSize)
{
	if (Device)
	{
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Pool already initialized"));
		return false;
	}

	Device = InDevice;

	// Pre-allocate fences
	AllFences.Reserve(PoolSize);
	AvailableFences.Reserve(PoolSize);

	for (uint32 i = 0; i < PoolSize; ++i)
	{
		auto Fence = MakeUnique<FVulkanExternalFence>();
		if (!Fence->Initialize(Device))
		{
			UE_LOG(LogFlightVulkanFence, Error,
				TEXT("Failed to create fence %d/%d"), i, PoolSize);
			Shutdown();
			return false;
		}

		AvailableFences.Add(Fence.Get());
		AllFences.Add(MoveTemp(Fence));
	}

	UE_LOG(LogFlightVulkanFence, Log,
		TEXT("External fence pool initialized with %d fences"), PoolSize);
	return true;
}

bool FExternalFencePool::InitializeFromRHI(uint32 PoolSize)
{
	VkDevice RHIDevice = GetVulkanDevice();
	if (!RHIDevice)
	{
		UE_LOG(LogFlightVulkanFence, Error,
			TEXT("Failed to get Vulkan device from RHI"));
		return false;
	}

	return Initialize(RHIDevice, PoolSize);
}

void FExternalFencePool::Shutdown()
{
	FScopeLock ScopeLock(&Lock);

	AvailableFences.Empty();
	AllFences.Empty();
	Device = nullptr;

	UE_LOG(LogFlightVulkanFence, Log, TEXT("External fence pool shutdown"));
}

FVulkanExternalFence* FExternalFencePool::Acquire()
{
	FScopeLock ScopeLock(&Lock);

	if (AvailableFences.Num() == 0)
	{
		// Pool exhausted - could expand here if needed
		UE_LOG(LogFlightVulkanFence, Warning, TEXT("Fence pool exhausted"));
		return nullptr;
	}

	FVulkanExternalFence* Fence = AvailableFences.Pop(EAllowShrinking::No);

	// Reset fence for reuse
	Fence->Reset();

	return Fence;
}

void FExternalFencePool::Release(FVulkanExternalFence* Fence)
{
	if (!Fence)
	{
		return;
	}

	FScopeLock ScopeLock(&Lock);

	// Verify fence belongs to this pool
	bool bOwned = false;
	for (const auto& Owned : AllFences)
	{
		if (Owned.Get() == Fence)
		{
			bOwned = true;
			break;
		}
	}

	if (bOwned)
	{
		AvailableFences.Add(Fence);
	}
	else
	{
		UE_LOG(LogFlightVulkanFence, Warning,
			TEXT("Attempted to release fence not from this pool"));
	}
}

uint32 FExternalFencePool::GetAvailableCount() const
{
	FScopeLock ScopeLock(&const_cast<FExternalFencePool*>(this)->Lock);
	return AvailableFences.Num();
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
