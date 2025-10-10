# FlightProject Navigation Network Design

This document outlines how to evolve FlightProject's existing spline-based autopilot into a buoy-driven navigation network that supports hierarchical routing and dynamic replanning for autonomous drones.

## Current Snapshot
- Drones are spawned and assigned a single `AFlightWaypointPath` spline during `AFlightGameMode::SpawnAutonomousFlights` (`Source/FlightProject/Private/FlightGameMode.cpp:182`). All agents share that spline, so rerouting requires rebuilding the spline globally.
- `AFlightNavBuoyRegion` emits `FFlightSpatialLayoutRow` entries that the layout director turns into `AFlightSpatialTestEntity` nav probes (`Source/FlightProject/Private/FlightNavBuoyRegion.cpp:77` and `Source/FlightProject/Private/FlightSpatialLayoutDirector.cpp:73`). These probes are visual only; the gameplay layer does not consume them.
- Spawn anchors (`AFlightSpawnSwarmAnchor`) provide counts and phase offsets but no destination awareness (`Source/FlightProject/Private/FlightSpawnSwarmAnchor.cpp:60`).

Taken together, we have a rich set of spatial markers but no runtime graph tying them into routable networks.

## Goals
1. **Graph-backed routing** – Represent nav buoys as nodes with weighted edges so drones can request and follow arbitrary point-to-point routes.
2. **Hierarchical topology** – Allow networks to be split into named regions (macro network) and local subnetworks (micro navigation) to cover large maps.
3. **Dynamic adjustments** – Support live cost updates and route replanning when hazards or congestion make an edge undesirable.
4. **Designer-friendly authoring** – Keep workflows data-driven: editors place buoy actors and tag networks; CSV overrides remain valid.

## Data Model

### Buoy Nodes
```cpp
struct FFlightNavNode
{
    FGuid NodeId;
    FName NetworkId;          // e.g., "DowntownRing"
    FName SubNetworkId;       // optional local grouping
    FVector Location;
    TArray<FFlightNavEdgeRef> OutgoingEdges;
    TArray<FName> Tags;       // e.g., "Refuel", "AvoidAtLowAlt"
};
```
- Nodes map one-to-one with buoy anchors or probes. They inherit transform data from `AFlightNavBuoyRegion` or CSV rows and capture authoring metadata.

### Edges
```cpp
struct FFlightNavEdge
{
    FFlightNavNodeId From;
    FFlightNavNodeId To;
    float BaseCost;           // default traversal time/effort
    float AltitudeDelta;      // used for climb/descend heuristics
    uint8 bBidirectional : 1;
    uint8 bCanHoldPattern : 1;
    TArray<FName> Conditions; // e.g., weather, blackout, etc.
};
```
- Edge cost can encode distance, altitude changes, or traffic weights. Flags support asymmetric traversal and special handling (e.g., circular holding patterns).

### Networks & Subnetworks
```cpp
struct FFlightNavNetwork
{
    FName NetworkId;
    FBox Bounds;
    TSet<FFlightNavNodeId> Nodes;
    TArray<FFlightNavConnector> Connectors;
};

struct FFlightNavConnector
{
    FName FromNetwork;
    FName ToNetwork;
    FFlightNavNodeId EntryNode;
    FFlightNavNodeId ExitNode;
    float TransferCost;
};
```
- Macro networks describe large areas (city, canyon, airport). Each can host multiple subnetworks (landing pattern, depot yard) identified by name. Connectors bridge networks and encode transfer costs.

## Runtime Systems

### `UFlightNavGraphSubsystem`
- World subsystem that owns the entire graph, built during world initialization.
- Responsibilities:
  1. **Graph construction** – Scan all `UFlightSpatialLayoutSourceComponent`s and `AFlightNavBuoyRegion`s to register nodes and edges at BeginPlay. Blueprint hooks let designers specify network IDs and connection hints per actor.
  2. **Runtime updates** – Expose `ReportEdgeCost`, `MarkEdgeBlocked`, `RegisterTemporaryNode` for live adjustments (e.g., weather hazard).
  3. **Queries** – Provide synchronous (`FindBestRoute`) and asynchronous (`RequestRouteAsync`) path searches. Internally use `FGraphAStar` or a custom Altitude-aware A* heuristic.
  4. **Debugging** – Draw network overlays, dump adjacency lists, and log route summaries.

### Route Representation
```cpp
struct FFlightNavRoute
{
    TArray<FFlightNavNodeId> NodeSequence;
    TArray<FTransform> WaypointTransforms; // world transforms along the path
    float EstimatedTravelTime;
    FFlightNavRouteMeta Meta;
};
```
- `WaypointTransforms` are sampled directly from node transforms or from interpolated bezier arcs between nodes to yield smooth flight. The meta payload captures chosen networks, constraints, and hazard notes for HUD/debug.

