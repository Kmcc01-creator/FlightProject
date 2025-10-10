#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightDataTypes.h"
#include "FlightNavGraphTypes.h"
#include "FlightNavBuoyRegion.generated.h"

class UFlightSpatialLayoutSourceComponent;
class UFlightNavGraphDataHubSubsystem;

/**
 * Designer-placeable actor that spawns a ring of navigation buoys relative to its transform.
 * Supports optional overrides from CSV-driven procedural anchor configuration.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightNavBuoyRegion : public AActor
{
    GENERATED_BODY()

public:
    AFlightNavBuoyRegion();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    FName GetAnchorId() const;

protected:
    UPROPERTY(VisibleAnywhere, Category = "Components")
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UFlightSpatialLayoutSourceComponent* LayoutSource;

    /** Optional identifier used to look up CSV overrides. Defaults to the actor name. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy")
    FName AnchorId;

    /** Number of buoys to generate around the anchor. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy", meta = (ClampMin = "1"))
    int32 BuoyCount = 6;

    /** Radius (in centimeters) for the buoy ring relative to the actor origin. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy", meta = (ClampMin = "0.0"))
    float Radius = 500.f;

    /** Height offset above the actor origin for the buoys (centimeters). */
    UPROPERTY(EditAnywhere, Category = "NavBuoy")
    float HeightOffset = 0.f;

    /** Optional initial azimuth offset for the first buoy (degrees). */
    UPROPERTY(EditAnywhere, Category = "NavBuoy")
    float AzimuthOffsetDeg = 0.f;

    /** Register generated nodes with the nav graph data hub for visualization/debug. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy|NavGraph")
    bool bRegisterWithNavGraph = true;

    /** Identifier for the macro network the generated buoys belong to. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy|NavGraph")
    FName NavNetworkId = NAME_None;

    /** Optional subnetwork grouping to help downstream routing. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy|NavGraph")
    FName NavSubNetworkId = NAME_None;

    /** When true, creates loop edges between consecutive buoys for quick visualization. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy|NavGraph")
    bool bCreateLoopEdges = true;

    /** Additional nav graph tags applied to each generated node. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy|NavGraph")
    TArray<FName> NavGraphTags;

    /** Base light intensity when no override is supplied. */
    UPROPERTY(EditAnywhere, Category = "NavBuoy|Light", meta = (ClampMin = "0.0"))
    float LightIntensity = 7500.f;

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Light", meta = (ClampMin = "0.0"))
    float LightRadius = 1600.f;

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Light")
    float LightHeightOffset = 280.f;

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Light")
    FLinearColor LightColor = FLinearColor(0.3f, 0.1f, 0.06f, 1.f);

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Visual", meta = (ClampMin = "0.01"))
    float PulseSpeed = 0.6f;

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Visual", meta = (ClampMin = "0.01"))
    float EmissiveScale = 4.f;

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Visual", meta = (ClampMin = "0.0"))
    float LightMinMultiplier = 0.55f;

    UPROPERTY(EditAnywhere, Category = "NavBuoy|Visual", meta = (ClampMin = "0.0"))
    float LightMaxMultiplier = 1.15f;

private:
    void RefreshLayout(bool bApplyOverrides);
    void BuildBuoyRows(const FFlightProceduralAnchorRow* OverrideRow, TArray<FFlightSpatialLayoutRow>& OutRows) const;
    void ApplyOverride(const FFlightProceduralAnchorRow* OverrideRow, int32& OutCount, float& OutRadius, float& OutHeight, float& OutAzimuth,
        float& OutLightIntensity, float& OutLightRadiusValue, float& OutLightHeight, FLinearColor& OutLightColor,
        float& OutPulseSpeed, float& OutEmissiveScale, float& OutMinMultiplier, float& OutMaxMultiplier) const;
    void SyncNavGraph(const TArray<FFlightSpatialLayoutRow>& Rows);
    void UnregisterNavGraphEntries();
    void RegisterLoopEdges(const TArray<FGuid>& NodeIds, const TArray<FVector>& NodeLocations, class UFlightNavGraphDataHubSubsystem& Hub);

    TArray<FGuid> RegisteredNodeIds;
    TArray<FGuid> RegisteredEdgeIds;
};
