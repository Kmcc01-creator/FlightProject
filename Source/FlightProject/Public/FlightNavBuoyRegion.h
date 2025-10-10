#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightDataTypes.h"
#include "FlightNavBuoyRegion.generated.h"

class UFlightSpatialLayoutSourceComponent;

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
};
