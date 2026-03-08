# Current Focus: Persistence Contract + Parser Reliability

## Active Goals
1. Lock down SCSL persistence semantics (explicit `Persistent` vs `Stateless` behavior with deterministic tests).
2. Complete cloud/lattice prior-frame seeding path without regressing build stability.
3. Resolve current VEX parser diagnostic regressions in headless automation.
4. Keep headless triage output high-signal (`focused` profile + error filtering + ANSI coloring).

## Current Status (2026-03-08)
- Development build is green (`./Scripts/build_targets.sh Development` succeeded).
- Persistence observability, seeding/merge behavior, and deterministic test hooks are in place.
- Persistence checklist tracks **A, B, C, D** are implemented and building successfully.
- Known parser failures currently observed:
  - `FlightProject.Schema.Vex.Parser.Semantics.IfConditionBoolLike`
  - `FlightProject.Schema.Vex.Parser.Semantics.TypeMismatch`
  - `FlightProject.Schema.Vex.Parser.Diagnostics`

## Immediate Next Steps
1. Run focused swarm persistence test bucket and capture results for both new tests.
2. Run focused parser bucket and capture root-cause notes for the three failing assertions.
3. Decide whether parser fixes or persistence runtime verification is the next implementation slice.

## Risks / Watch Items
- RDG dependency ordering hazards while introducing prior-frame texture seeding.
- False confidence from `NullRHI` contexts for GPU behavior; keep expected-skip semantics explicit.
- Parser and persistence workstreams may interact through shared logging noise; continue using focused/error-filtered runs for triage.
