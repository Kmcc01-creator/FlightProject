# Boundary-Aware IR Implementation Plan

This document turns the boundary-aware IR direction into a concrete compiler plan against the current FlightProject source tree.

For the architectural rationale, see `Docs/Architecture/GpuComputeFrameworkRefinement.md`.
For candidate node/op names, see `Docs/Architecture/BoundaryAwareIrCandidates.md`.
For the current SchemaIR plan, see `Docs/Workflow/SchemaIrImplementationPlan.md`.

## 1. Goal

Add a boundary-aware semantic layer to the VEX compiler that can preserve and validate host/GPU and GPU/GPU boundary intent without destabilizing the existing arithmetic IR, SIMD path, Verse path, or current schema binding path.

The first goal is not to redesign the whole compiler.
The first goal is to create a stable place where operators such as `|`, `<|`, and `|>` can retain semantic meaning long enough for:

- schema legality checks
- latency-class selection
- runtime/backend commit
- truthful reporting

## 2. Core Decision

Boundary meaning should live above low-level `FlightVexIr` in the early phases.

That means:

- do not force boundary semantics directly into the current arithmetic/control-flow IR first
- preserve boundary meaning in the AST-to-SchemaIR binding path
- let low-level `FlightVexIr` remain focused on arithmetic, symbol load/store, and control flow until boundary planning is proven

This is the least disruptive path because:

- current SIMD execution depends on the existing low-level IR shape
- current HLSL and Verse lowering already compile from the existing IR
- the current schema binding layer already has the right conceptual role for semantic enrichment

## 3. Current Code Surfaces

The implementation will build on these existing files:

- `Source/FlightProject/Public/Vex/Frontend/VexAst.h`
- `Source/FlightProject/Public/Vex/Frontend/VexParser.h`
- `Source/FlightProject/Private/Vex/Frontend/VexParser.cpp`
- `Source/FlightProject/Public/Vex/FlightVexSchemaIr.h`
- `Source/FlightProject/Private/Vex/FlightVexSchemaBinder.cpp`
- `Source/FlightProject/Public/Vex/FlightVexIr.h`
- `Source/FlightProject/Private/Vex/FlightVexIr.cpp`
- `Source/FlightProject/Public/Vex/FlightCompileArtifacts.h`
- `Source/FlightProject/Private/Vex/FlightCompileArtifacts.cpp`
- `Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h`
- `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`
- `Source/FlightProject/Public/Orchestration/FlightMegaKernelSubsystem.h`
- `Source/FlightProject/Private/Orchestration/FlightMegaKernelSubsystem.cpp`
- `Source/FlightProject/Private/Tests/FlightVexParserTests.cpp`
- `Source/FlightProject/Private/Tests/FlightVexSchemaIrTests.cpp`
- `Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp`
- `Source/FlightProject/Private/Tests/FlightMegaKernelTests.cpp`

## 4. New Files

Recommended first file additions:

- `Source/FlightProject/Public/Vex/FlightVexBoundaryTypes.h`
- `Source/FlightProject/Private/Vex/FlightVexBoundaryTypes.cpp`
- `Source/FlightProject/Private/Tests/FlightVexBoundaryIrTests.cpp`

Optional phase-two additions:

- `Source/FlightProject/Public/Vex/FlightVexBoundaryLowering.h`
- `Source/FlightProject/Private/Vex/FlightVexBoundaryLowering.cpp`
- `Source/FlightProject/Public/Verse/FlightGpuScriptBridge.h`

### Why Add `FlightVexBoundaryTypes.*`

The current `FlightVexSchemaIr.h` already carries schema binding results.
That is the right layer.

But boundary semantics will likely add:

- enums
- metadata structs
- binding records
- realization diagnostics

Keeping those types in a dedicated file prevents `FlightVexSchemaIr.h` from turning into an unstructured catch-all header.

## 5. Phase 1: Preserve Boundary Intent In The Frontend

### 5.1 `Public/Vex/Frontend/VexAst.h`

Current state:

- already distinguishes `Pipe`, `PipeIn`, and `PipeOut`

