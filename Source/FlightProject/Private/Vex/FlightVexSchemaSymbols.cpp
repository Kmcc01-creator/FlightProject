// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexParser.h"
#include "Schema/FlightRequirementSchema.h"
#include "Schema/FlightRequirementRegistry.h"

namespace Flight::Vex
{

TArray<FVexSymbolDefinition> BuildSymbolDefinitionsFromManifest(const Flight::Schema::FManifestData& ManifestData)
{
	TArray<FVexSymbolDefinition> Definitions;
	{
		FVexSymbolDefinition DeltaTime;
		DeltaTime.SymbolName = TEXT("@dt");
		DeltaTime.ValueType = TEXT("float");
		DeltaTime.Residency = EFlightVexSymbolResidency::Shared;
		DeltaTime.bWritable = false;
		DeltaTime.bRequired = false;
		Definitions.Add(DeltaTime);

		FVexSymbolDefinition Frame;
		Frame.SymbolName = TEXT("@frame");
		Frame.ValueType = TEXT("int");
		Frame.Residency = EFlightVexSymbolResidency::Shared;
		Frame.bWritable = false;
		Frame.bRequired = false;
		Definitions.Add(Frame);

		FVexSymbolDefinition Time;
		Time.SymbolName = TEXT("@time");
		Time.ValueType = TEXT("float");
		Time.Residency = EFlightVexSymbolResidency::Shared;
		Time.bWritable = false;
		Time.bRequired = false;
		Definitions.Add(Time);
	}

	for (const FFlightVexSymbolRow& Row : ManifestData.VexSymbolRequirements)
	{
		FVexSymbolDefinition Definition;
		Definition.SymbolName = Row.SymbolName;
		switch (Row.ValueType)
		{
		case EFlightVexSymbolValueType::Float: Definition.ValueType = TEXT("float"); break;
		case EFlightVexSymbolValueType::Float2: Definition.ValueType = TEXT("float2"); break;
		case EFlightVexSymbolValueType::Float3: Definition.ValueType = TEXT("float3"); break;
		case EFlightVexSymbolValueType::Float4: Definition.ValueType = TEXT("float4"); break;
		case EFlightVexSymbolValueType::Int: Definition.ValueType = TEXT("int"); break;
		case EFlightVexSymbolValueType::Bool: Definition.ValueType = TEXT("bool"); break;
		default: Definition.ValueType = TEXT("unknown"); break;
		}

		Definition.Residency = Row.Residency;
		Definition.Affinity = Row.Affinity;
		Definition.bWritable = Row.bWritable;
		Definition.bRequired = Row.bRequired;
		Definition.bSimdReadAllowed = Row.bSimdReadAllowed;
		Definition.bSimdWriteAllowed = Row.bSimdWriteAllowed;
		Definition.bGpuTier1Allowed = Row.bGpuTier1Allowed;
		Definition.AlignmentRequirement = Row.AlignmentRequirement;
		Definition.MathDeterminismProfile = Row.MathDeterminismProfile;
		Definitions.Add(MoveTemp(Definition));
	}

	return Definitions;
}

} // namespace Flight::Vex
