# Architecture

This directory holds the current subsystem and runtime-model docs for FlightProject.

Use this folder when you need architectural direction rather than dated implementation status.

## Start Here

1. [Overview.md](Overview.md)
2. [CurrentProjectVision.md](CurrentProjectVision.md)
3. [ProjectOrganization.md](ProjectOrganization.md)
4. [WorldExecutionModel.md](WorldExecutionModel.md)
5. [OrchestrationSubsystem.md](OrchestrationSubsystem.md)

## Current Core Docs

- [ActorAdapters.md](ActorAdapters.md)
  Actor-backed ingress surfaces and runtime lowering rules.
- [BehaviorComposition.md](BehaviorComposition.md)
  Composite behavior direction for VEX/Verse execution and the orchestration/runtime boundary.
- [Navigation.md](Navigation.md)
  Navigation candidates, legality, commit products, and current runtime lowering.
- [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md)
  Mutation-centered VEX/compiler frame.
- [SimdBackendContract.md](SimdBackendContract.md)
  Contract boundary between vector-shaped execution and provable hardware SIMD backends.
- [VexArbitrarySymbolVectorContract.md](VexArbitrarySymbolVectorContract.md)
  Schema-bound vector packing contract for arbitrary VEX symbols, adapters, and kernel variants.
- [VerseSubsystemModularization.md](VerseSubsystemModularization.md)
  Refactor plan for keeping `UFlightVerseSubsystem` as a facade while extracting compile, storage-host, Mass bridge, composite, and VM implementation seams.
- [CompiledFragmentRequirementReporting.md](CompiledFragmentRequirementReporting.md)
  Current fragment-dependency reporting surface and the path from schema-bound fragment facts to a compiled `MassQueryContract`.
- [EcsViewsAsOpticLikeProjections.md](EcsViewsAsOpticLikeProjections.md)
  Reference note connecting ECS view generation, schema-bound symbol projection, host bundles, and optics-inspired structural traversal.
- [ClangSimdBackendImplementation.md](ClangSimdBackendImplementation.md)
  Unreal/clang structure for ISA-specific SIMD kernels, UBT policy, and ratified dispatch.
- [MegaKernelOrchestration.md](MegaKernelOrchestration.md)
  GPU mega-kernel coordination layer.
- [GpuComputeFrameworkRefinement.md](GpuComputeFrameworkRefinement.md)
  GPU scheduling, latency classes, ownership boundaries, and refinement path.
- [BoundaryAwareIrCandidates.md](BoundaryAwareIrCandidates.md)
  Candidate IR and SchemaIR node/op names for boundary-aware lowering.
- [GpuResourceSchemaContract.md](GpuResourceSchemaContract.md)
  GPU resource/schema contract direction.
- [DataPipeline.md](DataPipeline.md)
  Data ingress and contract flow.

## Historical / Exploratory Material

Some docs here preserve older or more speculative directions and should not be used as the first source of truth.

Examples:

- [HistoricalVexVerseConcepts.md](HistoricalVexVerseConcepts.md)
- [SCSL_Engine.md](SCSL_Engine.md)
- [FieldTheoreticStylization.md](FieldTheoreticStylization.md)
- [SIMD_Strategy_ImplementationPlan.md](SIMD_Strategy_ImplementationPlan.md)

Prefer `Overview.md` and `CurrentProjectVision.md` when older docs disagree.
