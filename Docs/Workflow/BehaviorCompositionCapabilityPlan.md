# Behavior Composition Capability Plan

This document defines the next planning layer after the initial `Sequence` vertical slice for behavior composition.

It focuses on four follow-on areas:

- composite capability envelopes
- lane aggregation rules
- selector semantics
- vector/scalar capability reporting

For the durable runtime boundary, see `Docs/Architecture/BehaviorComposition.md`.
For the first narrow implementation slice, see `Docs/Workflow/BehaviorCompositionVerticalSlicePlan.md`.
For the broader target and policy framing, see `Docs/Architecture/TargetCapabilitySchema.md`.

## 1. Goal

Extend behavior composition from "composite behaviors can execute" into "composite behaviors can explain what execution futures are legal, preferred, and actually committed."

The next step is not more syntax.
The next step is a stronger contract between:

- behavior semantics
- capability detection
- policy selection
- execution-lane legality
- committed runtime truth
- reporting

The working pattern should stay:

current behavior intent
    -> capability envelope
        -> policy selection
            -> lane legality
                -> runtime evaluation
                    -> commit
                        -> report

## 2. Why This Layer Is Needed

The first slice proves that one selected `BehaviorID` can own child execution.
It does not yet answer the harder integration questions:

- what lane is a composite behavior actually legal on?
- when children disagree on lane support, what is the parent truth?
- how should selector fallback interact with lane selection?
- how should scalar-only and vector-capable children be reported without collapsing them into one vague backend label?

Without this layer, behavior composition risks becoming runtime-only control flow with weak policy visibility.
That would conflict with the current FlightProject direction of validation before commit and meaningful post-selection reports.

## 3. Scope

### In Scope

- define a capability-envelope model for atomic and composite behaviors
- define parent-lane aggregation rules from child capability data
- define truthful selector semantics for phase one and phase two
- define reporting surfaces for scalar/vector legality and committed execution shape
- identify the main implementation seams across Verse, VEX, orchestration, and reporting

### Out Of Scope

- full authored composition DSL
- rollback-capable selector execution
- generalized async composition
- GPU-specific composite scheduling details
- final data schema for every report/export surface

## 4. Core Definitions

### 4.1 Lane

A lane is the execution path category chosen or observed for a behavior.

Initial lane vocabulary should stay close to existing reality:

- `NativeScalar`
- `NativeSimd`
- `VerseVm`
- later `GpuKernel`
- later boundary-aware or staged-mutation lanes if they become real execution surfaces

### 4.2 Capability Envelope

A capability envelope is the bounded set of execution futures a behavior can legally project before runtime commit.

It should answer at least:

- which lanes are legal
- which lanes are preferred by policy
- which lanes are disallowed and why
- whether execution is scalar-only, vector-capable, or shape-flexible
- whether execution can tolerate mixed child lanes
- what contracts must hold before execution becomes legal

### 4.3 Lane Truth Levels

Each behavior, especially composites, should distinguish three truths:

- selected lane
- legal lane set
- committed lane

Definitions:

- selected lane: what compile/runtime policy prefers before execution
- legal lane set: what the behavior can actually execute on under current contracts
- committed lane: what execution really used after runtime evaluation

These truths must remain distinct in reporting.

## 5. Composite Capability Envelopes

## 5.1 Parent Envelope Principle

A composite behavior should not advertise one optimistic lane label.
It should advertise a capability envelope derived from:

- child legality
- child execution-shape compatibility
- parent operator semantics
- policy constraints

The parent envelope is therefore not only "intersection of child backends."
It is:

child capability facts
    + operator rules
    + host/schema compatibility
    + policy allowances
    = parent capability envelope

## 5.2 Recommended Envelope Fields

The next implementation pass should define an internal capability carrier with fields close to:

- `LegalLanes`
- `PreferredLane`
- `CommittedLane`
- `ExecutionShape`
- `bAllowsMixedLaneExecution`
- `bRequiresSharedTypeKey`
- `RequiredContracts`
- `DisallowedLaneReasons`
- `ChildSummaries`

`ExecutionShape` should be explicit rather than implied by backend name.
Recommended first values:

- `ScalarOnly`
- `VectorCapable`
- `VectorPreferred`
- `ShapeAgnostic`

