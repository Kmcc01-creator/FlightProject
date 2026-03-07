# Swarm Instancing: Master & Variant Architecture

This document defines the "Houdini-style" instancing architecture for the Flight Swarm simulation.

## 1. The "Master" Pipeline
The 6-pass GPU simulation (SPH + Predictive) acts as the **Master Material**. It defines the fundamental mathematics of the universe:
- **Spatial Resolution**: 3D Linked-List Grid.
- **Fluid Solver**: Navier-Stokes (SPH).
- **Temporal Insight**: Predictive Rollout.

## 2. The "Swarm Instance"
A Swarm Instance is a unique collection of data and parameters that uses the Master Pipeline for its execution.

### Components of an Instance:
1.  **State Buffer**: A unique `FDroidState` structured buffer in VRAM.
2.  **Command UBO**: A unique parameter set (Smoothing Radius, Viscosity, etc.).
3.  **VEX Snippet**: A custom logic injection that runs during the Force pass.

## 3. Programmatic Instancing (VEX)
Instances are defined via text in the **Swarm Orchestrator**, avoiding "Details Panel" fatigue.

```c++
// Example Orchestrator Definition
#instance "Attacker_Swarm" {
    @count = 100000;
    @color = red;
    @vex = { @velocity += normalize(@player_pos - @position) * 50; }
}
```

## 4. Multi-Dispatch Execution
The `UFlightSwarmSubsystem` will evolve to iterate over all active instances during `TickSimulation`:

```cpp
for (auto& Instance : ActiveSwarms) {
    // 1. Pack UBO for this instance
    // 2. Dispatch 6-pass Master Pipeline using this instance's buffers
    // 3. Signal io_uring for completion
}
```

## 5. Visual Diversification
Since the `Niagara Data Interface` is bound per emitter, we can have multiple Niagara Systems in the same level, each looking at a different Swarm Instance buffer, creating a diverse visual ecosystem from a single mathematical engine.
