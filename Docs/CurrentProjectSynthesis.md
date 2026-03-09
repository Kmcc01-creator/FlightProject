# FlightProject: Current Project Synthesis (March 2026)

## 1. Architectural Vision
FlightProject is a **Target-Aware Orchestration Platform** built on top of Unreal Engine 5.7 and Mass ECS. It moves away from imperative C++ simulation logic toward a declarative, schema-driven paradigm where behaviors are authored in the **VEX DSL** and executed across tiered runtimes (CPU/Verse and GPU/HLSL).

## 2. The Tiered Execution Model
Simulation logic is partitioned at compile-time based on residency directives:

| Tier | Runtime | Directive | Characteristics |
| :--- | :--- | :--- | :--- |
| **Tier 1 (SIMD)** | C++/Mass | `@simd` | Pure math, zero-branching, O(1) per agent. |
| **Tier 2 (DFA)** | HLSL Compute | `@gpu` | High-frequency spatial simulation, SPH, Boids. |
| **Tier 3 (Full)** | Verse VM | `@cpu` | Transactional logic (AutoRTFM), async behaviors, decision trees. |

## 3. Generic "Pipes and Control" Abstraction
To ensure the system remains general-purpose and extensible, we have refined the communication model into two primary "Pipes":

### A. The Control Pipe (Host-to-Device)
*   **Concept:** **Global State Envelopes**.
*   **Mechanism:** Uniform Buffer Objects (UBOs) or Constants.
*   **Narrative:** This pipe broadcasts "Directives" and "Environmental Context" from the Host (CPU) to the Execution environment (GPU). It sets the "rules of the world" for the current frame (e.g., Global Gravity, Scan Radii, Simulation Delta).
*   **Refinement:** Moving away from `FSwarmGlobalCommand` toward a generic **`FSimulationContext`** that can be extended by any module.

### C. Directional Domain Pipes (Handshaking)
To formalize the boundary between Host (CPU/Verse) and Device (GPU/Compute), VEX utilizes directional pipe operators that encode residency intent:

*   **Inject Pipe (`<|`):** *Host-to-Device*. Used to push parameters or state from the CPU into a GPU-resident symbol.
    *   *Syntax:* `TargetGpuSymbol <| SourceCpuExpression;`
    *   *Example:* `@gpu { @velocity <| normalize(WindVector); }`
*   **Extract Pipe (`|>`):** *Device-to-Host*. Used to pull results or observations from the GPU back into the CPU logic layer.
    *   *Syntax:* `SourceGpuExpression |> TargetCpuSymbol;`
    *   *Example:* `@cpu { float hit_count = @hit_dist |> count_hits(); }`

These operators serve as explicit "handshake" points, allowing the compiler to insert necessary synchronization (like `io_uring` readbacks) and perform strict residency validation.

## 4. Data Residency & Reflection
The **Entity State** (formerly `FDroidState`) is the shared memory contract between all tiers.
*   **Shared Symbols:** Symbols like `@position` and `@velocity` are mirrored across CPU and GPU memory.
*   **Local Symbols:** Symbols tagged as `GpuOnly` or `CpuOnly` reside strictly within their respective domains to optimize bandwidth.
*   **Dirty Tracking:** Sparse Blitting ensures that only "Dirty" entity states are synchronized through the pipe, maintaining O(N) performance where N is the number of changed entities, not total entities.

## 5. Summary of System Mechanisms
1.  **Orchestration:** VEX Compiler (Parser -> AST -> Optimizer).
2.  **Simulation:** Mass ECS (Entity Management) + RDG (Compute Dispatch).
3.  **Safety:** Verse VM + AutoRTFM (Transactional State Rollback).
4.  **Hardware Bridge:** `io_uring` (Zero-syscall, non-blocking GPU sync on Linux).
5.  **Validation:** Schema-driven Integration Tests (Vertical Slice).

---
*This document serves as the high-level technical reference for the current implementation phase.*
