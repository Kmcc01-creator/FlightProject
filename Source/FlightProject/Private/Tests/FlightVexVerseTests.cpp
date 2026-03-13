// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Verse/FlightGpuScriptBridge.h"
#include "Vex/FlightVexBackendCapabilities.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexSimdExecutor.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Vex/FlightVexSymbolRegistry.h"
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

	const void* GetVerseTestGpuTypeKey()
	{
		static uint8 TypeKeyMarker = 0;
		return &TypeKeyMarker;
	}

	Flight::Vex::FVexTypeSchema BuildVerseTestGpuSchema()
	{
		using namespace Flight::Vex;

		FVexTypeSchema Schema;
		Schema.TypeId.RuntimeKey = GetVerseTestGpuTypeKey();
		Schema.TypeId.StableName = TEXT("FVerseTestGpuCommitState");
		Schema.TypeName = TEXT("FVerseTestGpuCommitState");
		Schema.Size = sizeof(float);
		Schema.Alignment = alignof(float);

		FVexSymbolRecord Shield;
		Shield.SymbolName = TEXT("@shield");
		Shield.ValueType = EVexValueType::Float;
		Shield.Residency = EFlightVexSymbolResidency::GpuOnly;
		Shield.Storage.Kind = EVexStorageKind::GpuBufferElement;
		Shield.Storage.BufferBinding = TEXT("ShieldBuffer");
		Shield.Storage.ElementStride = sizeof(float);
		Schema.SymbolRecords.Add(Shield.SymbolName, MoveTemp(Shield));

		Schema.RebuildLegacyViews();
		Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
		return Schema;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightCompileArtifactReportTest, "FlightProject.Functional.Vex.CompileArtifactReport.Core", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightBoundaryCompileArtifactReportTest, "FlightProject.Functional.Vex.CompileArtifactReport.Boundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseBackendCommitTruthTest, "FlightProject.Functional.Verse.BackendCommitTruth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseRuntimeDispatchGatingTest, "FlightProject.Functional.Verse.RuntimeDispatchGating", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompilePolicyIntegrationTest, "FlightProject.Functional.Verse.CompilePolicyIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompositeSequenceExecutionTest, "FlightProject.Functional.Verse.CompositeSequenceExecution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompositeSequenceRegistrationFailureTest, "FlightProject.Functional.Verse.CompositeSequenceRegistrationFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompositeCapabilityEnvelopeTest, "FlightProject.Functional.Verse.CompositeCapabilityEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompositeSelectorExecutionTest, "FlightProject.Functional.Verse.CompositeSelectorExecution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseCompositeSelectorCapabilityEnvelopeTest, "FlightProject.Functional.Verse.CompositeSelectorCapabilityEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseGuardedCompositeSelectorExecutionTest, "FlightProject.Functional.Verse.GuardedCompositeSelectorExecution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseGuardedCompositeSelectorRegistrationFailureTest, "FlightProject.Functional.Verse.GuardedCompositeSelectorRegistrationFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVerseGpuTerminalCommitTest, "FlightProject.Functional.Verse.GpuTerminalCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightGpuScriptBridgeDeferredCompletionTest, "FlightProject.Gpu.ScriptBridge.DeferredCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

	FString SelectedBackend;
	TestTrue(TEXT("Compile artifact JSON should contain selectedBackend"), RootObject->TryGetStringField(TEXT("selectedBackend"), SelectedBackend));
	TestTrue(TEXT("Compile artifact report should record a non-empty selected backend"), !SelectedBackend.IsEmpty());

	FString CommittedBackend;
	TestTrue(TEXT("Compile artifact JSON should contain committedBackend"), RootObject->TryGetStringField(TEXT("committedBackend"), CommittedBackend));
	TestTrue(TEXT("Compile artifact report should record a non-empty committed backend"), !CommittedBackend.IsEmpty());

	FString TargetFingerprint;
	TestTrue(TEXT("Compile artifact JSON should contain targetFingerprint"), RootObject->TryGetStringField(TEXT("targetFingerprint"), TargetFingerprint));
	TestTrue(TEXT("Compile artifact report should record a non-empty target fingerprint"), !TargetFingerprint.IsEmpty());

	const TArray<TSharedPtr<FJsonValue>>* BackendReports = nullptr;
	TestTrue(TEXT("Compile artifact JSON should contain backendReports"), RootObject->TryGetArrayField(TEXT("backendReports"), BackendReports));
	TestTrue(TEXT("Compile artifact report should record at least one backend report"), BackendReports && BackendReports->Num() > 0);

	const TArray<TSharedPtr<FJsonValue>>* VectorSymbolReports = nullptr;
	TestTrue(TEXT("Compile artifact JSON should contain vectorSymbolReports"), RootObject->TryGetArrayField(TEXT("vectorSymbolReports"), VectorSymbolReports));

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

bool FFlightVerseBackendCommitTruthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for backend commit truth test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();

	{
		const FString LiteralSource = TEXT("@cpu { @velocity = @position; }");
		FString CompileDiagnostics;
		const bool bCompiled = VerseSubsystem->CompileVex(9201, LiteralSource, CompileDiagnostics, TypeKey);
		TestTrue(TEXT("Schema-bound literal behavior should compile"), bCompiled);

		const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(9201);
		TestNotNull(TEXT("Literal behavior metadata should be registered"), Behavior);
		if (Behavior)
		{
			const FString DirectBackend = VerseSubsystem->DescribeCommittedExecutionBackend(9201, TypeKey);
			TestTrue(TEXT("Selected backend should be recorded for schema-bound literal behavior"), !Behavior->SelectedBackend.IsEmpty());
			TestTrue(TEXT("Committed backend should be recorded for schema-bound literal behavior"), !Behavior->CommittedBackend.IsEmpty());
			TestEqual(TEXT("Committed backend should match direct execution backend"), Behavior->CommittedBackend, DirectBackend);
			TestEqual(TEXT("Compile artifact report should preserve committed backend truth"), Behavior->CompileArtifactReport.CommittedBackend, DirectBackend);
			TestEqual(TEXT("Literal direct execution should commit to native scalar backend"), DirectBackend, FString(TEXT("NativeScalar")));
			TestTrue(TEXT("Literal behavior should retain native fallback availability"), Behavior->bUsesNativeFallback);
		}

		Flight::Swarm::FDroidState DroidState;
		DroidState.Position = FVector3f(3.0f, 6.0f, 9.0f);
		DroidState.Velocity = FVector3f::ZeroVector;
		VerseSubsystem->ExecuteBehaviorOnStruct(9201, &DroidState, TypeKey);
		TestEqual(TEXT("Committed native scalar path should execute against the schema-bound state"), DroidState.Velocity, DroidState.Position);
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	{
		const FString AsyncSource = TEXT("@cpu @async { @velocity = @position; }");
		FString CompileDiagnostics;
		const bool bCompiled = VerseSubsystem->CompileVex(9202, AsyncSource, CompileDiagnostics, TypeKey);
		TestTrue(TEXT("Async schema-bound behavior should compile when Verse VM is available"), bCompiled);

		const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(9202);
		TestNotNull(TEXT("Async behavior metadata should be registered"), Behavior);
		if (Behavior)
		{
			const FString DirectBackend = VerseSubsystem->DescribeCommittedExecutionBackend(9202, TypeKey);
			TestEqual(TEXT("Async committed backend should match direct execution backend"), Behavior->CommittedBackend, DirectBackend);
			TestEqual(TEXT("Async compile artifact report should preserve committed backend truth"), Behavior->CompileArtifactReport.CommittedBackend, DirectBackend);
			TestEqual(TEXT("Async direct execution should commit to Verse VM backend"), DirectBackend, FString(TEXT("VerseVm")));
			TestTrue(TEXT("Async behavior should expose a VM entrypoint"), Behavior->bUsesVmEntryPoint);
			TestFalse(TEXT("Async behavior should not claim native fallback availability"), Behavior->bUsesNativeFallback);
		}
	}
#else
	AddInfo(TEXT("Skipping async backend commit truth assertions because Verse VM is unavailable in this build."));
#endif

	return true;
}

bool FFlightVerseRuntimeDispatchGatingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for runtime dispatch gating test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();

	FString CompileDiagnostics;
	const bool bCompiled = VerseSubsystem->CompileVex(9203, TEXT("@cpu { @velocity = @position; }"), CompileDiagnostics, TypeKey);
	TestTrue(TEXT("Literal schema-bound behavior should compile for runtime dispatch gating"), bCompiled);

	UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(9203);
	TestNotNull(TEXT("Runtime dispatch gating test should have a compiled behavior"), Behavior);
	if (!Behavior)
	{
		return false;
	}

	const FString OriginalSelectedBackend = Behavior->SelectedBackend;

	Behavior->SelectedBackend = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeScalar);
	TestEqual(
		TEXT("Bulk execution should honor an explicit native-scalar selection"),
		VerseSubsystem->DescribeBulkExecutionBackend(9203),
		FString(TEXT("NativeScalar")));

	if (Behavior->SimdPlan.IsValid())
	{
		Behavior->SelectedBackend = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeSimd);
		TestEqual(
			TEXT("Bulk execution should honor an explicit SIMD selection when a SIMD plan exists"),
			VerseSubsystem->DescribeBulkExecutionBackend(9203),
			FString(TEXT("NativeSimd")));
	}
	else
	{
		AddInfo(TEXT("Skipping NativeSimd bulk dispatch assertion because no SIMD plan was produced for the literal test behavior."));
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (Behavior->bUsesVmEntryPoint && Behavior->Procedure.Get())
	{
		Behavior->SelectedBackend = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::VerseVm);
		TestEqual(
			TEXT("Direct struct execution should honor an explicit Verse VM selection when a VM entrypoint exists"),
			VerseSubsystem->DescribeCommittedExecutionBackend(9203, TypeKey),
			FString(TEXT("VerseVm")));

		Flight::Swarm::FDroidState DroidState;
		DroidState.Position = FVector3f(4.0f, 8.0f, 12.0f);
		DroidState.Velocity = FVector3f::ZeroVector;
		VerseSubsystem->ExecuteBehaviorOnStruct(9203, &DroidState, TypeKey);
		TestEqual(TEXT("Verse VM-selected struct execution should still apply the behavior"), DroidState.Velocity, DroidState.Position);
	}
	else
	{
		AddInfo(TEXT("Skipping Verse VM direct dispatch assertion because the compiled behavior does not expose a VM entrypoint in this build."));
	}
