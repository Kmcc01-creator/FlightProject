# FlightProject Build & Overview

## Project Snapshot
- **Purpose**: Large-scale autonomous flight sandbox leveraging GPU compute, Mass AI batching, and height-aware navigation.
- **Key Modules**: 
    - `FlightProject` (Core runtime).
    - `SwarmEncounter` (Game Feature Plugin for Mass logic).
- **Core Plugins**: Enhanced Input, Niagara, MassGameplay, MassAI, StateTree, ComputeFramework.
- **Config Highlights**: Linux performance overrides in `Config/Linux/LinuxEngine.ini` (Vulkan SM5, No RayTracing).

## Regenerating Project Files (Linux)
1. **Set Engine Root**
   ```bash
   export UE_ROOT="$HOME/Unreal/UnrealEngine"
   ```
2. **Generate Project Files** from the FlightProject workspace:
   ```bash
   cd /home/kelly/Unreal/Projects/FlightProject
   ./Scripts/generate_project_files.sh
   ```
3. **Build Targets** (compiles the C++ module before launching the editor):
   ```bash
   ./Scripts/build_targets.sh Development
   ```
4. **Launch Editor**:
   ```bash
   ./Scripts/run_editor.sh
   ```
   *Note: This script automatically handles SDL3 paths and gamescope integration if requested.*

## Notes
- **Linux Performance:** If the editor is unresponsive, check `Config/Linux/LinuxEngine.ini`. We force Vulkan SM5 and disable RayTracing by default for stability.
- **Bootstrapping:** `UFlightWorldBootstrapSubsystem` automatically runs on `StartPlay` to spawn the `AFlightSpatialLayoutDirector` and apply lighting settings.
- **Mass Entity:** Spawning logic lives in `UFlightSwarmSpawnerSubsystem` (Plugin).
- **Shaders:** Custom shaders are mapped from `Shaders/` directory at module startup.

### Follow-up Items
- Author a lightweight `/Game/Maps/Loading` level to replace the current `PersistentFlightTest` transition map.