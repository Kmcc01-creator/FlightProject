// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/FlightVerseMassHostBundle.h"

#include "Verse/FlightVerseRuntimeValueAccess.h"

namespace
{
	const FMassFragmentHostView* ResolveMassFragmentHostView(
		const FMassFragmentHostBundle& Bundle,
		const int32 HostViewIndex)
	{
		return Bundle.Views.IsValidIndex(HostViewIndex)
			? &Bundle.Views[HostViewIndex]
			: nullptr;
	}

	FMassFragmentHostView* ResolveMutableMassFragmentHostView(
		FMassFragmentHostBundle& Bundle,
		const int32 HostViewIndex)
	{
		return Bundle.Views.IsValidIndex(HostViewIndex)
			? &Bundle.Views[HostViewIndex]
			: nullptr;
	}
}

bool FMassFragmentHostView::IsValid() const
{
	return !FragmentType.IsNone() && BasePtr != nullptr && FragmentStride > 0 && EntityCount >= 0;
}

const uint8* FMassFragmentHostView::GetConstFragmentPtr(const int32 EntityIndex) const
{
	if (!IsValid() || EntityIndex < 0 || EntityIndex >= EntityCount)
	{
		return nullptr;
	}

	return BasePtr + (static_cast<SIZE_T>(EntityIndex) * FragmentStride);
}

uint8* FMassFragmentHostView::GetMutableFragmentPtr(const int32 EntityIndex) const
{
	if (!bWritable)
	{
		return nullptr;
	}

	return const_cast<uint8*>(GetConstFragmentPtr(EntityIndex));
}

void FMassFragmentHostView::NotifyPostWrite(uint8* FragmentPtr) const
{
	if (PostWrite && FragmentPtr)
	{
		PostWrite(FragmentPtr);
	}
}

int32 FMassFragmentHostBundle::FindViewIndex(const FName FragmentType) const
{
	return Views.IndexOfByPredicate([FragmentType](const FMassFragmentHostView& View)
	{
		return View.FragmentType == FragmentType;
	});
}

const FMassFragmentHostView* FMassFragmentHostBundle::FindView(const FName FragmentType) const
{
	return Views.FindByPredicate([FragmentType](const FMassFragmentHostView& View)
	{
		return View.FragmentType == FragmentType;
	});
}

FMassFragmentHostView* FMassFragmentHostBundle::FindMutableView(const FName FragmentType)
{
	return Views.FindByPredicate([FragmentType](const FMassFragmentHostView& View)
	{
		return View.FragmentType == FragmentType;
	});
}

int32 FMassFragmentHostBundle::GetEntityCount() const
{
	int32 Count = INDEX_NONE;
	for (const FMassFragmentHostView& View : Views)
	{
		if (!View.IsValid())
		{
			continue;
		}

		Count = Count == INDEX_NONE ? View.EntityCount : FMath::Min(Count, View.EntityCount);
	}

	return Count == INDEX_NONE ? 0 : Count;
}

FMassFragmentHostBundleBuilder& FMassFragmentHostBundleBuilder::AddView(FMassFragmentHostView View)
{
	if (View.IsValid())
	{
		Bundle.Views.Add(MoveTemp(View));
	}

	return *this;
}

FMassFragmentHostBundleBuilder& FMassFragmentHostBundleBuilder::AddViews(const TConstArrayView<FMassFragmentHostView> Views)
{
	for (const FMassFragmentHostView& View : Views)
	{
		AddView(View);
	}

	return *this;
}

FMassFragmentHostBundle FMassFragmentHostBundleBuilder::Build()
{
	return MoveTemp(Bundle);
}

bool FMassResolvedSymbolAccess::IsValid() const
{
	return !SymbolName.IsEmpty()
		&& HostViewIndex != INDEX_NONE
		&& RuntimeRecord.Storage.MemberOffset != INDEX_NONE;
}

const FMassResolvedSymbolAccess* FMassResolvedSymbolAccessTable::FindAccess(const FString& SymbolName) const
{
	const int32* AccessIndex = AccessIndexBySymbol.Find(SymbolName);
	return (AccessIndex && Accesses.IsValidIndex(*AccessIndex))
		? &Accesses[*AccessIndex]
		: nullptr;
}

FMassFragmentHostBundle BuildDirectMassFragmentHostBundle(
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates)
{
	return FMassFragmentHostBundleBuilder()
		.AddTypedFragmentView<FFlightTransformFragment>(Transforms, TEXT("FFlightTransformFragment"))
		.AddTypedFragmentView<FFlightDroidStateFragment>(
			DroidStates,
			TEXT("FFlightDroidStateFragment"),
			true,
			[](uint8* FragmentPtr)
			{
				if (FFlightDroidStateFragment* DroidState = reinterpret_cast<FFlightDroidStateFragment*>(FragmentPtr))
				{
					DroidState->bIsDirty = true;
				}
			})
		.Build();
}

FMassFragmentHostBundle BuildMassFragmentHostBundle(const TConstArrayView<FMassFragmentHostView> Views)
{
	return FMassFragmentHostBundleBuilder()
		.AddViews(Views)
		.Build();
}

