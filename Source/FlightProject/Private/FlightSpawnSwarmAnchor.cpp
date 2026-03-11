#include "FlightSpawnSwarmAnchor.h"

#include "FlightDataSubsystem.h"
#include "FlightNavGraphDataHubSubsystem.h"
#include "Navigation/FlightNavigationContracts.h"
#include "Swarm/FlightSwarmAdapterContracts.h"

#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AFlightSpawnSwarmAnchor::AFlightSpawnSwarmAnchor()
{
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    EffectiveDroneCount = DroneCount;
    EffectivePhaseOffsetDeg = PhaseOffsetDeg;
    EffectivePhaseSpreadDeg = PhaseSpreadDeg;
    EffectiveAutopilotSpeed = AutopilotSpeedOverride;
    EffectivePreferredBehaviorId = PreferredBehaviorId;
    EffectiveAllowedBehaviorIds = AllowedBehaviorIds;
    EffectiveDeniedBehaviorIds = DeniedBehaviorIds;
    EffectiveRequiredBehaviorContracts = RequiredBehaviorContracts;

    NavGraphTags.AddUnique(TEXT("SpawnSwarmAnchor"));
}

void AFlightSpawnSwarmAnchor::OnAdapterConstruction(const FTransform& Transform)
{
    RefreshAnchor(/*bApplyOverrides=*/false);
}

void AFlightSpawnSwarmAnchor::OnAdapterRegistered()
{
    RefreshAnchor(/*bApplyOverrides=*/true);
}

void AFlightSpawnSwarmAnchor::OnAdapterUnregistered(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterNavGraphNode();
}

FName AFlightSpawnSwarmAnchor::GetAnchorId() const
{
    return AnchorId.IsNone() ? GetFName() : AnchorId;
}

FName AFlightSpawnSwarmAnchor::GetNavNetworkId() const
{
    return NavNetworkId.IsNone() ? GetAnchorId() : NavNetworkId;
}

bool AFlightSpawnSwarmAnchor::BuildParticipantRecord(Flight::Orchestration::FFlightParticipantRecord& OutRecord) const
{
    OutRecord = {};
    OutRecord.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
    OutRecord.Name = GetAnchorId().IsNone() ? GetFName() : GetAnchorId();
    OutRecord.OwnerSubsystem = TEXT("UFlightSwarmSpawnerSubsystem");
    OutRecord.SourceObject = const_cast<AFlightSpawnSwarmAnchor*>(this);
    OutRecord.SourceObjectPath = GetPathName();
    OutRecord.Tags.Add(TEXT("WorldActor"));
    OutRecord.Tags.Add(TEXT("Swarm"));
    OutRecord.Capabilities.Add(TEXT("SpawnSwarm"));
    OutRecord.Capabilities.Add(TEXT("NavigationIntentSource"));
    OutRecord.ContractKeys.Add(Flight::Navigation::Contracts::IntentKey);
    return true;
}

void AFlightSpawnSwarmAnchor::GetSchemaProviderDescriptors(TArray<Flight::Adapters::FFlightSchemaProviderDescriptor>& OutDescriptors) const
{
    Flight::Adapters::FFlightSchemaProviderDescriptor Descriptor;
    Descriptor.RuntimeTypeKey = Flight::Reflection::GetRuntimeTypeKey<Flight::Swarm::AdapterContracts::FFlightSpawnAnchorBatchContract>();
    Descriptor.TypeName = TEXT("Flight::Swarm::AdapterContracts::FFlightSpawnAnchorBatchContract");
    Descriptor.ExpectedCapability = Flight::Reflection::EVexCapability::VexCapableAuto;
    Descriptor.ContractKeys.Add(Flight::Swarm::AdapterContracts::SpawnBatchKey);
    Descriptor.bSupportsBatchResolution = true;
    OutDescriptors.Add(MoveTemp(Descriptor));
}

bool AFlightSpawnSwarmAnchor::BuildMassBatchLoweringPlan(Flight::Mass::FFlightMassBatchLoweringPlan& OutPlan) const
{
    OutPlan = {};
    OutPlan.AdapterName = GetAnchorId().IsNone() ? GetFName() : GetAnchorId();
    OutPlan.CohortName = FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *OutPlan.AdapterName.ToString()));
    OutPlan.BatchCount = GetDroneCount();
    OutPlan.PhaseOffsetDeg = GetPhaseOffsetDeg();
    OutPlan.PhaseSpreadDeg = GetPhaseSpreadDeg();
    OutPlan.DesiredSpeed = GetAutopilotSpeedOverride();
    OutPlan.bLooping = true;
    OutPlan.DesiredNavigationNetwork = GetNavNetworkId();
    OutPlan.DesiredNavigationSubNetwork = GetNavSubNetworkId();
    OutPlan.PreferredBehaviorId = GetPreferredBehaviorId();
    OutPlan.RequiredBehaviorContracts = GetRequiredBehaviorContracts();

    for (const int32 AllowedBehaviorId : GetAllowedBehaviorIds())
    {
        if (AllowedBehaviorId >= 0)
        {
            OutPlan.AllowedBehaviorIds.Add(static_cast<uint32>(AllowedBehaviorId));
        }
    }

    for (const int32 DeniedBehaviorId : GetDeniedBehaviorIds())
    {
        if (DeniedBehaviorId >= 0)
        {
            OutPlan.DeniedBehaviorIds.Add(static_cast<uint32>(DeniedBehaviorId));
        }
    }

    TArray<Flight::Adapters::FFlightSchemaProviderDescriptor> Descriptors;
    GetSchemaProviderDescriptors(Descriptors);
    if (!Descriptors.IsEmpty())
    {
        OutPlan.SchemaDescriptor = Descriptors[0];
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
        Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::IntentKey);
        Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::CandidateKey);
        Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::CommitKey);
    }

    return OutPlan.BatchCount >= 0 && OutPlan.SchemaDescriptor.IsValid();
}

