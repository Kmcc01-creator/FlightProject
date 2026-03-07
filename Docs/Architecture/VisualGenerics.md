# Visual Generics & Data-Driven Light

This document outlines the philosophy of treating simulation data as a generic field for visual interpretation in FlightProject.

## 1. The Houdini-FX Vantage Point
We do not simulate "Drones." We simulate a **High-Density Spatio-Temporal Data Field**.
- **The Point**: A coordinate in 3D space $(x, y, z)$.
- **The Vector**: A directional intent $(vx, vy, vz)$.
- **The Scalar**: A normalized energy value $(w)$.

The visual representation is a "Decoder" that maps this raw data into the photon budget of the engine.

## 2. Light as Data (Lumen Integration)
With 500,000 entities, traditional primitives (meshes, actors) fail. We utilize **Material-Emissive Global Illumination**:
- **Source**: 500k unlit particles.
- **Medium**: UE5 Lumen.
- **Outcome**: The swarm is not just "visible"; it is a dynamic light source that bounces off the environment, providing high-impact gameplay feedback (e.g., an "Infection" ripple illuminates a dark tunnel in neon green).

## 3. The Visual Pipe
The transformation from simulation to screen follows a functional pipe:
`FDroidState` $\to$ `Niagara Data Interface` $\to$ `HLSL Interpolator` $\to$ `Emissive Material`.

---

# Editor Workflow: Building the "Gauntlet" Swarm

To implement this in the Unreal Editor, follow these steps:

### Step 1: The Generic Material (`M_DataNode`)
1. Create a new Material. Set **Material Domain** to `Surface` and **Shading Model** to `Unlit`.
2. Enable **Used with Niagara Mesh Particles**.
3. Create a **Dynamic Parameter** node.
   - Index 0 (Energy): Maps to our `Shield` integrity.
4. Logic: `Lerp(BaseColor, AlertColor, 1.0 - Energy) * Intensity`.
5. Connect this to the **Emissive Color** input.

### Step 2: The Niagara System (`NS_DataSwarm`)
1. Create a Niagara System with a **GPU Compute Sim**.
2. Add our **Swarm Data Interface** to the System User Parameters.
3. In **Particle Update**, add a **Custom HLSL** module:
   ```hlsl
   float3 RawPos = Swarm.GetDroidPosition(ExecutionIndex);
   float3 RawVel = Swarm.GetDroidVelocity(ExecutionIndex);
   float  Shield = Swarm.GetDroidShield(ExecutionIndex);
   
   // Interpolation logic
   Particles.Position = RawPos + (RawVel * Niagara_DeltaTime);
   Particles.DynamicMaterialParameter0 = Shield;
   ```
4. Set the **Mesh Renderer** to use a simple sphere and our `M_DataNode` material.

### Step 3: Reactive Debugging UI
To avoid restarting the editor, we use the `FReflectedWidgetGenerator` to build a UI that docks in the editor. This UI reflects our `FSwarmSimulationParams` automatically.
