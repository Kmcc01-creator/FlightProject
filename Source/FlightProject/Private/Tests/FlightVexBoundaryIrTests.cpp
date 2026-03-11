// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexSchemaIr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexBoundaryImportBindingTest,
	"FlightProject.Vex.Schema.Boundary.ImportBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexBoundaryExportBindingTest,
	"FlightProject.Vex.Schema.Boundary.ExportBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexBoundaryIrRejectsBoundaryOperatorsTest,
	"FlightProject.Vex.Schema.Boundary.IrRejectsBoundaryOperators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{

Flight::Vex::FVexTypeSchema BuildBoundarySchema()
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	static uint8 TypeKeyMarker = 0;
	Schema.TypeId.RuntimeKey = &TypeKeyMarker;
	Schema.TypeId.StableName = FName(TEXT("FBoundaryAwareSchema"));
	Schema.TypeName = TEXT("FBoundaryAwareSchema");

	FVexSymbolRecord Velocity;
	Velocity.SymbolName = TEXT("@velocity");
	Velocity.ValueType = EVexValueType::Float3;
	Velocity.bReadable = true;
	Velocity.bWritable = true;
	Velocity.BackendBinding.HlslIdentifier = TEXT("VelocityValue");
	Schema.SymbolRecords.Add(Velocity.SymbolName, Velocity);

	FVexSymbolRecord HitDist;
	HitDist.SymbolName = TEXT("@hit_dist");
	HitDist.ValueType = EVexValueType::Float;
	HitDist.bReadable = true;
	HitDist.bWritable = false;
	HitDist.BackendBinding.HlslIdentifier = TEXT("HitDistance");
	Schema.SymbolRecords.Add(HitDist.SymbolName, HitDist);

	FVexSymbolRecord Status;
	Status.SymbolName = TEXT("@status");
	Status.ValueType = EVexValueType::Int;
	Status.bReadable = true;
	Status.bWritable = true;
	Status.BackendBinding.HlslIdentifier = TEXT("StatusValue");
	Schema.SymbolRecords.Add(Status.SymbolName, Status);

	Schema.RebuildLegacyViews();
	return Schema;
}

} // namespace

bool FFlightVexBoundaryImportBindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace Flight::Vex;

	const FVexTypeSchema Schema = BuildBoundarySchema();
	const TArray<FVexSymbolDefinition> Definitions = FVexSchemaBinder::BuildSymbolDefinitions(Schema);
	const FString Source = TEXT("@gpu { @velocity <| vec3(1,0,0); }\n");

	const FVexParseResult ParseResult = ParseAndValidate(Source, Definitions);
	TestTrue(TEXT("Boundary import source should parse successfully"), ParseResult.bSuccess);
	if (!ParseResult.bSuccess)
	{
		return false;
	}

	const FVexSchemaBindingResult Binding = FVexSchemaBinder::BindProgram(ParseResult.Program, Schema);
	TestTrue(TEXT("Boundary import source should bind successfully"), Binding.bSuccess);
	TestTrue(TEXT("Boundary import source should record boundary operators"), Binding.bHasBoundaryOperators);
	TestEqual(TEXT("Boundary import source should produce one boundary use"), Binding.BoundaryUses.Num(), 1);

	if (Binding.BoundaryUses.Num() != 1)
	{
		return false;
	}

	const FVexSchemaBoundaryUse& BoundaryUse = Binding.BoundaryUses[0];
	TestEqual(TEXT("Boundary import should preserve PipeIn operator kind"), BoundaryUse.OperatorKind, EVexExprKind::PipeIn);
	TestEqual(TEXT("Boundary import should classify direction as import"), BoundaryUse.Metadata.Direction, EVexBoundaryDirection::Import);
	TestEqual(TEXT("Boundary import should classify payload as symbol-backed"), BoundaryUse.Metadata.PayloadKind, EVexBoundaryPayloadKind::Symbol);
	TestEqual(TEXT("Boundary import should bind destination symbol"), BoundaryUse.DestinationSymbolName, FString(TEXT("@velocity")));
	TestTrue(TEXT("Boundary import summary should include destination symbol"), Binding.ImportedSymbols.Contains(TEXT("@velocity")));

	return true;
}

bool FFlightVexBoundaryExportBindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace Flight::Vex;

	const FVexTypeSchema Schema = BuildBoundarySchema();
	const TArray<FVexSymbolDefinition> Definitions = FVexSchemaBinder::BuildSymbolDefinitions(Schema);
	const FString Source = TEXT("@cpu { int count = @hit_dist |> count_hits(); @status = count; }\n");

	const FVexParseResult ParseResult = ParseAndValidate(Source, Definitions);
	TestTrue(TEXT("Boundary export source should parse successfully"), ParseResult.bSuccess);
	if (!ParseResult.bSuccess)
	{
		return false;
	}

	const FVexSchemaBindingResult Binding = FVexSchemaBinder::BindProgram(ParseResult.Program, Schema);
	TestTrue(TEXT("Boundary export source should bind successfully"), Binding.bSuccess);
	TestTrue(TEXT("Boundary export source should record boundary operators"), Binding.bHasBoundaryOperators);
	TestEqual(TEXT("Boundary export source should produce one boundary use"), Binding.BoundaryUses.Num(), 1);

	if (Binding.BoundaryUses.Num() != 1)
	{
		return false;
	}

	const FVexSchemaBoundaryUse& BoundaryUse = Binding.BoundaryUses[0];
	TestEqual(TEXT("Boundary export should preserve PipeOut operator kind"), BoundaryUse.OperatorKind, EVexExprKind::PipeOut);
	TestEqual(TEXT("Boundary export should classify direction as export"), BoundaryUse.Metadata.Direction, EVexBoundaryDirection::Export);
	TestEqual(TEXT("Boundary export should classify payload as symbol-backed"), BoundaryUse.Metadata.PayloadKind, EVexBoundaryPayloadKind::Symbol);
	TestEqual(TEXT("Boundary export should bind source symbol"), BoundaryUse.SourceSymbolName, FString(TEXT("@hit_dist")));
	TestTrue(TEXT("Boundary export summary should include source symbol"), Binding.ExportedSymbols.Contains(TEXT("@hit_dist")));

	return true;
}

bool FFlightVexBoundaryIrRejectsBoundaryOperatorsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace Flight::Vex;

	const FVexTypeSchema Schema = BuildBoundarySchema();
	const TArray<FVexSymbolDefinition> Definitions = FVexSchemaBinder::BuildSymbolDefinitions(Schema);
	const FString Source = TEXT("@gpu { @velocity <| vec3(1,0,0); }\n");

	const FVexParseResult ParseResult = ParseAndValidate(Source, Definitions);
	TestTrue(TEXT("Boundary IR rejection source should parse successfully"), ParseResult.bSuccess);
	if (!ParseResult.bSuccess)
	{
		return false;
	}

	FString Errors;
	const TSharedPtr<FVexIrProgram> Program = FVexIrCompiler::Compile(ParseResult.Program, Definitions, Errors);
	TestFalse(TEXT("Boundary operators should not lower into low-level IR yet"), Program.IsValid());
	TestTrue(TEXT("Boundary IR rejection should explain why lowering failed"), Errors.Contains(TEXT("boundary operators")));

	return true;
}
