#include "FlightHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FlightGameState.h"
#include "FlightVehiclePawn.h"

void AFlightHUD::DrawHUD()
{
    Super::DrawHUD();
    DrawFlightStats();
}

void AFlightHUD::DrawFlightStats()
{
    if (!Canvas)
    {
        return;
    }

    const AFlightVehiclePawn* FlightPawn = Cast<AFlightVehiclePawn>(GetOwningPawn());
    const AFlightGameState* FlightState = GetWorld() ? GetWorld()->GetGameState<AFlightGameState>() : nullptr;

    float Altitude = 0.f;
    if (FlightPawn)
    {
        Altitude = FlightPawn->GetHeightAboveGroundMeters();
    }

    const float AverageAltitude = FlightState ? FlightState->GetAverageAltitudeMeters() : Altitude;

    const FString AltitudeText = FString::Printf(TEXT("ALT: %0.0f m"), Altitude);
    const FString AverageAltitudeText = FString::Printf(TEXT("AVG ALT: %0.0f m"), AverageAltitude);

    FVector2D ScreenPos(50.f, 50.f);
    FCanvasTextItem AltitudeItem(ScreenPos, FText::FromString(AltitudeText), GEngine->GetSmallFont(), FLinearColor::Green);
    Canvas->DrawItem(AltitudeItem);

    ScreenPos.Y += 24.f;
    FCanvasTextItem AverageItem(ScreenPos, FText::FromString(AverageAltitudeText), GEngine->GetSmallFont(), FLinearColor::Yellow);
    Canvas->DrawItem(AverageItem);
}
