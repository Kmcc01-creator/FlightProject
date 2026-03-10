// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexParser.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Schema/FlightRequirementRegistry.h"
#include "FlightScriptingLibrary.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "FlightTestUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	UWorld* FindAutomationWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
			{
				return Context.World();
			}
		}

		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexParsingTest, "FlightProject.Unit.Vex.Parsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexParsingTest::RunTest(const FString& Parameters)
{
	// 1. Setup Mock Symbol Definitions
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
		Shield.Residency = EFlightVexSymbolResidency::GpuOnly;
		Shield.Affinity = EFlightVexSymbolAffinity::Any;
		Defs.Add(Shield);
	}

	// 2. Test Basic Parsing and Verse Lowering
	{
		FString VexSource = TEXT("@cpu { @position = {1, 2, 3}; }");
		Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

		TestTrue(TEXT("Should have no parsing issues"), Result.bSuccess);

		TMap<FString, FString> SymbolMap;
		SymbolMap.Add(TEXT("@position"), TEXT("Droid.Position"));

		FString VerseOutput = Flight::Vex::LowerToVerse(Result.Program, Defs, SymbolMap);

		// Check for idiomatic Verse syntax
		TestTrue(TEXT("Verse output should use 'set' for assignment"), VerseOutput.Contains(TEXT("set Droid.Position =")));
		TestFalse(TEXT("Verse output should not have semicolons"), VerseOutput.Contains(TEXT(";")));
	}

	// 3. Test Residency Validation (Negative Case)
	{
		// Try to use a GPU-only symbol in a CPU block
		FString VexSource = TEXT("@cpu { @shield = 1.0; }");
		Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

		bool bFoundResidencyError = false;
		for (const auto& Issue : Result.Issues)
		{
			if (Issue.Message.Contains(TEXT("residency error")))
			{
				bFoundResidencyError = true;
				break;
			}
		}

		TestTrue(TEXT("Should report residency error when using GPU symbol in CPU context"), bFoundResidencyError);
	}

	// 4. Test Affinity Validation
	{
		Flight::Vex::FVexSymbolDefinition GtSymbol;
		GtSymbol.SymbolName = TEXT("@uobject_data");
		GtSymbol.ValueType = TEXT("float");
		GtSymbol.Affinity = EFlightVexSymbolAffinity::GameThread;
		Defs.Add(GtSymbol);

		// Try to use a GameThread symbol in a Job block
		FString VexSource = TEXT("@cpu @job { @position = @uobject_data; }");
		Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

		bool bFoundAffinityError = false;
		for (const auto& Issue : Result.Issues)
		{
			if (Issue.Message.Contains(TEXT("affinity error")))
			{
				bFoundAffinityError = true;
				break;
			}
		}

		TestTrue(TEXT("Should report affinity error when using GameThread symbol in @job context"), bFoundAffinityError);
	}

	// 5. Test Task Capture and Async Lowering
	{
		FString VexSource = TEXT("@cpu @async { @position = {0,0,0}; }");
		Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

		TestEqual(TEXT("Should have 1 task descriptor"), Result.Program.Tasks.Num(), 1);
		if (Result.Program.Tasks.Num() > 0)
		{
			TestEqual(TEXT("Task type should be @async"), Result.Program.Tasks[0].Type, TEXT("@async"));
			TestTrue(TEXT("Task should capture @position for writing"), Result.Program.Tasks[0].WriteSymbols.Contains(TEXT("@position")));
		}

		TMap<FString, FString> SymbolMap;
		SymbolMap.Add(TEXT("@position"), TEXT("Droid.Position"));
		FString VerseOutput = Flight::Vex::LowerToVerse(Result.Program, TArray<Flight::Vex::FVexSymbolDefinition>(), SymbolMap);

		TestTrue(TEXT("Verse output should include <async> effect"), VerseOutput.Contains(TEXT("<async>")));
	}

	// 6. Test Rate Directive Parsing
	{
		FString VexSource = TEXT("@cpu @rate(15Hz) { @position = {0,0,0}; }");
		Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, Defs, false);

		TestEqual(TEXT("ExecutionRateHz should be 15.0"), Result.Program.ExecutionRateHz, 15.0f);
		TestEqual(TEXT("FrameInterval should be 0"), (int32)Result.Program.FrameInterval, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseSubsystemTest, "FlightProject.Functional.Verse.Subsystem", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVerseSubsystemTest::RunTest(const FString& Parameters)
{
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
	TArray<Flight::Vex::FVexSymbolDefinition> Defs = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);

	bool bFoundDroidPos = false;
	for (const auto& D : Defs)
	{
		if (D.SymbolName == TEXT("@position"))
		{
			bFoundDroidPos = true;
			TestEqual(TEXT("@position residency should be Shared"), D.Residency, EFlightVexSymbolResidency::Shared);
		}
		if (D.SymbolName == TEXT("@shield"))
		{
			TestEqual(TEXT("@shield residency should be GpuOnly"), D.Residency, EFlightVexSymbolResidency::GpuOnly);
		}
		if (D.SymbolName == TEXT("@status"))
		{
			TestEqual(TEXT("@status value type should map to int"), D.ValueType, FString(TEXT("int")));
		}
	}

	TestTrue(TEXT("Manifest should contain @position from FDroidState reflection"), bFoundDroidPos);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompileContractTest, "FlightProject.Integration.Verse.CompileContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVerseCompileContractTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for Verse compile contract test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	{
		FString OutErrors;
		const bool bCompiled = VerseSubsystem->CompileVex(9001, TEXT("@gpu { @velocity = @position; }"), OutErrors);
		TestFalse(TEXT("Compile should fail when required symbols are missing"), bCompiled);
		TestTrue(TEXT("Compile errors should report missing required symbols"), OutErrors.Contains(TEXT("missing required symbols")));
	}

	{
		const FString FullContractSource = TEXT("@gpu { @velocity = @position; @shield = @shield; @status = @status; }");
		FString OutErrors;
		const bool bCompiled = VerseSubsystem->CompileVex(9002, FullContractSource, OutErrors);
		TestTrue(TEXT("Compile should succeed when native fallback executor can run the behavior"), bCompiled);
		TestTrue(TEXT("Compile output should include generated Verse text"), OutErrors.Contains(TEXT("Generated Verse:")));
		TestTrue(TEXT("Compile output should report executable runtime path"),
			OutErrors.Contains(TEXT("Verse VM procedure")) || OutErrors.Contains(TEXT("native fallback executor")));

		const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(9002);
		TestNotNull(TEXT("Behavior metadata should be registered"), Behavior);
		if (Behavior)
		{
			TestTrue(TEXT("Behavior should be executable"), Behavior->bHasExecutableProcedure);
			TestTrue(TEXT("Behavior should use native fallback path"), Behavior->bUsesNativeFallback);
			TestTrue(TEXT("Behavior should store generated Verse source"), !Behavior->GeneratedVerseCode.IsEmpty());
			TestEqual(TEXT("Behavior compile state should be VmCompiled"), Behavior->CompileState, EFlightVerseCompileState::VmCompiled);
			TestTrue(TEXT("Behavior should retain compile diagnostics"), !Behavior->LastCompileDiagnostics.IsEmpty());
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
			if (Behavior->bUsesVmEntryPoint)
			{
				TestNotNull(TEXT("VM procedure should be created when VM path is active"), Behavior->Procedure.Get());
				TestNotNull(TEXT("VM entrypoint should be created when VM path is active"), Behavior->VmEntryPoint.Get());
				TestTrue(TEXT("Diagnostics should mention VM procedure path"), Behavior->LastCompileDiagnostics.Contains(TEXT("Verse VM procedure")));
			}
			else
			{
				TestTrue(TEXT("Diagnostics should mention fallback path when VM procedure is unavailable"),
					Behavior->LastCompileDiagnostics.Contains(TEXT("native fallback executor")));
			}
#else
			TestTrue(TEXT("Behavior should retain fallback diagnostics"), Behavior->LastCompileDiagnostics.Contains(TEXT("native fallback executor")));
#endif
		}

		TestEqual(TEXT("Scripting accessor should report VmCompiled state"),
			UFlightScriptingLibrary::GetBehaviorCompileState(World, 9002),
			EFlightVerseCompileState::VmCompiled);
		TestTrue(TEXT("Scripting accessor should report executable behavior"),
			UFlightScriptingLibrary::IsBehaviorExecutable(World, 9002));
		TestTrue(TEXT("Scripting accessor should return compile diagnostics"),
			!UFlightScriptingLibrary::GetBehaviorCompileDiagnostics(World, 9002).IsEmpty());

		Flight::Swarm::FDroidState DroidState;
		DroidState.Position = FVector3f(9.0f, -2.0f, 5.0f);
		DroidState.Velocity = FVector3f::ZeroVector;
		VerseSubsystem->ExecuteBehavior(9002, DroidState);
		TestEqual(TEXT("Native fallback execution should apply assignment"), DroidState.Velocity, DroidState.Position);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightCompileArtifactReportTest, "FlightProject.Functional.Vex.CompileArtifactReport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightCompileArtifactReportTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for compile artifact report test"));
		return false;
	}

	FString CompileDiagnostics;
	const FString FullContractSource = TEXT("@gpu { @velocity = @position; @shield = @shield; @status = @status; }");
	const bool bCompiled = UFlightScriptingLibrary::CompileVex(World, 9100, FullContractSource, CompileDiagnostics);
	TestTrue(TEXT("Compile should succeed for artifact report generation"), bCompiled);

	const FString ArtifactJson = UFlightScriptingLibrary::GetBehaviorCompileArtifactReportJson(World, 9100);
	TestTrue(TEXT("Compile artifact report JSON should be available"), !ArtifactJson.IsEmpty());

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArtifactJson);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Compile artifact JSON should deserialize"), bParsed);
	if (!bParsed || !RootObject.IsValid())
	{
		return false;
	}

	FString CompileOutcome;
	TestTrue(TEXT("Compile artifact JSON should contain compileOutcome"), RootObject->TryGetStringField(TEXT("compileOutcome"), CompileOutcome));
	TestEqual(TEXT("Compile artifact report should record VmCompiled outcome"), CompileOutcome, FString(TEXT("VmCompiled")));

	FString BackendPath;
	TestTrue(TEXT("Compile artifact JSON should contain backendPath"), RootObject->TryGetStringField(TEXT("backendPath"), BackendPath));
	TestTrue(TEXT("Compile artifact report should record a non-empty backend path"), !BackendPath.IsEmpty());

	FString TargetFingerprint;
	TestTrue(TEXT("Compile artifact JSON should contain targetFingerprint"), RootObject->TryGetStringField(TEXT("targetFingerprint"), TargetFingerprint));
	TestTrue(TEXT("Compile artifact report should record a non-empty target fingerprint"), !TargetFingerprint.IsEmpty());

	const TSharedPtr<FJsonObject>* CodeShapeObject = nullptr;
	TestTrue(TEXT("Compile artifact JSON should contain codeShapeMetrics"), RootObject->TryGetObjectField(TEXT("codeShapeMetrics"), CodeShapeObject));
	if (CodeShapeObject && CodeShapeObject->IsValid())
	{
		double InstructionCount = 0.0;
		TestTrue(TEXT("Code shape metrics should include instructionCount"), (*CodeShapeObject)->TryGetNumberField(TEXT("instructionCount"), InstructionCount));
		TestTrue(TEXT("Code shape metrics should report at least one instruction"), InstructionCount > 0.0);

		FString PressureClass;
		TestTrue(TEXT("Code shape metrics should include pressureClass"), (*CodeShapeObject)->TryGetStringField(TEXT("pressureClass"), PressureClass));
		TestTrue(TEXT("Code shape metrics should report a pressure class"), !PressureClass.IsEmpty());
	}

	const TSharedPtr<FJsonObject>* WarmupObject = nullptr;
	TestTrue(TEXT("Compile artifact JSON should contain warmupMetrics"), RootObject->TryGetObjectField(TEXT("warmupMetrics"), WarmupObject));
	if (WarmupObject && WarmupObject->IsValid())
	{
		double TotalCompileTimeMs = -1.0;
		TestTrue(TEXT("Warmup metrics should include totalCompileTimeMs"), (*WarmupObject)->TryGetNumberField(TEXT("totalCompileTimeMs"), TotalCompileTimeMs));
		TestTrue(TEXT("Warmup metrics should report a non-negative total compile time"), TotalCompileTimeMs >= 0.0);
	}

	return true;
}

