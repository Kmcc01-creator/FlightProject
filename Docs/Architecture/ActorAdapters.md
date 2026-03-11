# Actor Adapters

This note defines the intended role of Unreal actors as adapter surfaces inside FlightProject's schema-bound runtime architecture.

Use this alongside:

- `ProjectOrganization.md` for ownership boundaries
- `WorldExecutionModel.md` for world/runtime lifecycle rules
- `OrchestrationSubsystem.md` for participant/cohort/report integration
- `Navigation.md` for the current navigation-facing vertical slice
- `../Workflow/GameplaySystems.md` for the current gameplay/runtime flow

## 1. Why This Exists

FlightProject increasingly needs to bridge two different models cleanly:

- Unreal-native authored world content
- project-owned runtime state and batch execution

Those are not the same thing.

An actor adapter is the boundary object that lets the project keep strong Unreal authoring ergonomics without making actor instances the long-term authority for runtime simulation truth.

## 2. Core Rule

Use actors as authoring and world-ingress surfaces.
Use project-owned descriptors, schemas, registries, fragments, and execution plans as runtime truth.

Short version:

- actor = placement, metadata, registration, editor UX
- runtime model = compact execution data

## 3. What An Actor Adapter Is

An actor adapter is an Unreal actor or component that:

1. exposes authored data in Unreal-friendly form
2. participates in reflection/schema binding where appropriate
3. lowers authored meaning into project-owned runtime structures
4. triggers invalidation/rebuild when its authored state changes

It is not only a convenience wrapper.
It is the formal bridge between engine-native content and FlightProject runtime contracts.

## 4. What Actors Should Own

Actors remain the right place for:

- editor placement and transforms
- designer-facing `UPROPERTY` authoring
- construction-time preview and validation
- world registration hooks
- level-authored relationships
- direct visualization and debug affordances

This is why actor-backed surfaces such as `AFlightWaypointPath` and `AFlightSpawnSwarmAnchor` still make sense.

## 5. What Actors Should Not Own

Actors should not remain the main authority for:

- hot-loop simulation state
- swarm-scale per-entity mutation
- execution-domain selection
- VEX/Verse runtime storage
- route commitment for large groups
- repeated per-frame batch decisions

Those responsibilities should live in:

- Mass fragments
- registries and snapshots
- orchestration participant/cohort/candidate records
- compact schema/runtime artifacts
- GPU buffers where applicable

## 6. Required Adapter Surfaces

Each actor adapter should be understood in four layers.

### 6.1 Authoring Surface

The Unreal-facing form:

- `UPROPERTY` values
- actor transform
- optional components
- editor categories and defaults

### 6.2 Reflection / Schema Surface

The contract-facing form:

- reflected fields
- `VexSymbol` attrs when meaningful
- `VexCapableAuto` or `VexCapableManual` classification
- provider hooks where actor-backed types need manual schema resolution

This layer should expose only coherent runtime meaning, not arbitrary actor internals.

### 6.3 Runtime Lowering Surface

The lowered execution form:

- orchestration participant records
- cohort records
- navigation candidates
- registry entries
- Mass fragments or shared fragments
- GPU resource descriptors

This is the most important part of the adapter.

### 6.4 Invalidation Surface

The rebuild triggers:

- construction changes
- begin play registration
- end play unregister
- runtime metadata edits
- orchestration rebuild requests
- schema/provider refresh where needed

## 7. Identity Rules

Every actor adapter should provide a stable identity that does not depend on raw actor pointer lifetime.

Useful identity forms include:

- authored `FName`
- authored `FGuid`
- reflection runtime key
- schema type identity plus layout hash
- orchestration participant handle
- registry-issued runtime handle

The exact identity may differ by layer, but adapters should avoid leaving identity implicit.

## 8. Storage Rules

Actor adapter fields should be explicit about where they live after lowering.

Typical destinations:

- actor property only
- Mass fragment field
- Mass shared fragment field
- registry/LUT entry
- orchestration descriptor field
- GPU buffer element

This should line up with SchemaIR storage kinds rather than becoming a parallel one-off mapping system.

## 9. Read / Write Policy

Not every authored field should be writable at runtime.

Useful categories:

- authoring-only
- runtime-readable
- runtime-writable
- report/debug mirror only

For swarm-scale paths, prefer:

