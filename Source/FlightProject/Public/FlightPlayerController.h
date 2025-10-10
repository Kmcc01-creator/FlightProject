#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FlightPlayerController.generated.h"

class UInputMappingContext;

UCLASS()
class FLIGHTPROJECT_API AFlightPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AFlightPlayerController();

    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

protected:
    void ToggleMassDebugger();
    void SnapshotMassProfiler();

    UPROPERTY(EditDefaultsOnly, Category = "Flight|Input")
    TObjectPtr<UInputMappingContext> PlayerMappingContext;
};
