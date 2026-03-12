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
#include "FlightProject/Public/Core/FlightReflectionDebug.h"
#include "FlightProject/Public/Mass/FlightMassLoweringAdapter.h"
#include "FlightProject/Public/Mass/FlightMassFragments.h"
#include "FlightProject/Public/Mass/FlightWaypointPathRegistry.h"
#include "FlightProject/Public/Navigation/FlightNavigationCommitProduct.h"
#include "FlightProject/Public/Orchestration/FlightOrchestrationSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightSwarmSpawner, Log, All);

namespace
{

const FName DefaultSwarmCohortName(TEXT("Swarm.Default"));

struct FResolvedAnchorBatchPlan
{
	AFlightSpawnSwarmAnchor* Anchor = nullptr;
	Flight::Mass::FFlightMassBatchLoweringPlan Plan;
};

struct FResolvedSpawnPath
{
	AFlightWaypointPath* Path = nullptr;
	FGuid PathId;
	float PathLength = 0.0f;
	FVector InitialLocation = FVector::ZeroVector;
	bool bResolvedFromExecutionPlan = false;
};

const TCHAR* BoolText(const bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
}

FName MakeAnchorCohortName(const AFlightSpawnSwarmAnchor& Anchor)
{
	const FName AnchorName = Anchor.GetAnchorId().IsNone() ? Anchor.GetFName() : Anchor.GetAnchorId();
	return FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *AnchorName.ToString()));
}

void RemoveConstSharedFragmentFromEntities(
	FMassEntityManager& EntityManager,
	const TArray<FMassEntityHandle>& Entities,
	const UScriptStruct& FragmentType)
{
	for (const FMassEntityHandle& Entity : Entities)
	{
		EntityManager.RemoveConstSharedFragmentFromEntity(Entity, FragmentType);
	}
}

void ApplyBehaviorCohortFragment(FMassEntityManager& EntityManager, const TArray<FMassEntityHandle>& Entities, const FName CohortName)
{
	if (Entities.IsEmpty() || CohortName.IsNone())
	{
		return;
	}

	RemoveConstSharedFragmentFromEntities(EntityManager, Entities, *FFlightBehaviorCohortFragment::StaticStruct());

	FFlightBehaviorCohortFragment CohortFragment;
	CohortFragment.CohortName = CohortName;

	const FConstSharedStruct SharedFragment = EntityManager.GetOrCreateConstSharedFragment(CohortFragment);
	FMassArchetypeSharedFragmentValues SharedFragmentValues;
	SharedFragmentValues.Add(SharedFragment);

	TArray<FMassArchetypeEntityCollection> EntityCollections;
	UE::Mass::Utils::CreateEntityCollections(EntityManager, Entities, FMassArchetypeEntityCollection::NoDuplicates, EntityCollections);
	EntityManager.BatchAddSharedFragmentsForEntities(EntityCollections, SharedFragmentValues);
}

void ApplySharedFragmentPlan(
	FMassEntityManager& EntityManager,
	const TArray<FMassEntityHandle>& Entities,
	const Flight::Mass::FFlightMassSharedFragmentPlan& SharedPlan)
{
	if (SharedPlan.bHasBehaviorCohort)
	{
		ApplyBehaviorCohortFragment(EntityManager, Entities, SharedPlan.BehaviorCohort.CohortName);
	}
}

void ApplyNavigationCommitSharedFragment(
	FMassEntityManager& EntityManager,
	const TArray<FMassEntityHandle>& Entities,
	const Flight::Navigation::FFlightNavigationCommitProduct& CommitProduct)
{
	if (Entities.IsEmpty() || !CommitProduct.IsValid())
	{
		return;
	}

	RemoveConstSharedFragmentFromEntities(EntityManager, Entities, *FFlightNavigationCommitSharedFragment::StaticStruct());

	FFlightNavigationCommitSharedFragment CommitFragment;
	CommitProduct.WriteSharedFragment(CommitFragment);

	const FConstSharedStruct SharedFragment = EntityManager.GetOrCreateConstSharedFragment(CommitFragment);
	FMassArchetypeSharedFragmentValues SharedFragmentValues;
	SharedFragmentValues.Add(SharedFragment);

	TArray<FMassArchetypeEntityCollection> EntityCollections;
	UE::Mass::Utils::CreateEntityCollections(EntityManager, Entities, FMassArchetypeEntityCollection::NoDuplicates, EntityCollections);
	EntityManager.BatchAddSharedFragmentsForEntities(EntityCollections, SharedFragmentValues);
}

