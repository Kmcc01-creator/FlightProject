// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexSchema.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Core/FlightReflection.h"

namespace Flight::Vex
{

/**
 * FVexSymbolRegistry: Manages the mapping of VEX @symbols to reflected C++ fields.
 * Now formalized via FVexTypeSchema.
 */
class FLIGHTPROJECT_API FVexSymbolRegistry
{
public:
	static FVexSymbolRegistry& Get();

	/** Find or create a schema for a specific type key. */
	FVexTypeSchema& FindOrAddSchema(const void* TypeKey);

	/** Find an existing schema for a specific type key. */
	const FVexTypeSchema* GetSchema(const void* TypeKey) const;

	/** Find an accessor by name for a specific type key. */
	const FVexSymbolAccessor* FindSymbol(const void* TypeKey, const FString& Name) const;

	/** Find an existing schema using its reflected native struct bridge. */
	const FVexTypeSchema* GetSchemaByNativeStruct(const UScriptStruct* NativeStruct) const;

	/** Register or replace a complete schema. */
	void RegisterSchema(FVexTypeSchema Schema);

private:
	/** Map: TypeKey -> Formal Schema */
	TMap<const void*, FVexTypeSchema> Schemas;
};

/**
 * TTypeVexRegistry: Template helper to automatically register all VEX-tagged fields
 * for a reflectable C++ type.
 */
template<typename T>
struct TTypeVexRegistry
{
	static const void* GetTypeKey()
	{
		static uint8 TypeKeyMarker = 0;
		return &TypeKeyMarker;
	}

	static void Register()
	{
		FVexSymbolRegistry::Get().RegisterSchema(FVexSchemaOrchestrator::BuildSchemaFromReflection<T>(GetTypeKey()));
	}
};

} // namespace Flight::Vex
