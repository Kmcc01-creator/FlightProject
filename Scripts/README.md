# FlightProject Script Helpers

This directory hosts thin wrappers around Unreal's native tooling so we do not have to remember the long engine paths.

## Environment Bootstrap (`env_common.sh`)
- Normalises paths such as `PROJECT_ROOT` and `PROJECT_FILE`.
- Exports `UE_ROOT`, defaulting to `$HOME/Unreal/UnrealEngine` if the variable is unset.
- Provides helper guards (`ensure_project_file`, `ensure_file_exists`, `ensure_executable`, `resolve_ue_path`) that each script can reuse.
- Detects SDL2 dynamic libraries via `find_sdl_dynamic_api`, trying common distro locations before respecting a user-defined `FP_SDL_DYNAMIC_API`.
- Centralises Wayland/X11 environment tweaks with `configure_video_backend` so every launcher shares the same display defaults.

When adding new automation, source `env_common.sh` first so every helper shares the same assumptions.

## Typical Workflow
1. `./Scripts/generate_project_files.sh [-f]`  
   Regenerates IDE and project files via `GenerateProjectFiles.sh`. Use `-f` to pass Unreal's `-Force` flag.
2. `./Scripts/build_targets.sh [Configuration]`  
   Compiles C++ modules through `Build.sh`. Configuration defaults to `Development`.
3. `./Scripts/run_editor.sh [Extra UnrealEditor args]`  
   Launches `UnrealEditor` with the project descriptor, forwards additional arguments (e.g. `-Log`, `-NoRayTracing`), and honours our Wayland helpers (`--video-backend`, `--wayland`, `--x11`, `--gamescope`, `--no-gamescope`, `--gamescope-arg`).
4. `./Scripts/run_game.sh [options]`  
   Builds (unless `--no-build`), cooks content, stages a Linux build via `RunUAT.sh BuildCookRun -nocompileuat`, and launches the staged `Saved/StagedBuilds/Linux*/FlightProject.sh` wrapper. It auto-cooks the default map when one is not specified and will reuse the staged build on subsequent runs; pass `--no-cook` to skip the staging step if you know the staged artifacts are current. Display helpers such as `--video-backend`, `--gamescope`, `--half-window`, and `--windowed-size WxH` tune the launch before the staged binary spawns. Use `--` to forward extra flags to the game binary (for example `-- -windowed -opengl4` when Vulkan drivers misbehave). The helper also sandbox’s .NET state under `Saved/DotNetCli` so first-run sentinels never touch the global home directory. See `Docs/Troubleshooting.md` for the history behind these safeguards.
5. `./Scripts/run_tests_phased.sh [options]`  
   Runs validation in explicit phases: **Phase 1** (complex/generated + spec), **Phase 2** (simple automation), and optional **Phase 3** GPU/system checks (`--with-gpu`). Use `--phase1-only`, `--phase2-only`, or `--phase3-only` for focused triage.

## Wayland / Hyprland Experiments
- Both `run_editor.sh` and `run_game.sh` accept `--video-backend <auto|wayland|x11>` (with `--wayland`/`--x11` shortcuts) so you can force SDL to pick a display backend without touching your shell environment.
- Pass `--gamescope` to wrap the launch in `gamescope --expose-wayland --prefer-vk -- ...` by default. Repeat `--gamescope-arg <value>`, use `--gamescope-args=a,b`, or set `FP_GAMESCOPE_ARGS="--your --flags"` to customise the wrapper. Use `--no-gamescope` to disable it when the env var `FP_USE_GAMESCOPE` is set globally.
- Environment variables mirror the CLI: `FP_VIDEO_BACKEND`, `FP_USE_GAMESCOPE`, and `FP_GAMESCOPE_ARGS` provide persistent defaults, while `FP_SDL_DYNAMIC_API` can pin a specific `libSDL2.so` when Arch-style multi-lib layouts confuse SDL’s dynamic loader.
- See `Docs/LinuxWayland.md` for the broader context on Hyprland tuning, known Unreal Wayland bugs, and validation steps.

## Common Issues
- **Incorrect `UE_ROOT`**: Export `UE_ROOT` before running these helpers, or update your shell profile (both bash and fish) to keep it persistent.
- **Missing binaries**: Ensure the engine is compiled; if `Build.sh` or the staged `FlightProject.sh` launcher is absent, rerun `run_game.sh` without `--no-build`/`--no-cook` so it can rebuild and stage.
- **Shader code library errors**: These indicate the staged cook is stale or absent. Re-run `run_game.sh` (without `--no-cook`) to regenerate the shader archives before launching.
- **AutomationTool NU1903 failures**: We mask NuGet’s Magick.NET advisory (NU1901-1903) and pin dotnet audit mode to `none`, so these should only reappear if environment variables are overridden. Clear them by unsetting your overrides or re-running inside a clean shell.

Keep this README close at hand for new contributors so the workflow stays repeatable.