float ComputeNormalizedBatchPhase(const Flight::Mass::FFlightMassBatchLoweringPlan& Plan, const int32 EntityIndex)
{
	float NormalizedPhase = FMath::Fmod(Plan.PhaseOffsetDeg, 360.0f) / 360.0f;
	if (Plan.BatchCount > 1 && Plan.PhaseSpreadDeg > 0.0f)
	{
		const float Step = (Plan.PhaseSpreadDeg / 360.0f) / static_cast<float>(Plan.BatchCount);
		NormalizedPhase += Step * static_cast<float>(EntityIndex);
	}

	return FMath::Frac(NormalizedPhase);
}

bool BuildLegacyAnchorPlan(
	const AFlightSpawnSwarmAnchor& Anchor,
	Flight::Mass::FFlightMassBatchLoweringPlan& OutPlan)
{
	OutPlan = {};
	const FName AnchorName = Anchor.GetAnchorId().IsNone() ? Anchor.GetFName() : Anchor.GetAnchorId();
	OutPlan.AdapterName = AnchorName;
	OutPlan.CohortName = FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *AnchorName.ToString()));
	OutPlan.BatchCount = Anchor.GetDroneCount();
	OutPlan.PhaseOffsetDeg = Anchor.GetPhaseOffsetDeg();
	OutPlan.PhaseSpreadDeg = Anchor.GetPhaseSpreadDeg();
	OutPlan.DesiredSpeed = Anchor.GetAutopilotSpeedOverride();
	OutPlan.bLooping = true;
	OutPlan.DesiredNavigationNetwork = Anchor.GetNavNetworkId();
	OutPlan.DesiredNavigationSubNetwork = Anchor.GetNavSubNetworkId();
	OutPlan.PreferredBehaviorId = Anchor.GetPreferredBehaviorId();
	OutPlan.RequiredBehaviorContracts = Anchor.GetRequiredBehaviorContracts();

	for (const int32 AllowedBehaviorId : Anchor.GetAllowedBehaviorIds())
	{
		if (AllowedBehaviorId >= 0)
		{
			OutPlan.AllowedBehaviorIds.Add(static_cast<uint32>(AllowedBehaviorId));
		}
	}

	for (const int32 DeniedBehaviorId : Anchor.GetDeniedBehaviorIds())
	{
		if (DeniedBehaviorId >= 0)
		{
			OutPlan.DeniedBehaviorIds.Add(static_cast<uint32>(DeniedBehaviorId));
		}
	}

	OutPlan.SharedFragments.bHasBehaviorCohort = !OutPlan.CohortName.IsNone();
	OutPlan.SharedFragments.BehaviorCohort.CohortName = OutPlan.CohortName;

	OutPlan.Orchestration.bHasCohortRecord = !OutPlan.CohortName.IsNone();
	if (OutPlan.Orchestration.bHasCohortRecord)
	{
		Flight::Orchestration::FFlightCohortRecord& Cohort = OutPlan.Orchestration.Cohort;
		Cohort.Name = OutPlan.CohortName;
		Cohort.Tags.Add(TEXT("Swarm"));
		Cohort.Tags.Add(TEXT("AnchorScoped"));
		Cohort.DesiredNavigationNetwork = OutPlan.DesiredNavigationNetwork;
		Cohort.DesiredNavigationSubNetwork = OutPlan.DesiredNavigationSubNetwork;
		Cohort.PreferredBehaviorId = OutPlan.PreferredBehaviorId;
		Cohort.AllowedBehaviorIds = OutPlan.AllowedBehaviorIds;
		Cohort.DeniedBehaviorIds = OutPlan.DeniedBehaviorIds;
		Cohort.RequiredBehaviorContracts = OutPlan.RequiredBehaviorContracts;
		Cohort.RequiredNavigationContracts.AddUnique(TEXT("Navigation.Intent"));
		Cohort.RequiredNavigationContracts.AddUnique(TEXT("Navigation.Candidate"));
		Cohort.RequiredNavigationContracts.AddUnique(TEXT("Navigation.Commit"));
	}

	return OutPlan.BatchCount >= 0;
}

