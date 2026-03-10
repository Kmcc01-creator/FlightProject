// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Orchestration/FlightOrchestrationSubsystem.h"

#include "FlightProject.h"
#include "FlightSpawnSwarmAnchor.h"
#include "FlightWaypointPath.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "Core/FlightSpatialField.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Mass/FlightWaypointPathRegistry.h"
#include "Orchestration/FlightBehaviorBinding.h"
#include "Orchestration/FlightExecutionPlan.h"
#include "Orchestration/FlightOrchestrationReport.h"
#include "Orchestration/FlightParticipantTypes.h"
#include "Rendering/FlightSimpleSCSLShaderPipelineSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Spatial/FlightSpatialSubsystem.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Verse/UFlightVexTaskSubsystem.h"

namespace Flight::Orchestration
{

namespace
{

const FName DefaultSwarmCohortName(TEXT("Swarm.Default"));

FString ParticipantKindToString(const EFlightParticipantKind Kind)
{
	switch (Kind)
	{
	case EFlightParticipantKind::Service:
		return TEXT("Service");
	case EFlightParticipantKind::RenderAdapter:
		return TEXT("RenderAdapter");
	case EFlightParticipantKind::SpawnAnchor:
		return TEXT("SpawnAnchor");
	case EFlightParticipantKind::WaypointPath:
		return TEXT("WaypointPath");
	case EFlightParticipantKind::SpatialField:
		return TEXT("SpatialField");
	case EFlightParticipantKind::BehaviorProvider:
		return TEXT("BehaviorProvider");
	default:
		return TEXT("Unknown");
	}
}

FString ExecutionDomainToString(const EFlightExecutionDomain Domain)
{
	switch (Domain)
	{
	case EFlightExecutionDomain::NativeCpu:
		return TEXT("NativeCpu");
	case EFlightExecutionDomain::TaskGraph:
		return TEXT("TaskGraph");
	case EFlightExecutionDomain::VerseVm:
		return TEXT("VerseVm");
	case EFlightExecutionDomain::Simd:
		return TEXT("Simd");
	case EFlightExecutionDomain::Gpu:
		return TEXT("Gpu");
	default:
		return TEXT("Unknown");
	}
}

FString SpatialFieldTypeToString(const Flight::Spatial::ESpatialFieldType FieldType)
{
	switch (FieldType)
	{
	case Flight::Spatial::ESpatialFieldType::Force:
		return TEXT("Force");
	case Flight::Spatial::ESpatialFieldType::Density:
		return TEXT("Density");
	case Flight::Spatial::ESpatialFieldType::Gradient:
		return TEXT("Gradient");
	case Flight::Spatial::ESpatialFieldType::Occlusion:
		return TEXT("Occlusion");
	default:
		return TEXT("Unknown");
	}
}

FString VerseCompileStateToString(const EFlightVerseCompileState CompileState)
{
	switch (CompileState)
	{
	case EFlightVerseCompileState::GeneratedOnly:
		return TEXT("GeneratedOnly");
	case EFlightVerseCompileState::VmCompiled:
		return TEXT("VmCompiled");
	case EFlightVerseCompileState::VmCompileFailed:
		return TEXT("VmCompileFailed");
	default:
		return TEXT("Unknown");
	}
}

EFlightExecutionDomain ResolveExecutionDomain(const UFlightVerseSubsystem::FVerseBehavior& Behavior)
{
	if (Behavior.SimdPlan.IsValid())
	{
		return EFlightExecutionDomain::Simd;
	}

	if (Behavior.bUsesVmEntryPoint)
	{
		return Behavior.bIsAsync ? EFlightExecutionDomain::TaskGraph : EFlightExecutionDomain::VerseVm;
	}

	if (Behavior.bUsesNativeFallback || Behavior.bHasExecutableProcedure)
	{
		return Behavior.bIsAsync ? EFlightExecutionDomain::TaskGraph : EFlightExecutionDomain::NativeCpu;
	}

	return EFlightExecutionDomain::Unknown;
}

void AddNameArrayToJson(const TArray<FName>& Names, const TCHAR* FieldName, TSharedRef<FJsonObject> Object)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	Object->SetArrayField(FieldName, Values);
}

const Flight::Orchestration::FFlightBehaviorRecord* SelectBehaviorForCohort(
	const Flight::Orchestration::FFlightCohortRecord& Cohort,
	const TMap<uint32, Flight::Orchestration::FFlightBehaviorRecord>& BehaviorsById)
{
	if (Cohort.Name.IsNone())
	{
		return nullptr;
	}

	auto SatisfiesContracts = [](const TArray<FName>& RequiredContracts, const TArray<FName>& CandidateContracts) -> bool
	{
		for (const FName Contract : RequiredContracts)
		{
			if (!CandidateContracts.Contains(Contract))
			{
				return false;
			}
		}
		return true;
	};

	auto IsBehaviorLegal = [&Cohort, &SatisfiesContracts](const Flight::Orchestration::FFlightBehaviorRecord& Behavior) -> bool
	{
		if (!Behavior.bExecutable)
		{
			return false;
		}

		if (!Cohort.AllowedBehaviorIds.IsEmpty() && !Cohort.AllowedBehaviorIds.Contains(Behavior.BehaviorID))
		{
			return false;
		}

		if (Cohort.DeniedBehaviorIds.Contains(Behavior.BehaviorID))
		{
			return false;
		}

		if (!Cohort.RequiredBehaviorContracts.IsEmpty()
			&& !SatisfiesContracts(Cohort.RequiredBehaviorContracts, Behavior.RequiredContracts))
		{
			return false;
		}

		return true;
	};

	if (Cohort.PreferredBehaviorId >= 0)
	{
		if (const Flight::Orchestration::FFlightBehaviorRecord* PreferredBehavior = BehaviorsById.Find(static_cast<uint32>(Cohort.PreferredBehaviorId));
			PreferredBehavior && IsBehaviorLegal(*PreferredBehavior))
		{
			return PreferredBehavior;
		}
	}

	const Flight::Orchestration::FFlightBehaviorRecord* SelectedBehavior = nullptr;
	for (const TPair<uint32, Flight::Orchestration::FFlightBehaviorRecord>& Pair : BehaviorsById)
	{
		if (!IsBehaviorLegal(Pair.Value))
		{
			continue;
		}

		if (!SelectedBehavior || Pair.Key < SelectedBehavior->BehaviorID)
		{
			SelectedBehavior = &Pair.Value;
		}
	}

	return SelectedBehavior;
}

} // namespace

} // namespace Flight::Orchestration

void UFlightOrchestrationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UFlightWorldBootstrapSubsystem>();
	Collection.InitializeDependency<UFlightSwarmSubsystem>();
	Collection.InitializeDependency<UFlightSpatialSubsystem>();
	Collection.InitializeDependency<UFlightVerseSubsystem>();
	Collection.InitializeDependency<UFlightVexTaskSubsystem>();
	Collection.InitializeDependency<UFlightWaypointPathRegistry>();
	Collection.InitializeDependency<UFlightSimpleSCSLShaderPipelineSubsystem>();

	RebuildVisibility();
	RebuildExecutionPlan();
}

void UFlightOrchestrationSubsystem::Deinitialize()
{
	ResetVisibilityState();
	Bindings.Reset();
	Services.Reset();
	MissingContracts.Reset();
	ExecutionPlan = Flight::Orchestration::FFlightExecutionPlan();
	CachedReport = Flight::Orchestration::FFlightOrchestrationReport();
	Super::Deinitialize();
}

Flight::Orchestration::FFlightParticipantHandle UFlightOrchestrationSubsystem::RegisterParticipant(
	const Flight::Orchestration::FFlightParticipantRecord& Record)
{
	Flight::Orchestration::FFlightParticipantRecord StoredRecord = Record;
	if (!StoredRecord.Handle.IsValid())
	{
		StoredRecord.Handle.Value = NextParticipantHandle++;
	}

	ParticipantsByHandle.Add(StoredRecord.Handle.Value, StoredRecord);
	return StoredRecord.Handle;
}

void UFlightOrchestrationSubsystem::UnregisterParticipant(const Flight::Orchestration::FFlightParticipantHandle Handle)
{
	if (Handle.IsValid())
	{
		ParticipantsByHandle.Remove(Handle.Value);
	}
}

void UFlightOrchestrationSubsystem::RegisterBehavior(const uint32 BehaviorID, const Flight::Orchestration::FFlightBehaviorRecord& Record)
{
	BehaviorsById.Add(BehaviorID, Record);
}

void UFlightOrchestrationSubsystem::UnregisterBehavior(const uint32 BehaviorID)
{
	BehaviorsById.Remove(BehaviorID);
}

bool UFlightOrchestrationSubsystem::RegisterCohort(const Flight::Orchestration::FFlightCohortRecord& Cohort)
{
	if (Cohort.Name.IsNone())
	{
		return false;
	}

	CohortsByName.Add(Cohort.Name, Cohort);
	return true;
}

void UFlightOrchestrationSubsystem::UnregisterCohort(const FName CohortName)
{
	if (!CohortName.IsNone())
	{
		CohortsByName.Remove(CohortName);
	}
}

bool UFlightOrchestrationSubsystem::BindBehaviorToCohort(const Flight::Orchestration::FFlightBehaviorBinding& Binding)
{
	if (Binding.CohortName.IsNone() || Binding.BehaviorID == 0)
	{
		return false;
	}

	Bindings.Add(Binding);
	return true;
}

void UFlightOrchestrationSubsystem::ClearBindingsForCohort(const FName CohortName)
{
	Bindings.RemoveAll([CohortName](const Flight::Orchestration::FFlightBehaviorBinding& Binding)
	{
		return Binding.CohortName == CohortName;
	});
}

const Flight::Orchestration::FFlightExecutionPlan& UFlightOrchestrationSubsystem::GetExecutionPlan() const
{
	return ExecutionPlan;
}

const Flight::Orchestration::FFlightOrchestrationReport& UFlightOrchestrationSubsystem::GetReport() const
{
	return CachedReport;
}

bool UFlightOrchestrationSubsystem::IsServiceAvailable(const FName ServiceName) const
{
	for (const Flight::Orchestration::FFlightServiceStatus& Service : Services)
	{
		if (Service.ServiceName == ServiceName)
		{
			return Service.bAvailable;
		}
	}

	return false;
}

const Flight::Orchestration::FFlightParticipantRecord* UFlightOrchestrationSubsystem::FindParticipant(
	const Flight::Orchestration::FFlightParticipantHandle Handle) const
{
	return Handle.IsValid() ? ParticipantsByHandle.Find(Handle.Value) : nullptr;
}

