#include "FlightSpawnSwarmAnchor.h"

#include "FlightDataSubsystem.h"

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
}
