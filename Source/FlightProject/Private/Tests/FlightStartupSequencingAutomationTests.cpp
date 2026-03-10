// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "FlightScriptingLibrary.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "Mass/UFlightVexBehaviorProcessor.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_AUTOMATION_TESTS

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
			if (UWorld* World = Context.World())
			{
				return World;
			}
		}
	}

	return nullptr;
}

bool ParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

void RemoveVisibleBehaviorRecords(UFlightOrchestrationSubsystem& Orchestration)
{
	for (const Flight::Orchestration::FFlightBehaviorRecord& Behavior : Orchestration.GetReport().Behaviors)
	{
		Orchestration.UnregisterBehavior(Behavior.BehaviorID);
	}
}

UFlightVerseSubsystem::FVerseBehavior MakeExecutableBehavior(const uint32 FrameInterval)
{
	UFlightVerseSubsystem::FVerseBehavior Behavior;
	Behavior.FrameInterval = FrameInterval;
	Behavior.bHasExecutableProcedure = true;
	Behavior.bUsesNativeFallback = true;
	return Behavior;
}

} // namespace

/*
 * This suite stays intentionally light for the first startup-sequencing slice.
 * It validates the callable bootstrap/orchestration surfaces and their observable
 * artifacts without requiring a dedicated map fixture or direct StartPlay harness.
 * Future work can deepen this into full startup-profile and post-spawn coverage.
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FFlightStartupSequencingComplexTest,
	"FlightProject.Integration.Startup.Sequencing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightStartupSequencingComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("BootstrapCompletionSignal"));
	OutTestCommands.Add(TEXT("BootstrapCompletionSignal"));

	OutBeautifiedNames.Add(TEXT("OrchestrationRebuildAdvancesPlan"));
	OutTestCommands.Add(TEXT("OrchestrationRebuildAdvancesPlan"));

	OutBeautifiedNames.Add(TEXT("StartupReportJsonSurface"));
	OutTestCommands.Add(TEXT("StartupReportJsonSurface"));
}

bool FFlightStartupSequencingComplexTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for startup sequencing test"));
		return false;
	}

	UFlightWorldBootstrapSubsystem* Bootstrap = World->GetSubsystem<UFlightWorldBootstrapSubsystem>();
	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();

	if (!Bootstrap)
	{
		AddError(TEXT("FlightWorldBootstrapSubsystem not found"));
		return false;
	}

	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	if (Parameters == TEXT("BootstrapCompletionSignal"))
	{
		bool bBootstrapCompleted = false;
		const FDelegateHandle DelegateHandle = Bootstrap->OnBootstrapCompleted().AddLambda([&bBootstrapCompleted]()
		{
			bBootstrapCompleted = true;
		});
		ON_SCOPE_EXIT
		{
			Bootstrap->OnBootstrapCompleted().Remove(DelegateHandle);
		};

		UFlightScriptingLibrary::RunBootstrap(World);
		TestTrue(TEXT("Bootstrap should broadcast completion when run through the scripting surface"), bBootstrapCompleted);
		return true;
	}

	if (Parameters == TEXT("OrchestrationRebuildAdvancesPlan"))
	{
		const uint32 GenerationBefore = Orchestration->GetExecutionPlan().Generation;
		const FDateTime BuiltBefore = Orchestration->GetExecutionPlan().BuiltAtUtc;

		UFlightScriptingLibrary::RunBootstrap(World);
		UFlightScriptingLibrary::RebuildOrchestration(World);

		const Flight::Orchestration::FFlightExecutionPlan& Plan = Orchestration->GetExecutionPlan();
		TestTrue(TEXT("Orchestration rebuild should maintain a non-zero generation once the plan has been built"),
			GenerationBefore == 0 ? Plan.Generation > 0 : Plan.Generation >= GenerationBefore);
		TestTrue(TEXT("Orchestration should report the bootstrap service as available"),
			Orchestration->IsServiceAvailable(TEXT("UFlightWorldBootstrapSubsystem")));
		TestTrue(TEXT("Rebuilt execution plan should record a build timestamp"), Plan.BuiltAtUtc.GetTicks() > 0);
		TestTrue(TEXT("Rebuilt execution plan should refresh or preserve a valid build timestamp"),
			!BuiltBefore.GetTicks() || Plan.BuiltAtUtc >= BuiltBefore);
		return true;
	}

	if (Parameters == TEXT("StartupReportJsonSurface"))
	{
		const FString ReportJson = UFlightScriptingLibrary::GetOrchestrationReportJson(World, true);
		TestFalse(TEXT("Startup report JSON should not be empty after refresh"), ReportJson.IsEmpty());
		if (ReportJson.IsEmpty())
		{
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		TestTrue(TEXT("Startup report JSON should parse"), ParseJsonObject(ReportJson, RootObject));
		if (!RootObject.IsValid())
		{
			return false;
		}

		TestEqual(TEXT("Startup report should reference the active world"), RootObject->GetStringField(TEXT("worldName")), World->GetName());
		TestTrue(TEXT("Startup report should include a built timestamp"), RootObject->HasTypedField<EJson::String>(TEXT("builtAtUtc")));
		TestTrue(TEXT("Startup report should include services"), RootObject->HasTypedField<EJson::Array>(TEXT("services")));
		TestTrue(TEXT("Startup report should include an execution plan"), RootObject->HasTypedField<EJson::Object>(TEXT("executionPlan")));
		return true;
	}

	AddError(FString::Printf(TEXT("Unhandled startup sequencing test command '%s'"), *Parameters));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationBindingPlanTest,
	"FlightProject.Orchestration.Bindings.PlanPrefersExecutableBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightOrchestrationBindingPlanTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for orchestration binding plan test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Orchestration->RebuildVisibility();
	ON_SCOPE_EXIT
	{
		Orchestration->Rebuild();
	};

	RemoveVisibleBehaviorRecords(*Orchestration);
	Orchestration->UnregisterCohort(TEXT("Test.Cohort.BindingSelection"));

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Test.Cohort.BindingSelection");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Tags.Add(TEXT("Default"));
	TestTrue(TEXT("Test cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord NonExecutableBehavior;
	NonExecutableBehavior.BehaviorID = 6202;
	NonExecutableBehavior.FrameInterval = 9;
	NonExecutableBehavior.bExecutable = false;
	NonExecutableBehavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::VerseVm;
	Orchestration->RegisterBehavior(NonExecutableBehavior.BehaviorID, NonExecutableBehavior);

	Flight::Orchestration::FFlightBehaviorRecord ExecutableBehavior;
	ExecutableBehavior.BehaviorID = 6201;
	ExecutableBehavior.FrameInterval = 4;
	ExecutableBehavior.bExecutable = true;
	ExecutableBehavior.bAsync = true;
	ExecutableBehavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::TaskGraph;
	Orchestration->RegisterBehavior(ExecutableBehavior.BehaviorID, ExecutableBehavior);

	Orchestration->RebuildExecutionPlan();

	Flight::Orchestration::FFlightBehaviorBinding Binding;
	TestTrue(TEXT("Execution plan should publish a binding for the registered cohort"),
		Orchestration->TryGetBindingForCohort(Cohort.Name, Binding));
	TestEqual(TEXT("Binding should resolve the executable behavior"), Binding.BehaviorID, ExecutableBehavior.BehaviorID);
	TestEqual(TEXT("Binding should preserve the resolved execution domain"),
		Binding.ExecutionDomain, ExecutableBehavior.ResolvedDomain);
	TestEqual(TEXT("Binding should preserve the frame interval"),
		static_cast<int32>(Binding.FrameInterval), static_cast<int32>(ExecutableBehavior.FrameInterval));
	TestTrue(TEXT("Binding should preserve async execution metadata"), Binding.bAsync);

	const Flight::Orchestration::FFlightExecutionPlan& Plan = Orchestration->GetExecutionPlan();
	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Plan.Steps.FindByPredicate([&Cohort](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
	{
		return Candidate.CohortName == Cohort.Name;
	});

	TestNotNull(TEXT("Execution plan should contain a step for the registered cohort"), Step);
	if (Step)
	{
		TestEqual(TEXT("Plan step should use the selected executable behavior"), Step->BehaviorID, ExecutableBehavior.BehaviorID);
		TestEqual(TEXT("Plan step should use the selected execution domain"),
			Step->ExecutionDomain, ExecutableBehavior.ResolvedDomain);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationBindingResolverTest,
	"FlightProject.Orchestration.Bindings.ProcessorResolverPrefersOrchestration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightOrchestrationBindingResolverTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for orchestration binding resolver test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Orchestration->RebuildVisibility();
	ON_SCOPE_EXIT
	{
		Orchestration->Rebuild();
	};

	Orchestration->UnregisterCohort(TEXT("Swarm.Default"));
	Orchestration->ClearBindingsForCohort(TEXT("Swarm.Default"));

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Swarm.Default");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Tags.Add(TEXT("Default"));
	TestTrue(TEXT("Default swarm cohort should register for resolver test"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord BoundRecord;
	BoundRecord.BehaviorID = 7102;
	BoundRecord.bExecutable = true;
	BoundRecord.FrameInterval = 2;
	BoundRecord.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(BoundRecord.BehaviorID, BoundRecord);

	Flight::Orchestration::FFlightBehaviorBinding BoundBinding;
	BoundBinding.CohortName = Cohort.Name;
	BoundBinding.BehaviorID = BoundRecord.BehaviorID;
	BoundBinding.ExecutionDomain = BoundRecord.ResolvedDomain;
	BoundBinding.FrameInterval = BoundRecord.FrameInterval;
	TestTrue(TEXT("Manual orchestration binding should register"), Orchestration->BindBehaviorToCohort(BoundBinding));

	UFlightVerseSubsystem* TestVerseSubsystem = NewObject<UFlightVerseSubsystem>();
	TestVerseSubsystem->Behaviors.Add(7101, MakeExecutableBehavior(1));
	TestVerseSubsystem->Behaviors.Add(7102, MakeExecutableBehavior(2));

	uint32 ResolvedBehaviorID = 0;
	Flight::Orchestration::FFlightBehaviorBinding ResolvedBinding;
	TestTrue(TEXT("Processor resolver should return a behavior selection"),
		UFlightVexBehaviorProcessor::ResolveBehaviorSelection(World, *TestVerseSubsystem, ResolvedBehaviorID, &ResolvedBinding));
	TestEqual(TEXT("Processor resolver should prefer orchestration-issued binding over fallback ordering"),
		ResolvedBehaviorID, BoundRecord.BehaviorID);
	TestEqual(TEXT("Resolved binding should come from the default swarm cohort"), ResolvedBinding.CohortName, Cohort.Name);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationAnchorPreferencePlanTest,
	"FlightProject.Orchestration.Bindings.AnchorPreferenceLegality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightOrchestrationAnchorPreferencePlanTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for anchor preference legality test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Orchestration->RebuildVisibility();
	ON_SCOPE_EXIT
	{
		Orchestration->Rebuild();
	};

	RemoveVisibleBehaviorRecords(*Orchestration);
	Orchestration->UnregisterCohort(TEXT("SwarmAnchor.AutomationPreferred"));

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("SwarmAnchor.AutomationPreferred");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Tags.Add(TEXT("AnchorScoped"));
	Cohort.PreferredBehaviorId = 7202;
	TestTrue(TEXT("Anchor-scoped cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord LowerBehavior;
	LowerBehavior.BehaviorID = 7201;
	LowerBehavior.bExecutable = true;
	LowerBehavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(LowerBehavior.BehaviorID, LowerBehavior);

	Flight::Orchestration::FFlightBehaviorRecord PreferredBehavior;
	PreferredBehavior.BehaviorID = 7202;
	PreferredBehavior.bExecutable = true;
	PreferredBehavior.FrameInterval = 5;
	PreferredBehavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::TaskGraph;
	Orchestration->RegisterBehavior(PreferredBehavior.BehaviorID, PreferredBehavior);

	Orchestration->RebuildExecutionPlan();

	Flight::Orchestration::FFlightBehaviorBinding Binding;
	TestTrue(TEXT("Execution plan should publish a binding for the preferred anchor cohort"),
		Orchestration->TryGetBindingForCohort(Cohort.Name, Binding));
	TestEqual(TEXT("Preferred behavior should override lower executable IDs"), Binding.BehaviorID, PreferredBehavior.BehaviorID);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationAnchorContractPlanTest,
	"FlightProject.Orchestration.Bindings.AnchorContractLegality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightOrchestrationAnchorContractPlanTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for anchor contract legality test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	Orchestration->RebuildVisibility();
	ON_SCOPE_EXIT
	{
		Orchestration->Rebuild();
	};

	RemoveVisibleBehaviorRecords(*Orchestration);
	Orchestration->UnregisterCohort(TEXT("SwarmAnchor.AutomationContracts"));

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("SwarmAnchor.AutomationContracts");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Tags.Add(TEXT("AnchorScoped"));
	Cohort.RequiredBehaviorContracts.Add(TEXT("Formation"));
	TestTrue(TEXT("Anchor-scoped contract cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord GenericBehavior;
	GenericBehavior.BehaviorID = 7301;
	GenericBehavior.bExecutable = true;
	GenericBehavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(GenericBehavior.BehaviorID, GenericBehavior);

	Flight::Orchestration::FFlightBehaviorRecord ContractBehavior;
	ContractBehavior.BehaviorID = 7302;
	ContractBehavior.bExecutable = true;
	ContractBehavior.RequiredContracts.Add(TEXT("Formation"));
	ContractBehavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::VerseVm;
	Orchestration->RegisterBehavior(ContractBehavior.BehaviorID, ContractBehavior);

	Orchestration->RebuildExecutionPlan();

	Flight::Orchestration::FFlightBehaviorBinding Binding;
	TestTrue(TEXT("Execution plan should publish a binding for the contract-constrained anchor cohort"),
		Orchestration->TryGetBindingForCohort(Cohort.Name, Binding));
	TestEqual(TEXT("Contract-constrained cohort should choose a behavior that advertises the required contract"),
		Binding.BehaviorID, ContractBehavior.BehaviorID);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationBindingFallbackTest,
	"FlightProject.Orchestration.Bindings.ProcessorResolverFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightOrchestrationBindingFallbackTest::RunTest(const FString& Parameters)
{
	UFlightVerseSubsystem* TestVerseSubsystem = NewObject<UFlightVerseSubsystem>();
	TestVerseSubsystem->Behaviors.Add(8102, MakeExecutableBehavior(2));
	TestVerseSubsystem->Behaviors.Add(8101, MakeExecutableBehavior(1));

	uint32 ResolvedBehaviorID = 0;
	TestTrue(TEXT("Processor resolver should fall back when no orchestration world is available"),
		UFlightVexBehaviorProcessor::ResolveBehaviorSelection(nullptr, *TestVerseSubsystem, ResolvedBehaviorID));
	TestEqual(TEXT("Fallback should choose the lowest executable behavior ID"), ResolvedBehaviorID, static_cast<uint32>(8101));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