const Flight::Orchestration::FFlightCohortRecord* UFlightOrchestrationSubsystem::FindCohort(const FName CohortName) const
{
	return CohortsByName.Find(CohortName);
}

bool UFlightOrchestrationSubsystem::TryGetBindingForCohort(
	const FName CohortName,
	Flight::Orchestration::FFlightBehaviorBinding& OutBinding) const
{
	auto FindBinding = [this](const FName TargetCohortName) -> const Flight::Orchestration::FFlightBehaviorBinding*
	{
		const Flight::Orchestration::FFlightBehaviorBinding* SelectedBinding = nullptr;
		bool bSelectedBehaviorIsExecutable = false;

		for (const Flight::Orchestration::FFlightBehaviorBinding& Binding : Bindings)
		{
			if (Binding.CohortName != TargetCohortName)
			{
				continue;
			}

			const bool bBindingBehaviorIsExecutable = BehaviorsById.FindRef(Binding.BehaviorID).bExecutable;
			if (!SelectedBinding
				|| (bBindingBehaviorIsExecutable && !bSelectedBehaviorIsExecutable)
				|| (bBindingBehaviorIsExecutable == bSelectedBehaviorIsExecutable && Binding.BehaviorID < SelectedBinding->BehaviorID))
			{
				SelectedBinding = &Binding;
				bSelectedBehaviorIsExecutable = bBindingBehaviorIsExecutable;
			}
		}

		return SelectedBinding;
	};

	if (!CohortName.IsNone())
	{
		if (const Flight::Orchestration::FFlightBehaviorBinding* ExactBinding = FindBinding(CohortName))
		{
			OutBinding = *ExactBinding;
			return true;
		}
	}

	if (CohortName != Flight::Orchestration::DefaultSwarmCohortName)
	{
		if (const Flight::Orchestration::FFlightBehaviorBinding* DefaultBinding = FindBinding(Flight::Orchestration::DefaultSwarmCohortName))
		{
			OutBinding = *DefaultBinding;
			return true;
		}
	}

	return false;
}

TArray<Flight::Orchestration::FFlightBehaviorBinding> UFlightOrchestrationSubsystem::GetBindingsForCohort(const FName CohortName) const
{
	TArray<Flight::Orchestration::FFlightBehaviorBinding> MatchingBindings;
	for (const Flight::Orchestration::FFlightBehaviorBinding& Binding : Bindings)
	{
		if (Binding.CohortName == CohortName)
		{
			MatchingBindings.Add(Binding);
		}
	}

	return MatchingBindings;
}

bool UFlightOrchestrationSubsystem::ResolveFallbackBinding(Flight::Orchestration::FFlightBehaviorBinding& OutBinding) const
{
	if (!Bindings.IsEmpty())
	{
		return false;
	}

	const Flight::Orchestration::FFlightCohortRecord DefaultCohort{ Flight::Orchestration::DefaultSwarmCohortName };
	const Flight::Orchestration::FFlightBehaviorRecord* SelectedBehavior =
		Flight::Orchestration::SelectBehaviorForCohort(DefaultCohort, BehaviorsById);
	if (!SelectedBehavior)
	{
		return false;
	}

	OutBinding = Flight::Orchestration::FFlightBehaviorBinding();
	OutBinding.CohortName = Flight::Orchestration::DefaultSwarmCohortName;
	OutBinding.BehaviorID = SelectedBehavior->BehaviorID;
	OutBinding.ExecutionDomain = SelectedBehavior->ResolvedDomain;
	OutBinding.FrameInterval = SelectedBehavior->FrameInterval;
	OutBinding.bAsync = SelectedBehavior->bAsync;
	OutBinding.RequiredContracts = SelectedBehavior->RequiredContracts;
	return true;
}

void UFlightOrchestrationSubsystem::Rebuild()
{
	RebuildVisibility();
	RebuildExecutionPlan();
}

void UFlightOrchestrationSubsystem::RebuildVisibility()
{
	ResetVisibilityState();
	RefreshServiceStatuses();
	IngestRenderAdapters();
	IngestWaypointPaths();
	IngestSpawnAnchors();
	IngestSpatialFields();
	IngestBehaviors();
	BuildDefaultCohorts();
	BuildMissingContracts();
	RebuildCachedReport();
}

void UFlightOrchestrationSubsystem::RebuildExecutionPlan()
{
	Bindings.Reset();
	ExecutionPlan.Generation += 1;
	ExecutionPlan.BuiltAtUtc = FDateTime::UtcNow();
	ExecutionPlan.Steps.Reset();

	for (const TPair<FName, Flight::Orchestration::FFlightCohortRecord>& Pair : CohortsByName)
	{
		const Flight::Orchestration::FFlightBehaviorRecord* SelectedBehavior =
			Flight::Orchestration::SelectBehaviorForCohort(Pair.Value, BehaviorsById);
		if (!SelectedBehavior)
		{
			continue;
		}

		Flight::Orchestration::FFlightBehaviorBinding Binding;
		Binding.CohortName = Pair.Key;
		Binding.BehaviorID = SelectedBehavior->BehaviorID;
		Binding.ExecutionDomain = SelectedBehavior->ResolvedDomain;
		Binding.FrameInterval = SelectedBehavior->FrameInterval;
		Binding.bAsync = SelectedBehavior->bAsync;
		Binding.RequiredContracts = SelectedBehavior->RequiredContracts;
		Bindings.Add(Binding);

		Flight::Orchestration::FFlightExecutionPlanStep Step;
		Step.CohortName = Pair.Key;
		Step.BehaviorID = SelectedBehavior->BehaviorID;
		Step.ExecutionDomain = SelectedBehavior->ResolvedDomain;
		Step.FrameInterval = SelectedBehavior->FrameInterval;
		Step.bAsync = SelectedBehavior->bAsync;
		Step.InputContracts = SelectedBehavior->RequiredContracts;
		Step.OutputConsumers.Add(TEXT("Mass"));
		Step.OutputConsumers.Add(TEXT("Swarm"));
		if (const UFlightSimpleSCSLShaderPipelineSubsystem* SimpleSCSLSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UFlightSimpleSCSLShaderPipelineSubsystem>()
			: nullptr;
			SimpleSCSLSubsystem && SimpleSCSLSubsystem->IsEnabled())
		{
			Step.OutputConsumers.Add(TEXT("SimpleSCSLShaderPipeline"));
		}
		ExecutionPlan.Steps.Add(Step);
	}

	RebuildCachedReport();
}

