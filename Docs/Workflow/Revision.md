# Revision Plan

Date: March 8, 2026  
Scope: document current failing/blocked automation tests, then drive a logging-focused debugging pass.

## 1) Current Test Reality (Evidence-Backed)

### 1.1 Last full headless automation run (NullRHI)
- Run command path: `Scripts/run_tests_headless.sh`
- Script executes:
  - `Automation RunTests FlightProject; quit` (`Scripts/run_tests_headless.sh:47`)
  - with `-NullRHI` and local DDC (`Scripts/run_tests_headless.sh:49-50`)
- Evidence log:
  - `Saved/Logs/FlightProject-backup-2026.03.08-02.52.18.log`
  - Discovery: `Found 49 automation tests based on 'FlightProject'` (line 970)
  - Result count: 48 success / 1 fail
  - Failing test: `FlightProject.Schema.Vex.Parser.MegaKernel` (line 1286)
  - Exit: `**** TEST COMPLETE. EXIT CODE: -1 ****` (line 1334)

### 1.2 Known functional failure
- Test: `FlightProject.Schema.Vex.Parser.MegaKernel`
- Source definition:
  - `Source/FlightProject/Private/Tests/FlightVexParserTests.cpp:319`
  - Assertions around alias/lowering are at lines 352-356.
- Observed failure signals in run log:
  - `Expected 'Script 2 parse' to be true`
  - `Expected 'Should use local position alias in case 1' to be true`
  - `Expected 'Should use local position alias in case 2' to be true`

### 1.3 GPU-related tests in headless context
- In headless run, GPU tests currently resolve as success-with-info-skip (expected with NullRHI):
  - `FlightProject.Benchmark.GpuPerception` -> "Skipping benchmark: GPU Perception not available."
  - `FlightProject.Spatial.GpuPerception.CallbackResolves` -> "subsystem unavailable."
  - `FlightProject.Swarm.Pipeline.FullIntegration` -> "no GPU-capable RHI is available."
- Source behavior is explicit:
  - `Source/FlightProject/Private/Tests/FlightGpuPerceptionTests.cpp:323-326`
  - `Source/FlightProject/Private/Tests/FlightGpuPerceptionTests.cpp:116-119`
  - `Source/FlightProject/Private/Tests/FlightSwarmPipelineTests.cpp:105-110`

### 1.4 Discovery mismatch still open
- Source currently defines 50 `FlightProject.*` tests.
- Last headless discovery found 49 tests.
- Missing in discovery: `FlightProject.Spatial.GpuPerception` (source at `FlightGpuPerceptionTests.cpp:19`).
- This is already tracked as an open item in `AGENTS.md:45-47`.

### 1.5 New blocker on recent targeted runs
- More recent targeted runs (March 8, 2026 around 03:03 and 03:06) failed before automation execution:
  - `Saved/Logs/FlightProject-backup-2026.03.08-03.03.45.log`
  - `Saved/Logs/FlightProject.log`
- Fatal source: shader compile failures in project shader files, then engine fatal in shader compiler path.
- Practical impact: some targeted `RunTests` commands are currently blocked until shader compile state is repaired.

## 2) Logging Adventure: Unreal vs Flight Logging

## 2.1 Track A: Unreal-native automation logging (ground truth for CI)
- Primary channels to rely on for pass/fail:
  - `LogAutomationCommandLine`
  - `LogAutomationController`
  - `LogAutomationWorker`
  - `AutomationTestingLog`
- Current script-level control:
  - `TEST_LOG_PROFILE` and `LOG_CMDS` in:
    - `Scripts/run_tests_headless.sh`
    - `Scripts/run_tests_full.sh`
- Output source of truth:
  - `Saved/Logs/FlightProject*.log`
  - Parse `Found N automation tests`, `Test Completed`, `Error:`, and final exit line.

## 2.2 Track B: Flight custom logging framework (product/debug UX)
- Category layer:
  - `Source/FlightProject/Public/FlightLogCategories.h`
  - `Source/FlightProject/Private/FlightLogCategories.cpp`
- Capture + filtering layer:
  - `Source/FlightProject/Public/UI/FlightLogCapture.h`
  - `Source/FlightProject/Public/UI/FlightLogTypes.h`
  - `Source/FlightProject/Public/UI/FlightLogViewer.h`
