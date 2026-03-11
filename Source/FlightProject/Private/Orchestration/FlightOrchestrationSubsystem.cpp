// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Orchestration/FlightOrchestrationSubsystem.h"

#include "FlightProject.h"
#include "FlightNavGraphDataHubSubsystem.h"
#include "FlightSpawnSwarmAnchor.h"
#include "FlightWaypointPath.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "Core/FlightSpatialField.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Mass/FlightWaypointPathRegistry.h"
#include "Navigation/FlightNavigationContracts.h"
#include "Orchestration/FlightBehaviorBinding.h"
#include "Orchestration/FlightExecutionPlan.h"
#include "Orchestration/FlightParticipantAdapter.h"
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

template <typename ValueType>
bool AreEquivalentUnordered(const TArray<ValueType>& Left, const TArray<ValueType>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}

	TArray<ValueType> Remaining = Right;
	for (const ValueType& Value : Left)
	{
		const int32 MatchIndex = Remaining.IndexOfByKey(Value);
		if (MatchIndex == INDEX_NONE)
		{
			return false;
		}

		Remaining.RemoveAtSwap(MatchIndex, 1, EAllowShrinking::No);
	}

	return Remaining.IsEmpty();
}

bool BuildCohortReconciliationMismatch(
	const Flight::Orchestration::FFlightCohortRecord& Canonical,
	const Flight::Orchestration::FFlightCohortRecord& Proposed,
	FString& OutMismatch)
{
	if (Canonical.DesiredNavigationNetwork != Proposed.DesiredNavigationNetwork)
	{
		OutMismatch = FString::Printf(
			TEXT("DesiredNavigationNetwork mismatch: canonical='%s' proposed='%s'"),
			*Canonical.DesiredNavigationNetwork.ToString(),
			*Proposed.DesiredNavigationNetwork.ToString());
		return true;
	}

	if (Canonical.DesiredNavigationSubNetwork != Proposed.DesiredNavigationSubNetwork)
	{
		OutMismatch = FString::Printf(
			TEXT("DesiredNavigationSubNetwork mismatch: canonical='%s' proposed='%s'"),
			*Canonical.DesiredNavigationSubNetwork.ToString(),
			*Proposed.DesiredNavigationSubNetwork.ToString());
		return true;
	}

	if (Canonical.PreferredBehaviorId != Proposed.PreferredBehaviorId)
	{
		OutMismatch = FString::Printf(
			TEXT("PreferredBehaviorId mismatch: canonical=%d proposed=%d"),
			Canonical.PreferredBehaviorId,
			Proposed.PreferredBehaviorId);
		return true;
	}

	if (!AreEquivalentUnordered(Canonical.AllowedBehaviorIds, Proposed.AllowedBehaviorIds))
	{
		OutMismatch = TEXT("AllowedBehaviorIds mismatch between canonical cohort and proposed batch plan.");
		return true;
	}

	if (!AreEquivalentUnordered(Canonical.DeniedBehaviorIds, Proposed.DeniedBehaviorIds))
	{
		OutMismatch = TEXT("DeniedBehaviorIds mismatch between canonical cohort and proposed batch plan.");
		return true;
	}

	if (!AreEquivalentUnordered(Canonical.RequiredBehaviorContracts, Proposed.RequiredBehaviorContracts))
	{
		OutMismatch = TEXT("RequiredBehaviorContracts mismatch between canonical cohort and proposed batch plan.");
		return true;
	}

	if (!AreEquivalentUnordered(Canonical.RequiredNavigationContracts, Proposed.RequiredNavigationContracts))
	{
		OutMismatch = TEXT("RequiredNavigationContracts mismatch between canonical cohort and proposed batch plan.");
		return true;
	}

	return false;
}

bool AreCohortProposalsEquivalent(
	const Flight::Orchestration::FFlightCohortRecord& Left,
	const Flight::Orchestration::FFlightCohortRecord& Right)
{
	FString Mismatch;
	return !BuildCohortReconciliationMismatch(Left, Right, Mismatch);
}

void ApplyReconciledBatchCohorts(
	TMap<FName, Flight::Orchestration::FFlightCohortRecord>& CohortsByName,
	const TMap<FName, Flight::Orchestration::FFlightCohortRecord>& ReconciledBatchCohortsByName)
{
	for (const TPair<FName, Flight::Orchestration::FFlightCohortRecord>& Pair : ReconciledBatchCohortsByName)
	{
		if (!CohortsByName.Contains(Pair.Key))
		{
			CohortsByName.Add(Pair.Key, Pair.Value);
		}
	}
}

bool RegisterParticipantAdapter(
	UFlightOrchestrationSubsystem& Orchestration,
	UObject* SourceObject)
{
	if (!SourceObject)
	{
		return false;
	}

	IFlightParticipantAdapter* Adapter = Cast<IFlightParticipantAdapter>(SourceObject);
	if (!Adapter)
	{
		return false;
	}

	Flight::Orchestration::FFlightParticipantRecord Record;
	if (!Adapter->BuildParticipantRecord(Record))
	{
		return false;
	}

	Orchestration.RegisterParticipant(Record);
	return true;
}

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
	case EFlightParticipantKind::NavigationNode:
		return TEXT("NavigationNode");
	case EFlightParticipantKind::NavigationEdge:
		return TEXT("NavigationEdge");
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
	for (const Flight::Vex::FVexStatementAst& Statement : Behavior.NativeProgram.Statements)
	{
		if (Statement.Kind == Flight::Vex::EVexStatementKind::TargetDirective && Statement.SourceSpan == TEXT("@gpu"))
		{
			return EFlightExecutionDomain::Gpu;
		}
	}

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

void AddStringArrayToJson(const TArray<FString>& Strings, const TCHAR* FieldName, TSharedRef<FJsonObject> Object)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Strings.Num());
	for (const FString& Value : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(Value));
	}
	Object->SetArrayField(FieldName, Values);
}

