# Orchestration Subsystem

This document sketches a dedicated world-scoped orchestration subsystem for FlightProject.

For the project-organization and naming basis that should contain this subsystem, see `Docs/Architecture/ProjectOrganization.md`.
For the concrete implementation and migration steps, see `Docs/Workflow/OrchestrationImplementationPlan.md`.

The aim is not to create a giant god-object.
The aim is to create a single coordination surface that knows:

- what execution participants exist in the world
- which registries and services are available
- which script/schema contracts are active
- how simulation, scheduling, and rendering domains should be wired together

## 1. Why Add It

FlightProject already has several strong domain services:

- `UFlightWorldBootstrapSubsystem`
- `UFlightSwarmSubsystem`
- `UFlightSpatialSubsystem`
- `UFlightVerseSubsystem`
- `UFlightVexTaskSubsystem`
- `UFlightWaypointPathRegistry`
- `UFlightSwarmSpawnerSubsystem`

The missing piece is not more simulation code.
The missing piece is a world-level coordinator that can answer:

- what entities or execution domains are visible right now?
- what capabilities are registered in this world?
- what script behaviors are compiled and where do they apply?
- what contracts are satisfied or missing?
- what should run this frame and in what order?

Today that visibility is fragmented:

- path visibility is in `UFlightWaypointPathRegistry`
- config/schema visibility is in `Flight::Schema::BuildManifestData()`
- behavior visibility is in `UFlightVerseSubsystem::Behaviors`
- swarm spawn visibility is implicit in `UFlightSwarmSpawnerSubsystem`
- script execution in `UFlightVexBehaviorProcessor` now consumes orchestration-issued bindings, but the remaining execution-domain selection and reporting story is still distributed across runtime services

That is enough to prototype, but not enough to support a clean code-first orchestration model.

## 2. Proposed Role

Introduce a dedicated world subsystem:

- `UFlightOrchestrationSubsystem`

Recommended responsibility split:

- `UFlightOrchestrationSubsystem`
  owns registration, discovery, visibility, execution planning, and cross-domain coordination.
- Existing domain subsystems
  continue owning their local logic and resources.

In other words:

- orchestration subsystem = coordinator
- domain subsystem = owner/operator

## 3. Core Responsibilities

### 3.1 Service Discovery

At initialization, the orchestration subsystem should resolve and cache references to:

- data/config ingress
- bootstrap service
- spatial service
- swarm simulation service
- VEX/Verse compile-execute service
- task/job execution service
- path registries and similar domain registries

This gives one place to answer "what services are actually available in this world?"

### 3.2 Participant Registration

It should maintain a world-scoped registry of orchestration participants.

Participants can include:

- authored anchors
- waypoint/path providers
- field providers
- spawn groups
- script behavior providers
- render adapter providers
- async event/readback providers

The important change is that visibility should become explicit.

### 3.3 Contract Resolution

It should fuse together:

- schema manifest data
- plugin/profile requirements
- VEX symbol definitions
- active behavior registrations
- runtime capability availability

So "what can this world do?" becomes a queryable orchestration fact, not scattered implicit state.

### 3.4 Execution Planning

It should build per-world execution plans:

- which behaviors are active
- which entity cohorts they apply to
- which domains execute them (CPU, task, GPU, Verse VM, native fallback)
- what barriers or sync points are needed
- which render adapters consume the resulting state

### 3.5 Observability

It should export a coherent picture of:

- registered participants
- active behaviors
- missing contracts
- active execution domains
- frame-level orchestration decisions

That is much easier to debug than chasing several subsystem-local views.

## 4. Visibility Model

The orchestration subsystem should not "discover everything by scanning the world every frame."

It should rely on a layered visibility model.

### Source A: Static Service Visibility

These are world-available services discovered once:

- `UFlightDataSubsystem`
- `UFlightWorldBootstrapSubsystem`
- `UFlightSwarmSubsystem`
- `UFlightSpatialSubsystem`
- `UFlightVerseSubsystem`
- `UFlightVexTaskSubsystem`
- `UFlightWaypointPathRegistry`
- `UFlightSwarmSpawnerSubsystem`

This answers:

- what capabilities exist at all?

### Source B: Registry Visibility

These are domain-owned registries:

- path registry
- future field registry
- future spawn-group registry
- future behavior-binding registry

This answers:

- what world participants have been registered?

### Source C: Schema Visibility

These are manifest/contracts:

- asset requirements
- Niagara contracts
- VEX symbol requirements
- plugin/cvar requirements

This answers:

- what should exist?
- what symbols and contracts are legal?

### Source D: Runtime Visibility

These are live execution facts:

- compiled behaviors
- loaded execution plans
- active entity cohorts
- pending async jobs/readbacks
- render adapter state

This answers:

- what is active right now?

## 5. Registration Model

The orchestration subsystem should use explicit registration instead of hidden world iteration wherever possible.

### Recommended Registration Interfaces

| Participant Type | Registration Mechanism |
| --- | --- |
| waypoint/path provider | registers with path registry, orchestration subsystem subscribes to registry events |
| swarm spawn anchors | register/unregister with swarm spawn registry or orchestration subsystem directly |
| spatial fields | register with `UFlightSpatialSubsystem`; orchestration subsystem queries/indexes them |
| script behaviors | compile into `UFlightVerseSubsystem`, then register metadata with orchestration subsystem |
| render adapters | subsystem-owned registration with orchestration visibility metadata |
| async event sources | explicit provider registration |

### Recommended Participant Record

Each visible participant should reduce to a plain descriptor:

