#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityConfigAsset.h"
#include "FlightSwarmSpawnerSubsystem.generated.h"

class AFlightWaypointPath;
struct FFlightAutopilotConfigRow;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFlightSwarmSpawned, int32 /*Count*/);

/**
 * Orchestrates autonomous drone spawning using Mass Entity framework.
 */
UCLASS()
class SWARMENCOUNTER_API UFlightSwarmSpawnerSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Spawns the swarm using the Mass Entity Config. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Swarm")
    void SpawnInitialSwarm();

    /** Destroys all Mass entities tagged as swarm members. */
    void DestroySwarm();

    FOnFlightSwarmSpawned& OnSwarmSpawned() { return SwarmSpawnedEvent; }

protected:
    /** The Mass Config defining the drone (Traits: Transform, PathFollow, Visual). */
    UPROPERTY(EditAnywhere, Category = "Swarm Config")
    TObjectPtr<UMassEntityConfigAsset> SwarmEntityConfig;

private:
    const FFlightAutopilotConfigRow* ResolveAutopilotConfig() const;
    AFlightWaypointPath* FindOrCreateWaypointPath(const FFlightAutopilotConfigRow* AutopilotConfig);

    /** Batch spawns entities via MassSpawnerSubsystem */
    void SpawnMassEntities(const FFlightAutopilotConfigRow* Config, AFlightWaypointPath* Path);

    TWeakObjectPtr<AFlightWaypointPath> CachedWaypointPath;
    FOnFlightSwarmSpawned SwarmSpawnedEvent;
};