#else
	AddInfo(TEXT("Skipping Verse VM direct dispatch assertion because Verse VM is unavailable in this build."));
#endif

	Behavior->SelectedBackend = OriginalSelectedBackend;
	return true;
}

bool FFlightVerseCompilePolicyIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for compile policy integration test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();

	FFlightBehaviorCompilePolicyRow PolicyRow;
	PolicyRow.RowName = TEXT("Policy.NativeCpu");
	PolicyRow.PreferredDomain = EFlightBehaviorCompileDomainPreference::NativeCpu;
	PolicyRow.RequiredContracts = { TEXT("Nav.Route") };
	PolicyRow.RequiredSymbols = { TEXT("@position") };

	FString CompileDiagnostics;
	const bool bCompiled = UFlightScriptingLibrary::CompileVexWithExplicitPolicy(
		World,
		9204,
		TEXT("@cpu { @velocity = @position; }"),
		Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey(),
		PolicyRow,
		CompileDiagnostics);
	TestTrue(TEXT("Compile should succeed with an explicit native CPU policy"), bCompiled);

	const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(9204);
	TestNotNull(TEXT("Compile policy integration test should persist behavior metadata"), Behavior);
	if (!Behavior)
	{
		return false;
	}

	TestEqual(TEXT("Behavior should record the selected policy row"), Behavior->SelectedPolicyRowName, PolicyRow.RowName);
	TestEqual(TEXT("Behavior should record the preferred domain from policy"), Behavior->PolicyPreferredDomain, EFlightBehaviorCompileDomainPreference::NativeCpu);
	TestEqual(TEXT("Policy should steer selected backend to NativeScalar"), Behavior->SelectedBackend, FString(TEXT("NativeScalar")));
	TestTrue(TEXT("Behavior should carry policy-required contracts"), Behavior->RequiredContracts.Contains(TEXT("Nav.Route")));
	TestTrue(TEXT("Behavior should carry policy-required symbols"), Behavior->PolicyRequiredSymbols.Contains(TEXT("@position")));
	TestTrue(TEXT("Compile artifact report should retain a non-empty commit detail"), !Behavior->CommitDetail.IsEmpty());

	const Flight::Vex::FFlightCompileArtifactReport* Report = VerseSubsystem->GetBehaviorCompileArtifactReport(9204);
	TestNotNull(TEXT("Compile policy integration should produce an artifact report"), Report);
	if (Report)
	{
		TestEqual(TEXT("Artifact report should serialize the selected policy row"), Report->SelectedPolicyRow, PolicyRow.RowName.ToString());
		TestEqual(TEXT("Artifact report should serialize the preferred policy domain"), Report->PolicyPreferredDomain, FString(TEXT("NativeCpu")));
		TestTrue(TEXT("Artifact report should retain policy-required contracts"), Report->PolicyRequiredContracts.Contains(TEXT("Nav.Route")));
		TestTrue(TEXT("Artifact report should retain policy-required symbols"), Report->PolicyRequiredSymbols.Contains(TEXT("@position")));
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	TestNotNull(TEXT("Compile policy integration test should have access to orchestration"), Orchestration);
	if (Orchestration)
	{
		const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
			[](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
			{
				return Candidate.BehaviorID == 9204;
			});
		TestNotNull(TEXT("Orchestration report should surface the policy-selected behavior"), BehaviorRecord);
		if (BehaviorRecord)
		{
			TestEqual(TEXT("Orchestration report should preserve the selected policy row"), BehaviorRecord->SelectedPolicyRow, PolicyRow.RowName.ToString());
			TestEqual(TEXT("Orchestration report should preserve the policy-preferred domain"), BehaviorRecord->PolicyPreferredDomain, FString(TEXT("NativeCpu")));
			TestTrue(TEXT("Orchestration report should preserve required contracts"), BehaviorRecord->RequiredContracts.Contains(TEXT("Nav.Route")));
		}
	}

	return true;
}

bool FFlightVerseCompositeSequenceExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for composite sequence execution test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 CompositeBehaviorId = 19300;
	const uint32 FirstChildBehaviorId = 19301;
	const uint32 SecondChildBehaviorId = 19302;
	FFlightBehaviorCompilePolicyRow PolicyRow;
	PolicyRow.RowName = TEXT("Policy.CompositeSequenceTest");
	UFlightVerseSubsystem::FCompilePolicyContext CompilePolicyContext;
	CompilePolicyContext.ExplicitPolicy = &PolicyRow;

	FString CompileDiagnostics;
	TestTrue(
		TEXT("First child behavior should compile"),
		VerseSubsystem->CompileVex(FirstChildBehaviorId, TEXT("@cpu { @position = @velocity; }"), CompileDiagnostics, TypeKey, CompilePolicyContext));
	TestTrue(
		TEXT("Second child behavior should compile"),
		VerseSubsystem->CompileVex(SecondChildBehaviorId, TEXT("@cpu { @velocity = @position; }"), CompileDiagnostics, TypeKey, CompilePolicyContext));

	TArray<uint32> ChildBehaviorIds = { FirstChildBehaviorId, SecondChildBehaviorId };
	FString RegistrationErrors;
	const bool bRegistered = VerseSubsystem->RegisterCompositeSequenceBehavior(CompositeBehaviorId, ChildBehaviorIds, RegistrationErrors, TypeKey);
	TestTrue(TEXT("Composite sequence behavior should register"), bRegistered);
	TestTrue(TEXT("Composite sequence registration should not report errors"), RegistrationErrors.IsEmpty());

	Flight::Swarm::FDroidState DroidState;
	DroidState.Position = FVector3f::ZeroVector;
	DroidState.Velocity = FVector3f(1.0f, 2.0f, 3.0f);

	VerseSubsystem->ExecuteBehavior(CompositeBehaviorId, DroidState);

	const FVector3f ExpectedPosition(1.0f, 2.0f, 3.0f);
	TestEqual(TEXT("Composite sequence should execute the first child and overwrite position"), DroidState.Position, ExpectedPosition);
	TestEqual(TEXT("Composite sequence should execute the second child after the first and copy the new position into velocity"), DroidState.Velocity, ExpectedPosition);

	const UFlightVerseSubsystem::FVerseBehavior* CompositeBehavior = VerseSubsystem->Behaviors.Find(CompositeBehaviorId);
	TestNotNull(TEXT("Composite behavior metadata should be registered"), CompositeBehavior);
	if (!CompositeBehavior)
	{
		return false;
	}

	TestEqual(TEXT("Composite behavior should use the sequence kind"), CompositeBehavior->Kind, EFlightVerseBehaviorKind::Sequence);
	TestEqual(TEXT("Composite behavior should retain the child count"), CompositeBehavior->ChildBehaviorIds.Num(), 2);
	TestEqual(TEXT("Composite behavior should expose a composite bulk backend label"), VerseSubsystem->DescribeBulkExecutionBackend(CompositeBehaviorId), FString(TEXT("CompositeSequence")));
	TestTrue(TEXT("Composite behavior capability envelope should expose at least one legal lane"), CompositeBehavior->CapabilityEnvelope.LegalLanes.Num() > 0);
	TestTrue(TEXT("Composite behavior capability envelope should require a shared type key"), CompositeBehavior->CapabilityEnvelope.bRequiresSharedTypeKey);
	TestFalse(TEXT("Composite behavior capability envelope should not allow mixed-lane execution in phase one"), CompositeBehavior->CapabilityEnvelope.bAllowsMixedLaneExecution);
	TestTrue(TEXT("Composite behavior capability envelope should record a non-empty execution shape"), CompositeBehavior->CapabilityEnvelope.ExecutionShape != EFlightBehaviorExecutionShape::Unknown);

	const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
		[CompositeBehaviorId](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
		{
			return Candidate.BehaviorID == CompositeBehaviorId;
		});
	TestNotNull(TEXT("Orchestration report should surface the composite sequence behavior"), BehaviorRecord);
	if (BehaviorRecord)
	{
		TestTrue(TEXT("Orchestration behavior report should mark the behavior as composite"), BehaviorRecord->bIsComposite);
		TestEqual(TEXT("Orchestration behavior report should record the sequence operator"), BehaviorRecord->CompositeOperator, FString(TEXT("Sequence")));
		TestEqual(TEXT("Orchestration behavior report should preserve child behavior ids"), BehaviorRecord->ChildBehaviorIds.Num(), 2);
		TestTrue(TEXT("Composite behavior report should be executable"), BehaviorRecord->bExecutable);
		TestTrue(TEXT("Composite behavior report should expose legal lanes"), BehaviorRecord->LegalLanes.Num() > 0);
		TestEqual(TEXT("Composite behavior report should require a shared type key"), BehaviorRecord->bRequiresSharedTypeKey, true);
		TestEqual(TEXT("Composite behavior report should preserve the phase-one mixed-lane rule"), BehaviorRecord->bAllowsMixedLaneExecution, false);
		TestTrue(TEXT("Composite behavior report should surface a non-empty execution shape"), !BehaviorRecord->ExecutionShape.IsEmpty());
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Orchestration JSON report should be available"), !ReportJson.IsEmpty());

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Orchestration JSON report should deserialize"), bParsed);
	if (bParsed && RootObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* BehaviorValues = nullptr;
		TestTrue(TEXT("Orchestration JSON should contain behaviors"), RootObject->TryGetArrayField(TEXT("behaviors"), BehaviorValues));
		bool bFoundCompositeJsonRecord = false;
		if (BehaviorValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *BehaviorValues)
			{
				const TSharedPtr<FJsonObject> BehaviorObject = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!BehaviorObject.IsValid())
				{
					continue;
				}

				double BehaviorId = 0.0;
				if (!BehaviorObject->TryGetNumberField(TEXT("behaviorId"), BehaviorId) || static_cast<uint32>(BehaviorId) != CompositeBehaviorId)
				{
					continue;
				}

				bool bIsComposite = false;
				FString CompositeOperator;
				FString ExecutionShape;
				const TArray<TSharedPtr<FJsonValue>>* ChildIds = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* LegalLanes = nullptr;
				TestTrue(TEXT("Composite JSON record should include isComposite"), BehaviorObject->TryGetBoolField(TEXT("isComposite"), bIsComposite));
				TestTrue(TEXT("Composite JSON record should include compositeOperator"), BehaviorObject->TryGetStringField(TEXT("compositeOperator"), CompositeOperator));
				TestTrue(TEXT("Composite JSON record should include childBehaviorIds"), BehaviorObject->TryGetArrayField(TEXT("childBehaviorIds"), ChildIds));
				TestTrue(TEXT("Composite JSON record should include legalLanes"), BehaviorObject->TryGetArrayField(TEXT("legalLanes"), LegalLanes));
				TestTrue(TEXT("Composite JSON record should include executionShape"), BehaviorObject->TryGetStringField(TEXT("executionShape"), ExecutionShape));
				TestTrue(TEXT("Composite JSON record should mark the behavior as composite"), bIsComposite);
				TestEqual(TEXT("Composite JSON record should expose the sequence operator"), CompositeOperator, FString(TEXT("Sequence")));
				TestTrue(TEXT("Composite JSON record should contain two child ids"), ChildIds && ChildIds->Num() == 2);
				TestTrue(TEXT("Composite JSON record should contain at least one legal lane"), LegalLanes && LegalLanes->Num() > 0);
				TestFalse(TEXT("Composite JSON record should expose a non-empty execution shape"), ExecutionShape.IsEmpty());
				bFoundCompositeJsonRecord = true;
				break;
			}
		}
		TestTrue(TEXT("Orchestration JSON should contain the composite sequence behavior record"), bFoundCompositeJsonRecord);
	}

	return true;
}

