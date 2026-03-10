# SchemaIR Implementation Exploration

This document explores a concrete integration path for introducing a schema-bound semantic layer into the FlightProject VEX compiler.

The goal is not to replace the existing AST and low-level IR.
The goal is to insert a stable semantic boundary between them:

- AST remains source-oriented
- SchemaIR becomes type-bound semantic meaning
- low-level IR remains execution-oriented

For the current project organization and domain ownership basis, see `Docs/Architecture/ProjectOrganization.md`.
For target capability and policy normalization, see `Docs/Architecture/TargetCapabilitySchema.md`.
For the current orchestration direction, see `Docs/Architecture/OrchestrationSubsystem.md`.

## 1. Why Add SchemaIR

The current codebase already has several strong pieces:

- a real VEX frontend AST
- AST-level rewrites and tiering
- a shared low-level IR used by HLSL, Verse, VVM assembly, and SIMD
- a reflection-driven type schema surface

The missing layer is a stable semantic contract between:

- source syntax and directives
- reflected type/schema data
- storage strategy
- backend naming
- execution-domain constraints

Today those concerns are spread across:

- parser semantic validation
- manifest-derived `FVexSymbolDefinition` rows
- `FVexTypeSchema` symbol accessors
- backend-specific symbol maps
- runtime fallback execution

That works for the current generalized pilot, but it is too diffuse for the next step.

## 2. Current Shape

The project currently looks like this:

```text
VEX Source
    -> AST parse and semantic validation
    -> AST optimization / tiering
    -> low-level IR compile
    -> backend lowering or execution

Reflection / Manifest / Runtime Schema
    -> symbol definitions
    -> runtime accessors
    -> backend identifier maps
```

The important observation is:

- `FVexIrProgram` is already a useful shared execution IR
- `FVexTypeSchema` is already a useful runtime type contract
- but schema data is not yet the canonical semantic owner of compilation

## 3. Target Shape

Introduce this layered compiler model:

```text
VEX Source
    -> AST
    -> Schema Binding
    -> SchemaIR
    -> low-level IR
    -> HLSL / Verse / VVM / SIMD / native runtime paths
```

Recommended ownership:

- AST
  owned by `Vex/Frontend/`
- schema construction and binding
  owned by `Vex/`
- low-level IR and backend lowerings
  owned by `Vex/`
- runtime execution hosts
  owned by `Verse/`, `Mass/`, `Swarm/`, and later `Orchestration/`

## 4. Core Principle

Promote `FVexTypeSchema` from a runtime accessor bundle to a compiler contract.

That means `FVexTypeSchema` should answer:

- what logical symbols exist for this type?
- what are their value types?
- what execution-domain constraints apply?
- what backend bindings exist?
- how is each symbol physically stored?
- what stable type identity and layout contract apply?

It should not be limited to:

- `Getter`
- `Setter`
- `MemberOffset`

Those remain useful implementation details, but they should not define the semantic layer.

## 5. Proposed Schema Model

### 5.1 Stable Type Identity

Introduce an explicit type identity surface.

Recommended shape:

```text
FVexTypeId
    RuntimeKey
    StableName
    OptionalNativeStructPath
    LayoutHash
```

Design rule:

- `const void*` is acceptable as an in-process runtime key
- it is not sufficient as the full semantic identity of a type

The schema should carry both:

- runtime identity for lookup speed
- stable identity for reports, hashes, and versioning

### 5.2 Logical Symbol Schema

Split each symbol into a logical record and a storage record.

Recommended logical symbol fields:

```text
SymbolId
SymbolName
ValueType
Mutability
Residency
Affinity
RequiredContracts
HlslIdentifier
VerseIdentifier
CapabilityFlags
```

This is the information the compiler should bind against.

### 5.3 Storage Binding

Physical storage must be modeled separately.

Recommended storage kinds:

```text
AosOffset
Accessor
MassFragmentField
SoaColumn
GpuBufferElement
ExternalProvider
```

Recommended storage record shape:

```text
StorageKind
OptionalOffset
OptionalStride
OptionalFragmentType
OptionalBufferBinding
OptionalAccessorFns
```

This matters because:

- AoS host execution can use offsets
- SoA and Mass execution cannot be described by a single byte offset
- GPU or external providers may be indirect or staged

### 5.4 Backend Binding

Backend bindings should remain explicit.

Recommended rule:

- logical symbol identity is backend-neutral
- HLSL and Verse names are bindings, not the identity itself

That keeps `@shield` as the semantic symbol while allowing:

- HLSL local aliasing
- Verse runtime naming
- future backend-specific conventions