void AddVectorToJson(const FVector& Value, const TCHAR* FieldName, TSharedRef<FJsonObject> Object)
{
	TSharedRef<FJsonObject> VectorObject = MakeShared<FJsonObject>();
	VectorObject->SetNumberField(TEXT("x"), static_cast<double>(Value.X));
	VectorObject->SetNumberField(TEXT("y"), static_cast<double>(Value.Y));
	VectorObject->SetNumberField(TEXT("z"), static_cast<double>(Value.Z));
	Object->SetObjectField(FieldName, VectorObject);
}

struct FWaypointPathRoutingValidationResult
{
	FString Status = TEXT("NotApplicable");
	FName SuggestedNetworkId = NAME_None;
	FName SuggestedSubNetworkId = NAME_None;
	FString Detail;
};

FWaypointPathRoutingValidationResult ValidateWaypointPathRouting(
	const AFlightWaypointPath& Path,
	const FFlightNavGraphSnapshot& Snapshot)
{
	FWaypointPathRoutingValidationResult Result;
	if (Snapshot.Nodes.IsEmpty())
	{
		Result.Status = TEXT("NoNearbyNodes");
		Result.Detail = TEXT("No nav-graph nodes are registered.");
		return Result;
	}

	constexpr float ValidationRadiusCm = 2500.0f;
	const float ValidationRadiusSq = FMath::Square(ValidationRadiusCm);
	const FVector SamplePoints[] = {
		Path.GetLocationAtNormalizedPosition(0.0f),
		Path.GetLocationAtNormalizedPosition(0.5f),
		Path.GetLocationAtNormalizedPosition(1.0f)
	};

	TMap<FName, int32> NetworkCounts;
	TMap<FName, int32> SubNetworkCounts;
	int32 NearbyNodeCount = 0;

	for (const FFlightNavGraphNodeSnapshot& Node : Snapshot.Nodes)
	{
		bool bIsNearby = false;
		for (const FVector& SamplePoint : SamplePoints)
		{
			if (FVector::DistSquared(SamplePoint, Node.Location) <= ValidationRadiusSq)
			{
				bIsNearby = true;
				break;
			}
		}

		if (!bIsNearby)
		{
			continue;
		}

		++NearbyNodeCount;
		if (!Node.NetworkId.IsNone())
		{
			NetworkCounts.FindOrAdd(Node.NetworkId) += 1;
		}
		if (!Node.SubNetworkId.IsNone())
		{
			SubNetworkCounts.FindOrAdd(Node.SubNetworkId) += 1;
		}
	}

	if (NearbyNodeCount == 0)
	{
		Result.Status = TEXT("NoNearbyNodes");
		Result.Detail = FString::Printf(TEXT("No nav-graph nodes were found within %.0fcm of sampled path points."), ValidationRadiusCm);
		return Result;
	}

	auto ResolveDominantName = [](const TMap<FName, int32>& Counts, bool& bOutAmbiguous) -> FName
	{
		bOutAmbiguous = false;
		FName Dominant = NAME_None;
		int32 BestCount = 0;
		for (const TPair<FName, int32>& Pair : Counts)
		{
			if (Pair.Value > BestCount)
			{
				Dominant = Pair.Key;
				BestCount = Pair.Value;
				bOutAmbiguous = false;
			}
			else if (Pair.Value > 0 && Pair.Value == BestCount && Pair.Key != Dominant)
			{
				bOutAmbiguous = true;
			}
		}
		return Dominant;
	};

	bool bAmbiguousNetwork = false;
	bool bAmbiguousSubNetwork = false;
	Result.SuggestedNetworkId = ResolveDominantName(NetworkCounts, bAmbiguousNetwork);
	Result.SuggestedSubNetworkId = ResolveDominantName(SubNetworkCounts, bAmbiguousSubNetwork);

	if (bAmbiguousNetwork || bAmbiguousSubNetwork)
	{
		Result.Status = TEXT("AmbiguousNearbyNodes");
		Result.Detail = TEXT("Nearby nav-graph nodes disagree on dominant network or subnetwork.");
		return Result;
	}

	const FName AuthoredNetworkId = Path.GetNavNetworkId();
	const FName AuthoredSubNetworkId = Path.GetNavSubNetworkId();
	if (AuthoredNetworkId.IsNone() && AuthoredSubNetworkId.IsNone())
	{
		Result.Status = TEXT("MissingMetadata");
		Result.Detail = TEXT("Waypoint path has no authored network metadata.");
		return Result;
	}

	const bool bNetworkMatches = AuthoredNetworkId.IsNone() || Result.SuggestedNetworkId.IsNone() || AuthoredNetworkId == Result.SuggestedNetworkId;
	const bool bSubNetworkMatches = AuthoredSubNetworkId.IsNone() || Result.SuggestedSubNetworkId.IsNone() || AuthoredSubNetworkId == Result.SuggestedSubNetworkId;
	if (bNetworkMatches && bSubNetworkMatches)
	{
		Result.Status = TEXT("Match");
		Result.Detail = TEXT("Nearby nav-graph nodes agree with authored waypoint-path routing metadata.");
		return Result;
	}

	Result.Status = TEXT("Mismatch");
	Result.Detail = TEXT("Nearby nav-graph nodes disagree with authored waypoint-path routing metadata.");
	return Result;
}