FString UFlightOrchestrationSubsystem::BuildReportJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("worldName"), CachedReport.WorldName);
	Root->SetStringField(TEXT("builtAtUtc"), CachedReport.BuiltAtUtc.ToIso8601());

	TArray<TSharedPtr<FJsonValue>> ServiceValues;
	for (const Flight::Orchestration::FFlightServiceStatus& Service : CachedReport.Services)
	{
		TSharedRef<FJsonObject> ServiceObject = MakeShared<FJsonObject>();
		ServiceObject->SetStringField(TEXT("name"), Service.ServiceName.ToString());
		ServiceObject->SetBoolField(TEXT("available"), Service.bAvailable);
		ServiceObject->SetStringField(TEXT("detail"), Service.Detail);
		ServiceValues.Add(MakeShared<FJsonValueObject>(ServiceObject));
	}
	Root->SetArrayField(TEXT("services"), ServiceValues);

	TArray<TSharedPtr<FJsonValue>> ParticipantValues;
	for (const Flight::Orchestration::FFlightParticipantRecord& Participant : CachedReport.Participants)
	{
		TSharedRef<FJsonObject> ParticipantObject = MakeShared<FJsonObject>();
		ParticipantObject->SetNumberField(TEXT("handle"), static_cast<double>(Participant.Handle.Value));
		ParticipantObject->SetStringField(TEXT("kind"), Flight::Orchestration::ParticipantKindToString(Participant.Kind));
		ParticipantObject->SetStringField(TEXT("name"), Participant.Name.ToString());
		ParticipantObject->SetStringField(TEXT("ownerSubsystem"), Participant.OwnerSubsystem.ToString());
		ParticipantObject->SetStringField(TEXT("sourceObjectPath"), Participant.SourceObjectPath);
		Flight::Orchestration::AddNameArrayToJson(Participant.Tags, TEXT("tags"), ParticipantObject);
		Flight::Orchestration::AddNameArrayToJson(Participant.Capabilities, TEXT("capabilities"), ParticipantObject);
		Flight::Orchestration::AddNameArrayToJson(Participant.ContractKeys, TEXT("contractKeys"), ParticipantObject);
		ParticipantValues.Add(MakeShared<FJsonValueObject>(ParticipantObject));
	}
	Root->SetArrayField(TEXT("participants"), ParticipantValues);

	TArray<TSharedPtr<FJsonValue>> CohortValues;
	for (const Flight::Orchestration::FFlightCohortRecord& Cohort : CachedReport.Cohorts)
	{
		TSharedRef<FJsonObject> CohortObject = MakeShared<FJsonObject>();
		CohortObject->SetStringField(TEXT("name"), Cohort.Name.ToString());
		Flight::Orchestration::AddNameArrayToJson(Cohort.Tags, TEXT("tags"), CohortObject);
		CohortObject->SetNumberField(TEXT("preferredBehaviorId"), Cohort.PreferredBehaviorId);

		TArray<TSharedPtr<FJsonValue>> AllowedBehaviorValues;
		for (const uint32 AllowedBehaviorId : Cohort.AllowedBehaviorIds)
		{
			AllowedBehaviorValues.Add(MakeShared<FJsonValueNumber>(static_cast<double>(AllowedBehaviorId)));
		}
		CohortObject->SetArrayField(TEXT("allowedBehaviorIds"), AllowedBehaviorValues);

		TArray<TSharedPtr<FJsonValue>> DeniedBehaviorValues;
		for (const uint32 DeniedBehaviorId : Cohort.DeniedBehaviorIds)
		{
			DeniedBehaviorValues.Add(MakeShared<FJsonValueNumber>(static_cast<double>(DeniedBehaviorId)));
		}
		CohortObject->SetArrayField(TEXT("deniedBehaviorIds"), DeniedBehaviorValues);
		Flight::Orchestration::AddNameArrayToJson(Cohort.RequiredBehaviorContracts, TEXT("requiredBehaviorContracts"), CohortObject);

		TArray<TSharedPtr<FJsonValue>> ParticipantHandleValues;
		for (const Flight::Orchestration::FFlightParticipantHandle Handle : Cohort.Participants)
		{
			ParticipantHandleValues.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Handle.Value)));
		}
		CohortObject->SetArrayField(TEXT("participantHandles"), ParticipantHandleValues);
		CohortValues.Add(MakeShared<FJsonValueObject>(CohortObject));
	}
	Root->SetArrayField(TEXT("cohorts"), CohortValues);

	TArray<TSharedPtr<FJsonValue>> BehaviorValues;
	for (const Flight::Orchestration::FFlightBehaviorRecord& Behavior : CachedReport.Behaviors)
	{
		TSharedRef<FJsonObject> BehaviorObject = MakeShared<FJsonObject>();
		BehaviorObject->SetNumberField(TEXT("behaviorId"), Behavior.BehaviorID);
		BehaviorObject->SetStringField(TEXT("name"), Behavior.Name.ToString());
		BehaviorObject->SetStringField(TEXT("compileState"), Behavior.CompileState);
		BehaviorObject->SetNumberField(TEXT("executionRateHz"), Behavior.ExecutionRateHz);
		BehaviorObject->SetNumberField(TEXT("frameInterval"), Behavior.FrameInterval);
		BehaviorObject->SetBoolField(TEXT("async"), Behavior.bAsync);
		BehaviorObject->SetBoolField(TEXT("executable"), Behavior.bExecutable);
		BehaviorObject->SetStringField(TEXT("resolvedDomain"), Flight::Orchestration::ExecutionDomainToString(Behavior.ResolvedDomain));
		Flight::Orchestration::AddNameArrayToJson(Behavior.RequiredContracts, TEXT("requiredContracts"), BehaviorObject);
		BehaviorObject->SetStringField(TEXT("diagnostics"), Behavior.Diagnostics);
		BehaviorValues.Add(MakeShared<FJsonValueObject>(BehaviorObject));
	}
	Root->SetArrayField(TEXT("behaviors"), BehaviorValues);

	TArray<TSharedPtr<FJsonValue>> BindingValues;
	for (const Flight::Orchestration::FFlightBehaviorBinding& Binding : CachedReport.Bindings)
	{
		TSharedRef<FJsonObject> BindingObject = MakeShared<FJsonObject>();
		BindingObject->SetStringField(TEXT("cohortName"), Binding.CohortName.ToString());
		BindingObject->SetNumberField(TEXT("behaviorId"), Binding.BehaviorID);
		BindingObject->SetStringField(TEXT("executionDomain"), Flight::Orchestration::ExecutionDomainToString(Binding.ExecutionDomain));
		BindingObject->SetNumberField(TEXT("frameInterval"), Binding.FrameInterval);
		BindingObject->SetBoolField(TEXT("async"), Binding.bAsync);
		Flight::Orchestration::AddNameArrayToJson(Binding.RequiredContracts, TEXT("requiredContracts"), BindingObject);
		BindingValues.Add(MakeShared<FJsonValueObject>(BindingObject));
	}
	Root->SetArrayField(TEXT("bindings"), BindingValues);

	TArray<TSharedPtr<FJsonValue>> MissingContractValues;
	for (const Flight::Orchestration::FFlightMissingContract& MissingContract : CachedReport.MissingContracts)
	{
		TSharedRef<FJsonObject> MissingContractObject = MakeShared<FJsonObject>();
		MissingContractObject->SetStringField(TEXT("scope"), MissingContract.Scope.ToString());
		MissingContractObject->SetStringField(TEXT("contractKey"), MissingContract.ContractKey.ToString());
		MissingContractObject->SetStringField(TEXT("issue"), MissingContract.Issue);
		MissingContractValues.Add(MakeShared<FJsonValueObject>(MissingContractObject));
	}
	Root->SetArrayField(TEXT("missingContracts"), MissingContractValues);

	TSharedRef<FJsonObject> PlanObject = MakeShared<FJsonObject>();
	PlanObject->SetNumberField(TEXT("generation"), ExecutionPlan.Generation);
	PlanObject->SetStringField(TEXT("builtAtUtc"), ExecutionPlan.BuiltAtUtc.ToIso8601());

	TArray<TSharedPtr<FJsonValue>> PlanStepValues;
	for (const Flight::Orchestration::FFlightExecutionPlanStep& Step : ExecutionPlan.Steps)
	{
		TSharedRef<FJsonObject> StepObject = MakeShared<FJsonObject>();
		StepObject->SetStringField(TEXT("cohortName"), Step.CohortName.ToString());
		StepObject->SetNumberField(TEXT("behaviorId"), Step.BehaviorID);
		StepObject->SetStringField(TEXT("executionDomain"), Flight::Orchestration::ExecutionDomainToString(Step.ExecutionDomain));
		StepObject->SetNumberField(TEXT("frameInterval"), Step.FrameInterval);
		StepObject->SetBoolField(TEXT("async"), Step.bAsync);
		Flight::Orchestration::AddNameArrayToJson(Step.InputContracts, TEXT("inputContracts"), StepObject);
		Flight::Orchestration::AddNameArrayToJson(Step.OutputConsumers, TEXT("outputConsumers"), StepObject);
		PlanStepValues.Add(MakeShared<FJsonValueObject>(StepObject));
	}
	PlanObject->SetArrayField(TEXT("steps"), PlanStepValues);
	Root->SetObjectField(TEXT("executionPlan"), PlanObject);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

