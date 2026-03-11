# FlightProject Documentation

This tree has grown large enough that the main problem is no longer lack of information.
The main problem is distinguishing canonical guidance from historical or exploratory material.

This index is therefore organized by documentation status, not only by folder.

Use the repository [README.md](../README.md) for a short project orientation.
Use this file when you need the canonical reading order inside `Docs/`.

## Canonical Reading Paths

Use these first when you need the current project direction.

### Short Current Orientation

1. [Architecture/Overview.md](Architecture/Overview.md)
2. [Architecture/CurrentProjectVision.md](Architecture/CurrentProjectVision.md)
3. [Workflow/CurrentFocus.md](Workflow/CurrentFocus.md)
4. [Workflow/CurrentBuild.md](Workflow/CurrentBuild.md)

### Architecture Core

1. [Architecture/ProjectOrganization.md](Architecture/ProjectOrganization.md)
2. [Architecture/WorldExecutionModel.md](Architecture/WorldExecutionModel.md)
3. [Architecture/GameModeBootstrapBoundary.md](Architecture/GameModeBootstrapBoundary.md)
4. [Architecture/OrchestrationSubsystem.md](Architecture/OrchestrationSubsystem.md)
5. [Architecture/CurrentProjectVision.md](Architecture/CurrentProjectVision.md)
6. [Architecture/VexStateMutationSchemaFrame.md](Architecture/VexStateMutationSchemaFrame.md)
7. [Architecture/ActorAdapters.md](Architecture/ActorAdapters.md)
8. [Architecture/Navigation.md](Architecture/Navigation.md)
9. [Architecture/MegaKernelOrchestration.md](Architecture/MegaKernelOrchestration.md)
10. [Architecture/DataPipeline.md](Architecture/DataPipeline.md)
11. [Architecture/GpuResourceSchemaContract.md](Architecture/GpuResourceSchemaContract.md)

### Workflow / Current Status

Use these for what is active right now, with dates and execution priorities.

- [Workflow/CurrentFocus.md](Workflow/CurrentFocus.md)
- [Workflow/CurrentBuild.md](Workflow/CurrentBuild.md)
- [Workflow/OrchestrationImplementationPlan.md](Workflow/OrchestrationImplementationPlan.md)
- [Workflow/SchemaIrImplementationPlan.md](Workflow/SchemaIrImplementationPlan.md)
- [Workflow/GameplaySystems.md](Workflow/GameplaySystems.md)
- [Workflow/DocumentationConsolidationPlan.md](Workflow/DocumentationConsolidationPlan.md)

## Status Guide

Treat documents using these categories:

- Canonical:
  current architecture or implementation direction; should align with the active codebase.
- Operational:
  dated workflow/build/test status documents such as `CurrentFocus.md` and `CurrentBuild.md`.
- Reference:
  still useful subsystem detail, but not the best starting point for current direction.
- Exploratory / Historical:
  concept documents, migration notes, or older proposals that may preserve useful ideas but can contain stale assumptions or speculative syntax.

If two docs disagree, prefer:

1. `CurrentFocus.md` / `CurrentBuild.md` for dated status
2. `CurrentProjectVision.md`, `WorldExecutionModel.md`, and `ProjectOrganization.md` for architectural direction
3. the more recent implementation plan for active migration work

## Folder Guide

### Architecture

Primary design docs and subsystem contracts.

Directory entrypoint:

- [Architecture/README.md](Architecture/README.md)

Start with:

- [Architecture/Overview.md](Architecture/Overview.md)
- [Architecture/CurrentProjectVision.md](Architecture/CurrentProjectVision.md)
- [Architecture/WorldExecutionModel.md](Architecture/WorldExecutionModel.md)
- [Architecture/OrchestrationSubsystem.md](Architecture/OrchestrationSubsystem.md)

Use as current subsystem references when relevant:

