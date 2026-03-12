# Current Build & Test Documentation

This document outlines the build system configuration, test execution patterns, and strategies for structured development within FlightProject.

## 1. Test Execution & Discovery
This section captures the most recent observed build/test outcomes and commands used for repeatable triage.

### Latest Build Snapshot (2026-03-12)
- `./Scripts/build_targets.sh Development --no-uba` succeeds with the current mega-kernel startup/bootstrap changes.
- The generated mega-kernel shader include now has an explicit commandlet-safe bootstrap path:
  - `FlightProject` module startup now ensures `/FlightProject/Generated` is registered before shader-type initialization.
  - `UFlightMegaKernelSubsystem` now exposes a shared bootstrap helper and guarantees `Intermediate/Shaders/Generated/VexMegaKernel.ush` exists before later synthesis updates it.
- Verified on March 12, 2026 by deleting `Intermediate/Shaders/Generated/VexMegaKernel.ush`, then running:
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Integration.Orchestration.MegaKernel.Synthesis"`
- The commandlet run recreated the generated file and `Saved/Logs/FlightProject.log` now shows:
  - `LogShaders: Shader directory mapping /FlightProject/Generated -> ../../../../Projects/FlightProject/Intermediate/Shaders/Generated`
- The orchestration mega-kernel synthesis lane remains green after the startup change:
  - `FlightProject.Integration.Orchestration.MegaKernel.Synthesis`

### Latest Runtime Dispatch Gating Snapshot (2026-03-12)
- VEX/Verse runtime dispatch now resolves struct, bulk, and direct execution through shared backend-selection helpers instead of each surface using its own fallback ordering.
- `UFlightVerseSubsystem` now:
  - honors the recorded selected backend when that lane is executable on the current surface,
  - resolves `CommittedBackend` for direct struct execution through the same runtime selection logic,
  - exposes bulk/direct debug surfaces via `DescribeBulkExecutionBackend(...)` and `DescribeDirectExecutionBackend(...)`.
- Verified on March 12, 2026 with:
  - `./Scripts/run_tests_headless.sh --filter="FlightProject.Functional.Verse.BackendCommitTruth+FlightProject.Functional.Verse.RuntimeDispatchGating+FlightProject.Vex.Generalization.ExecuteOnMassHost+FlightProject.Functional.Vex.CompileArtifactReport.Core+FlightProject.Integration.Verse.CompileContract" --profile=focused --summary`
  - `./Scripts/run_tests_phased.sh --no-build --phase3-only`
  - `./Scripts/run_tests_phased.sh --no-build --phase4-only`
- Current result:
  - targeted runtime-dispatch and compile-artifact coverage passes,
  - the Mass-host direct execution surface still passes under the shared resolver,
  - Phase 3 passes,
  - Phase 4 passes.

### Latest Compile Policy / GPU Commitment Snapshot (2026-03-12)
- `CompileVex(...)` now resolves authored behavior compile policy through either explicit compile context or `UFlightDataSubsystem`, and carries selected policy metadata through behavior state, compile artifacts, and orchestration reports.
- Current policy-driven behavior:
  - preferred domain can steer selected backend,
  - native fallback and generated-only acceptance now respect policy,
  - policy-required symbols are validated during compile,
  - policy-required contracts now flow into orchestration legality surfaces.
- GPU-preferred policy is now explicit about commitment truth:
  - selected backend can remain `GpuKernel` when policy prefers that path,
  - committed backend stays `Unknown` until a real committed GPU runtime path exists,
  - orchestration no longer treats `resolvedDomain == Gpu` as executable by itself.
- Verified on March 12, 2026 with:
  - `./Scripts/run_tests_headless.sh --no-build --filter="FlightProject.Functional.Verse.CompilePolicyIntegration+FlightProject.Vex.Generalization.GpuPolicyCommitment+FlightProject.Functional.Verse.BackendCommitTruth+FlightProject.Functional.Verse.RuntimeDispatchGating+FlightProject.Vex.Generalization.ClassifyGpuHost+FlightProject.Integration.Verse.CompileContract" --profile=focused --summary`
  - `./Scripts/run_tests_phased.sh --no-build --phase3-only`
  - `./Scripts/run_tests_phased.sh --no-build --phase4-only`
- Current result:
  - explicit policy-selection coverage passes,
  - GPU-policy generated-only commitment truth passes,
  - Phase 3 passes,
  - Phase 4 passes.

### Latest Headless Automation Snapshot (2026-03-12)
- The Phase 2 order issue is now resolved and the headless phased lane is green again.
- Verified on March 12, 2026 with:
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Unit.Mass.NavigationCommitConstSharedFragmentIdentity"`
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Orchestration.Adapters.SpawnUsesExecutionPlanNavigationCommit+FlightProject.Orchestration.Adapters.SpawnUsesSelectedCandidateCommit"`
  - `TEST_PRESET=triage ./Scripts/run_tests_phased.sh --timestamps`
- Root cause:
  - `FFlightNavigationCommitSharedFragment` was being used as a Mass const shared fragment without reflected fields on its commit metadata,
  - so `GetOrCreateConstSharedFragment(...)` collapsed distinct navigation-commit instances to the same previously-created shared-fragment value,
  - which made the second spawn-commit test inherit the first test's waypoint-path commit metadata even though its `PathFollowFragment.PathId` was already the synthetic node path.
