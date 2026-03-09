# Verse Assembler Scaffold (Solaris/uLang)

## Purpose
This document defines the initial `IAssemblerPass` scaffold strategy for FlightProject and evaluates feasibility of driving C++ runtime behavior through Verse/Solaris.

## Target Functionality (What We Want)
1. Compile generated Verse text into executable `VProcedure` bytecode.
2. Execute behaviors through Verse VM procedure invocation instead of fallback interpretation.
3. Bind curated C++ gameplay APIs as Verse-callable native functions.
4. Preserve schema/validation guarantees (residency, affinity, required symbols) before bytecode emission.
5. Keep deterministic, testable headless behavior for CI.

## Current Capability (What Exists Today)
1. Verse source generation and validation are working (`CProgramBuildManager` path).
2. VM bridge runs through `VProcedure` wrapper + native thunk + C++ fallback execution.
3. No engine-provided VerseVM-targeting `IAssemblerPass` implementation is present in this checkout.
4. Core bytecode infrastructure exists (`FOpEmitter`, `VProcedure`, VM interpreter).
5. Result: direct source-to-bytecode emission from toolchain is still missing.

## Scaffold Added In This Phase
1. Experimental `IAssemblerPass` implementation:
   - `Source/FlightProject/Public/Verse/FlightVerseAssemblerPass.h`
   - `Source/FlightProject/Private/Verse/FlightVerseAssemblerPass.cpp`
2. Module registration in startup/shutdown:
   - `Source/FlightProject/Private/FlightProject.cpp`
3. Smoke automation coverage:
   - `FlightProject.Verse.AssemblerScaffold`
   - `Source/FlightProject/Private/Tests/FlightVerseAssemblerTests.cpp`

Current scaffold behavior:
- Registers as a modular assembler feature.
- Confirms codegen/link hooks are reachable.
- Does not emit real bytecode yet (intentional).

## Viability: Verse/Solaris Driving C++ Source
Short answer: viable with a constrained API surface, not as unrestricted "script arbitrary C++".

What is viable:
1. Expose selected C++ systems as Verse-native call points (thunks/adapters).
2. Use Verse for behavior orchestration and state transitions.
3. Keep high-performance kernels/data transforms in C++/GPU pathways.

What is not a good target:
1. Directly "scripting all C++ source" from Verse.
2. Treating Verse as a transparent replacement for native engine subsystem logic.

Recommended model:
1. C++ remains authoritative for data layout, performance-critical execution, and safety boundaries.
2. Verse controls policy/orchestration through explicit, versioned bindings.
3. Schema + parser validation remain hard gates ahead of any VM compile/execute path.

## Delivery Phases
### Phase A: Scaffold Validation
1. Keep assembler registration green in editor/headless tests.
2. Verify no regressions in current VM wrapper + fallback runtime.

### Phase B: Minimal Bytecode Emission
1. Emit bytecode for minimal subset: literal, move, return.
2. Produce one callable `VProcedure` directly from semantic program data.
3. Add compile+execute regression test for this subset.

### Phase C: VEX-Supported Subset
1. Add assignment, arithmetic ops, and symbol loads/stores.
2. Route compatible generated Verse through emitted `VProcedure`.
3. Keep fallback for unsupported constructs and annotate diagnostics.

### Phase D: Native API Surface
1. Register stable C++ -> Verse native functions for gameplay APIs.
2. Add contract tests for argument validation, failure behavior, and determinism.

## Exit Criteria For "Production-Ready"
1. At least one non-trivial behavior compiles from Verse text to bytecode without fallback.
2. VM execution path is deterministic in headless automation.
3. Unsupported features fail with explicit diagnostics and fallback strategy.
4. CI includes assembler scaffold + compile contract + vertical slice coverage.

