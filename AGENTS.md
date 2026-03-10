# Repository Guidelines

## Project Structure & Module Organization
FlightProject is an Unreal Engine 5 workspace rooted at `FlightProject.uproject`. Gameplay and editor modules live under `Source/FlightProject/{Public,Private}`; keep headers in `Public` for exports and implementation in `Private`. Config defaults live in `Config/Default*.ini`, runtime content plus CSV-driven data tables live under `Content/`, and generated outputs stay inside `Intermediate/` and `Saved/`.

## Environment Snapshot
- **Engine root**: default to `$HOME/Unreal/UnrealEngine` unless `UE_ROOT` is exported.
- **Primary targets**: `FlightProject` (runtime) and `FlightProjectEditor` (editor); module resides under `Source/FlightProject`.
- **C++ standard config**: `Source/FlightProject/FlightProject.Build.cs` sets `CppStandard = CppStandardVersion.Cpp23` for the `FlightProject` module.
- **PCH behavior with C++23**: UBT may downgrade this module to `NoPCHs` when it differs from engine C++ standard; this is expected in current logs.
- **Maps**: `Content/Maps/PersistentFlightTest.umap` currently serves as both `GameDefaultMap` and `TransitionMap` in `DefaultEngine.ini`.
- **Data pipeline**: CSV configuration in `Content/Data/*` feeds lighting, autopilot, and spatial layout loaded by `FlightDataSubsystem`.
- **Shader plans**: developer settings point `ComputeShaderDirectory` at `/Shaders`; module shader directory mapping is enabled for custom RDG/compute shaders.
- **Config reference**: see `Docs/Environment/Configuration.md` for the `Config/Default*.ini` inventory.

## Build, Test, and Development Commands
- `./Scripts/generate_project_files.sh -f` regenerates IDE files through `GenerateProjectFiles.sh`.
- `./Scripts/build_targets.sh Development` compiles C++ targets; pass `Shipping` as needed. In Codex/sandboxed terminals this now auto-applies `-NoUBA`; use `--use-uba` to force UBA or `--no-uba` to force-disable it.
- `./Scripts/build_targets.sh Development --verify` runs the breaking headless subset after a successful build, using the `triage` preset by default. Override with `--verify-preset <quiet|triage|startup-debug|full-debug>`.
- `"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" FlightProjectEditor Linux Development -project="$PWD/FlightProject.uproject" -game -progress -Module=FlightProject -NoUBA` is the deterministic module-only fallback when UBA stalls.
- `./Scripts/run_editor.sh -Log --wayland` starts UnrealEditor with helpers from `env_common.sh`.
- `./Scripts/run_game.sh --no-build -- -windowed` stages/cooks via `RunUAT BuildCookRun` and launches the staged Linux binary; omit `--no-build` for a clean rebuild.
- `./Scripts/run_tests_headless.sh` runs headless automation (`NullRHI`) and is the default local/CI test path.
- `./Scripts/run_tests_full.sh` runs Vulkan/offscreen GPU validation (`TEST_SCOPE=benchmark|gpu_smoke|all`).
- The generated `Makefile` exposes direct targets like `make FlightProjectEditor-Linux-Development` or `make FlightProject-Linux-Test` for CI parity.

## Coding Style & Naming Conventions
- Follow Unreal’s conventions: PascalCase classes (`AFlightVehiclePawn`), camelCase methods, and `b`-prefixed booleans.
- Use four spaces (no tabs) and group includes with project headers before engine headers.
- Expose types via headers in `Public/` and prefer `TWeakObjectPtr`, `TObjectPtr`, or `TArray` containers instead of raw pointers.
- Register logging categories with `DEFINE_LOG_CATEGORY_STATIC` and emit actionable `UE_LOG` messages.

## Testing Guidelines
- Use `./Scripts/run_tests_headless.sh` as the primary automation entrypoint.
- Baseline full-tree headless run:
  `TEST_PRESET=triage ./Scripts/run_tests_phased.sh --timestamps`
- Focused parser bucket:
  `./Scripts/run_tests_headless.sh --filter="FlightProject.Schema.Vex.Parser" --preset=triage`
