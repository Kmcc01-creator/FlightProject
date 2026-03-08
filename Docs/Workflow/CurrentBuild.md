# Current Build & Test Documentation

This document outlines the build system configuration, test execution patterns, and strategies for structured development within FlightProject.

## 1. Test Execution & Discovery
This section captures the most recent observed build/test outcomes and commands used for repeatable triage.

### Latest Build Snapshot (2026-03-08)
- `./Scripts/build_targets.sh Development` succeeded for `FlightProjectEditor` on Linux.
- Recent blocker resolved in `FlightSwarmSubsystem.cpp`:
  - invalid cast from `FRDGPooledTexture*` to `IPooledRenderTarget*`
  - replaced with `FRHITextureDesc` checks via `PersistentTexture->GetRHI()->GetDesc()`.

### Latest Headless Automation Snapshot (2026-03-08)
- `Automation RunTests FlightProject` discovered **49** tests in headless/`NullRHI`.
- Current known parser failures in focused runs:
  - `FlightProject.Schema.Vex.Parser.Semantics.IfConditionBoolLike`
  - `FlightProject.Schema.Vex.Parser.Semantics.TypeMismatch`
  - `FlightProject.Schema.Vex.Parser.Diagnostics`
- GPU-oriented tests still expected to skip in `NullRHI` contexts.

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
*Optional log filtering: set `TEST_LOG_PROFILE=focused|verbose|full` (default `full`) or provide explicit `LOG_CMDS` (which overrides profile selection).*
*Optional stream shaping: set `TEST_COLOR_MODE=auto|always|never` and `TEST_STREAM_FILTER=all|errors`.*

**Persistence Triage Command (SCSL field mode breadcrumbs):**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_LOG_PROFILE=focused TEST_STREAM_FILTER=all ./Scripts/run_tests_headless.sh
```
Expected marker (focused logs):
- `LogFlightSwarm: Display: Swarm persistence mode: Requested=... Applied=... LatticeValid=... LatticeMatch=... CloudValid=... CloudMatch=...`
Error-focused example:
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
TEST_LOG_PROFILE=focused TEST_COLOR_MODE=always TEST_STREAM_FILTER=errors ./Scripts/run_tests_headless.sh
```

**Headless GPU/Vulkan Benchmark Path:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
./Scripts/run_tests_full.sh
```
*Optional scope: set `TEST_SCOPE=benchmark|gpu_smoke|all` (default `benchmark`).*
*Optional log filtering: same `TEST_LOG_PROFILE` / `LOG_CMDS` behavior is supported for this path.*

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
