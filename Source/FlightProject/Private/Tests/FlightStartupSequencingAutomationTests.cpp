// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "FlightNavGraphDataHubSubsystem.h"
#include "FlightSpawnSwarmAnchor.h"
#include "FlightWaypointPath.h"
#include "FlightScriptingLibrary.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "Core/FlightReflection.h"
#include "Core/FlightSchemaProviderAdapter.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/FlightMassFragments.h"
#include "Mass/UFlightVexBehaviorProcessor.h"
#include "Mass/FlightMassLoweringAdapter.h"
#include "Mass/FlightWaypointPathRegistry.h"
#include "Navigation/FlightNavigationCommitProduct.h"
#include "Navigation/FlightNavigationContracts.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Swarm/FlightSwarmAdapterContracts.h"
#include "Vex/FlightVexSymbolRegistry.h"
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

UWorldSubsystem* FindOptionalWorldSubsystem(UWorld* World, const TCHAR* ClassPath)
{
	if (!World)
	{
		return nullptr;
	}

	if (UClass* SubsystemClass = FindObject<UClass>(nullptr, ClassPath))
	{
		return World->GetSubsystemBase(SubsystemClass);
	}

	return nullptr;
}

bool InvokeSubsystemNoArg(UWorld* World, const TCHAR* ClassPath, const TCHAR* FunctionName)
{
	UWorldSubsystem* Subsystem = FindOptionalWorldSubsystem(World, ClassPath);
	if (!Subsystem)
	{
		return false;
	}

	UFunction* Function = Subsystem->GetClass()->FindFunctionByName(FunctionName);
	if (!Function)
	{
		return false;
	}

	Subsystem->ProcessEvent(Function, nullptr);
	return true;
}

bool SetObjectNameProperty(UObject* Object, const TCHAR* PropertyName, const FName Value)
{
	if (!Object)
	{
		return false;
	}

	if (FNameProperty* Property = FindFProperty<FNameProperty>(Object->GetClass(), PropertyName))
	{
		Property->SetPropertyValue_InContainer(Object, Value);
		return true;
	}

	return false;
}

bool SetObjectReferenceProperty(UObject* Object, const TCHAR* PropertyName, UObject* Value)
{
	if (!Object)
	{
		return false;
	}

	if (FObjectProperty* Property = FindFProperty<FObjectProperty>(Object->GetClass(), PropertyName))
	{
		Property->SetObjectPropertyValue_InContainer(Object, Value);
		return true;
	}

	return false;
}

TArray<FGuid> GatherPathIdsForCohort(UWorld* World, const FName CohortName)
{
	TArray<FGuid> PathIds;
	if (!World)
	{
		return PathIds;
	}

	UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return PathIds;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	FMassEntityQuery Query(EntityManager.AsShared());
	Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FFlightPathFollowFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddConstSharedRequirement<FFlightBehaviorCohortFragment>(EMassFragmentPresence::Optional);

	FMassExecutionContext Context(EntityManager, 0.0f);
	Query.ForEachEntityChunk(Context, [&PathIds, CohortName](FMassExecutionContext& ChunkContext)
	{
		const FFlightBehaviorCohortFragment* CohortFragment = ChunkContext.GetConstSharedFragmentPtr<FFlightBehaviorCohortFragment>();
		const FName ChunkCohortName = CohortFragment ? CohortFragment->CohortName : NAME_None;
		if (ChunkCohortName != CohortName)
		{
			return;
		}

		const TConstArrayView<FFlightPathFollowFragment> PathFragments = ChunkContext.GetFragmentView<FFlightPathFollowFragment>();
		for (const FFlightPathFollowFragment& PathFragment : PathFragments)
		{
			PathIds.Add(PathFragment.PathId);
		}
	});

	return PathIds;
}

bool FindNavigationCommitSharedFragmentForCohort(
	UWorld* World,
	const FName CohortName,
	FFlightNavigationCommitSharedFragment& OutCommitFragment)
{
	if (!World)
	{
		return false;
	}

	UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	FMassEntityQuery Query(EntityManager.AsShared());
	Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);
	Query.AddConstSharedRequirement<FFlightBehaviorCohortFragment>(EMassFragmentPresence::Optional);
	Query.AddConstSharedRequirement<FFlightNavigationCommitSharedFragment>(EMassFragmentPresence::Optional);

	bool bFound = false;
	FMassExecutionContext Context(EntityManager, 0.0f);
	Query.ForEachEntityChunk(Context, [&OutCommitFragment, CohortName, &bFound](FMassExecutionContext& ChunkContext)
	{
		if (bFound)
		{
			return;
		}

		const FFlightBehaviorCohortFragment* CohortFragment = ChunkContext.GetConstSharedFragmentPtr<FFlightBehaviorCohortFragment>();
		const FName ChunkCohortName = CohortFragment ? CohortFragment->CohortName : NAME_None;
		if (ChunkCohortName != CohortName)
		{
			return;
		}

		const FFlightNavigationCommitSharedFragment* CommitFragment =
			ChunkContext.GetConstSharedFragmentPtr<FFlightNavigationCommitSharedFragment>();
		if (!CommitFragment)
		{
			return;
		}

		OutCommitFragment = *CommitFragment;
		bFound = true;
	});

	return bFound;
}