## 5.3 Envelope Rules By Operator

### Sequence

`Sequence` should initially require:

- shared type-key compatibility across all children
- compatible mutation host assumptions
- lane legality for each child under the same execution request unless mixed lanes are explicitly allowed

Parent envelope rules:

- parent legal lane exists only if every child is legal on that lane
- parent preferred lane should come from the highest-ranked common legal lane after policy filtering
- parent execution shape should degrade conservatively

Example:

- child A is `VectorCapable`
- child B is `ScalarOnly`
- parent sequence is legal on `NativeScalar`
- parent sequence is not automatically `NativeSimd` legal
- parent execution shape should report `ScalarOnly` unless mixed-lane execution is explicitly allowed and observed

### Selector

`Selector` is different because not every child must run.

Parent envelope rules:

- parent legal lane may be the union of child legal lanes
- parent preferred lane must still respect selector ordering and policy
- parent report must distinguish:
  - lanes legal for the parent as a whole
  - lanes legal for the chosen child branch
  - lanes rejected because an earlier child had priority but no legal path

This is why selector needs richer evidence than sequence.

## 6. Lane Aggregation Rules

## 6.1 Aggregation Principle

Lane aggregation should be operator-specific, not globally generic.

Recommended rule:

- `Sequence` aggregates by constrained intersection
- `Selector` aggregates by ordered union with branch evidence
- future `Parallel` would aggregate by compatibility class plus synchronization constraints

## 6.2 Sequence Aggregation

For phase one hardening, define these rules:

1. Compute each child's legal lane set.
2. Intersect those lane sets.
3. Remove lanes blocked by type-key, schema-host, or mutation-host incompatibility.
4. Apply policy ranking to the remaining lanes.
5. Report:
   - common legal lanes
   - dropped lanes
   - the reason each lane was dropped

This keeps sequence selection explainable and conservative.

## 6.3 Selector Aggregation

Selector cannot be modeled as a simple intersection or pure union.

Recommended rule:

1. Evaluate children in declared priority order.
2. For each child, compute legal lanes under the current host and policy.
3. Attempt execution on the best legal lane for that child.
4. If the child cannot execute or semantically fails, record the failure reason and continue.
5. Commit the first successful child branch.

Selector reporting should therefore preserve:

- branch order
- per-branch legal lanes
- selected lane for each attempted branch
- failure reason for each failed branch
- committed child id and committed lane for the winning branch

## 6.4 Mixed-Lane Rules

Mixed-lane execution should not be implicit.

Near-term recommendation:

- disallow mixed-lane `Sequence` execution by default
- allow selector branches to commit on different lanes because only one child commits
- record mixed-lane allowance as explicit policy, not a hidden fallback

Longer-term mixed-lane support should only exist when the runtime can prove:

- host compatibility across lane boundaries
- deterministic handoff semantics
- truthful reporting of selected vs committed lane per child

## 7. Selector Semantics

## 7.1 Phase-One Truthful Selector

The first selector implementation should make narrow, honest claims:

- evaluate children in priority order
- choose the first child that executes successfully
- record branch attempt and failure evidence
- do not promise rollback across already-applied writes

This gives useful behavior selection without over-claiming transactional semantics.

## 7.2 Semantic Failure vs Execution Failure

Selector needs two distinct failure classes:

- execution failure
- semantic failure

Execution failure examples:

- child missing
- lane unavailable
- incompatible type key
- unsupported backend for the current host

Semantic failure examples:

- explicit failable behavior result
- guard/precondition failure
- contract-visible state says "this branch does not apply"

Both should feed selector fallback, but they should not be merged into one opaque reason string.

## 7.3 Guarded Selector

The best near-term selector shape is:

guard -> child execute -> result

That suggests a future internal model where each child branch may carry:

- optional guard evaluation
- lane capability summary
- execution attempt
- branch result

This would let FlightProject answer:

- branch was legal but guard-rejected
- branch was guard-approved but lane-illegal
- branch was lane-legal but semantically failed
- branch succeeded and committed

## 7.4 Rollback Boundary

Rollback should remain a separate milestone, not an implied selector property.

Truthful rollback requires speculative mutation infrastructure such as:

- scratch AoS hosts
- write journals
- deferred commit products

