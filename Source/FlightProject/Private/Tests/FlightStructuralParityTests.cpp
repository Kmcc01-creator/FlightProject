// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightReflection.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexSchemaIr.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Swarm/SwarmSimulationTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FFlightStructuralParityTest
 * 
 * AUTOMATED GENERATIVE TEST:
 * Discovers ALL reflectable types in the project via FTypeRegistry
 * and verifies their VEX mapping, schema consistency, and HLSL lowering.
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FFlightStructuralParityTest, "FlightProject.Integration.Generative.StructuralParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{

FString DescribeVexCapability(const Flight::Reflection::EVexCapability Capability)
{
	switch (Capability)
	{
	case Flight::Reflection::EVexCapability::NotVexCapable:
		return TEXT("NotVexCapable");
	case Flight::Reflection::EVexCapability::VexCapableAuto:
		return TEXT("VexCapableAuto");
	case Flight::Reflection::EVexCapability::VexCapableManual:
		return TEXT("VexCapableManual");
	default:
		return TEXT("Unknown");
	}
}

FString DescribeResolutionStatus(const Flight::Vex::EVexSchemaResolutionStatus Status)
{
	switch (Status)
	{
	case Flight::Vex::EVexSchemaResolutionStatus::ResolvedExisting:
		return TEXT("ResolvedExisting");
	case Flight::Vex::EVexSchemaResolutionStatus::ResolvedAfterProvider:
		return TEXT("ResolvedAfterProvider");
	case Flight::Vex::EVexSchemaResolutionStatus::NotVexCapable:
		return TEXT("NotVexCapable");
	case Flight::Vex::EVexSchemaResolutionStatus::MissingProvider:
		return TEXT("MissingProvider");
	case Flight::Vex::EVexSchemaResolutionStatus::ProviderFailed:
		return TEXT("ProviderFailed");
	case Flight::Vex::EVexSchemaResolutionStatus::ProviderReturnedNoSchema:
		return TEXT("ProviderReturnedNoSchema");
	default:
		return TEXT("Unknown");
	}
}

FString JoinErrors(const TArray<FString>& Errors)
{
	return Errors.IsEmpty() ? FString() : FString::Join(Errors, TEXT("; "));
}

} // namespace

void FFlightStructuralParityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	TArray<FName> TypeNames;
	Flight::Reflection::FTypeRegistry::Get().GetAllTypes(TypeNames);

	for (const FName& Name : TypeNames)
	{
		OutBeautifiedNames.Add(Name.ToString());
		OutTestCommands.Add(Name.ToString());
	}
}

bool FFlightStructuralParityTest::RunTest(const FString& Parameters)
{
	using namespace Flight::Reflection;
	using namespace Flight::Vex;

	const FName TargetTypeName(*Parameters);
	const FTypeRegistry::FTypeInfo* Info = FTypeRegistry::Get().Find(TargetTypeName);
	
	if (!Info)
	{
		AddError(FString::Printf(TEXT("Type %s not found in registry"), *TargetTypeName.ToString()));
		return false;
	}

	if (Info->VexCapability == EVexCapability::NotVexCapable)
	{
		AddInfo(FString::Printf(TEXT("[%s] Skipping reflected type with VEX capability %s"),
			*TargetTypeName.ToString(),
			*DescribeVexCapability(Info->VexCapability)));
		return true;
	}

	const FVexSchemaResolutionResult Resolution = FVexSymbolRegistry::Get().ResolveSchemaForReflectedType(*Info);
	if (!Resolution.Schema)
	{
		AddError(FString::Printf(TEXT("[%s] Failed to resolve schema for reflected type with VEX capability %s (%s): %s"),
			*TargetTypeName.ToString(),
			*DescribeVexCapability(Info->VexCapability),
			*DescribeResolutionStatus(Resolution.Status),
			Resolution.Diagnostic.IsEmpty() ? TEXT("No diagnostic provided.") : *Resolution.Diagnostic));
		return false;
	}

	const FVexTypeSchema* Schema = Resolution.Schema;

	if (Schema->SymbolRecords.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("[%s] Reflected type resolved as %s but produced no VEX symbols"),
			*Schema->TypeName,
			*DescribeVexCapability(Info->VexCapability)));
		return true;
	}

	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
	const TArray<FVexSymbolDefinition> AmbientDefinitions = BuildSymbolDefinitionsFromManifest(Manifest);
	const TArray<FVexSymbolDefinition> Definitions = FVexSchemaBinder::BuildSymbolDefinitions(*Schema, AmbientDefinitions);

	for (const auto& Pair : Schema->SymbolRecords)
	{
		const FVexSymbolRecord& Record = Pair.Value;
		const FString SymbolName = Record.SymbolName;
		if (SymbolName.IsEmpty())
		{
			continue;
		}
		
		const FString VexSource = FString::Printf(TEXT("@rate(1Hz) { auto _probe = %s; }\n"), *SymbolName);
		
		const FVexParseResult Result = ParseAndValidate(VexSource, Definitions, false);
		TestTrue(FString::Printf(TEXT("[%s] Generative VEX probe for %s should parse"), *Schema->TypeName, *SymbolName), Result.bSuccess);
		
		if (Result.bSuccess)
		{
			const FVexSchemaBindingResult Binding = FVexSchemaBinder::BindProgram(Result.Program, *Schema, AmbientDefinitions);
			TestTrue(
				FString::Printf(TEXT("[%s] Probe for %s should bind to schema"), *Schema->TypeName, *SymbolName),
				Binding.bSuccess);

			if (!Binding.bSuccess)
			{
				AddError(FString::Printf(TEXT("[%s] Schema binding failed for %s: %s"), *Schema->TypeName, *SymbolName, *JoinErrors(Binding.Errors)));
				continue;
			}

			const FString Hlsl = LowerToHLSL(Result.Program, Binding.SymbolDefinitions, Binding.BuildHlslSymbolMap());
			if (!Record.BackendBinding.HlslIdentifier.IsEmpty())
			{
				TestTrue(FString::Printf(TEXT("[%s] Lowered HLSL for %s should contain identifier %s"), *Schema->TypeName, *SymbolName, *Record.BackendBinding.HlslIdentifier),
					Hlsl.Contains(Record.BackendBinding.HlslIdentifier));
			}
		}
	}

	return true;
}

#endif
