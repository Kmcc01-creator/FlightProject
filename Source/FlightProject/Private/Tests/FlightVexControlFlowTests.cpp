// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexIr.h"
#include "Vex/Frontend/VexParser.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexControlFlowTest, "FlightProject.Unit.Vex.ControlFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexControlFlowTest::RunTest(const FString& Parameters)
{
	// Setup Mock Symbol Definitions
	TArray<Flight::Vex::FVexSymbolDefinition> Defs;
	{
		Flight::Vex::FVexSymbolDefinition Pos;
		Pos.SymbolName = TEXT("@position");
		Pos.ValueType = TEXT("float3");
		Pos.Residency = EFlightVexSymbolResidency::Shared;
		Pos.Affinity = EFlightVexSymbolAffinity::Any;
		Defs.Add(Pos);

		Flight::Vex::FVexSymbolDefinition Shield;
		Shield.SymbolName = TEXT("@shield");
		Shield.ValueType = TEXT("float");
		Shield.Residency = EFlightVexSymbolResidency::Shared;
		Shield.Affinity = EFlightVexSymbolAffinity::Any;
		Defs.Add(Shield);
	}

	// Test script with an 'if' statement
	FString VexSource = TEXT("@cpu { if (@shield < 10) { @position = {0,0,0}; } }");
	Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

	TestTrue(TEXT("Script with 'if' should parse successfully"), Result.bSuccess);

	TMap<FString, FString> SymbolMap;
	SymbolMap.Add(TEXT("@position"), TEXT("Droid.Position"));
	SymbolMap.Add(TEXT("@shield"), TEXT("Droid.Shield"));
	
	FString VerseOutput = Flight::Vex::LowerToVerse(Result.Program, Defs, SymbolMap);
	
	// If the IR compiler is losing the 'if', this test will FAIL
	TestTrue(TEXT("Verse output should contain 'if' statement"), VerseOutput.Contains(TEXT("if ")));
	TestTrue(TEXT("Verse output should contain the assignment inside the if"), VerseOutput.Contains(TEXT("set Droid.Position =")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