void AFlightSpawnSwarmAnchor::RefreshAnchor(bool bApplyOverrides)
{
    EffectiveDroneCount = DroneCount;
    EffectivePhaseOffsetDeg = PhaseOffsetDeg;
    EffectivePhaseSpreadDeg = PhaseSpreadDeg;
    EffectiveAutopilotSpeed = AutopilotSpeedOverride;
    EffectivePreferredBehaviorId = PreferredBehaviorId;
    EffectiveAllowedBehaviorIds = AllowedBehaviorIds;
    EffectiveDeniedBehaviorIds = DeniedBehaviorIds;
    EffectiveRequiredBehaviorContracts = RequiredBehaviorContracts;

    if (bApplyOverrides)
    {
        if (UWorld* World = GetWorld())
        {
            if (UGameInstance* GameInstance = World->GetGameInstance())
            {
                if (UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>())
                {
                    if (const FFlightProceduralAnchorRow* OverrideRow = DataSubsystem->FindProceduralAnchorConfig(GetAnchorId(), EFlightProceduralAnchorType::SpawnSwarm))
                    {
                        ApplyOverrides(OverrideRow);
                    }
                }
            }
        }
    }

    EffectiveDroneCount = FMath::Max(EffectiveDroneCount, 0);
    EffectivePhaseSpreadDeg = FMath::Max(EffectivePhaseSpreadDeg, 0.f);
    SyncNavGraphNode();
}

void AFlightSpawnSwarmAnchor::ApplyOverrides(const FFlightProceduralAnchorRow* OverrideRow)
{
    if (!OverrideRow || OverrideRow->AnchorType != EFlightProceduralAnchorType::SpawnSwarm)
    {
        return;
    }

    if (OverrideRow->SwarmDroneCount >= 0)
    {
        EffectiveDroneCount = OverrideRow->SwarmDroneCount;
    }
    if (!FMath::IsNaN(OverrideRow->SwarmPhaseOffsetDeg))
    {
        EffectivePhaseOffsetDeg = OverrideRow->SwarmPhaseOffsetDeg;
    }
    if (!FMath::IsNaN(OverrideRow->SwarmPhaseSpreadDeg))
    {
        EffectivePhaseSpreadDeg = OverrideRow->SwarmPhaseSpreadDeg;
    }
    if (OverrideRow->SwarmAutopilotSpeed >= 0.f)
    {
        EffectiveAutopilotSpeed = OverrideRow->SwarmAutopilotSpeed;
    }
    if (OverrideRow->SwarmPreferredBehaviorId >= 0)
    {
        EffectivePreferredBehaviorId = OverrideRow->SwarmPreferredBehaviorId;
    }
    if (!OverrideRow->SwarmAllowedBehaviorIds.IsEmpty())
    {
        EffectiveAllowedBehaviorIds = OverrideRow->SwarmAllowedBehaviorIds;
    }
    if (!OverrideRow->SwarmDeniedBehaviorIds.IsEmpty())
    {
        EffectiveDeniedBehaviorIds = OverrideRow->SwarmDeniedBehaviorIds;
    }
    if (!OverrideRow->SwarmRequiredBehaviorContracts.IsEmpty())
    {
        EffectiveRequiredBehaviorContracts = OverrideRow->SwarmRequiredBehaviorContracts;
    }
}

void AFlightSpawnSwarmAnchor::SyncNavGraphNode()
{
    if (!bRegisterWithNavGraph)
    {
        UnregisterNavGraphNode();
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
    if (!NavGraphHub)
    {
        return;
    }

    FFlightNavGraphNodeDescriptor Descriptor;
    Descriptor.NodeId = RegisteredNavNodeId;
    Descriptor.DisplayName = GetAnchorId();
    Descriptor.NetworkId = GetNavNetworkId();
    Descriptor.SubNetworkId = NavSubNetworkId;
    Descriptor.Location = GetActorLocation();
    Descriptor.Tags = NavGraphTags;
    if (!GetAnchorId().IsNone())
    {
        Descriptor.Tags.AddUnique(GetAnchorId());
    }
    Descriptor.Tags.AddUnique(TEXT("SwarmAnchor"));

    RegisteredNavNodeId = NavGraphHub->RegisterNode(Descriptor);
}

void AFlightSpawnSwarmAnchor::UnregisterNavGraphNode()
{
    if (!RegisteredNavNodeId.IsValid())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (World)
    {
        if (UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>())
        {
            NavGraphHub->RemoveNode(RegisteredNavNodeId);
        }
    }

    RegisteredNavNodeId.Invalidate();
}
