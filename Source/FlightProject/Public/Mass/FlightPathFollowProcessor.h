#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "FlightPathFollowProcessor.generated.h"

/**
 * Mass processor that advances entities along their assigned flight paths.
 *
 * Replaces the Tick/AdvanceAlongPath logic from AFlightAIPawn with a
 * cache-efficient batch operation over all path-following entities.
 *
 * Registered with the Flight.PrePhysics phase (see DefaultMass.ini).
 */
UCLASS()
class FLIGHTPROJECT_API UFlightPathFollowProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlightPathFollowProcessor();

protected:
    //~ UMassProcessor interface
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
    //~ End UMassProcessor interface

private:
    /** Query for entities with path follow and transform fragments */
    FMassEntityQuery EntityQuery;
};

/**
 * Processor that syncs Mass entity transforms to their Actor representations.
 *
 * Only runs on entities that have been assigned an Actor (LOD0 visualization).
 * Registered with Flight.PostPhysics phase.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightTransformSyncProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlightTransformSyncProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
