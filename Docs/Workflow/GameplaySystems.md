# FlightProject Gameplay Systems

This document catalogs the runtime actors, data sources, and interaction flows that currently drive FlightProject's sandbox gameplay.

## Runtime bootstrap (StartPlay)
`AFlightGameMode::StartPlay` delegates bootstrap work to modular World Subsystems, ensuring clean separation of concerns:

- **`UFlightWorldBootstrapSubsystem`** – Resumes `UMassSimulationSubsystem` if paused, applies lighting profiles from `UFlightDataSubsystem`, and ensures a single `AFlightSpatialLayoutDirector` exists (calling `RebuildLayout` immediately).
- **`UFlightSwarmSpawnerSubsystem`** (in `SwarmEncounter` Plugin) – Configures/creates `AFlightWaypointPath`, gathers `AFlightSpawnSwarmAnchor` actors, and batched-spawns Mass Entities using the `DA_SwarmDroneConfig` asset.

## Key runtime actors & components

| Class | Purpose | Notes |
| --- | --- | --- |
| `AFlightVehiclePawn` | Player-controllable aircraft | Couples with `UFlightMovementComponent` for lift/thrust simulation. |
| `UFlightMovementComponent` | Custom movement stack | Exposes altitude buckets and autopilot hooks. |
| `AFlightWaypointPath` | Spline-backed flight path | Registers itself with `UFlightWaypointPathRegistry` for Mass Processor lookups. |
| `AFlightSpawnSwarmAnchor` | Editor/placeable swarm anchor | Declares drone counts, phase offsets, and optional speed overrides. |
| `AFlightNavBuoyRegion` | Editor-placeable buoy ring | Generates nav probe layout rows around its transform. |
| `UFlightSpatialLayoutSourceComponent` | Component for layout rows | Lets any actor contribute layout rows that `AFlightSpatialLayoutDirector` will spawn. |
| `AFlightSpatialLayoutDirector` | Orchestrates layout spawns | Aggregates CSV rows + layout components, spawns `AFlightSpatialTestEntity` actors. |
| `UFlightDataSubsystem` | Resolved gameplay-data ingress/binding arbiter | Provides typed lighting, autopilot, spatial layout, and procedural anchor data without becoming a live runtime blackboard. |
| `UFlightSwarmSpawnerSubsystem` | Swarm orchestration (Plugin) | Spawns Mass Entities based on anchors, using `MassSpawnerSubsystem`. |
| `UFlightProjectDeveloperSettings` | Config entry point | Points to CSV paths & tuning values. |

## Data-driven configuration

- **Lighting & Autopilot** – `Content/Data/FlightLightingConfig.csv` and `FlightAutopilotConfig.csv` currently feed `UFlightDataSubsystem`.
- **Mass Entity Config** – `DA_SwarmDroneConfig` (Data Asset) defines the composition of a swarm drone (Traits: `FlightSwarmTrait`, etc).
- **Static Spatial Layout** – `FlightSpatialLayout.csv` lists explicit points.
- **Developer Settings** – `Config/DefaultGame.ini` currently sets the default table paths and selected rows.

Boundary note:

- `UFlightDataSubsystem` is the source/binding/cache surface for resolved gameplay data
- active world truth, live execution state, and inter-system runtime ownership should remain in orchestration or domain subsystems instead of accreting into a general-purpose data blackboard

## Drone swarm flow (Mass/ECS)

1. **Anchors (optional)** – Designers place `AFlightSpawnSwarmAnchor` actors.
2. **Waypoint Registry** – `AFlightWaypointPath` registers its spline data into a lightweight LUT (Lookup Table) in `UFlightWaypointPathRegistry` on BeginPlay.
3. **Mass Spawning** – `UFlightSwarmSpawnerSubsystem`:
   - Iterates all Anchors.
   - Calls `MassSpawnerSubsystem` to spawn batches of entities using the `DA_SwarmDroneConfig` template.
   - Manually initializes fragments (`FFlightPathFollowFragment`) with:
     - `PathId` (from the registry).
     - `CurrentDistance` (calculated from Anchor Phase + Index).
     - `DesiredSpeed`.
4. **Simulation (Processors)** – `UFlightPathFollowProcessor` runs every frame (PrePhysics), queries the Registry using `PathId`, and updates the entity's Transform based on `CurrentDistance`.
5. **Visualization** –
   - **Far:** Mass Representation renders Instanced Static Meshes (ISM).
   - **Near:** `UFlightTransformSyncProcessor` syncs the Mass Transform to a temporary Actor (if spawned).

## Visual feedback & logging

- `LogFlightSwarmSpawner` reports batch spawn counts and template IDs.
- `LogFlightWorldBootstrap` covers Mass resume + lighting/layout setup.
- `Saved/Logs/FlightProject.log` captures runtime events.

## Designer workflow recap

1. **Place anchors** – Drag `BP_FlightSpawnSwarmAnchor` into the level.
2. **Run via scripts** – Use `run_editor.sh` to launch.
3. **Validate** – Check Output Log for "Mass Spawned X entities".

## Future cleanup goals
- **Unify data ingress** – Replace the repetitive `UFlightDataSubsystem` table loaders with a shared resolver/helper that can support additional ingress formats cleanly.
- **Anchor runtime base** – Introduce a shared procedural-anchor component.
- **Batch nav-graph updates** – Extend `UFlightNavGraphDataHubSubsystem` with scoped/batched notifications.
