#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FlightDataTypes.h"
#include "FlightDataSubsystem.generated.h"

/**
 * Loads configurable gameplay data (lighting, autopilot) from CSV tables at runtime.
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

private:
    bool LoadLightingConfig();
    bool LoadAutopilotConfig();
    bool LoadSpatialLayout();

    bool LoadTableRow(const FString& RelativePath, const FName& RowName, UScriptStruct* RowStruct, TFunctionRef<void(const uint8*)> OnRowLoaded);

    bool bLightingLoaded = false;
    bool bAutopilotLoaded = false;
    bool bSpatialLayoutLoaded = false;

    FFlightLightingConfigRow LightingConfig;
    FFlightAutopilotConfigRow AutopilotConfig;
    TArray<FFlightSpatialLayoutRow> SpatialLayoutRows;
};
