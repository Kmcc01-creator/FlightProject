# Compiler Artifact Testing

This document defines a concrete design for testing FlightProject compiler outputs through structured artifacts rather than runtime behavior alone.

The goal is to make VEX, Verse VM, SIMD, and GPU lowering easier to reason about during development.
The goal is not to turn the test suite into a brittle diff against arbitrary machine code.

For output-placement and ownership rules, see `Docs/Architecture/ProjectOrganization.md`.
For target-aware capability and policy concepts, see `Docs/Architecture/TargetCapabilitySchema.md`.
For the current validation flow, see `Docs/Workflow/TestingValidationPlan.md`.

## 1. Problem Statement

FlightProject is building several compiler-adjacent surfaces at once:

- VEX parsing and validation
- VEX IR lowering
- Verse VM bytecode emission
- native fallback execution
- SIMD lowering
- GPU lowering

Current automation mostly validates:

- parse success and failure
- semantic contract enforcement
- runtime execution results
- compile diagnostics strings

That is necessary, but it is not sufficient for codegen-heavy work.

When a compiler or backend change regresses quality, the failure may appear as:

- extra temporaries
- avoidable scalarization
- missing subgroup use
- lost fused math
- unexpected control-flow shape
- fallback routing that still "works" functionally

These regressions are often easier to see in artifacts than in black-box runtime assertions.

## 2. Design Goal

Introduce a structured compiler-artifact testing layer that can:

1. emit machine-readable compile artifacts
2. persist stable snapshots for inspection
3. support automation assertions over artifact structure
4. optionally support native assembly inspection in controlled environments

This should complement semantic execution tests, not replace them.

## 3. Core Principle

Not all compile outputs have the same stability.

FlightProject should test them in layers:

1. stable semantic artifacts first
2. stable structural codegen artifacts second
3. target-specific backend artifacts third
4. exact machine assembly only for narrow, pinned cases

This avoids the common mistake of treating unstable compiler noise as a product regression.

## 4. Artifact Levels

### Level 1: Frontend And IR Artifacts

These are the most stable and should be the default CI target.

Examples:

- parsed AST summary
- validation issue list
- symbol usage table
- lowered `FVexIrProgram`
- control-flow graph summary

Use cases:

- parser correctness
- schema validation coverage
- lowering regressions
- control-flow normalization checks

### Level 2: Verse VM Bytecode Artifacts

These are the preferred structural testing surface for the Verse path.

Examples:

- `VProcedure` opcode listing
- block labels and jump targets
- constant table summary
- native thunk call sites
- suspend/block/yield-capable op counts

Use cases:

- verify IR-to-VVM lowering shape
- verify control-flow assembly
- verify thunk insertion
- verify direct-procedure vs entry-thunk routing

This should become the primary code-inspection surface for the current Verse VM work.

### Level 3: Backend Shader Or Intermediate Artifacts

These are target-aware but still useful in automation.

Examples:

- generated HLSL text
- SPIR-V disassembly
- DXIL or DXBC disassembly
- workgroup and subgroup metadata
- occupancy and register-pressure estimates

Use cases:

- verify wave-op usage
- verify residency and symbol mapping
- verify threadgroup policy
- verify backend routing decisions

### Level 4: Native Assembly Artifacts

These are useful, but only in tightly controlled situations.

Examples:

- x86_64 disassembly of a microkernel
- AArch64 disassembly of a microkernel
- compiler-generated assembly for isolated SIMD helper functions

Use cases:

- validate microkernel shape
- inspect register-pressure heuristics
- confirm fused-math or vectorization outcomes

These should not become the default correctness gate for broad project code.

## 5. Artifact Policy

FlightProject should distinguish:

- snapshot artifacts
- assertion inputs
- developer-inspection outputs

Recommended rule:

- every artifact emitted for testing must be machine-readable
- human-readable disassembly can be generated alongside JSON
- tests should prefer structural assertions over raw text equality

That means:

- JSON or normalized text for stable comparisons
- plain disassembly text for developer review
- exact text goldens only where the backend and toolchain are pinned

## 6. Proposed Core Types

### 6.1 Compile Artifact Report