- [Architecture/Navigation.md](Architecture/Navigation.md)
- [Architecture/ActorAdapters.md](Architecture/ActorAdapters.md)
- [Architecture/EditorAssetShim.md](Architecture/EditorAssetShim.md)
- [Architecture/TargetCapabilitySchema.md](Architecture/TargetCapabilitySchema.md)
- [Architecture/CompilerArtifactTesting.md](Architecture/CompilerArtifactTesting.md)
- [Architecture/AssemblyInspectionModels.md](Architecture/AssemblyInspectionModels.md)
- [Architecture/SimpleSCSLShaderPipeline.md](Architecture/SimpleSCSLShaderPipeline.md)

Treat as historical or exploratory reference:

- [Architecture/HistoricalVexVerseConcepts.md](Architecture/HistoricalVexVerseConcepts.md) (Consolidated exploratory VEX/Verse runtime models)
- [Architecture/SCSL_Engine.md](Architecture/SCSL_Engine.md)
- [Architecture/SCSL_FieldResidencySchemaContract.md](Architecture/SCSL_FieldResidencySchemaContract.md)
- `SIMD_HLSL_INTRINSICS.md`
- `SIMD_Strategy_ImplementationPlan.md`
- `FieldTheoreticStylization.md`


### Workflow

Day-to-day implementation notes, rollout plans, and dated status.

Directory entrypoint:

- [Workflow/README.md](Workflow/README.md)

Canonical operational docs:

- [Workflow/CurrentFocus.md](Workflow/CurrentFocus.md)
- [Workflow/CurrentBuild.md](Workflow/CurrentBuild.md)
- [Workflow/TestingValidationPlan.md](Workflow/TestingValidationPlan.md)
- [Workflow/OrchestrationImplementationPlan.md](Workflow/OrchestrationImplementationPlan.md)
- [Workflow/SchemaIrImplementationPlan.md](Workflow/SchemaIrImplementationPlan.md)
- [Workflow/GameplaySystems.md](Workflow/GameplaySystems.md)

Historical or narrow-scope references:

- `CurrentFocus-VexRendering.md`
- `StartPlayDecomposition.md`
- `PIE_Spawn.md`
- `Phase0ImplementationChecklist.md`
- `Phase2_ModularParser_PostMortem.md`

### Environment

Build, configuration, and local setup documentation.

Directory entrypoint:

- [Environment/README.md](Environment/README.md)

Start with:

- [Environment/BuildAndRegen.md](Environment/BuildAndRegen.md)
- [Environment/Configuration.md](Environment/Configuration.md)
- [Environment/Troubleshooting.md](Environment/Troubleshooting.md)

### Scripting

Automation and tool-facing surfaces.

Directory entrypoint:

- [Scripting/README.md](Scripting/README.md)

Useful current references:

- [Scripting/VexSchemaValidation.md](Scripting/VexSchemaValidation.md)
- [Scripting/VerseAssemblerScaffold.md](Scripting/VerseAssemblerScaffold.md)
- [Scripting/VexUiScaffold.md](Scripting/VexUiScaffold.md)

### Future / Extensions / DevNotes

These are not canonical starting points.
Use them as proposal, archive, or research material unless a current doc links to them directly.

Directory entrypoints:

- [Archives/README.md](Archives/README.md)
- [Future/README.md](Future/README.md)
- [Extensions/README.md](Extensions/README.md)
- [DevNotes/README.md](DevNotes/README.md)
- [Reviews/README.md](Reviews/README.md)

## Documentation Maintenance Rules

- Keep the repository `README.md` short and current; treat it as the outer orientation surface, not a historical pitch deck.
- Keep dated build/test evidence in `Workflow/CurrentBuild.md`.
- Keep active milestones, risks, and next steps in `Workflow/CurrentFocus.md`.
- Keep `Architecture/Overview.md` short and current; do not let it drift into a historical project pitch.
- When an older concept doc is still worth keeping, label it as exploratory or historical instead of silently presenting it as current.
- Prefer linking to one canonical document rather than copying the same architectural story into multiple files.
