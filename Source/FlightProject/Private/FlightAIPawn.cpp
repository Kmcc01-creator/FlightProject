#include "FlightAIPawn.h"

#include "FlightDataTypes.h"
#include "FlightVehiclePawn.h"
#include "FlightWaypointPath.h"
#include "Components/SplineComponent.h"

AFlightAIPawn::AFlightAIPawn()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AFlightAIPawn::BeginPlay()
{
    Super::BeginPlay();
    SetAutopilotEnabled(true);
}

void AFlightAIPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AdvanceAlongPath(DeltaSeconds);
}

void AFlightAIPawn::SetFlightPath(AFlightWaypointPath* InPath, float InitialDistance, bool bLoop)
{
    FlightPath = InPath;
    CurrentDistance = InitialDistance;
    bLoopPath = bLoop;

    if (FlightPath.IsValid())
    {
        const FTransform TargetTransform = FlightPath->GetTransformAtDistance(CurrentDistance);
        SetActorLocationAndRotation(TargetTransform.GetLocation(), TargetTransform.GetRotation());
    }
}

void AFlightAIPawn::AdvanceAlongPath(float DeltaSeconds)
{
    if (!FlightPath.IsValid())
    {
        return;
    }

    const USplineComponent* Spline = FlightPath->GetSplineComponent();
    if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
    {
        return;
    }

    const float PathLength = Spline->GetSplineLength();
    if (PathLength <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    CurrentDistance += AutopilotSpeed * DeltaSeconds;

    if (bLoopPath)
    {
        CurrentDistance = FMath::Fmod(CurrentDistance, PathLength);
        if (CurrentDistance < 0.f)
        {
            CurrentDistance += PathLength;
        }
    }
    else
    {
        CurrentDistance = FMath::Clamp(CurrentDistance, 0.f, PathLength);
    }

    const FTransform TargetTransform = FlightPath->GetTransformAtDistance(CurrentDistance);
    SetActorLocationAndRotation(TargetTransform.GetLocation(), TargetTransform.GetRotation());
}

void AFlightAIPawn::SetAutopilotSpeed(float Speed)
{
    AutopilotSpeed = FMath::Max(0.f, Speed);
}

void AFlightAIPawn::ApplyAutopilotConfig(const FFlightAutopilotConfigRow& Config)
{
    SetAutopilotSpeed(Config.DroneSpeed);
    ApplyNavigationLightConfig(
        Config.NavigationLightIntensity,
        Config.NavigationLightRadius,
        Config.GetNavigationLightColor(),
        Config.UseInverseSquaredFalloff(),
        Config.NavigationLightHeightOffset);
}
