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

const FVexTypeSchema* FVexSymbolRegistry::GetSchema(const void* TypeKey) const
{
	return Schemas.Find(TypeKey);
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
