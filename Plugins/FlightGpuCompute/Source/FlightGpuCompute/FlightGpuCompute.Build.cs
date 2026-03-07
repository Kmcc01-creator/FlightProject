// Copyright Kelly Rey Wilson. All Rights Reserved.

using UnrealBuildTool;

public class FlightGpuCompute : ModuleRules
{
	public FlightGpuCompute(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Use C++23 standard to match main project
		CppStandard = CppStandardVersion.Cpp23;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"Renderer",
			"RHI",
			"Projects"  // For IPluginManager (shader directory mapping)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore"
		});

		// Enable RDG compute shader support - PUBLIC so dependents can see it
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicDefinitions.Add("WITH_FLIGHT_COMPUTE_SHADERS=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_FLIGHT_COMPUTE_SHADERS=0");
		}
	}
}
