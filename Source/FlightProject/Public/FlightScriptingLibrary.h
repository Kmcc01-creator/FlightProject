#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FlightScriptingLibrary.generated.h"

/**
 * Static function library for Python and Blueprint scripting integration.
 * Exposes FlightProject systems for editor automation and tooling.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightScriptingLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ============================================================================
    // Data Subsystem
    // ============================================================================

    /** Reload all Data Table configurations. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data", meta = (WorldContext = "WorldContextObject"))
    static void ReloadDataConfigs(const UObject* WorldContextObject);

    /** Legacy name - calls ReloadDataConfigs. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data", meta = (WorldContext = "WorldContextObject"))
    static void ReloadCSVConfigs(const UObject* WorldContextObject);

    /** Reload a specific configuration by name. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data", meta = (WorldContext = "WorldContextObject"))
    static bool ReloadCSVConfig(const UObject* WorldContextObject, const FString& ConfigName);

    /** Check if all data configurations are loaded. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Data", meta = (WorldContext = "WorldContextObject"))
    static bool IsDataFullyLoaded(const UObject* WorldContextObject);

    // ============================================================================
    // Bootstrap
    // ============================================================================

    /** Run the world bootstrap sequence. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap", meta = (WorldContext = "WorldContextObject"))
    static void RunBootstrap(const UObject* WorldContextObject);

    /** Spawn the initial swarm entities. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap", meta = (WorldContext = "WorldContextObject"))
    static int32 SpawnInitialSwarm(const UObject* WorldContextObject);

    /** Initialize the new GPU-driven swarm simulation. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap", meta = (WorldContext = "WorldContextObject"))
    static void InitializeGpuSwarm(const UObject* WorldContextObject, int32 EntityCount = 500000);

    // ============================================================================
    // Spatial Layout
    // ============================================================================

    /** Trigger rebuild of the spatial layout director. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Spatial", meta = (WorldContext = "WorldContextObject"))
    static void RebuildSpatialLayout(const UObject* WorldContextObject);

    /** Get the number of spatial layout rows loaded. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Spatial", meta = (WorldContext = "WorldContextObject"))
    static int32 GetSpatialLayoutRowCount(const UObject* WorldContextObject);

    // ============================================================================
    // Mass Entity
    // ============================================================================

    /** Get the current count of swarm entities. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Mass", meta = (WorldContext = "WorldContextObject"))
    static int32 GetSwarmEntityCount(const UObject* WorldContextObject);

    /** Destroy all swarm entities. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Mass", meta = (WorldContext = "WorldContextObject"))
    static void ClearAllSwarmEntities(const UObject* WorldContextObject);

    // ============================================================================
    // Validation
    // ============================================================================

    /** Validate all data assets and return issues as strings. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Validation")
    static TArray<FString> ValidateDataAssets();

    /** Get Data Table asset paths configured in developer settings. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Validation")
    static TArray<FString> GetConfiguredDataTablePaths();

    /** Legacy name - calls GetConfiguredDataTablePaths. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Validation")
    static TArray<FString> GetConfiguredCSVPaths();
};
