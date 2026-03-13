# FlightProject Script Helpers

This directory hosts thin wrappers around Unreal's native tooling so we do not have to remember the long engine paths.

## Environment Bootstrap (`env_common.sh`)
- Normalises paths such as `PROJECT_ROOT` and `PROJECT_FILE`.
- Exports `UE_ROOT`, defaulting to `$HOME/Unreal/UnrealEngine` if the variable is unset.
- Provides helper guards (`ensure_project_file`, `ensure_file_exists`, `ensure_executable`, `resolve_ue_path`) that each script can reuse.
- Centralises script output defaults through `FP_SCRIPT_COLOR_MODE`, `FP_SCRIPT_TIMESTAMPS`, `FP_TEST_LOG_PROFILE`, `FP_TEST_OUTPUT_MODE`, and `FP_TEST_EXTRA_LOG_CMDS`.
- Centralises test build policy through `FP_TEST_BUILD_BEFORE_RUN`, `FP_TEST_BUILD_CONFIGURATION`, and `FP_TEST_BUILD_EXTRA_ARGS` so test runners can rebuild changed source before validation.
- Provides shared helpers for banners, timestamp prefixes, color decisions, and test `-LogCmds` profile construction.
- Centralises Wayland/X11 environment tweaks with `configure_video_backend` so every launcher shares the same display defaults.
- Shares compositor/session launch wrappers through `FP_SESSION_WRAPPER`, `FP_USE_GAMESCOPE`, `FP_GAMESCOPE_ARGS`, and `build_launch_prefix`.
- Shares Vulkan validation toggles through `FP_VK_VALIDATION`, `FP_VK_GPU_VALIDATION`, `FP_VK_DEBUG_SYNC`, `FP_VK_BEST_PRACTICES`, and `FP_VK_DEBUG_UTILS`.

When adding new automation, source `env_common.sh` first so every helper shares the same assumptions.

## Typical Workflow
1. `./Scripts/generate_project_files.sh [-f]`  
   Regenerates IDE and project files via `GenerateProjectFiles.sh`. Use `-f` to pass Unreal's `-Force` flag.
2. `./Scripts/build_targets.sh [Configuration] [--no-uba|--use-uba]`  
   Compiles C++ modules through `Build.sh` (default `Development`). In Codex/sandboxed terminals it auto-applies `-NoUBA` to avoid write failures under `~/.epic/UnrealBuildAccelerator`; use `--use-uba` to override if your environment allows it.
   When combined with `--verify`, it now runs the headless breaking subset with the `triage` preset by default; override via `--verify-preset <quiet|triage|startup-debug|full-debug>` or `FP_TEST_PRESET`.
3. `./Scripts/run_editor.sh [Extra UnrealEditor args]`  
   Launches `UnrealEditor` with the project descriptor, forwards additional arguments (e.g. `-Log`, `-NoRayTracing`), and honours our Wayland helpers (`--video-backend`, `--wayland`, `--x11`, `--session-wrapper`, `--uwsm`, `--gamescope`, `--no-gamescope`, `--gamescope-arg`).
4. `./Scripts/run_game.sh [options]`  
   Builds (unless `--no-build`), cooks content, stages a Linux build via `RunUAT.sh BuildCookRun -nocompileuat`, and launches the staged `Saved/StagedBuilds/Linux*/FlightProject.sh` wrapper. It auto-cooks the default map when one is not specified and will reuse the staged build on subsequent runs; pass `--no-cook` to skip the staging step if you know the staged artifacts are current. Display helpers such as `--video-backend`, `--session-wrapper`, `--gamescope`, `--half-window`, and `--windowed-size WxH` tune the launch before the staged binary spawns. Use `--` to forward extra flags to the game binary (for example `-- -windowed -opengl4` when Vulkan drivers misbehave). The helper also sandbox’s .NET state under `Saved/DotNetCli` so first-run sentinels never touch the global home directory. See `Docs/Troubleshooting.md` for the history behind these safeguards.
5. `./Scripts/run_tests_phased.sh [options]`  
   Runs validation in explicit phases: **Phase 1** generated + spec, **Phase 2** architecture/development, **Phase 3** multi-case integration, **Phase 4** simple unit automation, and optional **Phase 5** GPU/system checks (`--with-gpu`). By default it now triggers `build_targets.sh` once before the phases run; use `--no-build` to skip or `--build-config <Debug|Development|Shipping>` to override the build configuration for that preflight compile. Use `--print-plan` to inspect the resolved build policy and phase filters without launching Unreal.
