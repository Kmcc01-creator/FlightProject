// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Orchestration/FlightBehaviorBinding.h"

namespace Flight::Orchestration
{

struct FLIGHTPROJECT_API FFlightExecutionPlanStep
{
	FName CohortName = NAME_None;
	uint32 BehaviorID = 0;
	EFlightExecutionDomain ExecutionDomain = EFlightExecutionDomain::Unknown;
	uint32 FrameInterval = 1;
	bool bAsync = false;
	FGuid NavigationCandidateId;
	FName NavigationCandidateName = NAME_None;
	float NavigationCandidateScore = 0.0f;
	int32 NavigationCandidateRankOrder = INDEX_NONE;
	FString NavigationSelectionReason;
	TArray<FName> InputContracts;
	TArray<FName> OutputConsumers;
};

struct FLIGHTPROJECT_API FFlightExecutionPlan
{
	uint32 Generation = 0;
	FDateTime BuiltAtUtc;
	TArray<FFlightExecutionPlanStep> Steps;
};

} // namespace Flight::Orchestration
