# DevNote 001: System Architecture & Data Flow (Dec 2025)

**Date:** Friday, December 26, 2025
**Status:** Post-Migration to UE 5.7 / Mass Architecture

## 1. Project Structure

We have transitioned from a monolithic Actor-based design to a modular **Game Feature** architecture.

### Modules
*   **`FlightProject` (Primary Module):**
    *   **Responsibility:** Core flight physics, Player Controller, Spatial Layout (Navigation Graph), and Shared Types.
    *   **Key Classes:** `AFlightVehiclePawn`, `AFlightWaypointPath`, `UFlightSpatialLayoutDirector`.
    *   **Location:** `Source/FlightProject/`
*   **`SwarmEncounter` (Game Feature Plugin):**
    *   **Responsibility:** Autonomous swarm logic, Spawning, Mass Entity definitions.
    *   **Key Classes:** `UFlightSwarmSpawnerSubsystem`, `UFlightSwarmTrait`.
    *   **Location:** `Plugins/GameFeatures/SwarmEncounter/`

### Dependencies
*   **MassEntity / MassGameplay:** The backbone of our simulation.
*   **StateTree:** Used for AI decision making (Integrated via Mass Traits).
*   **StructUtils:** For shared fragment storage.

---

## 2. The "Swarm" Data Flow

We moved from `AFlightAIPawn::Tick()` to a data-oriented pipeline.

### Step 1: Configuration (Editor)
1.  **Asset:** `DA_SwarmDroneConfig` (MassEntityConfigAsset).
2.  **Trait:** `UFlightSwarmTrait` (Adds fragments: `Transform`, `PathFollow`, `Visual`).
3.  **Input:** User defines `DroneCount` and `Speed` in `FlightDataSubsystem` (or directly on the Spawner).

### Step 2: Spawning (Runtime)
1.  **Trigger:** `UFlightSwarmSpawnerSubsystem::SpawnInitialSwarm()` called (via GameMode or Event).
2.  **Factory:** Calls `MassSpawnerSubsystem::SpawnEntities(...)` using the Template ID from `DA_SwarmDroneConfig`.
3.  **Initialization:** Subsystem manually iterates spawned entities to set initial `CurrentDistance` on `FFlightPathFollowFragment` (distributing them along the spline).

### Step 3: Simulation Loop (Per Frame)
**Processor:** `UFlightPathFollowProcessor` (Phase: `PrePhysics`)
1.  **Query:** "Give me all entities with `PathFollowFragment` + `TransformFragment`."
2.  **Logic:** 
    *   `NewDistance = CurrentDistance + (Speed * DeltaTime)`
    *   `SampleSpline(NewDistance)` -> Get Location/Rotation/Tangent.
    *   Write to `TransformFragment`.

### Step 4: Visualization (Post Physics)
**Processor:** `UFlightTransformSyncProcessor` (Phase: `PostPhysics`)
*   **Conditional:** Only runs if entity has `FMassActorFragment` (LOD 0 / Near Camera).
*   **Logic:** Copies `TransformFragment` -> `AActor::SetActorLocation()`.
*   *Note:* Distant entities are rendered via ISMs (Instanced Static Meshes) handled automatically by Mass Representation, bypassing this processor.

---

## 3. Automation & Tooling

To support this solo-dev workflow, we established a "Scripting Glue" layer.

*   **Python:** Located in `Content/Python/FlightProject/`. Used for creating Assets and setting up Scene Layouts.
*   **Editor Utility Widgets:** "Flight Tools" panel calls Python scripts to avoid manual repetitive tasks.
*   **Build Scripts:** `./Scripts/build_targets.sh` handles strictness flags and module linking for Linux.

## 4. Pending / Next Steps
*   **Layout:** Verify `AFlightSpatialLayoutDirector` logic (GameMode cast fixed, but needs testing).
*   **Rendering:** Verify Linux performance with RayTracing disabled.
*   **AI:** `StateTree` integration is currently scaffolding; need to implement actual decision logic (e.g., "Break formation to attack").