- Existing test coverage:
  - `FlightProject.Logging.Core.QueryFilter`
  - `FlightProject.Logging.Core.RingBufferStats`
  - Tests at `Source/FlightProject/Private/Tests/FlightLogTests.cpp`

## 2.3 Practical distinction (keep this strict)
- Unreal automation logs decide run health and CI gate status.
- Flight log system improves in-editor observability and runtime diagnostics.
- We should not treat Flight UI log behavior as a proxy for automation pass/fail truth.

## 3) Investigation Sequence (No Code Changes First)

1. Reconfirm baseline headless run outcome (49 discovered, MegaKernel fail) using focused automation logs.
2. Run a narrow schema subset (`FlightProject.Schema`) to isolate parser failures from unrelated domains.
3. Run the single failing test (`FlightProject.Schema.Vex.Parser.MegaKernel`) and capture only parser/lowering-relevant logs.
4. Re-run GPU smoke/full path only after confirming shader compile stability, to avoid false negatives from startup shader fatals.
5. Compare discovered tests vs source-defined tests each run; keep an explicit "missing leaf tests" list.

## 4) Logging-Specific Questions To Answer

- Unreal-side:
  - Are we getting enough parser-specific signal under `TEST_LOG_PROFILE=focused`, or do we need temporary targeted verbosity for VEX components?
  - Should GPU-smoke scripts fail when they emit skip messages (as planned in `AGENTS.md`)?
- Flight-side:
  - Should parser/lowering code emit dedicated `LogFlight` category breadcrumbs to correlate with automation failures?
  - Do we need a lightweight export of filtered Flight log entries alongside automation artifacts for post-run diffing?

## 5) Exit Criteria For This Revision

- We can reproduce and explain the `MegaKernel` failure with deterministic logs.
- We can clearly separate:
  - test failure,
  - expected GPU skip in headless,
  - and pre-test shader compile blockers.
- We have an agreed logging profile strategy for:
  - routine CI runs (low noise),
  - and local failure triage (high signal).

## 6) SCSL Research Track: Texture3D Atlas vs Texture3D Array

### 6.1 Research Goal
- Establish a single, explicit resource model for SCSL field residency and addressing.
- Resolve ambiguity between legacy `*Array` naming, documented `Texture3DArray` intent, and current `Texture3D` runtime behavior.
- Define how atlas IDs/pointers are represented and consumed end-to-end (C++ reflection -> GPU buffers -> shader sampling/injection).

### 6.2 Current Evidence Snapshot (No-Code Audit)
- Documentation currently states `Texture3DArray` is implemented:
  - `Docs/Architecture/InstancedVexOrchestration.md`
  - `README.md`
- Runtime and shader parameter structs currently bind `Texture3D`/`RWTexture3D`, not array textures:
  - `Plugins/FlightGpuCompute/Source/FlightGpuCompute/Public/FlightSwarmShaders.h`
  - `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp`
- GPU allocations use `FRDGTextureDesc::Create3D(...)` for lattice/cloud resources:
  - `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp`
- Shader helpers expose `LatticeID`/`CloudID` parameters but currently do not use them for page/slice remapping:
  - `Shaders/Private/FlightSwarmCommon.ush`
- Reflected swarm state has no per-entity lattice/cloud selector (`@LatticeID`, `@CloudID`):
  - `Source/FlightProject/Public/Swarm/SwarmSimulationTypes.h`

### 6.3 Working Hypothesis
- The live pipeline is effectively single-volume-per-field with ping-pong propagation.
- Multi-instance field support is partially scaffolded in naming, but not yet materialized in data contracts or address mapping.

### 6.4 Research Plan (Still No Code Changes)

1. Build a resource ownership map.
   - Track texture lifetimes from `UFlightSwarmSubsystem` allocation through RDG passes and view extension extraction.
   - Confirm where persistent pointer ownership transitions occur for lattice/cloud fields.
2. Build an addressing map.
   - Enumerate every sample/inject callsite and identify whether addressing is global-space, array-slice, or atlas-page aware.
   - Catalog hardcoded defaults (`ID = 0`) and global grid assumptions.
3. Define a canonical field-handle model.
   - Specify required handle fields (for example `LatticePageID`, `CloudPageID`) and where they live (`FDroidState`, command buffer, or side buffer).
   - Specify invalid/sentinel semantics and fallback behavior.
