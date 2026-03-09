// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlightGameMode.h"
#include "FlightStartupProfile.generated.h"

/**
 * Thin policy asset consumed by FlightGameMode.
 * This asset should describe startup policy, not implement bootstrap logic.
 */
UCLASS(BlueprintType)
class FLIGHTPROJECT_API UFlightStartupProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Startup policy chosen for worlds that use this profile asset. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Startup")
    EFlightStartupProfile StartupProfile = EFlightStartupProfile::DefaultSandbox;

    /** Entity count used by the Gauntlet GPU swarm startup path. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Startup", meta = (ClampMin = "1", UIMin = "1"))
    int32 GauntletGpuSwarmEntityCount = 100000;

    /** Optional note for describing intended use of the profile. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Startup")
    FString Description;
};

