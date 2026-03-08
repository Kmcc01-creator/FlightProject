# VEX-Verse Target-Aware Orchestration

This document defines the technical architecture for bridging the VEX DSL with the Unreal Verse Virtual Machine and GPU HLSL kernels.

## 1. Unified Source, Dual Targets

The VEX language in FlightProject serves as a single source of truth for both high-frequency simulation (GPU) and high-level behavioral logic (CPU).

### The Residency Model
VEX symbols and blocks are tagged with **Residency** metadata:
- **`@gpu`**: Logic restricted to HLSL compute shaders. Used for SPH, collision primitives, and field injection.
- **`@cpu`**: Logic restricted to the Verse VM. Used for navigation planning, state machines, and gameplay events.
- **`Shared`**: Symbols (like `@position` or `@velocity`) that exist in both domains and are synced via the orchestration layer.

## 2. The Verse VM Pipeline

Verse is used as the CPU execution target for VEX logic due to its transactional safety and native engine integration.

### Native Component Projection
C++ Mass fragments are projected into the Verse VM using the `VNI` (Verse Native Interface) bridge:
1. **Reflection**: `TReflectionTraits` identify fields tagged with `Attr::VersePackage`.
2. **Descriptors**: `UFlightVerseSubsystem` builds `FVniTypeDesc` at runtime.
3. **Binding**: Mass fragment memory is bound directly to Verse VM registers for zero-copy interaction.

### Transactional Safety (AutoRTFM)
Every Verse behavior execution is wrapped in an `AutoRTFM::Transact` block.
- **Speculative Execution**: Verse logic can "try" a move (e.g., dodging an obstacle).
- **Rollback**: If a language failure occurs (e.g., a `decides` function fails) or a GPU readback confirms a collision, the `FDroidState` memory is automatically restored to its pre-transaction state.

3. Declarative Scheduling

Timing is controlled directly in VEX using the @rate directive:
- @rate(15Hz): Throttles execution to approx. 15 updates per second.
- @rate(5): Executes every 5th simulation frame.

The UFlightVexBehaviorProcessor (Mass ECS) reads this metadata to optimize CPU utilization by skipping entity chunks that are not scheduled for the current @frame.

## 4. Sympathetic Pipeline Optimization

The orchestrator minimizes hardware overhead by treating data movement and sorting as sympathetic, gated operations.

### Sparse Blitting
- **Dirty Tracking**: Entities modified by Verse are flagged via `FFlightVexDirtyFragment`.
- **Targeted Upload**: Only dirty memory ranges are blitted to the GPU via `UpdateDroidState_Sparse`.

### Sort Gating
- **Normalization Barrier**: The GPU Radix Sort acts as a checkpoint that only executes if simulation logic (like BehaviorID changes) requires a memory re-normalization.
- **Warp Coherency**: Ensuring that execution remains contiguous in VRAM without redundant frame-by-frame sorting costs.

## 5. Hardware Synchronization
...

The `io_uring` bridge provides the low-latency notification path for GPU-to-CPU data flow:
1. **GPU Signals**: Completion exported via `sync_fd`.
2. **io_uring Poll**: `FIoUringExecutor` detects completion with zero syscalls in the steady state.
3. **Reactive Trigger**: Triggers a `TReactiveValue<bool>` which "wakes up" the relevant Verse behaviors to process new perception data.