- Extended mixed bucket (schema + Verse + parser + vertical slice):
  `./Scripts/run_tests_headless.sh --filter="FlightProject.Integration.SchemaDriven+FlightProject.Schema.Vex.ManifestValidation+FlightProject.Verse.CompileContract+FlightProject.Verse.Subsystem+FlightProject.Schema.Vex.Parser+FlightProject.Integration.Vex.VerticalSlice" --preset=triage`
- Verse-focused shortcut:
  `./Scripts/run_tests_headless.sh --verse`
- GPU/offscreen path:
  `TEST_SCOPE=gpu_smoke ./Scripts/run_tests_full.sh`
- Current test-runner control model:
  - presets: `quiet`, `triage`, `startup-debug`, `full-debug`
  - log profiles: `minimal`, `focused`, `python`, `verbose`, `full`
  - output modes: `errors`, `summary`, `automation`, `all`
- Recommended default for local triage:
  `./Scripts/run_tests_headless.sh --preset=triage --filter="..."`
- Recommended startup/Python debugging path:
  `./Scripts/run_tests_headless.sh --preset=startup-debug --filter="..."`
- Discovery rule: tests intended for `UnrealEditor-Cmd`/CI must use `EAutomationTestFlags::EditorContext`; `ClientContext` tests are not discovered in this path.
- After adding new test source files, run a full build once (`./Scripts/build_targets.sh Development`) to force discovery/index refresh before triage.
- For cooked validation, run `make FlightProject-Linux-Test` (or call `Build.sh`/`RunUAT` directly with Test config); `run_game.sh` does not currently expose a `--test` flag.
- Keep current bucket status snapshots in `Docs/Workflow/CurrentBuild.md` and parser-specific coverage details in `Docs/Scripting/VexSchemaValidation.md`.
- Document manual verification and open issues in `Docs/Environment/Troubleshooting.md` so staged builds stay reproducible.
- Current suite status to keep in mind:
  - headless phased run currently has 3 known failing tests
  - GPU-required suite currently fails before automation because Vulkan device creation does not complete in this environment

## Commit & Pull Request Guidelines
- Git history favors short, imperative summaries ("Add testing section to README", "testing flight"); keep the subject <72 characters and mention the subsystem up front.
- Reference tracking issues in the body, describe gameplay/editor impact, and list the commands you ran (`build_targets`, `run_game`, tests).
- PRs should include screenshots or logs for visual or networking changes, link to doc updates, and call out config or data assets touched.

## Environment & Tooling Tips
- Export `UE_ROOT` if the engine is outside `$HOME/Unreal/UnrealEngine`; scripts read it through `Scripts/env_common.sh`.
- Prefer `rg` for code search and keep temp assets out of source—only committed `Content/` assets should live in git.
- When editing ini files, lean on Unreal’s merge semantics (avoid wholesale copy/paste) and explain risky tweaks inside the matching doc under `Docs/`.
- Codex terminal sandbox note (confirmed March 9, 2026): writes to `~/.epic/UnrealBuildAccelerator` can be blocked, so UBA may hang after `Using Unreal Build Accelerator local executor`. Use `./Scripts/build_targets.sh --no-uba` (or default Codex behavior) and only enable UBA with `--use-uba` when the environment permits it.
- Optional override for non-Codex shells: set `FP_FORCE_NO_UBA=1` to keep UBA disabled by default, or `FP_FORCE_NO_UBA=0` to allow normal UBA selection.

## Phase Roadmap & TODOs
This section tracks near-term priorities after functional/reactive/reflection stabilization and row/schema integration work.

### Phase 0 Closeout: Operational Hardening
- [ ] Add strict GPU smoke gating in `Scripts/run_tests_full.sh`:
  - fail the run if GPU tests report "Skipping ... unavailable" in `TEST_SCOPE=gpu_smoke`.
  - enforce expected GPU smoke discovery count and fail on mismatch.
- [ ] Resolve GPU test discoverability mismatch:
  - `FlightProject.Spatial.GpuPerception` is defined in source but current filtered runs discover only `CallbackResolves`.
  - either make base test discoverable as a unique leaf path (recommended) or remove it from smoke expectations.
- [ ] Keep focused logs CI-safe:
  - maintain `TEST_LOG_PROFILE=focused` without log-verbosity ensures.

