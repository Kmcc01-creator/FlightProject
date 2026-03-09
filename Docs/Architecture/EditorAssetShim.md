# Editor Asset Shim

This project already uses Unreal's editor scripting surface, but the current automation split is uneven:

- Object existence is increasingly code-driven.
- Asset semantics still leak through editor-only authoring details.

This note captures where the engine actually creates/saves assets and how FlightProject can keep moving manual editor work behind a script-first shim.

## Engine Asset Lifecycle

For normal editor-created assets, the path is:

1. Content Browser or Python resolves an asset class/factory.
2. `IAssetTools::CreateAsset(...)` / `UAssetToolsImpl::CreateAsset(...)` sanitizes the package path and calls `CreatePackage(...)`.
3. The factory constructs the UObject via `UFactory::FactoryCreateNew(...)`.
4. The asset registry is notified with `FAssetRegistryModule::AssetCreated(...)`.
5. The package is marked dirty.
6. A later save call (`UEditorAssetLibrary` / `UEditorAssetSubsystem` / `UEditorLoadingAndSavingUtils`) pushes the package through `UPackage::SavePackage(...)`.
7. `SavePackage2` serializes to a temp file and finalizes the real `.uasset` on disk.

Practical consequence: create and save are separate phases. Most editor scripting only creates an in-memory asset plus a dirty package until an explicit save happens.

## FlightProject Pain Points

The current project already automates startup validation, schema export, Niagara existence checks, data reload, PIE tracing, and some asset creation.

The remaining manual friction is concentrated in editor-only authoring seams:

- `MassEntityConfigAsset` trait composition.
- Niagara graph internals beyond bare system creation.
- Level-authored actor setup where component/property authoring is not fully exposed to Python.
- Validation that knows an asset exists, but not whether its semantic payload is correct.

These are not fundamentally "UObject problems". They are usually one of these:

- a useful editor-only C++ method exists but is not script-exposed;
- the asset type requires a custom `UFactory`;
- the asset graph lives behind editor modules rather than runtime modules;
- the project only validates shallow existence instead of semantic contracts.

## Shim Pattern

The recommended pattern is:

1. Keep runtime systems ignorant of editor automation.
2. Expose small editor-only authoring primitives through `UFlightScriptingLibrary`.
3. Drive those primitives from Python startup/repair tools.
4. Make every shim idempotent and report issues instead of assuming clean state.

That gives a layered model:

- Python orchestrates workflows.
- `FlightScriptingLibrary` exposes stable script-callable editor hooks.
- Engine editor modules (`AssetTools`, `UnrealEd`, specialized asset editors) do the actual mutation work.

## First Shim

`UFlightScriptingLibrary::EnsureMassEntityConfigTraits(...)` is the first concrete example of this approach.

It bridges an editor-only capability already present in Unreal:

- `UMassEntityConfigAsset::AddTrait(...)`

That method exists in editor builds, but was not previously exposed to Python. FlightProject now uses a C++ shim to:

- load a Mass config asset;
- resolve requested trait classes;
- insert missing traits idempotently;
- mark the package dirty so normal save calls persist the change.

Python asset provisioning for `DA_SwarmDroneConfig` now calls this shim instead of stopping at "asset exists, configure manually".

## Next Candidates

Good next shim targets:

- Niagara system repair helpers:
  ensure required user params, data interfaces, emitters, and GPU sim flags.
- Level authoring helpers:
  populate waypoint spline points and actor defaults directly instead of leaving post-spawn editor steps.
- Project health command:
  one callable report that runs asset, schema, plugin, cvar, and semantic validation in one pass.
- Headless repair entrypoint:
  commandlet or unattended editor boot path that runs the same ensure/repair functions used interactively.

## Classification Tables

The goal is not to force all FlightProject semantics into Unreal assets.
The goal is to decide which layer owns each artifact:

- Unreal-native asset
- Unreal descriptor plus Flight-native payload
- Flight-native external/runtime artifact

### Table 1: Current FlightProject Artifact Classes

