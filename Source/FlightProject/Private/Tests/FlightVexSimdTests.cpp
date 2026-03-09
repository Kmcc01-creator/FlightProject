// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexSimdExecutor.h"
#include "Mass/FlightMassFragments.h"
#include "Swarm/SwarmSimulationTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexSimdParityTest, "FlightProject.Integration.Vex.SimdParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexSimdParityTest::RunTest(const FString& Parameters)
{
	using namespace Flight::Vex;

	// 1. Setup minimal program
	FString Source = TEXT("@shield = sin(@position.x) * 0.5 + 0.5;");
	
	TArray<FVexSymbolDefinition> Defs;
	{
		FVexSymbolDefinition Pos; 
		Pos.SymbolName = TEXT("@position"); 
		Pos.ValueType = TEXT("float3"); 
		Pos.bSimdReadAllowed = true; 
		Defs.Add(Pos);

		FVexSymbolDefinition Shield; 
		Shield.SymbolName = TEXT("@shield"); 
		Shield.ValueType = TEXT("float"); 
		Shield.bSimdWriteAllowed = true; 
		Defs.Add(Shield);
	}

	FVexParseResult ParseRes = ParseAndValidate(Source, Defs);
	if (!ParseRes.bSuccess)
	{
		for (const auto& I : ParseRes.Issues) AddError(I.Message);
		return false;
	}

	// 2. Compile to IR
	FString IrErrors;
	TSharedPtr<FVexIrProgram> Ir = FVexIrCompiler::Compile(ParseRes.Program, Defs, IrErrors);
	TestTrue(TEXT("IR Compilation should succeed"), Ir.IsValid());

	// 3. Compile to SIMD Plan
	TSharedPtr<FVexSimdExecutor> Simd = FVexSimdExecutor::Compile(Ir);
	TestTrue(TEXT("SIMD Plan compilation should succeed"), Simd.IsValid());

	// 4. Test Execute (Gather/Scatter)
	{
		TArray<Flight::Swarm::FDroidState> Droids;
		Droids.SetNum(4);
		for(int32 i=0; i<4; ++i) { Droids[i].Position = FVector3f(float(i), 0, 0); Droids[i].Shield = 0.0f; }

		Simd->Execute(Droids);

		for(int32 i=0; i<4; ++i)
		{
			float Expected = FMath::Sin(float(i)) * 0.5f + 0.5f;
			TestTrue(FString::Printf(TEXT("Droid %d shield parity (Gather/Scatter)"), i), FMath::IsNearlyEqual(Droids[i].Shield, Expected, 0.0001f));
		}
	}

	// 5. Test ExecuteDirect (SoA)
	{
		TArray<::FFlightTransformFragment> Transforms; Transforms.SetNum(4);
		TArray<::FFlightDroidStateFragment> DroidStates; DroidStates.SetNum(4);
		for(int32 i=0; i<4; ++i) 
		{ 
			Transforms[i].Location = FVector(double(i), 0, 0); 
			DroidStates[i].Shield = 0.0f; 
			DroidStates[i].bIsDirty = false;
		}

		Simd->ExecuteDirect(Transforms, DroidStates);

		for(int32 i=0; i<4; ++i)
		{
			float Expected = FMath::Sin(float(i)) * 0.5f + 0.5f;
			TestTrue(FString::Printf(TEXT("Droid %d shield parity (Direct SoA)"), i), FMath::IsNearlyEqual(DroidStates[i].Shield, Expected, 0.0001f));
			TestTrue(FString::Printf(TEXT("Droid %d should be dirty"), i), DroidStates[i].bIsDirty);
		}
	}

	return true;
}
