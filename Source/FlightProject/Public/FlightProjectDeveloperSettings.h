#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "FlightDataSources.h"
#include "FlightProjectDeveloperSettings.generated.h"

class UDataTable;

UCLASS(Config=Game, DefaultConfig, meta = (DisplayName = "Flight Project"))
class FLIGHTPROJECT_API UFlightProjectDeveloperSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UFlightProjectDeveloperSettings();

    FFlightDataRowSource GetLightingConfigSource() const;
    FFlightDataRowSource GetAutopilotConfigSource() const;
    FFlightDataTableSource GetSpatialLayoutSource() const;
    FFlightDataTableSource GetProceduralAnchorSource() const;
    FFlightDataTableSource GetBehaviorCompilePolicySource() const;

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

    /** Current default ingress asset for lighting configuration (row type: FFlightLightingConfigRow). */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightLightingConfigRow"))
    TSoftObjectPtr<UDataTable> LightingConfigTable;

    /** Selected lighting contract row in the current default ingress asset. */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName LightingConfigRow;

    /** Current default ingress asset for autopilot configuration (row type: FFlightAutopilotConfigRow). */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightAutopilotConfigRow"))
    TSoftObjectPtr<UDataTable> AutopilotConfigTable;

    /** Selected autopilot contract row in the current default ingress asset. */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName AutopilotConfigRow;

    /** Current default ingress asset for spatial layout entities (row type: FFlightSpatialLayoutRow). */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightSpatialLayoutRow"))
    TSoftObjectPtr<UDataTable> SpatialLayoutTable;

    /** Filter spatial layout rows to this scenario (empty = all rows) */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data")
    FName SpatialLayoutScenario;

    /** Current default ingress asset for procedural anchor overrides (row type: FFlightProceduralAnchorRow). */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightProceduralAnchorRow"))
    TSoftObjectPtr<UDataTable> ProceduralAnchorTable;

    /** Current default ingress asset for behavior compile policy rows (row type: FFlightBehaviorCompilePolicyRow). */
    UPROPERTY(EditAnywhere, Config, Category = "Flight|Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/FlightProject.FlightBehaviorCompilePolicyRow"))
    TSoftObjectPtr<UDataTable> BehaviorCompilePolicyTable;
};