void FinalizeNavigationCandidates(TArray<Flight::Orchestration::FFlightNavigationCandidateRecord>& Candidates)
{
	for (Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate : Candidates)
	{
		Candidate.bLegal = Candidate.ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey);
		Candidate.LegalityReason = NAME_None;

		switch (Candidate.SourceKind)
		{
		case Flight::Orchestration::EFlightParticipantKind::NavigationNode:
			if (!Candidate.SourceId.IsValid())
			{
				Candidate.bLegal = false;
				Candidate.LegalityReason = TEXT("MissingSourceId");
			}
			break;

		case Flight::Orchestration::EFlightParticipantKind::NavigationEdge:
			if (!Candidate.SourceId.IsValid())
			{
				Candidate.bLegal = false;
				Candidate.LegalityReason = TEXT("MissingSourceId");
			}
			break;

		case Flight::Orchestration::EFlightParticipantKind::WaypointPath:
			if (Candidate.EstimatedCost < 0.0f)
			{
				Candidate.bLegal = false;
				Candidate.LegalityReason = TEXT("InvalidCost");
			}
			break;

		default:
			break;
		}

		if (Candidate.Status.IsEmpty())
		{
			Candidate.Status = TEXT("Visible");
		}

		if (!Candidate.bLegal)
		{
			Candidate.Status = TEXT("Rejected");
			Candidate.RankScore = 0.0f;
			continue;
		}

		float Score = 1.0f / (1.0f + FMath::Max(Candidate.EstimatedCost, 0.0f));
		if (Candidate.ContractKeys.Contains(Flight::Navigation::Contracts::CommitKey))
		{
			Score += 1.0f;
		}
		if (Candidate.Tags.Contains(TEXT("Preferred")))
		{
			Score += 0.25f;
		}
		if (Candidate.Tags.Contains(TEXT("Registered")))
		{
			Score += 0.1f;
		}
		if (Candidate.Status == TEXT("Advertised"))
		{
			Score -= 0.05f;
		}

		Candidate.RankScore = FMath::Max(Score, 0.0f);
	}

	Candidates.Sort([](
		const Flight::Orchestration::FFlightNavigationCandidateRecord& Left,
		const Flight::Orchestration::FFlightNavigationCandidateRecord& Right)
	{
		if (Left.bLegal != Right.bLegal)
		{
			return Left.bLegal && !Right.bLegal;
		}

		if (!FMath::IsNearlyEqual(Left.RankScore, Right.RankScore))
		{
			return Left.RankScore > Right.RankScore;
		}

		if (Left.SourceKind != Right.SourceKind)
		{
			return static_cast<uint8>(Left.SourceKind) < static_cast<uint8>(Right.SourceKind);
		}

		return Left.Name.LexicalLess(Right.Name);
	});

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Candidates[Index].RankOrder = Index;
	}
}

TArray<Flight::Orchestration::FFlightNavigationCandidateRecord> BuildNavigationCandidateRecords(
	const UWorld* World,
	const TMap<uint64, Flight::Orchestration::FFlightParticipantRecord>& ParticipantsByHandle)
{
	TArray<Flight::Orchestration::FFlightNavigationCandidateRecord> Candidates;
	if (!World)
	{
		return Candidates;
	}

	FFlightNavGraphSnapshot NavigationSnapshot;
	bool bHasNavigationSnapshot = false;
	if (const UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>())
	{
		NavigationSnapshot = NavGraphHub->BuildSnapshot();
		bHasNavigationSnapshot = true;

		for (const FFlightNavGraphNodeSnapshot& Node : NavigationSnapshot.Nodes)
		{
			Flight::Orchestration::FFlightNavigationCandidateRecord Candidate;
			Candidate.SourceKind = Flight::Orchestration::EFlightParticipantKind::NavigationNode;
			Candidate.Name = Node.DisplayName.IsNone() ? FName(*Node.NodeId.ToString(EGuidFormats::Digits)) : Node.DisplayName;
			Candidate.SourceId = Node.NodeId;
			Candidate.OwnerSubsystem = TEXT("UFlightNavGraphDataHubSubsystem");
			Candidate.NetworkId = Node.NetworkId;
			Candidate.SubNetworkId = Node.SubNetworkId;
			Candidate.StartLocation = Node.Location;
			Candidate.EndLocation = Node.Location;
			Candidate.Tags = Node.Tags;
			Candidate.Tags.AddUnique(TEXT("Navigation"));
			Candidate.Tags.AddUnique(TEXT("NavGraph"));
			Candidate.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
			Candidate.Status = TEXT("Resolved");
			Candidates.Add(MoveTemp(Candidate));
		}

		for (const FFlightNavGraphEdgeSnapshot& Edge : NavigationSnapshot.Edges)
		{
			Flight::Orchestration::FFlightNavigationCandidateRecord Candidate;
			Candidate.SourceKind = Flight::Orchestration::EFlightParticipantKind::NavigationEdge;
			Candidate.Name = FName(*Edge.EdgeId.ToString(EGuidFormats::Digits));
			Candidate.SourceId = Edge.EdgeId;
			Candidate.OwnerSubsystem = TEXT("UFlightNavGraphDataHubSubsystem");
			Candidate.StartLocation = Edge.FromLocation;
			Candidate.EndLocation = Edge.ToLocation;
			Candidate.EstimatedCost = Edge.BaseCost;
			Candidate.bBidirectional = Edge.bBidirectional;
			Candidate.Tags = Edge.Tags;
			Candidate.Tags.AddUnique(TEXT("Navigation"));
			Candidate.Tags.AddUnique(TEXT("NavGraph"));
			Candidate.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
			Candidate.Status = TEXT("Resolved");
			Candidates.Add(MoveTemp(Candidate));
		}
	}

	for (const TPair<uint64, Flight::Orchestration::FFlightParticipantRecord>& Pair : ParticipantsByHandle)
	{
		const Flight::Orchestration::FFlightParticipantRecord& Participant = Pair.Value;
		if (!Participant.ContractKeys.Contains(Flight::Navigation::Contracts::CandidateKey))
		{
			continue;
		}

		if (Participant.Kind != Flight::Orchestration::EFlightParticipantKind::WaypointPath)
		{
			continue;
		}

		Flight::Orchestration::FFlightNavigationCandidateRecord Candidate;
		Candidate.SourceKind = Participant.Kind;
		Candidate.Name = Participant.Name;
		Candidate.OwnerSubsystem = Participant.OwnerSubsystem;
		Candidate.Tags = Participant.Tags;
		Candidate.ContractKeys = Participant.ContractKeys;
		Candidate.Tags.AddUnique(TEXT("Navigation"));

		if (const AFlightWaypointPath* Path = Cast<AFlightWaypointPath>(Participant.SourceObject.Get()))
		{
			Candidate.SourceId = Path->GetPathId();
			Candidate.NetworkId = Path->GetNavNetworkId();
			Candidate.SubNetworkId = Path->GetNavSubNetworkId();
			Candidate.StartLocation = Path->GetLocationAtNormalizedPosition(0.0f);
			Candidate.EndLocation = Path->GetLocationAtNormalizedPosition(1.0f);
			Candidate.EstimatedCost = Path->GetPathLength();
			Candidate.bBidirectional = true;
			Candidate.Tags.AddUnique(TEXT("SplinePath"));
			Candidate.Status = TEXT("Resolved");
			if (bHasNavigationSnapshot)
			{
				const FWaypointPathRoutingValidationResult Validation =
					ValidateWaypointPathRouting(*Path, NavigationSnapshot);
				Candidate.RoutingValidationStatus = Validation.Status;
				Candidate.SuggestedNetworkId = Validation.SuggestedNetworkId;
				Candidate.SuggestedSubNetworkId = Validation.SuggestedSubNetworkId;
				Candidate.RoutingValidationDetail = Validation.Detail;
			}
		}
		else
		{
			Candidate.Status = TEXT("Advertised");
			Candidate.RoutingValidationStatus = TEXT("NotApplicable");
		}

		Candidates.Add(MoveTemp(Candidate));
	}

	FinalizeNavigationCandidates(Candidates);

	return Candidates;
}

