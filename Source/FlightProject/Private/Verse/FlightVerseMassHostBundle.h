// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mass/FlightMassFragments.h"
#include "Vex/FlightVexSchema.h"
#include "Vex/FlightVexSchemaIr.h"

struct FMassFragmentHostView
{
	using FPostWriteFn = TFunction<void(uint8*)>;

	FName FragmentType = NAME_None;
	uint8* BasePtr = nullptr;
	uint32 FragmentStride = 0;
	int32 EntityCount = 0;
	bool bWritable = false;
	FPostWriteFn PostWrite;

	bool IsValid() const;
	const uint8* GetConstFragmentPtr(int32 EntityIndex) const;
	uint8* GetMutableFragmentPtr(int32 EntityIndex) const;
	void NotifyPostWrite(uint8* FragmentPtr) const;
};

struct FMassFragmentHostBundle
{
	TArray<FMassFragmentHostView> Views;

	int32 FindViewIndex(FName FragmentType) const;
	const FMassFragmentHostView* FindView(FName FragmentType) const;
	FMassFragmentHostView* FindMutableView(FName FragmentType);
	int32 GetEntityCount() const;
};

struct FMassFragmentHostBundleBuilder
{
	FMassFragmentHostBundle Bundle;

	FMassFragmentHostBundleBuilder& AddView(FMassFragmentHostView View);
	FMassFragmentHostBundleBuilder& AddViews(TConstArrayView<FMassFragmentHostView> Views);

	template <typename TFragment>
	FMassFragmentHostBundleBuilder& AddTypedFragmentView(
		TArrayView<TFragment> Fragments,
		const FName FragmentType,
		const bool bWritable = true,
		typename FMassFragmentHostView::FPostWriteFn PostWrite = {})
	{
		if (Fragments.Num() == 0)
		{
			return *this;
		}

		FMassFragmentHostView View;
		View.FragmentType = FragmentType;
		View.BasePtr = reinterpret_cast<uint8*>(Fragments.GetData());
		View.FragmentStride = sizeof(TFragment);
		View.EntityCount = Fragments.Num();
		View.bWritable = bWritable;
		View.PostWrite = MoveTemp(PostWrite);
		return AddView(MoveTemp(View));
	}

	FMassFragmentHostBundle Build();
};

struct FMassResolvedSymbolAccess
{
	FString SymbolName;
	int32 HostViewIndex = INDEX_NONE;
	Flight::Vex::FVexSymbolRecord RuntimeRecord;

	bool IsValid() const;
};

struct FMassResolvedSymbolAccessTable
{
	TArray<FMassResolvedSymbolAccess> Accesses;
	TMap<FString, int32> AccessIndexBySymbol;

	const FMassResolvedSymbolAccess* FindAccess(const FString& SymbolName) const;
};

FLIGHTPROJECT_API FMassFragmentHostBundle BuildDirectMassFragmentHostBundle(
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates);

FLIGHTPROJECT_API FMassFragmentHostBundle BuildMassFragmentHostBundle(
	TConstArrayView<FMassFragmentHostView> Views);

FLIGHTPROJECT_API bool TryReadMassBundleSymbol(
	const FMassFragmentHostBundle& Bundle,
	const Flight::Vex::FVexSymbolRecord& Symbol,
	int32 EntityIndex,
	Flight::Vex::FVexRuntimeValue& OutValue);

FLIGHTPROJECT_API bool TryWriteMassBundleSymbol(
	FMassFragmentHostBundle& Bundle,
	const Flight::Vex::FVexSymbolRecord& Symbol,
	int32 EntityIndex,
	const Flight::Vex::FVexRuntimeValue& Value);

FLIGHTPROJECT_API bool BuildResolvedMassSymbolAccessTable(
	const Flight::Vex::FVexTypeSchema& BaseSchema,
	const Flight::Vex::FVexSchemaBindingResult& Binding,
	const FMassFragmentHostBundle& Bundle,
	FMassResolvedSymbolAccessTable& OutTable);

FLIGHTPROJECT_API bool TryReadMassResolvedSymbolAccess(
	const FMassFragmentHostBundle& Bundle,
	const FMassResolvedSymbolAccess& Access,
	int32 EntityIndex,
	Flight::Vex::FVexRuntimeValue& OutValue);

FLIGHTPROJECT_API bool TryWriteMassResolvedSymbolAccess(
	FMassFragmentHostBundle& Bundle,
	const FMassResolvedSymbolAccess& Access,
	int32 EntityIndex,
	const Flight::Vex::FVexRuntimeValue& Value);
