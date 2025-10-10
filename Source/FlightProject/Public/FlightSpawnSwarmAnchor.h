#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightDataTypes.h"
#include "FlightNavGraphTypes.h"
#include "FlightSpawnSwarmAnchor.generated.h"

/**
 * Editor-placeable actor that declares a swarm spawn request for autonomous drones.
 * The game mode queries these anchors at startup to determine drone counts and phase offsets.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightSpawnSwarmAnchor : public AActor
{
    GENERATED_BODY()

public:
    AFlightSpawnSwarmAnchor();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    FName GetAnchorId() const;

    int32 GetDroneCount() const { return EffectiveDroneCount; }
    float GetPhaseOffsetDeg() const { return EffectivePhaseOffsetDeg; }
    float GetPhaseSpreadDeg() const { return EffectivePhaseSpreadDeg; }
    float GetAutopilotSpeedOverride() const { return EffectiveAutopilotSpeed > 0.f ? EffectiveAutopilotSpeed : -1.f; }

protected:
    UPROPERTY(VisibleAnywhere, Category = "Components")
    USceneComponent* Root;

    /** Optional identifier used for CSV overrides. Defaults to the actor name. */
    UPROPERTY(EditAnywhere, Category = "Swarm")
    FName AnchorId;

    /** Desired number of drones spawned for this swarm anchor. */
    UPROPERTY(EditAnywhere, Category = "Swarm", meta = (ClampMin = "0"))
    int32 DroneCount = 6;

    /** Starting phase along the autopilot path (degrees), mapped to 0..1. */
    UPROPERTY(EditAnywhere, Category = "Swarm")
    float PhaseOffsetDeg = 0.f;

    /** Spread (degrees) the drones will cover along the path. */
    UPROPERTY(EditAnywhere, Category = "Swarm")
    float PhaseSpreadDeg = 360.f;

    /** Optional override for drone autopilot speed (cm/s). */
    UPROPERTY(EditAnywhere, Category = "Swarm", meta = (ClampMin = "-1.0"))
    float AutopilotSpeedOverride = -1.f;

    /** Register this anchor as a node in the nav graph data hub for visualization/debug. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    bool bRegisterWithNavGraph = true;

    /** Macro network identifier used when registering the anchor node. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    FName NavNetworkId = NAME_None;

    /** Additional semantic tags applied to the nav graph node. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    TArray<FName> NavGraphTags;

private:
    int32 EffectiveDroneCount = 0;
    float EffectivePhaseOffsetDeg = 0.f;
    float EffectivePhaseSpreadDeg = 360.f;
    float EffectiveAutopilotSpeed = -1.f;
    FGuid RegisteredNavNodeId;

    void RefreshAnchor(bool bApplyOverrides);
    void ApplyOverrides(const FFlightProceduralAnchorRow* OverrideRow);
    void SyncNavGraphNode();
    void UnregisterNavGraphNode();
};
