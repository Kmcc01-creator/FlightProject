#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightWaypointPath.generated.h"

class USplineComponent;
struct FFlightAutopilotConfigRow;

/**
 * Authorable spline used to drive autonomous flight paths.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightWaypointPath : public AActor
{
    GENERATED_BODY()

public:
    AFlightWaypointPath();

    virtual void OnConstruction(const FTransform& Transform) override;

    USplineComponent* GetSplineComponent() const { return FlightSpline; }

    float GetPathLength() const;
    FTransform GetTransformAtDistance(float Distance) const;
    FVector GetLocationAtNormalizedPosition(float Alpha) const;

    void EnsureDefaultLoop();
    void ConfigureFromAutopilotConfig(const FFlightAutopilotConfigRow& Config);

private:
    UPROPERTY(VisibleAnywhere, Category = "Flight|Path")
    USplineComponent* FlightSpline;

    UPROPERTY(EditAnywhere, Category = "Flight|Path")
    float DefaultRadius = 3500.f;

    UPROPERTY(EditAnywhere, Category = "Flight|Path")
    float DefaultAltitude = 1200.f;

    void BuildDefaultLoop();
    void BuildLoop(float Radius, float Altitude);
};
