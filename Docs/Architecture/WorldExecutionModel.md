# World Execution Model

This document describes how FlightProject should think about Unreal world context, simulation ownership, rendering integration, and world scripting.

For folder placement and naming rules that support this model, see `Docs/Architecture/ProjectOrganization.md`.
For the specific split between Unreal `GameMode` ownership and reusable world bootstrap ownership, see `Docs/Architecture/GameModeBootstrapBoundary.md`.

The short version:

- `UWorld` is the host envelope.
- FlightProject owns the semantic pipeline.
- Unreal owns lifecycle, references, save/cook boundaries, and native presentation systems.

## 0. Launch Context Reality Check

FlightProject does not currently have a project-local `app.h` or an application-layer launch object that owns startup sequencing.

The active startup surfaces are:

- module startup via `FFlightProjectModule::StartupModule()`
- editor module startup via `FFlightProjectEditorModule::StartupModule()`
- subsystem initialization via Unreal's normal subsystem lifecycle
- mode/world bootstrap via `AFlightGameMode::StartPlay()`
- editor-only startup automation via `Content/Python/init_unreal.py`

That means "app" is no longer the right architectural center for reasoning about startup.
The right center is:

- module init for static registration
- editor module init for interactive editor tooling
- subsystem init for service availability
- `StartPlay` for game-world orchestration
- editor startup script for editor repair/validation

### Current Runtime Ordering

```text
Engine Launch
    -> FlightProject module startup
    -> FlightProjectEditor module startup (interactive editor only)
    -> GameInstance subsystem initialization
    -> World subsystem initialization
    -> map/world begins play
    -> AFlightGameMode::StartPlay
    -> world bootstrap
    -> swarm spawn / GPU init
    -> subsystem ticks
    -> render adapters consume sim state
```

### Current Startup Table

| Stage | Current Owner | What Happens |
| --- | --- | --- |
| Runtime module startup | `FFlightProjectModule::StartupModule()` | registers shader directory, runtime logging services, Verse assembler pass |
| Editor module startup | `FFlightProjectEditorModule::StartupModule()` | registers editor tabs and menus only when running an interactive editor session |
| Data/config ingress | `UFlightDataSubsystem::Initialize()` | loads lighting, autopilot, spatial layout, procedural anchor data |
| World service attach | world subsystem `Initialize()` methods | constructs service layer (`UFlightWorldBootstrapSubsystem`, `UFlightSwarmSubsystem`, `UFlightSpatialSubsystem`, `UFlightVerseSubsystem`, etc.) |
| Game-world orchestration | `AFlightGameMode::StartPlay()` | decides gauntlet vs default path, runs bootstrap, spawns initial swarm |
| Render bridge activation | `UFlightSwarmSubsystem::Initialize()` | creates scene view extension outside commandlets |
| Ongoing sim execution | subsystem tick / Mass / compute | `UFlightSwarmSubsystem::Tick()` and related execution domains advance runtime state |

### Recommended Interpretation

Think of `AFlightGameMode::StartPlay()` as the current orchestration trigger, not the semantic owner.

It is acceptable for now as a launch coordinator, but the long-term target should remain:

- game mode: thin trigger surface
- subsystems: real service boundary
- simulation domains: authoritative execution

That aligns with the World Execution Model without pretending there is still an "App" layer in the project.

For the next-step design of a dedicated coordination layer, see `Docs/Architecture/OrchestrationSubsystem.md`.

## 1. World Context In Unreal

At the engine level, `UWorld` is not "the game logic."
It is the container that coordinates:

- actors and components
- world subsystems
- ticking
- level/map state
- rendering scene state
- gameplay framework entrypoints
- engine-owned lifecycle and GC boundaries

For FlightProject, this means the world should be treated as the boundary surface where authored Unreal content is loaded and where runtime services are attached.

It should not be treated as the place where all simulation meaning lives.

## 2. Recommended Layering

FlightProject should separate world-facing concerns from simulation and rendering concerns.

### Layer 1: Authoring

This is Unreal-facing content:

- maps and placed actors
- Niagara systems
- Mass config assets
- game feature assets
- config tables and developer settings
- editor automation surfaces

This layer exists so Unreal can discover, reference, edit, save, and cook project state.

### Layer 2: World Services

This is the boundary/orchestration layer, typically owned by `UWorldSubsystem` or `UGameInstanceSubsystem`.

Examples already present in FlightProject:

- `UFlightWorldBootstrapSubsystem`
- `UFlightSwarmSubsystem`
- `UFlightSpatialSubsystem`
- `UFlightWaypointPathRegistry`

Responsibilities:

- world ingress
- repair/ensure flows
- lifecycle ownership
- low-frequency coordination
- building descriptors from authored content
- exposing safe APIs for scripts and gameplay hooks

### Layer 3: Simulation

This is where authoritative behavior should live:

- Mass fragments/processors
- bespoke entity state
- VEX lowering products
- scheduler/job/task IR
- event buffers
- GPU buffers
- async compute execution

This layer should prefer plain structs, SoA layouts, and explicit residency/affinity contracts over UObject graphs.

### Layer 4: Presentation

This is how simulation becomes visible:

- Niagara systems
- custom scene view extensions
- post-process passes
- materials and shader pipelines
- UI and diagnostics views

Presentation should consume snapshots and render-facing buffers.
It should not be the authoritative owner of simulation state.

### Layer 5: Observability

This layer captures operational truth:

- schema validation reports
- PIE trace output
- persistence debug snapshots
- compile diagnostics
- generated manifests
- automation artifacts

This should remain explicit and machine-readable.

## 3. FlightProject Mapping

### Current Subsystems and Roles

| System | Recommended Role |
| --- | --- |
| `UFlightWorldBootstrapSubsystem` | World setup/orchestration layer. Resume simulation, apply lighting policy, ensure layout actors. |
| `UFlightSwarmSubsystem` | Authoritative simulation service for swarm GPU state and reactive parameter ingress. |
| `UFlightSpatialSubsystem` | Spatial service registry and force aggregation boundary. |
| `UFlightVerseSubsystem` | CPU scripting/backend execution service, not world-authored state. |
| `FSwarmSceneViewExtension` | Presentation adapter from sim buffers to frame rendering. |

### Existing Good Separation

The current codebase already shows the right pattern in several places:

- `UFlightWorldBootstrapSubsystem` handles environment setup and world coordination rather than sim internals.
- `UFlightSwarmSubsystem` owns reactive inputs, persistent GPU resources, and simulation tick behavior.
- `FSwarmSceneViewExtension` reads shared render data and composites into the post-process chain.
- `UFlightSpatialSubsystem` acts as a registry/service rather than requiring direct actor traversal in hot loops.

### World Ownership Diagram

```text
┌──────────────────────────────────────────────────────────────────────┐
│ Unreal World Envelope                                                │
│                                                                      │
│  Maps / Actors / Assets / Niagara / Game Features / Config           │
│                  │                                                   │
│                  ▼                                                   │
│  World + GameInstance Services                                       │
│  - UFlightDataSubsystem                                              │
│  - UFlightWorldBootstrapSubsystem                                    │
│  - UFlightSpatialSubsystem                                           │
│  - UFlightSwarmSubsystem                                             │
│  - UFlightVerseSubsystem                                             │
│                  │                                                   │
└──────────────────┼───────────────────────────────────────────────────┘
                   │ boundary: descriptors, events, commands
                   ▼
┌──────────────────────────────────────────────────────────────────────┐
│ FlightProject Semantic Execution                                      │
│                                                                      │
│  Scheduler / VEX / Verse / Mass / Tasks / GPU Compute / Buffers      │
│                                                                      │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ boundary: render snapshots, debug artifacts
                   ▼
┌──────────────────────────────────────────────────────────────────────┐
│ Presentation + Observability                                          │
│                                                                      │
│  SceneViewExtension / Niagara / UI / Reports / Traces                │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### Current Class Placement Table

| Class / System | Layer | Primary Responsibility | Ownership Notes |
| --- | --- | --- | --- |
| `UFlightDataSubsystem` | World Services | Load and cache authored config rows for runtime consumption. | Keeps data ingestion outside hot loops and out of level scripts. |
| `UFlightWorldBootstrapSubsystem` | World Services | Resume Mass, apply lighting policy, ensure layout actors. | Good example of world orchestration instead of simulation ownership. |
| `UFlightSpatialSubsystem` | World Services | Register spatial fields and expose aggregate queries. | Should remain a boundary registry, not a place for arbitrary actor traversal. |
| `UFlightSwarmSubsystem` | World Services + Simulation Boundary | Own reactive ingress, GPU resources, simulation tick, and render bridge state. | This is the best current template for a sim domain attached to Unreal safely. |
| `UFlightVerseSubsystem` | Simulation Service | Compile and execute VEX-derived Verse behavior. | Should remain backend/service oriented rather than editor-authored state. |
| Mass processors/fragments | Simulation | Execute authoritative entity updates. | Keep plain-data oriented. |
| `FSwarmSceneViewExtension` | Presentation | Read sim-owned buffers and composite the frame. | Presentation adapter only; should not own gameplay truth. |
| Niagara systems / data interfaces | Authoring + Presentation | Visualize or expose render-facing state. | Useful for visualization, but should not become the simulation authority. |

## 4. Recommended Dataflow

The project should aim for this execution shape:

```text
World/Authored Content
    -> Ingress + Repair
    -> Descriptor Build
    -> Event Collection
    -> Scheduling
    -> Simulation Domains
    -> Render Adapters
    -> Optional Writeback
    -> Observability
