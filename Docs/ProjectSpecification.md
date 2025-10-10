# FlightProject Specification

## Vision
FlightProject is an Unreal Engine 5 simulation sandbox for autonomous aircraft and drone swarms. The project targets large-scale AI behavior experimentation, GPU-driven perception & batching, and height-aware navigation suitable for research and production prototyping.

## Guiding Goals
- Support thousands of concurrently simulated aircraft with deterministic batch execution.
- Pair height-aware AI decision making with custom flight dynamics.
- Exploit GPU compute (Niagara, RDG compute shaders) for perception, avoidance, and telemetry reduction.
- Optimize memory footprint using Structure-of-Arrays (SoA) layouts and zero-copy GPU interop where possible.
- Provide clear extension points for designers via Behavior Trees, State Tree authoring, and Mass processors.

## Core Systems
- **FlightVehiclePawn / FlightMovementComponent**: Custom pawn & movement stack providing thrust, lift, drag, and steering forces alongside autopilot overrides. Designed to be driven by both player input and Mass processors.
- **Height-Aware AI Stack**: Behavior Trees & State Trees (plugins enabled) consume altitude buckets (low/medium/high) sourced from the movement component and Mass queries.
- **Mass Simulation**: DefaultMass configuration defines pre/post physics phases. Game mode primes `MassEntitySubsystem`/`MassSimulationSubsystem` to run in all worlds.
- **GPU Telemetry**: Renderer flags enable async compute, Niagara compute scripts, and HLOD streaming to facilitate GPU workloads. Shader directories will host RDG compute shaders for perception tasks.
- **Networking/Batching**: Custom `UFlightNetworkManager` widens bandwidth headroom and smoothing settings for swarms. Asset Manager rules ensure flight data assets stream in batched groups.
- **Night Ops Autopilot Demo**: Autonomous drones (`AFlightAIPawn`) loop along spline-driven waypoints with navigation lights for rapid iteration on 3D flight path algorithms.
  The demo now layers in a `NightRange` spatial testbed composed of CSV-authored nav probes, illuminated landmarks, and collision towers.

## Content & Data
- **Maps**: Temporarily bootstrapped with `/Engine/Maps/Templates/Template_Default` until the project map `/Game/Maps/PersistentFlightTest` and loading transition `/Game/Maps/Loading` are authored.
- **CSV Tuning**: Lighting and autopilot settings live in `Content/Data/*.csv`; see `Docs/DataPipeline.md` for schema and pitfalls.
- **Spatial Layout**: `FlightSpatialLayout.csv` drives the placement of nav probes, developer-art obstacles, and landmark beacons spawned by `AFlightSpatialLayoutDirector` / `AFlightSpatialTestEntity` to stress avoidance and route planning in nightly conditions.
- **Mass Config**: `Config/DefaultMass.ini` seeds flight-specific phases (`Flight.PrePhysics`, `Flight.DuringPhysics`, `Flight.PostPhysics`).

## Pipeline & Tooling
- **Plugins Enabled**: Enhanced Input, Niagara, MassGameplay (incl. MassActors/MassSimulation), MassAI, StateTree, ComputeFramework, ChaosCaching, GeometryScripting, WorldPartitionHLODUtilities.
- **Module Dependencies**: FlightProject runtime module links AI, Mass, Niagara, ComputeFramework, DeveloperSettings, and editor hooks for tooling.
- **Input Mapping**: `DefaultInput.ini` exposes throttle/pitch/yaw/roll/climb axes plus Mass debugger tooling shortcuts.
- **Developer Settings**: `UFlightProjectDeveloperSettings` centralizes altitude thresholds, Mass batch sizing, and shader directory hints.

## Roadmap Highlights
1. **Flight AI Behaviors**: Implement Behavior Tree services for altitude stratification and collision avoidance; author State Trees for autopilot tasks.
2. **Mass Processors**: Build SoA component fragments for flight metrics; register processors for perception, steering, and scheduling with the new phases.
3. **GPU Compute Path**: Introduce RDG compute shaders for horizon scans and avoidance volumes; integrate with Niagara for visualization.
4. **Batch Spawning Tools**: Author data-driven spawners leveraging Mass Actors and the Asset Manager to instantiate swarm cohorts.
5. **Profiling & Automation**: Wire Mass debugger snapshots to automated performance captures, enabling regression tracking across altitude scenarios.
