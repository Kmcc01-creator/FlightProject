# FlightProject Documentation

This tree has grown large enough that the main problem is no longer lack of information.
The main problem is distinguishing canonical guidance from historical or exploratory material.

This index is therefore organized by documentation status, not only by folder.

## Canonical Reading Paths

Use these first when you need the current project direction.

### Architecture Core

1. [Architecture/ProjectOrganization.md](Architecture/ProjectOrganization.md)
2. [Architecture/WorldExecutionModel.md](Architecture/WorldExecutionModel.md)
3. [Architecture/GameModeBootstrapBoundary.md](Architecture/GameModeBootstrapBoundary.md)
4. [Architecture/OrchestrationSubsystem.md](Architecture/OrchestrationSubsystem.md)
5. [Architecture/CurrentProjectVision.md](Architecture/CurrentProjectVision.md)
6. [Architecture/VexStateMutationSchemaFrame.md](Architecture/VexStateMutationSchemaFrame.md)
7. [Architecture/MegaKernelOrchestration.md](Architecture/MegaKernelOrchestration.md)
8. [Architecture/DataPipeline.md](Architecture/DataPipeline.md)
9. [Architecture/GpuResourceSchemaContract.md](Architecture/GpuResourceSchemaContract.md)

### Workflow / Current Status

Use these for what is active right now, with dates and execution priorities.

- [Workflow/CurrentFocus.md](Workflow/CurrentFocus.md)
- [Workflow/CurrentBuild.md](Workflow/CurrentBuild.md)
- [Workflow/OrchestrationImplementationPlan.md](Workflow/OrchestrationImplementationPlan.md)
- [Workflow/SchemaIrImplementationPlan.md](Workflow/SchemaIrImplementationPlan.md)

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

Start with:

- [Architecture/Overview.md](Architecture/Overview.md)
- [Architecture/CurrentProjectVision.md](Architecture/CurrentProjectVision.md)
- [Architecture/WorldExecutionModel.md](WorldExecutionModel.md)
- [Architecture/OrchestrationSubsystem.md](OrchestrationSubsystem.md)

Use as current subsystem references when relevant:

- [Architecture/Navigation.md](Architecture/Navigation.md)
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

Canonical operational docs:

- [Workflow/CurrentFocus.md](Workflow/CurrentFocus.md)
- [Workflow/CurrentBuild.md](Workflow/CurrentBuild.md)
- [Workflow/TestingValidationPlan.md](Workflow/TestingValidationPlan.md)
- [Workflow/OrchestrationImplementationPlan.md](Workflow/OrchestrationImplementationPlan.md)
- [Workflow/SchemaIrImplementationPlan.md](Workflow/SchemaIrImplementationPlan.md)

Historical or narrow-scope references:

- `CurrentFocus-VexRendering.md`
- `StartPlayDecomposition.md`
- `PIE_Spawn.md`
- `Phase0ImplementationChecklist.md`
- `Phase2_ModularParser_PostMortem.md`

### Environment

Build, configuration, and local setup documentation.

Start with:

- [Environment/BuildAndRegen.md](Environment/BuildAndRegen.md)
- [Environment/Configuration.md](Environment/Configuration.md)
- [Environment/Troubleshooting.md](Environment/Troubleshooting.md)

### Scripting

Automation and tool-facing surfaces.

Useful current references:

- [Scripting/VexSchemaValidation.md](Scripting/VexSchemaValidation.md)
- [Scripting/VerseAssemblerScaffold.md](Scripting/VerseAssemblerScaffold.md)
- [Scripting/VexUiScaffold.md](Scripting/VexUiScaffold.md)

### Future / Extensions / DevNotes

These are not canonical starting points.
Use them as proposal, archive, or research material unless a current doc links to them directly.

## Documentation Maintenance Rules

- Keep dated build/test evidence in `Workflow/CurrentBuild.md`.
- Keep active milestones, risks, and next steps in `Workflow/CurrentFocus.md`.
- Keep `Architecture/Overview.md` short and current; do not let it drift into a historical project pitch.
- When an older concept doc is still worth keeping, label it as exploratory or historical instead of silently presenting it as current.
- Prefer linking to one canonical document rather than copying the same architectural story into multiple files.