bool FFlightVerseCompositeCapabilityEnvelopeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for composite capability envelope test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 CompositeBehaviorId = 19320;
	const uint32 VectorChildBehaviorId = 19321;
	const uint32 ScalarChildBehaviorId = 19322;

	UFlightVerseSubsystem::FVerseBehavior& VectorChildSeed = VerseSubsystem->Behaviors.FindOrAdd(VectorChildBehaviorId);
	VectorChildSeed = UFlightVerseSubsystem::FVerseBehavior();
	VectorChildSeed.Kind = EFlightVerseBehaviorKind::Atomic;
	VectorChildSeed.CompileState = EFlightVerseCompileState::VmCompiled;
	VectorChildSeed.Tier = Flight::Vex::EVexTier::Literal;
	VectorChildSeed.bHasExecutableProcedure = true;
	VectorChildSeed.bUsesNativeFallback = true;
	VectorChildSeed.FrameInterval = 1;
	VectorChildSeed.BoundTypeKey = TypeKey;
	VectorChildSeed.BoundTypeStableName = TEXT("FDroidState");
	VectorChildSeed.SelectedBackend = TEXT("NativeSimd");
	VectorChildSeed.CommittedBackend = TEXT("NativeScalar");
	VectorChildSeed.CommitDetail = TEXT("Synthetic vector-capable test behavior.");
	VectorChildSeed.LastCompileDiagnostics = TEXT("Synthetic vector-capable test behavior.");
	VectorChildSeed.SimdPlan = MakeShared<Flight::Vex::FVexSimdExecutor>();

	UFlightVerseSubsystem::FVerseBehavior& ScalarChildSeed = VerseSubsystem->Behaviors.FindOrAdd(ScalarChildBehaviorId);
	ScalarChildSeed = UFlightVerseSubsystem::FVerseBehavior();
	ScalarChildSeed.Kind = EFlightVerseBehaviorKind::Atomic;
	ScalarChildSeed.CompileState = EFlightVerseCompileState::VmCompiled;
	ScalarChildSeed.Tier = Flight::Vex::EVexTier::Full;
	ScalarChildSeed.bHasExecutableProcedure = true;
	ScalarChildSeed.bUsesNativeFallback = true;
	ScalarChildSeed.FrameInterval = 1;
	ScalarChildSeed.BoundTypeKey = TypeKey;
	ScalarChildSeed.BoundTypeStableName = TEXT("FDroidState");
	ScalarChildSeed.SelectedBackend = TEXT("NativeScalar");
	ScalarChildSeed.CommittedBackend = TEXT("NativeScalar");
	ScalarChildSeed.CommitDetail = TEXT("Synthetic scalar-only test behavior.");
	ScalarChildSeed.LastCompileDiagnostics = TEXT("Synthetic scalar-only test behavior.");

	TArray<uint32> ChildBehaviorIds = { VectorChildBehaviorId, ScalarChildBehaviorId };
	FString RegistrationErrors;
	TestTrue(
		TEXT("Composite capability test sequence should register"),
		VerseSubsystem->RegisterCompositeSequenceBehavior(CompositeBehaviorId, ChildBehaviorIds, RegistrationErrors, TypeKey));
	TestTrue(TEXT("Composite capability test registration should not report errors"), RegistrationErrors.IsEmpty());

	const UFlightVerseSubsystem::FVerseBehavior* VectorChild = VerseSubsystem->Behaviors.Find(VectorChildBehaviorId);
	const UFlightVerseSubsystem::FVerseBehavior* ScalarChild = VerseSubsystem->Behaviors.Find(ScalarChildBehaviorId);
	const UFlightVerseSubsystem::FVerseBehavior* CompositeBehavior = VerseSubsystem->Behaviors.Find(CompositeBehaviorId);
	TestNotNull(TEXT("Vector child behavior should exist"), VectorChild);
	TestNotNull(TEXT("Scalar child behavior should exist"), ScalarChild);
	TestNotNull(TEXT("Composite behavior should exist"), CompositeBehavior);
	if (!VectorChild || !ScalarChild || !CompositeBehavior)
	{
		return false;
	}

	TestTrue(TEXT("Vector child should retain NativeSimd as a legal lane"), VectorChild->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeSimd")));
	TestTrue(TEXT("Vector child should expose a vector-capable execution shape"), VectorChild->CapabilityEnvelope.ExecutionShape == EFlightBehaviorExecutionShape::VectorPreferred || VectorChild->CapabilityEnvelope.ExecutionShape == EFlightBehaviorExecutionShape::VectorCapable);
	TestFalse(TEXT("Scalar child should not expose NativeSimd as a legal lane"), ScalarChild->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeSimd")));
	TestEqual(TEXT("Scalar child should degrade to a scalar-only execution shape"), ScalarChild->CapabilityEnvelope.ExecutionShape, EFlightBehaviorExecutionShape::ScalarOnly);
	TestFalse(TEXT("Composite sequence should not retain NativeSimd when one child is scalar-only"), CompositeBehavior->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeSimd")));
	TestTrue(TEXT("Composite sequence should retain NativeScalar as a legal lane"), CompositeBehavior->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeScalar")));
	TestEqual(TEXT("Composite sequence should aggregate to a scalar-only execution shape"), CompositeBehavior->CapabilityEnvelope.ExecutionShape, EFlightBehaviorExecutionShape::ScalarOnly);
	TestFalse(TEXT("Composite sequence should keep mixed-lane execution disabled"), CompositeBehavior->CapabilityEnvelope.bAllowsMixedLaneExecution);
	TestTrue(TEXT("Composite sequence should require a shared type key"), CompositeBehavior->CapabilityEnvelope.bRequiresSharedTypeKey);

	Orchestration->Rebuild();
	const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
		[CompositeBehaviorId](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
		{
			return Candidate.BehaviorID == CompositeBehaviorId;
		});
	TestNotNull(TEXT("Orchestration should surface the composite capability behavior"), BehaviorRecord);
	if (BehaviorRecord)
	{
		TestFalse(TEXT("Composite report should not expose NativeSimd once aggregation removes it"), BehaviorRecord->LegalLanes.Contains(TEXT("NativeSimd")));
		TestTrue(TEXT("Composite report should preserve NativeScalar legality"), BehaviorRecord->LegalLanes.Contains(TEXT("NativeScalar")));
		TestEqual(TEXT("Composite report should preserve scalar-only execution shape"), BehaviorRecord->ExecutionShape, FString(TEXT("ScalarOnly")));
	}

	return true;
}

bool FFlightVerseCompositeSelectorExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for composite selector execution test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 SelectorBehaviorId = 19330;
	const uint32 FailingChildBehaviorId = 19331;
	const uint32 FallbackChildBehaviorId = 19332;

	UFlightVerseSubsystem::FVerseBehavior& FailingChild = VerseSubsystem->Behaviors.FindOrAdd(FailingChildBehaviorId);
	FailingChild = UFlightVerseSubsystem::FVerseBehavior();
	FailingChild.Kind = EFlightVerseBehaviorKind::Atomic;
	FailingChild.CompileState = EFlightVerseCompileState::VmCompiled;
	FailingChild.bHasExecutableProcedure = true;
	FailingChild.FrameInterval = 1;
	FailingChild.BoundTypeKey = TypeKey;
	FailingChild.BoundTypeStableName = TEXT("FDroidState");
	FailingChild.SelectedBackend = TEXT("NativeScalar");
	FailingChild.CommittedBackend = TEXT("Unknown");
	FailingChild.LastCompileDiagnostics = TEXT("Synthetic selector failure branch.");

	FFlightBehaviorCompilePolicyRow PolicyRow;
	PolicyRow.RowName = TEXT("Policy.CompositeSelectorExecutionTest");
	UFlightVerseSubsystem::FCompilePolicyContext CompilePolicyContext;
	CompilePolicyContext.ExplicitPolicy = &PolicyRow;

	FString CompileDiagnostics;
	TestTrue(
		TEXT("Fallback child behavior should compile"),
		VerseSubsystem->CompileVex(FallbackChildBehaviorId, TEXT("@cpu { @position = @velocity; }"), CompileDiagnostics, TypeKey, CompilePolicyContext));

	TArray<uint32> ChildBehaviorIds = { FailingChildBehaviorId, FallbackChildBehaviorId };
	FString RegistrationErrors;
	TestTrue(
		TEXT("Composite selector behavior should register"),
		VerseSubsystem->RegisterCompositeSelectorBehavior(SelectorBehaviorId, ChildBehaviorIds, RegistrationErrors, TypeKey));
	TestTrue(TEXT("Composite selector registration should not report errors"), RegistrationErrors.IsEmpty());

	Flight::Swarm::FDroidState DroidState;
	DroidState.Position = FVector3f::ZeroVector;
	DroidState.Velocity = FVector3f(5.0f, 6.0f, 7.0f);

	VerseSubsystem->ExecuteBehavior(SelectorBehaviorId, DroidState);

	const FVector3f ExpectedPosition(5.0f, 6.0f, 7.0f);
	TestEqual(TEXT("Composite selector should fall back to the second child after the first branch fails"), DroidState.Position, ExpectedPosition);

	const UFlightVerseSubsystem::FVerseBehavior* SelectorBehavior = VerseSubsystem->Behaviors.Find(SelectorBehaviorId);
	TestNotNull(TEXT("Composite selector behavior metadata should be registered"), SelectorBehavior);
	if (!SelectorBehavior)
	{
		return false;
	}

	TestEqual(TEXT("Composite selector should use the selector kind"), SelectorBehavior->Kind, EFlightVerseBehaviorKind::Selector);
	TestEqual(TEXT("Composite selector should retain the child count"), SelectorBehavior->ChildBehaviorIds.Num(), 2);
	TestEqual(TEXT("Composite selector should expose a composite bulk backend label"), VerseSubsystem->DescribeBulkExecutionBackend(SelectorBehaviorId), FString(TEXT("CompositeSelector")));
	TestTrue(TEXT("Composite selector capability envelope should allow mixed-lane execution"), SelectorBehavior->CapabilityEnvelope.bAllowsMixedLaneExecution);
	TestTrue(TEXT("Composite selector should expose NativeScalar as a legal lane"), SelectorBehavior->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeScalar")));

	const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
		[SelectorBehaviorId](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
		{
			return Candidate.BehaviorID == SelectorBehaviorId;
		});
	TestNotNull(TEXT("Orchestration report should surface the composite selector behavior"), BehaviorRecord);
	if (BehaviorRecord)
	{
		TestTrue(TEXT("Composite selector report should mark the behavior as composite"), BehaviorRecord->bIsComposite);
		TestEqual(TEXT("Composite selector report should record the selector operator"), BehaviorRecord->CompositeOperator, FString(TEXT("Selector")));
		TestTrue(TEXT("Composite selector report should allow mixed-lane execution"), BehaviorRecord->bAllowsMixedLaneExecution);
		TestTrue(TEXT("Composite selector report should preserve NativeScalar legality"), BehaviorRecord->LegalLanes.Contains(TEXT("NativeScalar")));
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Orchestration JSON report should be available for composite selector"), !ReportJson.IsEmpty());

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Composite selector orchestration JSON should deserialize"), bParsed);
	if (bParsed && RootObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* BehaviorValues = nullptr;
		TestTrue(TEXT("Composite selector JSON should contain behaviors"), RootObject->TryGetArrayField(TEXT("behaviors"), BehaviorValues));
		bool bFoundSelectorJsonRecord = false;
		if (BehaviorValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *BehaviorValues)
			{
				const TSharedPtr<FJsonObject> BehaviorObject = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!BehaviorObject.IsValid())
				{
					continue;
				}

				double BehaviorId = 0.0;
				if (!BehaviorObject->TryGetNumberField(TEXT("behaviorId"), BehaviorId) || static_cast<uint32>(BehaviorId) != SelectorBehaviorId)
				{
					continue;
				}

				FString CompositeOperator;
				bool bAllowsMixedLaneExecution = false;
				TestTrue(TEXT("Composite selector JSON should include compositeOperator"), BehaviorObject->TryGetStringField(TEXT("compositeOperator"), CompositeOperator));
				TestTrue(TEXT("Composite selector JSON should include allowsMixedLaneExecution"), BehaviorObject->TryGetBoolField(TEXT("allowsMixedLaneExecution"), bAllowsMixedLaneExecution));
				TestEqual(TEXT("Composite selector JSON should expose the selector operator"), CompositeOperator, FString(TEXT("Selector")));
				TestTrue(TEXT("Composite selector JSON should preserve mixed-lane allowance"), bAllowsMixedLaneExecution);
				bFoundSelectorJsonRecord = true;
				break;
			}
		}
		TestTrue(TEXT("Composite selector JSON should contain the selector behavior record"), bFoundSelectorJsonRecord);
	}

	return true;
}

bool FFlightVerseCompositeSelectorCapabilityEnvelopeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for composite selector capability envelope test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 SelectorBehaviorId = 19340;
	const uint32 VectorChildBehaviorId = 19341;
	const uint32 ScalarChildBehaviorId = 19342;

	UFlightVerseSubsystem::FVerseBehavior& VectorChildSeed = VerseSubsystem->Behaviors.FindOrAdd(VectorChildBehaviorId);
	VectorChildSeed = UFlightVerseSubsystem::FVerseBehavior();
	VectorChildSeed.Kind = EFlightVerseBehaviorKind::Atomic;
	VectorChildSeed.CompileState = EFlightVerseCompileState::VmCompiled;
	VectorChildSeed.Tier = Flight::Vex::EVexTier::Literal;
	VectorChildSeed.bHasExecutableProcedure = true;
	VectorChildSeed.bUsesNativeFallback = true;
	VectorChildSeed.FrameInterval = 1;
	VectorChildSeed.BoundTypeKey = TypeKey;
	VectorChildSeed.BoundTypeStableName = TEXT("FDroidState");
	VectorChildSeed.SelectedBackend = TEXT("NativeSimd");
	VectorChildSeed.CommittedBackend = TEXT("NativeScalar");
	VectorChildSeed.CommitDetail = TEXT("Synthetic vector-capable selector child.");
	VectorChildSeed.LastCompileDiagnostics = TEXT("Synthetic vector-capable selector child.");
	VectorChildSeed.SimdPlan = MakeShared<Flight::Vex::FVexSimdExecutor>();

	UFlightVerseSubsystem::FVerseBehavior& ScalarChildSeed = VerseSubsystem->Behaviors.FindOrAdd(ScalarChildBehaviorId);
	ScalarChildSeed = UFlightVerseSubsystem::FVerseBehavior();
	ScalarChildSeed.Kind = EFlightVerseBehaviorKind::Atomic;
	ScalarChildSeed.CompileState = EFlightVerseCompileState::VmCompiled;
	ScalarChildSeed.Tier = Flight::Vex::EVexTier::Full;
	ScalarChildSeed.bHasExecutableProcedure = true;
	ScalarChildSeed.bUsesNativeFallback = true;
	ScalarChildSeed.FrameInterval = 1;
	ScalarChildSeed.BoundTypeKey = TypeKey;
	ScalarChildSeed.BoundTypeStableName = TEXT("FDroidState");
	ScalarChildSeed.SelectedBackend = TEXT("NativeScalar");
	ScalarChildSeed.CommittedBackend = TEXT("NativeScalar");
	ScalarChildSeed.CommitDetail = TEXT("Synthetic scalar-only selector child.");
	ScalarChildSeed.LastCompileDiagnostics = TEXT("Synthetic scalar-only selector child.");

	TArray<uint32> ChildBehaviorIds = { VectorChildBehaviorId, ScalarChildBehaviorId };
	FString RegistrationErrors;
	TestTrue(
		TEXT("Composite selector capability behavior should register"),
		VerseSubsystem->RegisterCompositeSelectorBehavior(SelectorBehaviorId, ChildBehaviorIds, RegistrationErrors, TypeKey));
	TestTrue(TEXT("Composite selector capability registration should not report errors"), RegistrationErrors.IsEmpty());

	const UFlightVerseSubsystem::FVerseBehavior* SelectorBehavior = VerseSubsystem->Behaviors.Find(SelectorBehaviorId);
	TestNotNull(TEXT("Composite selector capability behavior should exist"), SelectorBehavior);
	if (!SelectorBehavior)
	{
		return false;
	}

	TestTrue(TEXT("Composite selector should retain NativeSimd as a legal lane"), SelectorBehavior->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeSimd")));
	TestTrue(TEXT("Composite selector should retain NativeScalar as a legal lane"), SelectorBehavior->CapabilityEnvelope.LegalLanes.Contains(TEXT("NativeScalar")));
	TestEqual(TEXT("Composite selector should aggregate to a shape-agnostic execution shape when branches disagree"), SelectorBehavior->CapabilityEnvelope.ExecutionShape, EFlightBehaviorExecutionShape::ShapeAgnostic);
	TestTrue(TEXT("Composite selector should allow mixed-lane execution"), SelectorBehavior->CapabilityEnvelope.bAllowsMixedLaneExecution);
	TestTrue(TEXT("Composite selector should require a shared type key"), SelectorBehavior->CapabilityEnvelope.bRequiresSharedTypeKey);

	Orchestration->Rebuild();
	const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
		[SelectorBehaviorId](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
		{
			return Candidate.BehaviorID == SelectorBehaviorId;
		});
	TestNotNull(TEXT("Orchestration should surface the selector capability behavior"), BehaviorRecord);
	if (BehaviorRecord)
	{
		TestTrue(TEXT("Selector capability report should preserve NativeSimd legality"), BehaviorRecord->LegalLanes.Contains(TEXT("NativeSimd")));
		TestTrue(TEXT("Selector capability report should preserve NativeScalar legality"), BehaviorRecord->LegalLanes.Contains(TEXT("NativeScalar")));
		TestEqual(TEXT("Selector capability report should preserve shape-agnostic execution shape"), BehaviorRecord->ExecutionShape, FString(TEXT("ShapeAgnostic")));
		TestTrue(TEXT("Selector capability report should preserve mixed-lane allowance"), BehaviorRecord->bAllowsMixedLaneExecution);
	}

	return true;
}

bool FFlightVerseGuardedCompositeSelectorExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for guarded composite selector execution test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 SelectorBehaviorId = 19350;
	const uint32 GuardedChildBehaviorId = 19351;
	const uint32 FallbackChildBehaviorId = 19352;

	FFlightBehaviorCompilePolicyRow PolicyRow;
	PolicyRow.RowName = TEXT("Policy.GuardedCompositeSelectorExecutionTest");
	UFlightVerseSubsystem::FCompilePolicyContext CompilePolicyContext;
	CompilePolicyContext.ExplicitPolicy = &PolicyRow;

	FString CompileDiagnostics;
	TestTrue(
		TEXT("Guarded child behavior should compile"),
		VerseSubsystem->CompileVex(GuardedChildBehaviorId, TEXT("@cpu { @position = @velocity; }"), CompileDiagnostics, TypeKey, CompilePolicyContext));
	TestTrue(
		TEXT("Fallback child behavior should compile"),
		VerseSubsystem->CompileVex(FallbackChildBehaviorId, TEXT("@cpu { @velocity = @position; }"), CompileDiagnostics, TypeKey, CompilePolicyContext));

	UFlightVerseSubsystem::FSelectorBranchGuard ShieldLowGuard;
	ShieldLowGuard.SymbolName = TEXT("@shield");
	ShieldLowGuard.Comparison = EFlightBehaviorGuardComparison::Less;
	ShieldLowGuard.ScalarValue = 10.0f;

	UFlightVerseSubsystem::FCompositeSelectorBranchSpec GuardedBranch;
	GuardedBranch.BehaviorID = GuardedChildBehaviorId;
	GuardedBranch.bHasGuard = true;
	GuardedBranch.Guard = ShieldLowGuard;

	UFlightVerseSubsystem::FCompositeSelectorBranchSpec FallbackBranch;
	FallbackBranch.BehaviorID = FallbackChildBehaviorId;

	TArray<UFlightVerseSubsystem::FCompositeSelectorBranchSpec> BranchSpecs = { GuardedBranch, FallbackBranch };
	FString RegistrationErrors;
	TestTrue(
		TEXT("Guarded composite selector should register"),
		VerseSubsystem->RegisterGuardedCompositeSelectorBehavior(SelectorBehaviorId, BranchSpecs, RegistrationErrors, TypeKey));
	TestTrue(TEXT("Guarded composite selector registration should not report errors"), RegistrationErrors.IsEmpty());

	Flight::Swarm::FDroidState GuardRejectedState;
	GuardRejectedState.Position = FVector3f(7.0f, 8.0f, 9.0f);
	GuardRejectedState.Velocity = FVector3f(3.0f, 4.0f, 5.0f);
	GuardRejectedState.Shield = 100.0f;
	VerseSubsystem->ExecuteBehavior(SelectorBehaviorId, GuardRejectedState);
	TestEqual(
		TEXT("Guarded selector should leave position untouched when the first branch guard rejects"),
		GuardRejectedState.Position,
		FVector3f(7.0f, 8.0f, 9.0f));
	TestEqual(
		TEXT("Guarded selector should execute the fallback branch when the first branch guard rejects"),
		GuardRejectedState.Velocity,
		FVector3f(7.0f, 8.0f, 9.0f));

	const Flight::Orchestration::FFlightBehaviorRecord* RejectedBehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
		[SelectorBehaviorId](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
		{
			return Candidate.BehaviorID == SelectorBehaviorId;
		});
	TestNotNull(TEXT("Guarded selector should report the rejected-guard execution"), RejectedBehaviorRecord);
	if (RejectedBehaviorRecord)
	{
		TestTrue(TEXT("Rejected-guard execution should be marked as a recent execution"), RejectedBehaviorRecord->bHasLastExecutionResult);
		TestTrue(TEXT("Rejected-guard execution should still succeed through fallback"), RejectedBehaviorRecord->bLastExecutionSucceeded);
		TestTrue(TEXT("Rejected-guard execution should commit through fallback"), RejectedBehaviorRecord->bLastExecutionCommitted);
		TestEqual(TEXT("Rejected-guard execution should report the fallback child as selected"), RejectedBehaviorRecord->LastSelectedChildBehaviorId, FallbackChildBehaviorId);
		TestTrue(
			TEXT("Rejected-guard execution should report a false guard outcome"),
			RejectedBehaviorRecord->LastGuardOutcomes.ContainsByPredicate([](const FString& Entry)
			{
				return Entry.Contains(TEXT("evaluated false"));
			}));
		TestTrue(
			TEXT("Rejected-guard execution commit detail should include branch evidence"),
			RejectedBehaviorRecord->CommitDetail.Contains(TEXT("BranchEvidence=[")));
	}

	Flight::Swarm::FDroidState GuardPassedState;
	GuardPassedState.Position = FVector3f::ZeroVector;
	GuardPassedState.Velocity = FVector3f(1.0f, 2.0f, 3.0f);
	GuardPassedState.Shield = 5.0f;
	VerseSubsystem->ExecuteBehavior(SelectorBehaviorId, GuardPassedState);
	TestEqual(
		TEXT("Guarded selector should choose the guarded branch when the guard passes"),
		GuardPassedState.Position,
		FVector3f(1.0f, 2.0f, 3.0f));
	TestEqual(
		TEXT("Guarded selector should not execute the fallback branch when the guard passes"),
		GuardPassedState.Velocity,
		FVector3f(1.0f, 2.0f, 3.0f));

	const UFlightVerseSubsystem::FVerseBehavior* SelectorBehavior = VerseSubsystem->Behaviors.Find(SelectorBehaviorId);
	TestNotNull(TEXT("Guarded selector behavior metadata should exist"), SelectorBehavior);
	if (!SelectorBehavior)
	{
		return false;
	}

	TestEqual(TEXT("Guarded selector should retain branch metadata"), SelectorBehavior->SelectorBranches.Num(), 2);
	TestTrue(TEXT("Guarded selector should retain the first branch guard"), SelectorBehavior->SelectorBranches[0].bHasGuard);
	TestTrue(TEXT("Guarded selector should record the latest execution truth"), SelectorBehavior->bHasLastExecutionResult);
	TestTrue(TEXT("Guarded selector should record the latest execution as succeeded"), SelectorBehavior->bLastExecutionSucceeded);
	TestTrue(TEXT("Guarded selector should record the latest execution as committed"), SelectorBehavior->bLastExecutionCommitted);
	TestEqual(TEXT("Guarded selector should record the guarded child as the latest selected branch"), SelectorBehavior->LastSelectedChildBehaviorId, GuardedChildBehaviorId);
	TestTrue(
		TEXT("Guarded selector should record a true guard outcome"),
		SelectorBehavior->LastGuardOutcomes.ContainsByPredicate([](const FString& Entry)
		{
			return Entry.Contains(TEXT("evaluated true"));
		}));

	const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
		[SelectorBehaviorId](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
		{
			return Candidate.BehaviorID == SelectorBehaviorId;
		});
	TestNotNull(TEXT("Guarded selector should appear in orchestration reports"), BehaviorRecord);
	if (BehaviorRecord)
	{
		TestEqual(TEXT("Guarded selector report should preserve the selector operator"), BehaviorRecord->CompositeOperator, FString(TEXT("Selector")));
		TestTrue(TEXT("Guarded selector report should expose guard summaries"), BehaviorRecord->GuardSummaries.Num() == 1);
		TestTrue(TEXT("Guarded selector report should expose last execution truth"), BehaviorRecord->bHasLastExecutionResult);
		TestTrue(TEXT("Guarded selector report should retain the guarded branch as the latest selected child"), BehaviorRecord->LastSelectedChildBehaviorId == GuardedChildBehaviorId);
		TestTrue(
			TEXT("Guarded selector report should expose the latest guard outcome"),
			BehaviorRecord->LastGuardOutcomes.ContainsByPredicate([](const FString& Entry)
			{
				return Entry.Contains(TEXT("evaluated true"));
			}));
		TestTrue(TEXT("Guarded selector report should retain the latest execution detail"), BehaviorRecord->LastExecutionDetail.Contains(TEXT("chose child behavior")));
		TestTrue(TEXT("Guarded selector report should retain selector branch evidence in commit detail"), BehaviorRecord->CommitDetail.Contains(TEXT("GuardOutcomes=[")));
		if (BehaviorRecord->GuardSummaries.Num() == 1)
		{
			TestTrue(TEXT("Guard summary should mention the shield guard"), BehaviorRecord->GuardSummaries[0].Contains(TEXT("@shield")));
		}
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Guarded selector orchestration JSON should be available"), !ReportJson.IsEmpty());

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Guarded selector orchestration JSON should deserialize"), bParsed);
	if (bParsed && RootObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* BehaviorValues = nullptr;
		TestTrue(TEXT("Guarded selector JSON should contain behaviors"), RootObject->TryGetArrayField(TEXT("behaviors"), BehaviorValues));
		bool bFoundGuardedSelectorJsonRecord = false;
		if (BehaviorValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *BehaviorValues)
			{
				const TSharedPtr<FJsonObject> BehaviorObject = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!BehaviorObject.IsValid())
				{
					continue;
				}

				double BehaviorId = 0.0;
				if (!BehaviorObject->TryGetNumberField(TEXT("behaviorId"), BehaviorId) || static_cast<uint32>(BehaviorId) != SelectorBehaviorId)
				{
					continue;
				}

				const TArray<TSharedPtr<FJsonValue>>* GuardSummaries = nullptr;
				TestTrue(TEXT("Guarded selector JSON should include guardSummaries"), BehaviorObject->TryGetArrayField(TEXT("guardSummaries"), GuardSummaries));
				TestTrue(TEXT("Guarded selector JSON should contain one guard summary"), GuardSummaries && GuardSummaries->Num() == 1);
				bool bHasLastExecutionResult = false;
				bool bLastExecutionCommitted = false;
				double LastSelectedChildBehaviorId = 0.0;
				const TArray<TSharedPtr<FJsonValue>>* LastGuardOutcomes = nullptr;
				FString LastExecutionDetail;
				TestTrue(TEXT("Guarded selector JSON should include hasLastExecutionResult"), BehaviorObject->TryGetBoolField(TEXT("hasLastExecutionResult"), bHasLastExecutionResult));
				TestTrue(TEXT("Guarded selector JSON should include lastExecutionCommitted"), BehaviorObject->TryGetBoolField(TEXT("lastExecutionCommitted"), bLastExecutionCommitted));
				TestTrue(TEXT("Guarded selector JSON should include lastSelectedChildBehaviorId"), BehaviorObject->TryGetNumberField(TEXT("lastSelectedChildBehaviorId"), LastSelectedChildBehaviorId));
				TestTrue(TEXT("Guarded selector JSON should include lastGuardOutcomes"), BehaviorObject->TryGetArrayField(TEXT("lastGuardOutcomes"), LastGuardOutcomes));
				TestTrue(TEXT("Guarded selector JSON should include lastExecutionDetail"), BehaviorObject->TryGetStringField(TEXT("lastExecutionDetail"), LastExecutionDetail));
				TestTrue(TEXT("Guarded selector JSON should report a recent execution"), bHasLastExecutionResult);
				TestTrue(TEXT("Guarded selector JSON should report a committed execution"), bLastExecutionCommitted);
				TestEqual(TEXT("Guarded selector JSON should report the guarded child as the latest selected child"), static_cast<uint32>(LastSelectedChildBehaviorId), GuardedChildBehaviorId);
				TestTrue(
					TEXT("Guarded selector JSON should include a true guard outcome"),
					LastGuardOutcomes && LastGuardOutcomes->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Entry)
					{
						return Entry.IsValid() && Entry->AsString().Contains(TEXT("evaluated true"));
					}));
				TestTrue(TEXT("Guarded selector JSON should retain the latest execution detail"), LastExecutionDetail.Contains(TEXT("chose child behavior")));
				bFoundGuardedSelectorJsonRecord = true;
				break;
			}
		}
		TestTrue(TEXT("Guarded selector JSON should contain the guarded selector behavior record"), bFoundGuardedSelectorJsonRecord);
	}

	return true;
}