4. Define atlas descriptor contract.
   - For each page: origin, extent, resolution, normalization rules, and bounds policy.
   - Define deterministic world->atlas UVW conversion and clamping behavior.
5. Define compatibility and migration phases.
   - Phase 0: preserve current behavior (`ID=0`, one page).
   - Phase 1: wire ID plumbing without changing visual output.
   - Phase 2: enable multiple active pages/instances.
6. Define verification strategy.
   - Add focused tests for ID routing and coordinate remapping.
   - Add log probes that report chosen field page/handle for failed assertions.

### 6.5 Logging Questions Specific to Atlas/Array Migration
- Unreal-side:
  - Which `LogFlight`/`LogAutomation*` messages are required to diagnose wrong page selection versus wrong coordinate mapping?
  - Should test scripts expose a dedicated filter profile for atlas-routing diagnostics?
- Flight-side:
  - Do we need a structured per-frame snapshot of active field pages (page id, extent, occupancy) for postmortem analysis?
  - Should VEX lowering emit explicit breadcrumbs when an ID-based sampling path is selected?

### 6.6 Decision Gates and Done Criteria (for this research track)
- We choose one canonical residency model and codify it in docs:
  - `Texture3DArray` slices, or
  - single `Texture3D` atlas pages, or
  - explicit single-volume (defer instancing).
- We have a written data contract for atlas/array IDs with source-of-truth owner for each field.
- We have a staged implementation plan with rollback-safe checkpoints and test/log coverage defined per phase.

### 6.7 Resource Ownership Map (Current Implementation)
- Frame entry:
  - `TickSimulation` passes `Self->LatticeTexture` and `Self->CloudTexture` into `InitResources(...)`.
- RDG allocation path:
  - `InitResources` creates fresh per-frame `Create3D(...)` textures for injection and propagation (`A/B`) for both lattice and cloud.
  - `InitResources` signature accepts `PersistentLattice/PersistentCloud`, but current body does not register or seed from them.
- Frame exit:
  - `QueueTextureExtraction` stores `LatticePropagateBArray` and `CloudPropagateBArray` into `Self->LatticeTexture` and `Self->CloudTexture`.
  - `ViewExtension->UpdateSwarmData_RenderThread(...)` receives those extracted textures for rendering.
- Observed implication:
  - Visual pass consumes extracted textures from the prior simulation submission, but simulation propagation appears to start from newly created textures each frame unless explicitly seeded (currently not visible in `InitResources`).

### 6.8 Addressing Map (Current Implementation)
- Global mapping model:
  - World-to-UVW uses `(WorldPos - GridCenter) / GridExtent + 0.5`.
  - Injection converts UVW to voxel coordinates with `GridResolution`.
- ID plumbing state:
  - `SampleLightLattice(..., LatticeID)` / `SampleCloudDensity(..., CloudID)` exist in helpers but ID parameters are not used for remapping.
  - Main force path currently calls these helpers with literal `0`.
- Texture addressing mode:
  - Lattice/cloud propagation and cloud sim use direct voxel `Load(...)` neighbor taps over a single 3D domain.
  - No page/slice offset logic appears in these passes.
- Render-side sampling mismatch to track:
  - `FlightSwarmRender.cpp` splat pass currently sets `GridCenter/GridExtent/GridResolution` to fixed constants (`0`, `10000`, `64`) instead of reading live reactive command parameters.

### 6.9 Immediate Research Follow-Ups
1. Confirm intended persistence semantics:
   - Should lattice/cloud propagation read prior-frame persistent textures, or is per-frame reset intentional?
2. Align render sampling contract:
   - Decide whether render pass should consume packed runtime lattice/cloud grid params to prevent drift when values are tuned.
3. Define handle ownership:
   - Decide whether field handle IDs belong in `FDroidState`, a sideband structured buffer, or a per-instance command block.
4. Define doc correction strategy:
   - Either update docs to reflect current single-volume behavior, or fast-track implementation milestones to match the documented `Texture3DArray`/instancing claims.

### 6.10 Residency Model Decision Matrix (Pre-Implementation)

