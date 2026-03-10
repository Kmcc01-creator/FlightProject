// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSymbolRegistry.h"

namespace Flight::Vex
{

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

const FVexSymbolAccessor* FVexSymbolRegistry::FindSymbol(const void* TypeKey, const FString& Name) const
{
	if (const FVexTypeSchema* Schema = GetSchema(TypeKey))
	{
		return Schema->Symbols.Find(Name);
	}
	return nullptr;
}

} // namespace Flight::Vex
