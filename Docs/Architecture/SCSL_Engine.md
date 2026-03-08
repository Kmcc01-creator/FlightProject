# SCSL Engine: Field-Based Simulation & Rendering

This document outlines the architecture of the **SCSL (Structures, Clouds, Swarms, Light)** engine, a high-performance "shadow engine" built within FlightProject. It leverages C++23 reflection, monadic functional paradigms, and a bespoke VEX-inspired DSL to achieve massive-scale GPU simulation.

## 1. The SCSL Paradigm
Rather than treating entities as individual actors, SCSL treats the world as a set of interacting **Data Fields** and **Lagrangian Points**.

| Domain | Representation | Role |
| :--- | :--- | :--- |
| **Structures (S)** | Signed Distance Fields (SDF) | Physical constraints, collisions, and occlusion. |
| **Clouds (C)** | 3D Density Volumes (R16F) | Volumetric persistence, trails, and atmospheric effects. |
| **Swarms (S)** | Structured Buffers (FDroidState) | Discrete agents that sample and manipulate all fields. |
| **Light (L)** | Radiance Lattice (RGBA16F) | 3D cellular propagation of emissive energy. |

---

## 2. VEX DSL & The Pipe Operator (`|`)

The VEX DSL is a C-style scripting language that lowers to both **HLSL** (for GPU simulation) and **Verse** (for gameplay logic).

### 2.1 Reflection-Driven Symbols
VEX symbols (e.g., `@position`, `@shield`) are automatically synchronized with C++ memory layouts via the reflection system.
*   **Source of Truth**: `FDroidState` in C++.
*   **Mechanism**: `GenerateVexSymbolsFromStruct<T>` iterates over reflected attributes to build the VEX symbol map.

### 2.2 Functional Pipe Operator
The pipe operator (`|`) implements the functional idiom `LHS | RHS(args) -> RHS(LHS, args)`. This allows for composable stream transforms:
```vex
// Sample SDF, calculate gradient, and apply as a force
@velocity += @position | sample_sdf() | gradient() * -50.0;

// Inject light into the lattice at the entity position
inject_light(@position, {0.1, 0.5, 1.0} * @shield);
```

---

## 3. Optimization Engine

To minimize GPU overhead, the VEX compiler performs an **Optimization Phase** on the CPU before lowering to HLSL.

### 3.1 Iterative AST Traversal
Modeled after the `m2` compiler framework, we use a stack-based post-order traversal (`TransformIterative`) to bypass C++ recursion depth limits during optimization.

### 3.2 Implemented Patterns
1.  **Constant Folding**: Arithmetic between literals (e.g., `1.0 + 2.0`) is evaluated at compile-time.
2.  **Identity Simplification**: Redundant operations (e.g., `x * 1.0`, `x + 0.0`) are pruned from the AST.
3.  **Lattice Hoisting (Planned)**: Automatic caching of expensive 3D texture samples.

---

## 4. Monadic RDG Orchestration

The simulation is executed as a 12-pass Render Dependency Graph (RDG) pipeline, orchestrated via a monadic `TResult` chain in `UFlightSwarmSubsystem`.

### The 12-Pass Sequence:
1.  **Clear Grid**: Reset spatial hash.
2.  **Build Grid**: Bin swarm entities into spatial cells.
3.  **Density Pass**: Calculate SPH density/pressure.
4.  **Force Pass**: Execute **VEX Logic** (Force accumulation + SCSL sampling).
5.  **Predictive Pass**: Multi-step rollout for collision avoidance.
6.  **Integration Pass**: Update swarm positions and velocities.
7.  **Light Injection**: Splat emissive swarm data into the 3D UINT buffer.
8.  **Light Convert**: Normalize UINT injection to FLOAT4 lattice.
9.  **Light Propagate**: 3D cellular diffusion of radiance.
10. **Cloud Injection**: Splat swarm density into 3D UINT buffer.
11. **Cloud Sim**: Perform diffusion and dissipation on the density field.
12. **Atomic Splat Resolve**: Render the swarm field to `SceneColor` using order-independent transparency.

---

## 5. Rendering: Atomic Field Splatting

Bypassing Niagara, the SCSL engine uses **Atomic Accumulation** to render 500k+ entities without flickering.
*   **Technique**: Entities use `InterlockedAdd` to write to fixed-point `R32_UINT` textures.
*   **Visuals**: The resolve pass converts fixed-point data to HDR color, sampling the **Light Lattice** and **Cloud Field** to produce a unified volumetric glow.
*   **Compatibility**: Injected at the `Tonemap` pass via `FSceneViewExtension`, ensuring full support for Unreal's Bloom, Exposure, and HDR pipeline.