- actor holds authoring metadata
- runtime writes happen to fragments/schemas
- optional report surfaces mirror state back for debugging

Direct VEX mutation of arbitrary actor properties should remain the exception.

## 10. Batch Processing Bridge

Actor adapters matter most when one authored object represents many runtime entities.

This is already happening in FlightProject:

- one spawn anchor can describe a whole swarm cohort
- one waypoint path can feed many entities through a single `PathId`
- one route candidate can be selected for a whole cohort
- one shared fragment or registry entry can serve many entities

The intended batch bridge is:

1. authored actor declares intent and placement
2. adapter lowers to participant/descriptor form
3. orchestration resolves legality and selection
4. spawn/runtime code lowers into fragments/shared state
5. processors run in batch

This is the preferred model for scalable runtime behavior.

## 11. VEX Compatibility Guidance

Actor adapters should be VEX-compatible only where the meaning is stable and runtime-coherent.

Good VEX surfaces:

- intent values
- route bias values
- cohort policy values
- committed runtime state already lowered into fragments
- field samples and compact observation data

Poor VEX surfaces:

- arbitrary component pointers
- incidental actor-only editor state
- transient visualization-only members
- UObject graphs that do not have a stable schema/runtime representation

The rule is:

- reflect narrowly
- expose meaningful symbols
- lower explicitly

## 12. Current FlightProject Examples

### `AFlightWaypointPath`

Current role:

- authored spline path
- nav network/subnetwork metadata source
- path registry registration source
- orchestration waypoint-path participant source
- navigation candidate source

Lowered runtime forms:

- `PathId`
- `UFlightWaypointPathRegistry` LUT entry
- `FFlightNavigationCandidateRecord`
- `FFlightPathFollowFragment` reference target

### `AFlightSpawnSwarmAnchor`

Current role:

- authored swarm spawn request
- behavior legality and preference source
- navigation intent/source metadata
- optional nav-graph node registration source

Lowered runtime forms:

- orchestration participant
- anchor-scoped cohort
- desired network/subnetwork constraints
- spawn ordering inputs
- cohort behavior legality metadata

## 13. Current Gap

The actor-adapter direction is real, but not fully unified yet.

Today:

- orchestration can reduce actors into participants, cohorts, navigation candidates, and selected execution-plan commitments
- runtime processors already operate on fragments and registries
- the swarm spawner now consumes orchestration-selected navigation commitments during spawn
- actor-backed navigation sources now lower into both current path execution data and shared navigation-commit runtime metadata

That means actor lowering is now strong through visibility, reporting, batch reconciliation, and spawn commitment. The remaining gap is deeper in runtime execution: more systems still need to consume the richer commit-product/shared-fragment handoff instead of inferring everything from `PathId`.

## 14. Recommended Categories

The project should likely standardize at least these adapter categories:

- spawn adapters
- navigation adapters
- field adapters
- behavior/provider adapters
- presentation/debug adapters

Each category may use different lowering targets, but should follow the same actor-to-runtime pattern.

## 15. Build-Tier Guidance

Actor adapters should follow the broader reflection retention split.

Editor/dev:

- full reflected metadata
- provider hooks
- validation and parity surfaces

Lightweight runtime:

- compact runtime schema/descriptor data
- stable identity
- execution-relevant fields only

Hot-compile/editor iteration:

- full metadata plus invalidation/rebuild hooks

This keeps authoring rich without forcing shipping runtime to carry unnecessary reflective state.

## 16. Practical Next Steps

1. Formalize an adapter vocabulary in runtime code for actor-backed providers.
2. Identify which current actor types are:
   - authoring-only
   - authoring plus runtime adapter
   - temporary scaffolding
3. Route `UFlightSwarmSpawnerSubsystem` through orchestration-selected navigation commitments.
4. Add explicit schema/provider guidance for actor-backed VEX-capable types.
5. Expand commit-product/shared-fragment consumers so one actor-backed request can lower cleanly to many runtime systems, not only the path-follow backend.

## 17. Short Version

FlightProject should not choose between "traditional Unreal actors" and "custom runtime systems."

The right model is:

- keep actors for authoring and world integration
- use actor adapters as the bridge
- lower into schemas, registries, cohorts, fragments, and buffers
- let batch runtime systems own execution truth

That preserves Unreal ergonomics without giving up the project’s schema-driven and swarm-scale runtime goals.
