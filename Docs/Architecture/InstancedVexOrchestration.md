# Instanced VEX Orchestration & Sparse Field Management

This document outlines the scaling strategy for the SCSL engine, moving from a global singleton simulation to a distributed, instanced management layer capable of handling diverse behaviors and sparse environments.

## 1. The Multi-Lattice Paradigm (Sparse Virtual Fields)
Global 3D textures do not scale for open-world environments. To achieve AAA-level scale, the SCSL engine transitions to a **Sparse Field** model.

*   **Local Field Instances**: Instead of one world-space lattice, the engine manages a pool of high-resolution local volumes ($32^3$ or $64^3$).
*   **Binding Strategy**: Resources are bound as a `Texture3DArray`. VEX scripts select the appropriate lattice instance based on an entity's `@LatticeID` or spatial proximity.
*   **Status**: [COMPLETE] Shader infrastructure and VEX lowering support `Texture3DArray`.

---

## 2. VEX as an Instance-Scripting Layer
The engine treats VEX not as a static shader, but as a **Dynamic Component Language**. Entities in the swarm are assigned to different **Behavior Classes**.

### 2.1 Behavior Multi-Dispatch
The VEX compiler generates a **Mega-Kernel** that encapsulates multiple scripts using a `switch(EntityClassID)` statement.
*   **Attribute Hoisting**: Shared symbols (e.g., `@position`) are loaded exactly once into registers before dispatching specific behavior logic, minimizing bandwidth.

### 2.2 Entity Sorting (Refined Radix Sort)
To maintain GPU performance and avoid execution divergence, the engine performs a **4-bit Parallel Radix Sort** every frame.
*   **Algorithm**: A 16-bin binning strategy ensures all entities of the same behavior class are contiguous in memory.
*   **Performance**: This ensures that threads in a warp/wavefront execute identical logic branch-free, maximizing throughput for massive multi-agent counts.
*   **Status**: [COMPLETE] Three-pass parallel sort (Count, Scan, Scatter) integrated into the monadic RDG pipeline.

---

## 3. The Event Topology Field (The Unexpected)
To handle dynamic gameplay events (explosions, triggers, player actions), we introduce the **Event Field (E)**.

*   **Transient Injection**: C++ or Verse pushes events into a circular `StructuredBuffer<FTransientEvent>`.
*   **VEX Interaction**: Drones sample this buffer via the `sample_events()` built-in, allowing VEX logic to react to "the unexpected" with sub-millisecond latency.
*   **Status**: [IN PROGRESS] Implementation of the C++ bridge and VEX event sampling.

---

## 4. Implementation Roadmap
1.  [x] **Resource Arrays**: Transition from `Texture3D` to `Texture3DArray` for Lattices and Clouds.
2.  [x] **Mega-Kernel Compiler**: Update VEX compiler to support multi-script AST merging and hoisting.
3.  [x] **GPU Sorter**: Implement high-speed 4-bit entity sorting by Behavior ID.
4.  [IN PROGRESS] **Event Bridge**: Implement the C++ to GPU event injection pipeline.