Changes:

- keep the existing expression kinds
- add optional lightweight metadata fields only if needed for downstream diagnostics
- do not overfit AST nodes to backend/runtime details

Design rule:

- AST should capture syntax and immediate expression identity
- AST should not become the final source of boundary legality

### 5.2 `Public/Vex/Frontend/VexParser.h`

Changes:

- keep `FVexParseResult` stable
- optionally expose boundary-summary metadata if useful for diagnostics and tests

Good additions:

- boundary operator count
- whether the program contains any explicit boundary operators
- whether the program contains mixed pipe/boundary composition

Avoid:

- embedding resolved latency or runtime owner information in parser results

### 5.3 `Private/Vex/Frontend/VexParser.cpp`

Changes:

- preserve current `Pipe`, `PipeIn`, and `PipeOut` parsing behavior
- add explicit tests and diagnostics around malformed boundary usage where needed
- keep semantic rules minimal at the parser layer

Parser responsibilities should remain:

- syntax shape
- gross contextual restrictions
- immediate structural correctness

The parser should not decide:

- host-to-GPU legality
- GPU-to-host legality
- same-frame vs next-frame realization

## 6. Phase 2: Add Boundary Types To SchemaIR

### 6.1 `Public/Vex/FlightVexBoundaryTypes.h`

Add small shared boundary types such as:

- `EVexBoundaryKind`
- `EVexBoundaryPayloadKind`
- `EVexBoundaryRealization`
- `EVexBoundaryCompletionSemantics`
- `FVexBoundaryMetadata`
- `FVexBoundaryBinding`
- `FVexBoundaryDiagnostic`

Design rule:

- this file should contain plain-data semantic types only
- no compiler orchestration logic here

### 6.2 `Public/Vex/FlightVexSchemaIr.h`

Extend SchemaIR/binding types to carry boundary meaning.

Recommended additions:

- `EVexSchemaExpressionKind` if a schema-layer expression classifier becomes useful
- `FVexSchemaBoundaryUse`
- optional boundary arrays on `FVexSchemaBindingResult`
- boundary-related summary sets such as:
  - imported contracts
  - exported contracts
  - mirror requests
  - awaitable boundaries

Suggested `FVexSchemaBoundaryUse` fields:

- `StatementIndex`
- `ExpressionNodeIndex`
- `OperatorKind`
- `BoundaryMetadata`
- `BoundSourceSymbolIndex` or contract reference
- `BoundDestinationSymbolIndex` or contract reference

Design rule:

- schema binding result should become the first authoritative semantic carrier for boundary intent
- do not lower boundary meaning away at this stage

### 6.3 `Private/Vex/FlightVexSchemaBinder.cpp`

This is the main phase-one implementation file for boundary-aware lowering.

Changes:

- walk expression trees and detect `PipeIn` / `PipeOut` explicitly
- bind those nodes to boundary metadata rather than treating them as ordinary function composition
- populate `FVexSchemaBindingResult` with boundary uses
- classify unresolved boundary intent as explicit diagnostics rather than silent fallback

Recommended behavior:

- plain `|` remains ordinary composition
- `<|` produces `ImportBoundary`-style semantic binding
- `|>` produces `ExportBoundary`-style semantic binding

The binder should answer:

- which symbols/contracts/resources are involved?
- is the operator describing import or export?
- does the bound symbol set imply host/GPU or GPU/GPU crossing?
- is the result awaitable or mirror-oriented?

The binder should not yet answer:

- exact RDG realization
- exact Vulkan/RHI primitive
- final pass schedule

## 7. Phase 3: Add Boundary Reporting

### 7.1 `Public/Vex/FlightCompileArtifacts.h`

Extend compile artifact reporting with explicit boundary information.

Recommended additions:

- `BoundaryOperatorCount`
- `ImportedContractKeys`
- `ExportedContractKeys`
- `RequestedLatencyClass`
- `ResolvedLatencyClass`
- `bHasAwaitableBoundary`
- `bHasMirrorRequest`

If needed, add a dedicated summary struct such as:

