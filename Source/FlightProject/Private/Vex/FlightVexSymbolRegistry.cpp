// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSymbolRegistry.h"

namespace Flight::Vex
{

FVexSymbolRegistry& FVexSymbolRegistry::Get()
{
	static FVexSymbolRegistry Instance;
	return Instance;
}

void FVexSymbolRegistry::RegisterSymbol(const FVexSymbolAccessor& Accessor)
{
	SymbolMap.Add(Accessor.SymbolName, Accessor);
}

const FVexSymbolAccessor* FVexSymbolRegistry::FindSymbol(const FString& Name) const
{
	return SymbolMap.Find(Name);
}

} // namespace Flight::Vex
