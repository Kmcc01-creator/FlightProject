# FlightProject Testing Validation Plan

Status note:
Use `Docs/Workflow/CurrentBuild.md` as the dated source of truth for the latest build/test/GPU status.
This document should focus on validation topology and recommended execution structure, not carry the only current-status snapshot.

## Objective

Establish a repeatable full-suite validation flow for FlightProject that:

- runs generated/complex validation before simple automation checks,
- surfaces architecture breakpoints early,
- makes failures actionable from logs and source locations.

## Current Validation Status

Use **[CurrentBuild.md](CurrentBuild.md)** for the latest dated snapshot of build results, test evidence, and GPU discovery status.

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

For active goals and architecture risk points, see **[CurrentFocus.md](CurrentFocus.md)**.
