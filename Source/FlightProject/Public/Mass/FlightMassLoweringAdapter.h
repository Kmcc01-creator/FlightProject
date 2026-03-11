#pragma once

#include "CoreMinimal.h"
#include "Core/FlightSchemaProviderAdapter.h"
#include "Mass/FlightMassFragments.h"
#include "Orchestration/FlightParticipantTypes.h"
#include "UObject/Interface.h"

#include "FlightMassLoweringAdapter.generated.h"

namespace Flight::Mass
{

struct FLIGHTPROJECT_API FFlightMassSharedFragmentPlan
{
	bool bHasBehaviorCohort = false;
	FFlightBehaviorCohortFragment BehaviorCohort;
};

struct FLIGHTPROJECT_API FFlightMassOrchestrationBatchMetadata
{
	bool bHasCohortRecord = false;
	Flight::Orchestration::FFlightCohortRecord Cohort;
};

struct FLIGHTPROJECT_API FFlightMassBatchLoweringPlan
{
	FName AdapterName = NAME_None;
	FName CohortName = NAME_None;
	int32 BatchCount = 0;
	float PhaseOffsetDeg = 0.0f;
	float PhaseSpreadDeg = 360.0f;
	float DesiredSpeed = -1.0f;
	bool bLooping = true;
	FName DesiredNavigationNetwork = NAME_None;
	FName DesiredNavigationSubNetwork = NAME_None;
	int32 PreferredBehaviorId = -1;
	TArray<uint32> AllowedBehaviorIds;
	TArray<uint32> DeniedBehaviorIds;
	TArray<FName> RequiredBehaviorContracts;
	Flight::Adapters::FFlightSchemaProviderDescriptor SchemaDescriptor;
	FFlightMassSharedFragmentPlan SharedFragments;
	FFlightMassOrchestrationBatchMetadata Orchestration;
};

} // namespace Flight::Mass

UINTERFACE()
class FLIGHTPROJECT_API UFlightMassLoweringAdapter : public UInterface
{
	GENERATED_BODY()
};

class FLIGHTPROJECT_API IFlightMassLoweringAdapter
{
	GENERATED_BODY()

public:
	virtual bool BuildMassBatchLoweringPlan(Flight::Mass::FFlightMassBatchLoweringPlan& OutPlan) const = 0;
};
