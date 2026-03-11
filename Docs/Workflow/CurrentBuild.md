# Current Build & Test Documentation

This document outlines the build system configuration, test execution patterns, and strategies for structured development within FlightProject.

## 1. Test Execution & Discovery
This section captures the most recent observed build/test outcomes and commands used for repeatable triage.

### Latest Build Snapshot (2026-03-10)
- `./Scripts/build_targets.sh Development --no-uba` succeeded for `FlightProjectEditor` on Linux.
- `./Scripts/build_targets.sh Development --no-uba --verify` also succeeded; verify now uses the `triage` preset by default.
- Recent non-parser hardening compiled cleanly:
  - schema-driven command handling fix (`INVALID_AFFINITY` + fail-fast unknown types)
  - `uint32` -> VEX `Int` mapping for schema symbol generation
  - required-symbol enforcement in `UFlightVerseSubsystem::CompileVex`
  - explicit Verse compile-state metadata (`GeneratedOnly`/`VmCompiled`/`VmCompileFailed`)
  - scripting accessors for compile state/executability/diagnostics
  - executable native fallback Verse runtime path (compile + behavior execution) with truthful diagnostics
  - VM procedure wrapper execution path (`VProcedure` + `VFunction::Invoke`) with native fallback retained as safety net
  - experimental `IAssemblerPass` scaffold registration in `FlightProject` module (codegen/link hook only)
  - hardened concurrency spec world acquisition + async compile-state assertions
  - runtime GPU contract alignment for `Swarm.DroidStateBuffer`:
    - schema-side structured-buffer contract resolution/validation,
    - swarm RDG/RHI descriptor consumption,
    - scriptable reflected GPU contract validation
- Parser stabilization also compiled cleanly:
  - precedence-aware expression parser (`function call`, `dot`, `pipe`, vector literals)
  - parser-side required-symbol enforcement path (`bRequireAllRequiredSymbols`)
  - mega-kernel hoisting/local alias generation aligned with parser tests

### Latest Headless Automation Snapshot (2026-03-10)
- Full phased headless validation is currently **green** on the default `triage` preset.
- Verified on March 10, 2026 with:
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Vex.Parser.Spec"`
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Logging.Boundaries"`
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Integration.Startup.Sequencing"`
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Orchestration.Bindings"`
  - `TEST_PRESET=triage ./Scripts/run_tests_phased.sh --timestamps`
- Phase 1 now passes (`27` tests, exit code `0`).
- Phase 2 now passes (`50` tests, exit code `0`).
- The previously failing tests are now passing:
  - `FlightProject.Vex.Parser.Spec.Verse Lowering.should generate idiomatic Verse code`
  - `FlightProject.Logging.Boundaries.DualSinkNoDuplication`
  - `FlightProject.Logging.Boundaries.UnrealOutputBridge`
- The lightweight startup sequencing suite is also passing in Phase 1:
  - `FlightProject.Integration.Startup.Sequencing.BootstrapCompletionSignal`
  - `FlightProject.Integration.Startup.Sequencing.OrchestrationRebuildAdvancesPlan`
  - `FlightProject.Integration.Startup.Sequencing.StartupReportJsonSurface`
- The new orchestration binding automation is also passing in Phase 2:
  - `FlightProject.Orchestration.Bindings.PlanPrefersExecutableBehavior`
  - `FlightProject.Orchestration.Bindings.ProcessorResolverPrefersOrchestration`
  - `FlightProject.Orchestration.Bindings.AnchorPreferenceLegality`
  - `FlightProject.Orchestration.Bindings.AnchorContractLegality`
  - `FlightProject.Orchestration.Bindings.ProcessorResolverFallback`
- Anchor legality is now part of the runtime binding baseline:
  - swarm anchors can author preferred, allowed, denied, and required-contract behavior policy;
  - orchestration enforces those constraints during per-cohort plan rebuild;
  - the remaining legality TODO is startup-profile-aware policy layered above anchor/default selection.
- GPU resource contract validation is now part of the schema baseline:
  - `FlightProject.Schema.Vex.ManifestValidation` now checks structured-buffer contract resolution and reflected runtime validation for `Swarm.DroidStateBuffer`;
  - the remaining TODO is to move beyond the current buffer-first, `FDroidState`-first path into images, transient attachments, and additional reflected resource types.