void UFlightOrchestrationSubsystem::LogReport(const bool bVerbose) const
{
	UE_LOG(
		LogFlightProject,
		Log,
		TEXT("Flight.Orchestration: World=%s Services=%d Participants=%d Cohorts=%d Behaviors=%d Bindings=%d MissingContracts=%d PlanSteps=%d"),
		*CachedReport.WorldName,
		CachedReport.Services.Num(),
		CachedReport.Participants.Num(),
		CachedReport.Cohorts.Num(),
		CachedReport.Behaviors.Num(),
		CachedReport.Bindings.Num(),
		CachedReport.MissingContracts.Num(),
		CachedReport.ExecutionPlan.Steps.Num());

	if (!bVerbose)
	{
		return;
	}

	UE_LOG(LogFlightProject, Display, TEXT("%s"), *BuildReportJson());
}

void UFlightOrchestrationSubsystem::ResetVisibilityState()
{
	NextParticipantHandle = 1;
	ParticipantsByHandle.Reset();
	CohortsByName.Reset();
	BehaviorsById.Reset();
	Bindings.Reset();
	Services.Reset();
	MissingContracts.Reset();
	ExecutionPlan = Flight::Orchestration::FFlightExecutionPlan();
	CachedReport = Flight::Orchestration::FFlightOrchestrationReport();
}

