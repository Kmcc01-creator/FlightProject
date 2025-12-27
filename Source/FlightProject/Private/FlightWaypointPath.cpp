#include "FlightWaypointPath.h"

#include "FlightDataTypes.h"
#include "Mass/FlightWaypointPathRegistry.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"

AFlightWaypointPath::AFlightWaypointPath()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    FlightSpline = CreateDefaultSubobject<USplineComponent>(TEXT("FlightSpline"));
    FlightSpline->SetupAttachment(RootComponent);
    FlightSpline->SetClosedLoop(true);
    FlightSpline->SetDrawDebug(true);
}

void AFlightWaypointPath::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (FlightSpline && FlightSpline->GetNumberOfSplinePoints() == 0)
    {
        BuildDefaultLoop();
    }
}

void AFlightWaypointPath::BeginPlay()
{
    Super::BeginPlay();

    if (UFlightWaypointPathRegistry* Registry = GetWorld()->GetSubsystem<UFlightWaypointPathRegistry>())
    {
        PathId = Registry->RegisterPath(this);
    }
}

void AFlightWaypointPath::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (PathId.IsValid())
    {
        if (UFlightWaypointPathRegistry* Registry = GetWorld()->GetSubsystem<UFlightWaypointPathRegistry>())
        {
            Registry->UnregisterPath(PathId);
        }
    }

    Super::EndPlay(EndPlayReason);
}

float AFlightWaypointPath::GetPathLength() const
{
    return FlightSpline ? FlightSpline->GetSplineLength() : 0.f;
}

FTransform AFlightWaypointPath::GetTransformAtDistance(float Distance) const
{
    if (!FlightSpline || FlightSpline->GetNumberOfSplinePoints() == 0)
    {
        return FTransform(GetActorRotation(), GetActorLocation());
    }

    const FVector Location = FlightSpline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    const FRotator Rotation = FlightSpline->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    return FTransform(Rotation, Location);
}

FVector AFlightWaypointPath::GetLocationAtNormalizedPosition(float Alpha) const
{
    if (!FlightSpline || FlightSpline->GetNumberOfSplinePoints() == 0)
    {
        return GetActorLocation();
    }

    const float PathLength = FlightSpline->GetSplineLength();
    const float Distance = FMath::Clamp(Alpha, 0.f, 1.f) * PathLength;
    return FlightSpline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

void AFlightWaypointPath::EnsureDefaultLoop()
{
    if (FlightSpline && FlightSpline->GetNumberOfSplinePoints() == 0)
    {
        BuildDefaultLoop();
    }
}

void AFlightWaypointPath::BuildDefaultLoop()
{
    BuildLoop(DefaultRadius, DefaultAltitude);
}

void AFlightWaypointPath::BuildLoop(float Radius, float Altitude)
{
    if (!FlightSpline)
    {
        return;
    }

    FlightSpline->ClearSplinePoints(false);

    const FVector Center = GetActorLocation();

    const TArray<FVector> Points = {
        Center + FVector(Radius, 0.f, Altitude),
        Center + FVector(0.f, Radius, Altitude * 0.8f),
        Center + FVector(-Radius, 0.f, Altitude * 0.9f),
        Center + FVector(0.f, -Radius, Altitude * 1.1f)
    };

    for (const FVector& Point : Points)
    {
        FlightSpline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
    }

    FlightSpline->SetClosedLoop(true);
    FlightSpline->UpdateSpline();
}

void AFlightWaypointPath::ConfigureFromAutopilotConfig(const FFlightAutopilotConfigRow& Config)
{
    DefaultRadius = Config.PathRadius;
    DefaultAltitude = Config.BaseAltitude;
    BuildLoop(DefaultRadius, DefaultAltitude);
}