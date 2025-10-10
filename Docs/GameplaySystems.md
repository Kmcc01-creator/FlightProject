# FlightProject Gameplay Systems

This document catalogs the runtime actors, data sources, and interaction flows that currently drive FlightProject's sandbox gameplay. Use it alongside `Docs/ProjectSpecification.md` for long-term goals and `Docs/DataPipeline.md` for detailed CSV schemas.

## Runtime bootstrap (StartPlay)
`AFlightGameMode::StartPlay` governs the world setup:

1. **Mass Runtime** – `InitializeMassRuntime` resumes `UMassSimulationSubsystem` if paused, ensuring Mass processors tick during play.
2. **Night Environment** – `SetupNightEnvironment` pulls an optional lighting row from `UFlightDataSubsystem` and applies it to any `ADirectionalLight`/`ASkyLight` in the level (spawning new actors if needed).
3. **Spatial Layout** – `BuildSpatialTestRange` guarantees a single `AFlightSpatialLayoutDirector` exists to materialize navigation markers.
4. **Autonomous Flights** – `SpawnAutonomousFlights` prepares the active waypoint path, then spawns drones either from **Spawn Swarm Anchors** (designer/authored) or the autopilot defaults.

Every major step logs via `LogFlightGameMode`, making it easy to trace world setup in `Saved/Logs/FlightProject.log`.

## Key runtime actors & components

| Class | Purpose | Notes |
| --- | --- | --- |
| `AFlightVehiclePawn` | Player-controllable aircraft | Couples with `UFlightMovementComponent` for lift/thrust simulation. |
| `UFlightMovementComponent` | Custom movement stack | Exposes altitude buckets and autopilot hooks used by AI and Mass processors. |
| `AFlightAIPawn` | Autonomous drone pawn | Follows spline waypoints, applies autopilot config, and logs path assignment / anomalies. |
| `AFlightWaypointPath` | Spline-backed flight path | Configured from autopilot data; reused or spawned on demand. |
| `AFlightSpawnSwarmAnchor` | Editor/placeable drone spawn anchor | Declares drone counts, phase offsets, and optional speed overrides. |
| `AFlightNavBuoyRegion` | Editor-placeable buoy ring | Generates nav probe layout rows around its transform and applies CSV overrides. |
| `UFlightSpatialLayoutSourceComponent` | Component for layout rows | Lets any actor contribute layout rows that `AFlightSpatialLayoutDirector` will spawn. |
| `AFlightSpatialLayoutDirector` | Orchestrates layout spawns | Aggregates CSV rows + layout components, spawns `AFlightSpatialTestEntity` actors. |
| `AFlightSpatialTestEntity` | Developer-art nav probe/obstacle | Switches mesh per entity type, drives pulsing nav-light visuals for probes. |
| `UFlightDataSubsystem` | CSV-backed data loader | Provides lighting, autopilot, spatial layout, and procedural anchor overrides. |
| `UFlightProjectDeveloperSettings` | Config entry point | Points to CSV paths & tuning values (lighting/autopilot/spatial/procedural). |

## Data-driven configuration

- **Lighting & Autopilot** – `Content/Data/FlightLightingConfig.csv` and `FlightAutopilotConfig.csv` feed `UFlightDataSubsystem`, which caches active rows chosen in `DefaultGame.ini`.
- **Static Spatial Layout** – `FlightSpatialLayout.csv` lists explicit points (legacy nav probes, obstacles, landmarks). Each row becomes a `FFlightSpatialLayoutRow` the layout director spawns.
- **Procedural Anchors** – `FlightSpatialProcedural.csv` maps anchor identifiers to overrides:
  - `AnchorType=NavBuoyRegion` – Adjusts ring count, radius, light profile, pulse speed, and emissive multipliers for matching `AFlightNavBuoyRegion`.
  - `AnchorType=SpawnSwarm` – Supplies drone counts, phase offsets/spread, and optional autopilot speed for `AFlightSpawnSwarmAnchor`.
- **Developer Settings** – `Config/DefaultGame.ini` sets the CSV paths via `UFlightProjectDeveloperSettings`, so changing environments only requires editing config or the CSVs.

## Spatial layout flow

1. `UFlightDataSubsystem::GetSpatialLayout` returns any rows from `FlightSpatialLayout.csv`.
2. At play/construction time, `AFlightSpatialLayoutDirector` gathers:
   - Static CSV rows.
   - Rows provided by every `UFlightSpatialLayoutSourceComponent` in the level (e.g., those owned by `AFlightNavBuoyRegion` actors).
3. Each row is converted into an `AFlightSpatialTestEntity`, which applies the row’s transform, selects an appropriate mesh (nav probe / obstacle / landmark), and configures the point light. Nav probes also read per-row pulse/emissive multipliers.

This hybrid approach lets designers author a baseline CSV layout while dropping bespoke anchor actors in the editor for rapid iteration.

## Drone swarm flow

1. **Anchors (optional)** – Designers place `AFlightSpawnSwarmAnchor` actors in the level. During BeginPlay they query `UFlightDataSubsystem` for matching procedural overrides (by `AnchorId` or type) to determine:
   - Drone count (`SwarmDroneCount` or fallback `DroneCount` property).
   - Path phase offset / spread (`SwarmPhaseOffsetDeg`, `SwarmPhaseSpreadDeg`).
   - Autopilot speed override (`SwarmAutopilotSpeed`).
2. **Waypoint preparation** – `AFlightGameMode::FindOrCreateWaypointPath` makes sure a spline exists and is configured from the autopilot settings.
3. **Spawn ordering** – `SpawnAutonomousFlights` collects anchors, builds a normalized list of `FSpawnOrder` entries, and fills any gaps with evenly-distributed fallback spawns if no anchors exist.
4. **Drone instantiation** – Each `AFlightAIPawn`:
   - Receives the autopilot config (lights, navigation parameters).
   - Gets a path distance based on the computed spawn order (phase offsets map to spline distances).
   - Applies any per-anchor speed overrides.
   - Logs its setup via `LogFlightAIPawn`.

When no anchors are present, the system reverts to the CSV-defined autopilot defaults for drone count and loop behavior, preserving previous behavior.

## Visual feedback & logging

- `LogFlightGameMode` traces the full bootstrap pipeline, including anchor usage vs. default counts.
- `LogFlightAIPawn` emits BeginPlay, flight-path assignment, and spline validation warnings. Missing paths, invalid spline data, and zero-length paths report once per drone to avoid spam.
- `AFlightSpatialTestEntity` generates pulsing nav lights for probes. Pulse speed/emissive intensity can now be overridden per row (CSV or `AFlightNavBuoyRegion`).
- `Saved/Logs/FlightProject.log` will capture all of the above; `run_game.sh` already hints where logs live (`Saved/Logs/AutomationTool` for staging, `Saved/Logs/FlightProject.log` for runtime).

## Designer workflow recap

1. **Place anchors** – Drag `BP_FlightNavBuoyRegion` / `BP_FlightSpawnSwarmAnchor` (after creating blueprints from the C++ classes) into the level. Adjust exposed parameters (counts, radii, offsets).
2. **Configure data** – Update the relevant CSV rows to batch-change lighting, autopilot, or anchor overrides without touching the level.
3. **Run via scripts** – Use `run_editor.sh` / `run_game.sh` to launch with logging, gamescope wrappers, and autopilot staging.
4. **Validate** – Inspect `FlightProject.log` for spawn summaries and spline warnings; watch the buoy pulses to ensure overrides applied.

With these systems, designers can iterate on drone swarms and navigation markers purely in-editor, while engineers keep behavior data-driven through CSVs and developer settings.
