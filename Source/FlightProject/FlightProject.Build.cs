using UnrealBuildTool;

public class FlightProject : ModuleRules
{
    public FlightProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Use C++23 standard for modern language features
        CppStandard = CppStandardVersion.Cpp23;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "GameplayTasks",
            "NavigationSystem",
            "Niagara",
            "Chaos",
            "PhysicsCore",
            "MassEntity",
            "MassActors",
            "MassSpawner",
            "MassSimulation",
            "StateTreeModule",
            "ComputeFramework",
            "DeveloperSettings",
            // Geometry/Modeling support for MeshIR system
            "GeometryCore",
            "GeometryFramework",
            "GeometryScriptingCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "RenderCore",
            "RHI",
            "Projects"
        });

        // Linux-specific io_uring and Vulkan integration
        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PrivateDependencyModuleNames.Add("VulkanRHI");
            PublicDefinitions.Add("VULKAN_RHI_AVAILABLE=1");
            // Vulkan functions loaded dynamically at runtime via dlopen
        }

        if (Target.Type == TargetRules.TargetType.Editor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "LevelEditor",
                "NiagaraEditor",
                "MassEntityEditor",
                "WorldPartitionEditor"
            });
        }

        bUseUnity = false;
    }
}
