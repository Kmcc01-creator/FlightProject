# Orchestration Implementation Plan

This document turns the orchestration concept into a concrete implementation plan for FlightProject.

It is intentionally narrow.
The first goal is not to replace every existing subsystem.
The first goal is to create one world-scoped coordination surface that can answer:

- what services exist in this world?
- what participants are visible?
- what behaviors and contracts are active?
- what execution bindings should downstream systems consume?

## 0. Current Session Status

The plan is no longer purely hypothetical.

Implemented in the current branch:

- `UFlightOrchestrationSubsystem` exists under `Source/FlightProject/Public|Private/Orchestration/`
- phase-one participant, behavior, binding, plan, and report types exist
- the subsystem currently reports:
  - service availability
  - waypoint path visibility
  - swarm anchor visibility
  - spatial field visibility
  - compiled Verse behavior visibility
- scripting/debug surfaces exist via:
  - `UFlightScriptingLibrary::GetOrchestrationReportJson`
  - `UFlightScriptingLibrary::ExportOrchestrationReport`
  - `Flight.Debug.DumpOrchestrationReport`

Still not done:

- processors do not yet consume orchestration-issued bindings
- the swarm spawner still owns its current world-scan path
- the startup profile assets are not yet materialized on disk in this checkout

## 1. Current Reality

The implementation plan has to fit the current project, not an imagined clean slate.

Today:

- `AFlightGameMode::StartPlay()` still triggers bootstrap and initial swarm spawn
- `UFlightWorldBootstrapSubsystem` already owns reusable world setup work
- `UFlightSwarmSpawnerSubsystem` still scans the world for `AFlightSpawnSwarmAnchor`
- `UFlightWaypointPathRegistry` already provides a strong explicit registry pattern
- `UFlightVerseSubsystem` already owns compiled behavior metadata
- `UFlightVexBehaviorProcessor` still assumes `BehaviorID = 1`

That means phase one should improve visibility and binding, not force an immediate replacement of the runtime stack.

## 2. Phase-One Goal

Add a dedicated world subsystem:

```text
UFlightOrchestrationSubsystem
```

Phase one responsibilities:

- resolve service availability
- ingest visible participants from explicit registries and registration calls
- register behavior metadata from `UFlightVerseSubsystem`
- build a queryable world orchestration report
- build simple cohort-to-behavior bindings
- provide a stable API for later processor and spawner integration

Phase one non-goals:

- replacing `UFlightSwarmSubsystem`
- replacing `UFlightVerseSubsystem`
- replacing `UFlightSpatialSubsystem`
- directly owning Mass entities
- directly executing simulation work
- solving every editor or asset-authoring pain point

## 3. Proposed File Layout

Create a new domain folder:

```text
Source/FlightProject/Public/Orchestration/
Source/FlightProject/Private/Orchestration/
```

Recommended first file set:

| File | Purpose |
| --- | --- |
| `Public/Orchestration/FlightOrchestrationSubsystem.h` | world-scoped coordination API |
| `Private/Orchestration/FlightOrchestrationSubsystem.cpp` | dependency resolution, registration, plan/report build |
| `Public/Orchestration/FlightParticipantTypes.h` | participant handles, kinds, tags, descriptor records |
| `Public/Orchestration/FlightBehaviorBinding.h` | behavior-to-cohort binding structs |
| `Public/Orchestration/FlightExecutionPlan.h` | per-world resolved execution plan structs |
| `Public/Orchestration/FlightOrchestrationReport.h` | observability/report surface |
| `Private/Orchestration/FlightOrchestrationLog.cpp` | optional dedicated log category |

Optional but likely useful in phase two:

| File | Purpose |
| --- | --- |
| `Public/Orchestration/FlightOrchestrationTypes.h` | small shared enums if the type count grows |
| `Public/Orchestration/FlightParticipantProvider.h` | provider interface if explicit provider registration becomes common |
| `Private/Orchestration/FlightOrchestrationDebug.cpp` | report formatting and trace helpers |

## 4. Core Types

### 4.1 Participant Types

The subsystem should not store raw world state as its primary model.
It should reduce visible things to plain records.

Recommended initial types:

| Type | Purpose |
| --- | --- |
| `FFlightParticipantHandle` | stable lightweight identifier for orchestration-owned records |
| `EFlightParticipantKind` | anchor, path, field, behavior provider, render adapter, async source, service |
| `FFlightParticipantRecord` | reduced world-visible record with owner, tags, and links |
| `FFlightCohortKey` | stable cohort identifier used for bindings and plans |
| `FFlightCohortRecord` | logical execution group, not one actor and not one Mass entity |

Recommended `FFlightParticipantRecord` fields:

```text
Handle
Kind
Name
OwnerSubsystem
SourceObjectPath
Tags
Capabilities
ContractKeys
Optional Actor/Object weak pointer
Optional runtime handle
```

### 4.2 Behavior And Binding Types

Recommended initial behavior/binding types:

