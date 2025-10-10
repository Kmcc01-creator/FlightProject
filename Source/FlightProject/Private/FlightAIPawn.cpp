#include "FlightAIPawn.h"

#include "FlightDataTypes.h"
#include "FlightVehiclePawn.h"
#include "FlightWaypointPath.h"
#include "Components/SplineComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightAIPawn, Log, All);

AFlightAIPawn::AFlightAIPawn()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AFlightAIPawn::BeginPlay()
{
    Super::BeginPlay();
    SetAutopilotEnabled(true);
    UE_LOG(LogFlightAIPawn, Log, TEXT("%s initialized; autopilot enabled."), *GetName());
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
    bLoggedMissingFlightPath = false;
    bLoggedInvalidSpline = false;
    bLoggedZeroLengthPath = false;

    if (FlightPath.IsValid())
    {
        const FTransform TargetTransform = FlightPath->GetTransformAtDistance(CurrentDistance);
        SetActorLocationAndRotation(TargetTransform.GetLocation(), TargetTransform.GetRotation());
        UE_LOG(LogFlightAIPawn, Log, TEXT("%s assigned flight path %s (InitialDistance=%.1f, Loop=%s)."),
            *GetName(),
            *GetNameSafe(FlightPath.Get()),
            CurrentDistance,
            bLoopPath ? TEXT("true") : TEXT("false"));
    }
    else
    {
        UE_LOG(LogFlightAIPawn, Warning, TEXT("%s received an invalid flight path handle; awaiting valid assignment."), *GetName());
    }
}

void AFlightAIPawn::AdvanceAlongPath(float DeltaSeconds)
{
    if (!FlightPath.IsValid())
    {
        if (!bLoggedMissingFlightPath)
        {
            UE_LOG(LogFlightAIPawn, Warning, TEXT("%s cannot advance: flight path no longer valid."), *GetName());
            bLoggedMissingFlightPath = true;
        }
        return;
    }

    const USplineComponent* Spline = FlightPath->GetSplineComponent();
    if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
    {
        if (!bLoggedInvalidSpline)
        {
            UE_LOG(LogFlightAIPawn, Warning, TEXT("%s cannot advance: spline data missing or insufficient on path %s."),
                *GetName(),
                *GetNameSafe(FlightPath.Get()));
            bLoggedInvalidSpline = true;
        }
        return;
    }

    const float PathLength = Spline->GetSplineLength();
    if (PathLength <= KINDA_SMALL_NUMBER)
    {
        if (!bLoggedZeroLengthPath)
        {
            UE_LOG(LogFlightAIPawn, Warning, TEXT("%s cannot advance: path %s has zero length."), *GetName(), *GetNameSafe(FlightPath.Get()));
            bLoggedZeroLengthPath = true;
        }
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
    UE_LOG(LogFlightAIPawn, Log, TEXT("%s autopilot speed set to %.1f uu/s."), *GetName(), AutopilotSpeed);
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
    UE_LOG(LogFlightAIPawn, Log, TEXT("%s applied autopilot config: Speed=%.1f, NavLightIntensity=%.1f, Radius=%.1f, Loop=%s."),
        *GetName(),
        Config.DroneSpeed,
        Config.NavigationLightIntensity,
        Config.NavigationLightRadius,
        Config.ShouldLoopPath() ? TEXT("true") : TEXT("false"));
}
