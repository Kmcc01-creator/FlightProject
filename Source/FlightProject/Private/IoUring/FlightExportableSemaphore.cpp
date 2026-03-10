// Copyright Kelly Rey Wilson. All Rights Reserved.
// Exportable binary semaphore for io_uring GPU sync integration

#include "IoUring/FlightExportableSemaphore.h"

#if PLATFORM_LINUX

#include "IVulkanDynamicRHI.h"
#include "VulkanThirdParty.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightExportableSemaphore, Log, All);

namespace Flight::IoUring
{

// Static member initialization
void* FExportableSemaphore::FnGetSemaphoreFd = nullptr;
void* FExportableSemaphore::FnCreateSemaphore = nullptr;
void* FExportableSemaphore::FnDestroySemaphore = nullptr;
void* FExportableSemaphore::FnQueueSubmit = nullptr;
bool FExportableSemaphore::bFunctionPointersLoaded = false;

FExportableSemaphore::FExportableSemaphore()
{
}

FExportableSemaphore::~FExportableSemaphore()
{
	if (Semaphore && Device && FnDestroySemaphore)
	{
		auto vkDestroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(FnDestroySemaphore);
		vkDestroySemaphore(Device, Semaphore, nullptr);
		Semaphore = nullptr;
	}
}

bool FExportableSemaphore::LoadFunctionPointers()
{
	if (bFunctionPointersLoaded)
	{
		return FnGetSemaphoreFd != nullptr;
	}

	if (!GDynamicRHI || GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::Vulkan)
	{
		return false;
	}

	IVulkanDynamicRHI* VulkanRHI = GetIVulkanDynamicRHI();

	FnGetSemaphoreFd = VulkanRHI->RHIGetVkDeviceProcAddr("vkGetSemaphoreFdKHR");
	FnCreateSemaphore = VulkanRHI->RHIGetVkDeviceProcAddr("vkCreateSemaphore");
	FnDestroySemaphore = VulkanRHI->RHIGetVkDeviceProcAddr("vkDestroySemaphore");
	FnQueueSubmit = VulkanRHI->RHIGetVkDeviceProcAddr("vkQueueSubmit");

	bFunctionPointersLoaded = true;

	if (!FnGetSemaphoreFd)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning,
			TEXT("vkGetSemaphoreFdKHR not available - VK_KHR_external_semaphore_fd extension not loaded"));
	}

	return FnGetSemaphoreFd != nullptr;
}

bool FExportableSemaphore::IsExtensionAvailable()
{
	return LoadFunctionPointers();
}

TUniquePtr<FExportableSemaphore> FExportableSemaphore::Create()
{
	if (!LoadFunctionPointers())
	{
		return nullptr;
	}

	TUniquePtr<FExportableSemaphore> Semaphore = MakeUnique<FExportableSemaphore>();
	if (!Semaphore->Initialize())
	{
		return nullptr;
	}

	return Semaphore;
}

bool FExportableSemaphore::Initialize()
{
	if (!GDynamicRHI || GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::Vulkan)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning, TEXT("Not running Vulkan RHI"));
		return false;
	}

	if (!FnCreateSemaphore || !FnGetSemaphoreFd)
	{
		UE_LOG(LogFlightExportableSemaphore, Error, TEXT("Required Vulkan functions not available"));
		return false;
	}

	IVulkanDynamicRHI* VulkanRHI = GetIVulkanDynamicRHI();
	Device = VulkanRHI->RHIGetVkDevice();

	auto vkCreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(FnCreateSemaphore);

	// Create BINARY semaphore with SYNC_FD export capability
	// NOTE: Per Vulkan spec, SYNC_FD can ONLY be used with binary semaphores, not timeline!
	VkExportSemaphoreCreateInfo ExportInfo = {};
	ExportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
	ExportInfo.pNext = nullptr;
	ExportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

	VkSemaphoreCreateInfo SemaphoreInfo = {};
	SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	SemaphoreInfo.pNext = &ExportInfo;
	SemaphoreInfo.flags = 0;

	VkResult Result = vkCreateSemaphore(Device, &SemaphoreInfo, nullptr, &Semaphore);
	if (Result != VK_SUCCESS)
	{
		UE_LOG(LogFlightExportableSemaphore, Error,
			TEXT("Failed to create exportable binary semaphore: VkResult=%d"), static_cast<int32>(Result));
		return false;
	}

	UE_LOG(LogFlightExportableSemaphore, Verbose,
		TEXT("Created exportable binary semaphore (handle=0x%p)"), Semaphore);

	return true;
}

