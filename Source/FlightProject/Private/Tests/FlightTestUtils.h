// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexParser.h"
#include "Schema/FlightRequirementRegistry.h"

namespace Flight::Test
{
	/** Setup standard symbol definitions for testing */
	inline TArray<Vex::FVexSymbolDefinition> GetMockSymbols()
	{
		TArray<Vex::FVexSymbolDefinition> Defs;
		
		Vex::FVexSymbolDefinition Pos;
		Pos.SymbolName = TEXT("@position");
		Pos.ValueType = TEXT("float3");
		Pos.Residency = EFlightVexSymbolResidency::Shared;
		Pos.Affinity = EFlightVexSymbolAffinity::Any;
		Pos.bWritable = false;
		Defs.Add(Pos);

		Vex::FVexSymbolDefinition Vel;
		Vel.SymbolName = TEXT("@velocity");
		Vel.ValueType = TEXT("float3");
		Vel.Residency = EFlightVexSymbolResidency::Shared;
		Vel.Affinity = EFlightVexSymbolAffinity::WorkerThread;
		Vel.bWritable = true;
		Defs.Add(Vel);

		Vex::FVexSymbolDefinition Shield;
		Shield.SymbolName = TEXT("@shield");
		Shield.ValueType = TEXT("float");
		Shield.Residency = EFlightVexSymbolResidency::GpuOnly;
		Shield.Affinity = EFlightVexSymbolAffinity::Any;
		Shield.bWritable = true;
		Defs.Add(Shield);

		Vex::FVexSymbolDefinition GtData;
		GtData.SymbolName = TEXT("@uobject_data");
		GtData.ValueType = TEXT("float");
		GtData.Affinity = EFlightVexSymbolAffinity::GameThread;
		Defs.Add(GtData);

		return Defs;
	}

	/** Helper to parse VEX and assert success */
	inline Vex::FVexParseResult ParseChecked(const FString& Source, const TArray<Vex::FVexSymbolDefinition>& Defs)
	{
		return Vex::ParseAndValidate(Source, Defs, false);
	}
}
