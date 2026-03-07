// Copyright Kelly Rey Wilson. All Rights Reserved.

using UnrealBuildTool;

public class FlightVulkanExtensions : ModuleRules
{
	public FlightVulkanExtensions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// VulkanRHI for IVulkanDynamicRHI - conditionally available
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PrivateDependencyModuleNames.Add("VulkanRHI");
			PrivateDefinitions.Add("WITH_VULKAN_EXTENSIONS=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_VULKAN_EXTENSIONS=0");
		}
	}
}
