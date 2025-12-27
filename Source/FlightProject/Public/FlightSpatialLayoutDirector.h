#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightSpatialLayoutDirector.generated.h"

class AFlightSpatialTestEntity;
struct FFlightSpatialLayoutRow;

/**
 * Spawns layout entities described in the spatial layout CSV to bootstrap a free-form navigation test level.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightSpatialLayoutDirector : public AActor
{
    GENERATED_BODY()

public:
    AFlightSpatialLayoutDirector();

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    UPROPERTY(EditAnywhere, Category = "Flight|Layout")
    bool bRespawnOnConstruction = true;

    UPROPERTY(EditAnywhere, Category = "Flight|Layout")
    TSubclassOf<AFlightSpatialTestEntity> NavProbeClass;

    UPROPERTY(EditAnywhere, Category = "Flight|Layout")
    TSubclassOf<AFlightSpatialTestEntity> ObstacleClass;

    UPROPERTY(EditAnywhere, Category = "Flight|Layout")
    TSubclassOf<AFlightSpatialTestEntity> LandmarkClass;

    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<AActor>> SpawnedActors;

public:
    void RebuildLayout();

private:
    void DestroySpawnedActors();
    void SpawnLayoutRow(const FFlightSpatialLayoutRow& Row);
    TSubclassOf<AFlightSpatialTestEntity> ResolveClassForType(const FFlightSpatialLayoutRow& Row) const;
};