6. `./Scripts/run_tests_full.sh`  
   Runs Vulkan GPU validation. It now relies on project/plugin Vulkan extension registration by default; set `TEST_FORCE_VULKAN_EXTENSIONS=1` only when you want to compare against explicit `-vulkanextension=...` forcing. On the current Linux runner the default GPU-validation preset is `TEST_GPU_VALIDATION_PRESET=local-radv`, which applies `TEST_VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json` and `TEST_VK_LOADER_LAYERS_DISABLE='~implicit~'` unless you override them. Set `TEST_GPU_VALIDATION_PRESET=off` to disable that default. The runner exposes explicit scopes:
   - `TEST_SCOPE=benchmark` for `FlightProject.Perf.GpuPerception`
   - `TEST_SCOPE=gpu_smoke` for the perception smoke pair
   - `TEST_SCOPE=swarm` for the three swarm GPU tests
   - `TEST_SCOPE=gpu_domain` for CPU-safe GPU-domain tests
   - `TEST_SCOPE=gpu_required` / `all` for the full GPU-required lane
   Additional comparison toggles: `TEST_VIDEO_BACKEND=auto|wayland|x11|dummy`, `TEST_RENDER_OFFSCREEN=1|0`, `TEST_SESSION_WRAPPER=auto|uwsm|none`, `TEST_USE_GAMESCOPE=1|0`, `TEST_GAMESCOPE_ARGS="..."`, `TEST_VK_VALIDATION=0..5`, `TEST_VK_GPU_VALIDATION=1|0`, `TEST_VK_DEBUG_SYNC=1|0`, `TEST_VK_BEST_PRACTICES=1|0`, `TEST_VK_DEBUG_UTILS=1|0`, `TEST_VK_DRIVER_FILES=/path/to/icd.json`, `TEST_VK_ICD_FILENAMES=/path/to/icd.json`, and `TEST_VK_LOADER_LAYERS_DISABLE='~implicit~'`.
   The GPU runner also now honors the shared output controls: `--preset`, `--profile`, `--output`, `--automation-only`, `--summary`, `--errors-only`, `--all-output`, `--show-python`, `--show-startup`, `--extra-log-cmds`, `--timestamps`, and `--scope`. Like the headless runner, it rebuilds by default before test execution; use `--no-build` or `--build-config <Debug|Development|Shipping>` to override.
7. `./Scripts/check_simd_baseline.sh [--json]`  
   Probes the local CPU ISA surface and the compiler's `-march=native` view, then prints a practical FlightProject SIMD verification baseline. Use `--json` when CI, orchestration tooling, or local scripts need the same result as machine-readable output. Use this when deciding whether a machine should be treated as `AVX2`, extended `AVX-512`, `Neon`, or scalar-only for backend verification work.

## Vulkan Recovery / Triage

When `run_tests_full.sh` fails before automation discovery, treat it as a Vulkan bring-up problem first and a test failure second.

Recommended order:

1. Start with the project default preset:

```bash
./Scripts/run_tests_full.sh
```

2. If startup fails before test discovery, make the loader state explicit:

```bash
TEST_VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
TEST_VK_LOADER_LAYERS_DISABLE='~implicit~' \
./Scripts/run_tests_full.sh
```

3. If Vulkan still fails early, disable the preset and compare against the unmodified host environment:

```bash
TEST_GPU_VALIDATION_PRESET=off ./Scripts/run_tests_full.sh
```

4. If the failure looks SDL/display-related, vary the display path separately from the Vulkan ICD:

```bash
TEST_VIDEO_BACKEND=dummy TEST_RENDER_OFFSCREEN=1 ./Scripts/run_tests_full.sh
```

```bash
TEST_VIDEO_BACKEND=x11 TEST_RENDER_OFFSCREEN=0 ./Scripts/run_tests_full.sh
```

On Hyprland/Wayland, prefer a session-aware launch once plain environment inheritance stops at `InitSDL()`:

```bash
TEST_SCOPE=gpu_smoke TEST_VIDEO_BACKEND=wayland TEST_RENDER_OFFSCREEN=0 \
TEST_SESSION_WRAPPER=uwsm ./Scripts/run_tests_full.sh
```

```bash
TEST_SCOPE=gpu_smoke TEST_VIDEO_BACKEND=wayland TEST_RENDER_OFFSCREEN=0 \
TEST_SESSION_WRAPPER=uwsm TEST_USE_GAMESCOPE=1 ./Scripts/run_tests_full.sh
```

5. If device creation works but extension registration is suspect, compare project/plugin registration against explicit command-line forcing:

