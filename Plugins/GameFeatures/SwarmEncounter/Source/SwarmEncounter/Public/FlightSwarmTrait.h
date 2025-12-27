#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "FlightSwarmTrait.generated.h"

/**
 * Trait that adds autonomous flight capabilities to a Mass entity.
 * Adds: Transform, PathFollowing, Visual fragments, and the SwarmMember tag.
 */
UCLASS(meta = (DisplayName = "Flight Swarm Drone"))
class SWARMENCOUNTER_API UFlightSwarmTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
