# SPH Swarm Architecture (3D)

This document outlines the Smoothed Particle Hydrodynamics (SPH) pipeline for massive-scale swarm intelligence (500k+ entities) in FlightProject.

## 1. Tactical Data Structures

### FDroidState (Persistent GPU Buffer)
Instead of transient Mass fragments, the "Source of Truth" lives in a long-lived GPU structured buffer.
- `float3 Position`: World-space coordinates.
- `float  Shield`: 1.0 (Full) to 0.0 (Popped). Drives Niagara visuals and survival logic.
- `float3 Velocity`: Current movement vector.
- `uint   Status`: Bitmask for gameplay states (Infected, Overloaded, TargetLocked).

### Command UBO (CPU -> GPU)
A tiny (<1KB) buffer sent every frame to drive the simulation.
- `float3 PlayerPos`: For proximity reactions.
- `float  InteractionRadius`: Distance for "Splash" events.
- `uint   CommandType`: 0=Idle, 1=Repel, 2=Infect, 3=Stasis.
- `float  DeltaTime`: Scaled simulation time.

---

## 2. The 5-Pass GPU Pipeline

### Pass 0: Spatial Binning (3D Uniform Grid)
- **Grid**: A 3D array of pointers to the `FDroidState` buffer.
- **Complexity**: $O(N)$.
- **Logic**: Maps drones to 3D cells based on their position to enable $O(1)$ neighbor and obstacle lookups.

### Pass 1: Obstacle Perception (SDF Sampling)
To handle complex geometries (Torus, Rectangles, Meshes), we use **Signed Distance Fields (SDF)**.
- **Algorithm**: Drones sample a Global SDF buffer.
- **Math**: 
  - `Dist = SampleSDF(DronePos)`
  - `Normal = GradientSDF(DronePos)`
- **Avoidance Force**: If `Dist < Threshold`, $F_{avoid} = Normal \times (1.0 / Dist^2)$. 
- **Benefit**: A drone inside a Torus naturally calculates the gradient away from the inner walls, allowing it to "fly through the hole" or avoid the rim with identical logic.

### Pass 2: SPH Density & Pressure
- **Density ($\rho_i$)**: Sum of neighbor influence using Poly6 Kernel.
- **Pressure ($P_i$)**: $k(\rho_i - \rho_0)$.
- **Output**: `DensityPressureBuffer`.

### Pass 3: SPH Force & Gameplay Events
- **Fluid Forces**: Pressure gradient + Viscosity smoothing.
- **Splash Damage**: If `dist(Drone, Player) < InteractionRadius` AND `Command=Infect`, apply damage to `Shield`.
- **Chained Overload**: If `Shield < 0`, write a "Shockwave" signal to the 3D Grid for neighbors to read next pass.
- **Output**: Write to **Force Blackboard**.

### Pass 4: Integration & Boundary
- Updates `FDroidState` (Position/Velocity) based on the Blackboard summation.

---

## 3. The "Gauntlet" Testing Playground

To verify the agility of this system, we will establish a dedicated test map:
- **Geometry Stress**: A series of large obstacles (A hollow Torus, a winding Rectangular Tunnel, and a field of Spheres).
- **Collision Course**: 100,000 drones are spawned on one side and commanded to fly through the obstacles to a Beacon.
- **Success Criteria**: Drones should fluidly navigate the Torus hole and tunnel without "clumping" or tunneling through walls, maintained by the SPH pressure forces.

## 4. Visual Integration (Niagara)
- **Direct Access**: Niagara reads the `FDroidState` buffer directly.
- **Material Logic**:
  - `Shield > 0.5`: Blue Fresnel (Stable).
  - `Shield < 0.2`: Flickering Orange/Red (Critical).
  - `Status: Infected`: Green Glow.
- **Temporal Interpolation**: Niagara calculates sub-frame positions using `Velocity` to maintain 60 FPS visual smoothness regardless of the SPH tick rate.
