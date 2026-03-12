// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "MassExecutionContext.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "Mass/FlightMassFragments.h"
#include "Navigation/FlightNavigationCommitProduct.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "RHI.h"
#include "Schema/FlightRequirementRegistry.h"
#include "UObject/UnrealType.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexParser.h"

namespace Flight::Test
{
	inline UWorld* FindAutomationWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
			{
				if (UWorld* World = Context.World())
				{
					return World;
				}
			}
		}

		return nullptr;
	}

	inline UWorldSubsystem* FindOptionalWorldSubsystem(UWorld* World, const TCHAR* ClassPath)
	{
		if (!World)
		{
			return nullptr;
		}

		if (UClass* SubsystemClass = FindObject<UClass>(nullptr, ClassPath))
		{
			return World->GetSubsystemBase(SubsystemClass);
		}

		return nullptr;
	}

	inline bool InvokeSubsystemNoArg(UWorld* World, const TCHAR* ClassPath, const TCHAR* FunctionName)
	{
		UWorldSubsystem* Subsystem = FindOptionalWorldSubsystem(World, ClassPath);
		if (!Subsystem)
		{
			return false;
		}

		UFunction* Function = Subsystem->GetClass()->FindFunctionByName(FunctionName);
		if (!Function)
		{
			return false;
		}

		Subsystem->ProcessEvent(Function, nullptr);
		return true;
	}

	inline bool SetObjectNameProperty(UObject* Object, const TCHAR* PropertyName, const FName Value)
	{
		if (!Object)
		{
			return false;
		}

		if (FNameProperty* Property = FindFProperty<FNameProperty>(Object->GetClass(), PropertyName))
		{
			Property->SetPropertyValue_InContainer(Object, Value);
			return true;
		}

		return false;
	}

	inline bool SetObjectReferenceProperty(UObject* Object, const TCHAR* PropertyName, UObject* Value)
	{
		if (!Object)
		{
			return false;
		}

		if (FObjectProperty* Property = FindFProperty<FObjectProperty>(Object->GetClass(), PropertyName))
		{
			Property->SetObjectPropertyValue_InContainer(Object, Value);
			return true;
		}

		return false;
	}

	inline TArray<FGuid> GatherPathIdsForCohort(UWorld* World, const FName CohortName)
	{
		TArray<FGuid> PathIds;
		if (!World)
		{
			return PathIds;
		}

		UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (!MassSubsystem)
		{
			return PathIds;
		}

		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
		FMassEntityQuery Query(EntityManager.AsShared());
		Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);
		Query.AddRequirement<FFlightPathFollowFragment>(EMassFragmentAccess::ReadOnly);
		Query.AddConstSharedRequirement<FFlightBehaviorCohortFragment>(EMassFragmentPresence::Optional);

		FMassExecutionContext Context(EntityManager, 0.0f);
		Query.ForEachEntityChunk(Context, [&PathIds, CohortName](FMassExecutionContext& ChunkContext)
		{
			const FFlightBehaviorCohortFragment* CohortFragment = ChunkContext.GetConstSharedFragmentPtr<FFlightBehaviorCohortFragment>();
			const FName ChunkCohortName = CohortFragment ? CohortFragment->CohortName : NAME_None;
			if (ChunkCohortName != CohortName)
			{
				return;
			}

			const TConstArrayView<FFlightPathFollowFragment> PathFragments = ChunkContext.GetFragmentView<FFlightPathFollowFragment>();
			for (const FFlightPathFollowFragment& PathFragment : PathFragments)
			{
				PathIds.Add(PathFragment.PathId);
			}
		});

		return PathIds;
	}

	inline bool FindNavigationCommitSharedFragmentForCohort(
		UWorld* World,
		const FName CohortName,
		const FGuid& ExpectedRuntimePathId,
		FFlightNavigationCommitSharedFragment& OutCommitFragment)
	{
		if (!World)
		{
			return false;
		}

		UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (!MassSubsystem)
		{
			return false;
		}

		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
		FMassEntityQuery Query(EntityManager.AsShared());
		Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);
		Query.AddConstSharedRequirement<FFlightBehaviorCohortFragment>(EMassFragmentPresence::Optional);
		Query.AddConstSharedRequirement<FFlightNavigationCommitSharedFragment>(EMassFragmentPresence::Optional);

		bool bFound = false;
		FMassExecutionContext Context(EntityManager, 0.0f);
		Query.ForEachEntityChunk(Context, [&OutCommitFragment, CohortName, ExpectedRuntimePathId, &bFound](FMassExecutionContext& ChunkContext)
		{
			if (bFound)
			{
				return;
			}

			const FFlightBehaviorCohortFragment* CohortFragment = ChunkContext.GetConstSharedFragmentPtr<FFlightBehaviorCohortFragment>();
			const FName ChunkCohortName = CohortFragment ? CohortFragment->CohortName : NAME_None;
			if (ChunkCohortName != CohortName)
			{
				return;
			}

			const FFlightNavigationCommitSharedFragment* CommitFragment =
				ChunkContext.GetConstSharedFragmentPtr<FFlightNavigationCommitSharedFragment>();
			if (!CommitFragment)
			{
				return;
			}
			if (ExpectedRuntimePathId.IsValid() && CommitFragment->Identity.RuntimePathId != ExpectedRuntimePathId)
			{
				return;
			}

			OutCommitFragment = *CommitFragment;
			bFound = true;
		});

		return bFound;
	}

	inline void DestroySwarmEntitiesDirect(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (!MassSubsystem)
		{
			return;
		}

		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
		FMassEntityQuery Query(EntityManager.AsShared());
		Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);

		TArray<FMassEntityHandle> EntitiesToDestroy = Query.GetMatchingEntityHandles();
		if (!EntitiesToDestroy.IsEmpty())
		{
			EntityManager.BatchDestroyEntities(EntitiesToDestroy);
			EntityManager.FlushCommands();
		}
	}

	inline void RemoveVisibleBehaviorRecords(UFlightOrchestrationSubsystem& Orchestration)
	{
		for (const Flight::Orchestration::FFlightBehaviorRecord& Behavior : Orchestration.GetReport().Behaviors)
		{
			Orchestration.UnregisterBehavior(Behavior.BehaviorID);
		}
	}

	inline UFlightVerseSubsystem::FVerseBehavior MakeExecutableBehavior(const uint32 FrameInterval)
	{
		UFlightVerseSubsystem::FVerseBehavior Behavior;
		Behavior.FrameInterval = FrameInterval;
		Behavior.bHasExecutableProcedure = true;
		Behavior.bUsesNativeFallback = true;
		return Behavior;
	}

	/** Setup standard symbol definitions for testing */
	inline TArray<Vex::FVexSymbolDefinition> GetMockSymbols()
	{
		TArray<Vex::FVexSymbolDefinition> Defs;
		
		Vex::FVexSymbolDefinition Pos;
		Pos.SymbolName = TEXT("@position");
		Pos.ValueType = TEXT("float3");
		Pos.Residency = EFlightVexSymbolResidency::Shared;
		Pos.Affinity = EFlightVexSymbolAffinity::Any;
		Pos.bWritable = false;
		Defs.Add(Pos);

		Vex::FVexSymbolDefinition Vel;
		Vel.SymbolName = TEXT("@velocity");
		Vel.ValueType = TEXT("float3");
		Vel.Residency = EFlightVexSymbolResidency::Shared;
		Vel.Affinity = EFlightVexSymbolAffinity::WorkerThread;
		Vel.bWritable = true;
		Defs.Add(Vel);

		Vex::FVexSymbolDefinition Shield;
		Shield.SymbolName = TEXT("@shield");
		Shield.ValueType = TEXT("float");
		Shield.Residency = EFlightVexSymbolResidency::GpuOnly;
		Shield.Affinity = EFlightVexSymbolAffinity::Any;
		Shield.bWritable = true;
		Defs.Add(Shield);

		Vex::FVexSymbolDefinition GtData;
		GtData.SymbolName = TEXT("@uobject_data");
		GtData.ValueType = TEXT("float");
		GtData.Affinity = EFlightVexSymbolAffinity::GameThread;
		Defs.Add(GtData);

		return Defs;
	}

	/** Helper to parse VEX and assert success */
	inline Vex::FVexParseResult ParseChecked(const FString& Source, const TArray<Vex::FVexSymbolDefinition>& Defs)
	{
		return Vex::ParseAndValidate(Source, Defs, false);
	}

	/** Returns true if the current environment is headless (-NullRHI) and GPU tests should be skipped. */
	inline bool ShouldSkipGpuTest()
	{
		// In UE 5.x, NullRHI is indicated by the RHI name
		static const FString RhiName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None");
		const bool bIsHeadless = (RhiName == TEXT("Null")) || IsRunningCommandlet();
		return bIsHeadless;
	}
}
