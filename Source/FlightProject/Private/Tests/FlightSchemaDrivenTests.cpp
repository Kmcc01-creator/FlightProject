// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Vex/FlightVexParser.h"

/**
 * FFlightSchemaDrivenValidationTest
 * 
 * An experimental "Schema-Driven" testing suite.
 * Instead of hardcoded tests, this suite dynamically generates validation tests 
 * based on the current state of the C++ Reflection Schema Manifest.
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FFlightSchemaDrivenValidationTest, "FlightProject.Integration.SchemaDriven", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightSchemaDrivenValidationTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	// 1. Load the Schema as the source of truth
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();

	// 2. Generate tests dynamically for every single registered VEX symbol
	for (const FFlightVexSymbolRow& Row : Manifest.VexSymbolRequirements)
	{
		// A. Positive Test: Valid Context
		FString ValidContext = (Row.Residency == EFlightVexSymbolResidency::GpuOnly) ? TEXT("@gpu") : TEXT("@cpu");
		OutBeautifiedNames.Add(FString::Printf(TEXT("VexSymbol.ValidAccess.%s"), *Row.SymbolName));
		OutTestCommands.Add(FString::Printf(TEXT("VALID|%s|%s"), *Row.SymbolName, *ValidContext));

		// B. Negative Test: Invalid Residency
		if (Row.Residency != EFlightVexSymbolResidency::Shared)
		{
			FString InvalidContext = (Row.Residency == EFlightVexSymbolResidency::GpuOnly) ? TEXT("@cpu") : TEXT("@gpu");
			OutBeautifiedNames.Add(FString::Printf(TEXT("VexSymbol.InvalidResidency.%s"), *Row.SymbolName));
			OutTestCommands.Add(FString::Printf(TEXT("INVALID_RESIDENCY|%s|%s"), *Row.SymbolName, *InvalidContext));
		}

		// C. Negative Test: Invalid Affinity
		if (Row.Affinity == EFlightVexSymbolAffinity::GameThread)
		{
			OutBeautifiedNames.Add(FString::Printf(TEXT("VexSymbol.InvalidAffinity.%s"), *Row.SymbolName));
			// @job forces a WorkerThread context
			OutTestCommands.Add(FString::Printf(TEXT("INVALID_AFFINITY|%s|@cpu @job"), *Row.SymbolName)); 
		}
	}
}

bool FFlightSchemaDrivenValidationTest::RunTest(const FString& Parameters)
{
	TArray<FString> Parts;
	Parameters.ParseIntoArray(Parts, TEXT("|"), true);
	if (Parts.Num() < 3) return false;

	const FString TestType = Parts[0];
	const FString SymbolName = Parts[1];
	const FString Context = Parts[2];

	// Dynamically generate a VEX script based on the test parameters
	FString VexSource;
	if (SymbolName.Contains(TEXT("()"))) // It's a function
	{
		VexSource = FString::Printf(TEXT("%s { float _tmp = %s; }\n"), *Context, *SymbolName);
	}
	else
	{
		VexSource = FString::Printf(TEXT("%s { float _tmp = %s.x; }\n"), *Context, *SymbolName);
	}

	// Fetch current definitions directly from the schema
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
	TArray<Flight::Vex::FVexSymbolDefinition> Defs = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);
	
	Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

	if (TestType == TEXT("VALID"))
	{
		TestTrue(FString::Printf(TEXT("Schema-generated test for %s should pass in %s context"), *SymbolName, *Context), Result.bSuccess);
		if (!Result.bSuccess)
		{
			for (const auto& Issue : Result.Issues) { AddError(Issue.Message); }
		}
	}
	else if (TestType == TEXT("INVALID_RESIDENCY"))
	{
		bool bFoundError = false;
		for (const auto& Issue : Result.Issues) { if (Issue.Message.Contains(TEXT("cannot be used in"))) bFoundError = true; }
		TestTrue(FString::Printf(TEXT("Schema-generated test should catch residency violation for %s in %s context"), *SymbolName, *Context), bFoundError);
	}
	else if (TestType == TEXT("INVALID_AFFIDINITY"))
	{
		bool bFoundError = false;
		for (const auto& Issue : Result.Issues) { if (Issue.Message.Contains(TEXT("Game Thread"))) bFoundError = true; }
		TestTrue(FString::Printf(TEXT("Schema-generated test should catch affinity violation for %s in %s context"), *SymbolName, *Context), bFoundError);
	}

	return true;
}