- `FlightProject.Functional.Vex.CompileArtifactReport` remains passing in headless mode.
- Headless startup no longer instantiates the GPU io_uring bridge under `-NullRHI`, so non-GPU runs are not carrying Vulkan reactor noise in their baseline logs.

### Latest GPU/Vulkan Snapshot (2026-03-10)
- Full GPU-required validation now reaches Vulkan device creation and automation discovery on the local Linux runner when using the recovered RADV loader preset.
- The previous `SM6`-only Linux target restriction has been relaxed: the project now advertises both `SF_VULKAN_SM5` and `SF_VULKAN_SM6`.
- `run_tests_full.sh` no longer forces `VK_KHR_external_semaphore` / `VK_KHR_external_semaphore_fd` on the command line by default; it relies on project/plugin registration unless `TEST_FORCE_VULKAN_EXTENSIONS=1` is set explicitly.
- `run_tests_full.sh` now defaults to `TEST_GPU_VALIDATION_PRESET=local-radv`, which applies:
  - `TEST_VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json`
  - `TEST_VK_LOADER_LAYERS_DISABLE='~implicit~'`
  - opt-out via `TEST_GPU_VALIDATION_PRESET=off`
- Verified on March 10, 2026 with:
  - `./Scripts/run_tests_full.sh`
  - `TEST_VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json TEST_VK_LOADER_LAYERS_DISABLE='~implicit~' TEST_LOG_PROFILE=verbose ./Scripts/run_tests_full.sh`
- Under that preset, Unreal now:
  - enumerates `AMD Radeon 860M Graphics (RADV KRACKAN1)` in minimal/plain/profile temp-instance probes
  - passes the `VP_UE_Vulkan_SM6` profile gate
  - creates the Vulkan device successfully
  - reaches automation discovery (`Found 1 automation tests based on 'FlightProject.Perf.GpuPerception'`)
- The current benchmark bucket exits `0`, but only because the test is intentionally skipped:
  - `Skipping FlightProject.Perf.GpuPerception - SCSL renderer pipeline under development`
- Local host Vulkan inspection on March 10, 2026 shows:
  - `vulkaninfo` sees `AMD Radeon 860M Graphics (RADV KRACKAN1)` plus `llvmpipe`
  - `fragmentStoresAndAtomics = true`
  - `maxBoundDescriptorSets = 32`
  - `VK_KHR_external_fence_fd` and `VK_KHR_external_semaphore_fd` are present on the device
- Current conclusion: the prior GPU blocker was primarily loader/layer/device-selection environment on this runner, not the project’s semaphore-extension registration or the io_uring bridge initialization path.
- Remaining GPU work is now about meaningful test coverage beyond initialization/discovery, not basic Vulkan bring-up.

### Latest GPU/Vulkan Follow-Up (2026-03-11)
- A fresh local rerun on March 11, 2026 did **not** reproduce the March 10 green bring-up on this runner.
- `./Scripts/run_tests_full.sh` currently fails before automation discovery with:
  - SDL/UDEV initialization errors under the dummy/offscreen path
  - Vulkan profile/device enumeration returning `VK_ERROR_INITIALIZATION_FAILED`
  - forced Vulkan device creation aborting before `Found ... automation tests`
- A session-aware Hyprland launch changes that failure mode materially:
  - `TEST_SCOPE=gpu_smoke TEST_VIDEO_BACKEND=wayland TEST_RENDER_OFFSCREEN=0 TEST_SESSION_WRAPPER=uwsm ./Scripts/run_tests_full.sh`
  - and `... TEST_USE_GAMESCOPE=1 ...`
  - both get past `InitSDL()` and later fail in `ShaderCompilerEditor.cpp` because `/FlightProject/Generated/VexMegaKernel.ush` is missing while compiling `FFlightSwarmForceCS` from `Shaders/Private/FlightSwarmForce.usf`
