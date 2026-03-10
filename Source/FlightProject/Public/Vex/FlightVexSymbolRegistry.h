// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexTypes.h"
#include "Swarm/SwarmSimulationTypes.h"

namespace Flight::Vex
{

/**
 * FVexSymbolAccessor: Handle for a specific symbol's Get/Set logic.
 */
struct FVexSymbolAccessor
{
	using GetterFn = TFunction<float(const Flight::Swarm::FDroidState&)>;
	using SetterFn = TFunction<void(Flight::Swarm::FDroidState&, float)>;

	FString SymbolName;
	GetterFn Getter;
	SetterFn Setter;
};

/**
 * FVexSymbolRegistry: Manages the mapping of VEX @symbols to C++ FDroidState fields.
 */
class FLIGHTPROJECT_API FVexSymbolRegistry
{
public:
	static FVexSymbolRegistry& Get();

	/** Register a new symbol mapping. */
	void RegisterSymbol(const FVexSymbolAccessor& Accessor);

	/** Find an accessor by name. */
	const FVexSymbolAccessor* FindSymbol(const FString& Name) const;

	/** Get all registered symbols. */
	const TMap<FString, FVexSymbolAccessor>& GetSymbols() const { return SymbolMap; }

private:
	TMap<FString, FVexSymbolAccessor> SymbolMap;
};

} // namespace Flight::Vex