bool ResolveAnchorBatchLoweringPlan(
	const AFlightSpawnSwarmAnchor& Anchor,
	Flight::Mass::FFlightMassBatchLoweringPlan& OutPlan)
{
	if (const IFlightMassLoweringAdapter* Adapter = Cast<IFlightMassLoweringAdapter>(&Anchor))
	{
		if (Adapter->BuildMassBatchLoweringPlan(OutPlan))
		{
			return true;
		}
	}

	return BuildLegacyAnchorPlan(Anchor, OutPlan);
}

const Flight::Orchestration::FFlightExecutionPlanStep* FindExecutionPlanStepForCohort(
	const UFlightOrchestrationSubsystem& Orchestration,
	const FName CohortName)
{
	return Orchestration.GetExecutionPlan().Steps.FindByPredicate(
		[CohortName](const Flight::Orchestration::FFlightExecutionPlanStep& Step)
		{
			return Step.CohortName == CohortName;
		});
}

FResolvedSpawnPath MakeResolvedSpawnPath(const Flight::Navigation::FFlightNavigationCommitProduct& Product)
{
	FResolvedSpawnPath Result;
	Result.Path = Product.Path;
	Result.PathId = Product.Identity.RuntimePathId;
	Result.PathLength = Product.PathLength;
	Result.InitialLocation = Product.InitialLocation;
	Result.bResolvedFromExecutionPlan = Product.Report.bResolvedFromExecutionPlan;
	return Result;
}

Flight::Navigation::FFlightNavigationCommitProduct ResolveCommitProductForBatchPlan(
	const UFlightOrchestrationSubsystem* Orchestration,
	const Flight::Navigation::FFlightNavigationCommitResolverContext& CommitContext,
	UFlightWaypointPathRegistry* PathRegistry,
	const Flight::Mass::FFlightMassBatchLoweringPlan& BatchPlan,
	AFlightWaypointPath* DefaultPath)
{
	if (Orchestration && !BatchPlan.CohortName.IsNone())
	{
		if (const Flight::Orchestration::FFlightExecutionPlanStep* Step =
			FindExecutionPlanStepForCohort(*Orchestration, BatchPlan.CohortName))
		{
			return Flight::Navigation::ResolveNavigationCommitProductForStep(
				*Step,
				Orchestration->GetReport(),
				CommitContext,
				PathRegistry,
				DefaultPath);
		}
	}

	Flight::Orchestration::FFlightExecutionPlanStep FallbackStep;
	FallbackStep.CohortName = BatchPlan.CohortName;
	return Flight::Navigation::ResolveNavigationCommitProductForStep(
		FallbackStep,
		Flight::Orchestration::FFlightOrchestrationReport(),
		CommitContext,
		PathRegistry,
		DefaultPath);
}