bool FFlightVerseGuardedCompositeSelectorRegistrationFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for guarded composite selector registration failure test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 SelectorBehaviorId = 19360;
	const uint32 ChildBehaviorId = 19361;

	UFlightVerseSubsystem::FVerseBehavior& ChildBehavior = VerseSubsystem->Behaviors.FindOrAdd(ChildBehaviorId);
	ChildBehavior = UFlightVerseSubsystem::FVerseBehavior();
	ChildBehavior.Kind = EFlightVerseBehaviorKind::Atomic;
	ChildBehavior.CompileState = EFlightVerseCompileState::VmCompiled;
	ChildBehavior.bHasExecutableProcedure = true;
	ChildBehavior.bUsesNativeFallback = true;
	ChildBehavior.FrameInterval = 1;
	ChildBehavior.BoundTypeKey = TypeKey;
	ChildBehavior.BoundTypeStableName = TEXT("FDroidState");
	ChildBehavior.SelectedBackend = TEXT("NativeScalar");
	ChildBehavior.CommittedBackend = TEXT("NativeScalar");

	UFlightVerseSubsystem::FCompositeSelectorBranchSpec InvalidBranch;
	InvalidBranch.BehaviorID = ChildBehaviorId;
	InvalidBranch.bHasGuard = true;
	InvalidBranch.Guard.SymbolName = TEXT("@missing_symbol");
	InvalidBranch.Guard.Comparison = EFlightBehaviorGuardComparison::Greater;
	InvalidBranch.Guard.ScalarValue = 1.0f;

	TArray<UFlightVerseSubsystem::FCompositeSelectorBranchSpec> BranchSpecs = { InvalidBranch };
	FString RegistrationErrors;
	const bool bRegistered = VerseSubsystem->RegisterGuardedCompositeSelectorBehavior(SelectorBehaviorId, BranchSpecs, RegistrationErrors, TypeKey);
	TestFalse(TEXT("Guarded selector registration should fail when the guard symbol is not readable"), bRegistered);
	TestTrue(TEXT("Guarded selector registration failure should mention the missing guard symbol"), RegistrationErrors.Contains(TEXT("@missing_symbol")));

	return true;
}

