using UnrealBuildTool;

public class SwarmEncounter : ModuleRules
{
	public SwarmEncounter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Use C++23 standard (consistent with FlightProject)
		CppStandard = CppStandardVersion.Cpp23;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MassEntity",
				"MassSpawner",
				"MassCommon",
				"MassMovement",
				"MassActors",
				"MassSimulation",
				"MassSignals",
				"StructUtils",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"ModularGameplay",
				"GameFeatures",
				"FlightProject" // Depend on main module for Types/Fragments if needed
				// ... add private dependencies that you statically link with here ...
			}
			);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
