# FlightProject Overview

This document is a short orientation surface for the current project shape.
It should stay aligned with the active architecture, not preserve every historical phase of the project.

For the current architectural source of truth, start with:

1. [ProjectOrganization.md](ProjectOrganization.md)
2. [WorldExecutionModel.md](WorldExecutionModel.md)
3. [GameModeBootstrapBoundary.md](GameModeBootstrapBoundary.md)
4. [OrchestrationSubsystem.md](OrchestrationSubsystem.md)
5. [CurrentProjectVision.md](CurrentProjectVision.md)
6. [ActorAdapters.md](ActorAdapters.md)
7. [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md)

## Current Identity

FlightProject is converging on a schema-bound execution architecture built on Unreal Engine.

The project is no longer best described as:

- an aircraft sandbox centered on pawn logic
- a Behavior Tree / State Tree integration project
- a GPU-only rendering or simulation experiment

The better description is:

- reflected types and schemas define legal state contracts
- VEX binds authored behavior against those contracts
- orchestration selects legal executable paths for world-scoped work
- runtime services commit those paths and report what was chosen

## Current System Center

The strongest current center of gravity is:

- `UFlightOrchestrationSubsystem` as the world-scoped coordination surface
- `FVexTypeSchema` and `FVexSchemaOrchestrator` as the schema contract surface
- `UFlightVerseSubsystem` as the compile/execute service for VEX-derived behavior
- schema-bound reports, compile artifacts, and validation outputs as the visible evidence layer

This means the most important project seams are now:

- contract definition
- binding
- lowering
- execution selection
- commit
- report

## Practical Reading Map

If your task is mostly about:

- project structure and ownership:
  [ProjectOrganization.md](ProjectOrganization.md)
- world/runtime composition:
  [WorldExecutionModel.md](WorldExecutionModel.md)
- startup/bootstrap boundaries:
  [GameModeBootstrapBoundary.md](GameModeBootstrapBoundary.md)
- orchestration:
  [OrchestrationSubsystem.md](OrchestrationSubsystem.md)
- actor-backed runtime ingress and lowering:
  [ActorAdapters.md](ActorAdapters.md)
- VEX/compiler direction:
  [CurrentProjectVision.md](CurrentProjectVision.md),
  [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md),
  [../Workflow/SchemaIrImplementationPlan.md](../Workflow/SchemaIrImplementationPlan.md)
- data contracts and resource/schema direction:
  [DataPipeline.md](DataPipeline.md),
  [GpuResourceSchemaContract.md](GpuResourceSchemaContract.md)

## Documentation Policy

Use this file only as a current orientation layer.

- Do not let it accumulate stale subsystem inventories.
- Do not let it act as a second copy of `CurrentProjectVision.md`.
- Move dated build/test status into `Docs/Workflow/CurrentBuild.md`.
- Move milestone and near-term execution status into `Docs/Workflow/CurrentFocus.md`.
- Keep older concept material in dedicated reference docs and label it clearly.