- Current interpretation: the primary Hyprland compositor/session blocker is now reduced. The next non-green GPU blocker is project-side shader startup state, not basic SDL session attachment.
- Current practical recommendation on Hyprland/CachyOS: treat `uwsm app --` as the preferred compositor/session wrapper for GPU-facing Unreal launches; plain inherited Wayland env vars are weaker evidence.
- Also note that several GPU-required tests remain intentionally hard-skipped in source while the SCSL renderer pipeline is under development, so successful device creation is still stronger evidence than successful behavioral coverage at the moment.

### Key Learning: Discovery Context
Tests defined with `EAutomationTestFlags::ClientContext` are **not** discovered when running via `UnrealEditor-Cmd`. To enable discovery in commandlet/CI environments, tests must use:
- `EAutomationTestFlags::EditorContext`

### Core Commands
**Build Command:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
stdbuf -oL -eL ./Scripts/build_targets.sh Development < /dev/null
```

**Headless Integrity Test Command:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh
```
*Note: `quit` ensures immediate exit. The script uses `-DDC=NoZenLocalFallback` with a local cache path to avoid Zen startup stalls while keeping DDC valid for commandlet execution.*
*Recommended local preset: `--preset=triage` for compact failure-focused output.*
*Named presets: `quiet`, `triage`, `startup-debug`, `full-debug`.*
*Advanced controls: `--profile`, `--output`, `--show-python`, `--show-startup`, and `--extra-log-cmds`.*

**Phased Validation Command (recommended default):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_PRESET=triage ./Scripts/run_tests_phased.sh --timestamps
```
*Use `--with-gpu --gpu-scope all` only when Vulkan/device availability is confirmed on the runner.*

**Persistence Triage Command (SCSL field mode breadcrumbs):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh --preset=startup-debug --show-startup
```
Expected marker (focused logs):
- `LogFlightSwarm: Display: Swarm persistence mode: Requested=... Applied=... LatticeValid=... LatticeMatch=... CloudValid=... CloudMatch=...`
Error-focused example:
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh --preset=triage
```

**Headless GPU/Vulkan Benchmark Path:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_full.sh
```
*Optional scope: set `TEST_SCOPE=benchmark|gpu_smoke|swarm|gpu_domain|gpu_required|all` (default `benchmark`).*
*Optional extension forcing: set `TEST_FORCE_VULKAN_EXTENSIONS=1` to add explicit semaphore-extension command-line flags for comparison runs.*
*Default GPU-validation preset: `TEST_GPU_VALIDATION_PRESET=local-radv` (applies the RADV ICD plus `VK_LOADER_LAYERS_DISABLE='~implicit~'` on this runner). Set `TEST_GPU_VALIDATION_PRESET=off` to disable it.*
*Optional log filtering: same `TEST_LOG_PROFILE` / `LOG_CMDS` behavior is supported for this path.*
*Session wrapper controls: `TEST_SESSION_WRAPPER=auto|uwsm|none`, `TEST_USE_GAMESCOPE=1|0`, `TEST_GAMESCOPE_ARGS="..."`.*

**GPU/Vulkan Recovery / Triage Sequence:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
TEST_VK_LOADER_LAYERS_DISABLE='~implicit~' \
./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_GPU_VALIDATION_PRESET=off ./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_VIDEO_BACKEND=dummy TEST_RENDER_OFFSCREEN=1 ./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_SCOPE=gpu_smoke TEST_VIDEO_BACKEND=wayland TEST_RENDER_OFFSCREEN=0 \
TEST_SESSION_WRAPPER=uwsm ./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_SCOPE=gpu_smoke TEST_VIDEO_BACKEND=wayland TEST_RENDER_OFFSCREEN=0 \
TEST_SESSION_WRAPPER=uwsm TEST_USE_GAMESCOPE=1 ./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_FORCE_VULKAN_EXTENSIONS=1 ./Scripts/run_tests_full.sh
```

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_SCOPE=gpu_smoke ./Scripts/run_tests_full.sh
```

Interpretation:
- failure before `Found ... automation tests` points first to ICD/layer/display/device bring-up;
- `TEST_SCOPE=gpu_domain` is the fallback when you want GPU-domain behavior coverage without relying on successful Vulkan device creation;
- a green `benchmark` scope still does not imply meaningful GPU behavioral coverage if the benchmark body is intentionally skipped.

