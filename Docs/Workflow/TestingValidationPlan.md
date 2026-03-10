# FlightProject Testing Validation Plan

## Objective

Establish a repeatable full-suite validation flow for FlightProject that:

- runs generated/complex validation before simple automation checks,
- surfaces architecture breakpoints early,
- makes failures actionable from logs and source locations.

## Status Snapshot (March 10, 2026)

- Phase 1 is currently green on the default `triage` preset.
- Phase 1 now includes the lightweight startup sequencing complex suite:
  - `FlightProject.Integration.Startup.Sequencing.BootstrapCompletionSignal`
  - `FlightProject.Integration.Startup.Sequencing.OrchestrationRebuildAdvancesPlan`
  - `FlightProject.Integration.Startup.Sequencing.StartupReportJsonSurface`
- Phase 2 is currently green after the earlier AutoRTFM + VEX SIMD fixes, the Milestone-0 logging/parser cleanup, and the deeper orchestration-binding slice that added anchor legality coverage.
- GPU phase remains environment-dependent and is still blocked before automation discovery in the current runner.

## Current Test Topology

Based on current `Source/FlightProject/Private/Tests` declarations:

- `81` simple automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`)
- `9` complex/generated tests (`IMPLEMENT_COMPLEX_AUTOMATION_TEST`)
- `3` spec tests (`BEGIN_DEFINE_SPEC`)
- `33` test files with declared automation/spec entries

Generated test frameworks:

- `FlightProject.Integration.SchemaDriven`
  - dynamically emits tests from manifest rows in `GetTests(...)`
  - reference: `Source/FlightProject/Private/Tests/FlightSchemaDrivenTests.cpp:17`
- `FlightProject.Integration.Vex.VerticalSlice`
  - dynamically emits tests per symbol contract in `GetTests(...)`
  - reference: `Source/FlightProject/Private/Tests/FlightVexVerticalSliceTests.cpp:87`

## Script Behavior Review

### `Scripts/run_tests_headless.sh`

- Runs a single pass using:
  - `Automation RunTests $TEST_FILTER; quit`
  - reference: `Scripts/run_tests_headless.sh:92`
- No built-in phase orchestration for complex-first then simple.
- `--verse` and `--simd` append `-NoShaderCompile`
  - reference: `Scripts/run_tests_headless.sh:33-43`
- `--no-shaders` also appends `-NoShaderCompile`
  - reference: `Scripts/run_tests_headless.sh:45-47`

### `Scripts/run_tests_full.sh`

- GPU/Vulkan path with scope switching (`benchmark`, `gpu_smoke`, `all`)
  - reference: `Scripts/run_tests_full.sh:48-57`
- Also a single `Automation RunTests` pass with no staged ordering.

### `Scripts/build_targets.sh`

- `--verify` runs only `--breaking` subset
  - reference: `Scripts/build_targets.sh:53`
- This is useful for a gate, but not a full logic validation pass.

## Evidence From Recent Runs

- Pass: startup sequencing complex suite (`3` tests)
  - filter: `FlightProject.Integration.Startup.Sequencing`
- Pass: orchestration binding bucket (`5` tests)
  - filter: `FlightProject.Orchestration.Bindings`
  - cases:
    - `PlanPrefersExecutableBehavior`
    - `ProcessorResolverPrefersOrchestration`
    - `AnchorPreferenceLegality`
    - `AnchorContractLegality`
    - `ProcessorResolverFallback`
- Pass: phase-1 automation filter (`27` tests)
  - filter: `FlightProject.Integration.SchemaDriven+FlightProject.Integration.Vex.VerticalSlice+FlightProject.Integration.Concurrency+FlightProject.Integration.Startup.Sequencing+FlightProject.Unit.Safety.MemoryLayout+FlightProject.Vex.Parser.Spec`
- Pass: phase-2 automation filter (`50` tests)
  - filter: `FlightProject.Schema+FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder+FlightProject.Vex.Parsing+FlightProject.Vex.Simd+FlightProject.Vex.UI+FlightProject.Verse+FlightProject.Verse.Bytecode+FlightProject.AutoRTFM+FlightProject.Gpu.Reactive+FlightProject.Logging+FlightProject.Orchestration+FlightProject.Swarm.Pipeline+FlightProject.Spatial.GpuPerception+FlightProject.Benchmark.GpuPerception+FlightProject.Reactive+FlightProject.Reflection+FlightProject.Functional`

## Current Architecture Risk Points

1. Phase ordering is addressed.
- `Scripts/run_tests_phased.sh` now enforces complex/spec-first, then phase-2 simple automation, with optional GPU phase.

2. Shader-compile suppression coupling remains an operational caveat.
- `-NoShaderCompile` is still useful for fast headless loops, but can interact with startup code paths that touch shader readiness.
- reference startup path: `Source/FlightProject/Private/IoUring/FlightGpuPerceptionSubsystem.cpp:47`

3. Phase-2 regression in AutoRTFM + SIMD parity is resolved.
- AutoRTFM integration test now validates transactional and fallback (runtime-disabled) semantics.
  - `Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp`
- VEX IR + SIMD execution fixes were applied for:
  - member-access lowering (`@position.x`) and unsupported-op diagnostics,
  - operand-kind handling (register vs constant),
  - scalar constant lane broadcast.
  - `Source/FlightProject/Private/Vex/FlightVexIr.cpp`
  - `Source/FlightProject/Private/Vex/FlightVexSimdExecutor.cpp`

4. Verification coverage gap remains for build-only verification.
- `build_targets.sh --verify` still runs a narrow `--breaking` subset and does not execute the full phased validation path.

## Recommended Full-Suite Execution Order

Preferred entrypoint is `./Scripts/run_tests_phased.sh` (now encodes this ordering).  
Equivalent explicit phased commands are listed below for manual/debug runs.

### Phase 1: Complex/Generated + Spec (run first)

```bash
TEST_STREAM_FILTER=errors ./Scripts/run_tests_headless.sh --timestamps --filter="FlightProject.Integration.SchemaDriven+FlightProject.Integration.Vex.VerticalSlice+FlightProject.Integration.Concurrency+FlightProject.Integration.Startup.Sequencing+FlightProject.Unit.Safety.MemoryLayout+FlightProject.Vex.Parser.Spec"
```

### Phase 2: Simple Automation (run second)

```bash
TEST_STREAM_FILTER=errors ./Scripts/run_tests_headless.sh --timestamps --filter="FlightProject.Schema+FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder+FlightProject.Vex.Parsing+FlightProject.Vex.Simd+FlightProject.Vex.UI+FlightProject.Verse+FlightProject.Verse.Bytecode+FlightProject.AutoRTFM+FlightProject.Gpu.Reactive+FlightProject.Logging+FlightProject.Orchestration+FlightProject.Swarm.Pipeline+FlightProject.Spatial.GpuPerception+FlightProject.Benchmark.GpuPerception+FlightProject.Reactive+FlightProject.Reflection+FlightProject.Functional"
```

### Phase 3: Optional GPU/System pass

```bash
TEST_SCOPE=all TEST_STREAM_FILTER=errors ./Scripts/run_tests_full.sh
```

## Immediate Next Targets

1. Keep the new orchestration binding bucket and the lightweight startup sequencing suite passing while the runtime boundary evolves.
2. Extend binding legality beyond default/exact fallback and current anchor-aware rules into startup-profile-aware behavior resolution and clearer legality reporting.
3. Add a dedicated startup fixture world/profile path when we are ready to assert full `StartPlay()` and post-spawn orchestration deltas.
4. Finalize GPU phase validation on Vulkan-capable or software-Vulkan environments and document pass/fail criteria.
