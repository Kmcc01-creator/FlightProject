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
- `UFlightSwarmSpawnerSubsystem` now commits navigation through orchestration-selected route candidates and lowers the result into the current path runtime

Current graph/probe work is mostly visibility and authoring support:

- `UFlightNavGraphDataHubSubsystem` stores nodes and edges
- `AFlightNavBuoyRegion` generates nav-probe layout rows and registers loop-style graph entries
- `AFlightSpawnSwarmAnchor` can register itself as a graph node for debug/visualization

Current orchestration work is adjacent but not fully integrated:

- orchestration already sees waypoint paths, anchors, cohorts, and behaviors
- anchor cohorts already carry behavior legality
- navigation legality and candidate selection have started moving into orchestration, but the model is still CPU-first and intentionally simple

### 2.2 What This Means

FlightProject currently has:

- a committed movement primitive: sampled spline path data
- a topology/probe visibility primitive: nav graph nodes and edges
- a cohort/behavior binding primitive: orchestration cohorts

These are not yet one model.

### 2.3 Implemented Navigation Slice

The project now has a real vertical slice for navigation-orchestration integration.

Implemented now:

- canonical navigation contract keys exist for:
  - `Navigation.Intent`
  - `Navigation.Candidate`
  - `Navigation.FieldSample`
  - `Navigation.Commit`
- reflected navigation contract types exist for intent, candidate, field sample, and commit
- `FFlightNavigationCommitContract` resolves through a real `VexCapableManual` Mass-backed schema provider
- orchestration promotes real route sources into `FFlightNavigationCandidateRecord`
- route candidates are collected from:
  - nav-graph nodes
  - nav-graph edges
  - waypoint paths
- orchestration reports now expose:
  - `navigationCandidates`
  - legality state
  - rank score and rank order
  - selected candidate information on each execution-plan step
  - top-level diagnostics
- cohort planning now consumes promoted navigation candidates directly instead of treating `Navigation.Candidate` as an advertised capability only
- cohort records now support CPU-first routing constraints:
  - `DesiredNavigationNetwork`
  - `DesiredNavigationSubNetwork`
- spawn anchors and waypoint paths now carry navigation network metadata
- real waypoint-path candidates validate authored routing metadata against nearby nav-graph registrations
- waypoint-path validation currently surfaces warnings/diagnostics rather than hard legality failure
- `FFlightNavigationCommitProduct` now acts as the first-class runtime commit product for selected navigation candidates
- `UFlightSwarmSpawnerSubsystem` now:
  - reconciles anchor batch plans through orchestration
  - consumes the selected execution-plan navigation candidate per cohort
  - lowers non-path candidates into synthetic runtime routes when needed
- spawned cohorts now also carry `FFlightNavigationCommitSharedFragment`, preserving:
  - commit kind
  - source candidate id/name
  - runtime path id
  - initial location
  - synthetic vs direct path lowering

This means the navigation work is no longer only a future design exercise. The project now has a real CPU-first route-candidate and report surface that can be extended toward richer legality, GPU field input, and stricter validation policy.

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

For the current runtime, that committed representation is now best understood as two linked layers:

- `FFlightNavigationCommitProduct`
- `FFlightNavigationCommitSharedFragment`
- current path-follow lowering:
  - `FGuid PathId`
  - `FFlightPathData`
  - `FFlightPathFollowFragment`

That lets FlightProject keep the current cache-friendly Mass path-follow path while moving planning, selection, and non-path route meaning to a better architectural boundary.

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

Current implementation already reports:

- promoted `navigationCandidates`
- candidate legality and legality reason
- candidate rank score and rank order
- selected navigation candidate ID/name per execution-plan step
- cohort desired network/subnetwork
- waypoint-path routing validation status, suggested nearby network/subnetwork, and validation detail
- top-level `diagnostics` entries for waypoint-path routing mismatches and ambiguity

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

That work has started. Cohorts already carry:

- required navigation contracts
- desired navigation network
- desired navigation subnetwork

The next step is to extend cohorts with additional navigation-facing constraints such as:

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

Current CPU-first legality inputs already include:

- candidate-source kind
- candidate availability/status
- cohort-required navigation contracts
- desired network match
- desired subnetwork match
- simple authored-path metadata validation against nearby nav-graph nodes

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

Current implementation provides a narrower but working version of this:

- `bLegal`
- `LegalityReason`
- `RankScore`
- `RankOrder`
- candidate `Status`
- plan-step `NavigationSelectionReason`
- warning diagnostics for routing metadata drift on waypoint paths

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

- select or build a `FFlightNavigationCommitProduct`
- preserve that product as a shared runtime handoff
- assign `PathId` for the current movement backend
- initialize `FFlightPathFollowFragment`
- let `UFlightPathFollowProcessor` do the hot-loop work

That means FlightProject does not need to rewrite movement execution immediately in order to redesign navigation correctly.

That is also the current implementation path:

- CPU-side orchestration selects a candidate
- `UFlightSwarmSpawnerSubsystem` consumes that selected candidate during spawn
- selected candidates lower into `FFlightNavigationCommitProduct`
- spawn-time lowering preserves the chosen commit as `FFlightNavigationCommitSharedFragment`
- navigation commit remains path-follow oriented for movement execution
- Mass continues to execute committed path state through `FFlightPathFollowFragment`

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
- keep `FFlightNavigationCommitProduct` as the shared handoff for route meaning
- move more runtime consumers from raw `PathId` assumptions to commit-product/shared-fragment awareness where that meaning matters

## 11. Source Surface Integration

Navigation should become one of the first production framework surfaces that uses:

- reflected authoring contracts
- schema-backed execution contracts
- orchestration-owned legality and selection
- Mass hot-loop execution
- GPU-produced field visibility and cost data

That gives FlightProject a practical place to prove the reflection/VEX/Verse architecture against a real domain.

### 11.1 CPU Ownership

CPU systems should remain authoritative for:

- route legality
- cohort-aware route selection
- fallback and degraded-capability decisions
- route commitment
- navigation reporting

In practice, CPU-side navigation should answer:

- which route candidates are visible?
- which candidates are legal for this cohort?
- which candidate is preferred?
- why was a route rejected or degraded?

The CPU side should also remain the authority that lowers the selected result into committed runtime state.

### 11.2 GPU Ownership

GPU systems should own dense, parallel-friendly spatial work rather than the full navigation policy surface.

Good GPU responsibilities:

- hazard field generation
- visibility or probe aggregation
- clearance sampling
- crowd-flow or density-field evaluation
- candidate-cost precomputation for large batches

The GPU should not initially become the final route-selection authority.
It should feed reduced field and cost facts back to the CPU/orchestration decision surface.

### 11.3 Mass Ownership

Mass should continue to own committed execution and hot-loop data movement.

Recommended navigation-facing Mass roles:

- intent fragments describing desired destination or route mode
- observation fragments carrying sampled field/cost results
- committed path fragments for current execution
- cohort/shared fragments for route policy and shared commitments

Near-term this should continue to lower into:

- `FFlightPathFollowFragment`
- `FFlightSharedPathFragment`
- `FFlightBehaviorCohortFragment`

That keeps the movement loop cache-friendly even while planning becomes more explicit.

### 11.4 Reflection, VEX, And Verse Ownership

Navigation is a good fit for the reflection/schema stack, but only if each layer keeps a clear role.

Reflection should own:

- authoring-time navigation descriptors
- schema-facing field declarations
- capability and provider classification

VEX should own:

- data-oriented route bias and steering mutations
- fast per-entity or per-cohort cost adjustments
- compatibility with CPU, VM, and GPU lowering

Verse should own:

- mission-level navigation intent
- async coordination
- higher-level fallback or reroute requests

The key rule is:

- editor/dev can keep rich reflection metadata alive
- runtime execution should consume compact `FVexTypeSchema` contracts

Navigation should follow the same rule as the rest of the project:
reflection is the authoring and validation surface, schema is the runtime contract.

### 11.5 Navigation Contract Families

The first useful contract families are:

#### A. Navigation Intent

Describes what a cohort or participant wants.

Suggested fields:

- destination or target anchor
- preferred route class
- forbidden route tags
- reroute policy
- formation width or clearance requirements

#### B. Navigation Candidate

Describes a projected route option before commit.

Suggested fields:

- route id
- route class
- estimated travel cost
- legality status
- degradation or fallback reason

#### C. Navigation Field Sample

Describes dense spatial facts produced by CPU or GPU domain work.

Suggested fields:

- hazard
- clearance
- density
- visibility
- flow direction

#### D. Navigation Commit

Describes the chosen executable result.

Suggested fields:

- selected route id
- commit revision
- fallback mode
- path lowering target

## 12. Implementation Roadmap

The roadmap should explicitly separate:

- dev/editor richness
- runtime execution requirements
- hot-compile/live iteration support

### Phase A: Define Navigation Schemas

Status: mostly implemented for the first vertical slice.

Create the first navigation-facing reflected/schema contracts:

- one reflected navigation intent type
- one reflected navigation candidate/report type
- one manual-provider Mass-backed execution schema
- one GPU-backed field sample schema

This is the minimum vertical slice that proves the framework in a production-facing domain.

Completed in the current slice:

- reflected navigation intent contract
- reflected navigation candidate/report contract
- reflected navigation field sample contract
- manual-provider Mass-backed navigation commit contract

### Phase B: Promote Mass-Backed Navigation Providers

Status: first production seam implemented.

Use the new `VexCapableManual` path for real navigation execution surfaces, not only tests.

Recommended targets:

- a navigation intent fragment group
- a route-commit fragment group
- a field-sample fragment group if Mass needs direct access to sampled results

The purpose is to make Mass-backed navigation schema resolution use the same provider model now proven in automation.