void DestroySwarmEntitiesDirect(UWorld* World)
{
	if (!World)
	{
		return;
	}

	UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	FMassEntityQuery Query(EntityManager.AsShared());
	Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);

	TArray<FMassEntityHandle> EntitiesToDestroy = Query.GetMatchingEntityHandles();
	for (const FMassEntityHandle& Entity : EntitiesToDestroy)
	{
		EntityManager.DestroyEntity(Entity);
	}
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
	FFlightSpawnAnchorBatchAdapterTest,
	"FlightProject.Orchestration.Adapters.SpawnAnchorBatchLowering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSpawnAnchorBatchAdapterTest::RunTest(const FString& Parameters)
{
	AFlightSpawnSwarmAnchor* Anchor = GetMutableDefault<AFlightSpawnSwarmAnchor>();
	if (!Anchor)
	{
		AddError(TEXT("Failed to resolve spawn-anchor class default object"));
		return false;
	}

	IFlightSchemaProviderAdapter* SchemaAdapter = Cast<IFlightSchemaProviderAdapter>(Anchor);
	IFlightMassLoweringAdapter* MassAdapter = Cast<IFlightMassLoweringAdapter>(Anchor);
	TestNotNull(TEXT("Spawn anchor should expose a schema-provider adapter interface"), SchemaAdapter);
	TestNotNull(TEXT("Spawn anchor should expose a Mass-lowering adapter interface"), MassAdapter);
	if (!SchemaAdapter || !MassAdapter)
	{
		return false;
	}

	TArray<Flight::Adapters::FFlightSchemaProviderDescriptor> Descriptors;
	SchemaAdapter->GetSchemaProviderDescriptors(Descriptors);
	TestEqual(TEXT("Spawn anchor should expose exactly one schema provider descriptor in the first slice"), Descriptors.Num(), 1);
	if (Descriptors.IsEmpty())
	{
		return false;
	}

	const Flight::Adapters::FFlightSchemaProviderDescriptor& Descriptor = Descriptors[0];
	TestTrue(TEXT("Schema provider descriptor should be valid"), Descriptor.IsValid());
	TestEqual(
		TEXT("Spawn anchor batch schema should classify as auto VEX-capable"),
		Descriptor.ExpectedCapability,
		Flight::Reflection::EVexCapability::VexCapableAuto);
	TestTrue(TEXT("Spawn anchor batch schema should advertise batch resolution"), Descriptor.bSupportsBatchResolution);
	TestTrue(TEXT("Spawn anchor batch schema should advertise the swarm batch contract key"),
		Descriptor.ContractKeys.Contains(Flight::Swarm::AdapterContracts::SpawnBatchKey));

	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo =
		Flight::Reflection::FTypeRegistry::Get().FindByRuntimeKey(Descriptor.RuntimeTypeKey);
	TestNotNull(TEXT("Spawn anchor batch schema type should be discoverable through the reflection registry"), TypeInfo);
	if (TypeInfo)
	{
		TestEqual(TEXT("Reflection registry should classify the batch contract as auto-capable"),
			TypeInfo->VexCapability, Flight::Reflection::EVexCapability::VexCapableAuto);
		const Flight::Vex::FVexSchemaResolutionResult Resolution =
			Flight::Vex::FVexSymbolRegistry::Get().ResolveSchemaForReflectedType(*TypeInfo);
		TestTrue(TEXT("Spawn anchor batch schema should resolve through the shared schema registry path"),
			Resolution.Schema != nullptr);
	}

	Flight::Mass::FFlightMassBatchLoweringPlan Plan;
	TestTrue(TEXT("Spawn anchor should build a Mass batch lowering plan"), MassAdapter->BuildMassBatchLoweringPlan(Plan));
	TestEqual(TEXT("Default spawn anchor batch count should lower into the plan"), Plan.BatchCount, 6);
	TestEqual(TEXT("Default spawn anchor phase spread should lower into the plan"), Plan.PhaseSpreadDeg, 360.0f);
	TestTrue(TEXT("Batch lowering plan should build an anchor-scoped cohort name"),
		Plan.CohortName.ToString().StartsWith(TEXT("SwarmAnchor.")));
	TestEqual(TEXT("Batch lowering plan should carry the same schema descriptor runtime key"),
		Plan.SchemaDescriptor.RuntimeTypeKey, Descriptor.RuntimeTypeKey);
	TestTrue(TEXT("Batch lowering plan should carry an explicit behavior-cohort shared fragment payload"),
		Plan.SharedFragments.bHasBehaviorCohort);
	TestEqual(TEXT("Shared fragment payload should use the same cohort name as the batch plan"),
		Plan.SharedFragments.BehaviorCohort.CohortName, Plan.CohortName);
	TestTrue(TEXT("Batch lowering plan should carry orchestration cohort metadata"),
		Plan.Orchestration.bHasCohortRecord);
	TestEqual(TEXT("Orchestration cohort metadata should use the same cohort name as the batch plan"),
		Plan.Orchestration.Cohort.Name, Plan.CohortName);
	TestTrue(TEXT("Orchestration cohort metadata should advertise navigation intent"),
		Plan.Orchestration.Cohort.RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::IntentKey));
	TestTrue(TEXT("Orchestration cohort metadata should advertise navigation candidate requirements"),
		Plan.Orchestration.Cohort.RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::CandidateKey));
	TestTrue(TEXT("Orchestration cohort metadata should advertise navigation commit requirements"),
		Plan.Orchestration.Cohort.RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::CommitKey));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightSpawnAnchorCohortReconciliationTest,
	"FlightProject.Orchestration.Adapters.SpawnAnchorCohortReconciliation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSpawnAnchorCohortReconciliationTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for spawn-anchor reconciliation test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	AFlightSpawnSwarmAnchor* Anchor = World->SpawnActor<AFlightSpawnSwarmAnchor>(AFlightSpawnSwarmAnchor::StaticClass(), FTransform::Identity);
	if (!Anchor)
	{
		AddError(TEXT("Failed to spawn automation swarm anchor"));
		return false;
	}

	ON_SCOPE_EXIT
	{
		TArray<Flight::Mass::FFlightMassBatchLoweringPlan> EmptyPlans;
		Orchestration->ReconcileBatchLoweringPlans(EmptyPlans);

		if (IsValid(Anchor))
		{
			Anchor->Destroy();
		}

		Orchestration->Rebuild();
	};

	IFlightMassLoweringAdapter* MassAdapter = Cast<IFlightMassLoweringAdapter>(Anchor);
	TestNotNull(TEXT("Spawn anchor should expose the Mass-lowering adapter interface"), MassAdapter);
	if (!MassAdapter)
	{
		return false;
	}

	Flight::Mass::FFlightMassBatchLoweringPlan Plan;
	TestTrue(TEXT("Spawn anchor should build a batch plan for cohort reconciliation"), MassAdapter->BuildMassBatchLoweringPlan(Plan));
	if (!Plan.Orchestration.bHasCohortRecord)
	{
		AddError(TEXT("Spawn anchor batch plan did not include orchestration cohort metadata"));
		return false;
	}

	Orchestration->RebuildVisibility();
	TArray<Flight::Mass::FFlightMassBatchLoweringPlan> PlansToReconcile;
	PlansToReconcile.Add(Plan);
	Orchestration->ReconcileBatchLoweringPlans(PlansToReconcile);

	const Flight::Orchestration::FFlightCohortRecord* CanonicalCohort = Orchestration->FindCohort(Plan.CohortName);
	TestNotNull(TEXT("Canonical cohort should be discoverable after reconciliation"), CanonicalCohort);
	if (!CanonicalCohort)
	{
		return false;
	}

	TestTrue(TEXT("Canonical cohort should retain navigation candidate requirements"),
		CanonicalCohort->RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::CandidateKey));
	TestFalse(TEXT("Matching reconciliation should not emit a cohort drift diagnostic"),
		Orchestration->GetReport().Diagnostics.ContainsByPredicate(
			[&Plan](const Flight::Orchestration::FFlightOrchestrationDiagnostic& Diagnostic)
			{
				return Diagnostic.Category == TEXT("CohortReconciliation")
					&& Diagnostic.SourceName == Plan.CohortName;
			}));

	Flight::Mass::FFlightMassBatchLoweringPlan DriftedPlan = Plan;
	DriftedPlan.Orchestration.Cohort.PreferredBehaviorId = 77;
	PlansToReconcile.Reset();
	PlansToReconcile.Add(DriftedPlan);
	Orchestration->ReconcileBatchLoweringPlans(PlansToReconcile);

	CanonicalCohort = Orchestration->FindCohort(Plan.CohortName);
	TestNotNull(TEXT("Canonical cohort should remain registered after drifted reconciliation"), CanonicalCohort);
	if (!CanonicalCohort)
	{
		return false;
	}

	TestEqual(TEXT("Canonical cohort should remain authoritative when a drifted proposal is reconciled"),
		CanonicalCohort->PreferredBehaviorId,
		Plan.Orchestration.Cohort.PreferredBehaviorId);
	TestTrue(TEXT("Drifted reconciliation should emit a cohort drift diagnostic"),
		Orchestration->GetReport().Diagnostics.ContainsByPredicate(
			[&Plan](const Flight::Orchestration::FFlightOrchestrationDiagnostic& Diagnostic)
			{
				return Diagnostic.Category == TEXT("CohortReconciliation")
					&& Diagnostic.SourceName == Plan.CohortName;
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightSpawnUsesExecutionPlanNavigationCommitTest,
	"FlightProject.Orchestration.Adapters.SpawnUsesExecutionPlanNavigationCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSpawnUsesExecutionPlanNavigationCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for spawn/execution-plan commit test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!Orchestration)
	{
		AddError(TEXT("FlightOrchestrationSubsystem not found"));
		return false;
	}

	UWorldSubsystem* SwarmSpawnerSubsystem =
		FindOptionalWorldSubsystem(World, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"));
	if (!SwarmSpawnerSubsystem)
	{
		AddError(TEXT("FlightSwarmSpawnerSubsystem not available"));
		return false;
	}

	UObject* SwarmEntityConfig = LoadObject<UObject>(nullptr, TEXT("/SwarmEncounter/DA_SwarmDroneConfig.DA_SwarmDroneConfig"));
	TestNotNull(TEXT("Swarm entity config asset should load for spawn commit test"), SwarmEntityConfig);
	TestTrue(TEXT("Spawn commit test should seed the spawner config explicitly"),
		SetObjectReferenceProperty(SwarmSpawnerSubsystem, TEXT("SwarmEntityConfig"), SwarmEntityConfig));

	DestroySwarmEntitiesDirect(World);

	FActorSpawnParameters FallbackPathParams;
	FallbackPathParams.Name = TEXT("Automation.SpawnCommit.FallbackPath");
	AFlightWaypointPath* FallbackPath = World->SpawnActor<AFlightWaypointPath>(
		FVector(0.0f, 0.0f, 1200.0f),
		FRotator::ZeroRotator,
		FallbackPathParams);

	FActorSpawnParameters SelectedPathParams;
	SelectedPathParams.Name = TEXT("Automation.SpawnCommit.SelectedPath");
	AFlightWaypointPath* SelectedPath = World->SpawnActor<AFlightWaypointPath>(
		FVector(8000.0f, 0.0f, 1200.0f),
		FRotator::ZeroRotator,
		SelectedPathParams);

	FActorSpawnParameters AnchorParams;
	AnchorParams.Name = TEXT("AutomationSpawnCommitNetwork");
	AFlightSpawnSwarmAnchor* Anchor = World->SpawnActor<AFlightSpawnSwarmAnchor>(
		FVector(7800.0f, 0.0f, 1200.0f),
		FRotator::ZeroRotator,
		AnchorParams);

	if (!FallbackPath || !SelectedPath || !Anchor)
	{
		AddError(TEXT("Failed to spawn automation actors for execution-plan commit test"));
		return false;
	}

	FallbackPath->SetNavigationRoutingMetadata(TEXT("AutomationFallbackNetwork"), TEXT("FallbackLane"));
	SelectedPath->SetNavigationRoutingMetadata(TEXT("AutomationSpawnCommitNetwork"), TEXT("SelectedLane"));
	TestTrue(TEXT("Automation anchor should accept explicit navigation-network metadata"),
		SetObjectNameProperty(Anchor, TEXT("NavNetworkId"), TEXT("AutomationSpawnCommitNetwork")));
	TestTrue(TEXT("Automation anchor should accept explicit navigation-subnetwork metadata"),
		SetObjectNameProperty(Anchor, TEXT("NavSubNetworkId"), TEXT("SelectedLane")));
	FallbackPath->EnsureDefaultLoop();
	SelectedPath->EnsureDefaultLoop();
	FallbackPath->EnsureRegisteredPath();
	const FGuid SelectedPathId = SelectedPath->EnsureRegisteredPath();

	ON_SCOPE_EXIT
	{
		DestroySwarmEntitiesDirect(World);
		if (Anchor)
		{
			Anchor->Destroy();
		}
		if (SelectedPath)
		{
			SelectedPath->Destroy();
		}
		if (FallbackPath)
		{
			FallbackPath->Destroy();
		}
		Orchestration->Rebuild();
	};

	Orchestration->RebuildVisibility();
	RemoveVisibleBehaviorRecords(*Orchestration);

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9301;
	Behavior.bExecutable = true;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);
	Orchestration->RebuildExecutionPlan();

	const FName CohortName = FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *Anchor->GetAnchorId().ToString()));
	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Orchestration->GetExecutionPlan().Steps.FindByPredicate(
		[CohortName](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == CohortName;
		});
	TestNotNull(TEXT("Anchor-scoped cohort should produce an execution-plan step before spawning"), Step);
	if (!Step)
	{
		return false;
	}

	TestEqual(TEXT("Execution plan should select the network-matching waypoint path for this anchor cohort"),
		Step->NavigationCandidateId, SelectedPathId);

	Flight::Navigation::FFlightNavigationCommitResolverContext CommitContext;
	CommitContext.BuildFromWorld(*World);
	const Flight::Navigation::FFlightNavigationCommitProduct CommitProduct =
		Flight::Navigation::ResolveNavigationCommitProductForStep(
			*Step,
			Orchestration->GetReport(),
			CommitContext,
			World->GetSubsystem<UFlightWaypointPathRegistry>(),
			FallbackPath);
	TestTrue(TEXT("Execution-plan waypoint-path selection should resolve a valid commit product"), CommitProduct.IsValid());
	TestEqual(TEXT("Waypoint-path selection should resolve a waypoint commit product"),
		CommitProduct.Kind,
		Flight::Navigation::EFlightNavigationCommitProductKind::WaypointPath);
	TestFalse(TEXT("Waypoint-path commit product should not be synthetic"), CommitProduct.bSynthetic);
	TestEqual(TEXT("Waypoint-path commit product should preserve the selected candidate id"),
		CommitProduct.SourceCandidateId,
		SelectedPathId);

	TestTrue(TEXT("Spawner should expose SpawnInitialSwarm for automation invocation"),
		InvokeSubsystemNoArg(World, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"), TEXT("SpawnInitialSwarm")));

	const TArray<FGuid> CohortPathIds = GatherPathIdsForCohort(World, CohortName);
	TestFalse(TEXT("Spawn should produce Mass entities for the anchor-scoped cohort"), CohortPathIds.IsEmpty());
	for (const FGuid& PathId : CohortPathIds)
	{
		TestEqual(TEXT("Anchor-scoped spawned entities should commit the execution-plan-selected path"),
			PathId,
			SelectedPathId);
	}

	FFlightNavigationCommitSharedFragment CommitFragment;
	TestTrue(TEXT("Waypoint-path commit should materialize a shared navigation commit fragment"),
		FindNavigationCommitSharedFragmentForCohort(World, CohortName, CommitFragment));
	TestEqual(TEXT("Waypoint-path shared commit fragment should preserve the selected candidate id"),
		CommitFragment.SourceCandidateId,
		SelectedPathId);
	TestEqual(TEXT("Waypoint-path shared commit fragment should preserve the runtime path id"),
		CommitFragment.RuntimePathId,
		SelectedPathId);
	TestEqual(TEXT("Waypoint-path shared commit fragment should classify as waypoint-path commit"),
		static_cast<int32>(CommitFragment.CommitKind),
		static_cast<int32>(Flight::Navigation::EFlightNavigationCommitProductKind::WaypointPath));
	TestFalse(TEXT("Waypoint-path shared commit fragment should not report synthetic lowering"),
		CommitFragment.bSynthetic);

	return !CohortPathIds.IsEmpty();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightSpawnUsesSelectedCandidateCommitTest,
	"FlightProject.Orchestration.Adapters.SpawnUsesSelectedCandidateCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSpawnUsesSelectedCandidateCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for selected-candidate commit test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	UFlightWaypointPathRegistry* PathRegistry = World->GetSubsystem<UFlightWaypointPathRegistry>();
	if (!Orchestration || !NavGraphHub || !PathRegistry)
	{
		AddError(TEXT("Required subsystems not found for selected-candidate commit test"));
		return false;
	}

	UWorldSubsystem* SwarmSpawnerSubsystem =
		FindOptionalWorldSubsystem(World, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"));
	if (!SwarmSpawnerSubsystem)
	{
		AddError(TEXT("FlightSwarmSpawnerSubsystem not available"));
		return false;
	}

	UObject* SwarmEntityConfig = LoadObject<UObject>(nullptr, TEXT("/SwarmEncounter/DA_SwarmDroneConfig.DA_SwarmDroneConfig"));
	TestNotNull(TEXT("Swarm entity config asset should load for selected-candidate commit test"), SwarmEntityConfig);
	TestTrue(TEXT("Selected-candidate commit test should seed the spawner config explicitly"),
		SetObjectReferenceProperty(SwarmSpawnerSubsystem, TEXT("SwarmEntityConfig"), SwarmEntityConfig));

	DestroySwarmEntitiesDirect(World);
	NavGraphHub->ResetGraph();

	FActorSpawnParameters FallbackPathParams;
	FallbackPathParams.Name = TEXT("Automation.CandidateCommit.FallbackPath");
	AFlightWaypointPath* FallbackPath = World->SpawnActor<AFlightWaypointPath>(
		FVector(0.0f, 0.0f, 1200.0f),
		FRotator::ZeroRotator,
		FallbackPathParams);

	FActorSpawnParameters AnchorParams;
	AnchorParams.Name = TEXT("AutomationCandidateCommitAnchor");
	AFlightSpawnSwarmAnchor* Anchor = World->SpawnActor<AFlightSpawnSwarmAnchor>(
		FVector(5000.0f, 0.0f, 1200.0f),
		FRotator::ZeroRotator,
		AnchorParams);

	if (!FallbackPath || !Anchor)
	{
		AddError(TEXT("Failed to spawn automation actors for selected-candidate commit test"));
		return false;
	}

	FallbackPath->SetNavigationRoutingMetadata(TEXT("FallbackOnlyNetwork"), TEXT("FallbackLane"));
	FallbackPath->EnsureDefaultLoop();
	const FGuid FallbackPathId = FallbackPath->EnsureRegisteredPath();

	TestTrue(TEXT("Automation anchor should accept explicit navigation-network metadata for selected-candidate commit"),
		SetObjectNameProperty(Anchor, TEXT("NavNetworkId"), TEXT("SyntheticCommitNetwork")));
	TestTrue(TEXT("Automation anchor should accept explicit navigation-subnetwork metadata for selected-candidate commit"),
		SetObjectNameProperty(Anchor, TEXT("NavSubNetworkId"), TEXT("SyntheticLane")));

	FFlightNavGraphNodeDescriptor Node;
	Node.DisplayName = TEXT("Automation.CandidateCommit.Node");
	Node.NetworkId = TEXT("SyntheticCommitNetwork");
	Node.SubNetworkId = TEXT("SyntheticLane");
	Node.Location = FVector(5200.0f, 0.0f, 1400.0f);
	const FGuid NodeId = NavGraphHub->RegisterNode(Node);

	ON_SCOPE_EXIT
	{
		DestroySwarmEntitiesDirect(World);
		NavGraphHub->ResetGraph();
		if (Anchor)
		{
			Anchor->Destroy();
		}
		if (FallbackPath)
		{
			FallbackPath->Destroy();
		}
		Orchestration->Rebuild();
	};

	Orchestration->RebuildVisibility();
	RemoveVisibleBehaviorRecords(*Orchestration);

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9302;
	Behavior.bExecutable = true;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);
	Orchestration->RebuildExecutionPlan();

	const FName CohortName = FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *Anchor->GetAnchorId().ToString()));
	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Orchestration->GetExecutionPlan().Steps.FindByPredicate(
		[CohortName](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == CohortName;
		});
	TestNotNull(TEXT("Anchor-scoped cohort should produce an execution-plan step for selected-candidate commit"), Step);
	if (!Step)
	{
		return false;
	}

	TestEqual(TEXT("Execution plan should select the nav-graph node candidate for synthetic commit"),
		Step->NavigationCandidateId, NodeId);
	Flight::Navigation::FFlightNavigationCommitResolverContext CommitContext;
	CommitContext.BuildFromWorld(*World);
	const Flight::Navigation::FFlightNavigationCommitProduct CommitProduct =
		Flight::Navigation::ResolveNavigationCommitProductForStep(
			*Step,
			Orchestration->GetReport(),
			CommitContext,
			PathRegistry,
			FallbackPath);
	TestTrue(TEXT("Selected nav-graph node should resolve a valid commit product"), CommitProduct.IsValid());
	TestEqual(TEXT("Selected nav-graph node should resolve a synthetic node-orbit commit product"),
		CommitProduct.Kind,
		Flight::Navigation::EFlightNavigationCommitProductKind::SyntheticNodeOrbit);
	TestTrue(TEXT("Synthetic node-orbit commit product should report synthetic lowering"), CommitProduct.bSynthetic);
	TestEqual(TEXT("Synthetic node-orbit commit product should preserve the selected candidate id"),
		CommitProduct.SourceCandidateId,
		NodeId);
	TestTrue(TEXT("Spawner should expose SpawnInitialSwarm for selected-candidate commit automation"),
		InvokeSubsystemNoArg(World, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"), TEXT("SpawnInitialSwarm")));

	const TArray<FGuid> CohortPathIds = GatherPathIdsForCohort(World, CohortName);
	TestFalse(TEXT("Synthetic selected-candidate commit should produce Mass entities for the anchor-scoped cohort"), CohortPathIds.IsEmpty());
	for (const FGuid& PathId : CohortPathIds)
	{
		TestEqual(TEXT("Anchor-scoped spawned entities should commit the selected nav-graph candidate as a synthetic path"),
			PathId,
			NodeId);
		TestNotEqual(TEXT("Synthetic selected-candidate commit should not fall back to the default waypoint path"),
			PathId,
			FallbackPathId);
	}

	const FFlightPathData* SyntheticPath = PathRegistry->FindPath(NodeId);
	TestNotNull(TEXT("Selected nav-graph candidate should materialize into a synthetic path registry entry"), SyntheticPath);
	if (SyntheticPath)
	{
		TestTrue(TEXT("Synthetic selected-candidate path should have a positive path length"), SyntheticPath->TotalLength > 0.0f);
	}

	FFlightNavigationCommitSharedFragment CommitFragment;
	TestTrue(TEXT("Synthetic selected-candidate commit should materialize a shared navigation commit fragment"),
		FindNavigationCommitSharedFragmentForCohort(World, CohortName, CommitFragment));
	TestEqual(TEXT("Synthetic shared commit fragment should preserve the selected nav-graph candidate id"),
		CommitFragment.SourceCandidateId,
		NodeId);
	TestEqual(TEXT("Synthetic shared commit fragment should preserve the runtime synthetic path id"),
		CommitFragment.RuntimePathId,
		NodeId);
	TestEqual(TEXT("Synthetic shared commit fragment should classify as synthetic node-orbit commit"),
		static_cast<int32>(CommitFragment.CommitKind),
		static_cast<int32>(Flight::Navigation::EFlightNavigationCommitProductKind::SyntheticNodeOrbit));
	TestTrue(TEXT("Synthetic shared commit fragment should report synthetic lowering"),
		CommitFragment.bSynthetic);

	return !CohortPathIds.IsEmpty();
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
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for waypoint-path network constraint test"));
		return false;
	}

	NavGraphHub->ResetGraph();

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
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for waypoint-path network constraint test"));
		return false;
	}

	NavGraphHub->ResetGraph();

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
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for waypoint-path network constraint test"));
		return false;
	}

	NavGraphHub->ResetGraph();

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
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for waypoint-path network constraint test"));
		return false;
	}

	NavGraphHub->ResetGraph();

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationNavigationContractsTest,
	"FlightProject.Orchestration.Navigation.ContractBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationNavigationParticipantSourceTest,
	"FlightProject.Orchestration.Navigation.RealParticipantSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationNavigationCandidateDrivenPlanningTest,
	"FlightProject.Orchestration.Navigation.CandidateDrivenPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationNavigationCandidateAffinityTest,
	"FlightProject.Orchestration.Navigation.CandidateAffinitySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationNavigationNetworkConstraintTest,
	"FlightProject.Orchestration.Navigation.NetworkConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationWaypointPathNetworkConstraintTest,
	"FlightProject.Orchestration.Navigation.WaypointPathConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightOrchestrationWaypointPathValidationTest,
	"FlightProject.Orchestration.Navigation.WaypointPathValidation",
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

bool FFlightOrchestrationNavigationContractsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for navigation contract binding test"));
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
	Orchestration->UnregisterCohort(TEXT("Automation.Navigation.Contracts"));

	Flight::Orchestration::FFlightParticipantRecord PathParticipant;
	PathParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
	PathParticipant.Name = TEXT("Automation.Navigation.Path");
	PathParticipant.OwnerSubsystem = TEXT("Automation");
	PathParticipant.Capabilities.Add(TEXT("NavigationCandidateSource"));
	PathParticipant.Capabilities.Add(TEXT("NavigationCommitLowering"));
	PathParticipant.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
	PathParticipant.ContractKeys.Add(Flight::Navigation::Contracts::CommitKey);
	const Flight::Orchestration::FFlightParticipantHandle PathHandle = Orchestration->RegisterParticipant(PathParticipant);

	Flight::Orchestration::FFlightParticipantRecord AnchorParticipant;
	AnchorParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
	AnchorParticipant.Name = TEXT("Automation.Navigation.Anchor");
	AnchorParticipant.OwnerSubsystem = TEXT("Automation");
	AnchorParticipant.Capabilities.Add(TEXT("NavigationIntentSource"));
	AnchorParticipant.ContractKeys.Add(Flight::Navigation::Contracts::IntentKey);
	const Flight::Orchestration::FFlightParticipantHandle AnchorHandle = Orchestration->RegisterParticipant(AnchorParticipant);

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Automation.Navigation.Contracts");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Participants.Add(AnchorHandle);
	Cohort.Participants.Add(PathHandle);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::IntentKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CandidateKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CommitKey);
	TestTrue(TEXT("Navigation test cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9201;
	Behavior.bExecutable = true;
	Behavior.FrameInterval = 3;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);

	Orchestration->RebuildExecutionPlan();

	const Flight::Orchestration::FFlightOrchestrationReport& Report = Orchestration->GetReport();
	const Flight::Orchestration::FFlightParticipantRecord* ReportedPath = Report.Participants.FindByPredicate(
		[](const Flight::Orchestration::FFlightParticipantRecord& Candidate)
		{
			return Candidate.Name == TEXT("Automation.Navigation.Path");
		});
	TestNotNull(TEXT("Navigation path participant should appear in the orchestration report"), ReportedPath);
	if (ReportedPath)
	{
		TestTrue(TEXT("Navigation path participant should advertise Navigation.Candidate"),
			ReportedPath->ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Navigation path participant should advertise Navigation.Commit"),
			ReportedPath->ContractKeys.Contains(Flight::Navigation::Contracts::CommitKey));
	}

	const Flight::Orchestration::FFlightCohortRecord* ReportedCohort = Report.Cohorts.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightCohortRecord& Candidate)
		{
			return Candidate.Name == Cohort.Name;
		});
	TestNotNull(TEXT("Navigation cohort should appear in the orchestration report"), ReportedCohort);
	if (ReportedCohort)
	{
		TestTrue(TEXT("Navigation cohort should preserve its required navigation intent contract"),
			ReportedCohort->RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::IntentKey));
		TestTrue(TEXT("Navigation cohort should preserve its required navigation candidate contract"),
			ReportedCohort->RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Navigation cohort should preserve its required navigation commit contract"),
			ReportedCohort->RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::CommitKey));
	}

	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Report.ExecutionPlan.Steps.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == Cohort.Name;
		});
	TestNotNull(TEXT("Navigation cohort should publish an execution plan step"), Step);
	if (Step)
	{
		TestTrue(TEXT("Execution plan step should include Navigation.Intent as an input contract"),
			Step->InputContracts.Contains(Flight::Navigation::Contracts::IntentKey));
		TestTrue(TEXT("Execution plan step should include Navigation.Candidate as an input contract"),
			Step->InputContracts.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Execution plan step should include Navigation.Commit as an input contract"),
			Step->InputContracts.Contains(Flight::Navigation::Contracts::CommitKey));
		TestTrue(TEXT("Execution plan step should advertise navigation commit as an output consumer"),
			Step->OutputConsumers.Contains(TEXT("NavigationCommit")));
		TestEqual(TEXT("Execution plan should select the cohort's waypoint-path candidate"),
			Step->NavigationCandidateName, PathParticipant.Name);
		TestTrue(TEXT("Execution plan should record a positive cohort-adjusted candidate score"),
			Step->NavigationCandidateScore > 0.0f);
		TestTrue(TEXT("Execution plan should expose a candidate rank order"),
			Step->NavigationCandidateRankOrder >= 0);
		TestTrue(TEXT("Execution plan should explain why the candidate was selected"),
			!Step->NavigationSelectionReason.IsEmpty());
	}

	const Flight::Orchestration::FFlightNavigationCandidateRecord* PathCandidate = Report.NavigationCandidates.FindByPredicate(
		[](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			return Candidate.SourceKind == Flight::Orchestration::EFlightParticipantKind::WaypointPath
				&& Candidate.Name == TEXT("Automation.Navigation.Path");
		});
	TestNotNull(TEXT("Waypoint path participants should promote into navigation candidate records for planning"), PathCandidate);
	if (PathCandidate)
	{
		TestTrue(TEXT("Waypoint path candidate records should advertise Navigation.Candidate"),
			PathCandidate->ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Waypoint path candidate records should still be legal when only advertised through orchestration"),
			PathCandidate->bLegal);
		TestEqual(TEXT("Synthetic waypoint path candidates should be marked as advertised"),
			PathCandidate->Status, FString(TEXT("Advertised")));
		TestTrue(TEXT("Waypoint path candidate records should carry a usable rank score"),
			PathCandidate->RankScore > 0.0f);
		TestTrue(TEXT("Waypoint path candidate records should publish a rank order"),
			PathCandidate->RankOrder >= 0);
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Orchestration report JSON should surface required navigation contracts"),
		ReportJson.Contains(TEXT("\"requiredNavigationContracts\"")));
	TestTrue(TEXT("Orchestration report JSON should surface Navigation.Commit"),
		ReportJson.Contains(TEXT("Navigation.Commit")));
	TestTrue(TEXT("Orchestration report JSON should surface navigation legality and ranking fields"),
		ReportJson.Contains(TEXT("\"rankScore\"")) && ReportJson.Contains(TEXT("\"legal\"")));
	TestTrue(TEXT("Orchestration report JSON should surface execution-plan navigation selection fields"),
		ReportJson.Contains(TEXT("\"navigationCandidateName\"")) && ReportJson.Contains(TEXT("\"navigationCandidateScore\"")));

	return true;
}

