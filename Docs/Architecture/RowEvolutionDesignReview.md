# Design Review: Row-Type Evolution and Systems Integration

**Date**: 2026-03-07  
**Scope**: Recent architecture direction and latest two commits (`f08c39a`, `8f896a1`)

## 1. Executive Summary

The project has a strong foundation for a row-centric architecture:
- Compile-time row composition (`TRow`, `Add`, `Remove`, `Merge`) already exists.
- Trait-based reflection and attributes already provide schema metadata.
- Reactive primitives are integrated across UI, ECS fragments, and swarm runtime systems.

The immediate risk is not conceptual. It is lifecycle correctness in core primitives.  
Addressing those safety gaps first will make the row/reflection/reactive strategy safe to scale into typed VEX compilation and swarm instancing.

## 1.1 Implementation Status Update (2026-03-07)

Status of Phase 0 safety operations from this review:

- **IOP-001** complete: `TReactiveValue` now cleans owned lambda userdata on unsubscribe/all teardown paths.
- **IOP-002** complete: `TEffect` now tracks and unsubscribes dependency callbacks during teardown and disallows unsafe copy/move.
- **IOP-003** complete: async continuation chain now uses shared completion state semantics with late-subscriber delivery.
- **IOP-004** complete: reflection serialization/diff/apply respects `Transient`/`DuplicateTransient`/`SkipSerialization`.

Validation evidence:
- Project automation discovery expanded to **27** tests.
- Added stabilization tests for reactive lifecycle, async chaining, reflection policy gating, and logging filter/stat behavior are passing.

## 2. Top Findings (Baseline from Review)

Note: Critical and high baseline findings below are now addressed by Phase 0 implementation work.  
Medium findings remain active roadmap items.

## Critical

1. `SubscribeLambda` leaked heap-allocated callbacks on unsubscribe paths (**resolved in Phase 0**).  
   Evidence: `FlightReactive.h:85`, `FlightReactive.h:97`, `FlightReactive.h:104`.

2. `TEffect` did not unsubscribe dependency callbacks in destructor, risking use-after-free when dependencies outlive effect instances (**resolved in Phase 0**).  
   Evidence: `FlightReactive.h:191`, `FlightReactive.h:198`, `FlightReactive.h:206`.

## High

3. `TAsyncOp::Then` captured `Next` by value; continuation completion could target the wrong instance, breaking chain semantics (**resolved in Phase 0**).  
   Evidence: `FlightFunctional.h:860`, `FlightFunctional.h:865`, `FlightFunctional.h:871`.

4. Reflection serialization and patch logic did not enforce attribute gates like `Transient` or `SkipSerialization` (**resolved in Phase 0**).  
   Runtime-only fields can be serialized/patched unintentionally.  
   Evidence: `FlightReflection.h:293`, `FlightReflection.h:917`, `FlightReflection.h:1034`.

## Medium

5. Vision drift: `TComputedValue` is planned but only forward-declared; differential docs include `Order::Jerk` but code currently defines up to `Acceleration`.  
   Evidence: `ReactiveSystemPlan.md:7`, `FlightReactive.h:27`, `FunctionalDifferentials.md:30`, `FlightFunctional.h:752`.

6. VEX toolchain currently performs string replacement rather than typed compilation; no semantic guarantees or robust path to `#instance` grammar.  
   Evidence: `CurrentFocus.md:20`, `SwarmInstancing.md:24`, `SwarmOrchestrator.cpp:123`.

## 3. Architectural Position: Why Row Types Should Expand

Row types should become the canonical schema contract for:
- Asset composition (authoring-time and runtime validation).
- ECS composition requirements (query/build contracts).
- Swarm instance configuration (`#instance` blocks lowered into typed row configs).
- Script-language symbol tables (VEX/Verse/HLSL lowering targets).

Current row implementation is already sufficient for schema-level composition.  
For hot loops, row usage should remain contract-level, with low-level contiguous buffers retained for simulation kernels.

## 4. Proposed System Model

## 4.1 Layer A: Row Schema Layer

Define stable schema rows for domain boundaries:
- `FAssetCoreRow`
- `FSimulationParamRow`
- `FSwarmInstanceRow`
- `FVexSymbolRow`

Each row should represent a typed contract, not a storage mandate for all runtime paths.

## 4.2 Layer B: Reflection Attribute Layer

Use reflection attributes as policy on row fields:
- Serialization policy (`SaveGame`, `Transient`, `SkipSerialization`)
- Replication policy (`Replicated`, `NotReplicated`)
- Editor policy (`EditAnywhere`, category, clamps)
- Runtime policy extensions (new attributes proposed below)

This becomes the single policy source for editor, network, serialization, and scripting exposure.

## 4.3 Layer C: Reactive Layer

Attach reactivity at field-policy boundaries:
- `TObservableField<T>` for ECS/high-frequency fields.
- `TReactiveValue<T>` for UI and orchestration.

Introduce a unified reflected-reactive bridge:
- A field can be marked as reactive in metadata.
- Generated or templated adapters connect field changes to observers without duplicating domain logic.

## 4.4 Layer D: Typed VEX Pipeline

Replace string-mapping with typed stages:
1. Parse
2. TypeCheck
3. Normalize
4. Lower (HLSL / Verse)

Model stages with `TPhantomState` + `TResult<Module, DiagnosticSet>` to enforce legal transitions.

