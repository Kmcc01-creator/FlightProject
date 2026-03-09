# Scripting & Automation

This section documents FlightProject's scripting infrastructure for rapid development and design iteration.

## Philosophy

As a solo developer building a simulation-focused project, the goal is to **spend time on design, not repetitive tasks**. The scripting layer enables:

- **Rapid iteration** without full C++ recompilation
- **Automation** of editor workflows and asset pipelines
- **Data-driven design** where parameters live outside code

## The Language Triad

| Language | Role | Solo Dev Rule |
|----------|------|---------------|
| **C++** | The Engine | "Write specific, reuse generic." Build robust base classes. |
| **Blueprints** | The Game | "Glue, don't calculate." Connect C++ blocks, handle events. |
| **Python** | The Assistant | "Never click twice." Automate any task done >3 times/day. |

## Documents

| Document | Description |
|----------|-------------|
| [DevelopmentCycle.md](DevelopmentCycle.md) | The Python → Blueprint → C++ iteration loop |
| [EditorAutomation.md](EditorAutomation.md) | Python tools, Editor Utility Widgets, bulk operations |
| [DataDrivenDesign.md](DataDrivenDesign.md) | CSV workflows, Data Assets, runtime tuning |
| [PythonAPI.md](PythonAPI.md) | Exposing C++ to Python (reflection patterns) |
| [VexSchemaValidation.md](VexSchemaValidation.md) | Schema-driven VEX symbol contracts, validation gates, and diagnostics |
| [VerseAssemblerScaffold.md](VerseAssemblerScaffold.md) | Verse/Solaris `IAssemblerPass` scaffold and phased bytecode plan |
| [VexUiScaffold.md](VexUiScaffold.md) | VEX-authored Slate composition and reactive data binding scaffold |

## Directory Structure

```
Content/Python/
├── init_unreal.py              # Editor startup - validation, asset creation
└── FlightProject/
    ├── __init__.py             # Auto-imports submodules
    ├── Bootstrap.py            # World bootstrap, swarm spawning
    ├── DataReload.py           # CSV hot-reload via C++ bridge
    ├── Validation.py           # CSV schema validation
    ├── SceneSetup.py           # Level/scene automation
    ├── AssetTools.py           # Asset creation/manipulation
    └── SwarmSetup.py           # SwarmEncounter asset setup

Source/FlightProject/
├── Public/FlightScriptingLibrary.h   # C++ bridge for Python
└── Private/FlightScriptingLibrary.cpp

Content/Data/
├── FlightLightingConfig.csv    # Lighting parameters
├── FlightAutopilotConfig.csv   # Drone behavior
├── FlightSpatialLayout.csv     # World layout
└── FlightSpatialProcedural.csv # Procedural overrides
```

## Quick Start

### Full Setup (One Command)

```python
from FlightProject import Bootstrap
Bootstrap.full_setup()  # Bootstrap + spawn swarm
```

### Common Tasks

```python
# Hot-reload CSV after editing
from FlightProject import DataReload
DataReload.reload_configs()

# Check system status
from FlightProject import Bootstrap
Bootstrap.status()

# Spawn/clear swarm entities
Bootstrap.spawn_swarm()
Bootstrap.clear_swarm()

# Rebuild spatial layout
Bootstrap.rebuild_spatial_layout()
```

### Validation

```python
from FlightProject import Validation
Validation.run_all_validation()  # Check CSV files and assets
```

## C++ Bridge: UFlightScriptingLibrary

Python calls route through `UFlightScriptingLibrary` (BlueprintFunctionLibrary):

| Python Call | C++ Function |
|-------------|--------------|
| `DataReload.reload_configs()` | `ReloadCSVConfigs()` |
| `Bootstrap.run_bootstrap()` | `RunBootstrap()` |
| `Bootstrap.spawn_swarm()` | `SpawnInitialSwarm()` |
| `Bootstrap.get_swarm_count()` | `GetSwarmEntityCount()` |

## Key Integrations

| System | Python Module | C++ Bridge |
|--------|---------------|------------|
| Data Subsystem | `DataReload` | `UFlightDataSubsystem` |
| World Bootstrap | `Bootstrap` | `UFlightWorldBootstrapSubsystem` |
| Swarm Spawning | `Bootstrap` | `UFlightSwarmSpawnerSubsystem` |
| Spatial Layout | `Bootstrap`, `SceneSetup` | `AFlightSpatialLayoutDirector` |
| Mass Entity | `Bootstrap` | `UMassEntitySubsystem` |

## Editor Startup

When the editor loads, `init_unreal.py` automatically:

1. Imports `FlightProject` module
2. Runs CSV validation
3. Ensures SwarmEncounter assets exist
4. Prints CSV summary and quick command reference
