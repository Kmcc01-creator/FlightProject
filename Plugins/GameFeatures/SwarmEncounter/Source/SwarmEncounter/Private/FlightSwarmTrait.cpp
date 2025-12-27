#include "FlightSwarmTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "FlightProject/Public/Mass/FlightMassFragments.h"

void UFlightSwarmTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Add the necessary data fragments
	BuildContext.AddFragment<FFlightTransformFragment>();
	BuildContext.AddFragment<FFlightPathFollowFragment>();
	BuildContext.AddFragment<FFlightVisualFragment>();

	// Add the tag so Processors can find these specific entities
	BuildContext.AddTag<FFlightSwarmMemberTag>();

	// Note: We do NOT add FFlightSharedPathFragment here.
	// Shared fragments are usually added by the Spawner or a specialized processor
	// because they depend on runtime context (which path specifically?).
}
