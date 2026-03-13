# Behavior Composition

This document defines the current FlightProject direction for composing VEX/Verse behaviors as reusable execution units rather than treating one compiled script as the only meaningful program boundary.

For the current project-wide architectural frame, see `Docs/Architecture/CurrentProjectVision.md`.
For the world coordination surface that consumes behavior selection, see `Docs/Architecture/OrchestrationSubsystem.md`.
For the mutation-centered compiler/runtime frame, see `Docs/Architecture/VexStateMutationSchemaFrame.md`.
For the next implementation-planning layer after the initial sequence slice, see `Docs/Workflow/BehaviorCompositionCapabilityPlan.md`.

## 1. Short Version

FlightProject should treat:

- atomic VEX behaviors as reusable compiled functions
- composite behaviors as behavior-owned execution graphs
- orchestration as the selector of legal executable behaviors for a cohort
- runtime composition as the owner of in-graph state-dependent branching

The main rule is:

composite behavior is a behavior kind, not a new orchestration kind.

That means:

- a cohort can still bind to one selected `BehaviorID`
- that selected behavior may internally represent a `Sequence`, `Selector`, or later `Parallel` composition
- the execution/runtime layer owns child invocation semantics
- orchestration continues owning legality, visibility, contracts, and reports

## 2. Why This Direction Fits FlightProject

FlightProject already has most of the prerequisites for compositional behavior:

- behaviors are already compiled and stored as first-class records keyed by `BehaviorID`
- compile policy already carries preferred domain, fallback allowance, and required contracts
- schema binding already describes legal symbol access and backend compatibility
- orchestration already binds behaviors to cohorts and lowers them into execution plans
- VVM assembly already exists for backend execution beyond plain text lowering

The missing piece is not "can FlightProject compile one behavior?"
The missing piece is "can one selected behavior invoke other behaviors under an explicit contract and report why that path was chosen?"

## 3. Current Reality

The current runtime is already modular at the behavior-record level.

It is not yet modular at the composed-program level.

Today:

- one `BehaviorID` maps to one compiled behavior record
- one cohort resolves to one selected behavior
- one execution-plan step carries one selected `BehaviorID`
- execution surfaces run one selected behavior per invocation or per chunk

This is already enough to support:

- atomic behavior reuse
- backend-aware compilation and dispatch
- schema-bound execution over arbitrary type keys
- reporting of compile/execution legality

This is not yet enough to support:

- behavior-to-behavior calls
- reusable selector/sequence composition
- composite reports that explain child evaluation order
- truthful rollback claims across all backends

## 4. Core Thesis

FlightProject should formalize two categories of behavior:

### 4.1 Atomic Behavior

An atomic behavior is one compiled VEX behavior with one owned semantic body.

Examples:

- `Avoidance`
- `PathFollow`
- `TargetAcquisition`
- `Flee`

Its meaning is still:

- bind against a type/schema contract
- compile into legal execution backends
- execute against one runtime state host
- report the selected and committed execution path

### 4.2 Composite Behavior

A composite behavior is a behavior whose body is defined in terms of child behaviors plus a composition operator.

Examples:

- `Sequence(Avoidance, PathFollow)`
- `Selector(Flee, Patrol)`
- later `Parallel(Scan, SignalBroadcast)`

A composite behavior is still selected and reported as one behavior from orchestration's point of view.
Its internal structure belongs to the Verse/VEX execution layer.

## 5. Architectural Boundary

The ownership split should remain:

- orchestration decides which behaviors are legal and visible for a cohort
- behavior composition decides how a selected behavior evaluates its child behaviors
- mutation/runtime hosts decide when writes become authoritative

This avoids pushing runtime state-dependent branching into plan rebuild logic.

Example:

- orchestration may select `Combat.Drone.Master`
- the selected behavior may internally evaluate `Selector(Flee, Sequence(Patrol, Scan))`
- the decision "shield is low, choose flee" belongs to runtime behavior execution, not to `RebuildExecutionPlan()`

## 6. Execution Model

### 6.1 Behavior ABI

Every behavior, atomic or composite, should converge on a shared execution ABI.

Conceptually:

- input: bound runtime state host + type/schema key + execution context
- output: structured execution result, not only void