void UFlightOrchestrationSubsystem::RefreshServiceStatuses()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AddServiceStatus(TEXT("UFlightWorldBootstrapSubsystem"), World->GetSubsystem<UFlightWorldBootstrapSubsystem>() != nullptr);
	AddServiceStatus(TEXT("UFlightSwarmSubsystem"), World->GetSubsystem<UFlightSwarmSubsystem>() != nullptr);
	AddServiceStatus(TEXT("UFlightSpatialSubsystem"), World->GetSubsystem<UFlightSpatialSubsystem>() != nullptr);
	AddServiceStatus(TEXT("UFlightVerseSubsystem"), World->GetSubsystem<UFlightVerseSubsystem>() != nullptr);
	AddServiceStatus(TEXT("UFlightVexTaskSubsystem"), World->GetSubsystem<UFlightVexTaskSubsystem>() != nullptr);
	AddServiceStatus(TEXT("UFlightWaypointPathRegistry"), World->GetSubsystem<UFlightWaypointPathRegistry>() != nullptr);
	if (const UFlightSimpleSCSLShaderPipelineSubsystem* SimpleSCSLSubsystem = World->GetSubsystem<UFlightSimpleSCSLShaderPipelineSubsystem>())
	{
		AddServiceStatus(
			TEXT("UFlightSimpleSCSLShaderPipelineSubsystem"),
			true,
			SimpleSCSLSubsystem->BuildServiceDetail());
	}
	else
	{
		AddServiceStatus(TEXT("UFlightSimpleSCSLShaderPipelineSubsystem"), false, TEXT("Not initialized"));
	}

	static const TCHAR* SwarmSpawnerClassPath = TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem");
	UClass* SwarmSpawnerClass = FindObject<UClass>(nullptr, SwarmSpawnerClassPath);
	if (SwarmSpawnerClass)
	{
		const bool bSpawnerAvailable = World->GetSubsystemBase(SwarmSpawnerClass) != nullptr;
		AddServiceStatus(TEXT("UFlightSwarmSpawnerSubsystem"), bSpawnerAvailable, TEXT("Resolved via optional plugin class"));
	}
	else
	{
		AddServiceStatus(TEXT("UFlightSwarmSpawnerSubsystem"), false, TEXT("Optional plugin class not loaded"));
	}
}

void UFlightOrchestrationSubsystem::IngestRenderAdapters()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFlightSimpleSCSLShaderPipelineSubsystem* SimpleSCSLSubsystem = World->GetSubsystem<UFlightSimpleSCSLShaderPipelineSubsystem>();
	if (!SimpleSCSLSubsystem)
	{
		return;
	}

	Flight::Orchestration::FFlightParticipantRecord Record;
	Record.Kind = Flight::Orchestration::EFlightParticipantKind::RenderAdapter;
	Record.Name = TEXT("SimpleSCSLShaderPipeline");
	Record.OwnerSubsystem = TEXT("UFlightSimpleSCSLShaderPipelineSubsystem");
	Record.SourceObject = const_cast<UFlightSimpleSCSLShaderPipelineSubsystem*>(SimpleSCSLSubsystem);
	Record.SourceObjectPath = SimpleSCSLSubsystem->GetPathName();
	Record.Tags.Add(TEXT("RuntimeService"));
	Record.Tags.Add(TEXT("Rendering"));
	Record.Tags.Add(SimpleSCSLSubsystem->IsEnabled() ? TEXT("Enabled") : TEXT("Disabled"));
	Record.Capabilities.Add(TEXT("PostProcessTonemap"));
	Record.Capabilities.Add(TEXT("SimpleSCSL"));
	Record.Capabilities.Add(TEXT("WorkflowPreview"));
	Record.ContractKeys.Add(TEXT("SceneColor"));
	RegisterParticipant(Record);
}

void UFlightOrchestrationSubsystem::IngestWaypointPaths()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AFlightWaypointPath> It(World); It; ++It)
	{
		AFlightWaypointPath* Path = *It;
		if (!Path)
		{
			continue;
		}

		Flight::Orchestration::FFlightParticipantRecord Record;
		Record.Kind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
		Record.Name = Path->GetFName();
		Record.OwnerSubsystem = TEXT("UFlightWaypointPathRegistry");
		Record.SourceObject = Path;
		Record.SourceObjectPath = Path->GetPathName();
		Record.Tags.Add(TEXT("WorldActor"));
		if (Path->GetPathId().IsValid())
		{
			Record.Tags.Add(TEXT("Registered"));
		}
		Record.Capabilities.Add(TEXT("SplinePath"));
		RegisterParticipant(Record);
	}
}