Introduce a plain report struct:

```text
FFlightCompileArtifactReport
```

Recommended fields:

```text
SchemaVersion
CompilerPolicyVersion
SourceHash
BehaviorId / TestCaseId
ArtifactKind
TargetFingerprint
CompileOutcome
Diagnostics
IRSummary
BytecodeSummary
BackendSummary
Telemetry
ArtifactPaths
```

### 6.2 Artifact Kinds

Recommended enum:

```text
EFlightCompileArtifactKind
- AstSummary
- IrProgram
- VvmBytecode
- HlslText
- SpirvDisassembly
- NativeAssembly
- CompileTelemetry
```

### 6.3 Assertion Spec

Introduce a simple assertion contract:

```text
FFlightArtifactAssertionSpec
```

Recommended categories:

- required substring
- forbidden substring
- required opcode count
- max opcode count
- required telemetry key/value
- required pressure class
- required backend path

This keeps tests structural rather than fragile.

## 7. Proposed Storage Layout

Artifacts should live under `Saved/Flight/`, not under `Content/`.

Recommended layout:

```text
Saved/Flight/CompilerArtifacts/
    latest/
        <test-or-behavior-key>/
            report.json
            ir.json
            bytecode.vvm.txt
            hlsl.txt
            spirv.txt
            asm.s
    history/
        <timestamp>_<test-or-behavior-key>/
            ...
```

Recommended rule:

- `latest/` is overwritten by the most recent accepted run
- `history/` is optional and should be pruned by retention policy

## 8. Ownership

Ownership should align with existing domain rules.

| Concern | Recommended Owner |
| --- | --- |
| artifact report contract | `Schema/` or small shared compiler-artifact header |
| AST/IR serialization | `Vex/` |
| VVM bytecode disassembly | `Verse/` |
| SIMD/native kernel artifact emission | `Vex/` |
| HLSL/SPIR-V artifact emission | GPU codegen owner |
| world/report export integration | `Orchestration/` or scripting/debug layer |
| file persistence helpers | `FlightScriptingLibrary` or compiler diagnostics helper layer |

Do not bury artifact emission logic directly in gameplay framework code.

## 9. Test Taxonomy

Artifact testing should use more than one automation style.

### 9.1 Simple Tests

Use for:

- one-off bytecode shape assertions
- one-off IR lowering checks
- focused regression coverage

Examples:

- literal return emits one constant and one return
- symbol load/store emits expected thunk sequence
- loop lowering emits backedge jump

### 9.2 Complex/Generated Tests

Use for:

- corpus-driven codegen checks
- symbol family coverage
- target-profile matrix runs
- backend capability permutations

This fits the existing pattern already used by generated tests such as `FlightProject.Integration.Vex.VerticalSlice`.

### 9.3 Developer-Only Inspection Tests

Use for:

- native assembly microkernel inspection
- pinned-toolchain performance experiments
- architecture-specific regression studies

These should not run in the broadest default CI lane unless the environment is controlled.

## 10. Assertion Strategy

The suite should prefer:

1. structural assertions
2. normalized snapshots
3. exact text matching only for tightly pinned outputs

Good assertions:

- contains opcode `Return`
- contains exactly one symbol load
- no native fallback path selected
- pressure class is `Low`
- subgroup ops enabled
- threadgroup size equals policy default

Bad default assertions:

- full native assembly text equals golden on every machine
- exact register names in broad CI
- exact block numbering if numbering is not normalized

## 11. Native Assembly Policy

Native assembly inspection is valuable, but it must be constrained.

Recommended rules:

1. restrict native assembly tests to isolated kernels
2. pin target triple, compiler, and optimization level
3. treat assembly as a specialized lane, not the universal gate
4. normalize obvious nondeterminism before comparison

Suitable targets:

- branchless math kernels
- vector pack/unpack helpers
- tiny rematerialization experiments
- register-pressure-sensitive microbenchmarks

Unsuitable targets:

- large mixed gameplay functions
- broad subsystem methods with unstable inlining context
- code whose shape depends on unrelated compilation-unit changes

## 12. Verse VM Specific Guidance

