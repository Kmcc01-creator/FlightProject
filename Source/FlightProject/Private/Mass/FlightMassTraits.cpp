#include "Mass/FlightMassTraits.h"
#include "MassEntityTemplateRegistry.h"

void UFlightPathFollowTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
    // Add transform fragment (required for all spatial entities)
    BuildContext.AddFragment<FFlightTransformFragment>();

    // Add path follow fragment with configured defaults
    FFlightPathFollowFragment& PathFragment = BuildContext.AddFragment_GetRef<FFlightPathFollowFragment>();
    PathFragment.DesiredSpeed = DefaultSpeed;
    PathFragment.bLooping = bLoopPath;
}

void UFlightVisualTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
    FFlightVisualFragment& VisualFragment = BuildContext.AddFragment_GetRef<FFlightVisualFragment>();
    VisualFragment.LightColor = LightColor;
    VisualFragment.LightIntensity = LightIntensity;
    VisualFragment.LightRadius = LightRadius;
    VisualFragment.LightHeightOffset = LightHeightOffset;
    VisualFragment.bUseInverseSquaredFalloff = bUseInverseSquaredFalloff;
}

void UFlightSwarmMemberTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
    BuildContext.AddTag<FFlightSwarmMemberTag>();
}
