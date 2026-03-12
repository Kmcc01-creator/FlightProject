// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/FlightNavigationCommitIdentity.h"
#include "Navigation/FlightNavigationCommitReport.h"

class AFlightWaypointPath;
struct FFlightNavigationCommitSharedFragment;
class UFlightWaypointPathRegistry;
class UWorld;

namespace Flight::Orchestration
{
	enum class EFlightParticipantKind : uint8;
	struct FFlightExecutionPlanStep;
	struct FFlightNavigationCandidateRecord;
	struct FFlightOrchestrationReport;
}

namespace Flight::Navigation
{

struct FLIGHTPROJECT_API FFlightNavigationCommitResolverContext
{
	TMap<FGuid, AFlightWaypointPath*> PathsById;
	TMap<FName, AFlightWaypointPath*> PathsByName;

	void BuildFromWorld(UWorld& World);
};

struct FLIGHTPROJECT_API FFlightNavigationCommitProduct
{
	FFlightNavigationCommitIdentity Identity;
	FFlightNavigationCommitReport Report;
	AFlightWaypointPath* Path = nullptr;
	float PathLength = 0.0f;
	FVector InitialLocation = FVector::ZeroVector;

	bool IsValid() const
	{
		return Identity.IsValid();
	}

	EFlightNavigationCommitProductKind GetKind() const
	{
		return Identity.GetCommitKind();
	}

	Flight::Orchestration::EFlightParticipantKind GetSourceKind() const
	{
		return Identity.GetSourceKind();
	}

	void WriteSharedFragment(FFlightNavigationCommitSharedFragment& OutFragment) const;
};

FLIGHTPROJECT_API const Flight::Orchestration::FFlightNavigationCandidateRecord* FindNavigationCandidateRecord(
	const Flight::Orchestration::FFlightOrchestrationReport& Report,
	const Flight::Orchestration::FFlightExecutionPlanStep& Step);

FLIGHTPROJECT_API FFlightNavigationCommitProduct ResolveNavigationCommitProductForStep(
	const Flight::Orchestration::FFlightExecutionPlanStep& Step,
	const Flight::Orchestration::FFlightOrchestrationReport& Report,
	const FFlightNavigationCommitResolverContext& Context,
	UFlightWaypointPathRegistry* PathRegistry,
	AFlightWaypointPath* DefaultPath);

} // namespace Flight::Navigation
