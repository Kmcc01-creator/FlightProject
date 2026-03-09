# VEX SIMD & HLSL Intrinsics Reference

This document codifies the architectural principles and low-level optimization strategies for the **VEX DSL** across both **CPU (SIMD)** and **GPU (HLSL/Vulkan)** execution paths. It serves as the foundational mandate for "Tier 1" performance engineering in the FlightProject.

## 1. Unifying Architectural Principles

To achieve parity between the `FVexSimdExecutor` (CPU) and our HLSL Mega-Kernels (GPU), we adhere to these four pillars of modern high-performance graphics and simulation.

| Principle | Technical Implementation | Goal |
| :--- | :--- | :--- |
| **Explicit Visibility** | `globallycoherent`, `precise`, `memory_barrier` | Prevent race conditions during high-speed parallel Read/Write cycles. |
| **Workgroup Alignment** | `[numthreads(64,1,1)]` & `Wave-level` logic | Map VEX logic directly to GPU hardware units (Warps/Wavefronts). |
| **Monolithic State** | Specialization Constants (`vk::constant_id`) | Optimize out branches/loops at pipeline creation time based on VEX metadata. |
| **Bindless Indirection** | HLSL 2021 Unbounded Arrays | Use universal resource heaps for Lattices/Fields to avoid re-binding overhead. |

---

## 2. VEX Execution Tiers & Optimization Paths

The VEX compiler uses these tiers to select the most aggressive hardware path available.

### Tier 1: Literal (Pure Math)
*   **CPU Path**: Vectorized 4-wide execution using `FVector4f` and `immintrin.h` (via `FVexSimdExecutor`).
*   **GPU Path**: Single-register scalarization and branchless flattening.
*   **Constraint**: No branching, no loops, no async calls.

### Tier 2: DFA (Deterministic Branches)
*   **CPU Path**: Standard interpreter with jump-table optimization.
*   **GPU Path**: Wave-coherent branching using `[branch]` attributes to skip divergence.

### Tier 3: Full (Verse VM / Async)
*   **CPU Path**: Verse VM procedure execution with support for `<async>` and `<transacts>`.
*   **GPU Path**: Multi-pass compute with synchronization points.

---

## 3. High-Impact HLSL Intrinsics for VEX

The VEX compiler lowers "Pure Math" patterns into these specialized intrinsics to maximize hardware occupancy.

### Wave-Level Intrinsics (GPU SIMD)
*   **`WaveReadLaneFirst(T value)`**: **Scalarization.** Moves uniform data from Vector Registers (VGPR) to Scalar Registers (SGPR), freeing up GPU resources for more threads.
*   **`WaveActiveSum(T value)`**: Ultra-fast reduction for neighborhood density or centroid calculation without memory atomics.
*   **`WaveIsFirstLane()`**: Ensures only one thread per wave performs high-cost operations (like global counter increments).
*   **`NonUniformResourceIndex(uint index)`**: Required for indexing into Bindless Lattices/Fields when the index varies across the wave.

### Optimized Math & Logic
*   **`fma(a, b, c)`**: Fused Multiply-Add for maximum precision and performance in polynomial approximations (sin, cos, exp).
*   **`msad4(ref, src, acc)`**: Masked Sum of Absolute Differences—used for fast pattern matching in AI/Navigation templates.
*   **`asfloat()` / `asuint()`**: Bit-casting for low-level data packing/unpacking without type conversion overhead.

---

## 4. Schema & Alignment Requirements

For SIMD and HLSL optimizations to be effective, the underlying data structures must satisfy strict hardware alignment.

*   **16-Byte Alignment**: All `@cpu @gpu` shared symbols (e.g., `@position`, `@velocity`) MUST be 16-byte aligned.
    *   **CPU Impact**: Allows `_mm_load_ps` (aligned) instead of `_mm_loadu_ps` (unaligned), doubling bandwidth.
    *   **GPU Impact**: Enables 128-bit `Load4` operations from StructuredBuffers.
*   **Data Layout (SoA vs. AoS)**:
    *   VEX prefers **Structure of Arrays (SoA)** for bulk processing to avoid memory padding gaps.
    *   The `FVexSimdExecutor` assumes a "packed" memory layout to maximize cache line utilization.

---

## 5. Performance Best Practices

1.  **Prefer Scalarization**: Always use `WaveReadLaneFirst` for constants passed via Push Constants or global parameters.
2.  **Avoid Subgroup Divergence**: Ensure that `if` conditions in VEX are "Wave-Coherent" whenever possible to prevent the GPU from splitting the execution path.
3.  **Use Specialized Constant Buffers**: Map VEX `@rate` and `@frame` metadata to **Push Constants** (Vulkan) for O(0) access latency.
4.  **Hardware-Native Math**: Use native `sin()` and `cos()` on the GPU, but utilize the `libm` SIMD stubs or `FMath` on the CPU to maintain simulation consistency.

---

## 6. Verification Pipeline

*   **Precision Guard**: Validate floating-point results between `FVexSimdExecutor` (CPU) and the HLSL kernel to identify precision de-syncs in `pow()` or `exp()` operations.
*   **Occupancy Report**: The VEX compiler should emit a "Register Pressure Report" for HLSL kernels, identifying which scripts might reduce GPU occupancy due to high VGPR usage.
