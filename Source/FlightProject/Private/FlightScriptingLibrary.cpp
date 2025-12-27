#include "FlightScriptingLibrary.h"

#include "FlightProject.h"
#include "FlightDataSubsystem.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "FlightSpatialLayoutDirector.h"
#include "FlightProjectDeveloperSettings.h"

#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/FlightMassFragments.h"

#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"

// ============================================================================
// Data Subsystem
// ============================================================================

void UFlightScriptingLibrary::ReloadDataConfigs(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: ReloadDataConfigs called with null context"));
        return;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: No GameInstance available"));
        return;
    }

    UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    if (DataSubsystem)
    {
        DataSubsystem->ReloadAllConfigs();
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Data configs reloaded"));
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightDataSubsystem not found"));
    }
}

void UFlightScriptingLibrary::ReloadCSVConfigs(const UObject* WorldContextObject)
{
    // Legacy name - forwards to new function
    ReloadDataConfigs(WorldContextObject);
}

bool UFlightScriptingLibrary::ReloadCSVConfig(const UObject* WorldContextObject, const FString& ConfigName)
{
    if (!WorldContextObject)
    {
        return false;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        return false;
    }

    UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    if (DataSubsystem)
    {
        return DataSubsystem->ReloadConfig(ConfigName);
    }

    return false;
}

bool UFlightScriptingLibrary::IsDataFullyLoaded(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return false;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        return false;
    }

    const UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    return DataSubsystem ? DataSubsystem->IsFullyLoaded() : false;
}

// ============================================================================
// Bootstrap
// ============================================================================

void UFlightScriptingLibrary::RunBootstrap(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    UFlightWorldBootstrapSubsystem* Bootstrap = World->GetSubsystem<UFlightWorldBootstrapSubsystem>();
    if (Bootstrap)
    {
        Bootstrap->RunBootstrap();
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Bootstrap completed"));
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightWorldBootstrapSubsystem not found"));
    }
}

int32 UFlightScriptingLibrary::SpawnInitialSwarm(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return 0;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return 0;
    }

    // SwarmSpawnerSubsystem is in the SwarmEncounter plugin - find it dynamically
    // This avoids a hard dependency on the GameFeatures plugin
    static UClass* SwarmSpawnerClass = nullptr;
    if (!SwarmSpawnerClass)
    {
        SwarmSpawnerClass = FindObject<UClass>(nullptr, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"));
    }

    if (!SwarmSpawnerClass)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: SwarmEncounter plugin not loaded"));
        return 0;
    }

    UWorldSubsystem* Subsystem = World->GetSubsystemBase(SwarmSpawnerClass);
    if (!Subsystem)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightSwarmSpawnerSubsystem not available"));
        return 0;
    }

    // Call SpawnInitialSwarm via reflection
    UFunction* SpawnFunc = SwarmSpawnerClass->FindFunctionByName(TEXT("SpawnInitialSwarm"));
    if (SpawnFunc)
    {
        Subsystem->ProcessEvent(SpawnFunc, nullptr);
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: SpawnInitialSwarm called"));
        // Return 1 to indicate success - the spawner logs actual count
        return 1;
    }

    UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: SpawnInitialSwarm function not found"));
    return 0;
}

// ============================================================================
// Spatial Layout
// ============================================================================

void UFlightScriptingLibrary::RebuildSpatialLayout(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    // Find the spatial layout director in the world
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(World, AFlightSpatialLayoutDirector::StaticClass(), FoundActors);

    if (FoundActors.Num() > 0)
    {
        AFlightSpatialLayoutDirector* Director = Cast<AFlightSpatialLayoutDirector>(FoundActors[0]);
        if (Director)
        {
            Director->RebuildLayout();
            UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Spatial layout rebuilt"));
        }
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: No FlightSpatialLayoutDirector found in world"));
    }
}

int32 UFlightScriptingLibrary::GetSpatialLayoutRowCount(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return 0;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        return 0;
    }

    const UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    if (DataSubsystem)
    {
        return DataSubsystem->GetSpatialLayout().Num();
    }

    return 0;
}

// ============================================================================
// Mass Entity
// ============================================================================

