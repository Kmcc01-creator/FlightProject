#include "FlightSwarmSpawnerSubsystem.h"

#include "MassSpawnerSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"
#include "MassExecutor.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityUtils.h"

#include "FlightProject/Public/FlightDataSubsystem.h"
#include "FlightProject/Public/FlightDataTypes.h"
#include "FlightProject/Public/FlightSpawnSwarmAnchor.h"
#include "FlightProject/Public/FlightWaypointPath.h"
#include "FlightProject/Public/Mass/FlightMassFragments.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightSwarmSpawner, Log, All);

namespace
{

const FName DefaultSwarmCohortName(TEXT("Swarm.Default"));

FName MakeAnchorCohortName(const AFlightSpawnSwarmAnchor& Anchor)
{
	const FName AnchorName = Anchor.GetAnchorId().IsNone() ? Anchor.GetFName() : Anchor.GetAnchorId();
	return FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *AnchorName.ToString()));
}

void ApplyBehaviorCohortFragment(FMassEntityManager& EntityManager, const TArray<FMassEntityHandle>& Entities, const FName CohortName)
{
	if (Entities.IsEmpty() || CohortName.IsNone())
	{
		return;
	}

	FFlightBehaviorCohortFragment CohortFragment;
	CohortFragment.CohortName = CohortName;

	const FConstSharedStruct SharedFragment = EntityManager.GetOrCreateConstSharedFragment(CohortFragment);
	FMassArchetypeSharedFragmentValues SharedFragmentValues;
	SharedFragmentValues.Add(SharedFragment);

	TArray<FMassArchetypeEntityCollection> EntityCollections;
	UE::Mass::Utils::CreateEntityCollections(EntityManager, Entities, FMassArchetypeEntityCollection::NoDuplicates, EntityCollections);
	EntityManager.BatchAddSharedFragmentsForEntities(EntityCollections, SharedFragmentValues);
}

} // namespace

void UFlightSwarmSpawnerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UMassSpawnerSubsystem>();
    UE_LOG(LogFlightSwarmSpawner, Verbose, TEXT("FlightSwarmSpawnerSubsystem initialized (Mass-ready)"));
}

void UFlightSwarmSpawnerSubsystem::SpawnInitialSwarm()
{
    const FFlightAutopilotConfigRow* AutopilotConfig = ResolveAutopilotConfig();
    AFlightWaypointPath* WaypointPath = FindOrCreateWaypointPath(AutopilotConfig);

    if (!WaypointPath)
    {
        UE_LOG(LogFlightSwarmSpawner, Error, TEXT("Spawn aborted: Missing WaypointPath"));
        return;
    }

    if (!SwarmEntityConfig)
    {
        SwarmEntityConfig = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/SwarmEncounter/DA_SwarmDroneConfig.DA_SwarmDroneConfig"));
    }

    if (!SwarmEntityConfig)
    {
        UE_LOG(LogFlightSwarmSpawner, Error, TEXT("Spawn aborted: No MassEntityConfigAsset assigned!"));
        return;
    }

    SpawnMassEntities(AutopilotConfig, WaypointPath);
}

void UFlightSwarmSpawnerSubsystem::DestroySwarm()
{
    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem) return;

    // TODO: Implement batch destroy via processor or tag query
    UE_LOG(LogFlightSwarmSpawner, Log, TEXT("DestroySwarm called - (Logic placeholder)"));
}

