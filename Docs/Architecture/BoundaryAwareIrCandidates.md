# Boundary-Aware IR Candidates

This document proposes candidate IR and SchemaIR node/op names for boundary-aware lowering in FlightProject.

It is not a final design.
It is a naming and decomposition aid for the next compiler/runtime refinement steps around:

- VEX pipe and boundary operators
- host/GPU and GPU/GPU crossings
- resource-bound context flow
- scriptable submission and await semantics

For the broader GPU execution model, see `Docs/Architecture/GpuComputeFrameworkRefinement.md`.
For the current mutation-centric schema frame, see `Docs/Architecture/VexStateMutationSchemaFrame.md`.
For the current mega-kernel direction, see `Docs/Architecture/MegaKernelOrchestration.md`.

## 1. Why Add A Boundary-Aware Layer

Current low-level VexIR is good at:

- arithmetic
- symbol load/store
- control flow

It is not yet shaped to preserve boundary meaning such as:

- import from an upstream execution context
- export to a downstream execution context
- resource visibility handoff
- deferred host mirror or awaitable completion

If boundary operators such as `<|` and `|>` are lowered too early into generic value flow, the compiler loses the information needed to:

- validate crossing legality
- choose same-frame vs next-frame realization
- distinguish GPU-to-GPU from GPU-to-host
- report why a given boundary realization was selected

## 2. Layering Recommendation

The simplest useful split is:

- VEX AST
  syntax and immediate expression structure
- boundary-aware IR
  explicit boundary intent preserved before final runtime realization
- SchemaIR / binding layer
  contract resolution, legality, resource mapping, and execution-domain selection
- backend/runtime lowering
  concrete RDG/resource/pass realization

This does not require a fully separate compiler for each stage.
It only requires that the compiler preserve boundary semantics long enough to make correct planning decisions.

## 3. Candidate VexIR Operations

These candidate ops are intended to express boundary semantics before final runtime lowering.

### 3.1 Core Boundary Ops

- `ImportBoundary`
  import data or context from an upstream boundary into the current execution context
- `ExportBoundary`
  export data or context from the current execution context to a downstream boundary
- `AwaitBoundary`
  explicit wait/resume point tied to a boundary completion handle
- `CommitBoundary`
  marks a boundary output as committed and legally consumable

### 3.2 Resource-Oriented Ops

- `SampleResource`
  sample a named resource under a resolved contract
- `ReadContext`
  read a named context product that may not be a plain symbol load
- `WriteContext`
  write a named context product for later consumers
- `BindResourceView`
  bind an abstract logical resource to a local execution scope

### 3.3 Scheduling-Oriented Ops

- `BeginSubmission`
  begin a runtime submission scope
- `EndSubmission`
  end a runtime submission scope
- `SignalCompletion`
  emit completion intent for a submission or context product
- `PollCompletion`
  query completion state without forcing suspension

### 3.4 Data Movement Ops

- `MaterializeMirror`
  request or realize a CPU-visible mirror of a boundary output
- `ImportMirror`
  consume a previously mirrored CPU-visible result
- `PromoteResidency`
  move from a less GPU-local to a more GPU-local residency class conceptually
- `DemoteResidency`
  move toward host-visible or mirrored residency conceptually

These names should be read as semantic operations, not as promises of a literal memcpy or backend transition.

## 4. Candidate SchemaIR Nodes

SchemaIR should carry the legality and contract meaning of a boundary crossing.

### 4.1 Boundary Nodes

- `FSchemaBoundaryImportNode`
- `FSchemaBoundaryExportNode`
- `FSchemaBoundaryAwaitNode`
- `FSchemaBoundaryCommitNode`

Suggested fields:

- source contract key
- destination contract key
- source resource id
- destination resource id
- requested latency class
- CPU mirror policy
- awaitability
- diagnostics or downgrade notes

### 4.2 Resource Nodes

- `FSchemaResourceReadNode`
- `FSchemaResourceWriteNode`
- `FSchemaResourceSampleNode`
- `FSchemaResourceBindNode`

Suggested fields:

- logical resource id
- resource kind
- storage/view mode
- access class
- producer identity
- consumer identity

### 4.3 Context Nodes

