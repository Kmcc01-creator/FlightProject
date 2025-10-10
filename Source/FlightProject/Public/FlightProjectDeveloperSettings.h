#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FlightProjectDeveloperSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta = (DisplayName = "Flight Project"))
class FLIGHTPROJECT_API UFlightProjectDeveloperSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UFlightProjectDeveloperSettings();

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Mass")
    int32 DefaultMassBatchSize;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|GPU")
    FString ComputeShaderDirectory;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Simulation")
    float DefaultTargetAltitude;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Simulation")
    float LowAltitudeThreshold;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Simulation")
    float HighAltitudeThreshold;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RelativeToProjectContentDir))
    FString LightingConfigPath;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName LightingConfigRow;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RelativeToProjectContentDir))
    FString AutopilotConfigPath;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName AutopilotConfigRow;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RelativeToProjectContentDir))
    FString SpatialLayoutConfigPath;

    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName SpatialLayoutScenario;
};
