// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Mass/UFlightVexBehaviorProcessor.h"

#include "Mass/FlightMassFragments.h"
#include "MassExecutionContext.h"
#include "MassEntitySubsystem.h"
#include "Orchestration/FlightBehaviorBinding.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Verse/FlightVerseMassHostBundle.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Verse/UFlightVexTaskSubsystem.h"

namespace
{

const FName DefaultSwarmCohortName(TEXT("Swarm.Default"));

template <typename TFragment>
FMassFragmentHostView MakeChunkFragmentHostView(
	TArrayView<TFragment> Fragments,
	const FName FragmentType,
	const bool bWritable = true,
	FMassFragmentHostView::FPostWriteFn PostWrite = {})
{
	FMassFragmentHostView View;
	View.FragmentType = FragmentType;
	View.BasePtr = reinterpret_cast<uint8*>(Fragments.GetData());
	View.FragmentStride = sizeof(TFragment);
	View.EntityCount = Fragments.Num();
	View.bWritable = bWritable;
	View.PostWrite = MoveTemp(PostWrite);
	return View;
}

bool BuildDirectFragmentViewsForBehavior(
	const UFlightVerseSubsystem::FVerseBehavior& Behavior,
	FMassExecutionContext& ChunkContext,
	TArray<FMassFragmentHostView>& OutViews)
{
	OutViews.Reset();

	if (!Behavior.SchemaBinding.IsSet())
	{
		return false;
	}

	for (const Flight::Vex::FVexSchemaFragmentBinding& FragmentBinding : Behavior.SchemaBinding->FragmentBindings)
	{
		if (FragmentBinding.StorageKind != Flight::Vex::EVexStorageKind::MassFragmentField
			|| FragmentBinding.FragmentType.IsNone())
		{
			return false;
		}

		const bool bWritable = FragmentBinding.WrittenSymbols.Num() > 0;
		if (FragmentBinding.FragmentType == TEXT("FFlightTransformFragment"))
		{
			OutViews.Add(MakeChunkFragmentHostView(
				ChunkContext.GetMutableFragmentView<FFlightTransformFragment>(),
				FragmentBinding.FragmentType,
				bWritable));
			continue;
		}

		if (FragmentBinding.FragmentType == TEXT("FFlightDroidStateFragment"))
		{
			OutViews.Add(MakeChunkFragmentHostView(
				ChunkContext.GetMutableFragmentView<FFlightDroidStateFragment>(),
				FragmentBinding.FragmentType,
				bWritable,
				[](uint8* FragmentPtr)
				{
					if (FFlightDroidStateFragment* DroidState = reinterpret_cast<FFlightDroidStateFragment*>(FragmentPtr))
					{
						DroidState->bIsDirty = true;
					}
				}));
			continue;
		}

		return false;
	}

	return OutViews.Num() > 0;
}

bool IsBehaviorExecutable(const UFlightVerseSubsystem::FVerseBehavior& Behavior)
{
	return Behavior.bHasExecutableProcedure || Behavior.bUsesNativeFallback || Behavior.SimdPlan.IsValid();
}

bool ResolveFallbackBehaviorSelection(const UFlightVerseSubsystem& VerseSubsystem, uint32& OutBehaviorID)
{
	const TPair<uint32, UFlightVerseSubsystem::FVerseBehavior>* SelectedPair = nullptr;
	for (const TPair<uint32, UFlightVerseSubsystem::FVerseBehavior>& Pair : VerseSubsystem.Behaviors)
	{
		if (!IsBehaviorExecutable(Pair.Value))
		{
			continue;
		}

		if (!SelectedPair || Pair.Key < SelectedPair->Key)
		{
			SelectedPair = &Pair;
		}
	}

	if (!SelectedPair)
	{
		return false;
	}

	OutBehaviorID = SelectedPair->Key;
	return true;
}

Flight::Orchestration::EFlightExecutionDomain ResolveFallbackExecutionDomain(const UFlightVerseSubsystem::FVerseBehavior& Behavior)
{
	if (Behavior.SimdPlan.IsValid())
	{
		return Flight::Orchestration::EFlightExecutionDomain::Simd;
	}

	if (Behavior.bUsesNativeFallback)
	{
		return Behavior.bIsAsync
			? Flight::Orchestration::EFlightExecutionDomain::TaskGraph
			: Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	}

	return Flight::Orchestration::EFlightExecutionDomain::Unknown;
}

} // namespace

UFlightVexBehaviorProcessor::UFlightVexBehaviorProcessor()
{
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	bAutoRegisterWithProcessingPhases = true;
	RegisterQuery(EntityQuery);
}

