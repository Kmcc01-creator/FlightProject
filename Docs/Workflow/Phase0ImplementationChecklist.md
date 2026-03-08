# Phase 0 Implementation Checklist

**Date**: 2026-03-07  
**Scope**: Safety baseline from `RowEvolutionDesignReview.md` (`IOP-001..004`)

## Objectives

- Stabilize reactive lifecycle ownership and teardown semantics.
- Fix async continuation chain propagation semantics.
- Enforce reflection serialization policy (`Transient`, `SkipSerialization`).
- Add focused tests for the above.

## Checklist

- [x] **IOP-001** `TReactiveValue` subscription ownership
- [x] Add cleanup path for lambda-captured callback userdata.
- [x] Ensure `Unsubscribe` and `UnsubscribeAll` release owned callback userdata.
- [x] Ensure destruction path clears all subscriptions.

- [x] **IOP-002** `TEffect` teardown safety
- [x] Track dependency subscription IDs.
- [x] Unsubscribe dependencies during effect disposal/destruction.
- [x] Prevent unsafe copy/move of effect objects that capture `this` in callbacks.

- [x] **IOP-003** `TAsyncOp::Then` chain semantics
- [x] Move to shared completion state so copied ops target the same continuation state.
- [x] Ensure late subscribers receive already-completed values.
- [x] Guard against duplicate completion.

- [x] **IOP-004** Reflection policy gating
- [x] Add serialization policy checks for `Transient`, `DuplicateTransient`, `SkipSerialization`.
- [x] Apply policy checks in `Serialize`, `Diff`, `Apply`, and replication path.

## Test Additions

- [x] Reactive subscription lifecycle test (`SubscribeLambda` ownership/release).
- [x] Reactive effect teardown test (`TEffect` no callback after destruction).
- [x] Functional async chain test (`TAsyncOp` propagation + late subscriber).
- [x] Reflection serialization policy test (`Transient`/`SkipSerialization` exclusion).
- [x] Reflection diff/patch policy test (only serializable fields changed/applied).
- [x] Logging query parser test (`cat:`, `-cat:`, `thread:`, `frame:` token behavior).
- [x] Logging ring-buffer stats test (bounded capacity + overwrite accounting).

## Validation Snapshot (2026-03-07)

- `Automation RunTests FlightProject` discovers **27** project tests.
- New stabilization tests are discovered and passing in headless runs:
  - `FlightProject.Functional.Async.ChainPropagation`
  - `FlightProject.Logging.Core.QueryFilter`
  - `FlightProject.Logging.Core.RingBufferStats`
  - `FlightProject.Reactive.Core.EffectTeardown`
  - `FlightProject.Reactive.Core.SubscriptionLifecycle`
  - `FlightProject.Reflection.Core.SerializationPolicy`
  - `FlightProject.Reflection.Core.DiffPolicy`
- Headless-only expected skips remain for GPU-dependent tests (`GpuPerception`, swarm full integration under `NullRHI`).

## Phase 0 Closeout Projection

Phase 0 core scope (`IOP-001..004`) is implemented and validated. Remaining closeout work is operational hardening:

- [ ] Capture one clean full-GPU test evidence run (`run_tests_full.sh`) with non-skipped GPU path.
- [ ] Add a short regression note linking phase-0 fixes to test names in `CurrentBuild.md`.
- [ ] Tag this checkpoint in project notes as "Phase 0 Stabilization Complete" after GPU evidence run.

Target: complete closeout in the next implementation cycle.

## Phase 1 Kickoff Projection

Planned first slice for Phase 1 (`Row/Reflection Contract Consolidation`):

- [x] Define initial canonical schema contracts:
  - `FFlightAssetRequirementRow`
  - `FFlightNiagaraRequirementRow`
  - `FFlightRenderProfileRow`
- [x] Implement first production bridge path from code-defined schema to runtime tooling:
  - Manifest export in `UFlightScriptingLibrary`
  - Python idempotent ensure/validate (`FlightProject.SchemaTools`)
  - One Niagara requirement end-to-end (`NS_SwarmVisualizer`)
- [x] Add initial schema contract automation coverage:
  - `FlightProject.Schema.Manifest.NiagaraContract`
  - `FlightProject.Schema.Manifest.Export`
- [ ] Expand schema coverage to include simulation row contracts (`FSimulationParamRow`, `FSwarmInstanceRow`, `FVexSymbolRow`).
- [ ] Add deeper Niagara property validation (user params/data interface binding introspection beyond existence).
- [ ] Add render-profile application checks (schema vs active project/render configuration).

## Follow-Up (Phase 1+)

- [ ] Add long-running stress test for high-frequency subscribe/unsubscribe cycles.
- [ ] Add explicit policy tests for replicated + transient conflict rules.
- [ ] Introduce `TComputedValue` implementation and dependency tracking tests.
- [ ] Begin typed VEX parse/typecheck pipeline scaffolding.
