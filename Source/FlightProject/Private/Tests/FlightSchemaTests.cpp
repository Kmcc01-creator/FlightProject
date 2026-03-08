// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Schema/FlightRequirementSchema.h"
#include "FlightScriptingLibrary.h"
#include "Core/FlightHlslReflection.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSchemaVexManifestValidationTest,
	"FlightProject.Schema.Vex.ManifestValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSchemaVexManifestValidationTest::RunTest(const FString&)
{
	const FString ManifestJson = Flight::Schema::BuildManifestJson();
	
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestJson);
	
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		AddError(TEXT("Failed to deserialize manifest JSON"));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* VexRequirements;
	if (!RootObject->TryGetArrayField(TEXT("vexSymbolRequirements"), VexRequirements))
	{
		AddError(TEXT("Manifest missing vexSymbolRequirements array"));
		return false;
	}

	TSet<FString> Symbols;
	for (const auto& Value : *VexRequirements)
	{
		const TSharedPtr<FJsonObject> Requirement = Value->AsObject();
		if (!Requirement.IsValid())
		{
			continue;
		}

		Symbols.Add(Requirement->GetStringField(TEXT("symbolName")));
	}

	TestTrue(TEXT("Manifest should declare @position symbol"), Symbols.Contains(TEXT("@position")));
	TestTrue(TEXT("Manifest should declare @velocity symbol"), Symbols.Contains(TEXT("@velocity")));
	TestTrue(TEXT("Manifest should declare @shield symbol"), Symbols.Contains(TEXT("@shield")));
	TestTrue(TEXT("Manifest should declare @status symbol"), Symbols.Contains(TEXT("@status")));

	const FString LayoutTokenName = TEXT("FLIGHT_DROIDSTATE_LAYOUT_HASH");
	const FString ReflectedHlsl = Flight::Reflection::HLSL::GenerateStructContractHLSL<Flight::Swarm::FDroidState>(
		TEXT("FDroidState"),
		LayoutTokenName);
	TestTrue(TEXT("Reflected HLSL should contain FDroidState struct"), ReflectedHlsl.Contains(TEXT("struct FDroidState")));
	TestTrue(TEXT("Reflected HLSL should contain Position field"), ReflectedHlsl.Contains(TEXT("float3 Position;")));
	TestTrue(TEXT("Reflected HLSL should contain Shield field"), ReflectedHlsl.Contains(TEXT("float Shield;")));
	TestTrue(TEXT("Reflected HLSL should contain Velocity field"), ReflectedHlsl.Contains(TEXT("float3 Velocity;")));
	TestTrue(TEXT("Reflected HLSL should contain Status field"), ReflectedHlsl.Contains(TEXT("uint Status;")));

	const uint32 ExpectedLayoutHash = Flight::Reflection::HLSL::ComputeLayoutHash<Flight::Swarm::FDroidState>();
	uint32 ParsedLayoutHash = 0;
	const bool bExtracted = Flight::Reflection::HLSL::ExtractLayoutHashFromContract(
		ReflectedHlsl,
		LayoutTokenName,
		ParsedLayoutHash);
	TestTrue(TEXT("Reflected HLSL should contain layout hash token"), bExtracted);
	TestEqual(TEXT("Reflected HLSL hash should match FDroidState layout hash"), ParsedLayoutHash, ExpectedLayoutHash);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSchemaCVarContractMissingVariableTest,
	"FlightProject.Schema.CVar.MissingVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSchemaCVarContractMissingVariableTest::RunTest(const FString&)
{
	// Correct call: CVarName, ExpectedValue, ValueType
	const TArray<FString> Issues = UFlightScriptingLibrary::ValidateCVarContract(
		TEXT("r.NonExistentCVar_FP"),
		TEXT("1"),
		EFlightCVarValueType::String);

	TestTrue(TEXT("Should report issue for missing CVar"), Issues.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSchemaNiagaraContractValidationTest,
	"FlightProject.Schema.Niagara.ContractValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSchemaNiagaraContractValidationTest::RunTest(const FString&)
{
	TArray<FString> RequiredParams;
	TArray<FString> RequiredDIs = {TEXT("/Script/FlightProject.NiagaraDataInterfaceSwarm")};

	const TArray<FString> Issues = UFlightScriptingLibrary::ValidateNiagaraSystemContract(
		TEXT("/Game/DoesNotExist/NS_Missing.NS_Missing"),
		RequiredParams,
		RequiredDIs);

	TestTrue(TEXT("Should report issues for non-existent Niagara system"), Issues.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSchemaPluginContractValidationTest,
	"FlightProject.Schema.Plugin.ContractValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSchemaPluginContractValidationTest::RunTest(const FString&)
{
	const TArray<FString> Issues = UFlightScriptingLibrary::DiffPluginRequirementsForProfile(
		TEXT("PluginNegativeTest"));

	TestTrue(TEXT("PluginNegativeTest should report at least one plugin issue"), Issues.Num() > 0);
	return true;
}
