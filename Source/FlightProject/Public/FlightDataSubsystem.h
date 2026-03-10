#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FlightDataTypes.h"
#include "Core/FlightFunctional.h"
#include "FlightDataSubsystem.generated.h"

/**
 * Resolves configurable gameplay data into typed runtime-facing contracts.
 * Current default ingress is Data Table-backed, but consumers should treat this
 * as a source/binding/cache boundary rather than as a CSV/DataTable-specific API.
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
    const TArray<FFlightBehaviorCompilePolicyRow>& GetBehaviorCompilePolicies() const { return BehaviorCompilePolicies; }
    bool HasBehaviorCompilePolicies() const { return bBehaviorCompilePolicyLoaded && !BehaviorCompilePolicies.IsEmpty(); }
    const FFlightBehaviorCompilePolicyRow* FindBehaviorCompilePolicy(uint32 BehaviorId, FName CohortName = NAME_None, FName ProfileName = NAME_None) const;

    /** Reload all configured data contracts from their current ingress sources. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Data")
    void ReloadAllConfigs();

    /** Reload a specific resolved data contract by name. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Data")
    bool ReloadConfig(const FString& ConfigName);

    /** Check if all configured data contracts loaded successfully. */
    UFUNCTION(BlueprintPure, Category = "Flight|Data")
    bool IsFullyLoaded() const;

private:
    bool LoadLightingConfig();
    bool LoadAutopilotConfig();
    bool LoadSpatialLayout();
    bool LoadProceduralAnchors();
    bool LoadBehaviorCompilePolicies();

    bool bLightingLoaded = false;
    bool bAutopilotLoaded = false;
    bool bSpatialLayoutLoaded = false;
    bool bProceduralAnchorsLoaded = false;
    bool bBehaviorCompilePolicyLoaded = false;

    FFlightLightingConfigRow LightingConfig;
    FFlightAutopilotConfigRow AutopilotConfig;
    TArray<FFlightSpatialLayoutRow> SpatialLayoutRows;
    TMap<FName, TArray<FFlightProceduralAnchorRow>> ProceduralAnchors;
    TArray<FFlightBehaviorCompilePolicyRow> BehaviorCompilePolicies;
};