## 6. Proposed SchemaIR Model

SchemaIR should be the result of binding AST against a schema.

Recommended program-level contents:

```text
BoundType
ResolvedSymbols
ResolvedLocals
ExecutionMetadata
ResolvedDirectives
SemanticIssues
BoundExpressions
BoundStatements
```

Recommended node-level changes compared to AST:

- symbol refs resolve to stable schema symbol ids
- field accesses resolve to logical field records
- inferred types become concrete semantic types
- backend and residency constraints become attached facts, not re-derived string lookups

SchemaIR is still not backend code.
It is semantic meaning plus resolved type/storage policy.

## 7. Role Of FVexSchemaOrchestrator

`FVexSchemaOrchestrator` should own schema construction and merging.

Recommended responsibilities:

- resolve runtime type identity
- build schema from reflection traits
- merge manifest requirements
- ingest optional `UScriptStruct` metadata
- compute layout/version hash
- surface storage and backend bindings
- issue the canonical `FVexTypeSchema`

Recommended non-responsibilities:

- parsing VEX source
- low-level IR code generation
- backend emission
- world orchestration ownership

That keeps the schema orchestrator focused on type contracts rather than becoming a new compiler god-object.

## 8. Offset And Member-Pointer Policy

This area needs an explicit rule because it affects both correctness and ergonomics.

### 8.1 Recommended Rule

Use member pointers for semantic access.
Use offsets only as an optional fast path.

That means:

- `&Owner::Field` is the source of type-safe access meaning
- `STRUCT_OFFSET(Owner, Field)` is a storage optimization fact when the field token is available
- derived offsets from generic member-pointer tricks should not be the canonical truth of the schema

### 8.2 Why This Matters

`TAttributedFieldDescriptor::GetOffset()` currently computes offset from a member pointer via pointer arithmetic on a null owner.

That can work for simple standard-layout AoS structs, but it should not be treated as universally valid for:

- non-standard-layout types
- inheritance-heavy layouts
- future SoA storage
- Mass fragment composites
- indirect or staged GPU data

### 8.3 Practical Criterion

Allow `AosOffset` only when all of these are true:

- the field is a real non-static data member
- the owner type is standard-layout
- the storage is contiguous AoS host memory
- the field token is available at macro expansion or registration time

Otherwise:

- do not expose an offset
- use an accessor or another storage binding kind

### 8.4 STRUCT_OFFSET Guidance

`STRUCT_OFFSET` is still useful, but only in the narrow case where the member token is available directly.

Recommended use:

- macro-authored reflected fields can supply an offset fast path directly

Avoid relying on:

- reconstructing a universal byte offset from an arbitrary member-pointer value

The macro layer already knows the member token.
That is the safest place to capture an offset when offset semantics are actually valid.

## 9. Integration Strategy

### Phase 1

Stabilize schema identity and storage modeling without changing compile behavior.

Deliverables:

- explicit type identity
- explicit storage binding kinds
- populated layout hash and native struct bridge
- preserved current symbol registry behavior

### Phase 2

Introduce schema-bound compile context.

Deliverables:

- compile API that accepts a real schema/type handle
- schema merge path no longer disguised as `UScriptStruct*`
- manifest and reflection data normalized through the schema orchestrator

### Phase 3

Add SchemaIR binder.

Deliverables:

- AST-to-SchemaIR binding pass
- resolved symbol ids and types
- semantic diagnostics attached to bound nodes

### Phase 4

Retarget low-level IR compilation to SchemaIR.

Deliverables:

- SchemaIR-to-IR lowering
- low-level IR no longer re-derives schema facts from string maps

### Phase 5

Move runtime and orchestration consumers onto schema-issued bindings.

Deliverables:

- runtime execution paths resolved from schema/orchestration bindings
- processor integration no longer hardcodes `BehaviorID = 1`

## 10. Testing Direction

The minimum safe test surface should include:

- schema construction from reflected custom structs
- layout-hash stability
- backend binding preservation
- offset fast-path eligibility vs fallback-accessor behavior
- AST-to-SchemaIR symbol resolution
- SchemaIR-to-IR parity for current scripts
- generalized execution on non-`FDroidState` types
- explicit negative tests for non-offset-eligible storage shapes

## 11. Near-Term Recommendation

The best first slice is:

1. add explicit schema identity and storage-binding types
2. add `FVexSchemaOrchestrator`
3. change compile APIs to use a real schema/type handle
4. add a small SchemaIR binder for scalar symbol programs
5. keep the current AST and low-level IR intact while integrating the new boundary

That is the lowest-risk path that still establishes the correct architecture.