void UFlightOrchestrationSubsystem::IngestSpawnAnchors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AFlightSpawnSwarmAnchor> It(World); It; ++It)
	{
		AFlightSpawnSwarmAnchor* Anchor = *It;
		if (!Anchor)
		{
			continue;
		}

		Flight::Orchestration::FFlightParticipantRecord Record;
		Record.Kind = Flight::Orchestration::EFlightParticipantKind::SpawnAnchor;
		Record.Name = Anchor->GetAnchorId().IsNone() ? Anchor->GetFName() : Anchor->GetAnchorId();
		Record.OwnerSubsystem = TEXT("UFlightSwarmSpawnerSubsystem");
		Record.SourceObject = Anchor;
		Record.SourceObjectPath = Anchor->GetPathName();
		Record.Tags.Add(TEXT("WorldActor"));
		Record.Tags.Add(TEXT("Swarm"));
		Record.Capabilities.Add(TEXT("SpawnSwarm"));
		RegisterParticipant(Record);
	}
}

void UFlightOrchestrationSubsystem::IngestSpatialFields()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFlightSpatialSubsystem* SpatialSubsystem = World->GetSubsystem<UFlightSpatialSubsystem>();
	if (!SpatialSubsystem)
	{
		return;
	}

	for (const TPair<FName, TSharedPtr<Flight::Spatial::IFlightSpatialField>>& Pair : SpatialSubsystem->GetFields())
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}

		Flight::Orchestration::FFlightParticipantRecord Record;
		Record.Kind = Flight::Orchestration::EFlightParticipantKind::SpatialField;
		Record.Name = Pair.Key;
		Record.OwnerSubsystem = TEXT("UFlightSpatialSubsystem");
		Record.Tags.Add(TEXT("RuntimeField"));
		Record.Capabilities.Add(*Flight::Orchestration::SpatialFieldTypeToString(Pair.Value->GetFieldType()));
		RegisterParticipant(Record);
	}
}

void UFlightOrchestrationSubsystem::IngestBehaviors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem)
	{
		return;
	}

	for (const TPair<uint32, UFlightVerseSubsystem::FVerseBehavior>& Pair : VerseSubsystem->Behaviors)
	{
		Flight::Orchestration::FFlightBehaviorRecord Record;
		Record.BehaviorID = Pair.Key;
		Record.Name = FName(*FString::Printf(TEXT("Behavior_%u"), Pair.Key));
		Record.CompileState = Flight::Orchestration::VerseCompileStateToString(Pair.Value.CompileState);
		Record.ExecutionRateHz = Pair.Value.ExecutionRateHz;
		Record.FrameInterval = Pair.Value.FrameInterval;
		Record.bAsync = Pair.Value.bIsAsync;
		Record.bExecutable = Pair.Value.bHasExecutableProcedure || Pair.Value.bUsesNativeFallback || Pair.Value.SimdPlan.IsValid();
		Record.ResolvedDomain = Flight::Orchestration::ResolveExecutionDomain(Pair.Value);
		Record.Diagnostics = Pair.Value.LastCompileDiagnostics;
		RegisterBehavior(Pair.Key, Record);
	}

	if (!VerseSubsystem->Behaviors.IsEmpty())
	{
		Flight::Orchestration::FFlightParticipantRecord ProviderRecord;
		ProviderRecord.Kind = Flight::Orchestration::EFlightParticipantKind::BehaviorProvider;
		ProviderRecord.Name = TEXT("UFlightVerseSubsystem");
		ProviderRecord.OwnerSubsystem = TEXT("UFlightVerseSubsystem");
		ProviderRecord.Tags.Add(TEXT("RuntimeService"));
		ProviderRecord.Capabilities.Add(TEXT("BehaviorCompilation"));
		ProviderRecord.Capabilities.Add(TEXT("BehaviorExecution"));
		RegisterParticipant(ProviderRecord);
	}
}