```bash
TEST_FORCE_VULKAN_EXTENSIONS=1 ./Scripts/run_tests_full.sh
```

6. Narrow scope once the engine reaches automation discovery:

```bash
TEST_SCOPE=gpu_smoke ./Scripts/run_tests_full.sh
```

```bash
TEST_SCOPE=swarm ./Scripts/run_tests_full.sh
```

```bash
TEST_SCOPE=benchmark ./Scripts/run_tests_full.sh
```

Interpretation guidance:
- Failure before `Found ... automation tests` usually means loader, layer, ICD, SDL, or device-selection trouble.
- `TEST_SCOPE=gpu_domain` is useful when you want GPU-domain behavior coverage without requiring a working Vulkan device.
- A green `benchmark` run is currently weak evidence if the benchmark test is intentionally skipped in source; treat it as bring-up confirmation, not full coverage.

## Vulkan Validation Layers

The current CachyOS host has the Khronos validation package installed, but FlightProject runners do not enable Vulkan validation by default.

Shared env controls:
- `FP_VK_VALIDATION=0..5` maps to Unreal's `-vulkanvalidation=<level>`
- `FP_VK_GPU_VALIDATION=1` adds `-gpuvalidation`
- `FP_VK_DEBUG_SYNC=1` adds `-vulkandebugsync`
- `FP_VK_BEST_PRACTICES=1` adds `-vulkanbestpractices`
- `FP_VK_DEBUG_UTILS=1` adds `-vulkandebugutils`

Runner-specific overrides for `run_tests_full.sh`:
- `TEST_VK_VALIDATION`
- `TEST_VK_GPU_VALIDATION`
- `TEST_VK_DEBUG_SYNC`
- `TEST_VK_BEST_PRACTICES`
- `TEST_VK_DEBUG_UTILS`

Recommended starting point for GPU automation triage:

```bash
FP_VK_VALIDATION=2 ./Scripts/run_tests_full.sh
```

More aggressive validation pass:

```bash
FP_VK_VALIDATION=2 FP_VK_GPU_VALIDATION=1 FP_VK_DEBUG_SYNC=1 ./Scripts/run_tests_full.sh
```

Notes:
- Development builds of Unreal on this project do not automatically enable validation; we opt in explicitly through the runner flags above.
- `-gpuvalidation`, `-vulkandebugsync`, and `-vulkanbestpractices` are only useful when validation is on. The shared helper will automatically raise the effective validation level to `2` if one of those modes is requested with `FP_VK_VALIDATION=0`.

## GPU Runner Output Controls

`run_tests_full.sh` now follows the same high-level log model as the headless runner:
- **Log profile** controls Unreal `-LogCmds`
- **Output mode** controls which runner lines are printed to the terminal

Useful examples:

```bash
./Scripts/run_tests_full.sh --profile=minimal --summary --scope=gpu_smoke
```

```bash
TEST_SESSION_WRAPPER=uwsm TEST_VIDEO_BACKEND=wayland TEST_RENDER_OFFSCREEN=0 \
./Scripts/run_tests_full.sh --profile=minimal --automation-only --scope=gpu_smoke
```

```bash
FP_SCRIPT_TIMESTAMPS=1 ./Scripts/run_tests_full.sh --profile=focused --errors-only
```

Notes:
- `minimal` and `focused` now work on the GPU runner; previously it effectively fell back to full engine logging.
- `summary` still prints fatal/error lines so early Vulkan/SDL bring-up failures are visible.
- `automation` is the best mode when you want test progress without full editor/startup noise. Add `--show-startup` only when you are debugging early initialization.

## Test Runner Controls

`run_tests_headless.sh` now separates two concerns:
- **Log profile**: which Unreal log categories are enabled via `-LogCmds`
- **Output mode**: which lines the shell runner prints to the terminal

Useful headless options:
- `--preset <quiet|triage|startup-debug|full-debug>`
- `--profile <minimal|focused|python|verbose|full>`
- `--output <errors|summary|automation|all>`
- `--automation-only`
- `--summary`
- `--errors-only`
- `--show-python`
- `--show-startup`
- `--extra-log-cmds "LogCategory verbosity,..."`

Matching environment variables:
- `FP_TEST_PRESET`
- `FP_TEST_LOG_PROFILE`
- `FP_TEST_OUTPUT_MODE`
- `FP_TEST_EXTRA_LOG_CMDS`
- `FP_TEST_BUILD_BEFORE_RUN`
- `FP_TEST_BUILD_CONFIGURATION`
- `FP_TEST_BUILD_EXTRA_ARGS`
- `TEST_INCLUDE_PYTHON_LOGS=1`
- `TEST_INCLUDE_STARTUP_LOGS=1`