bool BuildResolvedMassSymbolAccessTable(
	const Flight::Vex::FVexTypeSchema& BaseSchema,
	const Flight::Vex::FVexSchemaBindingResult& Binding,
	const FMassFragmentHostBundle& Bundle,
	FMassResolvedSymbolAccessTable& OutTable)
{
	OutTable.Accesses.Reset();
	OutTable.AccessIndexBySymbol.Reset();

	for (const TPair<FString, Flight::Vex::FVexSymbolRecord>& Pair : BaseSchema.SymbolRecords)
	{
		const Flight::Vex::FVexSymbolRecord& SourceRecord = Pair.Value;
		if (SourceRecord.Storage.Kind != Flight::Vex::EVexStorageKind::MassFragmentField)
		{
			return false;
		}

		const Flight::Vex::FVexSchemaBoundSymbol* BoundSymbol = Binding.FindBoundSymbolByName(Pair.Key);
		if (!BoundSymbol)
		{
			return false;
		}

		const Flight::Vex::FVexSchemaFragmentBinding* FragmentBinding =
			Binding.FindFragmentBindingByType(BoundSymbol->LogicalSymbol.Storage.FragmentType);
		if (!FragmentBinding || FragmentBinding->StorageKind != Flight::Vex::EVexStorageKind::MassFragmentField)
		{
			return false;
		}

		const int32 HostViewIndex = Bundle.FindViewIndex(FragmentBinding->FragmentType);
		if (HostViewIndex == INDEX_NONE)
		{
			return false;
		}

		FMassResolvedSymbolAccess& Access = OutTable.Accesses.AddDefaulted_GetRef();
		Access.SymbolName = Pair.Key;
		Access.HostViewIndex = HostViewIndex;
		Access.RuntimeRecord = SourceRecord;
		Access.RuntimeRecord.Storage.FragmentType = FragmentBinding->FragmentType;

		if (!Access.IsValid())
		{
			return false;
		}

		OutTable.AccessIndexBySymbol.Add(Access.SymbolName, OutTable.Accesses.Num() - 1);
	}

	return true;
}

bool TryReadMassBundleSymbol(
	const FMassFragmentHostBundle& Bundle,
	const Flight::Vex::FVexSymbolRecord& Symbol,
	const int32 EntityIndex,
	Flight::Vex::FVexRuntimeValue& OutValue)
{
	const FMassFragmentHostView* View = Bundle.FindView(Symbol.Storage.FragmentType);
	if (!View || Symbol.Storage.MemberOffset == INDEX_NONE)
	{
		return false;
	}

	const uint8* FragmentPtr = View->GetConstFragmentPtr(EntityIndex);
	if (!FragmentPtr)
	{
		return false;
	}

	const uint32 ElementStride = Flight::VerseRuntime::ResolveRuntimeValueStride(Symbol);
	const uint8* Bytes = FragmentPtr + Symbol.Storage.MemberOffset;
	OutValue = Flight::VerseRuntime::ReadRuntimeValueFromAddress(Bytes, Symbol.ValueType, ElementStride);
	return true;
}

bool TryWriteMassBundleSymbol(
	FMassFragmentHostBundle& Bundle,
	const Flight::Vex::FVexSymbolRecord& Symbol,
	const int32 EntityIndex,
	const Flight::Vex::FVexRuntimeValue& Value)
{
	FMassFragmentHostView* View = Bundle.FindMutableView(Symbol.Storage.FragmentType);
	if (!View || Symbol.Storage.MemberOffset == INDEX_NONE)
	{
		return false;
	}

	uint8* FragmentPtr = View->GetMutableFragmentPtr(EntityIndex);
	if (!FragmentPtr)
	{
		return false;
	}

	const uint32 ElementStride = Flight::VerseRuntime::ResolveRuntimeValueStride(Symbol);
	uint8* Bytes = FragmentPtr + Symbol.Storage.MemberOffset;
	if (!Flight::VerseRuntime::WriteRuntimeValueToAddress(Bytes, Symbol.ValueType, ElementStride, Value))
	{
		return false;
	}

	View->NotifyPostWrite(FragmentPtr);

	return true;
}

bool TryReadMassResolvedSymbolAccess(
	const FMassFragmentHostBundle& Bundle,
	const FMassResolvedSymbolAccess& Access,
	const int32 EntityIndex,
	Flight::Vex::FVexRuntimeValue& OutValue)
{
	const FMassFragmentHostView* View = ResolveMassFragmentHostView(Bundle, Access.HostViewIndex);
	if (!View || !Access.IsValid())
	{
		return false;
	}

	const uint8* FragmentPtr = View->GetConstFragmentPtr(EntityIndex);
	if (!FragmentPtr)
	{
		return false;
	}

	const Flight::Vex::FVexSymbolRecord& Symbol = Access.RuntimeRecord;
	const uint32 ElementStride = Flight::VerseRuntime::ResolveRuntimeValueStride(Symbol);
	const uint8* Bytes = FragmentPtr + Symbol.Storage.MemberOffset;
	OutValue = Flight::VerseRuntime::ReadRuntimeValueFromAddress(Bytes, Symbol.ValueType, ElementStride);
	return true;
}

bool TryWriteMassResolvedSymbolAccess(
	FMassFragmentHostBundle& Bundle,
	const FMassResolvedSymbolAccess& Access,
	const int32 EntityIndex,
	const Flight::Vex::FVexRuntimeValue& Value)
{
	FMassFragmentHostView* View = ResolveMutableMassFragmentHostView(Bundle, Access.HostViewIndex);
	if (!View || !Access.IsValid())
	{
		return false;
	}

	uint8* FragmentPtr = View->GetMutableFragmentPtr(EntityIndex);
	if (!FragmentPtr)
	{
		return false;
	}

	const Flight::Vex::FVexSymbolRecord& Symbol = Access.RuntimeRecord;
	const uint32 ElementStride = Flight::VerseRuntime::ResolveRuntimeValueStride(Symbol);
	uint8* Bytes = FragmentPtr + Symbol.Storage.MemberOffset;
	if (!Flight::VerseRuntime::WriteRuntimeValueToAddress(Bytes, Symbol.ValueType, ElementStride, Value))
	{
		return false;
	}

	View->NotifyPostWrite(FragmentPtr);

	return true;
}