Current implementation:

- `FFlightNavigationCommitContract` is already backed by a real manual schema provider
- that provider lowers onto `FFlightPathFollowFragment`
- this is the first production navigation framework seam using the reflected/schema provider model

### Phase C: Add GPU Navigation Field Contracts

Status: reflected field-sample contract exists, GPU-backed production consumption does not yet.

Introduce reflected GPU-facing descriptors for navigation field buffers or textures.

Those contracts should declare:

- buffer identity
- binding names
- storage kind
- permitted execution domains
- readable navigation symbols

This should initially be used for reduced field/cost data, not full route commitment.

### Phase D: Bind Navigation To Orchestration

Status: substantially implemented for CPU-first planning.

Extend orchestration so navigation is visible as:

- participants needing routes
- route candidates
- legality inputs
- selected route decisions
- explanatory reports

Implemented now:

- navigation contract requirements on cohorts
- real nav-graph participants and promoted candidate records
- waypoint-path candidate promotion
- candidate legality/ranking
- per-cohort candidate selection in `RebuildExecutionPlan()`
- selection reasons on execution-plan steps
- report JSON for candidates, cohort routing constraints, and diagnostics

At this stage, orchestration still treats committed path lowering as a separate execution step.

### Phase E: Add VEX And Verse Navigation Entry Points

Add one narrow behavior slice for each layer:

- Verse requests or modifies navigation intent
- VEX adjusts route bias or local steering weights
- Mass executes the committed result
- GPU contributes field or hazard facts

This gives a true end-to-end proof without prematurely replacing the whole movement system.

### Phase F: Introduce Build-Tiered Reflection Retention

Navigation should be one of the first places where the project enforces the reflection/runtime split explicitly.

Recommended retention model:

- editor/dev:
  - full reflected metadata
  - provider callbacks
  - parity and schema diagnostics
- lightweight runtime:
  - runtime key
  - compact schema contracts
  - layout hash and stable type identity
- hot-compile mode:
  - full reflected metadata plus invalidation/rebuild hooks

The runtime should keep executable navigation schema data alive, not the full authoring reflection payload for every type.

### Phase G: Add Navigation Reporting

Status: first report surface implemented.

The first reports should explain:

- visible route candidates
- selected route
- rejected candidates
- degraded/fallback reason
- capability source:
  - CPU legality
  - GPU field sample
  - orchestration policy
  - cohort constraint

This is necessary if navigation is going to follow the same explicit-contract philosophy as the rest of the project.

Current report surface already includes:

- visible navigation candidates
- selected candidate on plan steps
- rejected/illegal candidate state
- ranking data
- cohort network/subnetwork requirements
- waypoint-path routing validation results
- top-level diagnostics for routing mismatches and ambiguity

## 13. Current TODOs

- decide whether waypoint-path routing validation should remain warning-only or escalate into hard legality failure in stricter build/test profiles
- add stronger legality inputs beyond network/subnetwork matching:
  - route tags
  - startup-profile routing policy
  - degraded/fallback semantics
- feed GPU-produced navigation field facts into candidate legality/ranking
- validate or derive waypoint-path network metadata from richer topology queries rather than the current naive nearby-node sampling
- expand navigation commit beyond the current path-follow-oriented lowering only when the planner actually needs a richer executable form

## 14. What To Avoid

Avoid these failure modes:

- treating `AFlightWaypointPath` as the entire navigation architecture
- promoting `UFlightNavGraphDataHubSubsystem` from debug hub to implicit planner without explicit legality/report surfaces
- embedding navigation legality separately in spawn code, path code, and behavior code
- making nav probes or buoy regions the semantic authority instead of authoring inputs
- forcing hot-loop Mass execution to reason about UObjects or heavy graph structures directly

## 15. Recommended Next Steps

1. Add navigation participant and route-candidate vocabulary to orchestration docs and types.
2. tighten the current CPU-first legality model:
   - route tags,
   - cohort constraints,
   - startup-profile constraints,
   - degraded/fallback semantics.
3. decide which routing diagnostics escalate from warnings into stricter legality policy in dev/test profiles.
4. add one GPU-fed legality/ranking input path through the field-sample contract.
5. decide whether `UFlightNavGraphDataHubSubsystem` should remain a standalone topology hub or eventually become a provider owned by a navigation domain subsystem.
6. keep the current path-follow runtime as the committed backend until the route-selection layer actually requires a different execution form.

## 16. Short Version

FlightProject should not choose between "spline navigation" and "graph navigation" as if one of them must be the whole system.

The right direction is:

- author multiple navigation primitives
- reduce them into explicit visible descriptors
- evaluate legality at the orchestration/cohort level
- select one committed route
- lower that route into a cache-friendly runtime path
- use reflection/schema contracts to connect authoring, VEX/Verse, Mass, and GPU surfaces
- report why that route was chosen

That makes navigation fit the same architectural pattern already emerging in schemas, VEX, orchestration, and startup policy.
