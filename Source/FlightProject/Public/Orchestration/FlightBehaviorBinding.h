// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"

namespace Flight::Orchestration
{

enum class EFlightExecutionDomain : uint8
{
	Unknown,
	NativeCpu,
	TaskGraph,
	VerseVm,
	Simd,
	Gpu
};

enum class EFlightBehaviorBindingSelectionSource : uint8
{
	None,
	ManualBinding,
	AutomaticSelection,
	GeneratedFallback,
	VerseFallback
};

enum class EFlightBehaviorBindingSelectionRule : uint8
{
	None,
	ExplicitRegistration,
	LowestExecutableBehaviorId,
	PreferredBehaviorId
};

struct FLIGHTPROJECT_API FFlightBehaviorRecord
{
	uint32 BehaviorID = 0;
	FName Name = NAME_None;
	FString CompileState;
	bool bIsComposite = false;
	FString CompositeOperator;
	TArray<uint32> ChildBehaviorIds;
	float ExecutionRateHz = 0.0f;
	uint32 FrameInterval = 1;
	bool bAsync = false;
	bool bExecutable = false;
	EFlightExecutionDomain ResolvedDomain = EFlightExecutionDomain::Unknown;
	TArray<FName> RequiredContracts;
	TArray<FString> LegalLanes;
	FString PreferredLane;
	FString CommittedLane;
	FString ExecutionShape;
	FString CommittedExecutionShape;
	bool bAllowsMixedLaneExecution = false;
	bool bRequiresSharedTypeKey = false;
	TArray<FString> DisallowedLaneReasons;
	TArray<FString> GuardSummaries;
	FString SelectedBackend;
	FString CommittedBackend;
	FString CommitDetail;
	bool bHasLastExecutionResult = false;
	bool bLastExecutionSucceeded = false;
	bool bLastExecutionCommitted = false;
	uint32 LastSelectedChildBehaviorId = 0;
	TArray<FString> LastBranchEvidence;
	TArray<FString> LastGuardOutcomes;
	FString LastExecutionDetail;
	FString SelectedPolicyRow;
	FString PolicyPreferredDomain;
	TArray<FString> ImportedSymbols;
	TArray<FString> ExportedSymbols;
	TArray<FString> FragmentRequirementSummaries;
	TArray<FString> VectorLegalitySummaries;
	int32 BoundaryOperatorCount = 0;
	bool bHasBoundarySemantics = false;
	bool bBoundarySemanticsExecutable = true;
	bool bHasAwaitableBoundary = false;
	bool bHasMirrorRequest = false;
	FString BoundaryExecutionDetail;
	FString Diagnostics;
};

struct FLIGHTPROJECT_API FFlightBehaviorBinding
{
	FLIGHT_REFLECT_BODY(FFlightBehaviorBinding);

	struct FLIGHTPROJECT_API FIdentity
	{
		FName CohortName = NAME_None;
		uint32 BehaviorID = 0;

		FLIGHT_REFLECT_BODY(FIdentity);

		bool IsValid() const
		{
			return !CohortName.IsNone() && BehaviorID != 0;
		}
	};

	struct FLIGHTPROJECT_API FReport
	{
		struct FLIGHTPROJECT_API FSelectionProvenance
		{
			EFlightBehaviorBindingSelectionSource Source = EFlightBehaviorBindingSelectionSource::None;
			EFlightBehaviorBindingSelectionRule Rule = EFlightBehaviorBindingSelectionRule::None;
			FName RequestedCohortName = NAME_None;
			FName FallbackCohortName = NAME_None;
			bool bUsedDefaultCohortFallback = false;

			FLIGHT_REFLECT_BODY(FSelectionProvenance);

			bool HasSelectionSource() const
			{
				return Source != EFlightBehaviorBindingSelectionSource::None;
			}

			void ApplyDefaultCohortFallback(const FName InRequestedCohortName, const FName InFallbackCohortName)
			{
				bUsedDefaultCohortFallback = true;
				RequestedCohortName = InRequestedCohortName;
				FallbackCohortName = InFallbackCohortName;
			}
		};

		FIdentity Identity;
		EFlightExecutionDomain ExecutionDomain = EFlightExecutionDomain::Unknown;
		uint32 FrameInterval = 1;
		bool bAsync = false;
		TArray<FName> RequiredContracts;
		FSelectionProvenance Selection;

		FLIGHT_REFLECT_BODY(FReport);
	};

	FIdentity Identity;
	FReport Report;

	bool IsValid() const
	{
		return Identity.IsValid();
	}

	void SyncReportIdentity()
	{
		Report.Identity = Identity;
	}

	FName GetCohortName() const
	{
		return Identity.CohortName;
	}

	uint32 GetBehaviorID() const
	{
		return Identity.BehaviorID;
	}

	EFlightExecutionDomain GetExecutionDomain() const
	{
		return Report.ExecutionDomain;
	}

	uint32 GetFrameInterval() const
	{
		return Report.FrameInterval;
	}

	bool IsAsync() const
	{
		return Report.bAsync;
	}

	const TArray<FName>& GetRequiredContracts() const
	{
		return Report.RequiredContracts;
	}
};

} // namespace Flight::Orchestration

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FIdentity,
	FLIGHT_FIELD_ATTR(FName, CohortName),
	FLIGHT_FIELD_ATTR(uint32, BehaviorID)
)

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FReport,
	FLIGHT_FIELD_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FIdentity, Identity),
	FLIGHT_FIELD_ATTR(Flight::Orchestration::EFlightExecutionDomain, ExecutionDomain),
	FLIGHT_FIELD_ATTR(uint32, FrameInterval),
	FLIGHT_FIELD_ATTR(bool, bAsync),
	FLIGHT_FIELD_ATTR(TArray<FName>, RequiredContracts),
	FLIGHT_FIELD_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FReport::FSelectionProvenance, Selection)
)

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FReport::FSelectionProvenance,
	FLIGHT_FIELD_ATTR(Flight::Orchestration::EFlightBehaviorBindingSelectionSource, Source),
	FLIGHT_FIELD_ATTR(Flight::Orchestration::EFlightBehaviorBindingSelectionRule, Rule),
	FLIGHT_FIELD_ATTR(FName, RequestedCohortName),
	FLIGHT_FIELD_ATTR(FName, FallbackCohortName),
	FLIGHT_FIELD_ATTR(bool, bUsedDefaultCohortFallback)
)

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Orchestration::FFlightBehaviorBinding,
	FLIGHT_FIELD_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FIdentity, Identity),
	FLIGHT_FIELD_ATTR(Flight::Orchestration::FFlightBehaviorBinding::FReport, Report)
)

} // namespace Flight::Reflection
