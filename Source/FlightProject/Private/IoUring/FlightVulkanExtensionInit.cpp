// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Register Vulkan extensions for io_uring integration

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_LINUX && defined(VULKAN_RHI_AVAILABLE)

#include "IVulkanDynamicRHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightVulkanInit, Log, All);

/**
 * Register external fence fd extension before Vulkan RHI initializes.
 *
 * This uses FCoreDelegates::OnPostEngineInit to register extensions
 * before the first frame but after module loading.
 *
 * Note: For very early init, you'd need a PreInit module.
 * This approach is simpler but may miss the window on some platforms.
 */
class FFlightVulkanExtensionRegistrar
{
public:
	FFlightVulkanExtensionRegistrar()
	{
		// Try to register extensions as early as possible
		// This may be too late if RHI already initialized
		TryRegisterExtensions();
	}

	void TryRegisterExtensions()
	{
		// Check if we can still register (before RHI init)
		// GDynamicRHI being null means RHI hasn't initialized yet
		if (GDynamicRHI != nullptr)
		{
			UE_LOG(LogFlightVulkanInit, Warning,
				TEXT("RHI already initialized - cannot register extensions"));
			UE_LOG(LogFlightVulkanInit, Warning,
				TEXT("External fence fd may not be available. Add -vulkanextension in command line."));
			return;
		}

		// Extensions we need for io_uring integration
		static const ANSICHAR* DeviceExtensions[] = {
			"VK_KHR_external_fence",
			"VK_KHR_external_fence_fd",
		};

		// Try to register via IVulkanDynamicRHI
		// This function is only valid before RHI creation
		IVulkanDynamicRHI::AddEnabledDeviceExtensionsAndLayers(
			TArrayView<const ANSICHAR* const>(DeviceExtensions, UE_ARRAY_COUNT(DeviceExtensions)),
			TArrayView<const ANSICHAR* const>()  // No layers
		);

		UE_LOG(LogFlightVulkanInit, Log,
			TEXT("Registered Vulkan extensions: VK_KHR_external_fence, VK_KHR_external_fence_fd"));
	}
};

// Static instance - constructed before main()
// This gives us the best chance of registering before RHI init
static FFlightVulkanExtensionRegistrar GFlightVulkanExtensionRegistrar;

#endif // PLATFORM_LINUX && VULKAN_RHI_AVAILABLE
