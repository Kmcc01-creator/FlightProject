# VEX State Mutation Schema Frame

This document establishes a reference frame for thinking about state mutation in the FlightProject VEX compiler.

The goal is not to argue that VEX should become "just a mutation language."
The goal is to state clearly that state mutation is one of the project’s primary semantic concerns, and that the compiler, reflection system, schema surfaces, and orchestration layer should all model it explicitly.

For project organization and domain ownership, see `Docs/Architecture/ProjectOrganization.md`.
For the world-level coordination surface, see `Docs/Architecture/OrchestrationSubsystem.md`.
For target capability and lowering policy normalization, see `Docs/Architecture/TargetCapabilitySchema.md`.
For the current SchemaIR direction, see `Docs/Workflow/SchemaIrImplementationExploration.md`.

## 1. Why This Frame Matters

FlightProject already treats VEX as more than a text-to-text transpiler.

VEX scripts currently participate in:

- direct mutation of reflected host state
- backend-specific lowering to HLSL and Verse
- SIMD and native execution paths
- orchestration-time behavior binding
- schema-driven validation of symbol legality

That means the real question is not:

- "how do we emit HLSL or Verse from VEX?"

The deeper question is:

- "what state is a VEX program allowed to observe and mutate, under what contract, through what storage model, and with what guarantees?"

That is the right center of gravity for the compiler.

## 2. Core Thesis

Treat mutation as a first-class semantic contract, not as an incidental side effect of symbol access.

In practice, that means:

- reads and writes are part of the type/schema model
- mutation legality is part of compilation, not only runtime behavior
- storage shape is part of lowering policy, not hidden implementation detail
- backends should consume an explicit mutation model rather than rediscovering it from strings or hardcoded assumptions

## 3. What "Mutation" Means In FlightProject

Mutation in FlightProject is not one thing.
It appears at several levels.

### 3.1 Logical Mutation

This is what the script means:

- assign to `@shield`
- integrate `@position`
- update a temporary local
- branch based on a computed state value

This level should be backend-neutral.

### 3.2 Schema Mutation

This is what the type contract allows:

- field is writable or read-only
- mutation is allowed only in `@cpu` or `@gpu`
- mutation is legal only on certain threads or tiers
- mutation may require specific alignment or determinism policy

This level belongs in the schema layer.

### 3.3 Storage Mutation

This is how the write is physically realized:

- byte offset into an AoS struct
- accessor invocation
- Mass fragment write
- SoA column update
- GPU buffer element store
- staged writeback through an external provider

This level belongs in storage binding, not in logical symbol identity.

### 3.4 Transactional Mutation

This is how mutation participates in execution semantics:

- speculative mutation
- rollback-safe mutation
- deferred writeback
- sparse dirty-range propagation
- frame-bound or cohort-bound commits

This level belongs in backend/runtime policy and orchestration.

## 4. Recommended Compiler View

The compiler should think about mutation through four layers:

```text
Source Mutation
    -> author intent in AST

Semantic Mutation
    -> resolved through SchemaIR

Storage Mutation
    -> lowered through storage binding policy

Execution Mutation
    -> committed through runtime/backend semantics
```

This gives a cleaner split:

- AST answers what the author wrote
- SchemaIR answers what state operation that source means
- low-level IR answers how to execute it
- runtime/orchestration answers when and where the mutation becomes authoritative

## 5. Reflection As Mutation Metadata

The reflection system should not only describe fields.
It should describe mutation affordances.

The current trait-based reflection surface already gives the project the right entry point:

- field type
- field identity
- field attributes
- compile-time iteration

The next step is to treat those attributes as mutation metadata rather than only editor/schema decoration.

Examples of mutation-relevant facts reflection can express:

- symbol name
- logical value type
- writable vs read-only
- residency
- thread affinity
- backend naming
- alignment requirements
- determinism expectations
- offset eligibility

This is how the compiler escapes hardcoded field tables.

## 6. Schema As Mutation Contract

`FVexTypeSchema` should become the compiler-facing contract for mutation.

That contract should answer:

- what logical symbols exist?
- what type of value does each symbol carry?
- can it be read?
- can it be written?
- in which execution domains is it legal?
- what storage strategy backs it?
- what backend bindings exist?

This is more useful than a schema that only exposes:

- `Getter`
- `Setter`
- `MemberOffset`

Those are still useful, but they are implementation details of mutation, not the meaning of mutation.

## 7. Registry Generation vs Hardcoding

One of the core benefits of this frame is that it replaces backend-specific hardcoding with registry-issued facts.

### Hardcoded Pattern

A hardcoded mutation path usually looks like:

- known field names
- known struct type
- known offset assumptions
- known backend aliasing
- known runtime ownership path

That is fast to prototype and expensive to generalize.

### Registry-Generated Pattern

A schema/registry-generated mutation path looks like:

