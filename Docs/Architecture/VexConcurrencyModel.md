# VEX Concurrency & Task Orchestration Model

This document defines the architectural strategy for lifting Unreal Engine's threading and task models into the VEX DSL as first-class language constructs.

## 1. Concurrency Directives

To scale behavioral logic across millions of drones, VEX provides declarative directives to control the "where" and "how" of execution.

| Directive | Unreal Backend | Semantic Context | Dependency |
| :--- | :--- | :--- | :--- |
| **`@job`** | `UE::Tasks::Launch` | Short-lived, parallel batch math (e.g. Nav refinement). | `TaskGraph` |
| **`@thread`** | `ENamedThreads` | Long-running AI state evaluation or I/O. | `FQueuedThreadPool` |
| **`@async`** | Verse Leniency | Suspendable logic waiting for external events. | `VerseVM` |

### Example Usage
```vex
@cpu @rate(5) @job(Priority=High) {
    # This block is automatically offloaded to the UE Task Graph
    # Results are synced back to the Mass fragment via AutoRTFM
    @behavior_id = find_optimal_path(@position, @target);
}

@cpu @async {
    # Suspends execution until the GPU readback signals completion
    let result = wait_on_gpu(@tracking_id);
    if (result.collision) {
        set @velocity = reflect(@velocity, result.normal);
    }
}
```

## 2. Supporting Requirements

### A. Affinity-Aware Residency
The VEX **Residency Model** is extended to support **Thread Affinity**:
- **`GameThreadOnly`**: Symbols that access UObjects or certain engine globals.
- **`WorkerSafe`**: POD data (Mass Fragments) reflected via `TReflectionTraits`.
- **`Atomic`**: Shared resources requiring locked access (automatically handled by `AutoRTFM`).

### B. Task Thunk Generation
The VEX compiler lowers these blocks into **Task Thunks**:
1. **Capture**: Identifies all symbols used in the block (Read/Write sets).
2. **Marshal**: Packages symbols into a `TTaskMetadata<T>` struct for off-thread access.
3. **Dispatch**: Generates the C++/Verse code to launch the task via `UFlightVexTaskSubsystem`.
4. **Sync**: Uses `UE_AUTORTFM_ONCOMMIT` to ensure task results are only applied if the simulation tick is still valid.

## 3. Implementation Status

### Phase 1: Parser & Metadata (Complete)
- [x] Recognize `@job`, `@thread`, and `@async` tokens.
- [x] Implement `FVexTaskDescriptor` in `FVexProgramAst`.
- [x] Support for Thread Affinity validation in `PerformSemanticValidation`.

### Phase 2: Task Metadata & Capture Validation (Complete)
- [x] Automatic identification of "Read" and "Write" symbol capture groups.
- [x] Schema enforcement for Read-Only captures in concurrent blocks.

### Phase 3: The `@job` Thunk Generator (Complete)
- [x] Implemented `UFlightVexTaskSubsystem` for `UE::Tasks` integration.
- [x] Multi-threaded dispatch in `UFlightVexBehaviorProcessor`.
- [x] Transactional wrapping of background jobs via `AutoRTFM`.

### Phase 4: Verse Leniency Bridge (`@async`) (Complete)
- [x] Automatic injection of `<async>` effects in `LowerToVerse`.
- [x] Native thunk for `wait_on_gpu` returning `VPlaceholder`.
- [x] Support for task suspension/resumption in the `UFlightVerseSubsystem`.

## 4. Dependencies
- **`UnrealEngine::Tasks`**: For high-performance task scheduling.
- **`VerseVM`**: For native leniency and placeholder-based async execution.
- **`AutoRTFM`**: For transactional safety across thread boundaries.
- **`IFlightAsyncExecutor`**: For unified scheduling across different hardware backends.
