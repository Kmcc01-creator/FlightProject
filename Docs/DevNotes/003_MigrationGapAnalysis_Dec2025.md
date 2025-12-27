# DevNote 003: Migration Gap Analysis (Dec 2025)

**Date:** Friday, December 26, 2025
**Status:** Audit of UE 5.7 / Mass Architecture Migration

This note captures the current state of "Doc Drift" and "Code Gaps" identified during the migration to the Mass/GameFeature architecture.

## 1. Documentation Drift (Misleading Instructions)

The following documentation files reflect the legacy Actor-based architecture and need updating:

*   **`Docs/Workflow/GameplaySystems.md`**:
    *   Still frames runtime flow as `AFlightAIPawn` actor spawning.
    *   Does not mention the `SwarmEncounter` plugin boundary.
    *   Lists "StartPlay decomposition" as a future goal, though it is now implemented in `FlightWorldBootstrapSubsystem`.
*   **`Docs/Architecture/DataPipeline.md`**:
    *   Incorrectly claims `FlightGameMode` consumes CSV lighting/autopilot data. This logic moved to `UFlightWorldBootstrapSubsystem` and `SwarmEncounter`.
*   **`Docs/Architecture/Navigation.md`**:
    *   References the deleted method `AFlightGameMode::SpawnAutonomousFlights`.
    *   Routing discussion lacks integration with Mass `FlightPathFollowProcessor`.
*   **`Docs/Architecture/MassECS.md`**:
    *   Describes nonexistent classes like `UFlightMassSpawning` (deleted) and `DA_DroneEntityConfig` (actual asset name: `DA_SwarmDroneConfig`).
*   **`Docs/Environment/BuildAndRegen.md`**:
    *   Contains outdated paths (`~/Documents/...`) vs the actual `/home/kelly/Unreal/Projects/...`.
    *   Incorrectly states GameMode spawns `FlightSpatialLayoutDirector` (now done by Bootstrap subsystem).

## 2. Code Gaps (Broken Logic)

The new Mass implementation scaffolding is present but functionally incomplete:

*   **Logic Disconnect (`FlightSwarmSpawnerSubsystem`)**:
    *   **Anchors Ignored:** The spawner does not consult `AFlightSpawnSwarmAnchor`. It ignores per-anchor counts and phases.
    *   **Hardcoded Values:** `DesiredSpeed` is hardcoded to `1500.f`, ignoring CSV/Autopilot configs.
*   **Path Registry Disconnected (`FlightWaypointPathRegistry`)**:
    *   **No Registration:** Nothing calls `RegisterPath`.
    *   **No IDs:** `FFlightPathFollowFragment::PathId` is never set on spawned entities.
    *   **Result:** `UFlightPathFollowProcessor` will fail `FindPath()` and do nothing.
*   **Missing Assets**:
    *   The code tries to load `/SwarmEncounter/DA_SwarmDroneConfig.DA_SwarmDroneConfig`, but this asset does not exist in the repo (Content folder is empty). Spawning will abort.
*   **Initialization Gaps**:
    *   Entities are spawned but not correctly seeded along the spline (no loop flags, locations are just the waypoint origin).

## 3. Immediate Action Plan

1.  **Docs:** Update the 5 listed files to reflect the Plugin/Subsystem reality.
2.  **Asset:** Manually create `DA_SwarmDroneConfig` using the Python script tools.
3.  **Code:**
    *   Wire `AFlightSpawnSwarmAnchor` reading into `FlightSwarmSpawnerSubsystem`.
    *   Implement `RegisterPath` calls in `AFlightWaypointPath::BeginPlay`.
    *   Pass `PathId` to Mass Fragments during spawn.
