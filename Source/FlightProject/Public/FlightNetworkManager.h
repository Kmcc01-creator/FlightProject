#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameNetworkManager.h"
#include "FlightNetworkManager.generated.h"

class UNetDriver;

UCLASS(Config=Game)
class FLIGHTPROJECT_API AFlightNetworkManager : public AGameNetworkManager
{
    GENERATED_BODY()

public:
    AFlightNetworkManager(const FObjectInitializer& ObjectInitializer);

    virtual void PostInitializeComponents() override;

private:
    void ApplyNetDriverOverrides(UNetDriver& NetDriver) const;
};
