# Navigation

This document reframes FlightProject navigation around the current project vision rather than the older "splines grow into a graph" story.

Use `Docs/Workflow/UnrealDevelopmentImprovementPlan.md` as the active reference document for why this matters now.
Use `Docs/Architecture/WorldExecutionModel.md` for lifetime and world-boundary rules.
Use `Docs/Architecture/OrchestrationSubsystem.md` for the world-scoped coordination model this navigation system should plug into.

## 1. Why Revisit Navigation

FlightProject no longer fits a simple navigation story.

Today the project already has several partially overlapping navigation-adjacent systems:

- `AFlightWaypointPath` and `UFlightWaypointPathRegistry`
- `FFlightPathFollowFragment` and `UFlightPathFollowProcessor`
- `UFlightNavGraphDataHubSubsystem`
- `AFlightNavBuoyRegion`
- `AFlightSpawnSwarmAnchor`
- orchestration-visible waypoint paths, anchors, cohorts, and behavior bindings

Those systems are useful, but they are not yet one coherent navigation architecture.

The improvement plan raised the right question:

> What is the canonical navigation primitive for FlightProject, and how should legality, cohorts, and reports flow through it?

That is the purpose of this document.

## 2. Current Reality

### 2.1 What Exists Right Now

Current runtime behavior is still primarily spline-committed:

- entities carry `FFlightPathFollowFragment`
- the fragment stores a committed `PathId`, distance, speed, and loop flag
- `UFlightPathFollowProcessor` samples `UFlightWaypointPathRegistry`
- `UFlightSwarmSpawnerSubsystem` assigns the initial `PathId`

Current graph/probe work is mostly visibility and authoring support:

- `UFlightNavGraphDataHubSubsystem` stores nodes and edges
- `AFlightNavBuoyRegion` generates nav-probe layout rows and registers loop-style graph entries
- `AFlightSpawnSwarmAnchor` can register itself as a graph node for debug/visualization

Current orchestration work is adjacent but not fully integrated:

- orchestration already sees waypoint paths, anchors, cohorts, and behaviors
- anchor cohorts already carry behavior legality
- navigation legality is not yet an orchestration-owned concept

### 2.2 What This Means

FlightProject currently has:

- a committed movement primitive: sampled spline path data
- a topology/probe visibility primitive: nav graph nodes and edges
- a cohort/behavior binding primitive: orchestration cohorts

These are not yet one model.

## 3. Core Thesis

FlightProject navigation should be treated as a projected, contract-bound routing system.

The key pattern should be:

```text
navigation description
    -> legality contract
        -> candidate route projection
            -> route selection
                -> committed movement path
                    -> report
```

That is consistent with the broader project vision:

- authored intent becomes a contract
- contracts bind against world context
- legal candidates are projected before commit
- a selected result becomes runtime-executable
- the system leaves behind a report explaining what happened

Navigation should therefore not be "just splines," "just a graph," or "just a nav mesh."
It should be a routing layer that can consume several representations but still produce one explicit committed result.

## 4. Recommended Canonical Model

### 4.1 Canonical Primitive

The canonical primitive should be a routed corridor or lane plan, not a raw spline actor and not a raw graph node.

In practical terms:

- graphs, probes, fields, and authored paths are inputs
- route candidates are projected from those inputs
- one legal route is selected for a cohort or participant
- the selected route is lowered into a committed movement representation

For the current runtime, that committed representation can still be:

- `FGuid PathId`
- `FFlightPathData`
- `FFlightPathFollowFragment`

That lets FlightProject keep the current cache-friendly Mass path-follow path while moving planning and legality to a better architectural boundary.

### 4.2 Recommended Layer Split

#### Layer A: Authoring Primitives

These describe possible traversable structure:

- `AFlightWaypointPath`
- `AFlightNavBuoyRegion`
- CSV-authored nav probes / spatial rows
- future graph or volume assets

These are not the final authority on committed runtime routing.

#### Layer B: Navigation Visibility

These expose reduced topology facts:

- waypoint path descriptors
- nav node and edge descriptors
- probe-field descriptors
- anchor ingress/egress descriptors

This is where world-visible navigation facts become explicit and queryable.

#### Layer C: Navigation Legality

This layer answers:

- which cohorts may traverse which route classes?
- which altitude, airspace, obstacle, or mission constraints apply?
- which startup profile constraints apply in this world?
- which route candidates are degraded, illegal, or preferred?

This is the layer FlightProject is currently missing.

#### Layer D: Committed Runtime Path

