# FlightProject Testing Validation Plan

Status note:
Use `Docs/Workflow/CurrentBuild.md` as the dated source of truth for the latest build/test/GPU status.
This document should focus on validation topology and recommended execution structure, not carry the only current-status snapshot.

## Objective

Establish a repeatable full-suite validation flow for FlightProject that:

- sorts tests by validation intent rather than only by macro type,
- runs generated and architecture-shaping validation before broad simple automation checks,
- surfaces architecture breakpoints early,
- makes failures actionable from logs and source locations.

## Current Validation Status

Use **[CurrentBuild.md](CurrentBuild.md)** for the latest dated snapshot of build results, test evidence, and GPU discovery status.

## Current Test Topology

Based on current `Source/FlightProject/Private/Tests` declarations:

- `117` simple automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`)
- `11` complex automation tests (`IMPLEMENT_COMPLEX_AUTOMATION_TEST`)
- `3` spec tests (`BEGIN_DEFINE_SPEC`)
- `41` test files with declared automation/spec entries

Macro type is no longer a reliable topology boundary by itself:

- some `IMPLEMENT_COMPLEX_AUTOMATION_TEST` suites are truly generated/data-driven
- others are manually curated multi-case integration suites
- a few simple tests are really development/discovery probes for source architecture seams

## Proposed Topology Categories

Treat topology as an intent-based classification.
The macro used to register a test is an implementation detail, not the category.

### 1. Simple Unit Automation

Use for focused contract checks with one primary assertion surface.
These are usually `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, but the important property is scope, not macro choice.

Current fit examples:

- `FlightProject.Unit.*`
- `FlightProject.Schema.*`
- `FlightProject.Logging.Core.*`
- `FlightProject.Unit.Navigation.*`
- `FlightProject.Unit.Orchestration.*`

### 2. Multi-Case Integration Tests

Use for manually authored, cross-subsystem, world-backed, or multi-scenario suites.
These may be simple, complex, or spec-based depending on the fixture shape.

Current fit examples:

- `FlightProject.Integration.Startup.Sequencing`
- `FlightProject.Orchestration.*`
- `FlightProject.Navigation.VerticalSlice.Contracts`
- `FlightProject.Functional.*`
- `FlightProject.Logging.Complex`
- `FlightProject.Logging.Boundaries`
- `FlightProject.Reflection.Complex`
- `FlightProject.Integration.Verse.CompileContract`

### 3. Generated Automation Tests

Reserve this category for suites that actually emit cases from manifests, registries, reflected types, or other source-of-truth data in `GetTests(...)`.
This should not be used for fixed manual dispatch tables.

Current fit examples:

- `FlightProject.Integration.Generative.ProjectManifest`
- `FlightProject.Integration.Generative.StructuralParity`
- `FlightProject.Integration.SchemaDriven`
- `FlightProject.Integration.Vex.VerticalSlice`

### 4. Architecture / Development Tests

Keep a narrow architecture-development bucket for discovery tests, refactor guards, and source-architecture seams that are valuable during active system evolution but are not the best long-term fit for product-facing `Unit` or `Integration` names.

Recommended naming direction:

- `FlightProject.Architecture.*` for durable architecture invariants
- `FlightProject.Dev.Architecture.*` only when a suite is explicitly temporary or refactor-scoped

Current likely candidates:

- `FlightProject.Vex.Frontend.ModularRefactor`
- `FlightProject.Vex.Generalization.*`
- `FlightProject.Vex.Schema.*`
- `FlightProject.IoUring.Vulkan.Complex`

These should stay narrow.
When an architecture/development test stabilizes into a normal runtime contract, move it into `Unit`, `Integration`, or `Generated` rather than letting the architecture bucket become a catch-all.

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

### `Scripts/run_tests_phased.sh`

- Encodes the current topology directly:
  - Phase 1: generated + spec
  - Phase 2: architecture / development
  - Phase 3: multi-case integration
  - Phase 4: simple unit automation
  - Phase 5: optional GPU/system pass
- Runs one shared pre-test build before child phases by default.
- Supports `--print-plan` / `--dry-run` to inspect resolved filters and build settings without launching Unreal.

### `Scripts/run_tests_full.sh`

- GPU/Vulkan path with scope switching (`benchmark`, `gpu_smoke`, `all`)
  - reference: `Scripts/run_tests_full.sh:48-57`
- Also a single `Automation RunTests` pass with no staged ordering.

### `Scripts/build_targets.sh`

- `--verify` runs only `--breaking` subset
  - reference: `Scripts/build_targets.sh:53`
- This is useful for a gate, but not a full logic validation pass.

## Recommended Full-Suite Execution Order

Use the following as the intended topology order.
`run_tests_phased.sh` now encodes this model directly.

### Phase 1: Generated + Spec (run first)

Run source-of-truth expansion suites and spec-based architecture checks first so schema/registry/runtime breakpoints fail early.

Current candidate namespaces:

- `FlightProject.Integration.Generative.*`
- `FlightProject.Integration.SchemaDriven`
- `FlightProject.Integration.Vex.VerticalSlice`
- `FlightProject.Integration.Concurrency`
- `FlightProject.Vex.Parser.Spec`
- `FlightProject.Unit.Safety.MemoryLayout`

### Phase 2: Architecture / Development (optional but early)

If retained as a distinct lane, run these after generated suites and before broad integration sweeps so active refactor seams fail close to the source.

Current candidate namespaces:

- `FlightProject.Vex.Frontend.*`
- `FlightProject.Vex.Generalization.*`
- `FlightProject.Vex.Schema.*`
- `FlightProject.IoUring.Vulkan.Complex`

### Phase 3: Multi-Case Integration

Run world-backed, cross-subsystem, and manually curated multi-scenario suites after generated and architecture-specific checks.

Current candidate namespaces:

- `FlightProject.Integration.Startup.Sequencing`
- `FlightProject.Orchestration.*`
- `FlightProject.Navigation.*`
- `FlightProject.Functional.*`
- `FlightProject.Logging.Complex`
- `FlightProject.Logging.Boundaries`
- `FlightProject.Reflection.Complex`
- `FlightProject.Integration.Verse.*`
- `FlightProject.Integration.AutoRTFM`
- `FlightProject.Integration.Vex.SimdParity`

### Phase 4: Simple Unit Automation

Run broad contract-level simple automation after the higher-leverage generated and integration surfaces.

Current candidate namespaces:

- `FlightProject.Unit.*`
- `FlightProject.Schema.*`
- `FlightProject.Logging.Core.*`
- `FlightProject.Vex.RewriteRegistry.*`
- `FlightProject.Vex.UI.*`

### Phase 5: Optional GPU / Perf / System pass

Keep GPU-required and performance-sensitive validation as an explicit final lane.
These are still important, but they should not define the base CPU/headless topology.

Current candidate namespaces:

- `FlightProject.Gpu.*`
- `FlightProject.Perf.*`

## Recommended Runner Entry Points

Inspect the resolved topology without launching Unreal:

```bash
./Scripts/run_tests_phased.sh --print-plan
```

Run the full headless topology with the shared pre-test build:

```bash
./Scripts/run_tests_phased.sh
```

Run only generated and architecture-shaping validation:

```bash
./Scripts/run_tests_phased.sh --phase1-only
./Scripts/run_tests_phased.sh --phase2-only
```

Run the full topology plus the optional GPU lane:

```bash
./Scripts/run_tests_phased.sh --with-gpu
```

## Immediate Next Targets

For active goals and architecture risk points, see **[CurrentFocus.md](CurrentFocus.md)**.
