# Python API & C++ Reflection Guide

This document covers how to expose C++ classes, structs, and functions to Python in Unreal Engine.

## FlightProject Implementation

FlightProject uses `UFlightScriptingLibrary` as the primary Python ↔ C++ bridge:

```
Python Module                    C++ Class
─────────────────────────────────────────────────────
FlightProject.Bootstrap    →     UFlightScriptingLibrary
FlightProject.DataReload   →     UFlightScriptingLibrary
                                 UFlightDataSubsystem
```

### UFlightScriptingLibrary

Located at `Source/FlightProject/Public/FlightScriptingLibrary.h`:

```cpp
UCLASS()
class FLIGHTPROJECT_API UFlightScriptingLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Data Subsystem
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data",
              meta = (WorldContext = "WorldContextObject"))
    static void ReloadCSVConfigs(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data",
              meta = (WorldContext = "WorldContextObject"))
    static bool ReloadCSVConfig(const UObject* WorldContextObject, const FString& ConfigName);

    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Data",
              meta = (WorldContext = "WorldContextObject"))
    static bool IsDataFullyLoaded(const UObject* WorldContextObject);

    // Bootstrap
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap",
              meta = (WorldContext = "WorldContextObject"))
    static void RunBootstrap(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap",
              meta = (WorldContext = "WorldContextObject"))
    static int32 SpawnInitialSwarm(const UObject* WorldContextObject);

    // Spatial Layout
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Spatial",
              meta = (WorldContext = "WorldContextObject"))
    static void RebuildSpatialLayout(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Spatial",
              meta = (WorldContext = "WorldContextObject"))
    static int32 GetSpatialLayoutRowCount(const UObject* WorldContextObject);

    // Mass Entity
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Mass",
              meta = (WorldContext = "WorldContextObject"))
    static int32 GetSwarmEntityCount(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Mass",
              meta = (WorldContext = "WorldContextObject"))
    static void ClearAllSwarmEntities(const UObject* WorldContextObject);

    // Validation
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Validation")
    static TArray<FString> ValidateDataAssets();

    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Validation")
    static TArray<FString> GetConfiguredCSVPaths();
};
```

### Python Usage

```python
import unreal

# Get world context (required for most functions)
world = unreal.EditorLevelLibrary.get_editor_world()

# Data reload
unreal.FlightScriptingLibrary.reload_csv_configs(world)
unreal.FlightScriptingLibrary.reload_csv_config(world, "Lighting")
loaded = unreal.FlightScriptingLibrary.is_data_fully_loaded(world)

# Bootstrap
unreal.FlightScriptingLibrary.run_bootstrap(world)
count = unreal.FlightScriptingLibrary.spawn_initial_swarm(world)

# Spatial
unreal.FlightScriptingLibrary.rebuild_spatial_layout(world)
rows = unreal.FlightScriptingLibrary.get_spatial_layout_row_count(world)

# Mass Entity
entities = unreal.FlightScriptingLibrary.get_swarm_entity_count(world)
unreal.FlightScriptingLibrary.clear_all_swarm_entities(world)

# Validation (no world context needed)
issues = unreal.FlightScriptingLibrary.validate_data_assets()
paths = unreal.FlightScriptingLibrary.get_configured_csv_paths()
```

## General Reflection Patterns

### Exposing Structs

Python wrappers are generated for structs marked as `BlueprintType`:

```cpp
USTRUCT(BlueprintType)
struct FFlightLightingConfigRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DirectionalIntensity = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLinearColor DirectionalColor = FLinearColor::White;
};
```

**Python:**
```python
config = unreal.FlightLightingConfigRow()
config.directional_intensity = 5.0
config.directional_color = unreal.LinearColor(1.0, 0.9, 0.8, 1.0)
```

### Exposing Functions

Use `BlueprintCallable` for functions Python can call:

```cpp
UFUNCTION(BlueprintCallable, Category = "Flight|Data")
void ReloadAllConfigs();

UFUNCTION(BlueprintPure, Category = "Flight|Data")
bool IsFullyLoaded() const;
```

- `BlueprintCallable`: Can be called from Python/BP
- `BlueprintPure`: No side effects, usable in pure expressions

### WorldContext Meta

For functions needing world access, use `WorldContext`:

```cpp
UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
static void MyFunction(const UObject* WorldContextObject);
```

**Python:**
```python
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.MyClass.my_function(world)
```

### Exposing Subsystems

Subsystems can expose methods directly:

```cpp
// FlightDataSubsystem.h
UCLASS()
class UFlightDataSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Flight|Data")
    void ReloadAllConfigs();

    UFUNCTION(BlueprintPure, Category = "Flight|Data")
    bool IsFullyLoaded() const;
};
```

**Python (accessing subsystem):**
```python
# GameInstance subsystems require getting the game instance first
game_instance = unreal.GameplayStatics.get_game_instance(world)
# Note: Direct subsystem access from Python is limited
# Prefer using UFlightScriptingLibrary as the bridge
```

### Properties on Classes

```cpp
UCLASS(BlueprintType)
class AFlightVehiclePawn : public APawn
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
    float MaxSpeed = 2000.f;

    UPROPERTY(BlueprintReadOnly, Category = "Flight")
    float CurrentSpeed;
};
```

**Python:**
```python
pawn = unreal.EditorLevelLibrary.get_selected_level_actors()[0]
pawn.max_speed = 3000.0
current = pawn.current_speed  # Read-only
pawn.set_editor_property("MaxSpeed", 3000.0)  # Alternative
```

## Naming Conventions

Python API converts C++ `CamelCase` to `snake_case`:

| C++ | Python |
|-----|--------|
| `ReloadCSVConfigs` | `reload_csv_configs` |
| `GetSwarmEntityCount` | `get_swarm_entity_count` |
| `MaxSpeed` | `max_speed` |
| `bIsLoaded` | `is_loaded` (b prefix stripped) |

## Key Specifiers

| Specifier | Purpose |
|-----------|---------|
| `BlueprintType` | Struct/class visible to Python |
| `BlueprintCallable` | Function callable from Python |
| `BlueprintPure` | Pure function (no side effects) |
| `BlueprintReadWrite` | Property read/write from Python |
| `BlueprintReadOnly` | Property read-only from Python |
| `EditAnywhere` | Property editable in editor |

## Debugging

### Check if exposed correctly

```python
# List available methods on a class
dir(unreal.FlightScriptingLibrary)

# Check if function exists
hasattr(unreal.FlightScriptingLibrary, 'reload_csv_configs')
```

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| `AttributeError` | Class not found | Rebuild C++, restart editor |
| Function not visible | Missing `BlueprintCallable` | Add UFUNCTION specifier |
| Property not accessible | Missing `BlueprintReadWrite` | Add UPROPERTY specifier |
| Wrong return type | Type not exposed | Add `BlueprintType` to struct |
