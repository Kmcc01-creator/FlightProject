using UnrealBuildTool;

public class FlightProject : ModuleRules
{
    public FlightProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
            "DeveloperSettings"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "RenderCore",
            "RHI",
            "Projects"
        });

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
