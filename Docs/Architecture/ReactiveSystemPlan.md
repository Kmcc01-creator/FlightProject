# Architectural Plan: Flight Reactive Systems

## 1. Overview & Intentions
Our goal is to evolve the current reactive UI prototypes into a unified, high-performance **Flight Reactive** core. This system will provide a declarative way to manage data dependencies and side effects across multiple engine subsystems (UI, Mass ECS, Async IO).

### Key Paradigms:
- **Core Reactivity**: Move `TReactiveValue`, `TComputedValue`, and `TEffect` into a generic `Flight::Reactive` core.
- **Differentiated Observables**:
  - `TObservableField<T>`: Single-subscriber, zero-allocation (Performance/ECS path).
  - `TReactiveValue<T>`: Multi-subscriber, lifecycle-aware (UI/Logic path).
- **Functional Glue**: Use `TResult`, `TEffect`, and monadic chaining to reduce imperative boilerplate.

## 2. Implementation Strategy

### Phase 1: Core Refactor (Current Blocker)
- **C++23 Compatibility**: Refactor "Captured Binding" structs into classes with explicit constructors to satisfy Clang 20's strictness.
- **Memory Safety**: Implement managed cleanup for all heap-allocated binding state. Integrate with `FReactiveContext` to ensure zero-leak operation.
- **Separation of Concerns**: Split `FlightReactiveUI.h` into:
  - `FlightReactive.h`: Generic reactive primitives.
  - `FlightReactiveSlate.h`: UI-specific bindings and widget lifecycle integration.

### Phase 2: Mass ECS & Gameplay Integration
- **Reactive Fragments**: Explore using `TObservableField` inside Mass fragments to trigger system-level reactions only on data change.
- **Gameplay Effects**: Use `TEffect` for non-UI logic (e.g., sound triggers, AI state transitions) based on shared state containers.

## 3. Testing Plan

### Level 1: Unit Validity (Core)
Verify the foundational behavior of reactive primitives:
- `TReactiveValue`: Multi-subscription, notification ordering, and value round-tripping.
- `TComputedValue`: Lazy vs. eager evaluation and dependency tracking.
- `TEffect`: Initial execution, cleanup execution, and nested effect safety.

### Level 2: Integration (Contexts)
- **UI Context**: Bind to mock Slate widgets; verify `SetText` and visibility changes trigger correctly.
- **Logic Context**: Share a `TStateContainer` between two independent logical components and verify synchronization.

### Level 3: Performance & Stress
- **Throughput**: Verify `TObservableField` overhead in high-count (10k+) entity simulations.
- **Leak Detection**: Long-running tests ensuring `FReactiveContext` correctly cleans up thousands of transient bindings.

## 4. Potential Avenues for Data Reuse
The unified reactive core enables data to be accessed in different contexts without duplication:
- **Shared State**: A single `TStateContainer<FDroneState>` can drive both a **Slate HUD** and a **Mass Movement Processor**.
- **Async Synchronization**: Use `TReactiveValue` to signal completion of `io_uring` operations to the UI without poll-loops.
- **Live-Config**: React to live-reloads of reflectable structs, automatically updating both simulation parameters and editor debug panels.
