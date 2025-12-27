# Mass ECS Architecture (UE 5.7)

This document describes FlightProject's integration with Unreal's Mass Entity system for high-performance swarm simulation.

## Overview

Mass Entity is Unreal's Entity Component System (ECS) framework. Unlike traditional Actor-based code, Mass uses:
- **Fragments**: Pure data structs (no logic).
- **Processors**: Logic that iterates over entities with specific fragments.
- **Traits**: Building blocks that define an Entity Config.

This enables simulating thousands of entities with minimal overhead.

## The View vs. Simulation Split

### Mass Model
```
┌─────────────────────────────────────────────────────────┐
│ SIMULATION (Mass)                                        │
│ ┌─────────────────┐  ┌─────────────────┐                │
│ │ Data (Fragments)│  │ Logic (Processor)│                │
│ │ - PathId        │  │ ForEachChunk()  │                │
│ │ - Distance      │──│ {               │                │
│ │ - Speed         │  │   // tight loop │                │
│ │ - Location      │  │ }               │                │
│ └─────────────────┘  └─────────────────┘                │
└─────────────────────────────────────────────────────────┘
                          │
                          │ (LOD0 only)
                          ▼
┌─────────────────────────────────────────────────────────┐
│ VIEW (Actor Representation)                              │
│ ┌─────────────────────────────────────────────────────┐ │
│ │ AFlightAIPawn (spawned only when entity is close)   │ │
│ │ - Mesh, Lights, Effects                             │ │
│ └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## Implementation Details

### Fragments
Defined in `Source/FlightProject/Public/Mass/FlightMassFragments.h`.
*   `FFlightPathFollowFragment`: Stores `PathId` (GUID), `CurrentDistance`, `DesiredSpeed`.
*   `FFlightTransformFragment`: Stores pure location/rotation/velocity.
*   `FFlightVisualFragment`: Stores rendering properties (Light color/intensity).
*   `FFlightSwarmMemberTag`: Empty tag for filtering swarm entities.

### Processors
Defined in `Source/FlightProject/Public/Mass/FlightPathFollowProcessor.h`.

1.  **`UFlightPathFollowProcessor`** (Phase: `PrePhysics`)
    *   Iterates entities with `PathFollow` + `Transform` fragments.
    *   Advances `CurrentDistance` based on `DesiredSpeed * DeltaTime`.
    *   Queries `UFlightWaypointPathRegistry` using `PathId` to get the world location/rotation.
    *   Updates `FFlightTransformFragment`.

2.  **`UFlightTransformSyncProcessor`** (Phase: `PostPhysics`)
    *   Iterates entities with `Transform` + `MassActorFragment` (Representation).
    *   Copies the ECS transform to the `AActor` representation (if spawned).

### Path Registry
`UFlightWaypointPathRegistry` (World Subsystem) allows processors to query spline data without dereferencing `AFlightWaypointPath` actors.
*   **Registration:** `AFlightWaypointPath` registers itself on `BeginPlay`.
*   **Lookup:** Processors use `FindPath(FGuid)` to get a pre-sampled LUT (Lookup Table).

## Spawning (SwarmEncounter Plugin)

The `SwarmEncounter` Game Feature Plugin handles spawning via `UFlightSwarmSpawnerSubsystem`.

1.  **Config:** Uses `DA_SwarmDroneConfig` (MassEntityConfigAsset) which defines the entity traits (Visuals, PathFollowing).
2.  **Anchors:** Iterates `AFlightSpawnSwarmAnchor` actors in the level to determine spawn locations and counts.
3.  **Batching:** Uses `MassSpawnerSubsystem->SpawnEntities(...)` to create entities efficiently.
4.  **Init:** Manually initializes `PathId` and `CurrentDistance` on the new entities immediately after spawn.

## File Reference

```
Source/FlightProject/
├── Public/Mass/
│   ├── FlightMassFragments.h
│   ├── FlightPathFollowProcessor.h
│   └── FlightWaypointPathRegistry.h
Plugins/GameFeatures/SwarmEncounter/
├── Source/SwarmEncounter/
│   ├── Public/FlightSwarmTrait.h       # Trait definition
│   └── Private/FlightSwarmSpawnerSubsystem.cpp # Logic
```

## UE 5.7 Specifics
*   **SDL3:** The project runs on SDL3 (Linux).
*   **Strictness:** `BuildSettingsVersion.V6` is enforced.
*   **Processors:** `ConfigureQueries` is now done in the constructor or via `Initialize` to respect `final` override changes in 5.6+.