bool FFlightVerseCompositeSequenceRegistrationFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for composite sequence registration failure test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	const uint32 MissingCompositeBehaviorId = 19310;

	TArray<uint32> MissingChildBehaviorIds = { 999999u };
	FString RegistrationErrors;
	const bool bRegistered = VerseSubsystem->RegisterCompositeSequenceBehavior(MissingCompositeBehaviorId, MissingChildBehaviorIds, RegistrationErrors, TypeKey);
	TestFalse(TEXT("Composite sequence registration should fail when a child behavior is missing"), bRegistered);
	TestTrue(TEXT("Composite sequence registration failure should explain the missing child"), RegistrationErrors.Contains(TEXT("missing child behavior")));

	const UFlightVerseSubsystem::FVerseBehavior* MissingCompositeBehavior = VerseSubsystem->Behaviors.Find(MissingCompositeBehaviorId);
	TestNull(TEXT("Failed composite registration should not persist a behavior record"), MissingCompositeBehavior);

	return true;
}

bool FFlightVerseGpuTerminalCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for GPU terminal commit test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("FlightVerseSubsystem not found"));
		return false;
	}

	UFlightGpuScriptBridgeSubsystem* ScriptBridge = World->GetSubsystem<UFlightGpuScriptBridgeSubsystem>();
	if (!ScriptBridge)
	{
		AddError(TEXT("GPU script bridge subsystem not found"));
		return false;
	}

	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(BuildVerseTestGpuSchema());

	FFlightBehaviorCompilePolicyRow PolicyRow;
	PolicyRow.RowName = TEXT("Policy.GpuTerminalCommit");
	PolicyRow.PreferredDomain = EFlightBehaviorCompileDomainPreference::Gpu;
	PolicyRow.bAllowNativeFallback = 0;
	PolicyRow.bAllowGeneratedOnly = 1;
	PolicyRow.RequiredSymbols = { TEXT("@shield") };

	FString CompileDiagnostics;
	const uint32 BehaviorID = 9205;
	UFlightVerseSubsystem::FCompilePolicyContext CompilePolicyContext;
	CompilePolicyContext.ExplicitPolicy = &PolicyRow;
	const bool bCompiled = VerseSubsystem->CompileVex(
		BehaviorID,
		TEXT("@gpu { @shield = @shield + 1.0; }"),
		CompileDiagnostics,
		GetVerseTestGpuTypeKey(),
		CompilePolicyContext);
	TestTrue(TEXT("GPU policy compile should succeed for terminal commit testing"), bCompiled);

	UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
	TestNotNull(TEXT("GPU terminal commit test should persist behavior metadata"), Behavior);
	if (!Behavior)
	{
		return false;
	}

	TestEqual(TEXT("GPU terminal commit test should begin with GpuKernel selected"), Behavior->SelectedBackend, FString(TEXT("GpuKernel")));
	TestEqual(TEXT("GPU terminal commit test should begin with unknown committed backend"), Behavior->CommittedBackend, FString(TEXT("Unknown")));
	TestEqual(TEXT("GPU direct execution resolver should expose the pending GpuKernel lane"), VerseSubsystem->DescribeDirectExecutionBackend(BehaviorID), FString(TEXT("GpuKernel")));

	TArray<FFlightTransformFragment> Transforms;
	TArray<FFlightDroidStateFragment> DroidStates;
	VerseSubsystem->ExecuteBehaviorDirect(BehaviorID, Transforms, DroidStates);

	const int64 SubmissionHandleValue = VerseSubsystem->GetLastGpuSubmissionHandle(BehaviorID);
	TestTrue(TEXT("GPU direct execution should register a submission handle"), SubmissionHandleValue > 0);

	Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
	TestNotNull(TEXT("Behavior metadata should remain available after GPU submission"), Behavior);
	if (!Behavior)
	{
		return false;
	}

	TestTrue(TEXT("GPU direct execution should remain pending before terminal completion"), Behavior->bGpuExecutionPending);
	TestEqual(TEXT("GPU direct execution should keep committed backend unknown before terminal completion"), Behavior->CommittedBackend, FString(TEXT("Unknown")));
	TestTrue(TEXT("GPU direct execution should explain the pending terminal commit"), Behavior->CommitDetail.Contains(TEXT("waiting for a terminal bridge result")));

	FFlightGpuSubmissionHandle SubmissionHandle;
	SubmissionHandle.Value = SubmissionHandleValue;
	TestTrue(
		TEXT("GPU bridge should accept explicit terminal completion for the submitted behavior"),
		ScriptBridge->CompleteExternalSubmission(SubmissionHandle, true, TEXT("Automation GPU completion")));

	Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
	TestNotNull(TEXT("Behavior metadata should still be available after GPU completion"), Behavior);
	if (!Behavior)
	{
		return false;
	}

	TestFalse(TEXT("GPU direct execution should no longer be pending after terminal completion"), Behavior->bGpuExecutionPending);
	TestEqual(TEXT("GPU terminal completion should upgrade committed backend"), Behavior->CommittedBackend, FString(TEXT("GpuKernel")));
	TestTrue(TEXT("GPU terminal completion should explain the proven runtime backend"), Behavior->CommitDetail.Contains(TEXT("terminal completed state")));

	const Flight::Vex::FFlightCompileArtifactReport* Report = VerseSubsystem->GetBehaviorCompileArtifactReport(BehaviorID);
	TestNotNull(TEXT("GPU terminal completion should retain a compile artifact report"), Report);
	if (Report)
	{
		TestEqual(TEXT("Compile artifact report should upgrade committed backend after terminal GPU completion"), Report->CommittedBackend, FString(TEXT("GpuKernel")));
		TestTrue(TEXT("Compile artifact report should preserve the terminal commit detail"), Report->CommitDetail.Contains(TEXT("terminal completed state")));
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	TestNotNull(TEXT("GPU terminal commit test should have access to orchestration"), Orchestration);
	if (Orchestration)
	{
		const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
			[BehaviorID](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
			{
				return Candidate.BehaviorID == static_cast<int32>(BehaviorID);
			});
		TestNotNull(TEXT("Orchestration report should surface the GPU-committed behavior"), BehaviorRecord);
		if (BehaviorRecord)
		{
			TestEqual(TEXT("Orchestration report should upgrade committed backend after terminal GPU completion"), BehaviorRecord->CommittedBackend, FString(TEXT("GpuKernel")));
			TestTrue(TEXT("Orchestration report should retain the terminal commit detail"), BehaviorRecord->CommitDetail.Contains(TEXT("terminal completed state")));
			TestTrue(TEXT("Orchestration report should now treat the GPU behavior as executable"), BehaviorRecord->bExecutable);
		}
	}

	return true;
}

bool FFlightBoundaryCompileArtifactReportTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for boundary compile artifact report test"));
		return false;
	}

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		AddError(TEXT("No FlightVerseSubsystem available for boundary compile artifact report test"));
		return false;
	}

	FString CompileDiagnostics;
	const FString BoundarySource = TEXT("@gpu { @velocity <| vec3(1,0,0); @shield = 1.0; }\n");
	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();
	const void* TypeKey = Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	VerseSubsystem->CompileVex(9101, BoundarySource, CompileDiagnostics, TypeKey);

	const FString ArtifactJson = UFlightScriptingLibrary::GetBehaviorCompileArtifactReportJson(World, 9101);
	TestTrue(TEXT("Boundary compile artifact report JSON should be available"), !ArtifactJson.IsEmpty());

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArtifactJson);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Boundary compile artifact JSON should deserialize"), bParsed);
	if (!bParsed || !RootObject.IsValid())
	{
		return false;
	}

	bool bHasBoundaryOperators = false;
	TestTrue(TEXT("Boundary compile artifact JSON should contain hasBoundaryOperators"), RootObject->TryGetBoolField(TEXT("hasBoundaryOperators"), bHasBoundaryOperators));
	TestTrue(TEXT("Boundary compile artifact report should record boundary operators"), bHasBoundaryOperators);

	double BoundaryOperatorCount = 0.0;
	TestTrue(TEXT("Boundary compile artifact JSON should contain boundaryOperatorCount"), RootObject->TryGetNumberField(TEXT("boundaryOperatorCount"), BoundaryOperatorCount));
	TestEqual(TEXT("Boundary compile artifact report should record one boundary operator"), static_cast<int32>(BoundaryOperatorCount), 1);

	bool bHasAwaitableBoundary = true;
	TestTrue(TEXT("Boundary compile artifact JSON should contain hasAwaitableBoundary"), RootObject->TryGetBoolField(TEXT("hasAwaitableBoundary"), bHasAwaitableBoundary));
	TestFalse(TEXT("Boundary compile artifact report should not mark awaitable boundaries yet"), bHasAwaitableBoundary);

	const TArray<TSharedPtr<FJsonValue>>* ImportedSymbols = nullptr;
	TestTrue(TEXT("Boundary compile artifact JSON should contain importedSymbols"), RootObject->TryGetArrayField(TEXT("importedSymbols"), ImportedSymbols));
	TestTrue(TEXT("Boundary compile artifact report should record at least one imported symbol"), ImportedSymbols && ImportedSymbols->Num() == 1);
	if (ImportedSymbols && ImportedSymbols->Num() == 1)
	{
		FString ImportedSymbol;
		TestTrue(TEXT("Imported boundary symbol should be serialized as a string"), (*ImportedSymbols)[0]->TryGetString(ImportedSymbol));
		TestEqual(TEXT("Boundary compile artifact report should record @velocity as the imported symbol"), ImportedSymbol, FString(TEXT("@velocity")));
	}

	const TArray<TSharedPtr<FJsonValue>>* ExportedSymbols = nullptr;
	TestTrue(TEXT("Boundary compile artifact JSON should contain exportedSymbols"), RootObject->TryGetArrayField(TEXT("exportedSymbols"), ExportedSymbols));
	TestTrue(TEXT("Boundary compile artifact report should record no exported symbols for a PipeIn script"), ExportedSymbols && ExportedSymbols->Num() == 0);

	FString IrCompileErrors;
	TestTrue(TEXT("Boundary compile artifact JSON should contain irCompileErrors"), RootObject->TryGetStringField(TEXT("irCompileErrors"), IrCompileErrors));
	TestTrue(TEXT("Boundary compile artifact report should explain low-level IR rejection for boundary operators"), IrCompileErrors.Contains(TEXT("boundary operators")));

	const TArray<TSharedPtr<FJsonValue>>* BackendReports = nullptr;
	TestTrue(TEXT("Boundary compile artifact JSON should contain backendReports"), RootObject->TryGetArrayField(TEXT("backendReports"), BackendReports));
	bool bFoundBoundaryAwareBackendReason = false;
	if (BackendReports)
	{
		for (const TSharedPtr<FJsonValue>& BackendReportValue : *BackendReports)
		{
			const TSharedPtr<FJsonObject> BackendReportObject = BackendReportValue.IsValid() ? BackendReportValue->AsObject() : nullptr;
			if (!BackendReportObject.IsValid())
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* Reasons = nullptr;
			if (!BackendReportObject->TryGetArrayField(TEXT("reasons"), Reasons) || !Reasons)
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& ReasonValue : *Reasons)
			{
				FString Reason;
				if (ReasonValue.IsValid() && ReasonValue->TryGetString(Reason)
					&& (Reason.Contains(TEXT("Boundary")) || Reason.Contains(TEXT("boundary"))))
				{
					bFoundBoundaryAwareBackendReason = true;
					break;
				}
			}

			if (bFoundBoundaryAwareBackendReason)
			{
				break;
			}
		}
	}
	TestTrue(TEXT("Boundary compile artifact report should surface boundary-aware backend compatibility reasons"), bFoundBoundaryAwareBackendReason);

	const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(9101);
	TestNotNull(TEXT("Boundary compile should persist behavior metadata"), Behavior);
	if (Behavior)
	{
		TestTrue(TEXT("Behavior should record boundary semantics"), Behavior->bHasBoundarySemantics);
		TestFalse(TEXT("Behavior should truthfully report boundary semantics as non-executable for the current runtime path"), Behavior->bBoundarySemanticsExecutable);
		TestEqual(TEXT("Behavior should retain one boundary operator"), Behavior->BoundaryOperatorCount, 1);
		TestTrue(TEXT("Behavior should retain imported boundary symbols"), Behavior->ImportedSymbols.Contains(TEXT("@velocity")));
		TestTrue(TEXT("Behavior should retain a boundary execution detail message"), !Behavior->BoundaryExecutionDetail.IsEmpty());
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	TestNotNull(TEXT("Boundary compile should have access to the orchestration subsystem"), Orchestration);
	if (Orchestration)
	{
		const Flight::Orchestration::FFlightBehaviorRecord* BehaviorRecord = Orchestration->GetReport().Behaviors.FindByPredicate(
			[](const Flight::Orchestration::FFlightBehaviorRecord& Candidate)
			{
				return Candidate.BehaviorID == 9101;
			});
		TestNotNull(TEXT("Orchestration report should surface the boundary-aware behavior"), BehaviorRecord);
		if (BehaviorRecord)
		{
			TestTrue(TEXT("Orchestration behavior report should record boundary semantics"), BehaviorRecord->bHasBoundarySemantics);
			TestFalse(TEXT("Orchestration behavior report should report boundary semantics as non-executable"), BehaviorRecord->bBoundarySemanticsExecutable);
			TestFalse(TEXT("Orchestration behavior report should not advertise the boundary-aware behavior as executable yet"), BehaviorRecord->bExecutable);
			TestEqual(TEXT("Orchestration behavior report should preserve the boundary operator count"), BehaviorRecord->BoundaryOperatorCount, 1);
			TestTrue(TEXT("Orchestration behavior report should preserve imported symbols"), BehaviorRecord->ImportedSymbols.Contains(TEXT("@velocity")));
			TestTrue(TEXT("Orchestration behavior report should include a boundary execution detail"), !BehaviorRecord->BoundaryExecutionDetail.IsEmpty());
		}

		const FString OrchestrationJson = Orchestration->BuildReportJson();
		TestTrue(TEXT("Orchestration JSON should surface boundary semantics"), OrchestrationJson.Contains(TEXT("\"hasBoundarySemantics\"")));
		TestTrue(TEXT("Orchestration JSON should surface boundary execution status"), OrchestrationJson.Contains(TEXT("\"boundarySemanticsExecutable\"")));
		TestTrue(TEXT("Orchestration JSON should surface boundary execution detail"), OrchestrationJson.Contains(TEXT("\"boundaryExecutionDetail\"")));
	}

	return true;
}