This layer feeds hot-loop execution:

- `UFlightWaypointPathRegistry`
- `FFlightPathData`
- `FFlightPathFollowFragment`
- `UFlightPathFollowProcessor`

This should remain simple and cache-friendly.

#### Layer E: Observability

This layer reports:

- visible nav participants
- candidate route count
- selected route per cohort
- rejected candidates and reasons
- degraded-capability or fallback routing

Without this layer, navigation decisions stay implicit and hard to debug.

## 5. Answers To The Improvement-Plan Questions

### 5.1 What Is The Canonical Navigation Primitive?

Recommendation:

- not only spline
- not only probe cloud
- not only graph
- not only mesh/volume
- use a layered combination

The best current answer is:

- graph/probe/field data describe possible route topology
- orchestration or a dedicated navigation planner projects candidate routes
- committed execution lowers the chosen route into path segments that Mass can follow efficiently

In other words:

- topology is plural
- committed execution is singular

### 5.2 Which Layer Owns Legality?

Recommendation:

- authored actors and data rows may declare constraints
- spatial systems provide world facts
- startup profiles provide scenario policy
- orchestration owns final legality evaluation and report output

That split matters.

`AFlightNavBuoyRegion` or `AFlightSpawnSwarmAnchor` can author intent, but they should not become the final authority on whether a route is legal for a cohort in the active world context.

Legality should be evaluated from:

- cohort identity
- route tags / topology class
- behavior or mission contracts
- startup profile policy
- spatial hazard / clearance / capability facts

### 5.3 How Should Navigation Enter Orchestration?

Recommendation:

- navigation should enter orchestration as participants plus projected route candidates

More concretely:

- paths, graph nodes, graph edges, ingress anchors, and relevant spatial constraints become visible participants or reduced descriptors
- orchestration should be able to ask:
  - what route primitives are visible?
  - what cohorts need routes?
  - what constraints apply?
  - what route was selected?

The current nav graph hub is useful as a source of visibility, but it is not yet the actual route-selection owner.

### 5.4 What Should Reports Explain?

At minimum, the navigation report surface should answer:

- what navigation primitives were visible?
- what cohorts requested or implied navigation work?
- which route candidate was selected?
- which candidates were rejected?
- was the selection explicit, degraded, or fallback?
- what legality or capability facts drove that decision?

This is the navigation equivalent of the current orchestration binding report work.

### 5.5 Which Existing Systems Are Scaffolding Versus Long-Term Boundaries?

#### Keep As Long-Term Boundaries

- `UFlightWaypointPathRegistry`
  - good committed-runtime boundary for cache-friendly sampling
- `FFlightPathFollowFragment`
  - good execution-facing fragment for the current movement layer
- orchestration cohort identity
  - correct direction for route selection and legality

#### Keep, But Reframe

- `AFlightWaypointPath`
  - useful authoring primitive and source for committed lanes
  - should not be treated as the entire navigation model
- `AFlightSpawnSwarmAnchor`
  - useful ingress/cohort authoring surface
  - should participate in routing and legality, not own them

#### Treat As Scaffolding Or Transitional

- `UFlightNavGraphDataHubSubsystem`
  - currently a topology visibility/analytics hub
  - should not be mistaken for the final planner or legality owner
- `AFlightNavBuoyRegion`
  - useful probe/topology authoring surface
  - likely part of authoring and visibility, not the final runtime authority
- CSV-authored nav-probe layouts
  - useful as authored descriptors and scenario scaffolding
  - should feed explicit route/topology visibility instead of remaining a special-case path

## 6. Recommended Ownership Model

### 6.1 Authoring Ownership

Authoring surfaces describe navigation possibilities:

- waypoint lanes
- nav-probe regions
- ingress anchors
- route tags
- preferred route classes
- restricted route classes

These belong to authored content and data ingress.

### 6.2 Orchestration Ownership

Orchestration should own:

- which nav participants are visible
- which cohorts need navigation
- which constraints are active
- which route candidates are legal
- which route becomes committed
- the report explaining that decision

This does not mean orchestration has to perform every geometry computation itself.
It means orchestration should own the decision surface.

### 6.3 Domain Ownership

Domain systems should keep their local work:

- spatial systems own field queries and hazard facts
- path registry owns path sampling and runtime lookup
- Mass processors own hot-loop motion updates
- future route planners can own graph/path search algorithms

This preserves the "orchestration coordinates, domains execute" rule.

## 7. Cohorts And Navigation

Navigation should bind to cohorts, not only to individual entities.

