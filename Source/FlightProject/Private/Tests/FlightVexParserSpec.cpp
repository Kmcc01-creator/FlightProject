// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexParser.h"
#include "FlightTestUtils.h"

BEGIN_DEFINE_SPEC(FFlightVexParserSpec, "FlightProject.Vex.Parser.Spec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	TArray<Flight::Vex::FVexSymbolDefinition> MockSymbols;
END_DEFINE_SPEC(FFlightVexParserSpec)

void FFlightVexParserSpec::Define()
{
	BeforeEach([this]() {
		MockSymbols = Flight::Test::GetMockSymbols();
	});

	Describe("Syntax Parsing", [this]() {
		It("should parse basic assignments correctly", [this]() {
			FString Source = TEXT("@velocity = {1, 0, 0};");
			auto Result = Flight::Vex::ParseAndValidate(Source, MockSymbols, false);
			TestTrue("Parse Success", Result.bSuccess);
			TestEqual("Statement Count", Result.Program.Statements.Num(), 1);
		});

		It("should handle constant folding", [this]() {
			FString Source = TEXT("float x = 1.0 + 2.0 * 3.0;");
			auto Result = Flight::Vex::ParseAndValidate(Source, MockSymbols, false);
			const auto& Root = Result.Program.Expressions[Result.Program.Statements[0].ExpressionNodeIndex];
			TestEqual("Folded Kind", Root.Kind, Flight::Vex::EVexExprKind::NumberLiteral);
			TestTrue("Value Nearly 7.0", FMath::IsNearlyEqual(FCString::Atof(*Root.Lexeme), 7.0f));
		});
	});

	Describe("Semantic Validation", [this]() {
		Describe("Residency", [this]() {
			It("should block GPU-only symbols in CPU contexts", [this]() {
				FString Source = TEXT("@cpu { @shield = 1.0; }");
				auto Result = Flight::Vex::ParseAndValidate(Source, MockSymbols, false);
				
				bool bHasError = false;
				for (const auto& I : Result.Issues) if (I.Message.Contains(TEXT("residency error"))) bHasError = true;
				TestTrue("Detected Residency Violation", bHasError);
			});
		});

		Describe("Thread Affinity", [this]() {
			It("should block GameThread-only symbols in Worker contexts", [this]() {
				FString Source = TEXT("@cpu @job { @velocity = @uobject_data; }");
				auto Result = Flight::Vex::ParseAndValidate(Source, MockSymbols, false);
				
				bool bHasError = false;
				for (const auto& I : Result.Issues) if (I.Message.Contains(TEXT("affinity error"))) bHasError = true;
				TestTrue("Detected Affinity Violation", bHasError);
			});
		});
	});

	Describe("Verse Lowering", [this]() {
		It("should generate idiomatic Verse code", [this]() {
			FString Source = TEXT("@velocity = @position;");
			auto Result = Flight::Vex::ParseAndValidate(Source, MockSymbols, false);
			
			TMap<FString, FString> SymbolMap;
			SymbolMap.Add(TEXT("@velocity"), TEXT("Droid.Velocity"));
			SymbolMap.Add(TEXT("@position"), TEXT("Droid.Position"));
			FString Output = Flight::Vex::LowerToVerse(Result.Program, TArray<Flight::Vex::FVexSymbolDefinition>(), SymbolMap);
			
			TestTrue("Uses 'set' keyword", Output.Contains(TEXT("set Droid.Velocity")));
			TestFalse("No semicolons", Output.Contains(TEXT(";")));
		});
	});
}