| Type | Purpose |
| --- | --- |
| `FFlightBehaviorHandle` | orchestration-facing behavior identifier |
| `FFlightBehaviorRecord` | reduced metadata from `UFlightVerseSubsystem` |
| `EFlightExecutionDomain` | native CPU, task graph, Verse VM, SIMD, GPU |
| `FFlightBehaviorBinding` | behavior attached to a cohort |
| `FFlightCohortBinding` | aggregate binding view for a cohort |

Recommended `FFlightBehaviorRecord` fields:

```text
BehaviorID
Name
CompileState
ExecutionRateHz
FrameInterval
RequestedDomain
ResolvedDomain
RequiredContracts
Diagnostics
```

### 4.3 Plan And Report Types

Recommended initial planning/observability types:

| Type | Purpose |
| --- | --- |
| `FFlightExecutionPlanStep` | one resolved execution step |
| `FFlightExecutionPlan` | per-world plan built from visible cohorts and bindings |
| `FFlightMissingContract` | one unmet requirement |
| `FFlightOrchestrationReport` | coherent view of services, participants, behaviors, cohorts, and missing contracts |

Recommended `FFlightExecutionPlanStep` fields:

```text
CohortKey
BehaviorID
ExecutionDomain
FrameInterval
bAsync
InputContracts
OutputConsumers
```

## 5. Subsystem API Surface

The first API should be explicit and boring.
Do not begin with a large polymorphic framework.

Recommended initial public methods:

```cpp
virtual void Initialize(FSubsystemCollectionBase& Collection) override;
virtual void Deinitialize() override;

FFlightParticipantHandle RegisterParticipant(const FFlightParticipantRecord& Record);
void UnregisterParticipant(FFlightParticipantHandle Handle);

void RegisterBehavior(uint32 BehaviorID, const FFlightBehaviorRecord& Record);
void UnregisterBehavior(uint32 BehaviorID);

bool RegisterCohort(const FFlightCohortRecord& Cohort);
void UnregisterCohort(FName CohortName);

bool BindBehaviorToCohort(const FFlightBehaviorBinding& Binding);
void ClearBindingsForCohort(FName CohortName);

const FFlightExecutionPlan& GetExecutionPlan() const;
const FFlightOrchestrationReport& GetReport() const;

void RebuildVisibility();
void RebuildExecutionPlan();
```

Recommended first query methods:

```cpp
bool IsServiceAvailable(FName ServiceName) const;
const FFlightParticipantRecord* FindParticipant(FFlightParticipantHandle Handle) const;
const FFlightCohortRecord* FindCohort(FName CohortName) const;
TArray<FFlightBehaviorBinding> GetBindingsForCohort(FName CohortName) const;
```

## 6. Service Resolution

On `Initialize()`, the subsystem should resolve and cache the world services it depends on.

Recommended initial dependency set:

- `UFlightWorldBootstrapSubsystem`
- `UFlightSwarmSubsystem`
- `UFlightSpatialSubsystem`
- `UFlightVerseSubsystem`
- `UFlightVexTaskSubsystem`
- `UFlightWaypointPathRegistry`
- `UFlightSwarmSpawnerSubsystem` when the plugin is present

Recommended rule:

- resolve once in `Initialize()`
- expose availability in the report
- never crash orchestration because one optional domain subsystem is absent

That gives one canonical answer to "what services are active in this world?"

## 7. Registration Model

Phase one should mix two registration paths:

1. explicit registration where it already exists or is easy to add
2. controlled ingestion from existing registries where explicit registration is already owned elsewhere

### 7.1 Existing Systems And Their Phase-One Path

| Current System | Phase-One Approach | Long-Term Direction |
| --- | --- | --- |
| `UFlightWaypointPathRegistry` | orchestration reads registry state and mirrors participant/cohort visibility | registry event subscription or explicit provider callback |
| `AFlightSpawnSwarmAnchor` | still discovered through current spawn flow, but reduced to participant records | actor registers/unregisters directly or via a spawn registry |
| `UFlightSpatialSubsystem` | orchestration mirrors registered field visibility | explicit field descriptors and capability tags |
| `UFlightVerseSubsystem` | orchestration ingests compiled behavior metadata | direct behavior registration on compile/update |
| `UFlightSwarmSpawnerSubsystem` | orchestration reads/configures spawn cohorts | spawner consumes orchestration-issued cohorts |
| `UFlightSwarmSubsystem` | exposed as a service and render consumer | consumes orchestration plan metadata where useful |

### 7.2 First Explicit Registration Targets

The easiest first wins are:

- behavior registration from `UFlightVerseSubsystem`
- cohort registration for swarm cohorts
- participant registration for swarm anchors

These give the subsystem meaningful visibility without changing the whole runtime.

## 8. Startup Ordering

The subsystem should fit the current startup path instead of competing with it.

Recommended startup order:

```text
Module startup
    -> world subsystem initialization
    -> UFlightOrchestrationSubsystem resolves service availability
    -> AFlightGameMode::StartPlay()
        -> UFlightWorldBootstrapSubsystem::RunBootstrap()
        -> swarm/path/anchor visibility rebuild
        -> orchestration plan rebuild
        -> initial swarm spawn
        -> post-spawn orchestration report refresh
```

Recommended interpretation:

- `StartPlay()` remains the trigger in phase one
- orchestration becomes the coordination surface that `StartPlay()` calls into
- bootstrap and spawner stay owners of their local work

Phase-two target:

- `AFlightGameMode::StartPlay()` becomes thin delegation
- orchestration and bootstrap own the real startup sequence

## 9. VEX And Schema Integration

The orchestration subsystem becomes useful as soon as behavior metadata and legal bindings stop being scattered.

### 9.1 Schema Input

Schema should continue to define:

- symbol legality
- required contracts
- asset and plugin expectations
- future behavior-binding legality

### 9.2 VEX/Verse Input

`UFlightVerseSubsystem` should publish reduced behavior records into orchestration:

- compile state
- rate/frame interval
- async policy
- resolved execution domain
- diagnostics

### 9.3 Orchestration Output

The orchestration subsystem should resolve:

- which cohorts exist
- which behaviors are available
- which bindings are legal
- which contracts are missing
- which execution plan should be consumed this frame

This is the step that removes the hard-coded `BehaviorID = 1` assumption from being the only real binding model.

## 10. Processor And Spawner Integration Plan

### 10.1 `UFlightVexBehaviorProcessor`

Current problem:

- behavior selection is global and hard-wired

Phase-one change:

- allow the processor to query the orchestration subsystem for the active behavior binding set
- keep a simple fallback path for worlds with no orchestration bindings

Phase-two change:

- process cohort-resolved bindings instead of one global behavior ID

### 10.2 `UFlightSwarmSpawnerSubsystem`

Current problem:

- spawner scans anchors directly and infers world visibility itself

Phase-one change:

- orchestration builds swarm cohort records from visible anchors and autopilot/path state
- spawner can still perform current spawn logic, but with an orchestration query surface available

Phase-two change:

- spawner consumes orchestration-issued cohort descriptors instead of scanning the world directly

### 10.3 `UFlightWaypointPathRegistry`

This is already close to the desired model.

Phase-one change:

- orchestration mirrors path registry state into participant/cohort visibility

Phase-two change:

- registry emits change events or provider callbacks so orchestration can rebuild incrementally

## 11. First Implementation Slice

The first implementation slice should be small enough to land cleanly.

Recommended slice:

1. add `Orchestration/` folder and base types
2. add `UFlightOrchestrationSubsystem`
3. resolve and report service availability
4. ingest behavior metadata from `UFlightVerseSubsystem`
5. ingest path visibility from `UFlightWaypointPathRegistry`
6. add manual or bootstrap-triggered `RebuildVisibility()` and `RebuildExecutionPlan()`
7. expose a debug report through logs or scripting

That slice creates the nexus point without changing spawn or Mass behavior logic yet.

## 12. Migration Phases

### Phase 1: Visibility And Report

Deliverables:

- subsystem exists
- report exists
- service availability is explicit
- behavior visibility is explicit
- path/cohort visibility is explicit

### Phase 2: Registration And Cohorts

Deliverables:

- spawn anchors register as participants or feed a spawn registry
- orchestration owns explicit cohort records
- bootstrap/startup path calls orchestration rebuilds directly

### Phase 3: Binding And Consumption

Deliverables:

- `UFlightVerseSubsystem` registers behavior updates directly
- `UFlightVexBehaviorProcessor` consumes orchestration bindings
- spawner reads orchestration-issued cohort descriptors

### Phase 4: Contract-Driven Planning

Deliverables:

- schema-driven binding legality
- degraded-capability reporting
- render-consumer visibility
- per-world execution-plan export

## 13. Things To Defer

Do not force these into phase one:

- generalized runtime reflection framework for all participants
- full actor registration conversion across every system
- render-graph ownership changes
- Mass entity enumeration in orchestration
- full editor tooling UI for orchestration

Those are downstream benefits, not prerequisites for the first useful subsystem.

## 14. Immediate Implementation Checklist

When implementation starts, the first concrete tasks should be:

1. create `Public/Orchestration/` and `Private/Orchestration/`
2. add `UFlightOrchestrationSubsystem` with service-resolution and report storage
3. define `FFlightParticipantRecord`, `FFlightBehaviorRecord`, `FFlightBehaviorBinding`, and `FFlightExecutionPlan`
4. add a `RebuildVisibility()` path that ingests:
   - Verse behavior metadata
   - path registry state
   - current spawn-anchor visibility
5. add `RebuildExecutionPlan()` using simple cohort-to-behavior bindings
6. expose a scripting/debug entrypoint to dump the orchestration report
7. update `StartPlay` or bootstrap flow to call orchestration rebuild before and after swarm spawn

That is enough to make orchestration a real system rather than just a design note.