- `FFlightBoundaryCompileSummary`

### 7.2 `Private/Vex/FlightCompileArtifacts.cpp`

Serialize the new boundary information into compile artifact JSON.

This is important because boundary-aware lowering will otherwise become invisible and hard to debug.

Reports should explain:

- whether boundary operators were present
- how they were semantically classified
- whether they remained abstract or were fully realizable in the current build/runtime path

## 8. Phase 4: Runtime Commit Handshake

### 8.1 `Public/Verse/UFlightVerseSubsystem.h`

Add boundary-aware compile/runtime fields without forcing immediate execution support for every case.

Recommended additions:

- optional boundary summary on `FVerseBehavior`
- explicit record of whether the behavior contains boundary-aware semantics
- explicit record of whether those semantics are currently executable

Avoid:

- direct exposure of backend objects to script-facing or behavior-facing structures

### 8.2 `Private/Verse/UFlightVerseSubsystem.cpp`

Changes:

- consume boundary-aware schema binding results
- include boundary-aware information in compile diagnostics and reports
- reject or downgrade behaviors whose boundary semantics are not executable in the selected runtime path
- keep actual GPU execution ownership outside the Verse subsystem where appropriate

Short-term rule:

- boundary-aware compilation may be legal before every boundary-aware runtime path is executable
- when that happens, reports must state this explicitly

This is similar to the current reporting-first backend selection path and should evolve toward runtime commit rather than jumping directly to implicit execution.

## 9. Phase 5: Low-Level IR Integration

Low-level `FlightVexIr` should only be expanded after the boundary-aware SchemaIR layer is stable enough to prove what needs to survive into backend lowering.

### 9.1 `Public/Vex/FlightVexIr.h`

Near-term recommendation:

- do not add many boundary ops here immediately
- keep `EVexIrOp` focused on arithmetic, symbol load/store, and control flow

Possible later additions once the plan is validated:

- `ImportBoundary`
- `ExportBoundary`
- `AwaitBoundary`
- `SampleResource`

These should only be added if:

- multiple backends genuinely need a shared low-level representation
- the boundary-aware SchemaIR layer has already stabilized the semantics

### 9.2 `Private/Vex/FlightVexIr.cpp`

Near-term recommendation:

- keep current IR compile path stable
- reject unsupported boundary-aware programs from low-level IR compilation where necessary
- or compile only the purely local subexpressions while leaving boundary planning above this layer

Do not:

- wedge half-defined host/device copy semantics into the current IR compile path just because the syntax exists

## 10. Phase 6: Backend And Mega-Kernel Consumption

### 10.1 `Private/Vex/FlightVexLowering.cpp`

Changes:

- eventually consume boundary-aware binding information when lowering HLSL or mega-kernel code
- distinguish:
  - plain local composition
  - resource sampling
  - context read/write
  - store-back or export intent

Near-term rule:

- lower only those boundary semantics that have a clear RDG-native realization
- keep mirror/readback semantics out of generic expression lowering until the native script bridge/runtime owner exists

### 10.2 `Private/Orchestration/FlightMegaKernelSubsystem.cpp`

Changes:

- consume boundary-aware binding metadata rather than only plain symbol maps
- report when a GPU behavior references import/export/context operators
- reject inclusion in the mega-kernel when required runtime resource contracts are missing

The mega-kernel should not guess what `<|` and `|>` mean.
It should consume already-resolved boundary semantics from schema binding and orchestration/runtime commit.

### 10.3 `Public/Vex/FlightVexBackendCapabilities.h`
### 10.4 `Private/Vex/FlightVexBackendCapabilities.cpp`

Changes:

- extend backend compatibility evaluation to account for boundary-aware features
- report when a backend supports:
  - local compute only
  - resource sampling
  - export to mirror
  - awaitable completion

This does not require full implementation of those paths immediately.
It does require explicit reporting and legality surfaces.

## 11. Phase 7: Native GPU Script Bridge Integration

This phase depends on the broader scriptable GPU boundary work described in `GpuComputeFrameworkRefinement.md`.

