// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Spatial/FlightSpatialSubsystem.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "Core/FlightSpatialField.h"
#include "Core/FlightWindField.h"
#include "Spatial/FlightGpuPerceptionField.h"
#include "Core/FlightMassOptics.h"

using namespace Flight::Spatial;
using namespace Flight::MassOptics;

// ============================================================================
// UMassSpatialForceProcessor Implementation
// ============================================================================

UMassSpatialForceProcessor::UMassSpatialForceProcessor()
{
	// We want to run after movement has updated positions
	ExecutionOrder.ExecuteAfter.Add(TEXT("Movement"));
}

void UMassSpatialForceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// In UE 5.7, we must initialize the query with the manager before adding requirements.
	// This sets bInitialized = true.
	EntityQuery.Initialize(EntityManager);

	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassSpatialQueryFragment>(EMassFragmentAccess::ReadWrite);

	// Finalize query by caching matching archetypes
	EntityQuery.CacheArchetypes();
}

void UMassSpatialForceProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	if (!World) return;

	UFlightSpatialSubsystem* SpatialSubsystem = World->GetSubsystem<UFlightSpatialSubsystem>();
	if (!SpatialSubsystem) return;

	// Execute query
	EntityQuery.ForEachEntityChunk(Context, [SpatialSubsystem](FMassExecutionContext& ChunkContext)
	{
		auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		auto SpatialQueries = ChunkContext.GetMutableFragmentView<FMassSpatialQueryFragment>();
		float DeltaTime = ChunkContext.GetDeltaTimeSeconds();
		
		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			const FVector& Pos = Transforms[i].GetTransform().GetLocation();
			FMassSpatialQueryFragment& SpatialQuery = SpatialQueries[i];

			// 1. Synchronous Aggregation (Wind, Gravity, etc.)
			FVector NewForce = SpatialSubsystem->GetAggregateForce(Pos, SpatialQuery.FieldLayerMask);

			// 2. Reactive Update
			SpatialQuery.CurrentResult = NewForce;
			
			SpatialQuery.LastUpdateTime = DeltaTime;
		}
	});
}

// ============================================================================
// UFlightSpatialSubsystem Implementation
// ============================================================================

void UFlightSpatialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Register a default procedural wind field for testing
	RegisterField(MakeShared<FProceduralWindField>(FVector(1, 0, 0), 500.0f, 0.01f));

	// Register the GPU perception field
	RegisterField(MakeShared<FGpuPerceptionField>(GetWorld()));

	UE_LOG(LogTemp, Log, TEXT("FlightSpatialSubsystem initialized with default Wind and GPU Perception fields"));
}

void UFlightSpatialSubsystem::Deinitialize()
{
	Fields.Empty();
	Super::Deinitialize();
}
