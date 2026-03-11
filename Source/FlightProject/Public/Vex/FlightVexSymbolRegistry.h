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

enum class EVexSchemaResolutionStatus : uint8
{
	ResolvedExisting,
	ResolvedAfterProvider,
	NotVexCapable,
	MissingProvider,
	ProviderFailed,
	ProviderReturnedNoSchema,
};

struct FVexSchemaResolutionResult
{
	const FVexTypeSchema* Schema = nullptr;
	EVexSchemaResolutionStatus Status = EVexSchemaResolutionStatus::ResolvedExisting;
	FString Diagnostic;

	bool IsSuccess() const
	{
		return Schema != nullptr;
	}
};

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

	/** Find a canonical symbol record by name for a specific type key. */
	const FVexSymbolRecord* FindSymbolRecord(const void* TypeKey, const FString& Name) const;

	/** Find an existing schema using its reflected native struct bridge. */
	const FVexTypeSchema* GetSchemaByNativeStruct(const UScriptStruct* NativeStruct) const;

	/** Resolve a schema from reflected type metadata and report how it was obtained. */
	FVexSchemaResolutionResult ResolveSchemaForReflectedType(const Flight::Reflection::FTypeRegistry::FTypeInfo& TypeInfo);

	/** Resolve a schema from reflected type metadata, building it on demand when possible. */
	const FVexTypeSchema* GetSchemaForReflectedType(const Flight::Reflection::FTypeRegistry::FTypeInfo& TypeInfo);

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
inline const void* TTypeVexRegistry<T>::GetTypeKey()
{
	static_assert(Flight::Reflection::CReflectable<T>, "TTypeVexRegistry requires a reflectable type.");
	return Flight::Reflection::GetRuntimeTypeKey<T>();
}

template<typename T>
inline void TTypeVexRegistry<T>::Register()
{
	static_assert(Flight::Reflection::CReflectable<T>, "TTypeVexRegistry requires a reflectable type.");
	FVexSymbolRegistry::Get().RegisterSchema(FVexSchemaOrchestrator::BuildSchemaFromReflection<T>(GetTypeKey()));
}

} // namespace Flight::Vex