#include "AutoRTFM.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightAutoRTFMIntegrationTest, "FlightProject.Integration.AutoRTFM", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightAutoRTFMIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace Flight::Swarm;

	FDroidState Droid;
	Droid.Position = FVector3f(0, 0, 0);
	Droid.Velocity = FVector3f(10, 0, 0);

	bool bCommitCalled = false;
	bool bAbortCalled = false;

	AutoRTFM::ETransactionResult TransactionResult = AutoRTFM::Transact([&]()
	{
		AutoRTFM::OnCommit([&]() { bCommitCalled = true; });
		AutoRTFM::OnAbort([&]() { bAbortCalled = true; });

		Droid.Position += Droid.Velocity;

			bool bCollisionDetected = true;

		if (bCollisionDetected)
		{
			AutoRTFM::AbortTransaction();
		}
	});

	const bool bAbortedByRequest = (TransactionResult == AutoRTFM::ETransactionResult::AbortedByRequest);
	if (bAbortedByRequest)
	{
		TestEqual(TEXT("Droid Position should be rolled back to original"), Droid.Position, FVector3f(0, 0, 0));
		TestTrue(TEXT("Abort handler should have fired"), bAbortCalled);
		TestFalse(TEXT("Commit handler should NOT have fired"), bCommitCalled);
	}
	else
	{
		AddInfo(TEXT("AutoRTFM transactional runtime is inactive; validating fallback non-transactional semantics."));
		TestEqual(TEXT("Droid Position should reflect non-transactional execution"), Droid.Position, FVector3f(10, 0, 0));
		TestEqual(TEXT("Fallback transaction result should be committed"), TransactionResult, AutoRTFM::ETransactionResult::Committed);
		TestFalse(TEXT("Abort handler should not fire in non-transactional mode"), bAbortCalled);
		TestTrue(TEXT("Commit handler should fire immediately in non-transactional mode"), bCommitCalled);
	}

	TransactionResult = AutoRTFM::Transact([&]()
	{
		bCommitCalled = false;
		bAbortCalled = false;
		AutoRTFM::OnCommit([&]() { bCommitCalled = true; });

		Droid.Position += Droid.Velocity;
	});

	const FVector3f ExpectedCommittedPosition = bAbortedByRequest ? FVector3f(10, 0, 0) : FVector3f(20, 0, 0);
	TestEqual(TEXT("Droid Position should match expected committed value"), Droid.Position, ExpectedCommittedPosition);
	TestEqual(TEXT("Transaction should have committed"), TransactionResult, AutoRTFM::ETransactionResult::Committed);
	TestTrue(TEXT("Commit handler should have fired"), bCommitCalled);

	return true;
}

#include "Core/FlightAsyncExecutor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightGpuReactiveTest, "FlightProject.Gpu.Reactive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightGpuReactiveTest::RunTest(const FString& Parameters)
{
	if (Flight::Test::ShouldSkipGpuTest())
	{
		return true;
	}

	using namespace Flight::Async;

	TUniquePtr<IFlightAsyncExecutor> Executor = CreatePlatformExecutor();
	TestTrue(TEXT("Executor should be valid"), Executor.IsValid());

	int64 TrackingId = 12345;
	TSharedPtr<TReactiveValue<bool>> Status = Executor->WaitOnGpuWork(TrackingId);

	TestNotNull(TEXT("Status ReactiveValue should be valid"), Status.Get());
	TestFalse(TEXT("Initially status should be false"), Status->Get());

	bool bCallbackFired = false;
	Status->SubscribeLambda([&](bool Old, bool New) {
		if (New) bCallbackFired = true;
	});

	Status->Set(true);
	TestTrue(TEXT("Reactive callback should have fired"), bCallbackFired);

	return true;
}
