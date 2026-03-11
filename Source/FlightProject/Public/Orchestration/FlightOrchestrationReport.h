// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Orchestration/FlightBehaviorBinding.h"
#include "Orchestration/FlightExecutionPlan.h"
#include "Orchestration/FlightParticipantTypes.h"

namespace Flight::Orchestration
{

struct FLIGHTPROJECT_API FFlightServiceStatus
{
	FName ServiceName = NAME_None;
	bool bAvailable = false;
	FString Detail;
};

struct FLIGHTPROJECT_API FFlightMissingContract
{
	FName Scope = NAME_None;
	FName ContractKey = NAME_None;
	FString Issue;
};

struct FLIGHTPROJECT_API FFlightOrchestrationDiagnostic
{
	FName Severity = NAME_None;
	FName Category = NAME_None;
	FName SourceName = NAME_None;
	FString Message;
};

struct FLIGHTPROJECT_API FFlightNavigationCandidateRecord
{
	EFlightParticipantKind SourceKind = EFlightParticipantKind::Unknown;
	FName Name = NAME_None;
	FGuid SourceId;
	FName OwnerSubsystem = NAME_None;
	FName NetworkId = NAME_None;
	FName SubNetworkId = NAME_None;
	FVector StartLocation = FVector::ZeroVector;
	FVector EndLocation = FVector::ZeroVector;
	float EstimatedCost = 0.0f;
	bool bLegal = true;
	FName LegalityReason = NAME_None;
	float RankScore = 0.0f;
	int32 RankOrder = INDEX_NONE;
	bool bBidirectional = false;
	FString Status = TEXT("Visible");
	FString RoutingValidationStatus = TEXT("NotApplicable");
	FName SuggestedNetworkId = NAME_None;
	FName SuggestedSubNetworkId = NAME_None;
	FString RoutingValidationDetail;
	TArray<FName> Tags;
	TArray<FName> ContractKeys;
};

struct FLIGHTPROJECT_API FFlightOrchestrationReport
{
	FString WorldName;
	FDateTime BuiltAtUtc;
	TArray<FFlightServiceStatus> Services;
	TArray<FFlightParticipantRecord> Participants;
	TArray<FFlightNavigationCandidateRecord> NavigationCandidates;
	TArray<FFlightOrchestrationDiagnostic> Diagnostics;
	TArray<FFlightCohortRecord> Cohorts;
	TArray<FFlightBehaviorRecord> Behaviors;
	TArray<FFlightBehaviorBinding> Bindings;
	TArray<FFlightMissingContract> MissingContracts;
	FFlightExecutionPlan ExecutionPlan;
};

} // namespace Flight::Orchestration
