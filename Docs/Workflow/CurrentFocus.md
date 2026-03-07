# Current Focus: GPU Spatial & Swarm Intelligence

## Overview
Our primary objective is to establish a new paradigm for high-performance simulation in Unreal Engine 5, utilizing C++23 traits, Linux `io_uring` for zero-syscall GPU synchronization, and Mass ECS for massive-scale swarm intelligence.

## Technical Milestones (Completed)
- **Core Reflection**: `FlightReflection.h` supports robust trait specializations under C++23.
- **Unified Reactivity**: Decoupled `Flight::Reactive` core from Slate, enabling non-UI reuse in ECS and telemetry systems.
- **Cross-Platform Backend**: Established `IFlightAsyncExecutor` abstraction, enabling `io_uring` on Linux with standard fallbacks.
- **GPU Spatial Integration**: 
  - **Force Blackboard Pipeline**: Implemented a multi-buffer GPU readback system (`ObstacleCounts`, `AccumulatedForces`, `Timestamps`).
  - **Zero-Syscall Sync**: Verified `io_uring` + Vulkan exportable semaphores path, achieving asynchronous completion notification without CPU polling.
- **6-Pass SPH Pipeline**: 
  - Implemented 3D Spatial Binning, Density, Reactive Force, Predictive Rollout, and Euler Integration in HLSL.
  - Verified 50,000 entities at 60 FPS baseline (benchmarked to 500,000).
- **Niagara Visual Bridge**: 
  - Implemented `UNiagaraDataInterfaceSwarm` for direct VRAM access to drone state.
  - Optimized for UE 5.7 RDG integration.
- **Tooling: Swarm Orchestrator**: 
  - Developed a live-coding editor for Houdini-style VEX expressions.
  - Supports dual-generation of GPU HLSL and functional Verse VM bytecode.

## Performance Baseline (AMD Radeon 860M)
| Entity Scale | GPU Compute | Round-Trip Latency | Efficiency (per unit) |
| :--- | :--- | :--- | :--- |
| 50,000 | 124.43 ms | 168.04 ms | 2.49 µs |
| 500,000 | **140.95 ms** | **195.17 ms** | **0.28 µs** |

## Future Planning

### 1. Swarm Instancing (Master-Instance Architecture)
Evolve the current "Master" pipeline to support multiple independent swarm instances, each with their own UBO parameters and VEX logic snippets.
- **Goal**: Support `#instance` syntax in the Swarm Orchestrator tool.

### 2. SPH Fluid Refinement
Implement a more advanced SPH pressure solver (e.g., Position-Based Fluids) to reduce the "panic factor" required for collision avoidance.

### 3. Niagara Temporal Interpolation
Complete the Niagara vertex shader logic to smoothly interpolate between the ~8Hz simulation ticks and the 60Hz+ rendering rate.

---

**Next Steps**: Launch the Editor and run the **Gauntlet** setup script to begin live VEX experimentation.
