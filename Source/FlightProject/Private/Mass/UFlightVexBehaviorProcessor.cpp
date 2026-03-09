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
	EntityQuery.AddRequirement<FFlightDroidStateFragment>(EMassFragmentAccess::ReadWrite);
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

		const TArrayView<::FFlightTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<::FFlightTransformFragment>();
		const TArrayView<::FFlightDroidStateFragment> DroidStates = ChunkContext.GetMutableFragmentView<::FFlightDroidStateFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		// PILOT: Direct SIMD path
		if (Behavior && Behavior->Tier == Flight::Vex::EVexTier::Literal && Behavior->SimdPlan.IsValid())
		{
			VerseSubsystem->ExecuteBehaviorDirect(BehaviorID, Transforms, DroidStates);
			
			// Optional: Update SwarmSubsystem if needed, or assume Direct SIMD handles internal state.
			// For this pilot, we'll still request a sort if any were dirty.
			SwarmSubsystem->RequestSort(ESwarmSortRequirement::Behavior);
			return;
		}

		// FALLBACK: Gather/Scatter path
		TArray<Flight::Swarm::FDroidState> ModifiedDroids;
		ModifiedDroids.SetNumUninitialized(NumEntities);

		auto ProcessChunk = [&]()
		{
			VerseSubsystem->ExecuteBehaviorBulk(BehaviorID, TArrayView<Flight::Swarm::FDroidState>(ModifiedDroids));

			for (int32 i = 0; i < NumEntities; ++i)
			{
				Transforms[i].Location = (FVector)ModifiedDroids[i].Position;
				Transforms[i].Velocity = (FVector)ModifiedDroids[i].Velocity;
				DroidStates[i].Shield = ModifiedDroids[i].Shield;
				DroidStates[i].bIsDirty = true;
			}
		};

		bool bHasJob = Behavior != nullptr; 

		// Initialize ModifiedDroids with current state
		for (int32 i = 0; i < NumEntities; ++i)
		{
			ModifiedDroids[i].Position = (FVector3f)Transforms[i].Location;
			ModifiedDroids[i].Velocity = (FVector3f)Transforms[i].Velocity;
			ModifiedDroids[i].Shield = DroidStates[i].Shield;
		}

		if (bHasJob && Behavior->bIsAsync)
		{
			ActiveTasks.Add(TaskSubsystem->LaunchVexJob(BehaviorID, 0, [ProcessChunk]()
			{
				ProcessChunk();
			}));
		}
		else
		{
			ProcessChunk();
		}

		SwarmSubsystem->UpdateDroidState_Sparse(0, TArrayView<const Flight::Swarm::FDroidState>(ModifiedDroids));
		SwarmSubsystem->RequestSort(ESwarmSortRequirement::Behavior);
	});

	TaskSubsystem->WaitAll(ActiveTasks);
}
