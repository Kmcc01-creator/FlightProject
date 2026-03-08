// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Mass/UFlightVexBehaviorProcessor.h"
#include "Mass/FlightMassFragments.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Verse/UFlightVexTaskSubsystem.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "MassExecutionContext.h"
#include "MassEntitySubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"

UFlightVexBehaviorProcessor::UFlightVexBehaviorProcessor()
{
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	bAutoRegisterWithProcessingPhases = true;
	RegisterQuery(EntityQuery);
}

void UFlightVexBehaviorProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// We require the transform and drone state fragments
	EntityQuery.AddRequirement<FFlightTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FFlightVexDirtyFragment>(EMassFragmentAccess::ReadWrite);
}

void UFlightVexBehaviorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = GetWorld();
	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	UFlightVexTaskSubsystem* TaskSubsystem = World->GetSubsystem<UFlightVexTaskSubsystem>();
	UFlightSwarmSubsystem* SwarmSubsystem = World->GetSubsystem<UFlightSwarmSubsystem>();
	
	if (!VerseSubsystem || !TaskSubsystem || !SwarmSubsystem) return;

	const uint32 CurrentFrame = static_cast<uint32>(GFrameCounter);
	TArray<UE::Tasks::FTask> ActiveTasks;

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
	{
		const uint32 BehaviorID = 1; 
		
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		const auto* Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
		if (Behavior)
		{
			uint32 Interval = Behavior->FrameInterval;
			if (Behavior->ExecutionRateHz > 0.0f)
			{
				Interval = FMath::Max(1u, static_cast<uint32>(60.0f / Behavior->ExecutionRateHz));
			}
			if (CurrentFrame % Interval != 0) return;
		}
#endif

		const TArrayView<FFlightTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FFlightTransformFragment>();
		const TArrayView<FFlightVexDirtyFragment> DirtyFlags = ChunkContext.GetMutableFragmentView<FFlightVexDirtyFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		// For capturing dirty state for sparse blit
		TArray<Flight::Swarm::FDroidState> ModifiedDroids;
		ModifiedDroids.SetNumUninitialized(NumEntities);

		auto ProcessEntity = [&](int32 i)
		{
			Flight::Swarm::FDroidState TempState;
			TempState.Position = (FVector3f)Transforms[i].Location;
			TempState.Velocity = (FVector3f)Transforms[i].Velocity;

			VerseSubsystem->ExecuteBehavior(BehaviorID, TempState);

			Transforms[i].Location = (FVector)TempState.Position;
			Transforms[i].Velocity = (FVector)TempState.Velocity;
			DirtyFlags[i].bIsDirty = true;
			
			ModifiedDroids[i] = TempState;
		};

		bool bHasJob = false;
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		bHasJob = VerseSubsystem->Behaviors.Contains(BehaviorID); 
#endif

		if (bHasJob)
		{
			ActiveTasks.Add(TaskSubsystem->LaunchVexJob(BehaviorID, 0, [NumEntities, ProcessEntity]()
			{
				for (int32 i = 0; i < NumEntities; ++i) { ProcessEntity(i); }
			}));
		}
		else
		{
			for (int32 i = 0; i < NumEntities; ++i) { ProcessEntity(i); }
		}

		// Sparse Blit: Send the chunk's modified data to the GPU buffer
		// We'd need to map the Mass Chunk index to the GPU buffer index here.
		// For this prototype, we'll assume a 1:1 mapping for the first chunk.
		SwarmSubsystem->UpdateDroidState_Sparse(0, TArrayView<const Flight::Swarm::FDroidState>(ModifiedDroids));
		SwarmSubsystem->RequestSort(ESwarmSortRequirement::Behavior);
	});

	TaskSubsystem->WaitAll(ActiveTasks);
}