bool CohortHasRequiredNavigationInputs(
	const Flight::Orchestration::FFlightCohortRecord& Cohort,
	const bool bHasNavigationIntent,
	const bool bHasNavigationCommit,
	const TArray<Flight::Orchestration::FFlightNavigationCandidateRecord>& NavigationCandidates)
{
	for (const FName Contract : Cohort.RequiredNavigationContracts)
	{
		if (Contract == Flight::Navigation::Contracts::IntentKey && !bHasNavigationIntent)
		{
			return false;
		}

		if (Contract == Flight::Navigation::Contracts::CommitKey && !bHasNavigationCommit)
		{
			return false;
		}

		if (Contract == Flight::Navigation::Contracts::CandidateKey && NavigationCandidates.IsEmpty())
		{
			return false;
		}
	}

	return true;
}

struct FCohortNavigationSelection
{
	const Flight::Orchestration::FFlightNavigationCandidateRecord* Candidate = nullptr;
	float Score = 0.0f;
	FString Reason;
};

FCohortNavigationSelection ResolveNavigationCandidateForCohort(
	const Flight::Orchestration::FFlightCohortRecord& Cohort,
	const TMap<uint64, Flight::Orchestration::FFlightParticipantRecord>& ParticipantsByHandle,
	const TArray<Flight::Orchestration::FFlightNavigationCandidateRecord>& NavigationCandidates)
{
	FCohortNavigationSelection BestSelection;
	TArray<FName> CohortAffinityNames = Cohort.Tags;
	TArray<FVector> AnchorLocations;

	for (const Flight::Orchestration::FFlightParticipantHandle Handle : Cohort.Participants)
	{
		const Flight::Orchestration::FFlightParticipantRecord* Participant = ParticipantsByHandle.Find(Handle.Value);
		if (!Participant)
		{
			continue;
		}

		CohortAffinityNames.AddUnique(Participant->Name);
		for (const FName Tag : Participant->Tags)
		{
			CohortAffinityNames.AddUnique(Tag);
		}

		if (const AFlightSpawnSwarmAnchor* Anchor = Cast<AFlightSpawnSwarmAnchor>(Participant->SourceObject.Get()))
		{
			AnchorLocations.Add(Anchor->GetActorLocation());
		}
	}

	for (const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate : NavigationCandidates)
	{
		if (!Candidate.bLegal)
		{
			continue;
		}

		if (!Cohort.DesiredNavigationNetwork.IsNone())
		{
			if (Candidate.NetworkId.IsNone() || Candidate.NetworkId != Cohort.DesiredNavigationNetwork)
			{
				continue;
			}
		}

		if (!Cohort.DesiredNavigationSubNetwork.IsNone())
		{
			if (Candidate.SubNetworkId.IsNone() || Candidate.SubNetworkId != Cohort.DesiredNavigationSubNetwork)
			{
				continue;
			}
		}

		float Score = Candidate.RankScore;
		TArray<FString> Reasons;

		if (!Cohort.DesiredNavigationNetwork.IsNone())
		{
			Score += 1.5f;
			Reasons.Add(FString::Printf(TEXT("Network:%s"), *Cohort.DesiredNavigationNetwork.ToString()));
		}

		if (!Cohort.DesiredNavigationSubNetwork.IsNone())
		{
			Score += 1.0f;
			Reasons.Add(FString::Printf(TEXT("SubNetwork:%s"), *Cohort.DesiredNavigationSubNetwork.ToString()));
		}

		for (const Flight::Orchestration::FFlightParticipantHandle Handle : Cohort.Participants)
		{
			const Flight::Orchestration::FFlightParticipantRecord* Participant = ParticipantsByHandle.Find(Handle.Value);
			if (!Participant)
			{
				continue;
			}

			if (Participant->Kind == Candidate.SourceKind && Participant->Name == Candidate.Name)
			{
				Score += 2.0f;
				Reasons.Add(TEXT("ParticipantAffinity"));
				break;
			}
		}

		for (const FName CohortNameHint : CohortAffinityNames)
		{
			if (CohortNameHint.IsNone())
			{
				continue;
			}

			if (Candidate.Name == CohortNameHint || Candidate.NetworkId == CohortNameHint || Candidate.SubNetworkId == CohortNameHint || Candidate.Tags.Contains(CohortNameHint))
			{
				Score += 1.5f;
				Reasons.Add(FString::Printf(TEXT("IdentityMatch:%s"), *CohortNameHint.ToString()));
				break;
			}
		}

		if (!AnchorLocations.IsEmpty())
		{
			float BestDistance = TNumericLimits<float>::Max();
			for (const FVector& AnchorLocation : AnchorLocations)
			{
				BestDistance = FMath::Min(BestDistance, FVector::Dist(AnchorLocation, Candidate.StartLocation));
			}

			if (BestDistance < TNumericLimits<float>::Max())
			{
				const float DistanceBonus = 1.0f / (1.0f + (BestDistance / 1000.0f));
				Score += DistanceBonus;
				Reasons.Add(FString::Printf(TEXT("AnchorDistance:%.2f"), DistanceBonus));
			}
		}

		if (BestSelection.Candidate == nullptr
			|| Score > BestSelection.Score
			|| (FMath::IsNearlyEqual(Score, BestSelection.Score)
				&& Candidate.RankOrder < BestSelection.Candidate->RankOrder))
		{
			BestSelection.Candidate = &Candidate;
			BestSelection.Score = Score;
			BestSelection.Reason = Reasons.IsEmpty() ? TEXT("BaseRank") : FString::Join(Reasons, TEXT("+"));
		}
	}

	return BestSelection;
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
	Collection.InitializeDependency<UFlightNavGraphDataHubSubsystem>();
	Collection.InitializeDependency<UFlightSimpleSCSLShaderPipelineSubsystem>();

	RebuildVisibility();
	RebuildExecutionPlan();
}