| Option | Description | Pros | Risks / Costs |
| :--- | :--- | :--- | :--- |
| A | `Texture3DArray` slices for field instances | Clear per-instance indexing model, direct slice semantics, aligns with current docs | Requires broad shader/C++ parameter changes and slice-aware write paths; potential compatibility/perf caveats on some paths |
| B | Single `Texture3D` atlas with explicit page descriptors | Flexible sparse packing, can keep current `Texture3D` bindings, gradual migration from current code | Requires robust coordinate remapping contract and allocator discipline; debugging page mapping errors can be harder |
| C | Stay single-volume (defer instancing) | Lowest immediate risk, fastest stabilization for current tests | Conflicts with documented multi-instance goals; blocks sparse/open-world scaling plan |

### 6.11 Provisional Recommendation for Next Iteration
- Short term (stability): execute Option C behavior explicitly, fix persistence and render-parameter drift, and document as current truth.
- Medium term (instancing): adopt Option B (atlas + descriptor buffer) as migration path, because current runtime/shader bindings are already `Texture3D`-centric.
- Documentation guardrail:
  - Mark array/instancing docs as "target architecture" until code reaches parity.

### 6.12 Schema Contract Draft (Atlas/Page IDs)
- Draft contract document created:
  - `Docs/Architecture/SCSL_FieldResidencySchemaContract.md`
- Contract highlights:
  - Keeps current `FFlightVexSymbolRow` as the active symbol contract source.
  - Proposes two new schema rows for field residency:
    - `FFlightFieldPageRequirementRow`
    - `FFlightFieldBindingRow`
  - Proposes VEX symbol additions:
    - `@lattice_id`
    - `@cloud_id`
  - Defines compatibility mode:
    - No field rows -> single-page fallback (`PageId=0`) for both lattice/cloud.
  - Defines manifest versioning target:
    - `0.3` when field page/binding rows become active.

### 6.13 Focused Persistence Pass (Execution Plan)

Objective:
- Determine whether lattice/cloud data is intended to persist frame-to-frame, and make that behavior explicit and testable.

Known evidence to validate:
- `InitResources(...)` accepts `PersistentLattice/PersistentCloud` but currently allocates fresh propagation textures.
- `QueueTextureExtraction(...)` writes the final propagated textures back to subsystem-owned pooled textures.
- Render sampling currently uses fixed grid constants in the splat pass (`0`, `10000`, `64`), which may drift from reactive simulation parameters.

Pass steps (short-cycle):

1. Observability pass (no behavior change):
   - Add temporary logs/counters for:
     - whether persistent textures are valid at frame start
     - whether they are registered/consumed in RDG allocation path
     - extracted texture dimensions/formats at frame end
2. Persistence assertion pass:
   - Add a focused automation test that runs 2+ ticks and asserts expected persistence contract:
     - if persistent mode: prior frame contribution is visible in next frame propagation
     - if stateless mode: prior frame contribution is not expected
3. Render-contract alignment pass:
   - Route render splat sampling parameters from packed runtime command values (not fixed constants), then verify parity with simulation settings.
4. Artifact capture:
   - Export per-run trace artifact (JSON or log snapshot) with:
     - frame index
     - lattice/cloud resolution/extent
     - persistent texture validity
     - chosen contract mode (persistent/stateless)

Exit criteria for persistence track:
- We can state in one sentence whether fields are persistent or stateless in current profile.
- Automation has at least one deterministic check that fails on persistence regressions.
- Render sampling parameters are confirmed aligned with simulation parameters (or explicitly documented if intentionally decoupled).

### 6.14 Concrete Implementation Checklist (By File / Function)

#### A) Observability + Contract Toggle (first, no behavior change)

