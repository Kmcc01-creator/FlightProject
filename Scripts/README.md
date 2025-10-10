# FlightProject Script Helpers

This directory hosts thin wrappers around Unreal's native tooling so we do not have to remember the long engine paths.

## Environment Bootstrap (`env_common.sh`)
- Normalises paths such as `PROJECT_ROOT` and `PROJECT_FILE`.
- Exports `UE_ROOT`, defaulting to `$HOME/Unreal/UnrealEngine` if the variable is unset.
- Provides helper guards (`ensure_project_file`, `ensure_file_exists`, `ensure_executable`, `resolve_ue_path`) that each script can reuse.

When adding new automation, source `env_common.sh` first so every helper shares the same assumptions.

## Typical Workflow
1. `./Scripts/generate_project_files.sh [-f]`  
   Regenerates IDE and project files via `GenerateProjectFiles.sh`. Use `-f` to pass Unreal's `-Force` flag.
2. `./Scripts/build_targets.sh [Configuration]`  
   Compiles C++ modules through `Build.sh`. Configuration defaults to `Development`.
3. `./Scripts/run_editor.sh [Extra UnrealEditor args]`  
   Launches `UnrealEditor` with the project descriptor and forwards any additional arguments (e.g. `-Log`, `-NoRayTracing`).
4. `./Scripts/run_game.sh [options]`  
   Builds (unless `--no-build`) and launches the project in standalone game mode (`-game`) so you can play without the editor UI. Accepts `--map` to load a specific level.

## Common Issues
- **Incorrect `UE_ROOT`**: Export `UE_ROOT` before running these helpers, or update your shell profile (both bash and fish) to keep it persistent.
- **Missing binaries**: Ensure the engine is compiled; if `Build.sh` or `UnrealEditor` is absent, run the engine's `Setup.sh`/build steps.

Keep this README close at hand for new contributors so the workflow stays repeatable.
