# Current Extension Directions

This document consolidates the `Docs/Extensions` folder around the current project vision.

Use this file first when you want to understand which extension avenues still fit FlightProject's active architecture and which ones should remain exploratory/reference material.

For the architectural source of truth, see:

- [../Architecture/CurrentProjectVision.md](../Architecture/CurrentProjectVision.md)
- [../Architecture/Overview.md](../Architecture/Overview.md)
- [../Architecture/ActorAdapters.md](../Architecture/ActorAdapters.md)
- [../Architecture/OrchestrationSubsystem.md](../Architecture/OrchestrationSubsystem.md)
- [../Workflow/CurrentFocus.md](../Workflow/CurrentFocus.md)

## 1. The Current Lens

The current project vision is:

- authored intent becomes a contract
- contracts bind against reflected schemas and world context
- orchestration selects legal execution paths
- runtime systems lower those decisions into concrete execution
- reports preserve what was chosen and why

That means extension work is most valuable when it strengthens one of these layers:

- authoring ergonomics
- binding/validation tooling
- runtime observability
- editor/service productivity
- adjacent framework experiments that can clarify the contract/binding/lowering model

## 2. Active Extension Tracks

### Track A: Editor And Tooling Productivity

These docs still map cleanly to current project needs:

- [EditorServiceArchitecture.md](EditorServiceArchitecture.md)
- [EditorToolsGuide.md](EditorToolsGuide.md)
- [PythonReflectionGuide.md](PythonReflectionGuide.md)

Why they still matter:

- the project continues to rely on editor automation and tooling to reduce authoring cost
- reflection and scripting remain important bridges into schema-driven workflows
- service/runtime setup on local machines still affects iteration speed directly

Recommended use:

- keep as practical reference material
- refresh only when engine/tooling workflows materially change

### Track B: Workflow And Ergonomics Reference

These docs have useful ideas, but they are no longer the canonical expression of project architecture:

- [ArchitectureAndWorkflow.md](ArchitectureAndWorkflow.md)
- [SoloDeveloperWorkflow.md](SoloDeveloperWorkflow.md)

Why:

- they come from an earlier “Mass over Actors” transition period
- the current project story is now better expressed in `Architecture/` and `Workflow/`
- they still capture useful ergonomics for solo iteration, editor layout, and layered authoring

Recommended use:

- treat as reference/supporting material
- do not use as the first architectural entrypoint

### Track C: Exploratory Framework Integrations

These are interesting because they resonate with the current project vision, but they are still exploratory:

- [M2FrameworkIntegration.md](M2FrameworkIntegration.md)
- [MacrokidBehavioralSynthesis.md](MacrokidBehavioralSynthesis.md)

Why they are still relevant:

- `M2FrameworkIntegration.md` aligns with the project's interest in explicit structure, traversal, pattern matching, and staged transformation
- `MacrokidBehavioralSynthesis.md` aligns with field-mediated coordination, layered state, and scalable behavioral composition

Why they are not active plans yet:

- neither is currently part of the main runtime path
- neither has an implementation plan promoted into `Workflow/`
- both still need a narrower “what concrete FlightProject problem does this solve next?” framing

## 3. Promotion Criteria

An extension concept should move from reference/exploratory into active workflow only when:

1. it solves a current project pain point clearly
2. it maps onto the contract/binding/lowering/report model
3. it has a concrete first slice that can be verified in code or automation
4. it does not require inventing a parallel architecture to the one already landing

## 4. Recommended Next Promotions

The most realistic extension promotions right now are:

1. editor/service productivity improvements
   - DDC/editor service reliability
   - editor utility workflows
   - Python/reflection-backed tooling
2. authoring-cost reduction
   - low-click schema-driven setup
   - stronger editor validation surfaces
3. runtime observability extensions
   - richer visualization/debug layers tied to orchestration and schema/runtime reports

The less immediate promotions are:

- M2 integration
- Macrokid-inspired behavioral architecture

Those need narrower bridge docs before they should drive implementation.