bool FFlightOrchestrationNavigationParticipantSourceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for real navigation participant source test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for navigation participant source test"));
		return false;
	}

	NavGraphHub->ResetGraph();
	ON_SCOPE_EXIT
	{
		NavGraphHub->ResetGraph();
		Orchestration->Rebuild();
	};

	FFlightNavGraphNodeDescriptor NodeA;
	NodeA.DisplayName = TEXT("Automation.Nav.A");
	NodeA.NetworkId = TEXT("AutomationNet");
	NodeA.Location = FVector(0.0f, 0.0f, 1000.0f);
	NodeA.Tags.Add(TEXT("Ingress"));

	FFlightNavGraphNodeDescriptor NodeB;
	NodeB.DisplayName = TEXT("Automation.Nav.B");
	NodeB.NetworkId = TEXT("AutomationNet");
	NodeB.Location = FVector(1000.0f, 0.0f, 1100.0f);
	NodeB.Tags.Add(TEXT("Corridor"));

	const FGuid NodeAId = NavGraphHub->RegisterNode(NodeA);
	const FGuid NodeBId = NavGraphHub->RegisterNode(NodeB);

	FFlightNavGraphEdgeDescriptor Edge;
	Edge.FromNodeId = NodeAId;
	Edge.ToNodeId = NodeBId;
	Edge.BaseCost = 4.5f;
	Edge.Tags.Add(TEXT("Preferred"));
	const FGuid EdgeId = NavGraphHub->RegisterEdge(Edge);
	TestTrue(TEXT("Nav graph edge should register successfully"), EdgeId.IsValid());

	Orchestration->RebuildVisibility();

	const Flight::Orchestration::FFlightOrchestrationReport& Report = Orchestration->GetReport();
	const Flight::Orchestration::FFlightParticipantRecord* NavigationNode = Report.Participants.FindByPredicate(
		[](const Flight::Orchestration::FFlightParticipantRecord& Candidate)
		{
			return Candidate.Kind == Flight::Orchestration::EFlightParticipantKind::NavigationNode
				&& Candidate.Name == TEXT("Automation.Nav.A");
		});
	const Flight::Orchestration::FFlightParticipantRecord* NavigationEdge = Report.Participants.FindByPredicate(
		[](const Flight::Orchestration::FFlightParticipantRecord& Candidate)
		{
			return Candidate.Kind == Flight::Orchestration::EFlightParticipantKind::NavigationEdge;
		});
	const Flight::Orchestration::FFlightNavigationCandidateRecord* NodeCandidate = Report.NavigationCandidates.FindByPredicate(
		[&NodeAId](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			return Candidate.SourceKind == Flight::Orchestration::EFlightParticipantKind::NavigationNode
				&& Candidate.SourceId == NodeAId;
		});
	const Flight::Orchestration::FFlightNavigationCandidateRecord* EdgeCandidate = Report.NavigationCandidates.FindByPredicate(
		[&EdgeId](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			return Candidate.SourceKind == Flight::Orchestration::EFlightParticipantKind::NavigationEdge
				&& Candidate.SourceId == EdgeId;
		});

	TestNotNull(TEXT("Orchestration should ingest nav graph nodes as real navigation participants"), NavigationNode);
	TestNotNull(TEXT("Orchestration should ingest nav graph edges as real navigation participants"), NavigationEdge);
	TestNotNull(TEXT("Orchestration should promote nav graph nodes into navigation candidate records"), NodeCandidate);
	TestNotNull(TEXT("Orchestration should promote nav graph edges into navigation candidate records"), EdgeCandidate);

	if (NavigationNode)
	{
		TestTrue(TEXT("Navigation node participants should advertise Navigation.Candidate"),
			NavigationNode->ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Navigation node participants should advertise NavigationNode capability"),
			NavigationNode->Capabilities.Contains(TEXT("NavigationNode")));
		TestTrue(TEXT("Navigation node participants should retain nav graph tags"),
			NavigationNode->Tags.Contains(TEXT("Ingress")));
	}

	if (NavigationEdge)
	{
		TestTrue(TEXT("Navigation edge participants should advertise Navigation.Candidate"),
			NavigationEdge->ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Navigation edge participants should advertise NavigationEdge capability"),
			NavigationEdge->Capabilities.Contains(TEXT("NavigationEdge")));
		TestTrue(TEXT("Navigation edge participants should retain nav graph tags"),
			NavigationEdge->Tags.Contains(TEXT("Preferred")));
	}

	if (NodeCandidate)
	{
		TestEqual(TEXT("Navigation node candidates should preserve their display name"),
			NodeCandidate->Name, NodeA.DisplayName);
		TestEqual(TEXT("Navigation node candidates should preserve network id"),
			NodeCandidate->NetworkId, NodeA.NetworkId);
		TestEqual(TEXT("Navigation node candidates should preserve their location"),
			NodeCandidate->StartLocation, NodeA.Location);
		TestTrue(TEXT("Navigation node candidates should advertise Navigation.Candidate"),
			NodeCandidate->ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey));
		TestTrue(TEXT("Navigation node candidates should retain nav graph tags"),
			NodeCandidate->Tags.Contains(TEXT("Ingress")));
		TestTrue(TEXT("Navigation node candidates should be marked legal"),
			NodeCandidate->bLegal);
		TestEqual(TEXT("Navigation node candidates should be marked resolved"),
			NodeCandidate->Status, FString(TEXT("Resolved")));
		TestTrue(TEXT("Navigation node candidates should publish a non-negative rank score"),
			NodeCandidate->RankScore >= 0.0f);
		TestTrue(TEXT("Navigation node candidates should publish a rank order"),
			NodeCandidate->RankOrder >= 0);
	}

	if (EdgeCandidate)
	{
		TestEqual(TEXT("Navigation edge candidates should preserve their base cost"),
			EdgeCandidate->EstimatedCost, Edge.BaseCost);
		TestEqual(TEXT("Navigation edge candidates should preserve their start location"),
			EdgeCandidate->StartLocation, NodeA.Location);
		TestEqual(TEXT("Navigation edge candidates should preserve their end location"),
			EdgeCandidate->EndLocation, NodeB.Location);
		TestTrue(TEXT("Navigation edge candidates should preserve bidirectionality"),
			EdgeCandidate->bBidirectional);
		TestTrue(TEXT("Navigation edge candidates should retain nav graph tags"),
			EdgeCandidate->Tags.Contains(TEXT("Preferred")));
		TestTrue(TEXT("Navigation edge candidates should be marked legal"),
			EdgeCandidate->bLegal);
		TestEqual(TEXT("Navigation edge candidates should be marked resolved"),
			EdgeCandidate->Status, FString(TEXT("Resolved")));
		TestTrue(TEXT("Navigation edge candidates should publish a non-negative rank score"),
			EdgeCandidate->RankScore >= 0.0f);
		TestTrue(TEXT("Navigation edge candidates should publish a rank order"),
			EdgeCandidate->RankOrder >= 0);
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Orchestration report JSON should include promoted navigation candidates"),
		ReportJson.Contains(TEXT("\"navigationCandidates\"")));
	TestTrue(TEXT("Orchestration report JSON should include the real nav graph node name"),
		ReportJson.Contains(TEXT("Automation.Nav.A")));

	return true;
}

