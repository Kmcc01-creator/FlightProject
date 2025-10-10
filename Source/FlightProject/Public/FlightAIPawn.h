#pragma once

#include "CoreMinimal.h"
#include "FlightVehiclePawn.h"
#include "FlightAIPawn.generated.h"

class AFlightWaypointPath;
struct FFlightAutopilotConfigRow;

/**
 * Autonomous flight pawn that follows a waypoint path.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightAIPawn : public AFlightVehiclePawn
{
    GENERATED_BODY()

public:
    AFlightAIPawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    void SetFlightPath(AFlightWaypointPath* InPath, float InitialDistance, bool bLoop);

    void SetAutopilotSpeed(float Speed);
    void ApplyAutopilotConfig(const FFlightAutopilotConfigRow& Config);

protected:
    UPROPERTY(EditAnywhere, Category = "Flight|AI")
    float AutopilotSpeed = 1500.f;

    UPROPERTY(EditAnywhere, Category = "Flight|AI")
    bool bLoopPath = true;

private:
    UPROPERTY()
    TWeakObjectPtr<AFlightWaypointPath> FlightPath;

    float CurrentDistance = 0.f;
    bool bLoggedMissingFlightPath = false;
    bool bLoggedInvalidSpline = false;
    bool bLoggedZeroLengthPath = false;

    void AdvanceAlongPath(float DeltaSeconds);
};
