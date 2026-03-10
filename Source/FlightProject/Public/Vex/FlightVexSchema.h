// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexSchemaTypes.h"
#include "Vex/FlightVexTypes.h"
#include "UObject/WeakObjectPtr.h"

namespace Flight::Vex
{

/**
 * FVexSymbolAccessor: Handle for a specific symbol's Get/Set logic.
 * Enhanced with offset for zero-cost direct memory access.
 */
struct FVexSymbolAccessor
{
	using GetterFn = TFunction<float(const void*)>;
	using SetterFn = TFunction<void(void*, float)>;

	FString SymbolName;
	GetterFn Getter;
	SetterFn Setter;
	EVexValueType ValueType = EVexValueType::Unknown;

	/** Offset from the start of the struct (INDEX_NONE if not a simple POD field) */
	int32 MemberOffset = INDEX_NONE;

	/** Metadata for compiler validation */
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;
	FString HlslIdentifier;
	FString VerseIdentifier;
};

/**
 * FVexTypeSchema: The definitive "Identity Card" for a reflectable C++ type in the VEX system.
 * This bridges pure C++ traits with runtime orchestration and asset generation.
 */
struct FLIGHTPROJECT_API FVexTypeSchema
{
	/** Stable runtime and reporting identity for this type schema */
	FVexTypeId TypeId;

	/** Formal name of the C++ type (from traits) */
	FString TypeName;

	/** Total size of the struct in memory */
	uint32 Size = 0;

	/** Alignment requirement */
	uint32 Alignment = 0;

	/** Map of @symbols to their accessors and metadata */
	TMap<FString, FVexSymbolAccessor> Symbols;

	/** Compiler-facing logical symbol records */
	TMap<FString, FVexLogicalSymbolSchema> LogicalSymbols;

	/** Optional bridge back to Unreal's reflection if it's a USTRUCT */
	TWeakObjectPtr<UScriptStruct> NativeStruct;

	/** Unique hash representing the data layout (for versioning/binary compatibility) */
	uint32 LayoutHash = 0;

	/** Returns true if this schema has a direct memory mapping for a symbol */
	bool HasSymbol(const FString& Name) const { return Symbols.Contains(Name) || LogicalSymbols.Contains(Name); }

	const FVexLogicalSymbolSchema* FindLogicalSymbol(const FString& Name) const
	{
		return LogicalSymbols.Find(Name);
	}
};

} // namespace Flight::Vex
