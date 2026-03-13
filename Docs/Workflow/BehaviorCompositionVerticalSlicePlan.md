# Behavior Composition Vertical Slice Plan

This document turns the behavior-composition direction into a minimal, defensible first implementation plan for FlightProject.

For the durable architecture, see `Docs/Architecture/BehaviorComposition.md`.
For the current orchestration direction, see `Docs/Workflow/OrchestrationImplementationPlan.md`.

## 1. Goal

Land one narrow vertical slice that proves:

- a selected `BehaviorID` can represent a composite behavior
- the runtime can execute `Sequence(childA, childB)` using the existing behavior registry
- reports can explain that the selected behavior was composite and which children ran

The first goal is not generalized behavior graphs.
The first goal is not rollback.
The first goal is one honest, testable composite execution path.

## 2. Scope

### In Scope

- one composite behavior kind: `Sequence`
- child invocation through the existing `BehaviorID -> FVerseBehavior` registry
- synchronous execution only
- struct and bulk execution on shared type/schema hosts
- CPU-native and Verse VM runtime lanes when already executable today
- report surfaces that expose composite-vs-atomic identity and child execution order

### Out Of Scope

- `Selector`
- rollback or speculative mutation
- async child execution
- GPU composite execution
- boundary-aware composite execution
- orchestration selecting multiple behaviors for one cohort
- runtime-generated Verse master-script linking

## 3. Current Code Surfaces

The slice should build on these existing files:

- `Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h`
- `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`
- `Source/FlightProject/Public/Orchestration/FlightBehaviorBinding.h`
- `Source/FlightProject/Public/Orchestration/FlightExecutionPlan.h`
- `Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp`
- `Source/FlightProject/Private/Mass/UFlightVexBehaviorProcessor.cpp`
- `Source/FlightProject/Public/Vex/FlightCompileArtifacts.h`
- `Source/FlightProject/Private/Vex/FlightCompileArtifacts.cpp`
- `Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp`

Optional supporting additions:

- new focused tests for composite behavior execution and reporting
- new small shared type(s) under `Public/Verse/` if the result/executor structures grow

## 4. Minimum Design Rules

The first slice should follow these constraints:

- keep one selected `BehaviorID` per cohort
- represent composition inside `UFlightVerseSubsystem::FVerseBehavior`
- do not add a second orchestration planner for composite graphs
- do not claim rollback or transactional safety that the runtime cannot prove
- require child behaviors to share a compatible type key
- reject unsupported children explicitly and report why

## 5. Phase-One Runtime Model

### 5.1 Add Behavior Kind

Extend `FVerseBehavior` so a behavior can be:

- `Atomic`
- `Sequence`

Recommended first fields:

- `Kind`
- `ChildBehaviorIds`
- `bIsComposite`

Optional report fields:

- `CompositeOperator`
- `CompositeDiagnostics`

The atomic path should remain the default.

### 5.2 Add Structured Execution Result

Introduce a small execution-result type used internally by behavior execution.

Recommended first fields:

- `bSucceeded`
- `bCommitted`
- `Backend`
- `Detail`
- `ExecutedChildBehaviorIds`

This result does not need to be public Blueprint API in the first slice.
It does need to exist so sequence execution and later selector execution share one truth surface.

### 5.3 Add Composite Executor

Add an internal composite execution path in `UFlightVerseSubsystem`.

Recommended first methods:

```cpp
FBehaviorExecutionResult ExecuteBehaviorResolved(...);
FBehaviorExecutionResult ExecuteCompositeBehavior(...);
FBehaviorExecutionResult ExecuteSequenceBehavior(...);
```

The sequence executor should:

- look up each child behavior
- validate child executability
- validate child type-key compatibility
- run child A, then child B, in order
- stop and report failure if any child fails to execute
- aggregate child execution evidence into the result

For this first slice, "failure" can mean:

- child behavior missing
- child behavior non-executable
- incompatible type key
- unsupported backend for the current surface

It does not need to mean semantic failure inside the VEX language yet.

## 6. Compile / Authoring Strategy

The first slice does not need a new composite DSL.

Recommended approach:

- keep atomic behaviors compiled through `CompileVex(...)`
- register composite behaviors through a small explicit C++ helper in tests first

That helper can look like:

```cpp
bool RegisterCompositeSequenceBehavior(uint32 BehaviorID, TConstArrayView<uint32> ChildBehaviorIds, ...);
```

Why this is the right first step:

- it proves runtime semantics without prematurely designing authoring syntax
- it keeps the first vertical slice small
- it lets the team learn what reporting and validation data are actually needed

Authoring surfaces can come later:

- data-table-backed composition rows
- generated assets
- VEX-side composition syntax
- policy-driven composite definitions

## 7. Orchestration / Report Changes

Orchestration should remain structurally simple in the first slice.

Recommended changes:

- `FFlightBehaviorRecord` gains composite metadata:
  - `bIsComposite`
  - `CompositeOperator`
  - `ChildBehaviorIds`
- execution-plan JSON/report surfaces expose that the selected behavior is composite
- binding selection remains one selected `BehaviorID` per cohort

The plan does not need one step per child in phase one.
It only needs to preserve that the selected behavior is composite and report the child list.

## 8. Processor Changes

`UFlightVexBehaviorProcessor` should not need large structural change.

Expected update:

- after resolving the selected `BehaviorID`, execution should flow through the shared internal execution-result path
- processor still chooses one selected behavior for the chunk
- composite execution remains encapsulated inside `UFlightVerseSubsystem`

This preserves current orchestration and Mass integration assumptions.

## 9. Testing Plan

The first slice should land with focused automation.

### 9.1 Runtime Tests

Add tests that prove:

- atomic child behaviors can be compiled independently
- a composite sequence behavior can be registered from those children
- executing the composite runs children in order
- final state reflects both child mutations

### 9.2 Report Tests

Add tests that prove:

- orchestration behavior records mark the behavior as composite
- report JSON includes operator and child ids
- execution result carries child execution order

### 9.3 Failure Tests

Add tests that prove:

- missing child behavior is reported cleanly
- non-executable child behavior prevents composite execution
- incompatible type-key children are rejected with explicit diagnostics

## 10. Acceptance Criteria

The vertical slice is done when all of the following are true:

- one composite `Sequence` behavior can be registered and executed
- execution works on the current shared type/schema path without bypassing backend selection
- orchestration still binds one selected `BehaviorID` per cohort
- reports clearly identify the selected behavior as composite and list its children
- tests cover success plus at least one unsupported-child failure path

## 11. Immediate Follow-Ons

If the sequence slice lands cleanly, the next steps should be:

1. move from internal-only execution result to a more stable report carrier
2. add `Selector` without rollback claims
3. add speculative mutation hosts only if truthful selector rollback becomes a real requirement
4. evaluate whether composite behaviors deserve authored policy/data surfaces
5. investigate VM-native composition once the report and execution contracts are stable