```

### Expanded Frame Pipeline

```text
[Authoring / World State]
    Maps, placed actors, config assets, plugin state
            |
            v
[Ingress / Repair]
    ensure assets, normalize config, build stable references
            |
            v
[Descriptor Build]
    paths, swarm setup, field descriptors, script contracts
            |
            v
[Event Collection]
    gameplay triggers, readbacks, scheduler wakeups, requests
            |
            v
[Scheduling]
    choose CPU jobs / Verse work / GPU dispatches / barriers
            |
            v
[Simulation Domains]
    Mass + bespoke CPU + RDG compute + buffer mutation
            |
            +------------------+
            |                  |
            v                  v
[Render Snapshots]      [Observability Artifacts]
            |                  |
            v                  v
[Presentation]          reports, traces, manifests, debug snapshots
    Niagara / ViewExtensions / UI
            |
            v
[Optional Writeback]
    only where Unreal-facing state must be updated
```

### Stage 1: World/Authored Content

Inputs come from Unreal-facing content:

- maps
- placed actors
- config assets
- Niagara assets
- game feature/plugin state
- optional script entrypoints

### Stage 2: Ingress + Repair

World services validate and normalize that content:

- ensure required assets/configs exist
- repair missing traits/bindings
- convert authored content into stable descriptors
- reject invalid state early

This is where editor shims belong.

### Stage 3: Descriptor Build

Convert world and asset state into plain runtime descriptions:

- swarm spawn descriptors
- field descriptors
- path descriptors
- script contract tables
- render contract descriptors

These descriptors should be easier to diff, cache, validate, and pass across threads than raw UObject graphs.

### Stage 4: Event Collection

Collect low-frequency and high-frequency events separately:

- gameplay triggers
- perception results
- scheduler wakeups
- script requests
- world changes

Typed event streams are a better control surface than arbitrary world mutation.

### Stage 5: Scheduling

Decide what runs, where, and when:

- CPU jobs
- Verse-backed logic
- VEX-derived execution blocks
- GPU compute dispatches
- sort/rebuild barriers
- readback or notification gates

This is where residency, thread affinity, and determinism policies should be enforced.

### Stage 6: Simulation Domains

Run the actual state transitions:

- Mass processors
- task-based jobs
- bespoke CPU execution
- RDG compute passes
- buffer updates

This is the authoritative layer for entity evolution.

### Stage 7: Render Adapters

Rendering should read simulation output through explicit adapters:

- view extensions read pooled buffers/textures
- Niagara reads data interfaces and user params
- materials/post-process passes read render-facing textures

The render layer should consume stable snapshots, not drive gameplay authority.

### Stage 8: Optional Writeback

Only write back to Unreal objects when there is a strong reason:

- actor transforms that must be visible to the engine/editor
- asset updates during repair flows
- replicated framework state
- UI-visible reflected state

Avoid using writeback as the default path for every simulation step.

### Mutation Boundary Table

| Boundary | Preferred Payload | Avoid |
| --- | --- | --- |
| Authoring -> World Services | asset references, config rows, actor descriptors | direct hot-loop actor mutation |
| World Services -> Scheduler | typed commands, events, descriptors | raw UObject graphs passed across systems |
| Scheduler -> Simulation | plain structs, IR, buffers, fragment views | implicit side effects on editor/world objects |
| Simulation -> Presentation | immutable snapshots, pooled buffers/textures, user params | render code reaching back into gameplay authority |
| Simulation -> Writeback | explicit deltas, selected reflected state | whole-world synchronization every frame |
| Any background thread -> Unreal objects | queued commands or copied data | direct UObject mutation off-thread |

### Stage 9: Observability

Export the state needed to reason about the pipeline:

- validation reports
- event counts
- persistence snapshots
- automation artifacts
- generated code/manifests

If a system is important, it should produce inspectable outputs.

## 5. Rendering and Shading Guidance

For FlightProject, rendering systems should be treated as consumers of simulation, not as the owner of simulation.

### Recommended Rendering Philosophy

- Keep authoritative entity state outside Niagara graphs and material graphs.
- Use Niagara for visualization and artist-facing composition.
- Use RDG/compute for high-frequency simulation work.
- Use scene view extensions/post-process passes as presentation adapters.
- Prefer explicit contracts for buffer layout and symbol bindings.

### Rendering Ownership Diagram

```text
Simulation Authority
    FDroidState buffers / Mass fragments / field volumes / event buffers
            |
            v
