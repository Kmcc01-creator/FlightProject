// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVerseSubsystem.h"

#include "Verse/FlightVerseMassHostBundle.h"
#include "Vex/FlightVexSimdExecutor.h"
#include "Vex/FlightVexSymbolRegistry.h"

namespace
{
	template <typename TFragment>
	bool TryGetTypedMassFragmentView(
		FMassFragmentHostBundle& Bundle,
		const FName FragmentType,
		TArrayView<TFragment>& OutView)
	{
		const int32 ViewIndex = Bundle.FindViewIndex(FragmentType);
		if (ViewIndex == INDEX_NONE || !Bundle.Views.IsValidIndex(ViewIndex))
		{
			return false;
		}

		FMassFragmentHostView& View = Bundle.Views[ViewIndex];
		if (!View.IsValid() || View.FragmentStride != sizeof(TFragment))
		{
			return false;
		}

		OutView = TArrayView<TFragment>(reinterpret_cast<TFragment*>(View.BasePtr), View.EntityCount);
		return true;
	}

	struct FMassRuntimeSchemaExecutionContext
	{
		FMassFragmentHostBundle* Bundle = nullptr;
		int32 EntityIndex = INDEX_NONE;
	};

	bool BuildMassRuntimeSchemaTemplate(
		const Flight::Vex::FVexTypeSchema& BaseSchema,
		const FMassResolvedSymbolAccessTable& AccessTable,
		Flight::Vex::FVexTypeSchema& OutSchema)
	{
		OutSchema = BaseSchema;
		OutSchema.SymbolRecords.Reset();

		for (const TPair<FString, Flight::Vex::FVexSymbolRecord>& Pair : BaseSchema.SymbolRecords)
		{
			const FMassResolvedSymbolAccess* Access = AccessTable.FindAccess(Pair.Key);
			if (!Access || !Access->IsValid())
			{
				return false;
			}

			Flight::Vex::FVexSymbolRecord RuntimeRecord = Access->RuntimeRecord;
			RuntimeRecord.ReadValue = [Access](const void* Ptr) -> Flight::Vex::FVexRuntimeValue
			{
				const FMassRuntimeSchemaExecutionContext* Context =
					static_cast<const FMassRuntimeSchemaExecutionContext*>(Ptr);
				Flight::Vex::FVexRuntimeValue Value;
				if (!Context || !Context->Bundle)
				{
					return Value;
				}

				(void)TryReadMassResolvedSymbolAccess(*Context->Bundle, *Access, Context->EntityIndex, Value);
				return Value;
			};
			RuntimeRecord.WriteValue = [Access](void* Ptr, const Flight::Vex::FVexRuntimeValue& Value) -> bool
			{
				FMassRuntimeSchemaExecutionContext* Context =
					static_cast<FMassRuntimeSchemaExecutionContext*>(Ptr);
				return Context && Context->Bundle
					? TryWriteMassResolvedSymbolAccess(*Context->Bundle, *Access, Context->EntityIndex, Value)
					: false;
			};
			RuntimeRecord.Getter = {};
			RuntimeRecord.Setter = {};
			OutSchema.SymbolRecords.Add(Pair.Key, MoveTemp(RuntimeRecord));
		}

		OutSchema.RebuildLegacyViews();
		return true;
	}
}

bool UFlightVerseSubsystem::ExecuteOnMassSchemaHost(
	const FVerseBehavior& Behavior,
	const Flight::Vex::EVexBackendKind BackendKind,
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates)
{
	FMassFragmentHostBundle Bundle = BuildDirectMassFragmentHostBundle(Transforms, DroidStates);
	return ExecuteOnMassSchemaHost(Behavior, BackendKind, Bundle);
}

bool UFlightVerseSubsystem::ExecuteOnMassSchemaHost(
	const FVerseBehavior& Behavior,
	const Flight::Vex::EVexBackendKind BackendKind,
	FMassFragmentHostBundle& Bundle)
{
	if (!Behavior.SchemaBinding.IsSet())
	{
		return false;
	}

	const Flight::Vex::FVexTypeSchema* BaseSchema = Behavior.BoundTypeKey
		? Flight::Vex::FVexSymbolRegistry::Get().GetSchema(Behavior.BoundTypeKey)
		: nullptr;
	if (!BaseSchema)
	{
		return false;
	}

	if (BackendKind == Flight::Vex::EVexBackendKind::NativeSimd)
	{
		if (Behavior.Tier != Flight::Vex::EVexTier::Literal || !Behavior.SimdPlan.IsValid())
		{
			return false;
		}

		TArrayView<FFlightTransformFragment> Transforms;
		TArrayView<FFlightDroidStateFragment> DroidStates;
		if (!TryGetTypedMassFragmentView(Bundle, TEXT("FFlightTransformFragment"), Transforms)
			|| !TryGetTypedMassFragmentView(Bundle, TEXT("FFlightDroidStateFragment"), DroidStates))
		{
			return false;
		}

		Behavior.SimdPlan->ExecuteDirect(Transforms, DroidStates);
		return true;
	}

	if (BackendKind == Flight::Vex::EVexBackendKind::NativeAvx256x8)
	{
		if (Behavior.Tier != Flight::Vex::EVexTier::Literal
			|| !Behavior.SimdPlan.IsValid())
		{
			return false;
		}

		TArrayView<FFlightTransformFragment> Transforms;
		TArrayView<FFlightDroidStateFragment> DroidStates;
		if (!TryGetTypedMassFragmentView(Bundle, TEXT("FFlightTransformFragment"), Transforms)
			|| !TryGetTypedMassFragmentView(Bundle, TEXT("FFlightDroidStateFragment"), DroidStates)
			|| !Behavior.SimdPlan->ExecuteExplicitAvx256x8Direct(Transforms, DroidStates))
		{
			return false;
		}

		return true;
	}

	if (BackendKind != Flight::Vex::EVexBackendKind::NativeScalar)
	{
		return false;
	}

	FMassResolvedSymbolAccessTable AccessTable;
	if (!BuildResolvedMassSymbolAccessTable(*BaseSchema, *Behavior.SchemaBinding, Bundle, AccessTable))
	{
		return false;
	}

	Flight::Vex::FVexTypeSchema RuntimeSchemaTemplate;
	if (!BuildMassRuntimeSchemaTemplate(*BaseSchema, AccessTable, RuntimeSchemaTemplate))
	{
		return false;
	}

	const int32 NumEntities = Bundle.GetEntityCount();
	FMassRuntimeSchemaExecutionContext ExecutionContext;
	ExecutionContext.Bundle = &Bundle;
	for (int32 Index = 0; Index < NumEntities; ++Index)
	{
		ExecutionContext.EntityIndex = Index;
		ExecuteOnSchema(Behavior.NativeProgram, &ExecutionContext, RuntimeSchemaTemplate);
	}

	return true;
}
