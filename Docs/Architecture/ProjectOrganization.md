# Project Organization

This document establishes a project-organization basis for FlightProject.

The goal is not cosmetic consistency.
The goal is to make the World Execution Model, editor shims, schema surfaces, and the future orchestration subsystem fit together without creating another catch-all layer.

The short version:

- organize by owned domain, not by one-off task
- keep Unreal-facing boundary types thin
- keep simulation and compiler logic in their owned domains
- use orchestration as the coordination nexus, not as a duplicate simulation engine
- separate early runtime/render startup from editor-only tooling startup
- keep generated artifacts out of `Content/` unless Unreal must discover or cook them

## 1. Organization Principles

### 1.1 Domain First

Code should live with the domain that owns its meaning:

- `Schema/` owns schema contracts and manifest logic
- `Vex/` owns parsing, IR, lowering, optimizer, and compile-time language logic
- `Verse/` owns Verse VM bridge and execution-facing behavior runtime
- `Mass/` owns ECS fragments, traits, and processors
- `Swarm/` owns swarm simulation and render bridge state
- `Spatial/` owns spatial field registration and queries
- `Orchestration/` should own world-level coordination, bindings, plans, and reports

Do not place new runtime types at the module root if they clearly belong to one of these domains.

### 1.2 Root Is For Module Spine And Cross-Domain Entry Surfaces

The root of `Source/FlightProject/Public` and `Private` should stay small.

Root-level types are appropriate when they are:

- module entry points
- core game-framework entry points
- cross-domain boundary surfaces used everywhere
- temporary legacy locations pending migration into a domain folder

Examples that fit the root today:

- `FlightProject.h/.cpp`
- `FlightGameMode.h/.cpp`
- `FlightGameState.h/.cpp`
- `FlightPlayerController.h/.cpp`
- `FlightProjectDeveloperSettings.h/.cpp`
- `FlightScriptingLibrary.h/.cpp`

Everything else should prefer a domain folder.

### 1.3 World Scope Is A Lifetime Boundary, Not A Domain Name

`UWorld` matters because it defines lifecycle and visibility boundaries.
It should not become a catch-all naming prefix for every coordination type.

That means:

- use `UWorldSubsystem` when world lifetime is correct
- name the owned concept directly
- avoid inventing a vague `World*` folder as a second junk drawer

Recommended:

- `Orchestration/FlightOrchestrationSubsystem.h`
- `Orchestration/FlightExecutionPlan.h`
- `Orchestration/FlightParticipantRecord.h`

Avoid:

- `World/FlightWorldManager.h`
- `World/FlightWorldCoordinator.h`

`WorldBootstrap` is still a meaningful term because bootstrap is its own boundary concern.
The future nexus point should still be `Orchestration`, not a generic `World` bucket.

### 1.4 Orchestration Coordinates, Domains Execute

The orchestration subsystem should know:

- which services are available
- which participants are visible
- which contracts are satisfied
- which behaviors are bound
- which execution plans are active

It should not become the owner of:

- Mass entity internals
- Verse runtime internals
- GPU render passes
- spatial math implementation
- authored asset repair details

That ownership stays in the domain folders.

### 1.5 Separate Source, Generated, And Runtime Outputs

FlightProject is increasingly code-first and schema-first.
That requires a clean split between authored inputs and derived outputs.

Recommended rule:

- authored Unreal content lives in `Content/`
- authored code and contracts live in `Source/`, `Docs/`, and plain-text inputs
- generated non-asset outputs live outside `Content/`, preferably under `Saved/Flight/`

Examples of generated outputs that should not become normal assets by default:

- lowered HLSL text
- generated Verse text
- scheduler or job plans
- validation reports
- debug manifests
- cache files

### 1.6 Module Separation Is A Lifecycle Boundary

Module separation is not just a packaging preference.
It is how FlightProject avoids mixing systems that require incompatible Unreal startup phases.

Current policy:

- `FlightProject` is the runtime module.
- `FlightProject` should own early startup concerns such as shader directory registration, runtime subsystem registration, logging services, and other code that must be available by `PostConfigInit`.
- `FlightProjectEditor` is the editor module.
- `FlightProjectEditor` should own tabs, menus, tool registration, editor widgets, editor utility helpers, and other systems that depend on editor UI state and should load after engine/editor startup.

