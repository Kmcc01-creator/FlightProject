// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSymbolRegistry.h"

namespace Flight::Vex
{

namespace
{

const FVexTypeSchema* FindSchemaForReflectedType(
	FVexSymbolRegistry& Registry,
	const Flight::Reflection::FTypeRegistry::FTypeInfo& TypeInfo)
{
	if (TypeInfo.RuntimeKey)
	{
		if (const FVexTypeSchema* Schema = Registry.GetSchema(TypeInfo.RuntimeKey))
		{
			return Schema;
		}
	}

	if (const UScriptStruct* NativeStruct = TypeInfo.GetNativeStruct())
	{
		if (const FVexTypeSchema* Schema = Registry.GetSchemaByNativeStruct(NativeStruct))
		{
			return Schema;
		}
	}

	return nullptr;
}

} // namespace

FVexSymbolRegistry& FVexSymbolRegistry::Get()
{
	static FVexSymbolRegistry Instance;
	return Instance;
}

FVexTypeSchema& FVexSymbolRegistry::FindOrAddSchema(const void* TypeKey)
{
	return Schemas.FindOrAdd(TypeKey);
}

void FVexSymbolRegistry::RegisterSchema(FVexTypeSchema Schema)
{
	const void* RuntimeKey = Schema.TypeId.RuntimeKey;
	if (!RuntimeKey)
	{
		RuntimeKey = Schema.NativeStruct.Get();
		Schema.TypeId.RuntimeKey = RuntimeKey;
	}

	if (RuntimeKey)
	{
		Schemas.Add(RuntimeKey, MoveTemp(Schema));
	}
}

const FVexTypeSchema* FVexSymbolRegistry::GetSchema(const void* TypeKey) const
{
	return Schemas.Find(TypeKey);
}

const FVexTypeSchema* FVexSymbolRegistry::GetSchemaByNativeStruct(const UScriptStruct* NativeStruct) const
{
	if (!NativeStruct)
	{
		return nullptr;
	}

	for (const TPair<const void*, FVexTypeSchema>& Pair : Schemas)
	{
		if (Pair.Value.NativeStruct.Get() == NativeStruct)
		{
			return &Pair.Value;
		}
	}

	return nullptr;
}

FVexSchemaResolutionResult FVexSymbolRegistry::ResolveSchemaForReflectedType(const Flight::Reflection::FTypeRegistry::FTypeInfo& TypeInfo)
{
	if (const FVexTypeSchema* ExistingSchema = FindSchemaForReflectedType(*this, TypeInfo))
	{
		return { ExistingSchema, EVexSchemaResolutionStatus::ResolvedExisting, FString() };
	}

	if (TypeInfo.VexCapability == Flight::Reflection::EVexCapability::NotVexCapable)
	{
		return { nullptr, EVexSchemaResolutionStatus::NotVexCapable, TEXT("Reflected type is not VEX-capable.") };
	}

	if (!TypeInfo.ProvideVexSchemaFn)
	{
		return { nullptr, EVexSchemaResolutionStatus::MissingProvider, TEXT("No reflected VEX schema provider is registered for this type.") };
	}

	const Flight::Reflection::FVexSchemaProviderResult ProviderResult = TypeInfo.ProvideVexSchemaFn();
	if (!ProviderResult.bSuccess)
	{
		return { nullptr, EVexSchemaResolutionStatus::ProviderFailed, ProviderResult.Diagnostic };
	}

	if (const FVexTypeSchema* ProvidedSchema = FindSchemaForReflectedType(*this, TypeInfo))
	{
		return { ProvidedSchema, EVexSchemaResolutionStatus::ResolvedAfterProvider, ProviderResult.Diagnostic };
	}

	const FString Diagnostic = ProviderResult.Diagnostic.IsEmpty()
		? FString(TEXT("VEX schema provider returned success, but no schema was registered."))
		: ProviderResult.Diagnostic;
	return { nullptr, EVexSchemaResolutionStatus::ProviderReturnedNoSchema, Diagnostic };
}

const FVexTypeSchema* FVexSymbolRegistry::GetSchemaForReflectedType(const Flight::Reflection::FTypeRegistry::FTypeInfo& TypeInfo)
{
	return ResolveSchemaForReflectedType(TypeInfo).Schema;
}

const FVexSymbolAccessor* FVexSymbolRegistry::FindSymbol(const void* TypeKey, const FString& Name) const
{
	if (const FVexTypeSchema* Schema = GetSchema(TypeKey))
	{
		return Schema->FindAccessor(Name);
	}
	return nullptr;
}

const FVexSymbolRecord* FVexSymbolRegistry::FindSymbolRecord(const void* TypeKey, const FString& Name) const
{
	if (const FVexTypeSchema* Schema = GetSchema(TypeKey))
	{
		return Schema->FindSymbolRecord(Name);
	}
	return nullptr;
}

} // namespace Flight::Vex