int32 UFlightScriptingLibrary::GetSwarmEntityCount(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return 0;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return 0;
    }

    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
    if (!MassSubsystem)
    {
        return 0;
    }

    // Query entities with the swarm member tag
    FMassEntityQuery Query;
    Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);

    // UE 5.7: CacheArchetypes and GetNumMatchingEntities no longer need EntityManager param
    Query.CacheArchetypes();

    return Query.GetNumMatchingEntities();
}

void UFlightScriptingLibrary::ClearAllSwarmEntities(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    // Try the swarm spawner's DestroySwarm first (cleaner)
    static UClass* SwarmSpawnerClass = FindObject<UClass>(nullptr, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"));
    if (SwarmSpawnerClass)
    {
        UWorldSubsystem* Subsystem = World->GetSubsystemBase(SwarmSpawnerClass);
        if (Subsystem)
        {
            UFunction* DestroyFunc = SwarmSpawnerClass->FindFunctionByName(TEXT("DestroySwarm"));
            if (DestroyFunc)
            {
                Subsystem->ProcessEvent(DestroyFunc, nullptr);
                UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Swarm destroyed via spawner"));
                return;
            }
        }
    }

    // Fallback: destroy entities directly via Mass
    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
    if (!MassSubsystem)
    {
        return;
    }

    FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

    FMassEntityQuery Query;
    Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);

    // UE 5.7: Use GetMatchingEntityHandles to collect all matching entities
    TArray<FMassEntityHandle> EntitiesToDestroy = Query.GetMatchingEntityHandles();

    for (const FMassEntityHandle& Entity : EntitiesToDestroy)
    {
        EntityManager.DestroyEntity(Entity);
    }

    UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Destroyed %d swarm entities"), EntitiesToDestroy.Num());
}

// ============================================================================
// Validation
// ============================================================================

TArray<FString> UFlightScriptingLibrary::ValidateDataAssets()
{
    TArray<FString> Issues;

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        Issues.Add(TEXT("FlightProjectDeveloperSettings not found"));
        return Issues;
    }

    // Check Data Table configurations
    if (Settings->LightingConfigTable.IsNull())
    {
        Issues.Add(TEXT("LightingConfigTable is not configured"));
    }
    else if (!Settings->LightingConfigTable.LoadSynchronous())
    {
        Issues.Add(TEXT("LightingConfigTable failed to load"));
    }

    if (Settings->AutopilotConfigTable.IsNull())
    {
        Issues.Add(TEXT("AutopilotConfigTable is not configured"));
    }
    else if (!Settings->AutopilotConfigTable.LoadSynchronous())
    {
        Issues.Add(TEXT("AutopilotConfigTable failed to load"));
    }

    if (Settings->SpatialLayoutTable.IsNull())
    {
        Issues.Add(TEXT("SpatialLayoutTable is not configured"));
    }
    else if (!Settings->SpatialLayoutTable.LoadSynchronous())
    {
        Issues.Add(TEXT("SpatialLayoutTable failed to load"));
    }

    // ProceduralAnchorTable is optional
    if (!Settings->ProceduralAnchorTable.IsNull())
    {
        if (!Settings->ProceduralAnchorTable.LoadSynchronous())
        {
            Issues.Add(TEXT("ProceduralAnchorTable configured but failed to load"));
        }
    }

    if (Issues.Num() == 0)
    {
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: All data assets validated successfully"));
    }
    else
    {
        for (const FString& Issue : Issues)
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: Validation issue: %s"), *Issue);
        }
    }

    return Issues;
}

TArray<FString> UFlightScriptingLibrary::GetConfiguredDataTablePaths()
{
    TArray<FString> Paths;

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (Settings)
    {
        if (!Settings->LightingConfigTable.IsNull())
        {
            Paths.Add(Settings->LightingConfigTable.ToString());
        }
        if (!Settings->AutopilotConfigTable.IsNull())
        {
            Paths.Add(Settings->AutopilotConfigTable.ToString());
        }
        if (!Settings->SpatialLayoutTable.IsNull())
        {
            Paths.Add(Settings->SpatialLayoutTable.ToString());
        }
        if (!Settings->ProceduralAnchorTable.IsNull())
        {
            Paths.Add(Settings->ProceduralAnchorTable.ToString());
        }
    }

    return Paths;
}

TArray<FString> UFlightScriptingLibrary::GetConfiguredCSVPaths()
{
    // Legacy - now returns Data Table paths
    return GetConfiguredDataTablePaths();
}
