# FlightProject Navigation Network Design

This document outlines how FlightProject's navigation evolves from simple splines to a graph-backed network, integrated with the Mass ECS architecture.

## Current Snapshot (UE 5.7)
- **Mass Integration:** Drones are now Mass Entities. They follow paths using `UFlightPathFollowProcessor`.
- **Path Registry:** `UFlightWaypointPathRegistry` stores pre-sampled splines (`FFlightPathData`) referenced by `FGuid PathId` in `FFlightPathFollowFragment`.
- **Nav Hub:** `UFlightNavGraphDataHubSubsystem` collects nodes/edges from `AFlightNavBuoyRegion` for high-level connectivity visualization, but Mass entities currently follow linear splines (waypoints).

## Goals
1. **Graph-backed routing** – Allow Mass entities to switch `PathId` dynamically to traverse a network.
2. **Hierarchical topology** – Macro networks (Regions) and micro networks (Local Paths).
3. **Mass-efficient** – Path following must remain cache-friendly (Processors iterate pre-sampled data).

## Data Model

### Registry & Lookup
The **Registry** (`UFlightWaypointPathRegistry`) is the bridge.
*   **Input:** `AFlightWaypointPath` (Spline Actor).
*   **Internal:** `FFlightPathData` (TArray of locations/tangents).
*   **Key:** `FGuid`.

### Mass Fragments
```cpp
struct FFlightPathFollowFragment : public FMassFragment
{
    FGuid PathId;             // Current spline being followed
    float CurrentDistance;    // Progress along spline
    float DesiredSpeed;       // cm/s
};
```

## Runtime Systems

### `UFlightWaypointPathRegistry`
*   **Role:** The "Map".
*   **Behavior:** On `BeginPlay`, `AFlightWaypointPath` actors register themselves. The registry builds a LUT (Lookup Table) for efficient sampling.
*   **Usage:** Processors call `Registry->FindPath(PathId)` to get world-space transforms without touching UObjects.

### `UFlightSwarmSpawnerSubsystem` (Plugin)
*   **Role:** The "Dispatcher".
*   **Behavior:** Spawns entities and assigns their initial `PathId`.
*   **Future:** Will query a Navigation Graph to determine *which* sequence of PathIds an entity should follow to reach a destination.

## Integration Points

### `AFlightWaypointPath`
*   Registers automatically on BeginPlay.
*   Acts as the authoring tool for flight lanes.

### `AFlightSpawnSwarmAnchor`
*   Defines where entities enter the system.
*   Currently assigns entities to the nearest (or global) path.

## Future Implementation Phases
1.  **Graph Routing:** Implement a `UFlightNavRouteProcessor` that updates `PathId` in the fragment when an entity reaches the end of its current path, allowing complex multi-spline journeys.
2.  **Dynamic Avoidance:** Use Mass Avoidance traits (Steering) for local collision avoidance while adhering to the global spline.