```text
ParticipantId
ParticipantKind
OwnerSubsystem
WorldContext
Tags / capabilities
Schema/contract links
Optional asset or actor handle
Optional runtime handle
```

That record is much safer and easier to reason about than passing live UObject graphs through orchestration code.

## 6. Entity Visibility

The user-facing question was:

> how does our orchestration subsystem know what entities we have?

The recommended answer is:

It should not try to own all entities directly.
It should know about entity cohorts and bindings.

### Recommended Entity Visibility Levels

| Level | Meaning | Owner |
| --- | --- | --- |
| authored entity source | anchors, config assets, path sources, map-authored groups | Unreal/world layer |
| spawn cohort | a logical group to be spawned/managed together | orchestration layer |
| Mass entity population | actual ECS entities and fragments | Mass / simulation layer |
| render-visible population | entities projected to Niagara/view adapters | presentation layer |

This matters because "world knows entities" and "simulation knows entities" are not the same thing.

The orchestration subsystem should own mappings such as:

- spawn cohort -> config asset
- spawn cohort -> path binding
- spawn cohort -> behavior set
- behavior set -> execution domain
- simulation domain -> render adapters

That is much more useful than storing every individual entity in an orchestration registry.

## 7. VEX Integration

This is where the orchestration subsystem becomes especially useful.

### Current State

Right now:

- symbol definitions are built from the schema manifest in `UFlightVerseSubsystem`
- compiled behaviors live in `UFlightVerseSubsystem::Behaviors`
- task dispatch exists in `UFlightVexTaskSubsystem`
- the Mass VEX behavior processor now resolves orchestration-issued bindings per chunk/cohort before falling back

That means behavior visibility and basic cohort binding are present, but richer execution-domain selection and reporting are not fully centralized yet.

### Proposed VEX-Orchestration Model

The orchestration subsystem should introduce explicit behavior bindings:

| Concept | Description |
| --- | --- |
| behavior definition | compiled VEX/Verse/native behavior metadata |
| behavior binding | which entity cohort or participant set should use the behavior |
| execution domain | CPU / task / native SIMD / Verse / GPU |
| scheduling metadata | rate, frame interval, async policy, residency, affinity |
| symbol contract set | allowed/required symbols derived from schema |

### Suggested Flow

1. schema manifest defines symbol contracts and legal bindings
2. VEX compile produces behavior metadata
3. orchestration subsystem registers that behavior
4. a cohort/participant binding connects the behavior to a world-visible execution target
5. Mass/VEX processors consume orchestration-issued bindings instead of assuming one global behavior

That would move the project from:

- "there is a compiled behavior in the world"

to:

- "this world cohort is bound to these behaviors under these contracts"

## 8. Schema Integration

The orchestration subsystem should treat schema as a first-class input.

### What Schema Should Provide

- symbol legality
- required assets/configs
- rendering contracts
- plugin/cvar expectations
- future behavior binding schemas

### What Orchestration Should Add

- actual resolved world participants
- actual resolved services
- actual resolved behavior bindings
- actual missing or degraded capability reports

That gives a clean split:

- schema says what should be true
- orchestration says what is true in this world

## 9. Proposed Data Structures

### Service Index

```text
FOrchestrationServices
  DataSubsystem
  BootstrapSubsystem
  SpatialSubsystem
  SwarmSubsystem
  VerseSubsystem
  VexTaskSubsystem
  PathRegistry
  SwarmSpawnerSubsystem
```

### Participant Descriptor

```text
FOrchestrationParticipant
  ParticipantId
  Kind
  Tags
  AssetPath / ActorRef / RuntimeRef
  Capabilities
  ContractIds
```

### Behavior Descriptor

```text
FOrchestrationBehavior
  BehaviorId
  CompileState
  Tier
  ExecutionRate
  FrameInterval
  AsyncPolicy
  Residency
  Affinity
  ContractIds
```

### Binding Descriptor

```text
FOrchestrationBinding
  BindingId
  ParticipantSelector / CohortId
  BehaviorIds
  ExecutionDomain
  RenderTargets
  SyncPolicy
```

## 10. Frame-Level Role

The orchestration subsystem should not run the simulation itself.
It should prepare and coordinate the frame.

### Per-Frame Responsibilities

```text
refresh runtime visibility
    -> reconcile participants and bindings
    -> determine active behavior cohorts
    -> publish execution plan
    -> let simulation domains run
    -> collect observability state
```

This keeps the subsystem strategic rather than turning it into a duplicate simulation engine.

## 11. Recommended Adoption Path

### Phase 1: Visibility Only

Add `UFlightOrchestrationSubsystem` as a world subsystem that:

- discovers services
- records participant descriptors
- records behavior descriptors
- exports a world orchestration report

No simulation ownership changes yet.

### Phase 2: Binding Layer

Move from global VEX behavior assumptions to explicit bindings:

- cohort -> behavior
- behavior -> execution domain

### Phase 3: Scheduling Coordination

Let Mass/VEX/GPU services consume orchestration-issued execution plans rather than local ad hoc assumptions.

### Phase 4: Repair + Validation Integration

Fuse schema ensure/diff logic into orchestration reporting so one subsystem can answer:

- what exists
- what is active
- what is missing
- what degraded gracefully

## 12. Key Recommendation

The orchestration subsystem should know:

- services
- participants
- cohorts
- behaviors
- bindings
- contracts

It should not try to own every raw entity instance or all domain data directly.

For FlightProject, that is the right scale:

- world container
  owns service attachment and host lifecycle
- orchestration subsystem
  owns visibility and coordination
- domain subsystems
  own execution and resources
- schema/VEX layers
  define legal contracts and executable behavior
