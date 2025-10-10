#include "FlightGameState.h"

AFlightGameState::AFlightGameState()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AFlightGameState::RecordAltitudeSample(float AltitudeMeters)
{
    AltitudeSampleCount++;
    const float SampleAlpha = 1.f / static_cast<float>(AltitudeSampleCount);
    AverageAltitude = FMath::Lerp(AverageAltitude, AltitudeMeters, SampleAlpha);
}
