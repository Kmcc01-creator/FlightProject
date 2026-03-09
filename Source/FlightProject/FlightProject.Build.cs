using UnrealBuildTool;

public class FlightProject : ModuleRules
{
    public FlightProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Use C++23 standard for modern language features.
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
            "MassCommon",
            "MassSignals",
            "MassNavigation",
            "MassMovement",
            "MassLOD",
            "MassActors",
            "MassSpawner",
            "MassSimulation",
            "StateTreeModule",
            "ComputeFramework",
            "DeveloperSettings",
            // Geometry/Modeling support for MeshIR system
            "GeometryCore",
            "GeometryFramework",
            "GeometryScriptingCore",
            // Slate UI
            "Slate",
            "SlateCore",
            // Verse VM and Compiler integration
            "VerseCompiler",
            "uLangCore",
            "uLangJSON"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "RenderCore",
            "Renderer",
            "RHI",
            "Projects",
            "Json",
            "JsonUtilities",
            "FlightGpuCompute"  // Compute shaders with PostConfigInit registration
        });

        // Linux-specific io_uring and Vulkan integration
        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PrivateDependencyModuleNames.Add("VulkanRHI");
            PublicDefinitions.Add("VULKAN_RHI_AVAILABLE=1");
            // Vulkan functions loaded dynamically at runtime via dlopen
        }
        bUseUnity = false;
    }
}
