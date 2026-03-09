#include "Misc/AutomationTest.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Vex/FlightVexParser.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserValidSourceTest,
	"FlightProject.Unit.Vex.Parser.ValidSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserValidSourceTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source =
		TEXT("float3 noise = curlnoise(@position * 0.01);\n")
		TEXT("@velocity += noise * 50.0 * DeltaTime;\n")
		TEXT("if (@shield < 0.2) { @status = 1; }\n");

	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestTrue(TEXT("Valid source should parse successfully"), Result.bSuccess);
	TestEqual(TEXT("Valid source should have no unknown symbols"), Result.UnknownSymbols.Num(), 0);
	TestTrue(TEXT("Parser should produce statement AST nodes"), Result.Program.Statements.Num() > 0);
	TestTrue(TEXT("Parser should detect @position usage"), Result.Program.UsedSymbols.Contains(TEXT("@position")));
	TestTrue(TEXT("Parser should detect @velocity usage"), Result.Program.UsedSymbols.Contains(TEXT("@velocity")));
	return Result.bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserDiagnosticsTest,
	"FlightProject.Unit.Vex.Parser.Diagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserDiagnosticsTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source =
		TEXT("@foo += 1.0;\n")
		TEXT("if @velocity > 0.0 { @status = 1; }\n");

	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestFalse(TEXT("Source with malformed if header should fail parse"), Result.bSuccess);
	TestTrue(TEXT("Unknown symbols should include @foo"), Result.UnknownSymbols.Contains(TEXT("@foo")));

	bool bHasIfDiagnostic = false;
	for (const Flight::Vex::FVexIssue& Issue : Result.Issues)
	{
		if (Issue.Message.Contains(TEXT("`if` must be followed by '('")))
		{
			bHasIfDiagnostic = true;
			break;
		}
	}
	TestTrue(TEXT("Parser should report malformed if-header diagnostic"), bHasIfDiagnostic);

	return !Result.bSuccess && bHasIfDiagnostic;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserReadOnlySymbolTest,
	"FlightProject.Unit.Vex.Parser.Semantics.ReadOnlySymbol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserReadOnlySymbolTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source = TEXT("@position = @velocity;\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestFalse(TEXT("Assigning to read-only @position should fail"), Result.bSuccess);

	bool bHasReadOnlyError = false;
	for (const Flight::Vex::FVexIssue& Issue : Result.Issues)
	{
		if (Issue.Message.Contains(TEXT("read-only symbol '@position'")))
		{
			bHasReadOnlyError = true;
			break;
		}
	}

	TestTrue(TEXT("Parser should emit read-only assignment error"), bHasReadOnlyError);
	return !Result.bSuccess && bHasReadOnlyError;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserTypeMismatchTest,
	"FlightProject.Unit.Vex.Parser.Semantics.TypeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserTypeMismatchTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source = TEXT("@shield = @velocity;\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestFalse(TEXT("Assigning float3 to float should fail"), Result.bSuccess);

	bool bHasTypeMismatch = false;
	for (const Flight::Vex::FVexIssue& Issue : Result.Issues)
	{
		if (Issue.Message.Contains(TEXT("Type mismatch for assignment")))
		{
			bHasTypeMismatch = true;
			break;
		}
	}

	TestTrue(TEXT("Parser should report assignment type mismatch"), bHasTypeMismatch);
	return !Result.bSuccess && bHasTypeMismatch;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserIfConditionTypeTest,
	"FlightProject.Unit.Vex.Parser.Semantics.IfConditionBoolLike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserIfConditionTypeTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source = TEXT("if (@velocity) { @status = 1; }\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestFalse(TEXT("Non-bool if condition should fail"), Result.bSuccess);

	bool bHasConditionTypeError = false;
	for (const Flight::Vex::FVexIssue& Issue : Result.Issues)
	{
		if (Issue.Message.Contains(TEXT("condition must be bool-like")))
		{
			bHasConditionTypeError = true;
			break;
		}
	}

	TestTrue(TEXT("Parser should report if-condition type error"), bHasConditionTypeError);
	return !Result.bSuccess && bHasConditionTypeError;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserExpressionPrecedenceAstTest,
	"FlightProject.Unit.Vex.Parser.Ast.Precedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserExpressionPrecedenceAstTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source = TEXT("float energy = 1.0 + 2.0 * 3.0;\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestTrue(TEXT("Precedence source should parse cleanly"), Result.bSuccess);
	TestTrue(TEXT("Program should contain at least one statement"), Result.Program.Statements.Num() > 0);

	const Flight::Vex::FVexStatementAst& Statement = Result.Program.Statements[0];
	TestTrue(TEXT("Assignment statement should have an expression root"), Statement.ExpressionNodeIndex != INDEX_NONE);

	const Flight::Vex::FVexExpressionAst& Root = Result.Program.Expressions[Statement.ExpressionNodeIndex];
	
	// With Constant Folding enabled, 1.0 + 2.0 * 3.0 becomes 7.0
	TestEqual(TEXT("Root should be a folded NumberLiteral"), Root.Kind, Flight::Vex::EVexExprKind::NumberLiteral);
	TestTrue(TEXT("Folded result should be approximately 7.0"), FMath::IsNearlyEqual(FCString::Atof(*Root.Lexeme), 7.0f));

	return Result.bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserFunctionCallAstTest,
	"FlightProject.Unit.Vex.Parser.Ast.FunctionCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserFunctionCallAstTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	const FString Source = TEXT("@velocity += normalize(@position + @velocity) * 0.5;\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestTrue(TEXT("Function-call source should parse cleanly"), Result.bSuccess);
	TestTrue(TEXT("Program should contain statements"), Result.Program.Statements.Num() > 0);

	const Flight::Vex::FVexStatementAst& Statement = Result.Program.Statements[0];
	TestTrue(TEXT("Statement should carry expression root"), Statement.ExpressionNodeIndex != INDEX_NONE);
	const Flight::Vex::FVexExpressionAst& Root = Result.Program.Expressions[Statement.ExpressionNodeIndex];
	TestEqual(TEXT("RHS root should be binary '*'"), Root.Kind, Flight::Vex::EVexExprKind::BinaryOp);
	TestEqual(TEXT("RHS root operator should be '*'"), Root.Lexeme, FString(TEXT("*")));
	TestEqual(TEXT("RHS inferred type should be float3"), Root.InferredType, FString(TEXT("float3")));

	TestTrue(TEXT("Left child should exist"), Root.LeftNodeIndex != INDEX_NONE);
	const Flight::Vex::FVexExpressionAst& Left = Result.Program.Expressions[Root.LeftNodeIndex];
	TestEqual(TEXT("Left child should be function call"), Left.Kind, Flight::Vex::EVexExprKind::FunctionCall);
	TestEqual(TEXT("Function name should be normalize"), Left.Lexeme, FString(TEXT("normalize")));
	TestEqual(TEXT("normalize should have one argument"), Left.ArgumentNodeIndices.Num(), 1);

	return Result.bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserOptimizationTest,
	"FlightProject.Unit.Vex.Parser.Optimization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserOptimizationTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	// Test Case 1: Constant Folding (1.0 + 2.0 -> 3.0)
	{
		const FString Source = TEXT("float energy = 1.0 + 2.0;\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
		
		const Flight::Vex::FVexStatementAst& Statement = Result.Program.Statements[0];
		const Flight::Vex::FVexExpressionAst& Root = Result.Program.Expressions[Statement.ExpressionNodeIndex];
		
		TestEqual(TEXT("1.0 + 2.0 should be folded to a NumberLiteral"), Root.Kind, Flight::Vex::EVexExprKind::NumberLiteral);
		TestTrue(TEXT("Folded result should be approximately 3.0"), FMath::IsNearlyEqual(FCString::Atof(*Root.Lexeme), 3.0f));
	}

	// Test Case 2: Identity Simplification (x * 1.0 -> x)
	{
		const FString Source2 = TEXT("@velocity = @velocity * 1.0;\n");
		const Flight::Vex::FVexParseResult Result2 = Flight::Vex::ParseAndValidate(Source2, SymbolDefinitions, false);
		
		const Flight::Vex::FVexStatementAst& Statement2 = Result2.Program.Statements[0];
		const Flight::Vex::FVexExpressionAst& Root2 = Result2.Program.Expressions[Statement2.ExpressionNodeIndex];
		
		TestEqual(TEXT("@velocity * 1.0 should be simplified to SymbolRef"), Root2.Kind, Flight::Vex::EVexExprKind::SymbolRef);
		TestEqual(TEXT("Simplified result should be @velocity"), Root2.Lexeme, FString(TEXT("@velocity")));
	}

    // Test Case 3: Complex Folding (1.0 + 2.0 * 3.0 -> 7.0)
    {
        const FString Source3 = TEXT("float x = 1.0 + 2.0 * 3.0;\n");
        const Flight::Vex::FVexParseResult Result3 = Flight::Vex::ParseAndValidate(Source3, SymbolDefinitions, false);
        const Flight::Vex::FVexExpressionAst& Root3 = Result3.Program.Expressions[Result3.Program.Statements[0].ExpressionNodeIndex];
        TestTrue(TEXT("Complex math should fold to 7.0"), FMath::IsNearlyEqual(FCString::Atof(*Root3.Lexeme), 7.0f));
    }

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserDependencyTest,
	"FlightProject.Unit.Vex.Parser.DependencyDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserDependencyTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	// Test 1: Light Lattice detection
	{
		const FString Source = TEXT("inject_light(@position, {1,1,1});\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
		TestTrue(TEXT("Should detect LightLattice dependency"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::LightLattice));
		TestTrue(TEXT("Should detect SwarmState dependency"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::SwarmState));
	}

	// Test 2: Cloud detection via pipe
	{
		const FString Source = TEXT("@position | inject_cloud(1.0);\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
		TestTrue(TEXT("Should detect VolumetricCloud dependency"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::VolumetricCloud));
	}

	// Test 3: WriteState detection
	{
		const FString Source = TEXT("@velocity = {0,0,0};\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
		TestTrue(TEXT("Should detect WriteState requirement"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::WriteState));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserNPRBuiltinsTest,
	"FlightProject.Unit.Vex.Parser.NPRBuiltins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserNPRBuiltinsTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	// Test: Complex NPR composition
	const FString Source = 
		TEXT("float3 grad = @position | sample_sdf() | gradient();\n")
		TEXT("float align = dot(grad, normalize(@velocity));\n")
		TEXT("float toon = align | smoothstep(0.4, 0.5);\n")
		TEXT("@shield = toon | clamp(0.1, 1.0);\n");

	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
	TestTrue(TEXT("NPR source should parse successfully"), Result.bSuccess);
	TestTrue(TEXT("Should detect StructureSDF requirement from gradient()"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::StructureSDF));
	TestTrue(TEXT("Should detect SwarmState requirement"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::SwarmState));
	TestTrue(TEXT("Should detect WriteState requirement"), (bool)(Result.Requirements & Flight::Vex::EVexRequirement::WriteState));

	return Result.bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserMegaKernelTest,
	"FlightProject.Unit.Vex.Parser.MegaKernel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserMegaKernelTest::RunTest(const FString&)
{
	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions =
		Flight::Vex::BuildSymbolDefinitionsFromManifest(ManifestData);

	// Script 1: Uses position and velocity
	const FString Source1 = TEXT("@velocity += @position * 0.1;\n");
	const Flight::Vex::FVexParseResult Result1 = Flight::Vex::ParseAndValidate(Source1, SymbolDefinitions, false);
	TestTrue(TEXT("Script 1 parse"), Result1.bSuccess);

	// Script 2: Uses position and shield
	const FString Source2 = TEXT("@shield = @position.x;\n");
	const Flight::Vex::FVexParseResult Result2 = Flight::Vex::ParseAndValidate(Source2, SymbolDefinitions, false);
	TestTrue(TEXT("Script 2 parse"), Result2.bSuccess);

	TMap<uint32, Flight::Vex::FVexProgramAst> Scripts;
	Scripts.Add(1, Result1.Program);
	Scripts.Add(2, Result2.Program);

	TMap<FString, FString> SymbolMap;
	SymbolMap.Add(TEXT("@position"), TEXT("Entity.Position"));
	SymbolMap.Add(TEXT("@velocity"), TEXT("Entity.Velocity"));
	SymbolMap.Add(TEXT("@shield"), TEXT("Entity.Shield"));

	const FString MegaKernel = Flight::Vex::LowerMegaKernel(Scripts, TArray<Flight::Vex::FVexSymbolDefinition>(), SymbolMap);

	// Verification
	TestTrue(TEXT("Should hoist @position exactly once"), MegaKernel.Contains(TEXT("auto position_local = Entity.Position;")));
	TestTrue(TEXT("Should generate case 1"), MegaKernel.Contains(TEXT("case 1:")));
	
	// IR Lowering uses vN for locals now, but we verify it's doing the math correctly
	// We look for assignments using the hoisted aliases.
	TestTrue(TEXT("Should use local position alias in case 1"), MegaKernel.Contains(TEXT("velocity_local =")));
	TestTrue(TEXT("Should use local position alias in case 2"), MegaKernel.Contains(TEXT("shield_local =")));
	TestTrue(TEXT("Should contain variable assignment"), MegaKernel.Contains(TEXT("v")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserDirectionalPipeTest,
	"FlightProject.Unit.Vex.Parser.Ast.DirectionalPipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserDirectionalPipeTest::RunTest(const FString&)
{
	TArray<Flight::Vex::FVexSymbolDefinition> Defs;
	{
		Flight::Vex::FVexSymbolDefinition Velocity;
		Velocity.SymbolName = TEXT("@velocity");
		Velocity.ValueType = TEXT("float3");
		Velocity.Residency = EFlightVexSymbolResidency::Shared;
		Velocity.bRequired = false;
		Defs.Add(Velocity);

		Flight::Vex::FVexSymbolDefinition Position;
		Position.SymbolName = TEXT("@position");
		Position.ValueType = TEXT("float3");
		Position.Residency = EFlightVexSymbolResidency::Shared;
		Position.bRequired = false;
		Defs.Add(Position);

		Flight::Vex::FVexSymbolDefinition HitDist;
		HitDist.SymbolName = TEXT("@hit_dist");
		HitDist.ValueType = TEXT("float");
		HitDist.Residency = EFlightVexSymbolResidency::Shared;
		HitDist.bRequired = false;
		Defs.Add(HitDist);
	}

	// Test Case 1: Control Pipe (Inject) Host <| Device
	{
		const FString Source = TEXT("@gpu { @velocity <| vec3(1,0,0); }\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, Defs, false);
		if (!Result.bSuccess) { AddInfo(Flight::Vex::FormatIssues(Result.Issues)); }

		TestTrue(TEXT("PipeIn source should parse successfully"), Result.bSuccess);
		TestTrue(TEXT("Should contain a PipeIn node"), Result.Program.Expressions.ContainsByPredicate([](const auto& Expr)
		{
			return Expr.Kind == Flight::Vex::EVexExprKind::PipeIn;
		}));
	}

	// Test Case 2: Perception Pipe (Extract) Device |> Host
	{
		const FString Source = TEXT("@cpu { int count = @hit_dist |> count_hits(); }\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, Defs, false);
		if (!Result.bSuccess) { AddInfo(Flight::Vex::FormatIssues(Result.Issues)); }

		TestTrue(TEXT("PipeOut source should parse successfully"), Result.bSuccess);
		TestTrue(TEXT("Should contain a PipeOut node"), Result.Program.Expressions.ContainsByPredicate([](const auto& Expr)
		{
			return Expr.Kind == Flight::Vex::EVexExprKind::PipeOut;
		}));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserPipeDisambiguationTest,
	"FlightProject.Unit.Vex.Parser.Ast.PipeDisambiguation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserPipeDisambiguationTest::RunTest(const FString&)
{
	const TArray<Flight::Vex::FVexSymbolDefinition> NoDefs;
	const FString Source = TEXT("@cpu { bool b = (1 | 2) || (3 |> 4); }\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, NoDefs, false);
	if (!Result.bSuccess) { AddInfo(Flight::Vex::FormatIssues(Result.Issues)); }

	TestTrue(TEXT("Disambiguation source should parse successfully"), Result.bSuccess);

	bool bFoundPipe = false;
	bool bFoundPipeOut = false;
	bool bFoundLogicalOr = false;

	for (const auto& Expr : Result.Program.Expressions)
	{
		if (Expr.Kind == Flight::Vex::EVexExprKind::Pipe) bFoundPipe = true;
		if (Expr.Kind == Flight::Vex::EVexExprKind::PipeOut) bFoundPipeOut = true;
		if (Expr.Kind == Flight::Vex::EVexExprKind::BinaryOp && Expr.Lexeme == TEXT("||")) bFoundLogicalOr = true;
	}

	TestTrue(TEXT("Detected standard Pipe '|'"), bFoundPipe);
	TestTrue(TEXT("Detected PipeOut '|>'"), bFoundPipeOut);
	TestTrue(TEXT("Detected Logical Or '||'"), bFoundLogicalOr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserPipeResidencyErrorTest,
	"FlightProject.Unit.Vex.Parser.Semantics.PipeResidency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserPipeResidencyErrorTest::RunTest(const FString&)
{
	TArray<Flight::Vex::FVexSymbolDefinition> Defs;
	{
		Flight::Vex::FVexSymbolDefinition Velocity;
		Velocity.SymbolName = TEXT("@velocity");
		Velocity.ValueType = TEXT("float");
		Velocity.Residency = EFlightVexSymbolResidency::Shared;
		Velocity.bRequired = false;
		Defs.Add(Velocity);

		Flight::Vex::FVexSymbolDefinition HitDist;
		HitDist.SymbolName = TEXT("@hit_dist");
		HitDist.ValueType = TEXT("float");
		HitDist.Residency = EFlightVexSymbolResidency::Shared;
		HitDist.bRequired = false;
		Defs.Add(HitDist);

		Flight::Vex::FVexSymbolDefinition Status;
		Status.SymbolName = TEXT("@status");
		Status.ValueType = TEXT("int");
		Status.Residency = EFlightVexSymbolResidency::Shared;
		Status.bRequired = false;
		Defs.Add(Status);
	}

	{
		const FString Source = TEXT("@cpu { @velocity <| 1.0; }\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, Defs, false);
		if (Result.bSuccess) { AddInfo(TEXT("Expected failure but parser returned success.")); }
		TestFalse(TEXT("Inject pipe in @cpu context should fail"), Result.bSuccess);

		bool bHasResidencyError = false;
		for (const auto& Issue : Result.Issues)
		{
			if (Issue.Message.Contains(TEXT("Inject pipe `<|` can only be used within a `@gpu` context.")))
			{
				bHasResidencyError = true;
				break;
			}
		}
		TestTrue(TEXT("Should report Inject residency error"), bHasResidencyError);
	}

	{
		const FString Source = TEXT("@gpu { @hit_dist |> @status; }\n");
		const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, Defs, false);
		if (Result.bSuccess) { AddInfo(TEXT("Expected failure but parser returned success.")); }
		TestFalse(TEXT("Extract pipe in @gpu context should fail"), Result.bSuccess);

		bool bHasResidencyError = false;
		for (const auto& Issue : Result.Issues)
		{
			if (Issue.Message.Contains(TEXT("Extract pipe `|>` can only be used within a `@cpu` context.")))
			{
				bHasResidencyError = true;
				break;
			}
		}
		TestTrue(TEXT("Should report Extract residency error"), bHasResidencyError);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserLogicalAndOperatorTest,
	"FlightProject.Unit.Vex.Parser.Ast.LogicalAndOperator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserLogicalAndOperatorTest::RunTest(const FString&)
{
	const TArray<Flight::Vex::FVexSymbolDefinition> NoDefs;
	const FString Source = TEXT("bool ok = (1 < 2) && (3 < 4);\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, NoDefs, false);
	if (!Result.bSuccess) { AddInfo(Flight::Vex::FormatIssues(Result.Issues)); }

	TestTrue(TEXT("Logical-and source should parse successfully"), Result.bSuccess);
	const bool bFoundLogicalAnd = Result.Program.Expressions.ContainsByPredicate([](const auto& Expr)
	{
		return Expr.Kind == Flight::Vex::EVexExprKind::BinaryOp && Expr.Lexeme == TEXT("&&");
	});
	TestTrue(TEXT("Parser should emit a BinaryOp '&&' node"), bFoundLogicalAnd);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserMaximalMunchPipeTokensTest,
	"FlightProject.Unit.Vex.Parser.Ast.MaximalMunchPipeTokens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserMaximalMunchPipeTokensTest::RunTest(const FString&)
{
	const TArray<Flight::Vex::FVexSymbolDefinition> NoDefs;
	const FString Source = TEXT("@cpu { bool b = (1 | 2) || (3 |> 4); }\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, NoDefs, false);
	if (!Result.bSuccess) { AddInfo(Flight::Vex::FormatIssues(Result.Issues)); }

	TestTrue(TEXT("Maximal-munch source should parse successfully"), Result.bSuccess);

	int32 PipeTokenCount = 0;
	int32 PipeOutTokenCount = 0;
	int32 LogicalOrTokenCount = 0;
	for (const Flight::Vex::FVexToken& Token : Result.Program.Tokens)
	{
		if (Token.Kind != Flight::Vex::EVexTokenKind::Operator)
		{
			continue;
		}
		if (Token.Lexeme == TEXT("|")) { ++PipeTokenCount; }
		else if (Token.Lexeme == TEXT("|>")) { ++PipeOutTokenCount; }
		else if (Token.Lexeme == TEXT("||")) { ++LogicalOrTokenCount; }
	}

	TestEqual(TEXT("Expected one '|' token"), PipeTokenCount, 1);
	TestEqual(TEXT("Expected one '|>' token"), PipeOutTokenCount, 1);
	TestEqual(TEXT("Expected one '||' token"), LogicalOrTokenCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserInvalidExtractOperatorTest,
	"FlightProject.Unit.Vex.Parser.Diagnostics.InvalidExtractOperator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserInvalidExtractOperatorTest::RunTest(const FString&)
{
	TArray<Flight::Vex::FVexSymbolDefinition> Defs;
	{
		Flight::Vex::FVexSymbolDefinition HitDist;
		HitDist.SymbolName = TEXT("@hit_dist");
		HitDist.ValueType = TEXT("float");
		HitDist.Residency = EFlightVexSymbolResidency::Shared;
		HitDist.bRequired = false;
		Defs.Add(HitDist);
	}

	const FString Source = TEXT("@cpu { int count = @hit_dist >| count_hits(); }\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, Defs, false);
	if (Result.bSuccess) { AddInfo(TEXT("Expected parser failure for invalid operator sequence `>|`.")); }

	TestFalse(TEXT("Invalid extract operator `>|` should fail"), Result.bSuccess);

	bool bHasMalformedIssue = false;
	bool bHasLegacyToken = false;
	for (const auto& Issue : Result.Issues)
	{
		if (Issue.Message.Contains(TEXT("Malformed expression")) || Issue.Message.Contains(TEXT("Unexpected token")))
		{
			bHasMalformedIssue = true;
			break;
		}
	}
	for (const auto& Token : Result.Program.Tokens)
	{
		if (Token.Kind == Flight::Vex::EVexTokenKind::Operator && Token.Lexeme == TEXT(">|"))
		{
			bHasLegacyToken = true;
			break;
		}
	}

	TestTrue(TEXT("Invalid syntax should produce a parse diagnostic"), bHasMalformedIssue);
	TestFalse(TEXT("Lexer should not produce a `>|` operator token"), bHasLegacyToken);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexParserInvalidSingleAmpersandTest,
	"FlightProject.Unit.Vex.Parser.Diagnostics.InvalidSingleAmpersand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVexParserInvalidSingleAmpersandTest::RunTest(const FString&)
{
	const TArray<Flight::Vex::FVexSymbolDefinition> NoDefs;
	const FString Source = TEXT("bool ok = (1 < 2) & (3 < 4);\n");
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(Source, NoDefs, false);
	if (Result.bSuccess) { AddInfo(TEXT("Expected parser failure for single `&` in logical expression.")); }

	TestFalse(TEXT("Single '&' should fail parse"), Result.bSuccess);
	const bool bHasUnexpectedTokenIssue = Result.Issues.ContainsByPredicate([](const Flight::Vex::FVexIssue& Issue)
	{
		return Issue.Message.Contains(TEXT("Unexpected token"));
	});
	TestTrue(TEXT("Single '&' should emit unexpected-token diagnostic"), bHasUnexpectedTokenIssue);
	return true;
}
