// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Navigation/FlightNavigationCommitIdentity.h"

#include "FlightNavigationCommitReport.generated.h"

USTRUCT()
struct FLIGHTPROJECT_API FFlightNavigationCommitReport
{
	GENERATED_BODY()
	FLIGHT_REFLECT_BODY(FFlightNavigationCommitReport);

	UPROPERTY()
	FFlightNavigationCommitIdentity Identity;

	UPROPERTY()
	FName SourceCandidateName = NAME_None;

	UPROPERTY()
	FVector InitialLocation = FVector::ZeroVector;

	UPROPERTY()
	float PathLength = 0.0f;

	UPROPERTY()
	bool bSynthetic = false;

	UPROPERTY()
	bool bResolvedFromExecutionPlan = false;
};

FLIGHT_REFLECT_FIELDS_ATTR(FFlightNavigationCommitReport,
	FLIGHT_FIELD_ATTR(FFlightNavigationCommitIdentity, Identity),
	FLIGHT_FIELD_ATTR(FName, SourceCandidateName),
	FLIGHT_FIELD_ATTR(FVector, InitialLocation),
	FLIGHT_FIELD_ATTR(float, PathLength),
	FLIGHT_FIELD_ATTR(bool, bSynthetic),
	FLIGHT_FIELD_ATTR(bool, bResolvedFromExecutionPlan)
)