That result should eventually capture at least:

- success/failure
- backend used
- whether any commit became authoritative
- detail string or structured evidence for reporting

Without that result, selector-style behavior cannot explain why it advanced, failed, or fell through.

### 6.2 Composition Operators

The first operators should stay narrow:

- `Sequence`
  child behaviors execute in order
- `Selector`
  child behaviors execute in priority order until one succeeds

Later operators can expand to:

- `Parallel`
- scored/utility selection
- guarded composition
- profile-dependent or contract-dependent child activation

### 6.3 Runtime State Branching

State-dependent branching should happen inside composite behavior execution.

That includes questions such as:

- is the shield low enough to trigger `Flee`?
- did `TargetAcquisition` succeed?
- should fallback proceed to `Patrol`?

Orchestration should not answer those questions during plan rebuild.
Orchestration should only answer whether the selected composite behavior is legal, executable, and contract-compatible for that cohort.

## 7. Transaction And Rollback Truth

This area needs explicit discipline.

FlightProject should not over-claim Verse-style rollback semantics across runtime paths that do not currently prove them.

### 7.1 What Is Safe To Claim Now

Safe near-term claims:

- ordered sequence execution
- selector success/failure reporting
- explicit fallback from one child to another
- backend-aware explanation of why a child was or was not runnable

### 7.2 What Is Not Safe To Claim Yet

Not safe to claim as a general property today:

- automatic rollback across native fallback execution
- automatic rollback across SIMD execution
- automatic rollback across Mass-direct execution
- automatic rollback across GPU execution
- automatic rollback across boundary-aware or awaitable behavior paths

### 7.3 Requirement For Truthful Selector Rollback

If FlightProject wants selector semantics with rollback, then writes must first become speculative rather than immediately authoritative.

That implies a staged mutation model such as:

- scratch AoS host
- write journal
- deferred commit product
- other explicit staged-write mechanism

Only after validation succeeds should the behavior commit to the real runtime host.

## 8. Generated Verse Text vs VM-Native Composition

FlightProject should prefer VM-native composition over a text-first runtime linker.

Why:

- the project already assembles `VProcedure` directly from IR
- generated Verse text is currently better treated as observability and preview output
- a text-first linker would duplicate composition logic in a weaker, less explicit surface
- VM-native composition can stay closer to schema-bound legality and backend reporting

Generated Verse text is still useful:

- debug preview
- artifact export
- human inspection
- parity checking against direct VVM assembly

But it should not be the primary composition boundary.

## 9. Reporting Model

Composite behavior should extend the current report-first architecture rather than bypassing it.

Important report questions include:

- which child behaviors were considered?
- in what order were they evaluated?
- which child produced the chosen outcome?
- which child failed and why?
- which backend actually executed each child?
- did the composite commit any authoritative mutation?

This fits the project pattern:

description
    -> contract
        -> binding
            -> lowering
                -> execution
                    -> report

Composite behavior is just that same pattern one level higher.

## 10. Non-Goals

This direction should not immediately attempt to:

- replace orchestration with a behavior tree runtime
- treat runtime-generated Verse text as the canonical master-program path
- promise rollback on all execution domains
- solve generalized async composition in the first slice
- solve GPU or boundary-aware composite execution in the first slice

## 11. Recommended First Implementation Shape

The first realistic FlightProject slice should be:

- preserve one selected `BehaviorID` per cohort
- allow that selected behavior to be either atomic or composite
- implement `Sequence` first
- scope runtime execution to synchronous CPU and Verse VM paths only
- require all children to share the same bound type/schema host
- keep generated Verse as a report artifact rather than the source of truth

That preserves current orchestration shape while adding one new semantic layer:

selected behavior
    -> atomic execution
    or
    -> composite execution over child behaviors

## 12. Design Rule

When adding a new behavior-composition feature, ask:

1. Is this a behavior-owned execution concern or an orchestration-owned selection concern?
2. Can the selected behavior explain its child evaluation order and commit evidence?
3. Does this feature require speculative mutation before it can be claimed truthfully?
4. Can the current backend/runtime path actually prove the semantics being reported?

If the answer points toward runtime child invocation, reporting, and commit truth, it belongs in behavior composition rather than orchestration.
