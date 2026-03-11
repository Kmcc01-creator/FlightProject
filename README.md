# FlightProject

FlightProject is a C++23 Unreal Engine 5.7 project converging on a schema-bound execution architecture.

The current project center is not best described as only:

- a flight sandbox
- a swarm renderer
- a VEX compiler experiment
- an ECS-only gameplay project

The better description is:

- authored intent becomes a typed contract
- contracts bind against reflected schemas and world context
- orchestration selects legal executable paths for cohorts and services
- runtime systems lower those decisions into Mass, native, Verse, or GPU-facing execution
- reports and diagnostics preserve what was chosen and why

For the current architectural source of truth, start with [Docs/README.md](Docs/README.md).

## Current System Center

The strongest active seams are:

- `UFlightOrchestrationSubsystem` as the world-scoped coordination surface
- `FVexTypeSchema`, SchemaIR, and `FVexSchemaOrchestrator` as the contract/binding surface
- `UFlightVerseSubsystem` as the compile/execute service for VEX-derived behavior
- actor adapters as the bridge from Unreal-authored content into project-owned runtime data
- Mass fragments, registries, and shared fragments as the hot-loop execution truth

The most important recurring pattern in the codebase is:

```text
Description
    -> Contract
        -> Binding
            -> Lowering
                -> Execution
                    -> Report
```

## Current Vertical Slices

These are the clearest implemented examples of the current direction:

- schema-driven VEX binding and backend capability reporting
- orchestration-issued behavior bindings for Mass cohorts
- navigation candidate promotion, legality, ranking, and execution-plan selection
- actor-adapter lowering from authored anchors and waypoint paths into cohorts, candidates, and batch spawn plans
- navigation commit products that preserve selected route meaning across spawn/runtime boundaries

## Repository Layout

- `Source/FlightProject`
  Main runtime/editor module code.
- `Plugins/GameFeatures/SwarmEncounter`
  Swarm spawning and encounter-specific runtime logic.
- `Docs/Architecture`
  Canonical subsystem and runtime model docs.
- `Docs/Workflow`
  Current implementation status, plans, and migration notes.
- `Docs/Environment`
  Build, setup, and troubleshooting guidance.
- `Docs/Scripting`
  VEX, Verse, and validation-focused material.
- `Scripts`
  Build, launch, and automation entrypoints.

## Start Here

If you need:

- current documentation map:
  [Docs/README.md](Docs/README.md)
- high-level project orientation:
  [Docs/Architecture/Overview.md](Docs/Architecture/Overview.md)
- current architectural frame:
  [Docs/Architecture/CurrentProjectVision.md](Docs/Architecture/CurrentProjectVision.md)
- world/runtime ownership:
  [Docs/Architecture/WorldExecutionModel.md](Docs/Architecture/WorldExecutionModel.md)
- orchestration:
  [Docs/Architecture/OrchestrationSubsystem.md](Docs/Architecture/OrchestrationSubsystem.md)
- navigation and actor-adapter flow:
  [Docs/Architecture/Navigation.md](Docs/Architecture/Navigation.md)
  [Docs/Architecture/ActorAdapters.md](Docs/Architecture/ActorAdapters.md)
- active implementation status:
  [Docs/Workflow/CurrentFocus.md](Docs/Workflow/CurrentFocus.md)
  [Docs/Workflow/CurrentBuild.md](Docs/Workflow/CurrentBuild.md)

## Build And Test Quickstart

Set the engine path first if needed:

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
```

Common commands:

```bash
# Regenerate project files
./Scripts/generate_project_files.sh -f

# Build the main editor target
./Scripts/build_targets.sh Development

# Build and run the default verification bucket
./Scripts/build_targets.sh Development --verify

# Headless automation
./Scripts/run_tests_headless.sh

# Focused headless bucket
./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Navigation.VerticalSlice.Contracts"

# GPU/offscreen automation
TEST_SCOPE=gpu_smoke ./Scripts/run_tests_full.sh
```

Use these for current status and troubleshooting:

- [Docs/Workflow/CurrentBuild.md](Docs/Workflow/CurrentBuild.md)
- [Docs/Environment/Configuration.md](Docs/Environment/Configuration.md)
- [Docs/Environment/Troubleshooting.md](Docs/Environment/Troubleshooting.md)

## Documentation Policy

Use this file as a short current orientation layer.

- keep deep architectural detail in `Docs/Architecture`
- keep dated status and rollout notes in `Docs/Workflow`
- keep build/setup details in `Docs/Environment`
- prefer linking to canonical docs instead of repeating old project pitches here