- reflection yields field descriptors
- schema orchestrator builds logical symbol records
- manifest rows merge policy and capability metadata
- compiler binds source mutation against the resulting schema
- backends consume the resolved mutation contract

This is slower to design and much safer to scale.

## 8. Why SchemaIR Is The Right Next Step

SchemaIR is the layer where mutation should stop being stringly and start being semantic.

SchemaIR should resolve:

- symbol read vs write intent
- target logical symbol id
- value type
- mutability legality
- residency legality
- storage eligibility
- backend binding names

Without that layer, too much mutation meaning stays smeared across:

- parser validation
- symbol-definition rows
- backend lowering maps
- runtime schema lookup

SchemaIR is the cleanest place to unify those concerns.

## 9. Mutation And Storage Shape

Storage shape should be explicit because mutation legality depends on it.

### 9.1 AoS

For plain reflected host structs:

- byte offsets are acceptable
- direct access can be zero-cost
- mutation can often happen immediately

### 9.2 SoA And Mass

For fragment-oriented or columnar storage:

- a single byte offset is not the right abstraction
- mutation may need row/column addressing
- writeback may be chunked, sparse, or staged

### 9.3 GPU

For GPU-backed state:

- mutation is not just "set field"
- writes may imply barriers, staging, or deferred visibility
- backend legality depends on residency and target policy

### 9.4 External Providers

For indirect or service-backed mutation:

- mutation may be transformed into commands, events, or provider writes
- the schema still owns legality and logical identity
- the runtime owns the actual commit path

## 10. Member Pointers, Offsets, and Ergonomics

This area matters because it sits at the boundary between reflection ergonomics and mutation implementation.

### 10.1 Member Pointers

Pointer-to-member syntax is best treated as the semantic access primitive.

It gives:

- typed access
- compile-time identity of the member
- safe `Get` / `Set` behavior

### 10.2 Offsets

Offsets are useful when the storage shape is truly contiguous AoS host memory.

They give:

- zero-cost access
- backend-friendly direct addressing
- easier bulk mutation paths

But offsets should be treated as an optimization fact, not a universal schema truth.

### 10.3 Practical Rule

Use this rule:

- member pointer defines semantic access
- offset is optional storage metadata

This avoids overfitting the compiler to one storage model.

### 10.4 STRUCT_OFFSET

`STRUCT_OFFSET` is still useful when:

- the field token is available directly
- the owner type is known
- the storage really is standard AoS

That makes it a valid fast-path criterion, not a universal requirement.

### 10.5 Developer Error Surface

The main ergonomics risk is allowing generic offset assumptions to leak into types where they are not semantically valid.

The safest posture is:

- explicit offset eligibility
- fallback accessor/storage bindings otherwise
- no assumption that every reflected field can or should lower to a direct byte offset

## 11. Mutation Questions The Compiler Should Answer

A useful vertical slice is one where the compiler can answer these questions explicitly:

1. What state does this script read?
2. What state does this script mutate?
3. Are those mutations legal for the target schema?
4. Which mutations are immediate vs deferred?
5. Which mutations are CPU-only, GPU-only, or shared?
6. Which mutations require direct storage access vs provider-based commit?
7. Which mutations can be represented in low-level IR without losing semantic meaning?
8. Which mutations require orchestration-visible bindings or reports?

If the compiler cannot answer those questions, it is probably still too source-shaped and not yet schema-shaped enough.

## 12. Recommended Vertical Slice

The clean next vertical slice is:

- generalized reflected host struct
- explicit type key
- schema-issued logical symbol records
- clear read/write legality
- clear storage kind
- schema-aware native execution
- orchestration-visible behavior/schema binding later

That slice is valuable because it proves:

- mutation can be generalized beyond `FDroidState`
- reflection can provide mutation meaning, not just field lists
- schema can replace hardcoded symbol handling
- storage policy can remain explicit without forcing a full backend rewrite

## 13. Recommended Working Frame

Use this short working frame for VEX compiler design:

> VEX is a schema-bound state transformation language.
> The compiler’s job is to resolve legal mutations over typed state contracts and lower them into target-specific execution paths without losing mutation meaning.

That framing keeps the project centered on:

- typed state
- mutation legality
- storage shape
- backend policy
- orchestration visibility

instead of centering only on source parsing or backend code generation.

## 14. Practical Implication For Upcoming Work

Near-term compiler work should prefer changes that make mutation more explicit:

- schema-issued logical symbol records
- explicit read/write binding in SchemaIR
- storage binding kinds
- compile-time legality checks for mutation
- runtime/orchestration reporting of active state contracts

The project should avoid expanding:

- hardcoded symbol tables
- backend-local mutation rules
- special-case struct logic
- hidden storage assumptions inside generic compiler paths

That is the most direct way to turn the current generalized pilot into a durable compiler architecture.