- Landed fix:
  - added `UPROPERTY()` reflection coverage to the navigation-commit shared-fragment fields in `Mass/FlightMassFragments.h`,
  - added a direct Mass-level regression test proving identical commit metadata reuses a const shared fragment while distinct commit metadata produces a different one,
  - kept the spawn/test hardening from the debugging pass:
    - explicit `AnchorId` seeding in the two spawn-commit automation tests,
    - stricter shared-fragment lookup in the test helper using the expected runtime path id,
    - batch-destroy + `FlushCommands()` in `DestroySwarmEntitiesDirect(...)`,
    - a spawner-side remove/reapply step for const shared fragments before adding new cohort/commit fragments.
- Current result:
  - `SpawnUsesExecutionPlanNavigationCommit` passes,
  - `SpawnUsesSelectedCandidateCommit` passes both in isolation and when run immediately after the waypoint-path commit test,
  - Phase 1 passes,
  - Phase 2 passes.

### Latest Reflected Identity Prototype Snapshot (2026-03-12)
- Preparatory architecture notes now live in:
  - `Docs/Architecture/ReflectedIdentityTypes.md`
- Navigation commit identity now has an explicit dual-reflection prototype:
  - `FFlightNavigationCommitIdentity` is a separate `USTRUCT` with reflected identity-bearing fields,
  - `FFlightNavigationCommitReport` now carries explanation/provenance fields separately,
  - both types are registered with FlightProject's trait reflection system,
  - `FFlightNavigationCommitSharedFragment` now carries `Identity` only instead of mixing runtime sameness with explanation fields.
- A reusable reflected-field dump utility now exists for both reflection lanes:
  - trait-reflected values via `Flight::Reflection::Debug::DumpReflectableFieldsToString(...)`
  - native `UStruct` values via `Flight::Reflection::Debug::DumpNativeStructFieldsToString(...)`
- Verified on March 12, 2026 with:
  - `./Scripts/build_targets.sh Development --no-uba`
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Unit.Navigation.CommitIdentityTraitReflection+FlightProject.Unit.Navigation.CommitIdentityNativeDump+FlightProject.Unit.Navigation.CommitReportTraitReflection+FlightProject.Unit.Navigation.CommitReportNativeDump+FlightProject.Unit.Mass.NavigationCommitConstSharedFragmentIdentity+FlightProject.Orchestration.Adapters.SpawnUsesExecutionPlanNavigationCommit+FlightProject.Orchestration.Adapters.SpawnUsesSelectedCandidateCommit+FlightProject.Navigation.VerticalSlice.Contracts"`
- Current result:
  - the new identity/report trait-reflection and native-reflection dump tests pass,
  - the Mass const-shared-fragment identity regression still passes,
  - the ordered spawn-commit tests still pass after the identity/report split,
  - the navigation contracts vertical slice still passes.

### Latest Orchestration Binding Identity/Report Snapshot (2026-03-12)
- Orchestration binding selection now follows the same role split used by navigation commit metadata:
  - `FFlightBehaviorBinding::FIdentity` carries the binding key (`CohortName`, `BehaviorID`),
  - `FFlightBehaviorBinding::FReport` carries execution and selection explanation (`ExecutionDomain`, `FrameInterval`, `bAsync`, `RequiredContracts`, and structured `Selection` provenance),
  - `FFlightBehaviorBinding` now composes those roles instead of mixing selection identity and explanation in one flat field bag.
- `UFlightOrchestrationSubsystem` now:
  - resolves bindings through `Identity`,
  - stamps `Report` during automatic selection, manual binding registration, default-cohort fallback, and generated fallback resolution,
  - exports both compatibility strings and a structured `selection` object in orchestration report JSON.
- `UFlightVexBehaviorProcessor` fallback resolution now also emits a structured binding report instead of only a chosen behavior id.
- Binding provenance is now less stringly:
  - source and rule are separate enums,
  - default-cohort fallback is a separate reflected fact instead of being concatenated into the selection rule string,
  - the fallback path now preserves the original source/rule rather than overwriting them.
- Added focused trait-reflection tests for the new binding roles:
  - `FlightProject.Unit.Orchestration.BindingIdentityReflection`
  - `FlightProject.Unit.Orchestration.BindingSelectionReflection`
  - `FlightProject.Unit.Orchestration.BindingReportReflection`
- Verified on March 12, 2026 with:
  - `./Scripts/build_targets.sh Development --no-uba`
  - `./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Unit.Orchestration.BindingIdentityReflection+FlightProject.Unit.Orchestration.BindingSelectionReflection+FlightProject.Unit.Orchestration.BindingReportReflection+FlightProject.Orchestration.Bindings.PlanPrefersExecutableBehavior+FlightProject.Orchestration.Bindings.ProcessorResolverPrefersOrchestration+FlightProject.Orchestration.Bindings.DefaultCohortFallbackProvenance+FlightProject.Orchestration.Bindings.AnchorPreferenceLegality+FlightProject.Orchestration.Bindings.AnchorContractLegality+FlightProject.Orchestration.Bindings.ProcessorResolverFallback"`
- Current result:
  - the new binding identity/selection/report reflection tests pass,
  - default-cohort fallback now preserves manual/automatic selection provenance while recording the fallback separately,
  - orchestration binding plan selection still prefers legal executable behaviors,
  - processor-side orchestration resolution and Verse fallback both still pass after the split.

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
