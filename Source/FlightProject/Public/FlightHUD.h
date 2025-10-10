#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FlightHUD.generated.h"

UCLASS()
class FLIGHTPROJECT_API AFlightHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

protected:
    void DrawFlightStats();
};
