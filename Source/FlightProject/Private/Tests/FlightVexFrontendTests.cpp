// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vex/Frontend/VexLexer.h"
#include "Vex/Frontend/VexParser.h"
#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexSimdExecutor.h"
#include "Swarm/SwarmSimulationTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexModularFrontendTest, "FlightProject.Vex.Frontend.ModularRefactor", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FFlightVexModularFrontendTest::RunTest(const FString& Parameters)
{
	using namespace Flight::Vex;

	// 1. Test Lexer
	{
		FString Source = TEXT("@shield = 1.0;");
		TArray<FVexIssue> Issues;
		TArray<FVexToken> Tokens = FVexLexer::Tokenize(Source, Issues);
		
		TestTrue(TEXT("Lexer should produce tokens"), Tokens.Num() > 0);
		TestEqual(TEXT("First token should be @shield symbol"), Tokens[0].Lexeme, TEXT("@shield"));
		TestEqual(TEXT("Second token should be operator"), Tokens[1].Kind, EVexTokenKind::Operator);
		TestEqual(TEXT("Third token should be number"), Tokens[2].Kind, EVexTokenKind::Number);
	}

	// 2. Test Parser & IR Compilation
	{
		FString Source = TEXT("@shield = sin(0.5);");
		TArray<FVexSymbolDefinition> Defs;
		{
			FVexSymbolDefinition Shield;
			Shield.SymbolName = TEXT("@shield");
			Shield.ValueType = TEXT("float");
			Defs.Add(Shield);
		}

		FVexParseResult ParseRes = ParseAndValidate(Source, Defs);
		TestTrue(TEXT("Parser should succeed"), ParseRes.bSuccess);
		TestTrue(TEXT("AST should have statements"), ParseRes.Program.Statements.Num() > 0);

		FString IrErrors;
		TSharedPtr<FVexIrProgram> Ir = FVexIrCompiler::Compile(ParseRes.Program, Defs, IrErrors);
		TestTrue(TEXT("IR Compilation should succeed"), Ir.IsValid());
		TestTrue(TEXT("IR should have instructions"), Ir->Instructions.Num() > 0);

		// Verify IR lowering parity
		FString Verse = FVexIrCompiler::LowerToVerse(*Ir, TMap<FString, FString>());
		TestTrue(TEXT("Verse output should use 'set'"), Verse.Contains(TEXT("set")));
		
		FString Hlsl = FVexIrCompiler::LowerToHLSL(*Ir, TMap<FString, FString>());
		TestTrue(TEXT("HLSL output should have reg assignment"), Hlsl.Contains(TEXT("reg")));
	}

	return true;
}