### 11.1 New File: `Public/Verse/FlightGpuScriptBridge.h`

Recommended contents:

- typed submission request structs
- submission handle type
- status enums
- bridge interface

### 11.2 `Private/Verse/UFlightVerseSubsystem.cpp`

Changes:

- route awaitable or mirrored boundary semantics through the native GPU script bridge
- keep Verse as control plane only

This is the stage where boundary-aware compile semantics become truly meaningful for scriptable GPU work.

## 12. Tests

### 12.1 `Private/Tests/FlightVexParserTests.cpp`

Add or expand tests for:

- correct preservation of `Pipe`, `PipeIn`, and `PipeOut`
- malformed boundary syntax
- mixed pipe/boundary expression shape

### 12.2 New File: `Private/Tests/FlightVexBoundaryIrTests.cpp`

Add focused tests for:

- binder classification of boundary imports and exports
- binding of named symbols/contracts across boundary ops
- boundary diagnostics when no legal contract/resource exists
- separation between plain `|` and boundary operators

### 12.3 `Private/Tests/FlightVexSchemaIrTests.cpp`

Extend SchemaIR tests to verify:

- boundary metadata survives schema binding
- bound symbols/contracts are attached correctly
- imported/exported contract summaries are correct

### 12.4 `Private/Tests/FlightVexVerseTests.cpp`

Extend compile artifact tests to verify:

- boundary-related reporting fields exist
- non-executable boundary-aware behaviors report truthful reasons

### 12.5 `Private/Tests/FlightMegaKernelTests.cpp`

Add tests later for:

- GPU-eligible boundary-aware behavior inclusion
- rejection when required resource contracts are missing

## 13. First Concrete File Mapping

If work starts immediately, the first implementation slice should touch these files:

### Must Touch First

- `Source/FlightProject/Public/Vex/FlightVexBoundaryTypes.h`
- `Source/FlightProject/Public/Vex/FlightVexSchemaIr.h`
- `Source/FlightProject/Private/Vex/FlightVexSchemaBinder.cpp`
- `Source/FlightProject/Private/Tests/FlightVexBoundaryIrTests.cpp`
- `Source/FlightProject/Private/Tests/FlightVexSchemaIrTests.cpp`

### Likely Touch In The Same Slice

- `Source/FlightProject/Public/Vex/Frontend/VexParser.h`
- `Source/FlightProject/Private/Vex/Frontend/VexParser.cpp`
- `Source/FlightProject/Public/Vex/FlightCompileArtifacts.h`
- `Source/FlightProject/Private/Vex/FlightCompileArtifacts.cpp`

### Do Not Touch In The First Slice Unless Necessary

- `Source/FlightProject/Public/Vex/FlightVexIr.h`
- `Source/FlightProject/Private/Vex/FlightVexIr.cpp`
- `Source/FlightProject/Private/Vex/FlightVexLowering.cpp`
- `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`
- `Source/FlightProject/Private/Orchestration/FlightMegaKernelSubsystem.cpp`

The point of the first slice is semantic preservation and reporting, not full runtime realization.

## 14. Success Criteria

Phase-one success looks like:

- boundary operators are preserved as semantic facts through schema binding
- compile artifacts can report those facts
- tests prove the compiler distinguishes plain pipe composition from explicit boundary intent
- unsupported boundary-aware programs fail or downgrade truthfully rather than silently erasing meaning

Phase-two success looks like:

- runtime/backend selection can reason about boundary-aware semantics
- the mega-kernel and future GPU paths consume resolved boundary metadata rather than guessing from syntax
- Verse/runtime integration can await or mirror boundary results through a typed bridge rather than ad hoc thunks

## 15. Working Recommendation

The recommended first implementation slice is:

1. add dedicated boundary types
2. extend `FVexSchemaBindingResult` with boundary uses and summaries
3. teach `FlightVexSchemaBinder.cpp` to bind `PipeIn` and `PipeOut` semantically
4. add focused tests
5. expose the result in compile artifacts

Only after that should the project decide which parts must descend into low-level IR, runtime commit, mega-kernel lowering, or Verse async integration.
