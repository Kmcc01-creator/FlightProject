# Repository Guidelines

## Project Structure & Module Organization
FlightProject is an Unreal Engine 5 workspace rooted at `FlightProject.uproject`. Gameplay and editor modules live under `Source/FlightProject/{Public,Private}`; keep headers in `Public` for exports and implementation in `Private`. Config defaults live in `Config/Default*.ini`, runtime content plus CSV-driven data tables live under `Content/`, and generated outputs stay inside `Intermediate/` and `Saved/`.

## Build, Test, and Development Commands
- `./Scripts/generate_project_files.sh -f` regenerates IDE files through `GenerateProjectFiles.sh`.
- `./Scripts/build_targets.sh Development` compiles C++ targets; pass `Shipping` or `Test` as needed.
- `./Scripts/run_editor.sh -Log --wayland` starts UnrealEditor with helpers from `env_common.sh`.
- `./Scripts/run_game.sh --no-build -- -windowed` stages/cooks via `RunUAT BuildCookRun` and launches the staged Linux binary; omit `--no-build` for a clean rebuild.
- The generated `Makefile` exposes direct targets like `make FlightProjectEditor-Linux-Development` or `make FlightProject-Linux-Test` for CI parity.

## Coding Style & Naming Conventions
- Follow Unreal’s conventions: PascalCase classes (`AFlightVehiclePawn`), camelCase methods, and `b`-prefixed booleans.
- Use four spaces (no tabs) and group includes with project headers before engine headers.
- Expose types via headers in `Public/` and prefer `TWeakObjectPtr`, `TObjectPtr`, or `TArray` containers instead of raw pointers.
- Register logging categories with `DEFINE_LOG_CATEGORY_STATIC` and emit actionable `UE_LOG` messages.

## Testing Guidelines
- The project has two primary types of tests: in-world visualizers (`AFlightSpatialTestEntity`) and standalone C++ unit tests.
- To run all project automation tests:
  `./Scripts/run_editor.sh -ExecCmds="Automation RunTests FlightProject.*; Quit" -unattended -nop4`
- To run only the core functional C++ unit tests:
  `./Scripts/run_editor.sh -ExecCmds="Automation RunTests FlightProject.Functional; Quit" -unattended -nop4`
- For cooked validation, run `make FlightProject-Linux-Test` or append `--test` to `run_game.sh` to drive `RunUAT`’s Test configuration.
- Document manual verification and open issues in `Docs/Troubleshooting.md` so staged builds stay reproducible.

## Commit & Pull Request Guidelines
- Git history favors short, imperative summaries ("Add testing section to README", "testing flight"); keep the subject <72 characters and mention the subsystem up front.
- Reference tracking issues in the body, describe gameplay/editor impact, and list the commands you ran (`build_targets`, `run_game`, tests).
- PRs should include screenshots or logs for visual or networking changes, link to doc updates, and call out config or data assets touched.

## Environment & Tooling Tips
- Export `UE_ROOT` if the engine is outside `$HOME/Unreal/UnrealEngine`; scripts read it through `Scripts/env_common.sh`.
- Prefer `rg` for code search and keep temp assets out of source—only committed `Content/` assets should live in git.
- When editing ini files, lean on Unreal’s merge semantics (avoid wholesale copy/paste) and explain risky tweaks inside the matching doc under `Docs/`.

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

### Phase 6: Scalable Concurrency
- [x] Implement **Task Thunk Generation** for `@job` directives using `UE::Tasks`.
- [x] Integrate Verse **Leniency/Async** logic for `@async` VEX blocks.
- [x] Implement **Affinity Validation** in the VEX parser to prevent off-thread UObject access.

### Phase 7: Verification & Optimization
- [ ] Implement **Concurrency Stress Tests**: Automated tests for high-frequency task churning.
- [ ] Add **Performance Benchmarking**: Comparative analysis of synchronous vs. `@job` execution for large swarms.
- [ ] Implement **Task Completion Callback Thunks**: Allowing VEX logic to react to the completion of specific jobs.

## Linux Wayland Workflow
- Follow `Docs/LinuxWayland.md` when launching on CachyOS/Hyprland. Install `sdl2`, `lib32-sdl2`, `gamescope`, and `mangohud` so the wrapper flags work.
- Use `./Scripts/run_editor.sh --wayland --gamescope --gamescope-arg --fullscreen` for a native Wayland launch; append `--x11` to compare against the fallback path.
- Export `FP_SDL_DYNAMIC_API=/usr/lib/libSDL2-2.0.so.0` (or the path from `pacman -Ql sdl2`) when SDL cannot resolve the dynamic loader.
- Tweak display behavior with `--gamescope-arg`/`--gamescope-args` (e.g., `--gamescope-args=--prefer-vk,--hdr-enabled`) and keep compositor changes under `~/.config/hypr/config/*.conf`.
