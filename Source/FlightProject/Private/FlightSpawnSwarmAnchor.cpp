#include "FlightSpawnSwarmAnchor.h"

#include "FlightDataSubsystem.h"
#include "FlightNavGraphDataHubSubsystem.h"

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

void AFlightSpawnSwarmAnchor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshAnchor(/*bApplyOverrides=*/false);
}

void AFlightSpawnSwarmAnchor::BeginPlay()
{
    Super::BeginPlay();
    RefreshAnchor(/*bApplyOverrides=*/true);
}

void AFlightSpawnSwarmAnchor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterNavGraphNode();
    Super::EndPlay(EndPlayReason);
}

FName AFlightSpawnSwarmAnchor::GetAnchorId() const
{
    return AnchorId.IsNone() ? GetFName() : AnchorId;
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
    Descriptor.NetworkId = NavNetworkId.IsNone() ? GetAnchorId() : NavNetworkId;
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
