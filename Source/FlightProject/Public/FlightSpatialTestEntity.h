#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightDataTypes.h"
#include "FlightSpatialTestEntity.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class USceneComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Simple developer-art actor used to visualize layout CSV entries (nav probes, obstacles, landmarks).
 */
UCLASS()
class FLIGHTPROJECT_API AFlightSpatialTestEntity : public AActor
{
    GENERATED_BODY()

public:
    AFlightSpatialTestEntity();

    void ApplyLayoutRow(const FFlightSpatialLayoutRow& LayoutRow);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Components")
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UStaticMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UPointLightComponent* NavigationLight;

    UPROPERTY(EditAnywhere, Category = "Flight|Debug")
    bool bShowDebugLabel = true;

    UPROPERTY(VisibleAnywhere, Category = "Flight|Debug")
    FFlightSpatialLayoutRow LayoutSnapshot;

    UPROPERTY()
    UStaticMesh* NavProbeMesh;

    UPROPERTY()
    UStaticMesh* ObstacleMesh;

    UPROPERTY()
    UStaticMesh* LandmarkMesh;

    UPROPERTY()
    UMaterialInterface* NavProbeBaseMaterial;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* NavProbeMID;

    bool bIsNavProbe = false;
    float PulseTime = 0.f;

    UPROPERTY(EditAnywhere, Category = "Flight|Visual")
    float NavProbePulseSpeed = 0.6f;

    UPROPERTY(EditAnywhere, Category = "Flight|Visual")
    float NavProbeLightMinIntensity = 4800.f;

    UPROPERTY(EditAnywhere, Category = "Flight|Visual")
    float NavProbeLightMaxIntensity = 8600.f;

    UPROPERTY(EditAnywhere, Category = "Flight|Visual")
    float NavProbeMeshEmissiveScale = 4.0f;

    float NavProbeBaseLightIntensity = 0.f;
    float NavProbeMinIntensityMultiplier = 0.55f;
    float NavProbeMaxIntensityMultiplier = 1.15f;

    void ConfigureMeshForType(EFlightSpatialEntityType EntityType);
    void UpdateDebugLabel();
    void SetupNavProbeVisuals();
    void UpdateNavProbePulse(float DeltaSeconds);
};
