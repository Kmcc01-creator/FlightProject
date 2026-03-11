// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mass/FlightMassFragments.h"
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

Flight::Vex::FVexTypeSchema BuildSchemaIrMassBindingSchema(const void* TypeKey)
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	Schema.TypeId.RuntimeKey = TypeKey;
	Schema.TypeId.StableName = FName(TEXT("FSchemaIrMassBindingState"));
	Schema.TypeName = TEXT("FSchemaIrMassBindingState");
	Schema.Size = sizeof(FFlightTransformFragment) + sizeof(FFlightDroidStateFragment);
	Schema.Alignment = alignof(FFlightTransformFragment);

	FVexSymbolRecord Position;
	Position.SymbolName = TEXT("@position");
	Position.ValueType = EVexValueType::Float3;
	Position.Storage.Kind = EVexStorageKind::MassFragmentField;
	Position.Storage.FragmentType = FName(TEXT("FFlightTransformFragment"));
	Position.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightTransformFragment, Location));
	Position.Storage.ElementStride = sizeof(FVector);
	Schema.SymbolRecords.Add(Position.SymbolName, MoveTemp(Position));

	FVexSymbolRecord Velocity;
	Velocity.SymbolName = TEXT("@velocity");
	Velocity.ValueType = EVexValueType::Float3;
	Velocity.Storage.Kind = EVexStorageKind::MassFragmentField;
	Velocity.Storage.FragmentType = FName(TEXT("FFlightTransformFragment"));
	Velocity.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightTransformFragment, Velocity));
	Velocity.Storage.ElementStride = sizeof(FVector);
	Schema.SymbolRecords.Add(Velocity.SymbolName, MoveTemp(Velocity));

	FVexSymbolRecord Shield;
	Shield.SymbolName = TEXT("@shield");
	Shield.ValueType = EVexValueType::Float;
	Shield.Storage.Kind = EVexStorageKind::MassFragmentField;
	Shield.Storage.FragmentType = FName(TEXT("FFlightDroidStateFragment"));
	Shield.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightDroidStateFragment, Shield));
	Shield.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(Shield.SymbolName, MoveTemp(Shield));

	Schema.RebuildLegacyViews();
	Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
	return Schema;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexSchemaIrFragmentBindingsTest,
	"FlightProject.Vex.Schema.SchemaIr.FragmentBindings",
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

	const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
	TestNotNull(TEXT("SchemaIR compile integration should persist bound behavior metadata"), Behavior);
	if (Behavior)
	{
		TestEqual(TEXT("Behavior should retain the bound runtime key"), Behavior->BoundTypeKey, TypeKey);
		TestEqual(TEXT("Behavior should retain the schema stable name"), Behavior->BoundTypeStableName, FName(TEXT("FSchemaIrBindingState")));
		TestTrue(TEXT("Behavior should retain the schema layout hash"), Behavior->BoundSchemaLayoutHash != 0);
		TestTrue(TEXT("Behavior should retain schema binding metadata"), Behavior->SchemaBinding.IsSet());
		if (Behavior->SchemaBinding.IsSet())
		{
			TestTrue(TEXT("Behavior schema binding should record @health as written"), Behavior->SchemaBinding->WrittenSymbols.Contains(TEXT("@health")));
		}
	}

	if (Report)
	{
		TestEqual(TEXT("Compile artifact should retain the bound type name"), Report->BoundTypeName, FString(TEXT("FSchemaIrBindingState")));
		TestTrue(TEXT("Compile artifact should retain the schema layout hash"), Report->SchemaLayoutHash != 0);
		TestTrue(TEXT("Compile artifact should retain @health in the written symbol set"), Report->WrittenSymbols.Contains(TEXT("@health")));
		TestTrue(TEXT("Compile artifact should retain referenced storage kinds"), Report->ReferencedStorageKinds.Num() > 0);
		TestFalse(TEXT("Compile artifact committed backend should not be empty"), Report->CommittedBackend.IsEmpty());
		TestTrue(TEXT("Generated Verse should use the reflected backend identifier"), Report->GeneratedVerseCode.Contains(TEXT("state.health")));
	}

	return true;
}

bool FFlightVexSchemaIrFragmentBindingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace Flight::Vex;

	static uint8 TypeKeyMarker = 0;
	const void* TypeKey = &TypeKeyMarker;
	const FVexTypeSchema Schema = BuildSchemaIrMassBindingSchema(TypeKey);
	const TArray<FVexSymbolDefinition> Definitions = FVexSchemaBinder::BuildSymbolDefinitions(Schema);

	const FString Source = TEXT("@velocity = @position; @shield += 1.0;");
	const FVexParseResult ParseResult = ParseAndValidate(Source, Definitions);
	TestTrue(TEXT("SchemaIR fragment-binding test source should parse successfully"), ParseResult.bSuccess);
	if (!ParseResult.bSuccess)
	{
		return false;
	}

	const FVexSchemaBindingResult Binding = FVexSchemaBinder::BindProgram(ParseResult.Program, Schema);
	TestTrue(TEXT("SchemaIR fragment-binding binder should succeed for Mass-backed schema symbols"), Binding.bSuccess);
	TestEqual(TEXT("SchemaIR fragment-binding table should expose one entry per referenced fragment"), Binding.FragmentBindings.Num(), 2);

	const FVexSchemaBoundSymbol* Position = Binding.FindBoundSymbolByName(TEXT("@position"));
	const FVexSchemaBoundSymbol* Velocity = Binding.FindBoundSymbolByName(TEXT("@velocity"));
	const FVexSchemaBoundSymbol* Shield = Binding.FindBoundSymbolByName(TEXT("@shield"));
	TestNotNull(TEXT("Fragment-binding table should resolve @position"), Position);
	TestNotNull(TEXT("Fragment-binding table should resolve @velocity"), Velocity);
	TestNotNull(TEXT("Fragment-binding table should resolve @shield"), Shield);

	const FVexSchemaFragmentBinding* TransformBinding = Binding.FindFragmentBindingByType(FName(TEXT("FFlightTransformFragment")));
	const FVexSchemaFragmentBinding* DroidBinding = Binding.FindFragmentBindingByType(FName(TEXT("FFlightDroidStateFragment")));
	TestNotNull(TEXT("Fragment-binding table should expose the transform fragment"), TransformBinding);
	TestNotNull(TEXT("Fragment-binding table should expose the droid-state fragment"), DroidBinding);

	if (TransformBinding)
	{
		TestEqual(TEXT("Transform fragment binding should retain Mass storage kind"), TransformBinding->StorageKind, EVexStorageKind::MassFragmentField);
		TestTrue(TEXT("Transform fragment binding should capture reads of @position"), TransformBinding->ReadSymbols.Contains(TEXT("@position")));
		TestTrue(TEXT("Transform fragment binding should capture writes of @velocity"), TransformBinding->WrittenSymbols.Contains(TEXT("@velocity")));
	}

	if (DroidBinding)
	{
		TestTrue(TEXT("Droid fragment binding should capture read/write access for compound assignment"), DroidBinding->ReadSymbols.Contains(TEXT("@shield")) && DroidBinding->WrittenSymbols.Contains(TEXT("@shield")));
	}

	if (Velocity)
	{
		TestTrue(TEXT("@velocity should point at a fragment-binding entry"), Velocity->FragmentBindingIndex != INDEX_NONE);
	}

	return true;
}
