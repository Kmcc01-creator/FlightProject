# Historical VEX & Verse Runtime Concepts

Status: historical reference.
This document consolidates exploratory concepts, mental models, and early implementation notes for the VEX and Verse integration. For the current canonical architecture, see:
- [CurrentProjectVision.md](CurrentProjectVision.md)
- [WorldExecutionModel.md](WorldExecutionModel.md)
- [OrchestrationSubsystem.md](OrchestrationSubsystem.md)
- [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md)

---

## 1. Simulation Mindset & Lifecycle (Melt/Freeze)

Early designs focused on harmonizing high-frequency simulation (VEX) with transactional logic (Verse).

### Melt/Freeze Lifecycle
- **Melt**: Create a local, mutable copy of simulation state (e.g., `FDroidState`) for a logic transaction.
- **Freeze**: Mark the mutated state as immutable and validated before committing it back to the simulation.
- **Speculative Failure**: Utilizing Verse's failure contexts to "try" logic paths (e.g., dodging) and automatically rolling back via **AutoRTFM** if a path fails.

### Asynchronous Perception
- **io_uring Bridge**: Using zero-syscall polling to detect GPU work completion (e.g., spatial hash or collision readbacks).
- **Suspension/Wakeup**: Verse behaviors `Await` a signal (CQE) from the bridge, suspending their task and allowing other work to proceed without blocking threads.

---

## 2. Target-Aware Orchestration

VEX was envisioned as a unified source for dual targets (@gpu and @cpu).

- **Unified Source**: A single VEX script declares logic for both HLSL (SIMD/GPU) and Verse (Logic/CPU).
- **Residency Directives**: `@gpu` for compute shaders, `@cpu` for Verse logic, and `Shared` for mirrored symbols synced via orchestration.
- **Sparse Blitting**: Tracking "dirty" entities to minimize PCIe bandwidth by only uploading modified state ranges.

---

## 3. Concurrency & Tasking

Exploratory directives for lifting Unreal's task models into the DSL.

- **Directives**:
  - `@job`: Offloaded to `UE::Tasks` for parallel batch math.
  - `@thread`: Dedicated threads for long-running state evaluation.
  - `@async`: Suspendable Verse logic waiting for external events.
- **Task Thunks**: Automatically capturing Read/Write symbol sets to marshal data safely across thread boundaries.

---

## 4. Instancing & Scaling (SCSL-Era)

Scaling strategy for the "Structures, Clouds, Swarms, Light" (SCSL) engine.

- **Multi-Lattice Paradigm**: Moving from a global 3D texture to a `Texture3DArray` of local high-resolution volumes.
- **Mega-Kernel Dispatch**: Merging multiple VEX scripts into a single kernel using `switch(EntityClassID)` with hoisted shared attributes.
- **4-bit Radix Sort**: Maintaining GPU performance by ensuring entities of the same behavior class are contiguous in memory.
- **Event Topology**: A circular `StructuredBuffer` for injecting transient gameplay events (explosions, etc.) into the simulation.
