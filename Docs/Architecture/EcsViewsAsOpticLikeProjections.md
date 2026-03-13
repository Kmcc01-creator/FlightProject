# ECS Views As Optic-Like Projections

This note records a useful design analogy for FlightProject: ECS view generation and schema-bound symbol projection can be understood as optic-like structure projection.

It is not a claim that Mass ECS should literally implement a Haskell-style optics library.
It is a reference frame for recognizing reusable structural patterns as the project grows its query, schema, and execution-host layers.

For the exploratory optics surface, see [FlightMassOptics.h](../../Source/FlightProject/Public/Core/FlightMassOptics.h).
For the VEX mutation/storage frame, see [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md).
For arbitrary symbol projection and vector/storage contracts, see [VexArbitrarySymbolVectorContract.md](VexArbitrarySymbolVectorContract.md).
For Verse runtime modularization and future host bundles, see [VerseSubsystemModularization.md](VerseSubsystemModularization.md).
For compiled fragment dependency reporting and future query-contract validation, see [CompiledFragmentRequirementReporting.md](CompiledFragmentRequirementReporting.md).

## 1. Why This Analogy Is Useful

FlightProject already has several systems that are not best understood as ad hoc field access:

- Mass query/view construction
- schema-bound symbol resolution
- direct host materialization for Mass and future buffers
- vector/storage legality projection
- reactive or optic-like fragment composition experiments

These systems all share a deeper pattern:

- describe a structural focus
- validate that focus against a contract
- project the focus into an execution surface
- apply reads, writes, or rewrites through that projection

That is close enough to optics thinking that the analogy becomes useful for design.

## 2. What The Analogy Is

The optic-like reading is:

- a symbol projection behaves like a field focus
- a query over many entities behaves like a traversal
- optional fragment presence behaves like a partial focus
- generated execution views behave like lawful projections over storage structure

Recommended shorthand:

```text
schema
    -> declares legal focuses
host bundle
    -> realizes those focuses in concrete storage
execution
    -> reads/writes through those realized focuses
```

That is the main connection point between optics language and ECS runtime design.

## 3. What It Is Not

This is not a claim that ECS views are exactly `Lens`, `Traversal`, `Prism`, or `Plated`.

Important differences:

- ECS storage is heterogeneous and layout-aware
- execution lanes impose legality and performance constraints
- views often project into mutable batch storage, not just pure recursive data
- the system must account for residency, affinity, alignment, and backend legality

So the analogy should guide structure and reuse, not force a literal optics implementation.

## 4. Mapping To Familiar Optic Shapes

Useful correspondences:

- `Lens`
  one symbol or field projected from one host cell
- `Traversal`
  one symbol projected across many entities, or many legal cells in a cohort
- `Prism`
  optional or conditional fragment/tag presence
- `Fold`
  aggregate read-only inspection over archetype or cohort shape
- `Plated`
  recursively or compositionally generated projections over structure descriptions

The first four correspondences are straightforward.
`Plated` is the most interesting one for FlightProject.

## 5. Why It Feels Plated-Adjacent

Classic `Plated` abstractions are about self-similar recursive structure.
FlightProject’s ECS/view problem is not identical, but it shares the same structural intuition:

- the shape of traversal should be described once
- higher-level rewrites should be built from that shape
- local structural rules should compose into larger projections

In FlightProject, those structural rules show up as:

- query patterns over fragment composition
- schema binding over legal symbols
- host-bundle lookup over fragment type or buffer binding
- execution-layer rewrites through the resulting projection

So ECS view generation is best described as `Plated-adjacent`, not literally `Plated`.

## 6. FlightMassOptics As A Signal

[FlightMassOptics.h](../../Source/FlightProject/Public/Core/FlightMassOptics.h) already moves in this direction:

- declarative query patterns
- typed execution views
- archetype pattern matching
- category-aware structure description

The important design lesson is that queries and views are becoming structural descriptions, not just imperative calls to `AddRequirement(...)`.

That makes it reasonable to think of them as an optic-like algebra over ECS shape.

The current compiled fragment requirement report is an early example of that transition:

- schema binding produces structural fragment dependency facts
- reports surface those facts before execution
- the remaining step is to turn them into a validated query contract instead of leaving `AddRequirement(...)` as the only operational truth

## 7. VEX Symbol Resolution As Projection

The same idea appears in VEX:

- a VEX symbol is not only a string
- it is a legal focus described by schema
- that focus projects into storage through `EVexStorageKind`
- further projection into execution lanes happens through vector/storage contracts

That yields this interpretation:

```text
logical symbol
    -> schema focus
schema focus
    -> storage projection
storage projection
    -> executable host focus
```

This is one reason the optics analogy is valuable: it keeps symbol meaning, storage realization, and execution legality distinct while still treating them as one composed projection pipeline.

## 8. Host Bundles As Realized Optics

Future host bundles such as `FMassFragmentHostBundle` or `FGpuBufferHostBundle` can be understood as realized optic environments.

They do not define what a symbol means.
They answer where that symbol lands in concrete storage.

Recommended interpretation:

- schema owns legal focus definition
- host bundle owns focus realization
- runtime value access owns cell read/write semantics
- execution applies behavior through those resolved focuses

That is a strong architectural boundary because it prevents storage hosts from becoming the owners of behavior semantics.

## 9. Reactive Systems And Rewrite Intuition

The reactive/optic experiments in the project also fit this frame.

A reactive system often wants:

- a structural view over current state
- a lawful way to project local updates
- composition rules over multiple focuses

That is effectively a rewrite system over projections.

In that sense, the project’s reactive and ECS directions are not separate.
They are both different faces of “describe legal projections, then compose rewrites over them.”

## 10. Practical Design Rules

1. Keep schema as the owner of legal focuses.
2. Keep host bundles as storage realizers, not semantic owners.
3. Keep runtime value access generic and reusable across host families.
4. Build query/view generation as composable structural projection, not only ad hoc runtime wiring.
5. Use optic language as a design aid, not as an implementation straightjacket.
6. Treat compiled fragment requirement reports as intermediate structural evidence, not as the final query contract.

## 11. What This Suggests For FlightProject

This frame suggests a longer-term architecture like:

```text
archetype / fragment composition
    -> query pattern
    -> typed view generation
    -> schema-bound symbol projection
    -> host-bundle realization
    -> backend legality
    -> execution rewrite
    -> report
```

That is useful because it aligns:

- Mass optics
- VEX schema binding
- host-bundle generalization
- vector/storage legality
- orchestration reporting

under one structural idea instead of five unrelated mechanisms.

## 12. Final Position

ECS view generation in FlightProject is not literally `Plated`.
But it is close enough to optic-like projection and structural rewrite thinking that the analogy is worth keeping around.

Used carefully, it gives the project a better vocabulary for:

- generated views
- lawful fragment and buffer projection
- reusable host-bundle access
- compositional execution over schema-defined structure
