#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "FlightProjectDeveloperSettings.generated.h"

class UDataTable;

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

    /** Data table containing lighting configuration (row type: FFlightLightingConfigRow) */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightLightingConfigRow"))
    TSoftObjectPtr<UDataTable> LightingConfigTable;

    /** Row name to use from the lighting config table */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName LightingConfigRow;

    /** Data table containing autopilot configuration (row type: FFlightAutopilotConfigRow) */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightAutopilotConfigRow"))
    TSoftObjectPtr<UDataTable> AutopilotConfigTable;

    /** Row name to use from the autopilot config table */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName AutopilotConfigRow;

    /** Data table containing spatial layout entities (row type: FFlightSpatialLayoutRow) */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightSpatialLayoutRow"))
    TSoftObjectPtr<UDataTable> SpatialLayoutTable;

    /** Filter spatial layout rows to this scenario (empty = all rows) */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName SpatialLayoutScenario;

    /** Data table containing procedural anchor overrides (row type: FFlightProceduralAnchorRow) */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightProceduralAnchorRow"))
    TSoftObjectPtr<UDataTable> ProceduralAnchorTable;
};
