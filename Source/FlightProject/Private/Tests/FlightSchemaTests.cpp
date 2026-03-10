// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Schema/FlightRequirementSchema.h"
#include "FlightScriptingLibrary.h"
#include "Core/FlightHlslReflection.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
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

	const TArray<TSharedPtr<FJsonValue>>* GpuContracts = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("gpuResourceContracts"), GpuContracts))
	{
		AddError(TEXT("Manifest missing gpuResourceContracts array"));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* GpuAccessRules = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("gpuAccessRules"), GpuAccessRules))
	{
		AddError(TEXT("Manifest missing gpuAccessRules array"));
		return false;
	}

	TSet<FString> Symbols;
	TMap<FString, FString> ValueTypeBySymbol;
	for (const auto& Value : *VexRequirements)
	{
		const TSharedPtr<FJsonObject> Requirement = Value->AsObject();
		if (!Requirement.IsValid())
		{
			continue;
		}

		const FString SymbolName = Requirement->GetStringField(TEXT("symbolName"));
		Symbols.Add(SymbolName);
		ValueTypeBySymbol.Add(SymbolName, Requirement->GetStringField(TEXT("valueType")));
	}

	TestTrue(TEXT("Manifest should declare @position symbol"), Symbols.Contains(TEXT("@position")));
	TestTrue(TEXT("Manifest should declare @velocity symbol"), Symbols.Contains(TEXT("@velocity")));
	TestTrue(TEXT("Manifest should declare @shield symbol"), Symbols.Contains(TEXT("@shield")));
	TestTrue(TEXT("Manifest should declare @status symbol"), Symbols.Contains(TEXT("@status")));
	TestEqual(TEXT("@status should map to int valueType"), ValueTypeBySymbol.FindRef(TEXT("@status")), FString(TEXT("int")));

	TestEqual(TEXT("Manifest should expose one reflected GPU resource contract"), GpuContracts->Num(), 1);
	if (GpuContracts->Num() == 1)
	{
		const TSharedPtr<FJsonObject> Contract = (*GpuContracts)[0]->AsObject();
		TestTrue(TEXT("GPU contract object should be valid"), Contract.IsValid());
		if (Contract.IsValid())
		{
			TestEqual(TEXT("GPU contract should preserve the reflected resource id"), Contract->GetStringField(TEXT("resourceId")), FString(TEXT("Swarm.DroidStateBuffer")));
			TestEqual(TEXT("GPU contract should prefer Unreal RDG"), Contract->GetBoolField(TEXT("preferUnrealRdg")), true);
			TestEqual(TEXT("GPU contract should not require raw Vulkan interop"), Contract->GetBoolField(TEXT("requiresRawVulkanInterop")), false);
		}
	}

	TestEqual(TEXT("Manifest should expose four reflected GPU access rules"), GpuAccessRules->Num(), 4);

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

	const FString ResourceContractInclude = Flight::Reflection::HLSL::GenerateGpuResourceContractInclude<Flight::Swarm::FDroidState>(
		TEXT("FDROIDSTATE"));
	TestTrue(TEXT("Generated GPU resource include should preserve the resource id"), ResourceContractInclude.Contains(TEXT("// GPU Resource Contract: Swarm.DroidStateBuffer")));
	TestTrue(TEXT("Generated GPU resource include should preserve the access-rule count"), ResourceContractInclude.Contains(TEXT("#define FDROIDSTATE_RESOURCE_ACCESS_RULE_COUNT 4")));

	const FString ProjectGeneratedInclude = UFlightScriptingLibrary::GetGeneratedGpuResourceContractHlsl();
	TestTrue(TEXT("Project-level generated GPU contract include should preserve the resource id"), ProjectGeneratedInclude.Contains(TEXT("// GPU Resource Contract: Swarm.DroidStateBuffer")));
	TestTrue(TEXT("Project-level generated GPU contract include should expose the float4 stride macro"), ProjectGeneratedInclude.Contains(TEXT("#define FDROIDSTATE_RESOURCE_ELEMENT_STRIDE_FLOAT4 2")));

	const TOptional<Flight::Schema::FFlightGpuStructuredBufferContract> StructuredContract =
		Flight::Schema::ResolveStructuredBufferContract(TEXT("Swarm.DroidStateBuffer"));
	TestTrue(TEXT("Structured GPU contract should resolve for the swarm droid state buffer"), StructuredContract.IsSet());
	if (StructuredContract.IsSet())
	{
		TestEqual(TEXT("Structured GPU contract should match native FDroidState stride"), StructuredContract->ElementStrideBytes, static_cast<uint32>(sizeof(Flight::Swarm::FDroidState)));
		TestEqual(TEXT("Structured GPU contract should expose transfer-destination support"), StructuredContract->bSupportsTransferDestination, true);
		TestEqual(TEXT("Structured GPU contract should expose shader-read support"), StructuredContract->bSupportsShaderRead, true);
		TestEqual(TEXT("Structured GPU contract should expose shader-write support"), StructuredContract->bSupportsShaderWrite, true);
	}

	const TArray<FString> ReflectedGpuIssues = UFlightScriptingLibrary::ValidateReflectedGpuContracts();
	TestEqual(TEXT("Reflected GPU contracts should validate cleanly"), ReflectedGpuIssues.Num(), 0);

	const FString ExportedContractPath = UFlightScriptingLibrary::ExportGeneratedGpuResourceContracts(
		TEXT("Saved/Flight/Schema/test_gpu_resource_contracts.ush"));
	TestFalse(TEXT("Project-level generated GPU contract export should return a path"), ExportedContractPath.IsEmpty());
	if (!ExportedContractPath.IsEmpty())
	{
		FString ExportedSource;
		TestTrue(TEXT("Exported GPU contract include should be readable"), FFileHelper::LoadFileToString(ExportedSource, *ExportedContractPath));
		TestTrue(TEXT("Exported GPU contract include should contain the stride macro"), ExportedSource.Contains(TEXT("#define FDROIDSTATE_RESOURCE_ELEMENT_STRIDE_FLOAT4 2")));
	}

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