Until that exists, selector should be documented and reported as:

- ordered branch attempt
- first-success commit
- no general rollback guarantee

## 8. Vector/Scalar Capability Reporting

## 8.1 Why Backend Labels Are Not Enough

The current backend labels can tell us where code ran.
They do not fully tell us what execution shape was legal or preferred.

That distinction matters because:

- `NativeScalar` and `NativeSimd` have different data-shape assumptions
- a behavior may be SIMD-legal but not profitable
- a behavior may be vector-capable in isolation but scalar-only in a composite

Vector/scalar truth therefore needs explicit reporting.

## 8.2 Recommended Reporting Questions

Per behavior, FlightProject should eventually be able to report:

- is this behavior scalar-only?
- is it vector-capable?
- is vector execution preferred or only tolerated?
- what vector width classes are legal?
- what blocked vector execution?
- did the committed execution actually use vector shape?

## 8.3 Recommended First Report Fields

At minimum, add internal/report-facing fields conceptually equivalent to:

- `ExecutionShape`
- `PreferredExecutionShape`
- `CommittedExecutionShape`
- `LegalVectorWidths`
- `BlockedVectorReasons`

For composites, also report:

- `ChildExecutionShapes`
- `AggregatedExecutionShape`
- `MixedShapePolicy`

## 8.4 Parent Shape Aggregation

Recommended parent-shape rules:

- `Sequence` degrades to the most restrictive child shape unless mixed-shape execution is explicitly allowed
- `Selector` may expose multiple branch shapes, but the committed shape is the winning branch shape
- parent reports should distinguish aggregate possibility from committed reality

Example:

- child A: `VectorPreferred`
- child B: `ScalarOnly`
- sequence aggregate: `ScalarOnly`
- selector aggregate: `VectorPreferred | ScalarOnly` by branch, committed shape depends on chosen child

## 9. Suggested Implementation Seams

The next slice should likely touch these surfaces:

- `Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h`
- `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`
- `Source/FlightProject/Public/Orchestration/FlightBehaviorBinding.h`
- `Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp`
- existing compile artifact/report carriers under `Source/FlightProject/Public/Vex/`
- focused automation in `Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp`

Recommended internal additions:

1. stabilize `FBehaviorExecutionResult` into a reusable execution/report carrier
2. add a behavior capability-envelope carrier for atomic and composite behaviors
3. add sequence lane aggregation first
4. add selector execution with explicit branch evidence and no rollback promise
5. add vector/scalar reporting before mixed-lane execution support

## 10. Proposed Incremental Phases

### Phase A: Execution Result Hardening

- separate execution failure from semantic failure
- record selected lane, legal lanes, and committed lane
- record execution shape and committed shape
- preserve child attempt order

### Phase B: Composite Capability Envelopes

- compute envelope data for atomic behaviors
- aggregate parent envelopes for `Sequence`
- surface dropped-lane reasons in reports

### Phase C: Selector Without Rollback

- add ordered branch evaluation
- add per-branch evidence
- commit first successful branch
- report no-rollback truth explicitly

### Phase D: Vector/Scalar Reporting

- add explicit execution-shape fields
- report vector legality and blockers
- aggregate parent shape from child shapes

### Phase E: Mixed-Lane Policy Investigation

- determine whether mixed-lane execution is ever legal for `Sequence`
- if yes, require explicit policy and host-handoff proof
- if no, keep sequence as same-lane-only and document it as a constraint

## 11. Acceptance Criteria

This planning layer is ready to convert into code slices when the project has:

- a clear parent capability-envelope model for `Sequence` and `Selector`
- operator-specific lane aggregation rules
- selector semantics that are honest about failure and rollback boundaries
- explicit vector/scalar reporting vocabulary separate from backend labels
- a proposed report shape that can explain selected, legal, and committed truth without stringly fallback logic

## 12. Main Proposition

The strongest proposition from this follow-on plan is:

composite behavior should be treated as a projector of legal future execution states, not only as a child-call container

That means the next implementation work should optimize for:

- explicit legality
- conservative aggregation
- meaningful failure evidence
- truthful commit reporting

If those stay explicit, composition can grow into selector, policy-aware lane choice, and broader schema-bound runtime integration without collapsing back into hidden fallback behavior.
