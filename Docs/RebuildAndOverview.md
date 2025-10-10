# FlightProject Build & Overview

## Project Snapshot
- **Purpose**: Large-scale autonomous flight sandbox leveraging GPU compute, Mass AI batching, and height-aware navigation.
- **Key Modules**: `FlightProject` runtime module (custom pawn/movement, game mode/state, HUD, developer settings).
- **Core Plugins**: Enhanced Input, Niagara, MassGameplay (incl. MassActors/MassSimulation), MassAI, StateTree, ComputeFramework, ChaosCaching, GeometryScripting, WorldPartitionHLODUtilities.
- **Config Highlights**: Async compute enabled (`r.RDGAsyncCompute=1`), flying navigation defaults, Mass phases in `Config/DefaultMass.ini`, developer settings for altitude thresholds.

## Regenerating Project Files (Linux / CachyOS)
1. **Set Engine Root**
   ```bash
   export UE_ROOT="$HOME/Unreal/UnrealEngine"
   ```
2. **Run Setup if Needed** (first-time only)
   ```bash
   cd "$UE_ROOT"
   ./Setup.sh
   ```
3. **Generate Project Files** from the FlightProject workspace:
   ```bash
   cd ~/Documents/Unreal\ Projects/FlightProject
   ./Scripts/generate_project_files.sh
   ```
   - Pass `-f` to overwrite existing solution files (`-Force`).
   - Edit the script's `ARGS` array to add other target platforms if required.
4. **Build Targets** (compiles the C++ module before launching the editor):
   ```bash
   ./Scripts/build_targets.sh        # defaults to Development
   ```
   - Append `Debug` or `Shipping` to build another configuration.
   - Wraps `Engine/Build/BatchFiles/Linux/Build.sh` for the `FlightProjectEditor` target.
5. **Launch Editor** once the build succeeds:
   ```bash
   ./Scripts/run_editor.sh -Log
   ```
   - Extra arguments are forwarded to `UnrealEditor` (e.g., `-game`, `-NoSplash`).

## Notes
- If Unreal prompts to rebuild modules and exits, run `./Scripts/build_targets.sh` first—GUI prompts (zenity) do not appear in headless sessions.
- If the build fails on `Unable to find parent class type for 'AFlightNetworkManager' named 'AGameNetworkManager'`, ensure `Source/FlightProject/Public/FlightNetworkManager.h` includes `GameFramework/GameNetworkManager.h` (UE 5.6 relocated this header).
- MassEntity runtime code lives in the engine; keep MassGameplay/MassAI/StateTree plugins enabled for higher-level systems and authoring tools.
- Keep `Docs/ProjectSpecification.md` and `Docs/EditorTooling.md` updated as systems mature.
- When packaging, add `Content/Data` to *Additional Asset Directories to Cook* so the CSV configuration files ship alongside the build.
- Game mode automatically spawns a `FlightSpatialLayoutDirector` which reads `FlightSpatialLayout.csv`; tweak the CSV to rearrange nav probes, collision towers, or landmarks without touching level assets.
