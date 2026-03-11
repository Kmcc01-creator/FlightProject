# Exploration Map

This document consolidates the `Docs/Future` directory into a clearer current state.

Use it to understand which long-range ideas still fit the current project vision and what would have to happen for them to move into active work.

For the current architectural frame, see:

- [../Architecture/CurrentProjectVision.md](../Architecture/CurrentProjectVision.md)
- [../Architecture/Navigation.md](../Architecture/Navigation.md)
- [../Architecture/OrchestrationSubsystem.md](../Architecture/OrchestrationSubsystem.md)
- [../Workflow/CurrentFocus.md](../Workflow/CurrentFocus.md)

## 1. What This Folder Is For

`Docs/Future` is for long-range idea storage.

These docs are useful when:

- the project needs a place to preserve bigger ideas
- a concept is not yet narrow enough for `Workflow/`
- an idea still needs to prove its architectural fit

These docs should not be treated as active plans by default.

## 2. Current Exploratory Avenues

### Avenue A: Hegetic Interface

Document:

- [HegeticInterface.md](HegeticInterface.md)

Best current reading:

- a visualization and interaction layer for structured runtime data
- potentially useful for diagnostics, telemetry, and operator-facing interfaces

Why it fits the project vision:

- it depends on explicit data adapters and normalized contracts
- it would benefit from orchestration/report surfaces
- it aligns with the existing push toward observable, explainable runtime state

Why it is not active yet:

- it still needs a narrow first production problem
- it should likely begin as a diagnostics/reporting layer rather than a broad interface framework

Best promotion path:

1. bind it to orchestration/navigation/runtime diagnostics
2. prove one concrete visualization slice
3. move the implementation plan into `Workflow/`

### Avenue B: Level Loader

Document:

- [LevelLoader.md](LevelLoader.md)

Best current reading:

- a composable world/mission transition framework
- potentially useful once world transitions, mission orchestration, or persistent-map workflows become more central

Why it fits the project vision:

- it is naturally subsystem-oriented
- it could become another contract/binding/lowering/report surface
- it aligns with reusable world/bootstrap/orchestration boundaries

Why it is not active yet:

- the project's immediate center is still schema/orchestration/runtime commit rather than level-transition infrastructure
- it needs a clearer connection to current world execution and mission needs

Best promotion path:

1. define the first real transition use case
2. connect it to current world/bootstrap/orchestration responsibilities
3. move the active implementation plan into `Workflow/`

## 3. Promotion Rules

A future doc should move into active work only when:

1. it solves a concrete problem in the current codebase
2. it maps cleanly onto the current architecture
3. it has a narrow first slice with visible verification
4. it is no longer “idea storage,” but an actual implementation candidate

## 4. Current Recommendation

Right now:

- keep `HegeticInterface.md` as a future-facing diagnostics/visualization concept
- keep `LevelLoader.md` as a future-facing world-transition concept
- do not let either compete with the current `Architecture/` and `Workflow/` docs for attention

If one becomes active, create or update a concrete `Workflow/` plan and link back to the exploratory doc as background rather than trying to turn the exploratory doc into the implementation plan itself.
