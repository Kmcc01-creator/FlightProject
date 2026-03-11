#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Orchestration/FlightParticipantTypes.h"

#include "FlightParticipantAdapter.generated.h"

UINTERFACE()
class FLIGHTPROJECT_API UFlightParticipantAdapter : public UInterface
{
	GENERATED_BODY()
};

/**
 * Capability interface for actor-backed surfaces that lower into
 * orchestration participant records.
 */
class FLIGHTPROJECT_API IFlightParticipantAdapter
{
	GENERATED_BODY()

public:
	virtual bool BuildParticipantRecord(Flight::Orchestration::FFlightParticipantRecord& OutRecord) const = 0;
};