bool FFlightOrchestrationNavigationCandidateDrivenPlanningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for candidate-driven planning test"));
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
	Orchestration->UnregisterCohort(TEXT("Automation.Navigation.RequiresCandidates"));

	Flight::Orchestration::FFlightParticipantRecord AnchorParticipant;
	AnchorParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
	AnchorParticipant.Name = TEXT("Automation.Navigation.RequiresCandidates.Anchor");
	AnchorParticipant.OwnerSubsystem = TEXT("Automation");
	AnchorParticipant.Capabilities.Add(TEXT("NavigationIntentSource"));
	AnchorParticipant.ContractKeys.Add(Flight::Navigation::Contracts::IntentKey);
	const Flight::Orchestration::FFlightParticipantHandle AnchorHandle = Orchestration->RegisterParticipant(AnchorParticipant);

	Flight::Orchestration::FFlightParticipantRecord CommitParticipant;
	CommitParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
	CommitParticipant.Name = TEXT("Automation.Navigation.RequiresCandidates.CommitOnly");
	CommitParticipant.OwnerSubsystem = TEXT("Automation");
	CommitParticipant.Capabilities.Add(TEXT("NavigationCommitLowering"));
	CommitParticipant.ContractKeys.Add(Flight::Navigation::Contracts::CommitKey);
	const Flight::Orchestration::FFlightParticipantHandle CommitHandle = Orchestration->RegisterParticipant(CommitParticipant);

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Automation.Navigation.RequiresCandidates");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Participants.Add(AnchorHandle);
	Cohort.Participants.Add(CommitHandle);
	Cohort.DesiredNavigationNetwork = TEXT("__AutomationMissingNetwork__");
	Cohort.DesiredNavigationSubNetwork = TEXT("__AutomationMissingLane__");
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::IntentKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CandidateKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CommitKey);
	TestTrue(TEXT("Candidate-driven cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9202;
	Behavior.bExecutable = true;
	Behavior.FrameInterval = 2;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);

	Orchestration->RebuildExecutionPlan();

	Flight::Orchestration::FFlightBehaviorBinding Binding;
	TestFalse(TEXT("Cohorts requiring Navigation.Candidate should not bind when no eligible candidate matches the cohort routing constraints"),
		Orchestration->TryGetBindingForCohort(Cohort.Name, Binding));

	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Orchestration->GetExecutionPlan().Steps.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == Cohort.Name;
		});
	TestNull(TEXT("Execution plan should skip cohorts whose required navigation candidates are unavailable or ineligible"), Step);

	return true;
}

