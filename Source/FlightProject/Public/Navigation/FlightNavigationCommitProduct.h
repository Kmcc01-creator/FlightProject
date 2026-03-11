// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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

enum class EFlightNavigationCommitProductKind : uint8
{
	None,
	WaypointPath,
	FallbackWaypointPath,
	SyntheticNodeOrbit,
	SyntheticEdgePolyline
};

struct FLIGHTPROJECT_API FFlightNavigationCommitResolverContext
{
	TMap<FGuid, AFlightWaypointPath*> PathsById;
	TMap<FName, AFlightWaypointPath*> PathsByName;

	void BuildFromWorld(UWorld& World);
};

struct FLIGHTPROJECT_API FFlightNavigationCommitProduct
{
	EFlightNavigationCommitProductKind Kind = EFlightNavigationCommitProductKind::None;
	Flight::Orchestration::EFlightParticipantKind SourceKind;
	FName CohortName = NAME_None;
	FName SourceCandidateName = NAME_None;
	FGuid SourceCandidateId;
	AFlightWaypointPath* Path = nullptr;
	FGuid PathId;
	float PathLength = 0.0f;
	FVector InitialLocation = FVector::ZeroVector;
	bool bSynthetic = false;
	bool bResolvedFromExecutionPlan = false;

	bool IsValid() const
	{
		return PathId.IsValid();
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