Preset meanings:
- `quiet`: minimal log categories and summary-only terminal output
- `triage`: focused log categories and errors-only terminal output
- `startup-debug`: Python-enabled log profile with automation output plus startup/Python visibility
- `full-debug`: verbose Unreal logs and full terminal output

Examples:

```bash
./Scripts/run_tests_headless.sh --filter="FlightProject.Functional.Vex.CompileArtifactReport" --automation-only
```

```bash
./Scripts/run_tests_headless.sh --filter="FlightProject.Functional.Vex.CompileArtifactReport" --preset=triage
```

```bash
./Scripts/run_tests_headless.sh --filter="FlightProject.Functional.Vex.CompileArtifactReport" --profile=python --output=summary --show-python
```

```bash
FP_TEST_LOG_PROFILE=minimal FP_TEST_OUTPUT_MODE=automation ./Scripts/run_tests_phased.sh --phase4-only
```

```bash
./Scripts/run_tests_phased.sh --print-plan
```

Build-before-test behavior:
- `run_tests_headless.sh`, `run_tests_full.sh`, and `run_tests_phased.sh` now rebuild by default before automation starts.
- Use `--no-build` for one-off runs against an already current binary state.
- Use `--build-config <Debug|Development|Shipping>` when a specific build configuration must match the intended validation target.
- Use `FP_TEST_BUILD_BEFORE_RUN=0` to disable the default project-wide, or `FP_TEST_BUILD_EXTRA_ARGS="-NoUBA"` / similar to push shared flags into the pre-test `build_targets.sh` invocation.

```bash
FP_TEST_PRESET=startup-debug ./Scripts/run_tests_phased.sh --phase1-only
```

```bash
./Scripts/build_targets.sh Development --no-uba --verify --verify-preset=triage
```

## Wayland / Hyprland Experiments
- Both `run_editor.sh` and `run_game.sh` accept `--video-backend <auto|wayland|x11>` (with `--wayland`/`--x11` shortcuts) so you can force SDL to pick a display backend without touching your shell environment.
- `--session-wrapper <auto|uwsm|none>` and `--uwsm` let you opt into `uwsm app --` explicitly. The shared `auto` mode now treats `uwsm` as the preferred wrapper for real Wayland sessions.
- Pass `--gamescope` to wrap the launch in `gamescope --backend wayland --expose-wayland -- ...` by default on Wayland sessions. Repeat `--gamescope-arg <value>`, use `--gamescope-args=a,b`, or set `FP_GAMESCOPE_ARGS="--your --flags"` to customise the wrapper. Use `--no-gamescope` to disable it when the env var `FP_USE_GAMESCOPE` is set globally.
- Environment variables mirror the CLI: `FP_VIDEO_BACKEND`, `FP_SESSION_WRAPPER`, `FP_USE_GAMESCOPE`, and `FP_GAMESCOPE_ARGS` provide persistent defaults. `run_tests_full.sh` follows the same pattern with `TEST_SESSION_WRAPPER`, `TEST_USE_GAMESCOPE`, and `TEST_GAMESCOPE_ARGS`.
- On the March 11, 2026 Hyprland/CachyOS runner, `uwsm` is the first recovery step once plain `run_tests_full.sh` dies in `InitSDL()`. With `TEST_SESSION_WRAPPER=uwsm`, the GPU smoke lane progressed past SDL initialization and failed later inside Unreal shader compiler startup instead.
- See `Docs/LinuxWayland.md` for the broader context on Hyprland tuning, known Unreal Wayland bugs, and validation steps.

## Common Issues
- **Incorrect `UE_ROOT`**: Export `UE_ROOT` before running these helpers, or update your shell profile (both bash and fish) to keep it persistent.
- **Missing binaries**: Ensure the engine is compiled; if `Build.sh` or the staged `FlightProject.sh` launcher is absent, rerun `run_game.sh` without `--no-build`/`--no-cook` so it can rebuild and stage.
- **Shader code library errors**: These indicate the staged cook is stale or absent. Re-run `run_game.sh` (without `--no-cook`) to regenerate the shader archives before launching.
- **AutomationTool NU1903 failures**: We mask NuGet’s Magick.NET advisory (NU1901-1903) and pin dotnet audit mode to `none`, so these should only reappear if environment variables are overridden. Clear them by unsetting your overrides or re-running inside a clean shell.

Keep this README close at hand for new contributors so the workflow stays repeatable.