**Schema Manifest Export (Code-First Contract):**
```python
from FlightProject import SchemaTools
SchemaTools.export_manifest()  # writes Saved/Flight/Schema/requirements_manifest.json
```

**Schema Bucket (Headless, Fast Validation):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
PROJECT_DIR=/home/kelly/Unreal/Projects/FlightProject
stdbuf -oL -eL "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
  "$PROJECT_DIR/FlightProject.uproject" \
  -ExecCmds="Automation RunTests FlightProject.Schema; quit" \
  -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
  -NullRHI -NoPCH -NoBT -NoSound -NoDDCMaintenance \
  -DDC=NoZenLocalFallback -LocalDataCachePath="$PROJECT_DIR/DerivedDataCache"
```

**Non-Parser Hardening Bucket (Schema + Verse Compile Contract):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh \
  --preset=triage \
  --filter="FlightProject.Integration.SchemaDriven+FlightProject.Schema.Vex.ManifestValidation+FlightProject.Verse.CompileContract+FlightProject.Verse.Subsystem"
```

**Assembler Scaffold Smoke Test:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh \
  --preset=triage \
  --filter="FlightProject.Verse.AssemblerScaffold"
```

**Parser Bucket (Focused):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh \
  --preset=triage \
  --filter="FlightProject.Schema.Vex.Parser"
```

**Mixed Bucket (Schema + Verse + Parser):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh \
  --preset=triage \
  --filter="FlightProject.Integration.SchemaDriven+FlightProject.Schema.Vex.ManifestValidation+FlightProject.Verse.CompileContract+FlightProject.Verse.Subsystem+FlightProject.Schema.Vex.Parser"
```

**Extended Mixed Bucket (Schema + Verse + Parser + Vertical Slice):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_headless.sh \
  --preset=triage \
  --filter="FlightProject.Integration.SchemaDriven+FlightProject.Schema.Vex.ManifestValidation+FlightProject.Verse.CompileContract+FlightProject.Verse.Subsystem+FlightProject.Schema.Vex.Parser+FlightProject.Integration.Vex.VerticalSlice"
```

**Swarm Persistence Bucket (new deterministic tests):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
PROJECT_DIR=/home/kelly/Unreal/Projects/FlightProject
stdbuf -oL -eL "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
  "$PROJECT_DIR/FlightProject.uproject" \
  -ExecCmds="Automation RunTests FlightProject.Swarm.Pipeline.Persistence; quit" \
  -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
  -NoPCH -NoBT -NoSound -NoDDCMaintenance \
  -DDC=NoZenLocalFallback -LocalDataCachePath="$PROJECT_DIR/DerivedDataCache"
