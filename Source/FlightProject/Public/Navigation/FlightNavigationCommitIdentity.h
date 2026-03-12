// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"

#include "FlightNavigationCommitIdentity.generated.h"

namespace Flight::Orchestration
{
	enum class EFlightParticipantKind : uint8;
}

namespace Flight::Navigation
{

enum class EFlightNavigationCommitProductKind : uint8
{
	None,
	WaypointPath,
	FallbackWaypointPath,
	SyntheticNodeOrbit,
	SyntheticEdgePolyline
};

}

USTRUCT()
struct FLIGHTPROJECT_API FFlightNavigationCommitIdentity
{
	GENERATED_BODY()
	FLIGHT_REFLECT_BODY(FFlightNavigationCommitIdentity);

	UPROPERTY()
	FGuid SourceCandidateId;

	UPROPERTY()
	FName CohortName = NAME_None;

	UPROPERTY()
	FGuid RuntimePathId;

	UPROPERTY()
	uint8 CommitKind = 0;

	UPROPERTY()
	uint8 SourceKind = 0;

	bool IsValid() const
	{
		return RuntimePathId.IsValid();
	}

	Flight::Navigation::EFlightNavigationCommitProductKind GetCommitKind() const
	{
		return static_cast<Flight::Navigation::EFlightNavigationCommitProductKind>(CommitKind);
	}

	void SetCommitKind(const Flight::Navigation::EFlightNavigationCommitProductKind InCommitKind)
	{
		CommitKind = static_cast<uint8>(InCommitKind);
	}

	Flight::Orchestration::EFlightParticipantKind GetSourceKind() const
	{
		return static_cast<Flight::Orchestration::EFlightParticipantKind>(SourceKind);
	}

	void SetSourceKind(const Flight::Orchestration::EFlightParticipantKind InSourceKind)
	{
		SourceKind = static_cast<uint8>(InSourceKind);
	}
};

FLIGHT_REFLECT_FIELDS_ATTR(FFlightNavigationCommitIdentity,
	FLIGHT_FIELD_ATTR(FGuid, SourceCandidateId),
	FLIGHT_FIELD_ATTR(FName, CohortName),
	FLIGHT_FIELD_ATTR(FGuid, RuntimePathId),
	FLIGHT_FIELD_ATTR(uint8, CommitKind),
	FLIGHT_FIELD_ATTR(uint8, SourceKind)
)