bool UFlightVexBehaviorProcessor::ResolveBehaviorSelection(
	UWorld* World,
	const UFlightVerseSubsystem& VerseSubsystem,
	uint32& OutBehaviorID,
	Flight::Orchestration::FFlightBehaviorBinding* OutBinding,
	const FName CohortName)
{
	if (OutBinding)
	{
		*OutBinding = Flight::Orchestration::FFlightBehaviorBinding();
	}

	if (World)
	{
		if (const UFlightOrchestrationSubsystem* OrchestrationSubsystem = World->GetSubsystem<UFlightOrchestrationSubsystem>())
		{
			const FName EffectiveCohortName = CohortName.IsNone() ? DefaultSwarmCohortName : CohortName;
			Flight::Orchestration::FFlightBehaviorBinding Binding;
			if (OrchestrationSubsystem->TryGetBindingForCohort(EffectiveCohortName, Binding))
			{
				if (const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem.Behaviors.Find(Binding.Identity.BehaviorID);
					Behavior && IsBehaviorExecutable(*Behavior))
				{
					OutBehaviorID = Binding.Identity.BehaviorID;
					if (OutBinding)
					{
						*OutBinding = Binding;
					}
					return true;
				}
			}
		}
	}

	if (!ResolveFallbackBehaviorSelection(VerseSubsystem, OutBehaviorID))
	{
		return false;
	}

	if (OutBinding)
	{
		*OutBinding = Flight::Orchestration::FFlightBehaviorBinding();
		OutBinding->Identity.CohortName = CohortName.IsNone() ? DefaultSwarmCohortName : CohortName;
		OutBinding->Identity.BehaviorID = OutBehaviorID;
		OutBinding->SyncReportIdentity();
		if (const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem.Behaviors.Find(OutBehaviorID))
		{
			OutBinding->Report.ExecutionDomain = ResolveFallbackExecutionDomain(*Behavior);
			OutBinding->Report.FrameInterval = Behavior->FrameInterval;
			OutBinding->Report.bAsync = Behavior->bIsAsync;
		}
		OutBinding->Report.Selection.Source = Flight::Orchestration::EFlightBehaviorBindingSelectionSource::VerseFallback;
		OutBinding->Report.Selection.Rule = Flight::Orchestration::EFlightBehaviorBindingSelectionRule::LowestExecutableBehaviorId;
	}

	return true;
}

void UFlightVexBehaviorProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// We require the transform and drone state fragments
	EntityQuery.AddRequirement<FFlightTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FFlightDroidStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FFlightBehaviorCohortFragment>(EMassFragmentPresence::Optional);
}

void UFlightVexBehaviorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = GetWorld();
	UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;
	UFlightVexTaskSubsystem* TaskSubsystem = World ? World->GetSubsystem<UFlightVexTaskSubsystem>() : nullptr;
	UFlightSwarmSubsystem* SwarmSubsystem = World ? World->GetSubsystem<UFlightSwarmSubsystem>() : nullptr;

	if (!VerseSubsystem || !TaskSubsystem || !SwarmSubsystem)
	{
		return;
	}

	const uint32 CurrentFrame = static_cast<uint32>(GFrameCounter);
	TArray<UE::Tasks::FTask> ActiveTasks;

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
	{
		const FFlightBehaviorCohortFragment* CohortFragment = ChunkContext.GetConstSharedFragmentPtr<FFlightBehaviorCohortFragment>();
		const FName CohortName = (CohortFragment && !CohortFragment->CohortName.IsNone())
			? CohortFragment->CohortName
			: DefaultSwarmCohortName;

		uint32 BehaviorID = 0;
		if (!ResolveBehaviorSelection(World, *VerseSubsystem, BehaviorID, nullptr, CohortName))
		{
			return;
		}

		const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
		if (!Behavior)
		{
			return;
		}

		uint32 Interval = Behavior->FrameInterval;
		if (Behavior->ExecutionRateHz > 0.0f)
		{
			Interval = FMath::Max(1u, static_cast<uint32>(60.0f / Behavior->ExecutionRateHz));
		}
		if (CurrentFrame % Interval != 0)
		{
			return;
		}

		const TArrayView<::FFlightTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<::FFlightTransformFragment>();
		const TArrayView<::FFlightDroidStateFragment> DroidStates = ChunkContext.GetMutableFragmentView<::FFlightDroidStateFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		TArray<FMassFragmentHostView> DirectFragmentViews;
		const bool bHasDirectFragmentViews = BuildDirectFragmentViewsForBehavior(*Behavior, ChunkContext, DirectFragmentViews);
		if (Behavior->Tier == Flight::Vex::EVexTier::Literal
			&& Behavior->SimdPlan.IsValid()
			&& bHasDirectFragmentViews)
		{
			VerseSubsystem->ExecuteBehaviorDirect(BehaviorID, DirectFragmentViews);
			SwarmSubsystem->RequestSort(ESwarmSortRequirement::Behavior);
			return;
		}

		TArray<Flight::Swarm::FDroidState> ModifiedDroids;
		ModifiedDroids.SetNumUninitialized(NumEntities);

		auto ProcessChunk = [&]()
		{
			VerseSubsystem->ExecuteBehaviorBulk(BehaviorID, TArrayView<Flight::Swarm::FDroidState>(ModifiedDroids));

			for (int32 i = 0; i < NumEntities; ++i)
			{
				Transforms[i].Location = static_cast<FVector>(ModifiedDroids[i].Position);
				Transforms[i].Velocity = static_cast<FVector>(ModifiedDroids[i].Velocity);
				DroidStates[i].Shield = ModifiedDroids[i].Shield;
				DroidStates[i].bIsDirty = true;
			}
		};

		for (int32 i = 0; i < NumEntities; ++i)
		{
			ModifiedDroids[i].Position = static_cast<FVector3f>(Transforms[i].Location);
			ModifiedDroids[i].Velocity = static_cast<FVector3f>(Transforms[i].Velocity);
			ModifiedDroids[i].Shield = DroidStates[i].Shield;
		}

		if (Behavior->bIsAsync)
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
