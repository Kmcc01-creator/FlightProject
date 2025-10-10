#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FlightGameMode.generated.h"

class AFlightWaypointPath;
struct FFlightAutopilotConfigRow;

UCLASS()
class FLIGHTPROJECT_API AFlightGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFlightGameMode();

    virtual void StartPlay() override;

protected:
    void InitializeMassRuntime();

    void SetupNightEnvironment();
    void BuildSpatialTestRange();
    void SpawnAutonomousFlights();

private:
    AFlightWaypointPath* FindOrCreateWaypointPath(const FFlightAutopilotConfigRow* AutopilotConfig);
};