### Phase 1: Contract Consolidation (Rows + Reflection + Assets)
- [ ] Expand schema contracts from Niagara-first PoC to simulation row coverage (`FSimulationParamRow`, `FSwarmInstanceRow`, `FVexSymbolRow`).
- [ ] Resolve current Niagara contract drift for `NS_SwarmVisualizer`:
  - missing `User.SwarmSubsystem`
  - missing `User.DroneCount`
  - missing `NiagaraDataInterfaceSwarm` binding/class presence.
- [ ] Add automation that validates schema contract compliance (not just manifest export shape).

### Phase 2: Deterministic Asset + Config Provisioning
- [ ] Promote code-first requirement manifests to idempotent "ensure/repair" tooling for assets and settings.
- [ ] Add render profile contract checks (Lumen/Nanite/CVar policy) and enforce in automation.
- [ ] Emit machine-readable artifacts for CI (`Saved/Automation` summaries, schema validation reports).

### Phase 3: GPU Pipeline Reliability & Performance
- [ ] Add performance thresholds for GPU perception benchmark scales (10k/100k/250k/500k) with stable pass/fail criteria.
- [ ] Add stress coverage for async completion and subscription churn under load.
- [ ] Track and gate regressions in GPU bridge completion latency and timeout rates.

### Phase 4: Authoring UX (Low-Click Workflow)
- [ ] Expand scripting/editor tooling so required assets/configs are generated from schema contracts instead of manual editor setup.
- [ ] Add "project health" commandlet/report combining build, schema, asset, and automation status.
- [ ] Document one-command workflows for local iteration and CI parity.

### Phase 5: Advanced Orchestration
- [ ] Implement **Reactive Buffer Blitting**: Automated sync of Mass fragments to GPU buffers based on VEX scheduling directives.
- [ ] Develop **Python Behavior Debugger**: Real-time visualization of VEX execution rates, residency violations, and Verse VM state.
- [ ] Implement **Transactional Event Bridge**: Bridge Verse `<transacts>` failures to GPU event buffer rollbacks.

### Phase 6: Scalable Concurrency & VM Integration
- [x] Implement **Task Thunk Generation** for `@job` directives using `UE::Tasks`.
- [x] Integrate Verse **Leniency/Async** logic for `@async` VEX blocks via `VPlaceholder`.
- [x] Implement **Affinity Validation** in the VEX parser to prevent off-thread UObject access.
- [x] Establish **Schema-Driven Native Registry** for dynamic thunk resolution.

### Phase 7: Verification & Optimization
- [x] Land **Complex IR Support** (multi-block control flow) in the VVM assembler.
- [ ] Implement **Concurrency Stress Tests**: Automated tests for high-frequency task churning.
- [ ] Add **Performance Benchmarking**: Comparative analysis of synchronous vs. `@job` execution for large swarms.
- [ ] Implement **Task Completion Callback Thunks**: Allowing VEX logic to react to the completion of specific jobs.

### Phase 8: Architectural Refinement
- [ ] Implement **Module Decoupling**: Split `FlightProject` into `Runtime` and `Dev` modules for the Verse bridge.
- [ ] Implement **GC-Rooted Closure Migration**: Move all deferred behavior states to `TWriteBarrier`-rooted `VValue` objects.
- [ ] Expand **Intrinsic Math Library**: Complete parity between VEX IR and Verse/HLSL for all standard vector operations.

## Linux Wayland Workflow
- Follow `Docs/Environment/LinuxSetup.md` when launching on CachyOS/Hyprland. Install `sdl2`, `lib32-sdl2`, `gamescope`, and `mangohud` so the wrapper flags work.
- Use `./Scripts/run_editor.sh --wayland --gamescope --gamescope-arg --fullscreen` for a native Wayland launch; append `--x11` to compare against the fallback path.
- Export `FP_SDL_DYNAMIC_API=/usr/lib/libSDL2-2.0.so.0` (or the path from `pacman -Ql sdl2`) when SDL cannot resolve the dynamic loader.
- Tweak display behavior with `--gamescope-arg`/`--gamescope-args` (e.g., `--gamescope-args=--prefer-vk,--hdr-enabled`) and keep compositor changes under `~/.config/hypr/config/*.conf`.
