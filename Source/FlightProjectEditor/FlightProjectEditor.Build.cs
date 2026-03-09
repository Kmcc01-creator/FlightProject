using UnrealBuildTool;

public class FlightProjectEditor : ModuleRules
{
    public FlightProjectEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "FlightProject",
            "InputCore",
            "Slate",
            "SlateCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "FlightGpuCompute",
            "UnrealEd",
            "LevelEditor",
            "ToolMenus",
            "WorkspaceMenuStructure"
        });

        bUseUnity = false;
    }
}