void UFlightOrchestrationSubsystem::Deinitialize()
{
	ResetVisibilityState();
	ReconciledBatchCohortsByName.Reset();
	Bindings.Reset();
	Services.Reset();
	MissingContracts.Reset();
	Diagnostics.Reset();
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

bool UFlightOrchestrationSubsystem::ReconcileBatchLoweringPlans(
	const TArray<Flight::Mass::FFlightMassBatchLoweringPlan>& Plans)
{
	const TMap<FName, Flight::Orchestration::FFlightCohortRecord> PreviousReconciledBatchCohortsByName =
		ReconciledBatchCohortsByName;
	TMap<FName, Flight::Orchestration::FFlightCohortRecord> NextReconciledBatchCohortsByName;

	for (const Flight::Mass::FFlightMassBatchLoweringPlan& Plan : Plans)
	{
		if (!Plan.Orchestration.bHasCohortRecord || Plan.Orchestration.Cohort.Name.IsNone())
		{
			continue;
		}

		NextReconciledBatchCohortsByName.Add(Plan.Orchestration.Cohort.Name, Plan.Orchestration.Cohort);
	}

	bool bChanged = ReconciledBatchCohortsByName.Num() != NextReconciledBatchCohortsByName.Num();
	if (!bChanged)
	{
		for (const TPair<FName, Flight::Orchestration::FFlightCohortRecord>& Pair : NextReconciledBatchCohortsByName)
		{
			const Flight::Orchestration::FFlightCohortRecord* ExistingProposal =
				ReconciledBatchCohortsByName.Find(Pair.Key);
			if (!ExistingProposal || !Flight::Orchestration::AreCohortProposalsEquivalent(*ExistingProposal, Pair.Value))
			{
				bChanged = true;
				break;
			}
		}
	}

	ReconciledBatchCohortsByName = MoveTemp(NextReconciledBatchCohortsByName);
	for (const TPair<FName, Flight::Orchestration::FFlightCohortRecord>& Pair : PreviousReconciledBatchCohortsByName)
	{
		if (ReconciledBatchCohortsByName.Contains(Pair.Key))
		{
			continue;
		}

		if (const Flight::Orchestration::FFlightCohortRecord* ExistingCohort = CohortsByName.Find(Pair.Key);
			ExistingCohort && ExistingCohort->Participants.IsEmpty())
		{
			CohortsByName.Remove(Pair.Key);
		}
	}

	Flight::Orchestration::ApplyReconciledBatchCohorts(CohortsByName, ReconciledBatchCohortsByName);
	MissingContracts.Reset();
	BuildMissingContracts();
	BuildDiagnostics();
	RebuildExecutionPlan();
	return bChanged;
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

namespace
{

bool HasParticipantAdvertisingContract(
	const TMap<uint64, Flight::Orchestration::FFlightParticipantRecord>& ParticipantsByHandle,
	const FName ContractKey)
{
	for (const TPair<uint64, Flight::Orchestration::FFlightParticipantRecord>& Pair : ParticipantsByHandle)
	{
		if (Pair.Value.ContractKeys.Contains(ContractKey))
		{
			return true;
		}
	}

	return false;
}

} // namespace

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
	IngestNavigationGraph();
	IngestSpatialFields();
	IngestBehaviors();
	BuildDefaultCohorts();
	BuildMissingContracts();
	BuildDiagnostics();
	RebuildCachedReport();
}

void UFlightOrchestrationSubsystem::RebuildExecutionPlan()
{
	Bindings.Reset();
	ExecutionPlan.Generation += 1;
	ExecutionPlan.BuiltAtUtc = FDateTime::UtcNow();
	ExecutionPlan.Steps.Reset();

	const bool bHasNavigationIntent = HasParticipantAdvertisingContract(ParticipantsByHandle, Flight::Navigation::Contracts::IntentKey);
	const bool bHasNavigationCommit = HasParticipantAdvertisingContract(ParticipantsByHandle, Flight::Navigation::Contracts::CommitKey);
	const TArray<Flight::Orchestration::FFlightNavigationCandidateRecord> NavigationCandidates =
		Flight::Orchestration::BuildNavigationCandidateRecords(GetWorld(), ParticipantsByHandle);

	for (const TPair<FName, Flight::Orchestration::FFlightCohortRecord>& Pair : CohortsByName)
	{
		if (!Flight::Orchestration::CohortHasRequiredNavigationInputs(
			Pair.Value,
			bHasNavigationIntent,
			bHasNavigationCommit,
			NavigationCandidates))
		{
			continue;
		}

		const Flight::Orchestration::FCohortNavigationSelection NavigationSelection =
			Flight::Orchestration::ResolveNavigationCandidateForCohort(Pair.Value, ParticipantsByHandle, NavigationCandidates);
		if (Pair.Value.RequiredNavigationContracts.Contains(Flight::Navigation::Contracts::CandidateKey)
			&& NavigationSelection.Candidate == nullptr)
		{
			continue;
		}

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
		if (NavigationSelection.Candidate)
		{
			Step.NavigationCandidateId = NavigationSelection.Candidate->SourceId;
			Step.NavigationCandidateName = NavigationSelection.Candidate->Name;
			Step.NavigationCandidateScore = NavigationSelection.Score;
			Step.NavigationCandidateRankOrder = NavigationSelection.Candidate->RankOrder;
			Step.NavigationSelectionReason = NavigationSelection.Reason;
		}
		Step.InputContracts = SelectedBehavior->RequiredContracts;
		for (const FName Contract : Pair.Value.RequiredNavigationContracts)
		{
			Step.InputContracts.AddUnique(Contract);
		}
		Step.OutputConsumers.Add(TEXT("Mass"));
		Step.OutputConsumers.Add(TEXT("Swarm"));
		Step.OutputConsumers.AddUnique(TEXT("NavigationCommit"));
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
	OnExecutionPlanUpdated.Broadcast();
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

	TArray<TSharedPtr<FJsonValue>> NavigationCandidateValues;
	for (const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate : CachedReport.NavigationCandidates)
	{
		TSharedRef<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
		CandidateObject->SetStringField(TEXT("kind"), Flight::Orchestration::ParticipantKindToString(Candidate.SourceKind));
		CandidateObject->SetStringField(TEXT("name"), Candidate.Name.ToString());
		CandidateObject->SetStringField(TEXT("sourceId"), Candidate.SourceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		CandidateObject->SetStringField(TEXT("ownerSubsystem"), Candidate.OwnerSubsystem.ToString());
		CandidateObject->SetStringField(TEXT("networkId"), Candidate.NetworkId.ToString());
		CandidateObject->SetStringField(TEXT("subNetworkId"), Candidate.SubNetworkId.ToString());
		Flight::Orchestration::AddVectorToJson(Candidate.StartLocation, TEXT("startLocation"), CandidateObject);
		Flight::Orchestration::AddVectorToJson(Candidate.EndLocation, TEXT("endLocation"), CandidateObject);
		CandidateObject->SetNumberField(TEXT("estimatedCost"), static_cast<double>(Candidate.EstimatedCost));
		CandidateObject->SetBoolField(TEXT("legal"), Candidate.bLegal);
		CandidateObject->SetStringField(TEXT("legalityReason"), Candidate.LegalityReason.ToString());
		CandidateObject->SetNumberField(TEXT("rankScore"), static_cast<double>(Candidate.RankScore));
		CandidateObject->SetNumberField(TEXT("rankOrder"), Candidate.RankOrder);
		CandidateObject->SetBoolField(TEXT("bidirectional"), Candidate.bBidirectional);
		CandidateObject->SetStringField(TEXT("status"), Candidate.Status);
		CandidateObject->SetStringField(TEXT("routingValidationStatus"), Candidate.RoutingValidationStatus);
		CandidateObject->SetStringField(TEXT("suggestedNetworkId"), Candidate.SuggestedNetworkId.ToString());
		CandidateObject->SetStringField(TEXT("suggestedSubNetworkId"), Candidate.SuggestedSubNetworkId.ToString());
		CandidateObject->SetStringField(TEXT("routingValidationDetail"), Candidate.RoutingValidationDetail);
		Flight::Orchestration::AddNameArrayToJson(Candidate.Tags, TEXT("tags"), CandidateObject);
		Flight::Orchestration::AddNameArrayToJson(Candidate.ContractKeys, TEXT("contractKeys"), CandidateObject);
		NavigationCandidateValues.Add(MakeShared<FJsonValueObject>(CandidateObject));
	}
	Root->SetArrayField(TEXT("navigationCandidates"), NavigationCandidateValues);

	TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
	for (const Flight::Orchestration::FFlightOrchestrationDiagnostic& Diagnostic : CachedReport.Diagnostics)
	{
		TSharedRef<FJsonObject> DiagnosticObject = MakeShared<FJsonObject>();
		DiagnosticObject->SetStringField(TEXT("severity"), Diagnostic.Severity.ToString());
		DiagnosticObject->SetStringField(TEXT("category"), Diagnostic.Category.ToString());
		DiagnosticObject->SetStringField(TEXT("sourceName"), Diagnostic.SourceName.ToString());
		DiagnosticObject->SetStringField(TEXT("message"), Diagnostic.Message);
		DiagnosticValues.Add(MakeShared<FJsonValueObject>(DiagnosticObject));
	}
	Root->SetArrayField(TEXT("diagnostics"), DiagnosticValues);

	TArray<TSharedPtr<FJsonValue>> CohortValues;
	for (const Flight::Orchestration::FFlightCohortRecord& Cohort : CachedReport.Cohorts)
	{
		TSharedRef<FJsonObject> CohortObject = MakeShared<FJsonObject>();
		CohortObject->SetStringField(TEXT("name"), Cohort.Name.ToString());
		Flight::Orchestration::AddNameArrayToJson(Cohort.Tags, TEXT("tags"), CohortObject);
		CohortObject->SetStringField(TEXT("desiredNavigationNetwork"), Cohort.DesiredNavigationNetwork.ToString());
		CohortObject->SetStringField(TEXT("desiredNavigationSubNetwork"), Cohort.DesiredNavigationSubNetwork.ToString());
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
		Flight::Orchestration::AddNameArrayToJson(Cohort.RequiredNavigationContracts, TEXT("requiredNavigationContracts"), CohortObject);

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
		BehaviorObject->SetStringField(TEXT("selectedBackend"), Behavior.SelectedBackend);
		BehaviorObject->SetStringField(TEXT("committedBackend"), Behavior.CommittedBackend);
		Flight::Orchestration::AddStringArrayToJson(Behavior.ImportedSymbols, TEXT("importedSymbols"), BehaviorObject);
		Flight::Orchestration::AddStringArrayToJson(Behavior.ExportedSymbols, TEXT("exportedSymbols"), BehaviorObject);
		BehaviorObject->SetNumberField(TEXT("boundaryOperatorCount"), Behavior.BoundaryOperatorCount);
		BehaviorObject->SetBoolField(TEXT("hasBoundarySemantics"), Behavior.bHasBoundarySemantics);
		BehaviorObject->SetBoolField(TEXT("boundarySemanticsExecutable"), Behavior.bBoundarySemanticsExecutable);
		BehaviorObject->SetBoolField(TEXT("hasAwaitableBoundary"), Behavior.bHasAwaitableBoundary);
		BehaviorObject->SetBoolField(TEXT("hasMirrorRequest"), Behavior.bHasMirrorRequest);
		BehaviorObject->SetStringField(TEXT("boundaryExecutionDetail"), Behavior.BoundaryExecutionDetail);
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
		StepObject->SetStringField(TEXT("navigationCandidateId"), Step.NavigationCandidateId.ToString(EGuidFormats::DigitsWithHyphensLower));
		StepObject->SetStringField(TEXT("navigationCandidateName"), Step.NavigationCandidateName.ToString());
		StepObject->SetNumberField(TEXT("navigationCandidateScore"), static_cast<double>(Step.NavigationCandidateScore));
		StepObject->SetNumberField(TEXT("navigationCandidateRankOrder"), Step.NavigationCandidateRankOrder);
		StepObject->SetStringField(TEXT("navigationSelectionReason"), Step.NavigationSelectionReason);
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
	Diagnostics.Reset();
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
	AddServiceStatus(TEXT("UFlightNavGraphDataHubSubsystem"), World->GetSubsystem<UFlightNavGraphDataHubSubsystem>() != nullptr);
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
		Flight::Orchestration::RegisterParticipantAdapter(*this, *It);
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
		Flight::Orchestration::RegisterParticipantAdapter(*this, *It);
	}
}

void UFlightOrchestrationSubsystem::IngestNavigationGraph()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFlightNavGraphDataHubSubsystem* NavGraphHub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
	if (!NavGraphHub || (!NavGraphHub->HasNodes() && !NavGraphHub->HasEdges()))
	{
		return;
	}

	const FFlightNavGraphSnapshot Snapshot = NavGraphHub->BuildSnapshot();

	for (const FFlightNavGraphNodeSnapshot& Node : Snapshot.Nodes)
	{
		Flight::Orchestration::FFlightParticipantRecord Record;
		Record.Kind = Flight::Orchestration::EFlightParticipantKind::NavigationNode;
		Record.Name = Node.DisplayName.IsNone() ? FName(*Node.NodeId.ToString(EGuidFormats::Digits)) : Node.DisplayName;
		Record.OwnerSubsystem = TEXT("UFlightNavGraphDataHubSubsystem");
		Record.SourceObject = const_cast<UFlightNavGraphDataHubSubsystem*>(NavGraphHub);
		Record.SourceObjectPath = NavGraphHub->GetPathName();
		Record.Tags = Node.Tags;
		Record.Tags.AddUnique(TEXT("Navigation"));
		Record.Tags.AddUnique(TEXT("NavGraph"));
		Record.Capabilities.Add(TEXT("NavigationNode"));
		Record.Capabilities.Add(TEXT("NavigationCandidateSource"));
		Record.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
		RegisterParticipant(Record);
	}

	for (const FFlightNavGraphEdgeSnapshot& Edge : Snapshot.Edges)
	{
		Flight::Orchestration::FFlightParticipantRecord Record;
		Record.Kind = Flight::Orchestration::EFlightParticipantKind::NavigationEdge;
		Record.Name = FName(*Edge.EdgeId.ToString(EGuidFormats::Digits));
		Record.OwnerSubsystem = TEXT("UFlightNavGraphDataHubSubsystem");
		Record.SourceObject = const_cast<UFlightNavGraphDataHubSubsystem*>(NavGraphHub);
		Record.SourceObjectPath = NavGraphHub->GetPathName();
		Record.Tags = Edge.Tags;
		Record.Tags.AddUnique(TEXT("Navigation"));
		Record.Tags.AddUnique(TEXT("NavGraph"));
		Record.Capabilities.Add(TEXT("NavigationEdge"));
		Record.Capabilities.Add(TEXT("NavigationCandidateSource"));
		Record.ContractKeys.Add(Flight::Navigation::Contracts::CandidateKey);
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
		Record.ContractKeys.Add(Flight::Navigation::Contracts::FieldSampleKey);
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
		Record.ResolvedDomain = Flight::Orchestration::ResolveExecutionDomain(Pair.Value);
		Record.SelectedBackend = Pair.Value.SelectedBackend;
		Record.CommittedBackend = Pair.Value.CommittedBackend;
		Record.ImportedSymbols = Pair.Value.ImportedSymbols;
		Record.ExportedSymbols = Pair.Value.ExportedSymbols;
		Record.BoundaryOperatorCount = Pair.Value.BoundaryOperatorCount;
		Record.bHasBoundarySemantics = Pair.Value.bHasBoundarySemantics;
		Record.bBoundarySemanticsExecutable = Pair.Value.bBoundarySemanticsExecutable;
		Record.bHasAwaitableBoundary = Pair.Value.bHasAwaitableBoundary;
		Record.bHasMirrorRequest = Pair.Value.bHasMirrorRequest;
		Record.BoundaryExecutionDetail = Pair.Value.BoundaryExecutionDetail;
		Record.bExecutable = (Pair.Value.bHasExecutableProcedure || Pair.Value.bUsesNativeFallback || Pair.Value.SimdPlan.IsValid() || (Record.ResolvedDomain == Flight::Orchestration::EFlightExecutionDomain::Gpu))
			&& (!Record.bHasBoundarySemantics || Record.bBoundarySemanticsExecutable);
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
			Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::IntentKey);
			Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::CandidateKey);
			Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::CommitKey);
			if (const AFlightSpawnSwarmAnchor* Anchor = Cast<AFlightSpawnSwarmAnchor>(Participant.SourceObject.Get()))
			{
				Cohort.DesiredNavigationNetwork = Anchor->GetNavNetworkId();
				Cohort.DesiredNavigationSubNetwork = Anchor->GetNavSubNetworkId();
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
		Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::IntentKey);
		Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::CandidateKey);
		Cohort.RequiredNavigationContracts.AddUnique(Flight::Navigation::Contracts::CommitKey);
		RegisterCohort(Cohort);
	}

	Flight::Orchestration::ApplyReconciledBatchCohorts(CohortsByName, ReconciledBatchCohortsByName);
}