Render Adapter Layer
    - Niagara data interfaces
    - Scene view extensions
    - RDG textures / pooled buffers
            |
            v
Presentation Systems
    - Niagara systems
    - post-process resolve/composite
    - UI overlays / diagnostics
```

### Rendering Integration Table

| Concern | Recommended Owner | Reason |
| --- | --- | --- |
| swarm state and behavior | simulation buffers / Mass / VEX backends | authoritative, high-frequency, non-UObject friendly |
| render-friendly field volumes | sim-owned textures with explicit adapters | keeps compute and rendering synchronized without making render graphs authoritative |
| visual composition and stylization | Niagara + scene view extensions + materials | these are Unreal-native strengths |
| shader contracts and symbol bindings | FlightProject schema/contracts | keeps backend semantics stable across HLSL/Verse/CPU |
| artist-facing tuning | Unreal assets/config plus shim repair | good editor UX without moving authority into assets |

### Practical Rule

If a piece of data changes at high frequency and is central to simulation correctness, it should probably live in FlightProject-owned buffers/structs first and only be projected into Unreal render systems second.

### For World Scripting

World scripting should usually:

- submit intents
- call subsystem APIs
- trigger ensure/repair
- enqueue events
- request compilation/rebuilds

It should usually not:

- mutate large numbers of actors every frame
- drive rendering state directly in hot paths
- treat asset graphs as authoritative runtime state

### World Scripting Table

| Good World Script Action | Why |
| --- | --- |
| call `UFlightScriptingLibrary` or subsystem entrypoints | keeps mutations centralized and repairable |
| request compile/rebuild/ensure flows | fits code-first + schema-first workflow |
| emit typed events or commands | decouples authoring and execution |
| load/validate data and inspect reports | aligns with observability layer |

| Risky World Script Action | Why |
| --- | --- |
| iterate and mutate many actors every frame | bypasses sim architecture and scales poorly |
| use Niagara assets as authoritative behavior state | pushes logic into the presentation layer |
| directly couple editor assets to worker-thread execution | unsafe lifecycle/threading boundary |
| write simulation truth back to UObject properties every tick | creates unnecessary engine coupling and contention |

## 6. Minimum Safe Rules

### UObject Rule

Live UObject mutation should stay on the appropriate Unreal thread.

### Worker Rule

Background work should operate on plain data, descriptors, IR, and buffers rather than live UObject graphs.

### Render Rule

Render-thread code should consume snapshots and render resources, not author gameplay truth.

### Persistence Rule

Create and save are separate phases.
Dirty packages are not durable content yet.

### Bridge Rule

Every boundary crossing should be explicit:

- asset -> descriptor
- descriptor -> simulation state
- simulation state -> render snapshot
- render/diagnostic result -> optional writeback

## 7. What To Keep In Unreal vs FlightProject

### Keep In Unreal

- maps and placed world anchors
- Niagara systems
- Mass config assets
- game feature assets
- editor tools and repair entrypoints
- engine-facing wrappers and references

### Keep In FlightProject

- schema contracts
- VEX source and lowering
- scheduler/task/job semantics
- entity execution plans
- GPU/CPU simulation buffers
- generated reports and manifests

## 8. Recommended Operating Model

The most defensible default for FlightProject is:

- Unreal owns the world envelope.
- FlightProject owns semantic execution.
- Subsystems form the boundary.
- Rendering reads from sim-owned state through explicit adapters.
- World scripting talks to services, not directly to hot-loop engine objects.

That keeps the project flexible enough for bespoke simulation and scripting work without becoming unsafe or hostile to Unreal's lifecycle.
