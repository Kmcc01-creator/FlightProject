#pragma once

#include "CoreMinimal.h"
#include "Core/FlightActorAdapter.h"
#include "Core/FlightSchemaProviderAdapter.h"
#include "FlightDataTypes.h"
#include "FlightNavGraphTypes.h"
#include "Mass/FlightMassLoweringAdapter.h"
#include "Orchestration/FlightParticipantAdapter.h"
#include "Swarm/FlightSwarmAdapterContracts.h"
#include "FlightSpawnSwarmAnchor.generated.h"

/**
 * Editor-placeable actor that declares a swarm spawn request for autonomous drones.
 * The game mode queries these anchors at startup to determine drone counts and phase offsets.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightSpawnSwarmAnchor
	: public AFlightActorAdapterBase
	, public IFlightParticipantAdapter
	, public IFlightSchemaProviderAdapter
	, public IFlightMassLoweringAdapter
{
    GENERATED_BODY()

public:
    AFlightSpawnSwarmAnchor();

    FName GetAnchorId() const;

    int32 GetDroneCount() const { return EffectiveDroneCount; }
    float GetPhaseOffsetDeg() const { return EffectivePhaseOffsetDeg; }
    float GetPhaseSpreadDeg() const { return EffectivePhaseSpreadDeg; }
    float GetAutopilotSpeedOverride() const { return EffectiveAutopilotSpeed > 0.f ? EffectiveAutopilotSpeed : -1.f; }
    int32 GetPreferredBehaviorId() const { return EffectivePreferredBehaviorId; }
    const TArray<int32>& GetAllowedBehaviorIds() const { return EffectiveAllowedBehaviorIds; }
    const TArray<int32>& GetDeniedBehaviorIds() const { return EffectiveDeniedBehaviorIds; }
    const TArray<FName>& GetRequiredBehaviorContracts() const { return EffectiveRequiredBehaviorContracts; }
    FName GetNavNetworkId() const;
    FName GetNavSubNetworkId() const { return NavSubNetworkId; }
    virtual bool BuildParticipantRecord(Flight::Orchestration::FFlightParticipantRecord& OutRecord) const override;
    virtual void GetSchemaProviderDescriptors(TArray<Flight::Adapters::FFlightSchemaProviderDescriptor>& OutDescriptors) const override;
    virtual bool BuildMassBatchLoweringPlan(Flight::Mass::FFlightMassBatchLoweringPlan& OutPlan) const override;

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

    /** Preferred behavior ID for this anchor cohort. -1 means no preference. */
    UPROPERTY(EditAnywhere, Category = "Swarm|Behavior", meta = (ClampMin = "-1"))
    int32 PreferredBehaviorId = -1;

    /** Optional allowlist of behavior IDs legal for this anchor cohort. Empty means unrestricted. */
    UPROPERTY(EditAnywhere, Category = "Swarm|Behavior")
    TArray<int32> AllowedBehaviorIds;

    /** Optional denylist of behavior IDs illegal for this anchor cohort. */
    UPROPERTY(EditAnywhere, Category = "Swarm|Behavior")
    TArray<int32> DeniedBehaviorIds;

    /** Optional required contracts that a bound behavior must advertise. */
    UPROPERTY(EditAnywhere, Category = "Swarm|Behavior")
    TArray<FName> RequiredBehaviorContracts;

    /** Register this anchor as a node in the nav graph data hub for visualization/debug. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    bool bRegisterWithNavGraph = true;

    /** Macro network identifier used when registering the anchor node. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    FName NavNetworkId = NAME_None;

    /** Optional subnetwork grouping used for local route constraints. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    FName NavSubNetworkId = NAME_None;

    /** Additional semantic tags applied to the nav graph node. */
    UPROPERTY(EditAnywhere, Category = "Swarm|NavGraph")
    TArray<FName> NavGraphTags;

private:
    int32 EffectiveDroneCount = 0;
    float EffectivePhaseOffsetDeg = 0.f;
    float EffectivePhaseSpreadDeg = 360.f;
    float EffectiveAutopilotSpeed = -1.f;
    int32 EffectivePreferredBehaviorId = -1;
    TArray<int32> EffectiveAllowedBehaviorIds;
    TArray<int32> EffectiveDeniedBehaviorIds;
    TArray<FName> EffectiveRequiredBehaviorContracts;
    FGuid RegisteredNavNodeId;

    virtual void OnAdapterConstruction(const FTransform& Transform) override;
    virtual void OnAdapterRegistered() override;
    virtual void OnAdapterUnregistered(const EEndPlayReason::Type EndPlayReason) override;

    void RefreshAnchor(bool bApplyOverrides);
    void ApplyOverrides(const FFlightProceduralAnchorRow* OverrideRow);
    void SyncNavGraphNode();
    void UnregisterNavGraphNode();
};
