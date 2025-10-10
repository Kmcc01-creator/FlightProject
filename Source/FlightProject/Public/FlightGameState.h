#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FlightGameState.generated.h"

UCLASS()
class FLIGHTPROJECT_API AFlightGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AFlightGameState();

    void RecordAltitudeSample(float AltitudeMeters);

    UFUNCTION(BlueprintCallable, Category = "Flight")
    float GetAverageAltitudeMeters() const { return AverageAltitude; }

    UFUNCTION(BlueprintCallable, Category = "Flight")
    int32 GetSampleCount() const { return AltitudeSampleCount; }

private:
    UPROPERTY(VisibleAnywhere, Category = "Flight")
    float AverageAltitude = 0.f;

    UPROPERTY(VisibleAnywhere, Category = "Flight")
    int32 AltitudeSampleCount = 0;
};
