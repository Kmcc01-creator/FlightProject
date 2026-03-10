// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Core/FlightReflection.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexSchemaIr.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Verse/UFlightVerseSubsystem.h"

namespace
{

struct FSchemaIrBindingState
{
	float Health = 10.0f;
	bool bEnabled = true;

	FLIGHT_REFLECT_BODY(FSchemaIrBindingState);
};

} // namespace

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(FSchemaIrBindingState,
	FLIGHT_FIELD_ATTR(
		float,
		Health,
		::Flight::Reflection::Attr::VexSymbol<"@health">,
		::Flight::Reflection::Attr::VerseIdentifier<"state.health">,
		::Flight::Reflection::Attr::HlslIdentifier<"HealthValue">
	),
	FLIGHT_FIELD_ATTR(
		bool,
		bEnabled,
		::Flight::Reflection::Attr::VexSymbol<"@enabled">,
		::Flight::Reflection::Attr::VerseIdentifier<"state.enabled">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::CpuOnly>
	)
)

} // namespace Flight::Reflection

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexSchemaIrBindingTest,
	"FlightProject.Vex.Schema.SchemaIr.Binding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexSchemaIrCompileIntegrationTest,
	"FlightProject.Vex.Schema.SchemaIr.CompileIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexSchemaIrBindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace Flight::Vex;

	static uint8 TypeKeyMarker = 0;
	const void* TypeKey = &TypeKeyMarker;
	const FVexTypeSchema Schema = FVexSchemaOrchestrator::BuildSchemaFromReflection<FSchemaIrBindingState>(TypeKey);
	const TArray<FVexSymbolDefinition> Definitions = FVexSchemaBinder::BuildSymbolDefinitions(Schema);

	const FString Source = TEXT("@health += 1.0; if (@enabled) { @health = @health + 2.0; }");
	const FVexParseResult ParseResult = ParseAndValidate(Source, Definitions);
	TestTrue(TEXT("SchemaIR binding test source should parse successfully"), ParseResult.bSuccess);
	if (!ParseResult.bSuccess)
	{
		return false;
	}

	const FVexSchemaBindingResult Binding = FVexSchemaBinder::BindProgram(ParseResult.Program, Schema);
	TestTrue(TEXT("SchemaIR binder should succeed for reflected schema symbols"), Binding.bSuccess);
	TestEqual(TEXT("SchemaIR binder should expose one binding record per statement"), Binding.StatementBindings.Num(), ParseResult.Program.Statements.Num());
	TestTrue(TEXT("SchemaIR binder should record reads of @enabled"), Binding.ReadSymbols.Contains(TEXT("@enabled")));
	TestTrue(TEXT("SchemaIR binder should record both read and write usage for @health"), Binding.ReadSymbols.Contains(TEXT("@health")) && Binding.WrittenSymbols.Contains(TEXT("@health")));

	const FVexSchemaBoundSymbol* Health = Binding.FindBoundSymbolByName(TEXT("@health"));
	TestNotNull(TEXT("SchemaIR binder should resolve @health"), Health);
	if (Health)
	{
		TestEqual(TEXT("@health should preserve its verse backend binding"), Health->LogicalSymbol.BackendBinding.VerseIdentifier, FString(TEXT("state.health")));
		TestEqual(TEXT("@health should preserve its HLSL backend binding"), Health->LogicalSymbol.BackendBinding.HlslIdentifier, FString(TEXT("HealthValue")));
	}

	const TMap<FString, FString> VerseMap = Binding.BuildVerseSymbolMap();
	const FString* VerseHealth = VerseMap.Find(TEXT("@health"));
	TestNotNull(TEXT("SchemaIR binder should build a verse symbol map entry for @health"), VerseHealth);
	if (VerseHealth)
	{
		TestEqual(TEXT("Verse map should use the reflected backend identifier"), *VerseHealth, FString(TEXT("state.health")));
	}

	const FVexSchemaStatementBinding& CompoundAssignmentBinding = Binding.StatementBindings[0];
	TestTrue(TEXT("Compound assignment should read @health"), CompoundAssignmentBinding.ReadSymbols.Num() > 0);
	TestTrue(TEXT("Compound assignment should write @health"), CompoundAssignmentBinding.WrittenSymbols.Num() > 0);

	return true;
}

bool FFlightVexSchemaIrCompileIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!TestNotNull(TEXT("SchemaIR compile integration test requires an editor world"), World))
	{
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!TestNotNull(TEXT("SchemaIR compile integration test requires the verse subsystem"), VerseSubsystem))
	{
		return false;
	}

	Flight::Vex::TTypeVexRegistry<FSchemaIrBindingState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<FSchemaIrBindingState>::GetTypeKey();
	const uint32 BehaviorID = 19031;
	FString Errors;
	const bool bCompiled = VerseSubsystem->CompileVex(BehaviorID, TEXT("@health = 42.0;"), Errors, TypeKey);
	TestTrue(TEXT("SchemaIR compile integration should compile reflected schema source"), bCompiled);
	if (!bCompiled)
	{
		AddError(Errors);
		return false;
	}

	const Flight::Vex::FFlightCompileArtifactReport* Report = VerseSubsystem->GetBehaviorCompileArtifactReport(BehaviorID);
	TestNotNull(TEXT("SchemaIR compile integration should emit a compile artifact report"), Report);
	if (Report)
	{
		TestTrue(TEXT("Generated Verse should use the reflected backend identifier"), Report->GeneratedVerseCode.Contains(TEXT("state.health")));
	}

	return true;
}
