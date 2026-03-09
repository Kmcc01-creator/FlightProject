# FlightProject Testing Validation Plan

## Objective

Establish a repeatable full-suite validation flow for FlightProject that:

- runs generated/complex validation before simple automation checks,
- surfaces architecture breakpoints early,
- makes failures actionable from logs and source locations.

## Current Test Topology

Based on current `Source/FlightProject/Private/Tests` declarations:

- `65` simple automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`)
- `2` complex/generated tests (`IMPLEMENT_COMPLEX_AUTOMATION_TEST`)
- `3` spec tests (`BEGIN_DEFINE_SPEC`)
- `18` test files with declared automation/spec entries

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

- Pass: `29` tests
  - filter: `FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder+FlightProject.Schema+FlightProject.Integration.SchemaDriven`
  - log: `Saved/Logs/FlightProject-backup-2026.03.09-04.00.43.log:891,1105`
- Fail: `15` tests scope run (single failing test in that scope)
  - filter: `FlightProject.Vex.Simd+FlightProject.Verse+FlightProject.Integration.Vex.VerticalSlice+FlightProject.Vex.Parser.Spec`
  - log: `Saved/Logs/FlightProject-backup-2026.03.09-04.01.38.log:891,1015`
- Pass: `2` tests
  - filter: `FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder`
  - log: `Saved/Logs/FlightProject-backup-2026.03.09-04.02.11.log:891,915`
- Fail (isolated): `1` test
  - filter: `FlightProject.Vex.Simd.Parity`
  - log: `Saved/Logs/FlightProject.log:891,917`

## Current Architecture Failure Points

1. No phase ordering in runner scripts.
- Current scripts do not enforce generated/spec-first execution.

2. Shader-compile suppression coupling.
- `-NoShaderCompile` is convenient for headless speed, but can conflict with startup paths that query shader readiness during subsystem initialization.
- reference startup path: `Source/FlightProject/Private/IoUring/FlightGpuPerceptionSubsystem.cpp:47`

3. Confirmed logic regression in SIMD parity.
- `FlightProject.Vex.Simd.Parity` fails in both execution paths:
  - gather/scatter parity checks
  - direct SoA parity checks
- failing assertions:
  - `Source/FlightProject/Private/Tests/FlightVexSimdTests.cpp:61`
  - `Source/FlightProject/Private/Tests/FlightVexSimdTests.cpp:81`
- Since both paths fail, the likely fault is shared (IR lowering, SIMD op semantics, or math intrinsic behavior), not only marshaling layout.

4. Verification coverage gap in default build verification.
- `build_targets.sh --verify` does not cover `FlightProject.Vex.Simd.Parity`.

## Recommended Full-Suite Execution Order

Preferred entrypoint is `./Scripts/run_tests_phased.sh` (now encodes this ordering).  
Equivalent explicit phased commands are listed below for manual/debug runs.

### Phase 1: Complex/Generated + Spec (run first)

```bash
TEST_STREAM_FILTER=errors ./Scripts/run_tests_headless.sh --timestamps --filter="FlightProject.Integration.SchemaDriven+FlightProject.Integration.Vex.VerticalSlice+FlightProject.Integration.Concurrency+FlightProject.Unit.Safety.MemoryLayout+FlightProject.Vex.Parser.Spec"
```

### Phase 2: Simple Automation (run second)

```bash
TEST_STREAM_FILTER=errors ./Scripts/run_tests_headless.sh --timestamps --filter="FlightProject.Schema+FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder+FlightProject.Vex.Parsing+FlightProject.Vex.Simd+FlightProject.Vex.UI+FlightProject.Verse+FlightProject.Verse.Bytecode+FlightProject.AutoRTFM+FlightProject.Gpu.Reactive+FlightProject.Logging+FlightProject.Swarm.Pipeline+FlightProject.Spatial.GpuPerception+FlightProject.Benchmark.GpuPerception+FlightProject.Reactive+FlightProject.Reflection+FlightProject.Functional"
```

### Phase 3: Optional GPU/System pass

```bash
TEST_SCOPE=all TEST_STREAM_FILTER=errors ./Scripts/run_tests_full.sh
```

## Immediate Next Targets

1. Fix `FlightProject.Vex.Simd.Parity` before treating the full suite as green.
2. Use the phased runner (`Scripts/run_tests_phased.sh`) as the default validation entrypoint for CI/local verification.
3. Update `build_targets.sh --verify` to include the phased headless flow or a minimum parity subset (`FlightProject.Vex.Simd.Parity`).
