#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FlightDataTypes.h"
#include "FlightDataSubsystem.generated.h"

/**
 * Loads configurable gameplay data from Data Table assets.
 * Configure asset references in Project Settings > Flight Project.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightDataSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    const FFlightLightingConfigRow* GetLightingConfig() const
    {
        return bLightingLoaded ? &LightingConfig : nullptr;
    }

    const FFlightAutopilotConfigRow* GetAutopilotConfig() const
    {
        return bAutopilotLoaded ? &AutopilotConfig : nullptr;
    }

    const TArray<FFlightSpatialLayoutRow>& GetSpatialLayout() const
    {
        return SpatialLayoutRows;
    }

    bool HasSpatialLayout() const
    {
        return bSpatialLayoutLoaded;
    }

    const FFlightProceduralAnchorRow* FindProceduralAnchorConfig(FName AnchorId, EFlightProceduralAnchorType AnchorType) const;

    /** Reload all Data Table configurations. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Data")
    void ReloadAllConfigs();

    /** Reload a specific configuration by name. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Data")
    bool ReloadConfig(const FString& ConfigName);

    /** Check if all configurations loaded successfully. */
    UFUNCTION(BlueprintPure, Category = "Flight|Data")
    bool IsFullyLoaded() const;

private:
    bool LoadLightingConfig();
    bool LoadAutopilotConfig();
    bool LoadSpatialLayout();
    bool LoadProceduralAnchors();

    bool bLightingLoaded = false;
    bool bAutopilotLoaded = false;
    bool bSpatialLayoutLoaded = false;
    bool bProceduralAnchorsLoaded = false;

    FFlightLightingConfigRow LightingConfig;
    FFlightAutopilotConfigRow AutopilotConfig;
    TArray<FFlightSpatialLayoutRow> SpatialLayoutRows;
    TMap<FName, TArray<FFlightProceduralAnchorRow>> ProceduralAnchors;
};