void UFlightOrchestrationSubsystem::BuildMissingContracts()
{
	const bool bHasSwarmParticipants = HasParticipantOfKind(Flight::Orchestration::EFlightParticipantKind::SpawnAnchor);
	const bool bHasWaypointPaths = HasParticipantOfKind(Flight::Orchestration::EFlightParticipantKind::WaypointPath);
	const bool bHasNavigationIntent = HasParticipantAdvertisingContract(ParticipantsByHandle, Flight::Navigation::Contracts::IntentKey);
	const bool bHasNavigationCommit = HasParticipantAdvertisingContract(ParticipantsByHandle, Flight::Navigation::Contracts::CommitKey);
	const TArray<Flight::Orchestration::FFlightNavigationCandidateRecord> NavigationCandidates =
		Flight::Orchestration::BuildNavigationCandidateRecords(GetWorld(), ParticipantsByHandle);
	const bool bHasNavigationCandidate = !NavigationCandidates.IsEmpty();

	if (bHasSwarmParticipants && !bHasWaypointPaths)
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Swarm");
		MissingContract.ContractKey = TEXT("WaypointPath");
		MissingContract.Issue = TEXT("Swarm anchors are visible, but no waypoint path is available.");
		MissingContracts.Add(MissingContract);
	}

	if (bHasSwarmParticipants && !bHasNavigationIntent)
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Navigation");
		MissingContract.ContractKey = Flight::Navigation::Contracts::IntentKey;
		MissingContract.Issue = TEXT("Swarm anchors are visible, but no participant is advertising Navigation.Intent.");
		MissingContracts.Add(MissingContract);
	}

	if (bHasSwarmParticipants && !bHasNavigationCandidate)
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Navigation");
		MissingContract.ContractKey = Flight::Navigation::Contracts::CandidateKey;
		MissingContract.Issue = TEXT("Swarm anchors are visible, but no navigation candidate records are available.");
		MissingContracts.Add(MissingContract);
	}

	if (bHasSwarmParticipants && !bHasNavigationCommit)
	{
		Flight::Orchestration::FFlightMissingContract MissingContract;
		MissingContract.Scope = TEXT("Navigation");
		MissingContract.ContractKey = Flight::Navigation::Contracts::CommitKey;
		MissingContract.Issue = TEXT("Swarm anchors are visible, but no participant is advertising Navigation.Commit.");
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

void UFlightOrchestrationSubsystem::BuildDiagnostics()
{
	Diagnostics.Reset();

	for (const TPair<FName, Flight::Orchestration::FFlightCohortRecord>& Pair : ReconciledBatchCohortsByName)
	{
		const Flight::Orchestration::FFlightCohortRecord* CanonicalCohort = CohortsByName.Find(Pair.Key);
		if (!CanonicalCohort)
		{
			continue;
		}

		FString Mismatch;
		if (Flight::Orchestration::BuildCohortReconciliationMismatch(*CanonicalCohort, Pair.Value, Mismatch))
		{
			Flight::Orchestration::FFlightOrchestrationDiagnostic Diagnostic;
			Diagnostic.Severity = TEXT("Warning");
			Diagnostic.Category = TEXT("CohortReconciliation");
			Diagnostic.SourceName = Pair.Key;
			Diagnostic.Message = Mismatch;
			Diagnostics.Add(MoveTemp(Diagnostic));
		}
	}

	const TArray<Flight::Orchestration::FFlightNavigationCandidateRecord> NavigationCandidates =
		Flight::Orchestration::BuildNavigationCandidateRecords(GetWorld(), ParticipantsByHandle);

	for (const Flight::Orchestration::FFlightNavigationCandidateRecord& Candidate : NavigationCandidates)
	{
		if (Candidate.SourceKind != Flight::Orchestration::EFlightParticipantKind::WaypointPath)
		{
			continue;
		}

		if (Candidate.RoutingValidationStatus == TEXT("Match") || Candidate.RoutingValidationStatus == TEXT("NotApplicable"))
		{
			continue;
		}

		Flight::Orchestration::FFlightOrchestrationDiagnostic Diagnostic;
		Diagnostic.Severity = TEXT("Warning");
		Diagnostic.Category = TEXT("NavigationRouting");
		Diagnostic.SourceName = Candidate.Name;
		Diagnostic.Message = FString::Printf(
			TEXT("Waypoint-path routing validation reported %s. %s SuggestedNetwork=%s SuggestedSubNetwork=%s"),
			*Candidate.RoutingValidationStatus,
			*Candidate.RoutingValidationDetail,
			*Candidate.SuggestedNetworkId.ToString(),
			*Candidate.SuggestedSubNetworkId.ToString());
		Diagnostics.Add(MoveTemp(Diagnostic));
	}

	Diagnostics.Sort([](
		const Flight::Orchestration::FFlightOrchestrationDiagnostic& Left,
		const Flight::Orchestration::FFlightOrchestrationDiagnostic& Right)
	{
		if (Left.Category == Right.Category)
		{
			return Left.SourceName.LexicalLess(Right.SourceName);
		}

		return Left.Category.LexicalLess(Right.Category);
	});
}

void UFlightOrchestrationSubsystem::RebuildCachedReport()
{
	CachedReport = Flight::Orchestration::FFlightOrchestrationReport();
	CachedReport.WorldName = GetWorld() ? GetWorld()->GetName() : FString();
	CachedReport.BuiltAtUtc = FDateTime::UtcNow();
	CachedReport.Services = Services;
	CachedReport.MissingContracts = MissingContracts;
	CachedReport.Diagnostics = Diagnostics;
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

	CachedReport.NavigationCandidates = Flight::Orchestration::BuildNavigationCandidateRecords(GetWorld(), ParticipantsByHandle);

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
