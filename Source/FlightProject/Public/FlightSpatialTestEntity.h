#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightDataTypes.h"
#include "FlightSpatialTestEntity.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class USceneComponent;

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

    void ConfigureMeshForType(EFlightSpatialEntityType EntityType);
    void UpdateDebugLabel();
};
