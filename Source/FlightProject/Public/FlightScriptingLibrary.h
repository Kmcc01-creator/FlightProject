#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Rendering/FlightSimpleSCSLShaderPipelineSubsystem.h"
#include "Schema/FlightRequirementSchema.h"
#include "Verse/UFlightVerseSubsystem.h"
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

    /**
     * Ensure a Mass entity config asset contains the requested trait classes.
     * Trait classes should be passed as script class paths (for example "/Script/SwarmEncounter.FlightSwarmTrait").
     * Returns issues encountered during load/class resolution or trait insertion.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Editor")
    static TArray<FString> EnsureMassEntityConfigTraits(const FString& ConfigAssetPath, const TArray<FString>& TraitClassPaths);

    /** Return the trait class names currently hosted by a Mass entity config asset. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Editor")
    static TArray<FString> GetMassEntityConfigTraitClassNames(const FString& ConfigAssetPath);

    // ============================================================================
    // Schema Contracts
    // ============================================================================

    /** Build and return the current code-first requirement manifest as JSON. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Schema")
    static FString GetRequirementManifestJson();

    /**
     * Export the requirement manifest to disk.
     * Relative paths are rooted to the project directory.
     * Returns absolute output path on success, empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static FString ExportRequirementManifest(const FString& RelativeOutputPath = TEXT("Saved/Flight/Schema/requirements_manifest.json"));

    /**
     * Validate that a Niagara system satisfies required user params and data interface classes.
     * Returns a list of contract issues (empty if valid).
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static TArray<FString> ValidateNiagaraSystemContract(
        const FString& SystemObjectPath,
        const TArray<FString>& RequiredUserParameters,
        const TArray<FString>& RequiredDataInterfaceClassPaths);

    /**
     * Validate a single CVar contract.
     * Returns a list of contract issues (empty if valid).
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static TArray<FString> ValidateCVarContract(
        const FString& CVarName,
        const FString& ExpectedValue,
        EFlightCVarValueType ValueType = EFlightCVarValueType::String,
        float FloatTolerance = 0.001f);

    /**
     * Validate all CVar contracts for a schema profile defined in the code-first manifest.
     * Returns a list of contract issues (empty if valid).
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static TArray<FString> ValidateCVarRequirementsForProfile(FName ProfileName);

    /**
     * Diff declared CVar requirements against currently observed runtime values for a profile.
     * Returns drift issues (empty if in-sync).
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static TArray<FString> DiffCVarRequirementsForProfile(FName ProfileName);

    /**
     * Export currently observed CVar values for the given profile with per-row match status.
     * Relative paths are rooted to the project directory.
     * Returns absolute output path on success, empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static FString ExportObservedCVarSnapshotForProfile(
        FName ProfileName,
        const FString& RelativeOutputPath = TEXT("Saved/Flight/Schema/observed_cvars.json"));

    /**
     * Diff declared plugin requirements against currently observed runtime plugin state for a profile.
     * Returns drift issues (empty if in-sync).
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static TArray<FString> DiffPluginRequirementsForProfile(FName ProfileName);

    /**
     * Export currently observed plugin states for the given profile with per-row match status.
     * Relative paths are rooted to the project directory.
     * Returns absolute output path on success, empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Schema")
    static FString ExportObservedPluginSnapshotForProfile(
        FName ProfileName,
        const FString& RelativeOutputPath = TEXT("Saved/Flight/Schema/observed_plugins.json"));

    /**
     * Export the current PIE entity trace snapshot for the active world.
     * Returns absolute output path on success, empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Observability", meta = (WorldContext = "WorldContextObject"))
    static FString ExportPIEEntityTrace(
        const UObject* WorldContextObject,
        const FString& RelativeOutputPath = TEXT("Saved/Flight/Observations/pie_entity_trace_manual.json"));

    /** Returns current PIE trace event count for the active world. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Observability", meta = (WorldContext = "WorldContextObject"))
    static int32 GetPIEEntityTraceEventCount(const UObject* WorldContextObject);

    /**
     * Rebuild the current world's orchestration visibility and execution plan,
     * then return the report as JSON.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Observability", meta = (WorldContext = "WorldContextObject"))
    static FString GetOrchestrationReportJson(const UObject* WorldContextObject, bool bRefresh = true);

    /**
     * Export the current world's orchestration report to disk.
     * Relative paths are rooted to the project directory.
     * Returns absolute output path on success, empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Observability", meta = (WorldContext = "WorldContextObject"))
    static FString ExportOrchestrationReport(
        const UObject* WorldContextObject,
        const FString& RelativeOutputPath = TEXT("Saved/Flight/Orchestration/orchestration_report.json"),
        bool bRefresh = true);

    // ============================================================================
    // Rendering
    // ============================================================================

    /** Enable or disable the lightweight SimpleSCSL preview pipeline for the current world. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Rendering", meta = (WorldContext = "WorldContextObject"))
    static bool SetSimpleSCSLShaderPipelineEnabled(const UObject* WorldContextObject, bool bEnabled);

    /** Replace the SimpleSCSL preview pipeline configuration for the current world. */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Rendering", meta = (WorldContext = "WorldContextObject"))
    static bool ConfigureSimpleSCSLShaderPipeline(
        const UObject* WorldContextObject,
        const FFlightSimpleSCSLShaderPipelineConfig& Config);

    /** Return the current SimpleSCSL preview pipeline configuration. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Rendering", meta = (WorldContext = "WorldContextObject"))
    static FFlightSimpleSCSLShaderPipelineConfig GetSimpleSCSLShaderPipelineConfig(const UObject* WorldContextObject);

    /** Return the current SimpleSCSL preview pipeline state as JSON. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Rendering", meta = (WorldContext = "WorldContextObject"))
    static FString GetSimpleSCSLShaderPipelineStateJson(const UObject* WorldContextObject);

    // ============================================================================
    // VEX & Verse
    // ============================================================================

    /**
     * Compiles VEX source code using the Verse subsystem for a specific behavior ID.
     * Returns true only when an executable behavior is produced.
     * OutErrors always includes parse/contract diagnostics and generated Verse preview.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static bool CompileVex(const UObject* WorldContextObject, int32 BehaviorID, const FString& VexSource, FString& OutErrors);

    /** Returns all registered behavior IDs. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static TArray<int32> GetRegisteredBehaviorIDs(const UObject* WorldContextObject);

    /** Returns the configured execution rate (Hz) for a behavior. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static float GetBehaviorExecutionRate(const UObject* WorldContextObject, int32 BehaviorID);

    /** Returns the frame interval for a behavior. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static int32 GetBehaviorFrameInterval(const UObject* WorldContextObject, int32 BehaviorID);

    /** Returns compile-state metadata for a behavior ID. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static EFlightVerseCompileState GetBehaviorCompileState(const UObject* WorldContextObject, int32 BehaviorID);

    /** Returns true when the behavior has an executable runtime path. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static bool IsBehaviorExecutable(const UObject* WorldContextObject, int32 BehaviorID);

    /** Returns latest compile diagnostics for a behavior ID, if available. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static FString GetBehaviorCompileDiagnostics(const UObject* WorldContextObject, int32 BehaviorID);

    /** Returns the latest compile artifact report JSON for a behavior ID, if available. */
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static FString GetBehaviorCompileArtifactReportJson(const UObject* WorldContextObject, int32 BehaviorID);

    /**
     * Export the latest compile artifact report for a behavior to disk.
     * Relative paths are rooted to the project directory.
     * Returns absolute output path on success, empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Vex", meta = (WorldContext = "WorldContextObject"))
    static FString ExportBehaviorCompileArtifactReport(
        const UObject* WorldContextObject,
        int32 BehaviorID,
        const FString& RelativeOutputPath = TEXT("Saved/Flight/CompilerArtifacts/latest/behavior_report.json"));
};
