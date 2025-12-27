# Engine Integration

This document maps FlightProject's custom code to the Unreal Engine systems it leverages.

## Engine Version

- **Unreal Engine**: 5.7.1 (source build)
- **Engine Association**: `5F9B7F2C-6733-46F8-8CDF-2E962DA75B9C`
- **Platform**: Linux (CachyOS/Arch) with Vulkan SM6

## Plugin Dependencies

### Runtime Plugins

| Plugin | Purpose | Our Integration |
|--------|---------|-----------------|
| **EnhancedInput** | Modern input system | Flight controls (throttle, pitch, yaw, roll, climb) via `AFlightVehiclePawn` |
| **MassGameplay** | Entity Component System | Custom phases, fragments, processors for swarm simulation |
| **MassAI** | AI behaviors for Mass | Dependency declared, planned for autopilot behaviors |
| **StateTree** | Hierarchical state machines | Planned for complex autopilot decision-making |
| **Niagara** | GPU particle/compute system | Planned for perception visualization and GPU compute scripts |
| **ComputeFramework** | RDG compute shaders | Planned for horizon scans, avoidance calculations |

### Editor Plugins

| Plugin | Purpose |
|--------|---------|
| ModelingToolsEditorMode | Geometry editing |
| ChaosCaching | Physics caching |
| GeometryScripting | Procedural geometry |
| WorldPartitionHLODUtilities | Streaming level management |
| MassEntityEditor | Mass debugging tools |
| NiagaraEditor | Niagara authoring |

## Module Dependencies

From `FlightProject.Build.cs`:

```csharp
// Core
Core, CoreUObject, Engine, InputCore

// Input
EnhancedInput

// AI & Navigation
AIModule, GameplayTasks, NavigationSystem

// Mass ECS
MassEntity, MassActors, MassSpawner, MassSimulation

// GPU & Rendering
Niagara, ComputeFramework, RenderCore, RHI

// Physics
Chaos, PhysicsCore

// Behavior
StateTreeModule

// Configuration
DeveloperSettings
```

## Engine Systems Usage

### Navigation System

**Configuration** (`DefaultEngine.ini`):
```ini
[/Script/NavigationSystem.NavigationSystemV1]
bAutoCreateNavigationData=True
DefaultAgentName=Flying
```

**Our Usage**:
- Custom flying agent configured
- `UFlightNavGraphDataHubSubsystem` provides graph-based navigation (prototype)
- `AFlightNavBuoyRegion` generates navigation reference points

### Enhanced Input

**Configuration** (`DefaultInput.ini`):
- Action mappings: `Toggle_AI_Override`, `Toggle_Autopilot`, `Emergency_Stop`, `Cycle_Camera`
- Axis mappings: `Fly_Throttle`, `Fly_Pitch`, `Fly_Yaw`, `Fly_Roll`, `Fly_Climb`
- Debug: `Mass_Profiler_Snapshot` (Ctrl+Shift+F1), `Mass_Debugger_Toggle` (Shift+F2)

**Our Usage**:
- `AFlightVehiclePawn::SetupPlayerInputComponent()` binds Enhanced Input actions
- `UFlightMovementComponent` receives smoothed control inputs
- Input smoothing configurable via `bSmoothInput`, `InputSmoothingSpeed`

### Mass Entity System

**Configuration** (`DefaultMass.ini`):
```ini
[/Script/MassSimulation.MassSimulationSettings]
ProcessingPhases=(PhaseName="Flight.PrePhysics", TickGroup=TG_PrePhysics)
ProcessingPhases=(PhaseName="Flight.DuringPhysics", TickGroup=TG_DuringPhysics)
ProcessingPhases=(PhaseName="Flight.PostPhysics", TickGroup=TG_PostPhysics)
FixedTickInterval=0.016667
```