- `FSchemaContextReadNode`
- `FSchemaContextWriteNode`
- `FSchemaContextMergeNode`
- `FSchemaContextResolveNode`

These are useful when the thing being transferred is not a raw value or buffer, but a derived context product such as:

- navigation context
- force blackboard contribution
- perception aggregate
- planner-visible report surface

## 5. Candidate Enumerations

These enums are intentionally narrow and descriptive.

### 5.1 Boundary Kind

```text
None
HostToGpu
GpuToHost
GpuToGpu
CpuToCpu
CrossSubsystem
```

### 5.2 Boundary Payload Kind

```text
Value
Symbol
Context
ResourceView
Mirror
SubmissionHandle
```

### 5.3 Boundary Realization

```text
Unknown
SameFrameDependency
NextFrameDependency
Submission
MirrorReadback
AliasReuse
ExtractAndReregister
```

### 5.4 Completion Semantics

```text
NotCompletable
FireAndForget
Pollable
Awaitable
Committed
```

## 6. Candidate Metadata Fields

Whether these live directly on IR ops, SchemaIR nodes, or side tables, the compiler/runtime likely needs metadata such as:

- `SourceContractKey`
- `DestinationContractKey`
- `LogicalResourceId`
- `BoundaryKind`
- `PayloadKind`
- `RequestedLatencyClass`
- `ResolvedLatencyClass`
- `CpuMirrorPolicy`
- `ExecutionDomainPreference`
- `ResolvedExecutionDomain`
- `bAwaitable`
- `bSameFramePreferred`
- `bSameFrameRequired`
- `bMaterializeMirror`
- `SubmissionHandleKind`

## 7. Suggested Mapping From Syntax

These mappings are illustrative rather than final.

### 7.1 Plain Pipe

Syntax:

```text
A | B
```

Suggested interpretation:

- normal composition in the current execution context
- no boundary node required unless `B` itself implies one

### 7.2 Pipe-In

Syntax:

```text
X <| Y
```

Suggested interpretation:

- `ImportBoundary`
- then normal local value or context flow

Useful when:

- importing an upstream context
- binding a named resource/product into the current scope
- expressing a host/GPU or GPU/GPU handoff intent

### 7.3 Pipe-Out

Syntax:

```text
X |> Y
```

Suggested interpretation:

- local computation followed by `ExportBoundary`

Useful when:

- exporting a context product
- requesting a mirror
- signaling a downstream consumer surface

## 8. Lowering Heuristics

Boundary-aware ops should lower differently depending on what SchemaIR and orchestration resolve.

Examples:

- `ImportBoundary` + same-frame GPU legality
  lower to resource binding or pass dependency
- `ImportBoundary` + next-frame legality
  lower to extracted-resource read or last-committed view
- `ExportBoundary` + CPU mirror policy
  lower to mirror/readback request rather than inline value return
- `AwaitBoundary` + awaitable submission
  lower to native submission-handle suspension/resume integration

This is the main reason to keep boundary ops explicit through planning.

## 9. Suggested Naming Principles

Preferred naming style:

- describe semantic role, not backend primitive
- use `Boundary`, `Context`, `Resource`, `Submission`, and `Mirror` as the main vocabulary
- avoid names that imply Vulkan-only or RDG-only behavior

Prefer:

- `ImportBoundary`
- `MaterializeMirror`
- `AwaitBoundary`

Avoid:

- `CopyToDeviceNow`
- `ReadbackValueImmediate`
- `VkSemaphoreWaitNode`

The IR should remain portable across different concrete realizations.

## 10. Minimal First Slice

A practical first slice does not need every candidate op in this document.

The smallest useful set is likely:

- `ImportBoundary`
- `ExportBoundary`
- `SampleResource`
- `WriteContext`
- `AwaitBoundary`

That is enough to support experimentation around:

- GPU context building
- scriptable submission/wait
- resource-bound context flow
- future `|`, `<|`, and `|>` lowering

## 11. Working Recommendation

The current recommendation is:

- keep arithmetic/control-flow VexIR small
- add a narrow boundary-aware vocabulary rather than many ad hoc builtins
- let SchemaIR carry legality, resource identity, and latency semantics
- let final lowering choose RDG/resource/pass realization only after planning and validation

This keeps the language expressive without forcing the compiler to commit too early to the wrong runtime model.