| Status | File | Function / Type | Implementation Task | Validation |
| :--- | :--- | :--- | :--- | :--- |
| [x] | `Source/FlightProject/Public/Swarm/FlightSwarmSubsystem.h` | `UFlightSwarmSubsystem` | Add a small debug snapshot struct for persistence tracing (frame index, persistent texture validity, effective resolutions/extents, selected mode). Add a read accessor for tests. | Unit/compile check + test can read snapshot after `TickSimulation`. |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` | `InitResources(...)` | Record whether `PersistentLattice` / `PersistentCloud` are valid and whether dimensions/formats match current command resolution. | Log line appears once per mode transition; snapshot fields populated. |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` | `TickSimulation(...)` | Emit explicit mode banner using `LogFlightSwarm` (`Persistent` vs `Stateless`) and include frame + grid params; stop using `LogTemp` for this path. | With focused logging enabled, mode line is visible in headless output. |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` | `DoesPersistentTextureMatch(...)` | Replace invalid pooled-target cast with descriptor validation through `FRHITextureDesc` from `FRDGPooledTexture::GetRHI()`. | `./Scripts/build_targets.sh Development` succeeds on Linux editor target. |

#### B) Persistence Seeding in Simulation Pipeline

| Status | File | Function / Type | Implementation Task | Validation |
| :--- | :--- | :--- | :--- | :--- |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` | `InitResources(...)` | Register external prior-frame textures when valid and seed `Ctx.CloudPropagateAArray` from persistent cloud texture via copy pass before cloud sim chain. | Two-tick run shows cloud state continuity (not reset each frame). |
| [x] | `Plugins/FlightGpuCompute/Source/FlightGpuCompute/Public/FlightSwarmShaders.h` | `FFlightLightConvertCS::FParameters` | Add optional prior-lattice input (`PrevLatticeArray`) and a small mode flag so convert pass can merge previous propagated lattice with new injection. | Shader compile succeeds; parameter binding is explicit. |
| [x] | `Shaders/Private/FlightLightLattice.usf` | `LatticeConvertMain` | Implement merge path: `Output = PrevLattice + CurrentInjection` (then propagation pass handles decay/diffusion). Keep compatibility path for stateless mode. | Visual continuity across ticks when persistence is enabled; parity with old behavior when disabled. |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` | `AddLightConvertPass(...)` | Bind prior lattice SRV and mode flag into convert pass; ensure pass order preserves deterministic write/read dependencies. | RDG execution stable; no resource hazard warnings; persistence test passes. |

#### C) Render Sampling Alignment (remove hardcoded grid constants)

| Status | File | Function / Type | Implementation Task | Validation |
| :--- | :--- | :--- | :--- | :--- |
| [x] | `Source/FlightProject/Public/Swarm/FlightSwarmRender.h` | `UpdateSwarmData_RenderThread(...)` | Extend API to pass effective lattice/cloud sampling params (center/extent/resolution) from subsystem to view extension. | Compile passes; API call sites updated. |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmRender.cpp` | `UpdateSwarmData_RenderThread(...)` / `PostProcessPass_RenderThread(...)` | Store passed params and replace fixed constants (`0`, `10000`, `64`) with runtime values for splat sampling. | Changing `LatticeExtent` / `LatticeResolution` at runtime affects render sampling consistently. |
| [x] | `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` | `TickSimulation(...)` | Pass packed command sampling params into `ViewExtension->UpdateSwarmData_RenderThread(...)`. | Runtime logs show identical simulation/render grid params per frame. |

#### D) Automation Coverage for Persistence

| Status | File | Function / Type | Implementation Task | Validation |
| :--- | :--- | :--- | :--- | :--- |
| [x] | `Source/FlightProject/Private/Tests/FlightSwarmPipelineTests.cpp` | new tests under `FlightProject.Swarm.Pipeline.*` | Add deterministic persistence tests (at least one for cloud, one for lattice) that run 2+ ticks and assert whether frame N+1 depends on frame N under persistent mode. | Tests fail if persistence regresses; skip cleanly on `NullRHI`. |
| [x] | `Source/FlightProject/Public/Swarm/FlightSwarmSubsystem.h` + `.cpp` | test-facing debug hooks | Expose minimal, test-safe counters/snapshots needed to assert persistence without introducing heavy readback overhead in normal runs. | Test does not rely on fragile log parsing only. |

#### E) Script/Logging Wiring for Triage

| Status | File | Function / Type | Implementation Task | Validation |
| :--- | :--- | :--- | :--- | :--- |
| [x] | `Scripts/run_tests_headless.sh` | focused profile `LOG_CMDS` | Ensure `LogFlightSwarm` is included in focused profile during persistence bring-up. | `TEST_LOG_PROFILE=focused` prints persistence mode/snapshot breadcrumbs. |
| [x] | `Docs/Workflow/CurrentBuild.md` | test command guidance | Add one command recipe for persistence triage runs and expected log markers. | Docs and script behavior match. |

#### F) Suggested Execution Order