**Our Usage**:
- Custom processing phases for flight simulation
- Fragments: `FFlightPathFollowFragment`, `FFlightTransformFragment`, `FFlightVisualFragment`
- Processors: `UFlightPathFollowProcessor`, `UFlightTransformSyncProcessor`
- Path registry: `UFlightWaypointPathRegistry` for cache-efficient spline lookups

See [MassECS.md](MassECS.md) for detailed Mass architecture.

### Renderer & GPU

**Configuration** (`DefaultEngine.ini`):
```ini
r.RDGAsyncCompute=1
r.RayTracing=True
r.VirtualShadows=True
r.Nanite=True
r.MeshStreaming=True
```

**Our Usage**:
- Async compute enabled for parallel GPU workloads
- Custom shaders in `Shaders/` directory (registered via `FFlightProjectModule`)
- Planned: RDG compute shaders for horizon scans, Niagara data interfaces

### World Partition

**Configuration** (`DefaultEngine.ini`):
```ini
[/Script/Engine.WorldPartitionSubsystem]
bEnableStreaming=True
bEnableHLODStreaming=True
SpatialHashCellSize=12800
```

**Our Usage**:
- Streaming enabled for large-scale simulation
- HLOD for distant geometry
- Planned: Level streaming integration with `UFlightLevelLoaderSubsystem`

### Asset Manager

**Configuration** (`DefaultGame.ini`):
```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="FlightVehicleData",...)
+PrimaryAssetTypesToScan=(PrimaryAssetType="FlightBehaviorTree",...)
+PrimaryAssetTypesToScan=(PrimaryAssetType="FlightMassProcessor",...)
```

**Our Usage**:
- Custom asset types for flight vehicles, behavior trees, Mass processors
- Enables async loading and asset registry queries

## Our Code vs. Engine Code

### What We Build

| Category | Classes | Replaces/Extends |
|----------|---------|------------------|
| **Pawns** | `AFlightVehiclePawn`, `AFlightAIPawn` | Custom flight pawns (not using ADefaultPawn) |
| **Movement** | `UFlightMovementComponent` | Custom physics (not UFloatingPawnMovement) |
| **Game Framework** | `AFlightGameMode`, `AFlightGameState`, `AFlightPlayerController` | Standard game framework classes |
| **Subsystems** | `UFlightWorldBootstrapSubsystem`, `UFlightSwarmSpawnerSubsystem`, `UFlightDataSubsystem` | Custom world/game instance subsystems |
| **Mass ECS** | Fragments, Processors, Traits | Extends Mass framework |
| **Data** | CSV loading, `UFlightProjectDeveloperSettings` | Custom data pipeline |
| **Spatial** | `AFlightSpatialLayoutDirector`, `AFlightNavBuoyRegion`, `AFlightSpawnSwarmAnchor` | Custom level population |

### What We Use Directly

| Engine System | How We Use It |
|---------------|---------------|
| `USplineComponent` | `AFlightWaypointPath` wraps spline for flight paths |
| `UStaticMeshComponent` | Vehicle body rendering |
| `USpringArmComponent` | Camera boom for chase view |
| `UCameraComponent` | Cockpit and chase cameras |
| `UPointLightComponent` | Navigation lights on vehicles |
| Enhanced Input | Direct binding in pawn |
| Mass Entity Manager | Direct API calls for entity spawning |
| Data Tables | CSV import for configuration |

### What We Don't Use (Yet)

| Engine System | Reason |
|---------------|--------|
| Behavior Trees | Planned via StateTree instead |
| AIController | Using direct path-following, not AI perception |
| Character Movement | Flight dynamics are custom |
| Gameplay Abilities | Not needed for current scope |
| Common UI | No UI framework yet |

## Integration Points for Future Work

1. **StateTree**: Author autopilot behaviors as state machines
2. **Niagara**: GPU compute scripts for perception, trail visualization
3. **ComputeFramework**: RDG shaders for horizon scanning
4. **MassAI**: Perception and steering behaviors
5. **World Partition**: Streaming for massive environments