## 4.5 Layer E: Swarm Instancing Runtime

Use typed row-based instance descriptors to feed runtime dispatch:
- Per-instance state buffer handles
- Per-instance command UBO values
- Per-instance validated script module

Runtime executes multi-instance dispatch in one subsystem tick loop with explicit resource ownership.

## 5. Data Row Evolution Extensions

Recommended next extensions to `FlightRowTypes`:

1. `RowView` (type-erased read/write view) for editor/tooling ingestion.
2. Row schema version traits and migration transforms.
3. Row-to-fragment mapping traits for Mass query/config generation.
4. Row projection policies (compile-time include/exclude by attribute class).
5. Diff helpers optimized for row fields to avoid current reflective full scans where not needed.

## 6. Integration Operations Backlog

The following integration operations provide a concrete bridge from current state to target architecture.

## Safety and Correctness Operations

- **IOP-001**: Fix `TReactiveValue` lambda subscription ownership and deallocation.
- **IOP-002**: Add unsubscribe lifecycle management to `TEffect`.
- **IOP-003**: Repair `TAsyncOp` continuation chaining semantics.
- **IOP-004**: Enforce attribute-gated serialization/diff/patch.

## Schema and Metadata Operations

- **IOP-010**: Define core row schemas for assets, simulation params, and swarm instances.
- **IOP-011**: Add reflection policy attributes for scripting visibility and GPU uniform mapping.
- **IOP-012**: Add schema version/migration traits for row-based data evolution.

## Reactive-Reflection Operations

- **IOP-020**: Implement reflected-reactive binding adapters from field traits.
- **IOP-021**: Define per-field propagation policy (`UIOnly`, `RuntimeOnly`, `Replicated`, `GpuUniform`).
- **IOP-022**: Add deterministic update ordering policy for multi-subscriber change propagation.

## VEX Compiler Operations

- **IOP-030**: Build typed AST for current VEX subset.
- **IOP-031**: Bind AST symbols to reflected row descriptors.
- **IOP-032**: Implement diagnostics (syntax + type + policy checks).
- **IOP-033**: Lower typed IR to HLSL/Verse backends.
- **IOP-034**: Add `#instance` grammar lowering to `FSwarmInstanceRow`.

## Differential/Control Operations

- **IOP-040**: Add `Order::Jerk` and compile-time derivative constraints.
- **IOP-041**: Introduce integrator concepts (`Euler`, `RK2`, `RK4`) and strategy selection.
- **IOP-042**: Prototype dual-number AD path for gradient steering in predictive rollout.

## 7. Phased Delivery Plan

## Phase 0: Safety Baseline (Immediate)

Deliver IOP-001..004 plus targeted tests before broader abstraction growth.

Exit criteria:
- No subscription leaks in core tests.
- Effect teardown validated under dependency lifetime inversion.
- Async chain tests validate correct continuation target behavior.
- Serialization tests prove `Transient`/`SkipSerialization` are excluded.

Current status:
- **Implemented and validated** in headless automation.
- Remaining operational closeout is to record one clean GPU-capable evidence run for non-skipped GPU paths.

## Phase 1: Row/Reflection Contract Consolidation

Deliver IOP-010..012 and IOP-020..022.

Exit criteria:
- At least one production path uses row schema + reflection policy as single source.
- Metadata policy influences editor + serialization + runtime propagation consistently.

## Phase 2: Typed VEX and Instancing Bridge

Deliver IOP-030..034.

Exit criteria:
- `#instance` text compiles into typed descriptors.
- Failed scripts return diagnostics, not silent string substitutions.
- Valid modules lower deterministically to HLSL and Verse.

## Phase 3: Differential System Elevation

Deliver IOP-040..042.

Exit criteria:
- Jerk-order abstractions compile and integrate in at least one swarm control path.
- Integrator strategy is switchable without changing high-level pipeline wiring.

## 8. Testing Expansion Plan

Add tests that map directly to known risks and target features:

1. Reactive lifecycle tests:
   - `SubscribeLambda` ownership/deallocation under unsubscribe and bulk-clear paths.
   - `TEffect` destructor teardown while dependencies remain alive.

2. Functional async tests:
   - `TAsyncOp::Then` chain propagation across 2+ continuation hops.

3. Reflection policy tests:
   - Serialization excludes `Transient`/`SkipSerialization`.
   - Diff/Patch excludes non-persistable policy fields.

4. Typed VEX tests:
   - Syntax/type diagnostics for invalid scripts.
   - Successful `#instance` compile to typed instance row with stable lowering snapshots.

5. Row evolution tests:
   - Schema version migration correctness.
   - Row-to-fragment mapping contract checks.

## 9. Decisions and Open Questions

## Proposed Decisions

1. Row types are promoted to first-class schema contracts for assets/ECS/scripting.
2. Reflection attributes become policy authority across serialization, replication, editor, and scripting exposure.
3. Typed VEX compilation replaces direct string substitution as the default orchestration path.

## Open Questions

1. Should row schemas remain purely compile-time, or should selected boundaries support runtime row descriptors for dynamic tools?
2. How strict should script symbol exposure be when a reflected field lacks policy attributes?
3. Should reactive propagation support transactional batching semantics for multi-field updates in one frame?

## 10. Recommended Next Action

Start with **Phase 0** immediately.  
The lifecycle and ownership issues are preconditions for safely scaling row/reactive/reflection integration and typed scripting.