bool FFlightOrchestrationNavigationCandidateAffinityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for candidate affinity planning test"));
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
	Orchestration->UnregisterCohort(TEXT("Automation.Navigation.Affinity"));

	Flight::Orchestration::FFlightParticipantRecord PreferredPath;
	PreferredPath.Kind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
	PreferredPath.Name = TEXT("Automation.Navigation.PreferredPath");
	PreferredPath.OwnerSubsystem = TEXT("Automation");
	PreferredPath.Capabilities.Add(TEXT("NavigationCandidateSource"));
	PreferredPath.Capabilities.Add(TEXT("NavigationCommitLowering"));
	PreferredPath.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
	PreferredPath.ContractKeys.Add(Flight::Navigation::Contracts::CommitKey);
	const Flight::Orchestration::FFlightParticipantHandle PreferredPathHandle = Orchestration->RegisterParticipant(PreferredPath);

	Flight::Orchestration::FFlightParticipantRecord CompetingPath;
	CompetingPath.Kind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
	CompetingPath.Name = TEXT("Automation.Navigation.CompetingPath");
	CompetingPath.OwnerSubsystem = TEXT("Automation");
	CompetingPath.Capabilities.Add(TEXT("NavigationCandidateSource"));
	CompetingPath.Capabilities.Add(TEXT("NavigationCommitLowering"));
	CompetingPath.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
	CompetingPath.ContractKeys.Add(Flight::Navigation::Contracts::CommitKey);
	Orchestration->RegisterParticipant(CompetingPath);

	Flight::Orchestration::FFlightParticipantRecord AnchorParticipant;
	AnchorParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
	AnchorParticipant.Name = TEXT("Automation.Navigation.Affinity.Anchor");
	AnchorParticipant.OwnerSubsystem = TEXT("Automation");
	AnchorParticipant.Capabilities.Add(TEXT("NavigationIntentSource"));
	AnchorParticipant.ContractKeys.Add(Flight::Navigation::Contracts::IntentKey);
	const Flight::Orchestration::FFlightParticipantHandle AnchorHandle = Orchestration->RegisterParticipant(AnchorParticipant);

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Automation.Navigation.Affinity");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Participants.Add(AnchorHandle);
	Cohort.Participants.Add(PreferredPathHandle);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::IntentKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CandidateKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CommitKey);
	TestTrue(TEXT("Affinity cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9203;
	Behavior.bExecutable = true;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);

	Orchestration->RebuildExecutionPlan();

	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Orchestration->GetExecutionPlan().Steps.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == Cohort.Name;
		});
	TestNotNull(TEXT("Candidate-affinity cohort should produce an execution-plan step"), Step);
	if (!Step)
	{
		return false;
	}

	TestEqual(TEXT("Cohort planning should prefer the candidate represented by a cohort participant"),
		Step->NavigationCandidateName, PreferredPath.Name);
	TestTrue(TEXT("Cohort planning should explain participant-affinity selection"),
		Step->NavigationSelectionReason.Contains(TEXT("ParticipantAffinity")));

	return true;
}

