#include "FlightNetworkManager.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"

AFlightNetworkManager::AFlightNetworkManager(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bUseDistanceBasedRelevancy = true;
    MaxClientSmoothingDeltaTime = 0.25f;
}

void AFlightNetworkManager::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    if (UWorld* World = GetWorld())
    {
        if (UNetDriver* NetDriver = World->GetNetDriver())
        {
            ApplyNetDriverOverrides(*NetDriver);
        }
    }
}

void AFlightNetworkManager::ApplyNetDriverOverrides(UNetDriver& NetDriver) const
{
    NetDriver.MaxClientRate = FMath::Max<int32>(NetDriver.MaxClientRate, 80000);
    NetDriver.MaxInternetClientRate = FMath::Max<int32>(NetDriver.MaxInternetClientRate, 80000);
    if (NetDriver.GetNetServerMaxTickRate() < 120)
    {
        NetDriver.SetNetServerMaxTickRate(120);
    }
}