void UFlightOrchestrationSubsystem::BuildDefaultCohorts()
{
	TArray<Flight::Orchestration::FFlightParticipantHandle> SwarmParticipants;

	for (const TPair<uint64, Flight::Orchestration::FFlightParticipantRecord>& Pair : ParticipantsByHandle)
	{
		const Flight::Orchestration::FFlightParticipantRecord& Participant = Pair.Value;
		if (Participant.Kind == Flight::Orchestration::EFlightParticipantKind::SpawnAnchor
			|| Participant.Kind == Flight::Orchestration::EFlightParticipantKind::WaypointPath)
		{
			SwarmParticipants.Add(Participant.Handle);
		}

		if (Participant.Kind == Flight::Orchestration::EFlightParticipantKind::SpawnAnchor)
		{
			Flight::Orchestration::FFlightCohortRecord Cohort;
			Cohort.Name = FName(*FString::Printf(TEXT("SwarmAnchor.%s"), *Participant.Name.ToString()));
			Cohort.Participants.Add(Participant.Handle);
			Cohort.Tags.Add(TEXT("Swarm"));
			Cohort.Tags.Add(TEXT("AnchorScoped"));
			if (const AFlightSpawnSwarmAnchor* Anchor = Cast<AFlightSpawnSwarmAnchor>(Participant.SourceObject.Get()))
			{
				Cohort.PreferredBehaviorId = Anchor->GetPreferredBehaviorId();
				for (const int32 AllowedBehaviorId : Anchor->GetAllowedBehaviorIds())
				{
					if (AllowedBehaviorId >= 0)
					{
						Cohort.AllowedBehaviorIds.Add(static_cast<uint32>(AllowedBehaviorId));
					}
				}
				for (const int32 DeniedBehaviorId : Anchor->GetDeniedBehaviorIds())
				{
					if (DeniedBehaviorId >= 0)
					{
						Cohort.DeniedBehaviorIds.Add(static_cast<uint32>(DeniedBehaviorId));
					}
				}
				Cohort.RequiredBehaviorContracts = Anchor->GetRequiredBehaviorContracts();
			}
			RegisterCohort(Cohort);
		}
	}

	if (!SwarmParticipants.IsEmpty())
	{
		Flight::Orchestration::FFlightCohortRecord Cohort;
		Cohort.Name = TEXT("Swarm.Default");
		Cohort.Participants = MoveTemp(SwarmParticipants);
		Cohort.Tags.Add(TEXT("Swarm"));
		Cohort.Tags.Add(TEXT("Default"));
		RegisterCohort(Cohort);
	}
}

void UFlightOrchestrationSubsystem::BuildMissingContracts()
{
	const bool bHasSwarmParticipants = HasParticipantOfKind(Flight::Orchestration::EFlightParticipantKind::SpawnAnchor);
	const bool bHasWaypointPaths = HasParticipantOfKind(Flight::Orchestration::EFlightParticipantKind::WaypointPath);

	if (bHasSwarmParticipants && !bHasWaypointPaths)
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Swarm");
		MissingContract.ContractKey = TEXT("WaypointPath");
		MissingContract.Issue = TEXT("Swarm anchors are visible, but no waypoint path is available.");
		MissingContracts.Add(MissingContract);
	}

	if (!CohortsByName.IsEmpty() && BehaviorsById.IsEmpty())
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Orchestration");
		MissingContract.ContractKey = TEXT("BehaviorBinding");
		MissingContract.Issue = TEXT("Visible cohorts exist, but no compiled behaviors are registered.");
		MissingContracts.Add(MissingContract);
	}

	if (bHasSwarmParticipants && !IsServiceAvailable(TEXT("UFlightSwarmSpawnerSubsystem")))
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Swarm");
		MissingContract.ContractKey = TEXT("SwarmSpawnerSubsystem");
		MissingContract.Issue = TEXT("Swarm anchors are visible, but the optional SwarmEncounter spawner subsystem is unavailable.");
		MissingContracts.Add(MissingContract);
	}
}

void UFlightOrchestrationSubsystem::RebuildCachedReport()
{
	CachedReport = Flight::Orchestration::FFlightOrchestrationReport();
	CachedReport.WorldName = GetWorld() ? GetWorld()->GetName() : FString();
	CachedReport.BuiltAtUtc = FDateTime::UtcNow();
	CachedReport.Services = Services;
	CachedReport.MissingContracts = MissingContracts;
	CachedReport.ExecutionPlan = ExecutionPlan;

	ParticipantsByHandle.GenerateValueArray(CachedReport.Participants);
	CohortsByName.GenerateValueArray(CachedReport.Cohorts);
	BehaviorsById.GenerateValueArray(CachedReport.Behaviors);
	CachedReport.Bindings = Bindings;

	CachedReport.Participants.Sort([](const Flight::Orchestration::FFlightParticipantRecord& Left, const Flight::Orchestration::FFlightParticipantRecord& Right)
	{
		return Left.Name.LexicalLess(Right.Name);
	});

	CachedReport.Cohorts.Sort([](const Flight::Orchestration::FFlightCohortRecord& Left, const Flight::Orchestration::FFlightCohortRecord& Right)
	{
		return Left.Name.LexicalLess(Right.Name);
	});

	CachedReport.Behaviors.Sort([](const Flight::Orchestration::FFlightBehaviorRecord& Left, const Flight::Orchestration::FFlightBehaviorRecord& Right)
	{
		return Left.BehaviorID < Right.BehaviorID;
	});

	CachedReport.Bindings.Sort([](const Flight::Orchestration::FFlightBehaviorBinding& Left, const Flight::Orchestration::FFlightBehaviorBinding& Right)
	{
		if (Left.CohortName == Right.CohortName)
		{
			return Left.BehaviorID < Right.BehaviorID;
		}

		return Left.CohortName.LexicalLess(Right.CohortName);
	});
}

void UFlightOrchestrationSubsystem::AddServiceStatus(const FName ServiceName, const bool bAvailable, FString Detail)
{
	Flight::Orchestration::FFlightServiceStatus Status;
	Status.ServiceName = ServiceName;
	Status.bAvailable = bAvailable;
	Status.Detail = MoveTemp(Detail);
	Services.Add(MoveTemp(Status));
}

bool UFlightOrchestrationSubsystem::HasParticipantOfKind(const Flight::Orchestration::EFlightParticipantKind Kind) const
{
	for (const TPair<uint64, Flight::Orchestration::FFlightParticipantRecord>& Pair : ParticipantsByHandle)
	{
		if (Pair.Value.Kind == Kind)
		{
			return true;
		}
	}

	return false;
}
