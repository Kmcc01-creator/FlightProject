#include "Mass/FlightPathFollowProcessor.h"
#include "Mass/FlightMassFragments.h"
#include "Mass/FlightWaypointPathRegistry.h"
#include "MassExecutionContext.h"
#include "MassEntitySubsystem.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassProcessingTypes.h" 

//-----------------------------------------------------------------------------
// UFlightPathFollowProcessor
//-----------------------------------------------------------------------------

UFlightPathFollowProcessor::UFlightPathFollowProcessor()
{
    ProcessingPhase = EMassProcessingPhase::PrePhysics;
    bAutoRegisterWithProcessingPhases = true;
    RegisterQuery(EntityQuery);  // Must register here so query is initialized before ConfigureQueries
}

void UFlightPathFollowProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FFlightPathFollowFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FFlightTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UFlightPathFollowProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    // Get the path registry for lookups
    UFlightWaypointPathRegistry* PathRegistry = GetWorld()->GetSubsystem<UFlightWaypointPathRegistry>();
    if (!PathRegistry)
    {
        return;
    }

    const float DeltaTime = Context.GetDeltaTimeSeconds();

    // Removed EntityManager arg per deprecation warning
    EntityQuery.ForEachEntityChunk(Context,
        [PathRegistry, DeltaTime](FMassExecutionContext& Context)
    {
        const TArrayView<FFlightPathFollowFragment> PathFragments =
            Context.GetMutableFragmentView<FFlightPathFollowFragment>();
        const TArrayView<FFlightTransformFragment> TransformFragments =
            Context.GetMutableFragmentView<FFlightTransformFragment>();

        const int32 NumEntities = Context.GetNumEntities();

        for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
        {
            FFlightPathFollowFragment& PathData = PathFragments[EntityIndex];
            FFlightTransformFragment& Transform = TransformFragments[EntityIndex];

            // Lookup path data (cached LUT, no UObject dereference)
            const FFlightPathData* Path = PathRegistry->FindPath(PathData.PathId);
            if (!Path || Path->TotalLength <= 0.f)
            {
                continue;
            }

            // Advance distance along path
            PathData.CurrentDistance += PathData.DesiredSpeed * DeltaTime;

            // Handle path boundaries
            if (PathData.CurrentDistance >= Path->TotalLength)
            {
                if (PathData.bLooping)
                {
                    PathData.CurrentDistance = FMath::Fmod(PathData.CurrentDistance, Path->TotalLength);
                }
                else
                {
                    PathData.CurrentDistance = Path->TotalLength;
                }
            }
            else if (PathData.CurrentDistance < 0.f)
            {
                if (PathData.bLooping)
                {
                    PathData.CurrentDistance = Path->TotalLength + FMath::Fmod(PathData.CurrentDistance, Path->TotalLength);
                }
                else
                {
                    PathData.CurrentDistance = 0.f;
                }
            }

            // Sample path LUT for new transform
            Transform.Location = Path->SampleLocation(PathData.CurrentDistance);
            Transform.Rotation = Path->SampleRotation(PathData.CurrentDistance);

            const FVector Tangent = Path->SampleTangent(PathData.CurrentDistance);
            Transform.Velocity = Tangent * PathData.DesiredSpeed;
        }
    });
}

//-----------------------------------------------------------------------------
// UFlightTransformSyncProcessor
//-----------------------------------------------------------------------------

UFlightTransformSyncProcessor::UFlightTransformSyncProcessor()
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    bAutoRegisterWithProcessingPhases = true;
    RegisterQuery(EntityQuery);  // Must register here so query is initialized before ConfigureQueries
}

void UFlightTransformSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FFlightTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
}

void UFlightTransformSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    EntityQuery.ForEachEntityChunk(Context,
        [](FMassExecutionContext& Context)
    {
        const TConstArrayView<FFlightTransformFragment> TransformFragments =
            Context.GetFragmentView<FFlightTransformFragment>();
        const TArrayView<FMassActorFragment> ActorFragments =
            Context.GetMutableFragmentView<FMassActorFragment>();

        const int32 NumEntities = Context.GetNumEntities();

        for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
        {
            const FFlightTransformFragment& Transform = TransformFragments[EntityIndex];
            FMassActorFragment& ActorFragment = ActorFragments[EntityIndex];

            // Access Actor safely
            if (AActor* Actor = ActorFragment.GetMutable())
            {
                Actor->SetActorLocationAndRotation(
                    Transform.Location,
                    Transform.Rotation,
                    false, 
                    nullptr, 
                    ETeleportType::TeleportPhysics
                );
            }
        }
    });
}