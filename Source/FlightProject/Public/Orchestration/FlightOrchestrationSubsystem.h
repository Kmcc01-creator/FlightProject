// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Orchestration/FlightOrchestrationReport.h"
#include "FlightOrchestrationSubsystem.generated.h"

UCLASS()
class FLIGHTPROJECT_API UFlightOrchestrationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	Flight::Orchestration::FFlightParticipantHandle RegisterParticipant(const Flight::Orchestration::FFlightParticipantRecord& Record);
	void UnregisterParticipant(Flight::Orchestration::FFlightParticipantHandle Handle);

	void RegisterBehavior(uint32 BehaviorID, const Flight::Orchestration::FFlightBehaviorRecord& Record);
	void UnregisterBehavior(uint32 BehaviorID);

	bool RegisterCohort(const Flight::Orchestration::FFlightCohortRecord& Cohort);
	void UnregisterCohort(FName CohortName);

	bool BindBehaviorToCohort(const Flight::Orchestration::FFlightBehaviorBinding& Binding);
	void ClearBindingsForCohort(FName CohortName);

	const Flight::Orchestration::FFlightExecutionPlan& GetExecutionPlan() const;
	const Flight::Orchestration::FFlightOrchestrationReport& GetReport() const;

	bool IsServiceAvailable(FName ServiceName) const;
	const Flight::Orchestration::FFlightParticipantRecord* FindParticipant(Flight::Orchestration::FFlightParticipantHandle Handle) const;
	const Flight::Orchestration::FFlightCohortRecord* FindCohort(FName CohortName) const;
	bool TryGetBindingForCohort(FName CohortName, Flight::Orchestration::FFlightBehaviorBinding& OutBinding) const;
	TArray<Flight::Orchestration::FFlightBehaviorBinding> GetBindingsForCohort(FName CohortName) const;
	bool ResolveFallbackBinding(Flight::Orchestration::FFlightBehaviorBinding& OutBinding) const;

	void Rebuild();
	void RebuildVisibility();
	void RebuildExecutionPlan();

	FString BuildReportJson() const;
	void LogReport(bool bVerbose = false) const;

private:
	void ResetVisibilityState();
	void RefreshServiceStatuses();
	void IngestRenderAdapters();
	void IngestWaypointPaths();
	void IngestSpawnAnchors();
	void IngestSpatialFields();
	void IngestBehaviors();
	void BuildDefaultCohorts();
	void BuildMissingContracts();
	void RebuildCachedReport();

	void AddServiceStatus(FName ServiceName, bool bAvailable, FString Detail = FString());
	bool HasParticipantOfKind(Flight::Orchestration::EFlightParticipantKind Kind) const;

	uint64 NextParticipantHandle = 1;
	TMap<uint64, Flight::Orchestration::FFlightParticipantRecord> ParticipantsByHandle;
	TMap<FName, Flight::Orchestration::FFlightCohortRecord> CohortsByName;
	TMap<uint32, Flight::Orchestration::FFlightBehaviorRecord> BehaviorsById;
	TArray<Flight::Orchestration::FFlightBehaviorBinding> Bindings;
	TArray<Flight::Orchestration::FFlightServiceStatus> Services;
	TArray<Flight::Orchestration::FFlightMissingContract> MissingContracts;
	Flight::Orchestration::FFlightExecutionPlan ExecutionPlan;
	Flight::Orchestration::FFlightOrchestrationReport CachedReport;
};