This is already consistent with the current project direction:

- swarm batches already carry `FFlightBehaviorCohortFragment`
- orchestration already builds cohort records
- anchor cohorts already carry behavior legality

The next step is to let cohorts also carry navigation-facing constraints such as:

- preferred route class
- forbidden route tags
- altitude band or airspace policy
- formation width / clearance requirements
- startup-profile routing policy

That allows the system to answer:

- what route is legal for `SwarmAnchor.Alpha`?
- what route is legal for `Swarm.Default` in the active startup profile?
- what changed when the world or profile changed?

## 8. Navigation Legality

Behavior legality has already started landing in orchestration.
Navigation should follow the same pattern.

### 8.1 Inputs To Legality

Navigation legality should be derived from:

- route descriptor tags
- anchor-authored constraints
- startup-profile constraints
- mission/scenario policy
- spatial hazard or occupancy facts
- world capability availability

### 8.2 Example Legality Questions

- Is this route legal for the current cohort?
- Does this route violate required altitude or clearance?
- Does the startup profile forbid this route class?
- Does the route require a capability this world does not currently have?
- Is this route only a degraded fallback because a better topology source is missing?

### 8.3 Expected Output

The output should not only be yes/no.
It should be:

- legal
- illegal
- degraded but usable
- preferred
- fallback only

That gives the project a much better surface for routing, testing, and reporting.

## 9. Proposed Runtime Shape

The recommended shape is:

```text
authored navigation inputs
    -> reduced navigation descriptors
        -> orchestration-visible cohorts and route candidates
            -> legality evaluation
                -> selected committed route
                    -> path/lane lowering
                        -> Mass path-follow execution
                            -> navigation report
```

### 9.1 Near-Term Practical Interpretation

Near-term, this can still lower to the current path-follow runtime:

- select or build a path-like committed route
- assign `PathId`
- initialize `FFlightPathFollowFragment`
- let `UFlightPathFollowProcessor` do the hot-loop work

That means FlightProject does not need to rewrite movement execution immediately in order to redesign navigation correctly.

## 10. Migration Direction

### Phase 1: Make Current Reality Explicit

- document that splines are the current committed movement primitive
- document that the nav graph hub is visibility/analytics, not final planning authority
- document that buoy regions and probe layouts are authoring inputs

### Phase 2: Add Navigation Visibility To Orchestration

- reduce graph/path/probe/anchor surfaces into orchestration-visible nav participants
- make nav availability visible in the world report
- define the first route-candidate descriptor types

### Phase 3: Add Cohort-Aware Navigation Legality

- allow cohort-level nav constraints
- allow startup-profile nav policy
- report legal, preferred, degraded, and rejected candidates

### Phase 4: Add Route Projection And Selection

- project route candidates from graph, probes, paths, and fields
- select one committed route per relevant cohort or request
- preserve selection reasons in reports

### Phase 5: Improve Runtime Lowering

- keep path registry as the committed execution backend where it still fits
- add richer committed route representations only when the planner actually needs them

## 11. What To Avoid

Avoid these failure modes:

- treating `AFlightWaypointPath` as the entire navigation architecture
- promoting `UFlightNavGraphDataHubSubsystem` from debug hub to implicit planner without explicit legality/report surfaces
- embedding navigation legality separately in spawn code, path code, and behavior code
- making nav probes or buoy regions the semantic authority instead of authoring inputs
- forcing hot-loop Mass execution to reason about UObjects or heavy graph structures directly

## 12. Recommended Next Steps

1. Add navigation participant and route-candidate vocabulary to orchestration docs and types.
2. Define the first minimal navigation legality model:
   - route tags,
   - cohort constraints,
   - startup-profile constraints,
   - degraded/fallback semantics.
3. Decide whether `UFlightNavGraphDataHubSubsystem` should remain a standalone topology hub or eventually become a provider owned by a navigation domain subsystem.
4. Add reporting surfaces that explain selected versus rejected route candidates.
5. Keep the current path-follow runtime as the committed backend until the route-selection layer actually requires a different execution form.

## 13. Short Version

FlightProject should not choose between "spline navigation" and "graph navigation" as if one of them must be the whole system.

The right direction is:

- author multiple navigation primitives
- reduce them into explicit visible descriptors
- evaluate legality at the orchestration/cohort level
- select one committed route
- lower that route into a cache-friendly runtime path
- report why that route was chosen

That makes navigation fit the same architectural pattern already emerging in schemas, VEX, orchestration, and startup policy.
