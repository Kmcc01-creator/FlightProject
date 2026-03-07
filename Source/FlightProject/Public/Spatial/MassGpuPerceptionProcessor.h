// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "IoUring/FlightGpuPerceptionSubsystem.h"
#include "MassGpuPerceptionProcessor.generated.h"

/**
 * UMassGpuPerceptionProcessor
 * 
 * High-performance processor that batches FMassSpatialQueryFragment requests
 * and dispatches them to the UFlightGpuPerceptionSubsystem.
 * 
 * Results are applied reactively when the GPU work completes.
 */
UCLASS()
class FLIGHTPROJECT_API UMassGpuPerceptionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassGpuPerceptionProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** Query for entities that need spatial perception */
	FMassEntityQuery EntityQuery;

	/** Throttling: How often to update perception (seconds) */
	float UpdateInterval = 0.1f;
};