For the current Verse work, the most important addition is a VVM bytecode disassembler/test surface.

Recommended additions:

1. `DisassembleProcedure(const Verse::VProcedure&) -> FString`
2. `BuildProcedureSummary(const Verse::VProcedure&) -> FFlightVvmBytecodeSummary`
3. artifact export helper for `FVexVvmAssembler`

Recommended VVM assertions:

- opcode sequence shape
- block count
- constant table contents
- native thunk references
- direct procedure vs VM entry thunk path
- whether emitted code contains suspend-capable operations

This is more stable and more semantically useful than jumping immediately to host machine assembly.

## 13. SIMD And Native Backend Guidance

For SIMD and host-native codegen, FlightProject should separate:

- compile-shape assertions
- performance observations
- exact assembly inspection

Recommended telemetry for SIMD artifacts:

- selected vector width
- estimated live temporary count
- pressure class
- fallback reason if SIMD was rejected
- intrinsic usage list

This will usually provide more durable value than exact register-name comparisons.

## 14. GPU Backend Guidance

For GPU codegen, artifact tests should emphasize:

- residency correctness
- subgroup and wave-op eligibility
- workgroup-size choice
- occupancy-sensitive estimates
- generated synchronization points

Recommended outputs:

- HLSL source
- backend metadata summary
- disassembly when available
- register-pressure or occupancy estimate

## 15. Environment Classes

Not all artifact tests should run everywhere.

Recommended environment classes:

### Class A: Universal Headless

Runs in normal `UnrealEditor-Cmd` CI.

Includes:

- AST/IR snapshots
- VVM bytecode snapshots
- compile telemetry checks

### Class B: Backend-Capable

Runs where required backend tools or drivers exist.

Includes:

- HLSL/SPIR-V artifact checks
- target-profile-sensitive codegen assertions

### Class C: Pinned Toolchain Inspection

Runs in dedicated environments.

Includes:

- exact native assembly shape checks
- microkernel register-pressure studies

## 16. Failure Output Requirements

Artifact tests should fail with actionable output.

Recommended failure payload:

- test case key
- target fingerprint
- artifact path
- normalized diff summary
- relevant telemetry values
- concise reason for failure

Do not rely only on raw log spam.

## 17. Initial Rollout

### Phase 1: VVM Bytecode Inspection

1. Add `VProcedure` disassembly helper.
2. Export bytecode and summary artifacts under `Saved/Flight/CompilerArtifacts/`.
3. Add simple automation tests for:
   - literal return
   - load/store symbol path
   - branch and loop lowering

### Phase 2: Report Contract

1. Add `FFlightCompileArtifactReport`.
2. Persist machine-readable artifact metadata.
3. Add scripting/debug helper to export latest compile artifacts.

### Phase 3: Generated Artifact Tests

1. Add a complex automation test that iterates a corpus of VEX kernels.
2. Assert structural codegen properties instead of only runtime success.
3. Record per-case compile telemetry.

### Phase 4: GPU Artifact Lane

1. Persist HLSL and backend summaries.
2. Add subgroup and occupancy-oriented assertions.
3. Restrict these tests to capable environments.

### Phase 5: Native Assembly Microkernel Lane

1. Add isolated host-native microkernels.
2. Compile them with pinned flags and a pinned toolchain.
3. Assert narrow structural properties, not broad text equality.

## 18. Recommended First Implementation Surface

The first concrete surface should be:

- one bytecode disassembler
- one compile artifact report
- one generated artifact test corpus for VVM lowering

That gives immediate value to the Verse VM effort without taking on full native assembly instability.

## 19. Decision Summary

FlightProject should support compiler-artifact testing as a first-class validation surface.

The recommended model is:

1. treat IR and VVM bytecode as primary stable artifacts
2. treat GPU and native backend artifacts as secondary, target-aware artifacts
3. keep exact native assembly inspection narrow and environment-pinned
4. persist outputs under `Saved/Flight/CompilerArtifacts/`
5. use complex automation tests for corpus-style artifact validation

This gives the project a practical way to reason about compiler quality, backend routing, and register-pressure strategy without making the suite brittle or toolchain-dependent by default.