void UFlightSwarmSpawnerSubsystem::SpawnMassEntities(const FFlightAutopilotConfigRow* AutopilotConfig, AFlightWaypointPath* WaypointPath)
{
    UMassSpawnerSubsystem* MassSpawner = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>();
    UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!MassSpawner || !EntitySubsystem) return;

    const FMassEntityConfig& Config = SwarmEntityConfig->GetConfig();
    const FMassEntityTemplate& Template = Config.GetOrCreateEntityTemplate(*GetWorld());
    FMassEntityTemplateID TemplateID = Template.GetTemplateID();

    const float PathLength = WaypointPath->GetPathLength();
    const FGuid PathId = WaypointPath->GetPathId();
    const float GlobalSpeed = (AutopilotConfig && AutopilotConfig->DroneSpeed > 0.f) ? AutopilotConfig->DroneSpeed : 1500.f;

    int32 TotalSpawned = 0;
    bool bHasAnchors = false;

    // Iterate Anchors and spawn per-group
    for (TActorIterator<AFlightSpawnSwarmAnchor> It(GetWorld()); It; ++It)
    {
        AFlightSpawnSwarmAnchor* Anchor = *It;
        int32 Count = Anchor->GetDroneCount();
        if (Count <= 0) continue;

        bHasAnchors = true;
        TArray<FMassEntityHandle> SpawnedEntities;
        MassSpawner->SpawnEntities(TemplateID, Count, FConstStructView(), nullptr, SpawnedEntities);

        FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
        ApplyBehaviorCohortFragment(EntityManager, SpawnedEntities, MakeAnchorCohortName(*Anchor));

        float PhaseOffset = Anchor->GetPhaseOffsetDeg();
        float PhaseSpread = Anchor->GetPhaseSpreadDeg();
        float AnchorSpeed = Anchor->GetAutopilotSpeedOverride();
        float EffectiveSpeed = (AnchorSpeed > 0.f) ? AnchorSpeed : GlobalSpeed;

        for (int32 i = 0; i < SpawnedEntities.Num(); ++i)
        {
            FMassEntityHandle Entity = SpawnedEntities[i];

            if (FFlightPathFollowFragment* PathFrag = EntityManager.GetFragmentDataPtr<FFlightPathFollowFragment>(Entity))
            {
                // Calculate distance along spline based on Phase
                float NormalizedPhase = FMath::Fmod(PhaseOffset, 360.f) / 360.f;
                if (Count > 1 && PhaseSpread > 0.f)
                {
                    float Step = (PhaseSpread / 360.f) / Count;
                    NormalizedPhase += Step * i;
                }
                
                PathFrag->PathId = PathId;
                PathFrag->CurrentDistance = FMath::Frac(NormalizedPhase) * PathLength;
                PathFrag->DesiredSpeed = EffectiveSpeed;
                PathFrag->bLooping = true; // Always loop for now
            }

            if (FFlightTransformFragment* TransFrag = EntityManager.GetFragmentDataPtr<FFlightTransformFragment>(Entity))
            {
                // Initial placement (Processor will snap to spline next frame, but good to start close)
                TransFrag->Location = WaypointPath->GetActorLocation(); 
            }
        }
        TotalSpawned += Count;
    }

    // Fallback if no anchors exist (use Global Config)
    if (!bHasAnchors && AutopilotConfig)
    {
        int32 Count = AutopilotConfig->DroneCount;
        if (Count > 0)
        {
            TArray<FMassEntityHandle> SpawnedEntities;
            MassSpawner->SpawnEntities(TemplateID, Count, FConstStructView(), nullptr, SpawnedEntities);
            
            FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
            ApplyBehaviorCohortFragment(EntityManager, SpawnedEntities, DefaultSwarmCohortName);
            for (int32 i = 0; i < SpawnedEntities.Num(); ++i)
            {
                FMassEntityHandle Entity = SpawnedEntities[i];
                if (FFlightPathFollowFragment* PathFrag = EntityManager.GetFragmentDataPtr<FFlightPathFollowFragment>(Entity))
                {
                    PathFrag->PathId = PathId;
                    PathFrag->CurrentDistance = (PathLength / Count) * i;
                    PathFrag->DesiredSpeed = GlobalSpeed;
                    PathFrag->bLooping = true;
                }
            }
            TotalSpawned += Count;
        }
    }

    UE_LOG(LogFlightSwarmSpawner, Log, TEXT("Mass Spawned %d entities total from anchors/config"), TotalSpawned);
    SwarmSpawnedEvent.Broadcast(TotalSpawned);
}

const FFlightAutopilotConfigRow* UFlightSwarmSpawnerSubsystem::ResolveAutopilotConfig() const
{
    UWorld* World = GetWorld();
    if (World)
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            if (const UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>())
            {
                return DataSubsystem->GetAutopilotConfig();
            }
        }
    }
    return nullptr;
}

AFlightWaypointPath* UFlightSwarmSpawnerSubsystem::FindOrCreateWaypointPath(const FFlightAutopilotConfigRow* AutopilotConfig)
{
    if (CachedWaypointPath.IsValid()) return CachedWaypointPath.Get();
    
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    for (TActorIterator<AFlightWaypointPath> It(World); It; ++It)
    {
        CachedWaypointPath = *It;
        return *It;
    }
    
    AFlightWaypointPath* Path = World->SpawnActor<AFlightWaypointPath>(AFlightWaypointPath::StaticClass(), FTransform::Identity);
    if (Path) Path->EnsureDefaultLoop();
    
    CachedWaypointPath = Path;
    return Path;
}
