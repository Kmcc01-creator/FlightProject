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

struct FLIGHTPROJECT_API FFlightOrchestrationReport
{
	FString WorldName;
	FDateTime BuiltAtUtc;
	TArray<FFlightServiceStatus> Services;
	TArray<FFlightParticipantRecord> Participants;
	TArray<FFlightCohortRecord> Cohorts;
	TArray<FFlightBehaviorRecord> Behaviors;
	TArray<FFlightBehaviorBinding> Bindings;
	TArray<FFlightMissingContract> MissingContracts;
	FFlightExecutionPlan ExecutionPlan;
};

} // namespace Flight::Orchestration

