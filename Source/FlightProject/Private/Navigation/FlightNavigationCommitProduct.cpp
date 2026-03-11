// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Navigation/FlightNavigationCommitProduct.h"

#include "FlightWaypointPath.h"
#include "Mass/FlightMassFragments.h"
#include "Mass/FlightWaypointPathRegistry.h"
#include "Orchestration/FlightExecutionPlan.h"
#include "Orchestration/FlightOrchestrationReport.h"

#include "Engine/World.h"
#include "EngineUtils.h"

namespace Flight::Navigation
{

namespace
{

FFlightNavigationCommitProduct MakeResolvedWaypointPathProduct(
	AFlightWaypointPath* Path,
	const bool bResolvedFromExecutionPlan,
	const bool bFallbackPath,
	const Flight::Orchestration::FFlightExecutionPlanStep& Step)
{
	FFlightNavigationCommitProduct Product;
	Product.Kind = bFallbackPath
		? EFlightNavigationCommitProductKind::FallbackWaypointPath
		: EFlightNavigationCommitProductKind::WaypointPath;
	Product.SourceKind = Flight::Orchestration::EFlightParticipantKind::WaypointPath;
	Product.CohortName = Step.CohortName;
	Product.SourceCandidateId = Step.NavigationCandidateId;
	Product.SourceCandidateName = Step.NavigationCandidateName;
	Product.Path = Path;
	Product.bResolvedFromExecutionPlan = bResolvedFromExecutionPlan;
	Product.bSynthetic = false;

	if (Path)
	{
		Product.PathId = Path->EnsureRegisteredPath();
		Product.PathLength = Path->GetPathLength();
		Product.InitialLocation = Path->GetActorLocation();
	}

	return Product;
}

bool BuildSyntheticRoutePointsForCandidate(
	const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate,
	TArray<FVector>& OutControlPoints,
	EFlightNavigationCommitProductKind& OutKind)
{
	OutControlPoints.Reset();
	OutKind = EFlightNavigationCommitProductKind::None;

	switch (Candidate.SourceKind)
	{
	case Flight::Orchestration::EFlightParticipantKind::NavigationNode:
	{
		constexpr float OrbitRadiusCm = 600.0f;
		const FVector Center = Candidate.StartLocation;
		OutControlPoints = {
			Center + FVector(OrbitRadiusCm, 0.0f, 0.0f),
			Center + FVector(0.0f, OrbitRadiusCm, 0.0f),
			Center + FVector(-OrbitRadiusCm, 0.0f, 0.0f),
			Center + FVector(0.0f, -OrbitRadiusCm, 0.0f),
			Center + FVector(OrbitRadiusCm, 0.0f, 0.0f)
		};
		OutKind = EFlightNavigationCommitProductKind::SyntheticNodeOrbit;
		return true;
	}

	case Flight::Orchestration::EFlightParticipantKind::NavigationEdge:
		if (Candidate.StartLocation == Candidate.EndLocation)
		{
			return false;
		}

		OutControlPoints = { Candidate.StartLocation, Candidate.EndLocation };
		OutKind = EFlightNavigationCommitProductKind::SyntheticEdgePolyline;
		return true;

	default:
		return false;
	}
}

FFlightNavigationCommitProduct MakeSyntheticCommitProduct(
	UFlightWaypointPathRegistry& PathRegistry,
	const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate,
	const Flight::Orchestration::FFlightExecutionPlanStep& Step)
{
	FFlightNavigationCommitProduct Product;
	Product.SourceKind = Candidate.SourceKind;
	Product.CohortName = Step.CohortName;
	Product.SourceCandidateId = Candidate.SourceId;
	Product.SourceCandidateName = Candidate.Name;
	Product.bSynthetic = true;
	Product.bResolvedFromExecutionPlan = true;

	TArray<FVector> ControlPoints;
	EFlightNavigationCommitProductKind Kind = EFlightNavigationCommitProductKind::None;
	if (!BuildSyntheticRoutePointsForCandidate(Candidate, ControlPoints, Kind))
	{
		return Product;
	}

	Product.Kind = Kind;
	Product.InitialLocation = ControlPoints[0];
	Product.PathId = PathRegistry.RegisterSyntheticPath(
		ControlPoints,
		Kind == EFlightNavigationCommitProductKind::SyntheticNodeOrbit,
		100.0f,
		Candidate.SourceId);
	Product.PathLength = Candidate.EstimatedCost;

	if (const FFlightPathData* PathData = PathRegistry.FindPath(Product.PathId))
	{
		Product.PathLength = PathData->TotalLength;
	}

	return Product;
}

} // namespace

void FFlightNavigationCommitResolverContext::BuildFromWorld(UWorld& World)
{
	PathsById.Reset();
	PathsByName.Reset();

	for (TActorIterator<AFlightWaypointPath> It(&World); It; ++It)
	{
		AFlightWaypointPath* Path = *It;
		if (!Path)
		{
			continue;
		}

		if (Path->GetPathId().IsValid())
		{
			PathsById.Add(Path->GetPathId(), Path);
		}

		PathsByName.Add(Path->GetFName(), Path);
	}
}

void FFlightNavigationCommitProduct::WriteSharedFragment(FFlightNavigationCommitSharedFragment& OutFragment) const
{
	OutFragment.SourceCandidateId = SourceCandidateId;
	OutFragment.SourceCandidateName = SourceCandidateName;
	OutFragment.CohortName = CohortName;
	OutFragment.RuntimePathId = PathId;
	OutFragment.InitialLocation = InitialLocation;
	OutFragment.PathLength = PathLength;
	OutFragment.CommitKind = static_cast<uint8>(Kind);
	OutFragment.SourceKind = static_cast<uint8>(SourceKind);
	OutFragment.bSynthetic = bSynthetic;
	OutFragment.bResolvedFromExecutionPlan = bResolvedFromExecutionPlan;
}

const Flight::Orchestration::FFlightNavigationCandidateRecord* FindNavigationCandidateRecord(
	const Flight::Orchestration::FFlightOrchestrationReport& Report,
	const Flight::Orchestration::FFlightExecutionPlanStep& Step)
{
	return Report.NavigationCandidates.FindByPredicate(
		[&Step](const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate)
		{
			if (Step.NavigationCandidateId.IsValid() && Candidate.SourceId == Step.NavigationCandidateId)
			{
				return true;
			}

			return !Step.NavigationCandidateName.IsNone() && Candidate.Name == Step.NavigationCandidateName;
		});
}

FFlightNavigationCommitProduct ResolveNavigationCommitProductForStep(
	const Flight::Orchestration::FFlightExecutionPlanStep& Step,
	const Flight::Orchestration::FFlightOrchestrationReport& Report,
	const FFlightNavigationCommitResolverContext& Context,
	UFlightWaypointPathRegistry* PathRegistry,
	AFlightWaypointPath* DefaultPath)
{
	if (Step.NavigationCandidateId.IsValid())
	{
		if (AFlightWaypointPath* const* SelectedPath = Context.PathsById.Find(Step.NavigationCandidateId))
		{
			return MakeResolvedWaypointPathProduct(*SelectedPath, true, false, Step);
		}
	}

	if (!Step.NavigationCandidateName.IsNone())
	{
		if (AFlightWaypointPath* const* SelectedPath = Context.PathsByName.Find(Step.NavigationCandidateName))
		{
			return MakeResolvedWaypointPathProduct(*SelectedPath, true, false, Step);
		}
	}

	if (PathRegistry)
	{
		if (const Flight::Orchestration::FFlightNavigationCandidateRecord* Candidate =
			FindNavigationCandidateRecord(Report, Step))
		{
			FFlightNavigationCommitProduct Product = MakeSyntheticCommitProduct(*PathRegistry, *Candidate, Step);
			if (Product.IsValid())
			{
				return Product;
			}
		}
	}

	return MakeResolvedWaypointPathProduct(DefaultPath, false, true, Step);
}

} // namespace Flight::Navigation