```

### Flight Log Viewer Query Filters
The Flight Log Viewer search box supports structured query tokens:
- `cat:LogFlightAI` include categories by substring.
- `-cat:Mass` exclude category substrings.
- `thread:42` restrict to a specific thread ID.
- `frame:100-200` restrict to frame range (also supports `frame:120`, `frame:120-`, `frame:-200`).
- Plain terms still match message/category text; prefix with `-` to exclude (`-deprecated`).

### Phase 0 Stabilization Coverage
The following tests now act as guardrails for phase-0 safety fixes:
- `FlightProject.Functional.Async.ChainPropagation`
- `FlightProject.Logging.Core.QueryFilter`
- `FlightProject.Logging.Core.RingBufferStats`
- `FlightProject.Reactive.Core.EffectTeardown`
- `FlightProject.Reactive.Core.SubscriptionLifecycle`
- `FlightProject.Reflection.Core.SerializationPolicy`
- `FlightProject.Reflection.Core.DiffPolicy`

### Phase 1 Contract PoC Coverage
- `FlightProject.Schema.Manifest.NiagaraContract`
- `FlightProject.Schema.Manifest.Export`
- `FlightProject.Schema.Niagara.MissingSystem`
- `FlightProject.Schema.Manifest.CVarContract`
- `FlightProject.Schema.CVar.MissingVariable`
- `FlightProject.Schema.CVar.Profile.HeadlessValidation`
- `FlightProject.Schema.Manifest.PluginContract`
- `FlightProject.Schema.Manifest.VexSymbolContract`
- `FlightProject.Schema.Plugin.Profile.HeadlessValidation`
- `FlightProject.Schema.Plugin.MissingPlugin`

## 1.1 Observed-vs-Declared Contract Artifacts
Phase 1 now includes declared-vs-observed drift checks and snapshot artifacts for headless runs:

- CVar observed snapshot:
  - `Saved/Automation/Tmp/observed_cvars_headless_validation_test.json`
- Plugin observed snapshot:
  - `Saved/Automation/Tmp/observed_plugins_headless_validation_test.json`

Both artifacts include:
- declared requirement identity (`owner`, `requirementId`, `profileName`)
- expected values/policy
- observed runtime values/state
- `matches` boolean and mismatch `issue` when drift is detected

## 2. Documentation Links
- [Project README](Projects/FlightProject/Docs/README.md): High-level project overview and setup.
- [Workflow Guide](Projects/FlightProject/Docs/Workflow/README.md): Development cycles and CI patterns.
- [Architecture Docs](Projects/FlightProject/Docs/Architecture/): Deep technical design specifications.

## 3. Structured Testing Strategy
As the project grows, we organize tests into a hierarchical structure:

### Directory Organization
- `Source/FlightProject/Private/Tests/Unit/Core/`: Reflection, RowTypes, Result traits.
- `Source/FlightProject/Private/Tests/Unit/Mass/`: Fragment composition, Optics queries.
- `Source/FlightProject/Private/Tests/Integration/UI/`: Slate ReactiveUI bindings.
- `Source/FlightProject/Private/Tests/Integration/IoUring/`: GPU bridge and async completion.

### Test Categories
- **Smoke Tests (`SmokeFilter`)**: Fast, trait-level verification (run every build).
- **Engine Tests (`EngineFilter`)**: Integration tests requiring a full World/Subsystem state.

## 4. Build Environments & Contexts

### Context Types
- **EditorContext**: Required for tests run via `UnrealEditor-Cmd`.
- **ClientContext**: Tests behavior in the standalone game client.

### Build Variations
- **Development**: `WITH_DEV_AUTOMATION_TESTS=1`. Includes debug symbols and test registry.
- **Shipping**: `WITH_DEV_AUTOMATION_TESTS=0`. Minimal overhead, no tests included.

## 5. Performance & Environment Configuration

### Shader Compilation Optimization
To handle massive shader queues (e.g., global recompiles), we maximize core utilization in `DefaultEngine.ini`:
- **Core Allocation**: 15 out of 16 logical threads (on 8-core CPU) are assigned to workers.
- **Batching**: `MaxShaderJobBatchSize` set to 16 to reduce worker launch overhead.
- **Priority**: Workers run at `Below Normal` priority to keep the Editor UI responsive.

### Editor Responsiveness (Linux/Wayland)
Specific tuning in `LinuxEngine.ini` for high-performance development:
- **UI Refresh**: `Slate.TickRate=120` for high-Hz display smoothness.
- **Background Ticking**: `Editor.Performance.ThrottleUnfocused=0` to prevent "wake-up" lag.
- **Task Graph**: 12 worker threads allocated for runtime tasks, 4 high-priority threads reserved for UI/Main loop.
- **Anti-Aliasing**: Forced TAA (`r.AntiAliasingMethod=2`) and enabled `r.Editor.MovingGizmoTAA` to fix "jaggy" widgets.

### Display & Resolution
- **Native Resolution**: Forced `r.setres=1920x1200` to align with hardware and prevent Wayland compositor clipping.
- **High DPI**: Recommended to **Disable High DPI Support** in Editor Preferences when running on Wayland to avoid coordinate scaling conflicts.

## 6. Pragma & Macro Usage
- **Optimization Guards**: Use `PRAGMA_DISABLE_OPTIMIZATION` around sensitive templates if Clang 20 recursion depth becomes an issue.
- **Macro Semicolons**: Custom macros like `FLIGHT_REFLECT_FIELDS` provide trailing semicolons for standard C++ compatibility.