1. A1-A3 (observability and mode banner)  
2. C1-C3 (render parameter alignment; low risk, high signal)  
3. B1-B4 (actual persistence seeding semantics)  
4. D1-D2 (automated regression coverage)  
5. E1-E2 (triage ergonomics + docs)

#### G) Completion Gate for Implementation

- `FlightProject.Swarm.Pipeline.FullIntegration` remains green (or expected-skip in `NullRHI`).
- New persistence tests pass in GPU-capable context.
- Focused logs include deterministic mode + parameter breadcrumbs.
- One line in docs states current mode explicitly: `Persistent` or `Stateless` for lattice/cloud.

### 6.15 Build Blocker Resolved (2026-03-08)

- `Source/FlightProject/Private/Swarm/FlightSwarmSubsystem.cpp` compile error fixed in `DoesPersistentTextureMatch(...)`.
- Replaced invalid cast path (`FRDGPooledTexture*` -> `IPooledRenderTarget*`) with direct RHI descriptor inspection:
  - `PersistentTexture->GetRHI()->GetDesc()`
  - checks now compare format + `Texture3D` dimension + extent/depth against expected resolution.
- `./Scripts/build_targets.sh Development` now completes successfully (`Result: Succeeded`).

### 6.16 Next Steps Plan (Persistence + Parser) (2026-03-08)

Immediate objective:
- Complete persistence behavior (not just observability) while keeping build/test loops short and debuggable.

Execution plan:
1. B1 implementation (`InitResources` cloud seeding).
   - Register prior cloud persistent texture when valid/matching and copy into cloud propagation input before cloud sim chain.
   - Validation: run two ticks in the same session and confirm continuity breadcrumbs in `LogFlightSwarm`.
2. B2-B4 implementation (lattice merge path).
   - Add prior lattice input and mode flag to convert shader parameters.
   - Implement shader merge path and bind/order passes in subsystem.
   - Validation: no RDG hazard warnings; targeted swarm tests still compile and execute.
3. D1-D2 automation coverage.
   - Add deterministic persistence tests in `FlightSwarmPipelineTests.cpp`.
   - Prefer assertions on debug snapshot/test hooks over brittle raw log string checks.
4. Parser failure follow-up in parallel.
   - Re-run `FlightProject.Schema.Vex.Parser` bucket with focused error filtering.
   - Triage failing assertions currently seen in:
     - `Diagnostics` (`FlightVexParserTests.cpp:60`)
     - `TypeMismatch` (`FlightVexParserTests.cpp:118`)
     - `IfConditionBoolLike` (`FlightVexParserTests.cpp:147`)

Definition of done for this pass:
- Development build remains green after persistence seeding changes.
- At least one new persistence regression test fails when prior-frame seeding is disabled.
- Parser diagnostics failures are either fixed or documented with root-cause notes + owner/next action.

### 6.17 Track B Completed (2026-03-08)

- Implemented cloud persistence seeding in `InitResources(...)`:
  - registers prior-frame cloud texture when requested/matching
  - copies prior cloud into `CloudPropagateAArray` before cloud simulation
  - falls back to volumetric black dummy when persistence is unavailable.
- Implemented lattice merge contract:
  - `FFlightLightConvertCS::FParameters` now includes `PrevLatticeArray` and `bUsePrevLattice`
  - `LatticeConvertMain` now writes `PrevLattice + CurrentInjection` when persistence is enabled
  - subsystem binds prior-lattice SRV and mode flag in `AddLightConvertPass(...)`.
- Development build confirmation after B changes:
  - `./Scripts/build_targets.sh Development` -> `Result: Succeeded` (March 8, 2026).

### 6.18 Track D Implemented (2026-03-08)

- Added two persistence-mode automation tests:
  - `FlightProject.Swarm.Pipeline.Persistence.StatelessMode`
  - `FlightProject.Swarm.Pipeline.Persistence.PersistentMode`
- Added test-facing snapshot fields:
  - `bUsedPriorLatticeFrame`
  - `bUsedPriorCloudFrame`
- Tests run two ticks with explicit persistence CVar mode and assert:
  - stateless mode does not consume prior frame fields
  - persistent mode consumes prior frame fields on second tick.
- Development build remains green after test/hook changes:
  - `./Scripts/build_targets.sh Development` -> `Result: Succeeded` (March 8, 2026).