FResolvedSpawnPath ResolveSpawnPathForBatchPlan(
	const UFlightOrchestrationSubsystem* Orchestration,
	const Flight::Navigation::FFlightNavigationCommitResolverContext& CommitContext,
	UFlightWaypointPathRegistry* PathRegistry,
	const Flight::Mass::FFlightMassBatchLoweringPlan& BatchPlan,
	AFlightWaypointPath* DefaultPath)
{
	return MakeResolvedSpawnPath(
		ResolveCommitProductForBatchPlan(
			Orchestration,
			CommitContext,
			PathRegistry,
			BatchPlan,
			DefaultPath));
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
    UFlightOrchestrationSubsystem* Orchestration = GetWorld()->GetSubsystem<UFlightOrchestrationSubsystem>();
    UFlightWaypointPathRegistry* PathRegistry = GetWorld()->GetSubsystem<UFlightWaypointPathRegistry>();

    const FMassEntityConfig& Config = SwarmEntityConfig->GetConfig();
    const FMassEntityTemplate& Template = Config.GetOrCreateEntityTemplate(*GetWorld());
    FMassEntityTemplateID TemplateID = Template.GetTemplateID();

    const float GlobalSpeed = (AutopilotConfig && AutopilotConfig->DroneSpeed > 0.f) ? AutopilotConfig->DroneSpeed : 1500.f;

    int32 TotalSpawned = 0;
    bool bHasAnchors = false;
    TArray<FResolvedAnchorBatchPlan> ResolvedAnchorPlans;

    // Iterate Anchors and spawn per-group
    for (TActorIterator<AFlightSpawnSwarmAnchor> It(GetWorld()); It; ++It)
    {
        AFlightSpawnSwarmAnchor* Anchor = *It;
        if (!Anchor)
        {
            continue;
        }

        Flight::Mass::FFlightMassBatchLoweringPlan BatchPlan;
        if (!ResolveAnchorBatchLoweringPlan(*Anchor, BatchPlan))
        {
            UE_LOG(LogFlightSwarmSpawner, Warning, TEXT("Skipping swarm anchor '%s': failed to build a Mass batch lowering plan."),
                *Anchor->GetName());
            continue;
        }

        const int32 Count = BatchPlan.BatchCount;
        if (Count <= 0)
        {
            continue;
        }

        FResolvedAnchorBatchPlan& ResolvedPlan = ResolvedAnchorPlans.AddDefaulted_GetRef();
        ResolvedPlan.Anchor = Anchor;
        ResolvedPlan.Plan = MoveTemp(BatchPlan);
    }

    if (Orchestration)
    {
        TArray<Flight::Mass::FFlightMassBatchLoweringPlan> CohortPlans;
        CohortPlans.Reserve(ResolvedAnchorPlans.Num());
        for (const FResolvedAnchorBatchPlan& ResolvedPlan : ResolvedAnchorPlans)
        {
            CohortPlans.Add(ResolvedPlan.Plan);
        }

        const bool bReconciled = Orchestration->ReconcileBatchLoweringPlans(CohortPlans);
        UE_LOG(
            LogFlightSwarmSpawner,
            Verbose,
            TEXT("SwarmSpawner reconciled %d batch cohort plans through orchestration (changed=%s)."),
            CohortPlans.Num(),
            bReconciled ? TEXT("true") : TEXT("false"));
    }

    Flight::Navigation::FFlightNavigationCommitResolverContext CommitContext;
    CommitContext.BuildFromWorld(*GetWorld());

    for (const FResolvedAnchorBatchPlan& ResolvedPlan : ResolvedAnchorPlans)
    {
        const Flight::Mass::FFlightMassBatchLoweringPlan& BatchPlan = ResolvedPlan.Plan;
        const int32 Count = BatchPlan.BatchCount;
        const Flight::Navigation::FFlightNavigationCommitProduct CommitProduct =
            ResolveCommitProductForBatchPlan(Orchestration, CommitContext, PathRegistry, BatchPlan, WaypointPath);
        const FResolvedSpawnPath SpawnPath =
            ResolveSpawnPathForBatchPlan(Orchestration, CommitContext, PathRegistry, BatchPlan, WaypointPath);
        if (!SpawnPath.PathId.IsValid())
        {
            UE_LOG(
                LogFlightSwarmSpawner,
                Warning,
                TEXT("Skipping cohort '%s': no waypoint path could be resolved for spawn commitment."),
                *BatchPlan.CohortName.ToString());
            continue;
        }

        bHasAnchors = true;
		TArray<FMassEntityHandle> SpawnedEntities;
		MassSpawner->SpawnEntities(TemplateID, Count, FConstStructView(), nullptr, SpawnedEntities);

        FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
        ApplySharedFragmentPlan(EntityManager, SpawnedEntities, BatchPlan.SharedFragments);
        ApplyNavigationCommitSharedFragment(EntityManager, SpawnedEntities, CommitProduct);

		const FFlightBehaviorCohortFragment* AppliedCohortFragment = SpawnedEntities.IsEmpty()
			? nullptr
			: EntityManager.GetConstSharedFragmentDataPtr<FFlightBehaviorCohortFragment>(SpawnedEntities[0]);
		const FFlightNavigationCommitSharedFragment* AppliedCommitFragment = SpawnedEntities.IsEmpty()
			? nullptr
			: EntityManager.GetConstSharedFragmentDataPtr<FFlightNavigationCommitSharedFragment>(SpawnedEntities[0]);
		UE_LOG(
			LogFlightSwarmSpawner,
			Verbose,
			TEXT("Spawn cohort '%s': commitProduct={valid=%s report=%s} applied={cohort=%s commitPresent=%s identity=%s} spawnPath={path=%s fromExecutionPlan=%s}."),
			*BatchPlan.CohortName.ToString(),
			BoolText(CommitProduct.IsValid()),
			*Flight::Reflection::Debug::DumpNativeStructFieldsToString(
				*FFlightNavigationCommitReport::StaticStruct(),
				&CommitProduct.Report),
			AppliedCohortFragment ? *AppliedCohortFragment->CohortName.ToString() : TEXT("<missing>"),
			BoolText(AppliedCommitFragment != nullptr),
			AppliedCommitFragment
				? *Flight::Reflection::Debug::DumpNativeStructFieldsToString(
					*FFlightNavigationCommitIdentity::StaticStruct(),
					&AppliedCommitFragment->Identity)
				: TEXT("<missing>"),
			*SpawnPath.PathId.ToString(EGuidFormats::DigitsWithHyphensLower),
			BoolText(SpawnPath.bResolvedFromExecutionPlan));

        const float EffectiveSpeed = (BatchPlan.DesiredSpeed > 0.0f) ? BatchPlan.DesiredSpeed : GlobalSpeed;

        for (int32 i = 0; i < SpawnedEntities.Num(); ++i)
        {
            FMassEntityHandle Entity = SpawnedEntities[i];

            if (FFlightPathFollowFragment* PathFrag = EntityManager.GetFragmentDataPtr<FFlightPathFollowFragment>(Entity))
            {
                const float NormalizedPhase = ComputeNormalizedBatchPhase(BatchPlan, i);
                PathFrag->PathId = SpawnPath.PathId;
                PathFrag->CurrentDistance = NormalizedPhase * SpawnPath.PathLength;
                PathFrag->DesiredSpeed = EffectiveSpeed;
                PathFrag->bLooping = BatchPlan.bLooping;
            }

            if (FFlightTransformFragment* TransFrag = EntityManager.GetFragmentDataPtr<FFlightTransformFragment>(Entity))
            {
                // Initial placement (Processor will snap to spline next frame, but good to start close)
                TransFrag->Location = SpawnPath.InitialLocation;
            }
        }

        UE_LOG(
            LogFlightSwarmSpawner,
            Verbose,
            TEXT("Spawn cohort '%s' committed path '%s' (fromExecutionPlan=%s)."),
            *BatchPlan.CohortName.ToString(),
            SpawnPath.Path ? *SpawnPath.Path->GetName() : *SpawnPath.PathId.ToString(EGuidFormats::DigitsWithHyphensLower),
            SpawnPath.bResolvedFromExecutionPlan ? TEXT("true") : TEXT("false"));
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
            const float PathLength = WaypointPath->GetPathLength();
            const FGuid PathId = WaypointPath->GetPathId();
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