### Planning Flow
1. **Request** – Drone or manager submits `FFlightNavRouteRequest` with start node (or location), desired destination (node/network), allowed subnetworks, altitude constraints, and optional avoidance tags.
2. **Coarse pass** – Run pathfinding on network connectors to decide macro route between subnetworks.
3. **Local pass** – For each subnetwork segment, run fine-grained search over buoy nodes to produce `WaypointTransforms`.
4. **Smoothing** – Optionally generate intermediate spline points to keep roll/pitch continuous.
5. **Dispatch** – Return `FFlightNavRoute` to the caller with change notifications (multicast delegate) so drones can subscribe to future adjustments.

### Dynamic Adjustments
- Subsystem maintains a version counter per route. When costs change, it recomputes affected routes and notifies listeners.
- Drones implement an interface (`IFlightNavRouteListener`) to receive `OnRouteReplanned` and blend toward updated waypoints.

## Integration Points

### `AFlightNavBuoyRegion`
- Gains editable properties: `NetworkId`, `SubNetworkId`, `bRegisterEdgesAutomatically`, `EdgeConnectors`. During `RefreshLayout`, it registers nodes with the subsystem alongside generated layout rows.
- Generated nav probes still spawn for visualization, ensuring legacy debug visuals stay in sync with gameplay nodes.

### `AFlightSpawnSwarmAnchor`
- Adds a `PreferredDestinationNetwork` (FName) and optional `DestinationNode`. When drones spawn, the game mode can request a path from the anchor location to the target network instead of distributing along a static spline.

### `AFlightAIPawn`
- Extends autopilot with `SetNavRoute(const FFlightNavRoute& Route)` alongside current spline assignment.
- Internally maintains progress along node-to-node segments using the same throttle model, interpolating orientation between consecutive waypoint transforms.
- If no graph is available, falls back to legacy spline behavior for backward compatibility.

### `AFlightGameMode`
- During `SpawnAutonomousFlights`, query the nav graph subsystem for routes per swarm anchor. When multiple drones launch from one anchor, seed each with offset fractions along the shared route or request parallel lanes via route options.

## Editor & Authoring Workflow
1. **Place buoy regions** and assign network/subnetwork IDs via new details panel fields (Blueprint version mirrors the C++ defaults).
2. **Optional: author CSV overrides** to define inter-network connectors or fine-tune costs without touching the level.
3. **Preview networks** by toggling a debug command (e.g., `Flight.NavGraph.Show`). Designers can inspect node IDs and ensure connectors bridge the correct regions.
4. **Define mission routes** by tagging spawn anchors or mission objectives with destination networks, letting the runtime generate the correct path.

## Implementation Phases
1. **Graph foundation** – Implement `FFlightNavNode`/`Edge` structs, the subsystem, and registration hooks in buoy regions. Provide debug visualization.
2. **Path queries** – Add A* search with altitude-aware heuristics, route structs, and synchronous API. Integrate with a debug commandlet for validation.
3. **Drone adoption** – Extend `AFlightAIPawn` and `AFlightGameMode` to consume routes, including fallbacks to the existing spline system.
4. **Hierarchical & dynamic features** – Introduce network connectors, asynchronous queries, and live edge cost updates via events (e.g., congestion, weather).
5. **Tooling polish** – Blueprint wrappers, CSV schema updates, and editor visualization to keep designer workflows smooth.

### Prototype Status
- `UFlightNavGraphDataHubSubsystem` (`Source/FlightProject/Public/FlightNavGraphDataHubSubsystem.h`) now tracks nodes/edges, builds snapshots for visualization, and emits change events for the future hegetic interface.
- `FFlightNavGraphNodeDescriptor` / `FFlightNavGraphEdgeDescriptor` (`Source/FlightProject/Public/FlightNavGraphTypes.h`) expose blueprint-friendly descriptors so anchors or layout actors can register graph data at runtime.
- `AFlightNavBuoyRegion` auto-registers its generated buoys (and ring edges) with the hub, while `AFlightSpawnSwarmAnchor` contributes anchor nodes for last-mile routing overlays.
- Use `Flight.Debug.DumpNavGraph [verbose]` in the console to inspect the current snapshot and verify that buoy rings and anchors are contributing nodes/edges as expected.

## Open Questions
- How should we represent altitude layers (stacked paths) within a single network? A simple tag may be insufficient if drones of different classes must maintain vertical separation. Keeping this unresolved for now lets us gather design constraints before committing to a data model.
- Mission-level intent should flow from a scripting layer (e.g., GOAP-style planner) that selects high-level objectives and requests routes from the nav subsystem. Anchors remain the “last-mile” handoff—mission scripts pick a goal network/destination, and the subsystem handles the detailed buoy path so individual drones do not run their own global planners.
- Hypothesis for performance budget: target <2 ms of navigation time per frame for 50 active drones on a dedicated server core. Start with full A* replans on a throttled cadence (e.g., staggered every 0.5–1.0 s per agent); if congestion or emergent behaviors (collisions, traffic jams) drive the average cost higher, investigate incremental techniques (D* Lite, Lifelong Planning A*) and SIMD/“wave intrinsic” optimizations in the tight loops. Instrument route requests (queue depth, nodes expanded, time per solve) so we can validate the budget as systems scale.

Answering these items will steer the detailed implementation, but the architecture above keeps the system modular: cells register nodes, the subsystem manages routing, and drones consume routes without hard-coding spline layouts.
