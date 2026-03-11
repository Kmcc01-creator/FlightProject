# FlightProject Gameplay Systems

This document catalogs the runtime actors, data sources, and interaction flows that currently drive FlightProject's sandbox gameplay.

## Runtime bootstrap (StartPlay)
`AFlightGameMode::StartPlay` delegates bootstrap work to modular World Subsystems, ensuring clean separation of concerns:

- **`UFlightWorldBootstrapSubsystem`** – Resumes `UMassSimulationSubsystem` if paused, applies lighting profiles from `UFlightDataSubsystem`, and ensures a single `AFlightSpatialLayoutDirector` exists (calling `RebuildLayout` immediately).
- **`UFlightSwarmSpawnerSubsystem`** (in `SwarmEncounter` Plugin) – Configures/creates `AFlightWaypointPath`, gathers `AFlightSpawnSwarmAnchor` actors, and batched-spawns Mass Entities using the `DA_SwarmDroneConfig` asset.

## Current architectural reading

The project is no longer treating authored Unreal actors as the final runtime model.

The working split is:

- Unreal actors/components are authoring, placement, and registration surfaces
- registries, participant descriptors, Mass fragments, and orchestration records are the runtime-facing model

In practice:

- `AFlightWaypointPath` is an authored spline surface and registration source
- `AFlightSpawnSwarmAnchor` is an authored spawn/intent surface and nav-graph registration source
- `UFlightWaypointPathRegistry` is the cache-friendly runtime path lookup surface
- `FFlightPathFollowFragment` and related fragments are the hot-loop simulation state
- `UFlightOrchestrationSubsystem` reduces visible actors and services into participants, cohorts, navigation candidates, and reports

This means the current architecture is already moving toward an actor-adapter model even if that name is not yet formalized in code.

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

## Actor adapter direction

The recommended direction is to keep Unreal-native authoring surfaces while making them adapters into the project-owned runtime model.

### What an actor should own

Actors are still the right place for:

- editor placement
- transform and world presence
- designer-facing properties
- level-authored relationships
- registration hooks into nav graph, orchestration, or registries

### What an actor should not own

Actors should not remain the long-term authority for:

- hot-loop swarm state
- per-entity runtime mutation
- route commitment for large groups
- execution-domain selection
- VEX/Verse execution truth

Those responsibilities belong to:

- Mass fragments
- compact registries/LUTs
- schema/runtime contracts
- orchestration-owned cohort and candidate records

### Practical adapter examples

- `AFlightWaypointPath`
  - authored spline + navigation metadata
  - adapts into `PathId`, LUT data, orchestration participant records, and navigation candidates
- `AFlightSpawnSwarmAnchor`
  - authored spawn and intent metadata
  - adapts into nav-graph nodes, orchestration participants, cohort constraints, and spawn orders

That is the bridge point between Unreal-specific content and the project’s own runtime model.

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
   - Builds `FFlightMassBatchLoweringPlan` per anchor.
   - Reconciles those plans through `UFlightOrchestrationSubsystem`.
   - Resolves the selected navigation candidate per cohort through `FFlightNavigationCommitProduct`.
   - Calls `MassSpawnerSubsystem` to spawn batches of entities using the `DA_SwarmDroneConfig` template.
   - Initializes runtime navigation state with:
     - `FFlightNavigationCommitSharedFragment` for the selected commit product.
     - `FFlightPathFollowFragment` for the current path-follow backend.
     - `PathId` from either a real waypoint path or a synthetic lowered route.
     - `CurrentDistance` calculated from Anchor Phase + Index.
     - `DesiredSpeed`.
4. **Simulation (Processors)** – `UFlightPathFollowProcessor` runs every frame (PrePhysics), queries the Registry using `PathId`, and updates the entity's Transform based on `CurrentDistance`.
5. **Visualization** –
   - **Far:** Mass Representation renders Instanced Static Meshes (ISM).
   - **Near:** `UFlightTransformSyncProcessor` syncs the Mass Transform to a temporary Actor (if spawned).

## Individual actor vs batch/swarm processing

This is the main bridge problem the project is now solving.

### Individual actor mode

Unreal-level actors are still useful when the system needs:

- authored placement
- unique hand-tuned metadata
- direct editor interaction
- debug visualization
- one-off gameplay objects

Examples:

- a single spawn anchor
- a waypoint spline path
- a nav buoy region

### Batch/swarm mode

The runtime wants something different:

- many entities sharing the same execution rules
- compact shared state
- predictable cache-friendly processing
- no per-entity UObject dereference in hot loops

That is why the swarm path already lowers into:

- `FFlightPathFollowFragment`
- `FFlightTransformFragment`
- `FFlightBehaviorCohortFragment`
- `UFlightWaypointPathRegistry`

### Recommended bridge model

The best next-step model is:

1. authored actors declare intent and world placement
2. adapter logic reduces them into project-owned descriptors
3. orchestration resolves cohorts, legality, and chosen candidates
4. spawn/runtime code lowers that decision into Mass fragments and shared runtime state
5. processors run in batch

This keeps Unreal content ergonomic while preserving the performance model needed for swarm-scale simulation.

### Why batch processing matters

For swarm-scale simulation, the key win is not just "use Mass."
It is to move from per-actor decision ownership to cohort/batch ownership.

That means:

- one anchor can describe a whole spawn cohort
- one path or route candidate can feed many entities
- one legality/ranking decision can apply to a whole group
- one shared fragment or path registry entry can serve many entities

This is the natural place where orchestration, navigation, and reflection/schema work converge.

### Current limitation

The bridge is not fully unified yet.

Today:

- orchestration can score and select navigation candidates per cohort
- the spawner reconciles cohort metadata through orchestration and consumes the selected navigation commitment during spawn
- selected non-path candidates now survive spawn as shared runtime commit metadata instead of collapsing immediately into an untraceable fallback

So the current batch-processing story is now unified through the spawn-commit seam. The remaining limitation is lower in the stack: hot-loop movement still executes through `FFlightPathFollowFragment.PathId`, even though richer commit meaning is now preserved as shared runtime metadata.

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
- **Formalize actor adapters** – Make actor-to-runtime reduction explicit for paths, anchors, and future navigation providers.
- **Extend commit-product consumers** – Let more runtime systems consume `FFlightNavigationCommitProduct` / `FFlightNavigationCommitSharedFragment` directly instead of assuming `PathId` is the whole navigation story.
- **Promote shared batch descriptors** – Let cohorts and shared fragments carry more of the runtime truth so authored actors remain ingress surfaces rather than simulation authorities.
