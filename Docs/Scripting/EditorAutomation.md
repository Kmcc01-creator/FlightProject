# Editor Automation

This document covers Python-based editor automation for reducing repetitive tasks.

## Philosophy: Never Click Twice

If you perform a task more than 3 times:
1. **First time**: Do it manually, note the steps
2. **Second time**: Confirm the pattern
3. **Third time**: Write a script

## Python Module Structure

```
Content/Python/
├── init_unreal.py              # Runs on editor startup
└── FlightProject/
    ├── __init__.py             # Auto-imports submodules
    ├── Bootstrap.py            # World bootstrap, swarm spawning
    ├── DataReload.py           # CSV hot-reload via C++ bridge
    ├── Validation.py           # CSV/asset validation
    ├── SceneSetup.py           # Level/scene automation
    ├── AssetTools.py           # Asset creation/manipulation
    └── SwarmSetup.py           # SwarmEncounter asset setup
```

## Running Scripts

**Output Log Console:**
```python
from FlightProject import Bootstrap
Bootstrap.full_setup()
```

**One-liner:**
```python
from FlightProject import DataReload; DataReload.reload_configs()
```

## Module Reference

### Bootstrap.py

Primary module for runtime setup and swarm management.

```python
from FlightProject import Bootstrap

# Full setup sequence
Bootstrap.full_setup()          # Bootstrap + spawn swarm

# Individual steps
Bootstrap.run_bootstrap()       # Lighting, spatial layout, Mass resume
Bootstrap.spawn_swarm()         # Create drone entities
Bootstrap.clear_swarm()         # Destroy all swarm entities
Bootstrap.rebuild_spatial_layout()  # Regenerate world layout

# Status checks
Bootstrap.status()              # Print current state
Bootstrap.get_swarm_count()     # Returns int
```

### DataReload.py

CSV hot-reload and data management.

```python
from FlightProject import DataReload

# Hot-reload all CSV configs
DataReload.reload_configs()     # Calls C++ UFlightDataSubsystem

# Reload specific config
DataReload.reload_csv_config("Lighting")
DataReload.reload_csv_config("Autopilot")
DataReload.reload_csv_config("SpatialLayout")
DataReload.reload_csv_config("ProceduralAnchors")

# Asset reimport (from disk to DataTable)
DataReload.reimport_all_csv_assets()
DataReload.reimport_csv_asset("FlightLightingConfig")

# Status
DataReload.print_csv_summary()
DataReload.check_csv_freshness()
DataReload.is_data_loaded()     # Returns bool
```

### Validation.py

CSV schema and asset validation.

```python
from FlightProject import Validation

# Run all validation checks
Validation.run_all_validation()  # Returns bool

# Individual checks
Validation.validate_csv_file_on_disk()  # Check raw CSV files
Validation.validate_swarm_assets()      # Check SwarmEncounter assets
```

### SceneSetup.py

Level and actor automation.

```python
from FlightProject import SceneSetup

# Map loading
SceneSetup.load_map("/Game/Maps/L_SwarmTest")

# Actor spawning
SceneSetup.spawn_waypoint_path("TestPath", unreal.Vector(0,0,0))
SceneSetup.spawn_nav_buoy_region("OuterRing", unreal.Vector(0,0,0))
SceneSetup.spawn_swarm_anchor("MainSwarm", unreal.Vector(0,0,1200))

# Cleanup
SceneSetup.clear_all_flight_actors()
SceneSetup.clear_actors_by_class("FlightAIPawn")

# Inspection
SceneSetup.list_flight_actors()
```

### AssetTools.py

Asset creation and manipulation.

```python
from FlightProject import AssetTools

# Create assets
AssetTools.create_swarm_config("NewSwarm")
AssetTools.create_game_feature_data("SwarmEncounter")
AssetTools.ensure_swarm_encounter_assets()

# Bulk operations
AssetTools.duplicate_and_modify("/Game/Path/Asset", "NewName", property=value)
AssetTools.list_assets("/Game/Data", asset_class="DataTable")
AssetTools.rename_assets_by_pattern("/Game/Data", "Old", "New")
```

## C++ Bridge: UFlightScriptingLibrary

Python modules call into C++ via `UFlightScriptingLibrary`:

```cpp
// FlightScriptingLibrary.h
UCLASS()
class FLIGHTPROJECT_API UFlightScriptingLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Data
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data")
    static void ReloadCSVConfigs(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Data")
    static bool ReloadCSVConfig(const UObject* WorldContextObject, const FString& ConfigName);

    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Data")
    static bool IsDataFullyLoaded(const UObject* WorldContextObject);

    // Bootstrap
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap")
    static void RunBootstrap(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Bootstrap")
    static int32 SpawnInitialSwarm(const UObject* WorldContextObject);

    // Spatial
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Spatial")
    static void RebuildSpatialLayout(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Spatial")
    static int32 GetSpatialLayoutRowCount(const UObject* WorldContextObject);

    // Mass Entity
    UFUNCTION(BlueprintPure, Category = "Flight|Scripting|Mass")
    static int32 GetSwarmEntityCount(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Mass")
    static void ClearAllSwarmEntities(const UObject* WorldContextObject);

    // Validation
    UFUNCTION(BlueprintCallable, Category = "Flight|Scripting|Validation")
    static TArray<FString> ValidateDataAssets();
};
```

**Python usage:**
```python
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.FlightScriptingLibrary.reload_csv_configs(world)
count = unreal.FlightScriptingLibrary.get_swarm_entity_count(world)
```

## Editor Startup (init_unreal.py)

Runs automatically when editor loads:

```python
# Content/Python/init_unreal.py
import unreal

def on_editor_startup():
    unreal.log("=== FlightProject Editor Startup ===")

    import FlightProject
    FlightProject.initialize()

    # 1. Validate data files
    from FlightProject import Validation
    Validation.run_all_validation()

    # 2. Ensure required assets exist
    from FlightProject import AssetTools
    AssetTools.ensure_swarm_encounter_assets()

    # 3. Print CSV summary
    from FlightProject import DataReload
    DataReload.print_csv_summary()

    unreal.log("=== FlightProject Ready ===")

on_editor_startup()
```

## Editor Utility Widgets (EUW)

EUWs provide a dockable UI panel that calls Python scripts.

### Creating a Tools Panel

1. **Content Browser** → Right-click → Editor Utilities → Editor Utility Widget
2. Name: `EUW_FlightTools`
3. Design the UI with buttons
4. Each button → OnClick → Execute Python Script

### Example: Flight Tools Panel

```
┌─────────────────────────────────────────┐
│  Flight Tools                       [X] │
├─────────────────────────────────────────┤
│  BOOTSTRAP                              │
│  [Full Setup]                           │
│  [Run Bootstrap Only]                   │
│  [Spawn Swarm]                          │
│  [Clear Swarm]                          │
├─────────────────────────────────────────┤
│  DATA                                   │
│  [Reload CSV Configs]                   │
│  [Reimport All CSVs]                    │
│  [Validate All]                         │
├─────────────────────────────────────────┤
│  STATUS                                 │
│  [Print Status]                         │
│  [Print CSV Summary]                    │
└─────────────────────────────────────────┘
```

### Button Scripts

```
Button: "Full Setup"
Script: "from FlightProject import Bootstrap; Bootstrap.full_setup()"

Button: "Reload CSV Configs"
Script: "from FlightProject import DataReload; DataReload.reload_configs()"

Button: "Print Status"
Script: "from FlightProject import Bootstrap; Bootstrap.status()"
```

## Common Workflows

### After Editing CSV Files

```python
from FlightProject import DataReload
DataReload.reload_configs()  # Hot-reload into C++ subsystem
```

### Testing Swarm Behavior

```python
from FlightProject import Bootstrap
Bootstrap.clear_swarm()      # Remove existing
Bootstrap.spawn_swarm()      # Spawn fresh
```

### Verifying Setup

```python
from FlightProject import Bootstrap, DataReload, Validation
Validation.run_all_validation()
DataReload.print_csv_summary()
Bootstrap.status()
```

## Debugging

### Logging

```python
unreal.log("Info message")           # Normal log
unreal.log_warning("Warning!")       # Yellow warning
unreal.log_error("Error!")           # Red error
```

### Output Log Filter

Filter the Output Log by "LogPython" to see only Python output.

### Common Issues

| Issue | Solution |
|-------|----------|
| `ModuleNotFoundError` | Check `Content/Python/` path, ensure `__init__.py` exists |
| `AttributeError: FlightScriptingLibrary` | Rebuild C++ module, restart editor |
| Function not found | Ensure C++ uses `BlueprintCallable` |
| Changes not reflected | Restart editor (Python modules are cached) |
| "No editor world available" | Run from Output Log while a level is open |