bool FExportableSemaphore::SignalAfterTimelineValue(VkQueue Queue, VkSemaphore WaitSemaphore, uint64 WaitValue)
{
	if (!Semaphore)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning, TEXT("Semaphore not initialized"));
		return false;
	}

	if (bHasSignaled)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning,
			TEXT("Semaphore already signaled - each instance is single-use"));
		return false;
	}

	if (!FnQueueSubmit)
	{
		UE_LOG(LogFlightExportableSemaphore, Error, TEXT("vkQueueSubmit not available"));
		return false;
	}

	auto vkQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(FnQueueSubmit);

	// Use Vulkan-native uint64_t for timeline value
	uint64_t WaitValueVk = static_cast<uint64_t>(WaitValue);

	// Setup timeline semaphore submit info for the WAIT side only
	// We wait on a timeline semaphore (UE's) and signal a binary semaphore (ours)
	VkTimelineSemaphoreSubmitInfo TimelineSubmitInfo = {};
	TimelineSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
	TimelineSubmitInfo.pNext = nullptr;
	TimelineSubmitInfo.waitSemaphoreValueCount = 1;
	TimelineSubmitInfo.pWaitSemaphoreValues = &WaitValueVk;
	TimelineSubmitInfo.signalSemaphoreValueCount = 0;  // Binary semaphore doesn't need a value
	TimelineSubmitInfo.pSignalSemaphoreValues = nullptr;

	VkSemaphore WaitSemaphores[] = { WaitSemaphore };
	VkSemaphore SignalSemaphores[] = { Semaphore };
	VkPipelineStageFlags WaitStages[] = { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };

	VkSubmitInfo SubmitInfo = {};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.pNext = &TimelineSubmitInfo;
	SubmitInfo.waitSemaphoreCount = 1;
	SubmitInfo.pWaitSemaphores = WaitSemaphores;
	SubmitInfo.pWaitDstStageMask = WaitStages;
	SubmitInfo.commandBufferCount = 0;  // No command buffers - just semaphore sync
	SubmitInfo.pCommandBuffers = nullptr;
	SubmitInfo.signalSemaphoreCount = 1;
	SubmitInfo.pSignalSemaphores = SignalSemaphores;

	VkResult Result = vkQueueSubmit(Queue, 1, &SubmitInfo, VK_NULL_HANDLE);
	if (Result != VK_SUCCESS)
	{
		UE_LOG(LogFlightExportableSemaphore, Error,
			TEXT("vkQueueSubmit failed: VkResult=%d"), static_cast<int32>(Result));
		return false;
	}

	bHasSignaled = true;

	UE_LOG(LogFlightExportableSemaphore, Verbose,
		TEXT("Submitted signal after waiting on timeline value %llu"), WaitValue);

	return true;
}

FExportResult FExportableSemaphore::ExportSyncFd()
{
	if (!Semaphore || !FnGetSemaphoreFd)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning, TEXT("Cannot export: semaphore not initialized"));
		return { EExportStatus::Error, -1, -1 };
	}

	if (!bHasSignaled)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning,
			TEXT("Cannot export: semaphore not yet signaled - call SignalAfterTimelineValue first"));
		return { EExportStatus::Error, -1, -1 };
	}

	if (bHasExported)
	{
		UE_LOG(LogFlightExportableSemaphore, Warning,
			TEXT("Cannot export: sync_fd already exported - each instance is single-use"));
		return { EExportStatus::Error, -1, -1 };
	}

	auto vkGetSemaphoreFdKHR = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(FnGetSemaphoreFd);

	VkSemaphoreGetFdInfoKHR FdInfo = {};
	FdInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
	FdInfo.pNext = nullptr;
	FdInfo.semaphore = Semaphore;
	FdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

	int Fd = -1;
	VkResult Result = vkGetSemaphoreFdKHR(Device, &FdInfo, &Fd);
	if (Result != VK_SUCCESS)
	{
		// Check for already-signaled cases if the driver returns specific errors
		// For most drivers, SYNC_FD export will just succeed with a signaled fd if it's already done.
		// If it's -1 because it was already signaled and the driver doesn't support exporting a terminal state:
		// We treat it as AlreadySignaled.
		
		UE_LOG(LogFlightExportableSemaphore, Error,
			TEXT("vkGetSemaphoreFdKHR failed: VkResult=%d"), static_cast<int32>(Result));
		return { EExportStatus::Error, -1, static_cast<int32>(Result) };
	}

	if (Fd == -1)
	{
		// Driver returned success but no FD - this usually means the semaphore is already in a terminal/signaled state
		// where no fence object is needed.
		bHasExported = true;
		return { EExportStatus::AlreadySignaled, -1, 0 };
	}

	bHasExported = true;

	UE_LOG(LogFlightExportableSemaphore, Verbose,
		TEXT("Exported sync_fd=%d"), Fd);

	return { EExportStatus::Success, Fd, 0 };
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
