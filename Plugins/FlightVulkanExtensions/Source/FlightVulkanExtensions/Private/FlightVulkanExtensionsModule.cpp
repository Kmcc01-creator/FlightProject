// Copyright Kelly Rey Wilson. All Rights Reserved.
// Registers Vulkan extensions for io_uring GPU sync integration
//
// CRITICAL: This module must load at PostConfigInit phase (before RHI creation)
// to successfully register extensions via IVulkanDynamicRHI::AddEnabledDeviceExtensionsAndLayers

#include "FlightVulkanExtensionsModule.h"
#include "Modules/ModuleManager.h"

#if WITH_VULKAN_EXTENSIONS
#include "IVulkanDynamicRHI.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightVulkanExt, Log, All);

bool FFlightVulkanExtensionsModule::bExtensionRequested = false;

void FFlightVulkanExtensionsModule::StartupModule()
{
#if WITH_VULKAN_EXTENSIONS
	UE_LOG(LogFlightVulkanExt, Log, TEXT("FlightVulkanExtensions: Registering Vulkan extensions for io_uring integration..."));

	// These must be registered BEFORE VulkanRHI creates the device
	// PostConfigInit loading phase ensures we run before RHI initialization

	// Required extensions for exporting timeline semaphores as sync_fd
	static const ANSICHAR* DeviceExtensions[] = {
		"VK_KHR_external_semaphore",      // Base external semaphore support
		"VK_KHR_external_semaphore_fd",   // Linux fd export for semaphores
		// Note: VK_KHR_timeline_semaphore is already enabled by UE 5.7
	};

	// Instance extensions (needed for physical device queries)
	static const ANSICHAR* InstanceExtensions[] = {
		"VK_KHR_external_semaphore_capabilities",
	};

	// Register instance extensions first
	IVulkanDynamicRHI::AddEnabledInstanceExtensionsAndLayers(
		TArrayView<const ANSICHAR* const>(InstanceExtensions, UE_ARRAY_COUNT(InstanceExtensions)),
		TArrayView<const ANSICHAR* const>()  // No extra layers
	);

	// Register device extensions
	IVulkanDynamicRHI::AddEnabledDeviceExtensionsAndLayers(
		TArrayView<const ANSICHAR* const>(DeviceExtensions, UE_ARRAY_COUNT(DeviceExtensions)),
		TArrayView<const ANSICHAR* const>()  // No extra layers
	);

	bExtensionRequested = true;

	UE_LOG(LogFlightVulkanExt, Log,
		TEXT("FlightVulkanExtensions: Registered %d device extensions, %d instance extensions"),
		UE_ARRAY_COUNT(DeviceExtensions), UE_ARRAY_COUNT(InstanceExtensions));

#else
	UE_LOG(LogFlightVulkanExt, Warning,
		TEXT("FlightVulkanExtensions: Vulkan extensions not available on this platform"));
#endif
}

void FFlightVulkanExtensionsModule::ShutdownModule()
{
	UE_LOG(LogFlightVulkanExt, Log, TEXT("FlightVulkanExtensions: Module shutdown"));
}

IMPLEMENT_MODULE(FFlightVulkanExtensionsModule, FlightVulkanExtensions)
