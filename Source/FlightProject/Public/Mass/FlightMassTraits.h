#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "FlightMassFragments.h"
#include "FlightMassTraits.generated.h"

struct FFlightAutopilotConfigRow;

/**
 * Trait that adds path-following capability to a Mass entity.
 * Configure in a MassEntityConfigAsset to create autonomous drones.
 */
UCLASS(meta = (DisplayName = "Flight Path Follow"))
class FLIGHTPROJECT_API UFlightPathFollowTrait : public UMassEntityTraitBase
{
    GENERATED_BODY()

public:
    /** Default speed in cm/s */
    UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0"))
    float DefaultSpeed = 1500.f;

    /** Whether to loop when reaching path end */
    UPROPERTY(EditAnywhere, Category = "Flight")
    bool bLoopPath = true;

protected:
    virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

/**
 * Trait that adds visual/rendering properties to a Mass entity.
 * Required for entities that will have Actor representation at LOD0.
 */
UCLASS(meta = (DisplayName = "Flight Visual"))
class FLIGHTPROJECT_API UFlightVisualTrait : public UMassEntityTraitBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Visual")
    FLinearColor LightColor = FLinearColor(0.588f, 0.784f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Visual", meta = (ClampMin = "0"))
    float LightIntensity = 8000.f;

    UPROPERTY(EditAnywhere, Category = "Visual", meta = (ClampMin = "0"))
    float LightRadius = 1500.f;

    UPROPERTY(EditAnywhere, Category = "Visual")
    float LightHeightOffset = 250.f;

    UPROPERTY(EditAnywhere, Category = "Visual")
    bool bUseInverseSquaredFalloff = false;

protected:
    virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

/**
 * Trait that marks an entity as a swarm member.
 * Enables batch operations on all swarm entities.
 */
UCLASS(meta = (DisplayName = "Flight Swarm Member"))
class FLIGHTPROJECT_API UFlightSwarmMemberTrait : public UMassEntityTraitBase
{
    GENERATED_BODY()

protected:
    virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