bool FFlightGpuScriptBridgeDeferredCompletionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No valid world context available for GPU script bridge test"));
		return false;
	}

	UFlightGpuScriptBridgeSubsystem* ScriptBridge = World->GetSubsystem<UFlightGpuScriptBridgeSubsystem>();
	if (!ScriptBridge)
	{
		AddError(TEXT("No GPU script bridge subsystem available for test"));
		return false;
	}

	FFlightGpuInvocation Invocation;
	Invocation.ProgramId = TEXT("Automation.DeferredGpuBridge");
	Invocation.BehaviorId = 99101;
	Invocation.LatencyClass = EFlightGpuLatencyClass::CpuObserved;
	Invocation.bAwaitRequested = true;
	Invocation.bAllowDeferredExternalSignal = true;

	FString SubmissionDetail;
	const FFlightGpuSubmissionHandle Handle = ScriptBridge->Submit(Invocation, SubmissionDetail);
	TestTrue(TEXT("Deferred GPU bridge submission should produce a valid handle"), Handle.IsValid());
	TestTrue(TEXT("Deferred GPU bridge submission should explain deferred completion"), SubmissionDetail.Contains(TEXT("external")));

	FString PollDetail;
	TestEqual(
		TEXT("Deferred GPU bridge submission should begin in submitted state"),
		ScriptBridge->Poll(Handle, &PollDetail),
		EFlightGpuSubmissionStatus::Submitted);
	TestTrue(TEXT("Deferred GPU bridge poll detail should be populated"), !PollDetail.IsEmpty());

	bool bResolved = false;
	bool bSucceeded = false;
	FString AwaitReason;
	const bool bAwaitRegistered = ScriptBridge->Await(
		Handle,
		[&bResolved, &bSucceeded](EFlightGpuSubmissionStatus Status, const FString&)
		{
			bResolved = true;
			bSucceeded = Status == EFlightGpuSubmissionStatus::Completed;
		},
		AwaitReason);
	TestTrue(TEXT("Deferred GPU bridge should accept await registration"), bAwaitRegistered);
	TestTrue(TEXT("Deferred GPU bridge should explain await registration"), !AwaitReason.IsEmpty());
	TestFalse(TEXT("Deferred GPU bridge should not resolve before completion"), bResolved);

	TestTrue(
		TEXT("Deferred GPU bridge should accept explicit external completion"),
		ScriptBridge->CompleteExternalSubmission(Handle, true, TEXT("Automation completion")));
	TestTrue(TEXT("Deferred GPU bridge await callback should resolve"), bResolved);
	TestTrue(TEXT("Deferred GPU bridge await callback should report success"), bSucceeded);
	TestEqual(
		TEXT("Deferred GPU bridge submission should report completed after explicit resolution"),
		ScriptBridge->Poll(Handle, &PollDetail),
		EFlightGpuSubmissionStatus::Completed);
	TestTrue(TEXT("Deferred GPU bridge completion detail should be preserved"), PollDetail.Contains(TEXT("Automation completion")));

	FFlightGpuAwaitToken AwaitToken;
	TestTrue(TEXT("Deferred GPU bridge should build an await token"), ScriptBridge->TryBuildAwaitToken(Handle, AwaitToken));
	TestEqual(TEXT("Await token should preserve the submission handle"), AwaitToken.Handle.Value, Handle.Value);
	TestEqual(TEXT("Await token should default tracking id to zero for deferred completions"), AwaitToken.ExternalTrackingId, int64(0));

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
