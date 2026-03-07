// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Spatial/MassGpuPerceptionProcessor.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassEntitySubsystem.h"
#include "Core/FlightSpatialField.h"
#include "Engine/World.h"

using namespace Flight::Spatial;

UMassGpuPerceptionProcessor::UMassGpuPerceptionProcessor()
{
	// We want to run after movement
	ExecutionOrder.ExecuteAfter.Add(TEXT("Movement"));
}

void UMassGpuPerceptionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// In UE 5.7, we must initialize the query with the manager before adding requirements.
	// This sets bInitialized = true.
	EntityQuery.Initialize(EntityManager);

	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassSpatialQueryFragment>(EMassFragmentAccess::ReadWrite);

	// Finalize query by caching matching archetypes
	EntityQuery.CacheArchetypes();
}

void UMassGpuPerceptionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	if (!World) return;

	UFlightGpuPerceptionSubsystem* PerceptionSubsystem = World->GetSubsystem<UFlightGpuPerceptionSubsystem>();
	if (!PerceptionSubsystem || !PerceptionSubsystem->IsAvailable()) return;

	float CurrentTime = World->GetTimeSeconds();

	// Prepare batch
	TSharedPtr<FFlightPerceptionRequest> Request = MakeShared<FFlightPerceptionRequest>();
	TSharedPtr<TArray<FMassEntityHandle>> BatchEntities = MakeShared<TArray<FMassEntityHandle>>();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
	{
		auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		auto SpatialQueries = ChunkContext.GetMutableFragmentView<FMassSpatialQueryFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			FMassSpatialQueryFragment& SpatialQuery = SpatialQueries[i];

			// Throttle updates
			if (CurrentTime - SpatialQuery.LastUpdateTime < UpdateInterval)
			{
				continue;
			}

			// Only process if interested in Occlusion layer (GPU Perception)
			if (!(SpatialQuery.FieldLayerMask & 0x2))
			{
				continue;
			}

			// Add to batch
			Request->EntityPositions.Add(FVector4f((FVector3f)Transforms[i].GetTransform().GetLocation(), 1000.0f));
			BatchEntities->Add(ChunkContext.GetEntity(i));

			SpatialQuery.LastUpdateTime = CurrentTime;
		}
	});

	if (Request->EntityPositions.Num() > 0)
	{
		// Submit the batch
		TWeakObjectPtr<UWorld> WeakWorld(World);

		PerceptionSubsystem->SubmitPerceptionRequest(*Request,
			[WeakWorld, BatchEntities](const FFlightPerceptionResult& Result)
			{
				if (!Result.bSuccess || !WeakWorld.IsValid()) return;

				UMassEntitySubsystem* MassSubsystem = WeakWorld->GetSubsystem<UMassEntitySubsystem>();
				if (!MassSubsystem) return;

				FMassEntityManager& EM = MassSubsystem->GetMutableEntityManager();
				
				for (int32 i = 0; i < BatchEntities->Num(); ++i)
				{
					FMassEntityHandle Entity = (*BatchEntities)[i];
					if (EM.IsEntityValid(Entity))
					{
						if (FMassSpatialQueryFragment* Fragment = EM.GetFragmentDataPtr<FMassSpatialQueryFragment>(Entity))
						{
							// Update the reactive value
							float Count = static_cast<float>(Result.ObstacleCounts[i]);
							Fragment->CurrentResult = FVector(Count, Count > 0 ? 1.0f : 0.0f, 0.0f);
						}
					}
				}
			});
	}
}