Practical rule:

- if a type depends on `ToolMenus`, `LevelEditor`, `WorkspaceMenuStructure`, `FAppStyle`, or similar editor UI systems, it belongs in `FlightProjectEditor`
- if a type must exist for command-line startup, shader registration, runtime worlds, cooked builds, or non-editor execution, it belongs in `FlightProject`

Do not solve module-boundary problems with ad hoc guards alone.
Guarding a call site is useful, but it does not justify keeping incompatible runtime and editor concerns in the same module.

Shared services that cross the module boundary should not rely on header-only "singletons".
If both modules need the same runtime-owned state, define it once in a `.cpp` owned by the runtime module and export the API surface.

## 2. Top-Level Project Map

| Location | Purpose | Notes |
| --- | --- | --- |
| `Source/FlightProject/` | Runtime module code and public API surfaces | Organize by owned domain. |
| `Source/FlightProjectEditor/` | Editor module code and tooling-only surfaces | Own tabs, menus, editor widgets, and late editor startup hooks. |
| `Content/` | Unreal-authored assets and editor-facing content | Keep descriptor-like assets here, not arbitrary generated payloads. |
| `Content/Python/FlightProject/` | Editor automation, validation, asset ensure flows | Use as orchestration glue for editor-time workflows. |
| `Docs/Architecture/` | Stable architecture intent and target-state guidance | Prefer durable noun-based document names. |
| `Docs/Workflow/` | Transitional plans, decomposition notes, current focus | Use for stepwise migration and execution plans. |
| `Plugins/` | Engine/plugin extension surfaces with clear ownership | Use when a system has plugin-level packaging or dependency boundaries. |
| `Shaders/` | Authored shader sources that Unreal compiles | Keep generated shader intermediates elsewhere. |
| `Scripts/` | Build/test/helper automation | Prefer explicit task names over generic helper scripts. |
| `Saved/Flight/` | Recommended home for generated external FlightProject artifacts | Keep source/generated/runtime outputs separate. |

## 3. C++ Folder Ownership

### 3.1 Current Good Domains

The current domain split is already useful:

- `Core/`
- `Diagnostics/`
- `IoUring/`
- `Mass/`
- `Modeling/`
- `Platform/`
- `Schema/`
- `Spatial/`
- `Swarm/`
- `UI/`
- `Verse/`
- `Vex/`

The main missing folder is:

- `Orchestration/`

### 3.2 Recommended Home For The Nexus Point

The future coordination surface should live here:

```text
Source/FlightProject/Public/Orchestration/
Source/FlightProject/Private/Orchestration/
```

That folder should eventually own types such as:

- `UFlightOrchestrationSubsystem`
- `FFlightParticipantRecord`
- `FFlightParticipantHandle`
- `FFlightBehaviorBinding`
- `FFlightCohortBinding`
- `FFlightExecutionPlan`
- `FFlightExecutionPlanStep`
- `FFlightOrchestrationReport`

World-scoped coordination types should cluster there even if they touch several domains.

### 3.3 Placement Rules

| If A Type Primarily Owns... | Put It In... |
| --- | --- |
| module startup, game mode, player/controller, broad project settings | module root |
| schema manifest, contract rows, validation schema | `Schema/` |
| parsing, AST, IR, lowering, optimizer, compile diagnostics | `Vex/` |
| Verse bridge, executable behavior runtime, VM contracts | `Verse/` |
| Mass traits, fragments, processors, entity-facing ECS logic | `Mass/` |
| swarm simulation state, render bridge resources, drone contracts | `Swarm/` |
| spatial field registration and query logic | `Spatial/` |
| world-level coordination, visibility, bindings, execution plans, reports | `Orchestration/` |
| runtime UI/data-binding surfaces shared with runtime code | `UI/` in `FlightProject` |
| editor tabs, menu extensions, and editor-only widgets | `FlightProjectEditor/` |
| test-only helpers and automation fixtures | `Private/Tests/` |

### 3.4 Root-Level Cleanup Direction

Some existing root-level files are valid long term.
Some are better treated as migration candidates.

Good long-term root residents:

- `FlightProject`
- `FlightGameMode`
- `FlightGameState`
- `FlightPlayerController`
- `FlightProjectDeveloperSettings`
- `FlightScriptingLibrary`

