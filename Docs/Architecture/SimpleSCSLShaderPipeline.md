# SimpleSCSL Shader Pipeline

Status: reference document for an active testing/development workflow tool.
This pipeline is integrated with [OrchestrationSubsystem.md](OrchestrationSubsystem.md) as a render adapter for workflow preview and diagnostics.

## Intent

`UFlightSimpleSCSLShaderPipelineSubsystem` is a lightweight rendering adapter for testing and development workflows.

It exists to provide:

- a minimal post-process entry point for SCSL-style shading experiments
- an orchestration-visible rendering surface that can be toggled without reactivating the heavier swarm render stack
- a stable preview path for intrinsic workflow development, diagnostics, and future post-processing experiments

## Runtime Shape

- World-scoped owner: `UFlightSimpleSCSLShaderPipelineSubsystem`
- Render hook: `FSceneViewExtensionBase`
- Post-process phase: `EPostProcessingPass::Tonemap`
- Shader: `/FlightProject/Private/FlightSimpleSCSLPostProcess.usf`

The subsystem owns a tiny config surface:

- enable/disable
- tint
- blend weight
- posterization steps
- edge emphasis
- scanline weight
- time scale

The implementation is intentionally shallow:

- it consumes scene color
- applies a simple stylized fullscreen pass
- returns a new scene-color target

This keeps the pipeline cheap to reason about and safe to use in headless-adjacent workflow development without reintroducing the more brittle swarm-specific resolve path.

## Orchestration Role

The pipeline is modeled as a render adapter inside `UFlightOrchestrationSubsystem`.

It contributes:

- service status for `UFlightSimpleSCSLShaderPipelineSubsystem`
- a render-adapter participant with `PostProcessTonemap`, `SimpleSCSL`, and workflow-preview capabilities
- an optional execution-plan output consumer when the pipeline is enabled

This keeps rendering experimentation visible in the same report and plan surface as behaviors, cohorts, waypoint paths, and spatial fields.

## Next Extensions

- promote config to a data asset or developer settings profile
- add optional material/post-process blendable interop
- allow behavior outputs to drive tint or weight channels through orchestration bindings
- stage a second pass for field visualization or Vex diagnostics overlays