bool FFlightOrchestrationNavigationNetworkConstraintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for navigation network constraint test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for navigation network constraint test"));
		return false;
	}

	NavGraphHub->ResetGraph();
	ON_SCOPE_EXIT
	{
		NavGraphHub->ResetGraph();
		Orchestration->Rebuild();
	};

	Orchestration->RebuildVisibility();
	RemoveVisibleBehaviorRecords(*Orchestration);
	Orchestration->UnregisterCohort(TEXT("Automation.Navigation.NetworkScoped"));

	FFlightNavGraphNodeDescriptor NodeA;
	NodeA.DisplayName = TEXT("Automation.Network.A");
	NodeA.NetworkId = TEXT("NetworkA");
	NodeA.SubNetworkId = TEXT("Lane1");
	NodeA.Location = FVector(0.0f, 0.0f, 1000.0f);
	const FGuid NodeAId = NavGraphHub->RegisterNode(NodeA);

	FFlightNavGraphNodeDescriptor NodeB;
	NodeB.DisplayName = TEXT("Automation.Network.B");
	NodeB.NetworkId = TEXT("NetworkB");
	NodeB.SubNetworkId = TEXT("Lane2");
	NodeB.Location = FVector(2500.0f, 0.0f, 1000.0f);
	NavGraphHub->RegisterNode(NodeB);

	FFlightNavGraphNodeDescriptor NodeC;
	NodeC.DisplayName = TEXT("Automation.Network.C");
	NodeC.NetworkId = TEXT("NetworkA");
	NodeC.SubNetworkId = TEXT("Lane2");
	NodeC.Location = FVector(500.0f, 0.0f, 1000.0f);
	NavGraphHub->RegisterNode(NodeC);

	Orchestration->RebuildVisibility();

	Flight::Orchestration::FFlightParticipantRecord AnchorParticipant;
	AnchorParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
	AnchorParticipant.Name = TEXT("Automation.Navigation.NetworkScoped.Anchor");
	AnchorParticipant.OwnerSubsystem = TEXT("Automation");
	AnchorParticipant.Capabilities.Add(TEXT("NavigationIntentSource"));
	AnchorParticipant.ContractKeys.Add(Flight::Navigation::Contracts::IntentKey);
	const Flight::Orchestration::FFlightParticipantHandle AnchorHandle = Orchestration->RegisterParticipant(AnchorParticipant);

	Flight::Orchestration::FFlightParticipantRecord CommitParticipant;
	CommitParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
	CommitParticipant.Name = TEXT("Automation.Navigation.NetworkScoped.Commit");
	CommitParticipant.OwnerSubsystem = TEXT("Automation");
	CommitParticipant.Capabilities.Add(TEXT("NavigationCommitLowering"));
	CommitParticipant.ContractKeys.Add(Flight::Navigation::Contracts::CommitKey);
	const Flight::Orchestration::FFlightParticipantHandle CommitHandle = Orchestration->RegisterParticipant(CommitParticipant);

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Automation.Navigation.NetworkScoped");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Participants.Add(AnchorHandle);
	Cohort.Participants.Add(CommitHandle);
	Cohort.DesiredNavigationNetwork = TEXT("NetworkA");
	Cohort.DesiredNavigationSubNetwork = TEXT("Lane1");
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::IntentKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CandidateKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CommitKey);
	TestTrue(TEXT("Network-scoped cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9204;
	Behavior.bExecutable = true;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);

	Orchestration->RebuildExecutionPlan();

	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Orchestration->GetExecutionPlan().Steps.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == Cohort.Name;
		});
	TestNotNull(TEXT("Network-scoped cohort should produce an execution-plan step"), Step);
	if (!Step)
	{
		return false;
	}

	TestEqual(TEXT("Network-scoped planning should select the only candidate in the requested network and subnetwork"),
		Step->NavigationCandidateId, NodeAId);
	TestTrue(TEXT("Network-scoped planning should explain the network legality constraint"),
		Step->NavigationSelectionReason.Contains(TEXT("Network:NetworkA")));
	TestTrue(TEXT("Network-scoped planning should explain the subnetwork legality constraint"),
		Step->NavigationSelectionReason.Contains(TEXT("SubNetwork:Lane1")));

	const Flight::Orchestration::FFlightCohortRecord* ReportedCohort = Orchestration->GetReport().Cohorts.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightCohortRecord& Candidate)
		{
			return Candidate.Name == Cohort.Name;
		});
	TestNotNull(TEXT("Network-scoped cohort should appear in the orchestration report"), ReportedCohort);
	if (ReportedCohort)
	{
		TestEqual(TEXT("Network-scoped cohort should preserve desired network"),
			ReportedCohort->DesiredNavigationNetwork, Cohort.DesiredNavigationNetwork);
		TestEqual(TEXT("Network-scoped cohort should preserve desired subnetwork"),
			ReportedCohort->DesiredNavigationSubNetwork, Cohort.DesiredNavigationSubNetwork);
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Orchestration report JSON should surface desired navigation network"),
		ReportJson.Contains(TEXT("\"desiredNavigationNetwork\"")) && ReportJson.Contains(TEXT("NetworkA")));
	TestTrue(TEXT("Orchestration report JSON should surface desired navigation subnetwork"),
		ReportJson.Contains(TEXT("\"desiredNavigationSubNetwork\"")) && ReportJson.Contains(TEXT("Lane1")));

	return true;
}

bool FFlightOrchestrationWaypointPathNetworkConstraintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for waypoint-path network constraint test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for waypoint-path network constraint test"));
		return false;
	}

	NavGraphHub->ResetGraph();

	FActorSpawnParameters MatchingParams;
	MatchingParams.Name = TEXT("Automation.Network.PathMatch");
	AFlightWaypointPath* MatchingPath = World->SpawnActor<AFlightWaypointPath>(FVector(0.0f, 0.0f, 1000.0f), FRotator::ZeroRotator, MatchingParams);

	FActorSpawnParameters OtherParams;
	OtherParams.Name = TEXT("Automation.Network.PathOther");
	AFlightWaypointPath* OtherPath = World->SpawnActor<AFlightWaypointPath>(FVector(12000.0f, 0.0f, 1000.0f), FRotator::ZeroRotator, OtherParams);

	if (!MatchingPath || !OtherPath)
	{
		AddError(TEXT("Failed to spawn waypoint-path actors for network constraint test"));
		return false;
	}

	MatchingPath->SetNavigationRoutingMetadata(TEXT("PathNetworkA"), TEXT("LaneA"));
	OtherPath->SetNavigationRoutingMetadata(TEXT("PathNetworkB"), TEXT("LaneB"));
	MatchingPath->EnsureDefaultLoop();
	OtherPath->EnsureDefaultLoop();

	FFlightNavGraphNodeDescriptor MatchingNode;
	MatchingNode.DisplayName = TEXT("Automation.Network.PathMatch.Node");
	MatchingNode.NetworkId = TEXT("PathNetworkA");
	MatchingNode.SubNetworkId = TEXT("LaneA");
	MatchingNode.Location = MatchingPath->GetLocationAtNormalizedPosition(0.5f);
	NavGraphHub->RegisterNode(MatchingNode);

	FFlightNavGraphNodeDescriptor OtherNode;
	OtherNode.DisplayName = TEXT("Automation.Network.PathOther.Node");
	OtherNode.NetworkId = TEXT("PathNetworkB");
	OtherNode.SubNetworkId = TEXT("LaneB");
	OtherNode.Location = OtherPath->GetLocationAtNormalizedPosition(0.5f);
	NavGraphHub->RegisterNode(OtherNode);

	ON_SCOPE_EXIT
	{
		NavGraphHub->ResetGraph();
		if (MatchingPath)
		{
			MatchingPath->Destroy();
		}
		if (OtherPath)
		{
			OtherPath->Destroy();
		}
		Orchestration->Rebuild();
	};

	Orchestration->RebuildVisibility();
	RemoveVisibleBehaviorRecords(*Orchestration);
	Orchestration->UnregisterCohort(TEXT("Automation.Navigation.PathNetworkScoped"));

	Flight::Orchestration::FFlightParticipantRecord AnchorParticipant;
	AnchorParticipant.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
	AnchorParticipant.Name = TEXT("Automation.Navigation.PathNetworkScoped.Anchor");
	AnchorParticipant.OwnerSubsystem = TEXT("Automation");
	AnchorParticipant.Capabilities.Add(TEXT("NavigationIntentSource"));
	AnchorParticipant.ContractKeys.Add(Flight::Navigation::Contracts::IntentKey);
	const Flight::Orchestration::FFlightParticipantHandle AnchorHandle = Orchestration->RegisterParticipant(AnchorParticipant);

	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Automation.Navigation.PathNetworkScoped");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.Participants.Add(AnchorHandle);
	Cohort.DesiredNavigationNetwork = TEXT("PathNetworkA");
	Cohort.DesiredNavigationSubNetwork = TEXT("LaneA");
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::IntentKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CandidateKey);
	Cohort.RequiredNavigationContracts.Add(Flight::Navigation::Contracts::CommitKey);
	TestTrue(TEXT("Waypoint-path constrained cohort should register"), Orchestration->RegisterCohort(Cohort));

	Flight::Orchestration::FFlightBehaviorRecord Behavior;
	Behavior.BehaviorID = 9205;
	Behavior.bExecutable = true;
	Behavior.ResolvedDomain = Flight::Orchestration::EFlightExecutionDomain::NativeCpu;
	Orchestration->RegisterBehavior(Behavior.BehaviorID, Behavior);

	Orchestration->RebuildExecutionPlan();

	const Flight::Orchestration::FFlightExecutionPlanStep* Step = Orchestration->GetExecutionPlan().Steps.FindByPredicate(
		[&Cohort](const Flight::Orchestration::FFlightExecutionPlanStep& Candidate)
		{
			return Candidate.CohortName == Cohort.Name;
		});
	TestNotNull(TEXT("Waypoint-path constrained cohort should produce an execution-plan step"), Step);
	if (!Step)
	{
		return false;
	}

	TestEqual(TEXT("Network-constrained planning should select the matching real waypoint path"),
		Step->NavigationCandidateName, MatchingPath->GetFName());
	TestTrue(TEXT("Waypoint-path selection should explain network legality"),
		Step->NavigationSelectionReason.Contains(TEXT("Network:PathNetworkA")));
	TestTrue(TEXT("Waypoint-path selection should explain subnetwork legality"),
		Step->NavigationSelectionReason.Contains(TEXT("SubNetwork:LaneA")));

	const Flight::Orchestration::FFlightNavigationCandidateRecord* MatchingCandidate = Orchestration->GetReport().NavigationCandidates.FindByPredicate(
		[MatchingPath](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			return Candidate.SourceKind == Flight::Orchestration::EFlightParticipantKind::WaypointPath
				&& Candidate.Name == MatchingPath->GetFName();
		});
	TestNotNull(TEXT("Real waypoint path should promote into a navigation candidate record"), MatchingCandidate);
	if (MatchingCandidate)
	{
		TestEqual(TEXT("Real waypoint-path candidates should carry authored navigation network"),
			MatchingCandidate->NetworkId, FName(TEXT("PathNetworkA")));
		TestEqual(TEXT("Real waypoint-path candidates should carry authored navigation subnetwork"),
			MatchingCandidate->SubNetworkId, FName(TEXT("LaneA")));
		TestEqual(TEXT("Real waypoint-path candidates should be marked resolved"),
			MatchingCandidate->Status, FString(TEXT("Resolved")));
		TestEqual(TEXT("Real waypoint-path candidates should report nearby-node validation match"),
			MatchingCandidate->RoutingValidationStatus, FString(TEXT("Match")));
		TestEqual(TEXT("Real waypoint-path candidates should suggest the matching network"),
			MatchingCandidate->SuggestedNetworkId, FName(TEXT("PathNetworkA")));
		TestEqual(TEXT("Real waypoint-path candidates should suggest the matching subnetwork"),
			MatchingCandidate->SuggestedSubNetworkId, FName(TEXT("LaneA")));
	}

	return true;
}

bool FFlightOrchestrationWaypointPathValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = FindAutomationWorld();
	if (!World)
	{
		AddError(TEXT("No automation world available for waypoint-path validation test"));
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!Orchestration || !NavGraphHub)
	{
		AddError(TEXT("Required subsystems not found for waypoint-path validation test"));
		return false;
	}

	NavGraphHub->ResetGraph();

	FActorSpawnParameters MatchingParams;
	MatchingParams.Name = TEXT("Automation.Validation.PathMatch");
	AFlightWaypointPath* MatchingPath = World->SpawnActor<AFlightWaypointPath>(FVector(0.0f, 0.0f, 1200.0f), FRotator::ZeroRotator, MatchingParams);

	FActorSpawnParameters MismatchParams;
	MismatchParams.Name = TEXT("Automation.Validation.PathMismatch");
	AFlightWaypointPath* MismatchPath = World->SpawnActor<AFlightWaypointPath>(FVector(4000.0f, 0.0f, 1200.0f), FRotator::ZeroRotator, MismatchParams);

	if (!MatchingPath || !MismatchPath)
	{
		AddError(TEXT("Failed to spawn waypoint-path actors for validation test"));
		return false;
	}

	MatchingPath->SetNavigationRoutingMetadata(TEXT("ValidateNet"), TEXT("LaneOne"));
	MismatchPath->SetNavigationRoutingMetadata(TEXT("WrongNet"), TEXT("LaneWrong"));
	MatchingPath->EnsureDefaultLoop();
	MismatchPath->EnsureDefaultLoop();

	FFlightNavGraphNodeDescriptor MatchingNode;
	MatchingNode.DisplayName = TEXT("Automation.Validation.MatchNode");
	MatchingNode.NetworkId = TEXT("ValidateNet");
	MatchingNode.SubNetworkId = TEXT("LaneOne");
	MatchingNode.Location = MatchingPath->GetLocationAtNormalizedPosition(0.5f);
	NavGraphHub->RegisterNode(MatchingNode);

	FFlightNavGraphNodeDescriptor MismatchNode;
	MismatchNode.DisplayName = TEXT("Automation.Validation.MismatchNode");
	MismatchNode.NetworkId = TEXT("ActualNet");
	MismatchNode.SubNetworkId = TEXT("ActualLane");
	MismatchNode.Location = MismatchPath->GetLocationAtNormalizedPosition(0.5f);
	NavGraphHub->RegisterNode(MismatchNode);

	ON_SCOPE_EXIT
	{
		NavGraphHub->ResetGraph();
		if (MatchingPath)
		{
			MatchingPath->Destroy();
		}
		if (MismatchPath)
		{
			MismatchPath->Destroy();
		}
		Orchestration->Rebuild();
	};

	Orchestration->RebuildVisibility();

	const Flight::Orchestration::FFlightNavigationCandidateRecord* MatchingCandidate = Orchestration->GetReport().NavigationCandidates.FindByPredicate(
		[MatchingPath](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			return Candidate.SourceKind == Flight::Orchestration::EFlightParticipantKind::WaypointPath
				&& Candidate.Name == MatchingPath->GetFName();
		});
	const Flight::Orchestration::FFlightNavigationCandidateRecord* MismatchCandidate = Orchestration->GetReport().NavigationCandidates.FindByPredicate(
		[MismatchPath](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			return Candidate.SourceKind == Flight::Orchestration::EFlightParticipantKind::WaypointPath
				&& Candidate.Name == MismatchPath->GetFName();
		});

	TestNotNull(TEXT("Validation test should promote the matching waypoint path"), MatchingCandidate);
	TestNotNull(TEXT("Validation test should promote the mismatching waypoint path"), MismatchCandidate);

	if (MatchingCandidate)
	{
		TestEqual(TEXT("Matching waypoint path should report validation match"),
			MatchingCandidate->RoutingValidationStatus, FString(TEXT("Match")));
		TestEqual(TEXT("Matching waypoint path should suggest the expected network"),
			MatchingCandidate->SuggestedNetworkId, FName(TEXT("ValidateNet")));
		TestEqual(TEXT("Matching waypoint path should suggest the expected subnetwork"),
			MatchingCandidate->SuggestedSubNetworkId, FName(TEXT("LaneOne")));
	}

	if (MismatchCandidate)
	{
		TestEqual(TEXT("Mismatching waypoint path should report validation mismatch"),
			MismatchCandidate->RoutingValidationStatus, FString(TEXT("Mismatch")));
		TestEqual(TEXT("Mismatching waypoint path should surface the nearby suggested network"),
			MismatchCandidate->SuggestedNetworkId, FName(TEXT("ActualNet")));
		TestEqual(TEXT("Mismatching waypoint path should surface the nearby suggested subnetwork"),
			MismatchCandidate->SuggestedSubNetworkId, FName(TEXT("ActualLane")));
		TestTrue(TEXT("Mismatching waypoint path should explain the routing drift"),
			MismatchCandidate->RoutingValidationDetail.Contains(TEXT("disagree")));
	}

	const Flight::Orchestration::FFlightOrchestrationDiagnostic* ValidationDiagnostic = Orchestration->GetReport().Diagnostics.FindByPredicate(
		[MismatchPath](const Flight::Orchestration::FFlightOrchestrationDiagnostic& Diagnostic)
		{
			return Diagnostic.Category == TEXT("NavigationRouting")
				&& Diagnostic.SourceName == MismatchPath->GetFName();
		});
	TestNotNull(TEXT("Waypoint-path validation mismatch should surface as an orchestration diagnostic"), ValidationDiagnostic);
	if (ValidationDiagnostic)
	{
		TestEqual(TEXT("Waypoint-path validation diagnostics should be warnings"),
			ValidationDiagnostic->Severity, FName(TEXT("Warning")));
		TestTrue(TEXT("Waypoint-path validation diagnostics should mention mismatch status"),
			ValidationDiagnostic->Message.Contains(TEXT("Mismatch")));
	}

	const FString ReportJson = Orchestration->BuildReportJson();
	TestTrue(TEXT("Orchestration report JSON should surface waypoint-path routing validation fields"),
		ReportJson.Contains(TEXT("\"routingValidationStatus\"")) && ReportJson.Contains(TEXT("\"suggestedNetworkId\"")));
	TestTrue(TEXT("Orchestration report JSON should surface diagnostics"),
		ReportJson.Contains(TEXT("\"diagnostics\"")) && ReportJson.Contains(TEXT("NavigationRouting")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