Likely migration candidates as orchestration matures:

- `FlightWorldBootstrapSubsystem`
- `FlightNavGraphDataHubSubsystem`
- other world-scoped coordination surfaces that are not true game-framework roots

That migration does not need to happen immediately.
The important part is to stop adding new coordination types at the root.

### 3.5 Current TODO: Editor-Only UI Migration

The module split is now in place, but one cleanup item remains:

- TODO: move editor-only UI registration/types that still live under `Source/FlightProject/Public/UI/` into `Source/FlightProjectEditor/`

This includes the tab/menu layer around:

- `FlightLogTab`
- `SwarmOrchestratorTab`
- other editor-only UI wrappers that do not belong on the runtime module's public surface

The target end state is:

- runtime log capture / reactive data surfaces stay in `FlightProject`
- editor docking tabs and menu plumbing move to `FlightProjectEditor`

## 4. Naming Conventions

### 4.1 Type Name Shape

Use Unreal prefixes normally:

- `U` for UObject-derived types
- `A` for actors
- `F` for plain structs
- `I` for interfaces
- `S` for Slate widgets
- `E` for enums

After the Unreal prefix, use:

```text
Flight + Domain + Role
```

Examples:

- `UFlightSwarmSubsystem`
- `UFlightOrchestrationSubsystem`
- `FFlightExecutionPlan`
- `FFlightBehaviorBinding`
- `IFlightParticipantProvider`

Avoid names that lead with the role and hide the owned concept.

Prefer:

- `FlightSwarmSubsystem`
- `FlightSpatialSubsystem`
- `FlightRequirementRegistry`

Avoid:

- `FlightSubsystemSwarm`
- `FlightRegistryRequirements`

### 4.2 File Name Rule

File names should match the primary type exactly.

Examples:

- `FlightOrchestrationSubsystem.h`
- `FlightExecutionPlan.h`
- `FlightBehaviorBinding.h`

Do not hide several unrelated concepts behind one file name like `FlightUtils.h` or `FlightHelpers.h`.

### 4.3 Namespace Rule

Use `Flight::<Domain>` for non-UObject runtime code and compile/runtime helper types.

Examples:

- `Flight::Vex`
- `Flight::Swarm`
- `Flight::Schema`
- `Flight::Orchestration`

This keeps plain-data and compiler/runtime code from collapsing into the UObject namespace surface.

### 4.4 Suffix Semantics

Use suffixes consistently so names communicate ownership and lifecycle.

| Suffix | Meaning | Example |
| --- | --- | --- |
| `Subsystem` | world/game-instance/editor service boundary | `UFlightSwarmSubsystem` |
| `Registry` | owned visibility/index of providers or authored sources | `UFlightWaypointPathRegistry` |
| `Provider` | explicit source of data or participation | `IFlightParticipantProvider` |
| `Source` | authored or external input surface | `AFlightSpatialLayoutSourceComponent` |
| `Descriptor` | plain resolved input record | `FFlightSpawnDescriptor` |
| `Binding` | relationship between behavior/contract/cohort/domain | `FFlightBehaviorBinding` |
| `Plan` | resolved execution or orchestration plan | `FFlightExecutionPlan` |
| `Snapshot` | read-only frame or report capture | `FFlightRenderSnapshot` |
| `Contract` | required schema/runtime agreement | `FFlightRenderContract` |
| `Report` | human/machine-readable observability output | `FFlightOrchestrationReport` |
| `Bridge` | boundary adapter across systems/APIs | `FlightGpuIoUringBridge` |
| `Adapter` | presentation or integration adapter | `FlightRenderAdapter` |

### 4.5 Names To Avoid By Default

Avoid these unless there is a very clear reason:

- `Manager`
- `Utils`
- `Helper`
- `Data`
- `System`

These names usually hide ownership.

Prefer a name that tells the reader:

- what it owns
- what boundary it lives at
- whether it is runtime state, a descriptor, a binding, or a report

### 4.6 Use "Orchestrator" Carefully

For runtime coordination, prefer `Subsystem`, `Plan`, `Binding`, and `Report`.

