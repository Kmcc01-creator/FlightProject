# VerseVM Architecture: Simulation Mindset

This document codifies the architectural "thinking" required to effectively bridge simulation systems (like VEX) to the Verse Virtual Machine.

## 1. The VerseVM Mindset

VerseVM is not a traditional general-purpose VM; it is a **Simulation VM** optimized for speculative execution, concurrency, and persistent state.

### Failure as a First-Class Citizen
- **Mechanism**: Every block can exist within a `FailureContext`.
- **Mindset**: Don't use exceptions for errors; use "failure" for logic branches.
- **Utilization**: VerseVM expects "Fallible" expressions that can speculate and rollback if a condition isn't met.

### The Melt/Freeze Lifecycle
- **Mechanism**: `VValue` objects are often immutable by default once visible to the simulation.
- **Mindset**: To modify data, you "Melt" it (create a mutable local copy), perform your logic, and "Freeze" it to commit.
- **Utilization**: This ensures deterministic state across replicated simulation ticks.

### Concurrency via Suspensions
- **Mechanism**: `VTask` and `VSuspension` allow code to pause without blocking threads.
- **Mindset**: Think in "Micro-tasks" rather than threads.
- **Utilization**: Async behaviors (e.g., waiting for a GPU readback) should `Yield` and allow other behaviors to execute, rather than polling.

## 2. Timing & Synchronization

When designing systems that interface with VerseVM, synchronization should be handled at the **Boundary**, not inside the VM logic.

### The Synchronous Boundary
- **Current Pattern**: `VerseContext.EnterVM([...])` is used to execute behaviors during the Mass ECS `PostPhysics` or `Logic` phase.
- **Rule**: VM execution is synchronous relative to the calling thread (e.g., the Task Graph thread running the Mass Processor).

### Asynchronous Perception (GpuWork)
- **Problem**: Simulation data often arrives from the GPU (via `io_uring`) at a different rate than the VM tick.
- **Solution**:
    1. The `io_uring` bridge detects GPU completion.
    2. It updates a `TReactiveValue<bool>` or signals a `VSemaphore`.
    3. The Verse Task that was `Await`-ing that signal is moved from `Suspended` to `Active`.
    4. The next VM tick resumes the task.

### Task Graph Alignment
- **Integration**: VerseVM execution should be treated as a high-priority job within the Unreal **Task Graph (FTaskGraphInterface)** or **UE::Tasks**.
- **Constraint**: Never hold a mutex inside a `VerseContext.EnterVM` block. The VM's transactional nature (`AutoRTFM`) may cause retries, leading to deadlocks if external locks are held.

## 3. Performance Considerations

### NaN-Boxing
- **Mechanism**: All values (`VValue`) are 64-bit tagged payloads.
- **Cost**: Accessing a "Thunk" (Native C++ function) requires tagging/untagging.
- **Optimization**: Use `ExecuteBehaviorBulk` to minimize the "Enter/Exit VM" overhead. Batching 1000 droids into one VM context is significantly faster than 1000 individual `EnterVM` calls.