| Artifact | Recommended Form | Why | Unreal Requirement |
| --- | --- | --- | --- |
| `GameFeatureData` for `SwarmEncounter` | Unreal-native asset | Plugin activation, mounting, content discovery, editor workflows are Unreal-owned. | Must be a real asset. |
| `MassEntityConfigAsset` such as `DA_SwarmDroneConfig` | Unreal-native asset with shim repair | Mass spawning and editor authoring expect this asset type, but trait setup can be scripted. | Must stay an asset; semantic repair can be externalized. |
| Niagara systems such as `NS_SwarmVisualizer` | Unreal-native asset with shim repair | Niagara graphs, exposed params, and data interface wiring live inside Unreal editor modules. | Must stay an asset; creation/repair should be shim-driven. |
| Maps, worlds, placed actors | Unreal-native asset/state | Level loading, world partition, placement, cooking, and editor UX are Unreal-owned. | Must stay in Unreal. |
| Flight plugin/module settings and cvar profiles | Unreal descriptor plus code/schema contract | Unreal reads config and plugin state, but the desired policy should remain code/schema driven. | Unreal needs config presence; source of truth should stay in schema/code. |
| VEX source files | Flight-native external source | This is your authored language, not Unreal's. It benefits from normal text tooling and version control. | No asset required unless you want editor-native browsing wrappers. |
| VEX symbol contracts | Flight-native schema manifest | Symbol meaning belongs to FlightProject compilation/runtime policy. | Unreal only needs the resulting validated bindings. |
| Lowered HLSL text | Flight-native generated artifact | Derived compiler output, not authored content. | Unreal only needs shader registration/consumption. |
| Lowered/generated Verse text | Flight-native generated artifact | Derived backend output. | Unreal/Verse VM only needs the compiled or loaded result. |
| Scheduler/job/task graphs | Flight-native payload, optionally wrapped by thin descriptor asset | The semantics are bespoke to FlightProject orchestration. | Only use an asset wrapper if Unreal tools need to reference/select them. |
| Validation reports, manifest exports, compile diagnostics | Flight-native external artifact | These are operational outputs and should remain machine-readable files under `Saved/` or generated dirs. | No asset required. |
| Runtime swarm state, GPU buffers, execution plans | Flight-native runtime-only data | High-frequency state should not be pushed through UObject/property systems. | Keep outside asset flow entirely. |

### Table 2: Decision Rules

| If the artifact primarily exists to... | Recommended Placement |
| --- | --- |
| be referenced by Content Browser, Blueprints, maps, or cooked asset references | Unreal-native asset |
| participate in editor undo/redo, dirty tracking, save/load, or source control checkout | Unreal-native asset or editor-owned wrapper |
| drive bespoke compile/lower/schedule logic | Flight-native payload |
| store generated intermediate or final compiler output | Flight-native generated artifact |
| hold a stable Unreal-facing handle to external FlightProject data | Unreal descriptor plus Flight-native payload |
| carry high-frequency runtime state or worker-thread execution data | Flight-native runtime-only data |

### Table 3: Recommended Split For Core FlightProject Systems

| System | Unreal Layer | Flight Layer |
| --- | --- | --- |
| VEX authoring | Optional editor command surface, validation entrypoints, asset wrappers only if needed | Source language, parser, contracts, lowerings, optimizer |
| HLSL backend | Shader directory registration and shader consumption | Symbol mapping, codegen, backend policies |
| Verse backend | VM integration, entry thunks, subsystem registration | Source lowering, behavior contracts, backend orchestration |
| Swarm visualization | Niagara assets and data interfaces | Schema contract, parameter policy, repair logic |
| Swarm spawning | Mass config assets, game feature assets, world hooks | Entity semantics, trait requirements, repair/ensure logic |
| Scheduling/tasks/jobs | Thin Unreal entrypoints only when gameplay/editor systems need to call in | IR, scheduler semantics, affinity/residency rules, execution planning |

### Table 4: Minimum Safe Unreal Compliance

| Concern | Minimum Requirement |
| --- | --- |
| UObject access | Touch live UObjects only on the appropriate Unreal thread. |
| Background work | Move worker-thread execution onto plain structs, IR, and buffers instead of UObject graphs. |
| Asset persistence | Treat create and save as separate steps; dirty packages are not yet durable content. |
| Cooking | Ensure every runtime-needed artifact has an explicit cook/stage path, whether asset-backed or copied/generated. |
| References | Anything another Unreal asset must point to should have a stable UObject/asset shell. |
| Validation | Prefer `ensure`/`diff`/`repair` flows over manual editor steps. |

### Recommended Operating Model

For FlightProject, the most defensible default is:

- Keep Unreal as the host, reference, save, and cook boundary.
- Keep FlightProject as the semantic owner of schema, compilation, scheduling, and generated code.
- Add thin editor shims only where Unreal-native asset types or subsystems must be manipulated safely.

That keeps the project asset-light without becoming engine-hostile.

## Guardrails

- Keep shims narrow and composable. Do not bury large workflows in opaque editor code.
- Prefer "ensure X" and "diff X" APIs over one-shot creation APIs.
- Every shim should be safe to run repeatedly.
- Every shim should return structured issues that Python or CI can surface.