Reserve `Orchestrator` for cases where the type is explicitly a tool or UI surface.
That keeps names like `SSwarmOrchestrator` understandable as editor/UI tooling while the runtime nexus stays `UFlightOrchestrationSubsystem`.

## 5. Registration And Visibility Lexicon

Because FlightProject is moving toward explicit registration and schema-driven visibility, these terms need stable meanings.

| Term | Meaning | Recommended Home |
| --- | --- | --- |
| participant | any world-visible thing that can be coordinated | `Orchestration/` |
| cohort | a logical execution group, not necessarily one actor or one Mass entity | `Orchestration/` |
| registry | an owned index of visible providers or authored sources | domain folder or `Orchestration/` if cross-domain |
| descriptor | plain reduced record built from authored/runtime state | owning domain |
| contract | what must be true for legality or interoperability | `Schema/` |
| binding | resolved link between behavior, cohort, and execution domain | `Orchestration/` |
| plan | resolved per-world execution shape | `Orchestration/` |
| snapshot | read-only view handed to rendering/diagnostics | owning domain |
| report | exported state of visibility, health, or planning | `Diagnostics/` or `Orchestration/` |

This is the naming basis the orchestration subsystem should use.

## 6. Python And Tooling Organization

`Content/Python/FlightProject/` is currently a practical editor-time glue layer.
That is fine, but its purpose should stay narrow.

Python should own:

- editor asset ensure/repair flows
- validation and report generation
- orchestration of C++ editor shims
- codegen or import/export task entrypoints

Python should not become the only place where project semantics live.
Core contracts still belong in C++ schema/runtime code.

As the package grows, prefer grouping by concern:

- `FlightProject/Editor/`
- `FlightProject/Schema/`
- `FlightProject/Vex/`
- `FlightProject/Diagnostics/`

That refactor is optional for now.
The important rule is that Python remains an editor-time and tooling surface, not the canonical owner of runtime contracts.

## 7. Asset And Generated Output Rules

### 7.1 Use Unreal Assets For Anchors

Use Unreal assets and UObjects where Unreal needs to:

- discover and reference content
- save and diff it
- expose it in editor tooling
- gather it for cook/package
- attach it to maps, Niagara, or game features

### 7.2 Use Flight-Native Payloads For Meaning

Use Flight-native files or plain-data outputs for:

- VEX source
- generated HLSL or Verse text
- scheduler or job IR
- validation artifacts
- execution plans
- runtime caches

### 7.3 Recommended Output Split

| Kind | Recommended Home |
| --- | --- |
| authored Unreal descriptors/assets | `Content/` or plugin content |
| authored code/contracts | `Source/`, `Docs/`, plain-text source inputs |
| generated editor-time artifacts | `Saved/Flight/Generated/` |
| generated validation/reports | `Saved/Flight/Reports/` |
| runtime caches/snapshots | `Saved/Flight/Runtime/` |

## 8. Concrete Guidance For The Orchestration Subsystem

If the project wants one nexus point for "everything interacting together," the right answer is:

- world-scoped lifetime
- explicit `Orchestration` domain
- narrow coordination responsibility
- strong naming around participant, binding, plan, and report

Recommended initial type set:

```text
UFlightOrchestrationSubsystem
FFlightParticipantHandle
FFlightParticipantRecord
FFlightCohortBinding
FFlightBehaviorBinding
FFlightExecutionPlan
FFlightExecutionPlanStep
FFlightOrchestrationReport
```

Recommended relationship to existing domains:

- `Schema/` defines legal contracts
- `Vex/` and `Verse/` define behavior artifacts
- `Mass/`, `Swarm/`, and `Spatial/` execute owned logic
- `Orchestration/` resolves visibility and tells the world what is bound to what

That gives FlightProject one coordination nexus without making it the owner of every domain.

## 9. Decision Rule

When adding a new type or file, ask:

1. What domain owns the meaning?
2. Is this a boundary/service, a descriptor, a binding, a plan, or a report?
3. Does Unreal need to discover/cook/save this, or is it Flight-native?
4. Is this world-scoped coordination, or does it belong to a domain execution layer?

If the answer is still unclear, prefer:

- domain folder over module root
- explicit ownership name over generic name
- plain descriptor/binding/report types over live UObject graphs
- `Orchestration/` for cross-domain world coordination
