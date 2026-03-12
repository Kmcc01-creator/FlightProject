# Reflected Identity Types

This note defines a small design rule that emerged from the navigation-commit Mass debugging work:

- semantic projection types are useful,
- but runtime identity in Unreal-owned systems must be expressed in the reflection model those systems actually consume.

The immediate prototype target is `FFlightNavigationCommitIdentity`.

## 1. Why This Exists

FlightProject already uses more than one reflection surface:

- Unreal reflection for runtime systems such as Mass shared fragments, serialization, and property export,
- trait-based reflection for FlightProject-native schema, VEX symbol generation, and compile-time field traversal.

Those are complementary, not interchangeable.

The Mass shared-fragment failure on navigation commit metadata made that boundary concrete:

- a type can be semantically meaningful in project code,
- but if Mass deduplicates it through reflected field hashing, then its identity must be visible to Unreal reflection.

## 2. The Design Split

When introducing a new “meaningful” type, decide which role it plays.

### Runtime Carrier Type

Use this when the type is consumed by Unreal-owned runtime systems.

Examples:

- `FMassConstSharedFragment` payloads
- `USTRUCT` state exported through property iteration
- data whose identity must survive hashing/deduplication/serialization

Rules:

- use `USTRUCT`
- reflect identity-bearing fields with `UPROPERTY`
- assume equality, deduplication, and debug export depend on reflected state

### Semantic Projection Type

Use this when the type exists to make intent, legality, or schema explicit.

Examples:

- mutation contracts
- schema-bound state surfaces
- report rows
- selection/commit explanations

Rules:

- prefer small value types
- use trait reflection when the main consumer is FlightProject’s schema/compiler layer
- attach explicit attributes when the type should participate in VEX/schema generation

### Dual-Reflected Boundary Type

Use this when the same value must be legible to both systems.

That is the current prototype pattern for `FFlightNavigationCommitIdentity`:

- Unreal reflection carries runtime identity into Mass/shared-fragment hashing and native property dumps,
- trait reflection exposes the same fields to type-registry lookup and lightweight structured inspection.

## 3. Why Navigation Commit Identity Is A Good Prototype

Navigation commit metadata already behaves like a value object:

- selected source candidate
- selected runtime path
- cohort binding
- lowering/source classification
- synthetic vs authored provenance
- execution-plan resolution flag

That metadata sits exactly on a world-selection -> commit -> report boundary.

It is therefore a good place to prototype a future pattern:

- `Product` types own resolution and transport,
- `Identity` types own reflected sameness,
- `Report` types own human-facing explanation.

The next likely refinement is to split current navigation-commit explanation/report fields away from the runtime identity surface so:

- `Identity` stays minimal, hash-stable, and runtime-authoritative,
- `Report` stays descriptive, operator-facing, and free to grow without changing Mass/shared-fragment sameness.

That split is now the active navigation-commit prototype:

- `FFlightNavigationCommitSharedFragment` carries `Identity` only,
- `FFlightNavigationCommitProduct` carries both `Identity` and `Report`,
- runtime sameness checks and Mass deduplication now ignore explanation-only fields such as candidate display name and provenance flags.

## 4. Inspection Rule

Do not mix “gather field data” and “pretty-print field data” ad hoc at each call site.

Prefer:

1. a utility that extracts reflected field rows,
2. a formatter that joins those rows for logs/tests/reports.

This keeps diagnostics from depending on incidental container APIs and makes reflected inspection reusable across:

- tests
- verbose runtime logs
- future console commands
- future report exporters

## 5. Safe Development Checklist

Before introducing a new reflected identity type:

1. State what future/state boundary the type represents.
2. Separate identity fields from transport-only or report-only fields.
3. If Mass or another Unreal-owned runtime will hash/deduplicate it, make the identity fields `UPROPERTY`.
4. If FlightProject schema/tooling should inspect it, add trait reflection too.
5. Add a direct low-level test for the actual runtime behavior.
6. Add a reflection dump path so debugging can inspect the type directly instead of inferring it indirectly from downstream failures.

## 6. Current Prototype Direction

Current prototype pieces:

- `FFlightNavigationCommitIdentity` as a separate reflected type,
- `FFlightNavigationCommitReport` as the explanation/provenance companion type,
- `FFlightNavigationCommitSharedFragment` carrying that identity explicitly,
- reflected field-dump helpers for:
  - native `UStruct` fields
  - trait-reflected fields

If this pattern continues to pay off, similar splits may make sense for:

- GPU resource contract identity
- orchestration selection reasons
- schema mutation contracts

That prediction is already starting to pay off in orchestration binding selection:

- `FFlightBehaviorBinding` now follows the same role split with nested `Identity` and `Report` value objects,
- `Identity` carries the runtime selection key (`CohortName`, `BehaviorID`),
- `Report` carries selection/execution explanation (`ExecutionDomain`, `FrameInterval`, `bAsync`, `RequiredContracts`, and a structured `Selection` provenance object),
- the orchestration subsystem and runtime behavior processor now consume `Identity` for selection and `Report` for explanation.

The next refinement also landed there:

- binding provenance is now less stringly,
- `Selection` separates:
  - source (`ManualBinding`, `AutomaticSelection`, `GeneratedFallback`, `VerseFallback`),
  - rule (`ExplicitRegistration`, `LowestExecutableBehaviorId`, `PreferredBehaviorId`),
  - default-cohort fallback metadata (`RequestedCohortName`, `FallbackCohortName`, `bUsedDefaultCohortFallback`).

That matters because fallback is not the same kind of fact as initial selection. A structured report can preserve:

- what originally won,
- why it won,
- and what later fallback path was applied.

Current TODO for this lane:

- introduce a structured ranking/provenance companion for binding selection evidence so startup-profile policy, contract filtering, and backend-availability inputs do not collapse into future catch-all strings,
- prefer a reflected `Ranking` or `Evidence` sub-struct once multiple scoring/legality inputs need to travel together through reports and JSON export.

This is useful because behavior selection is another projection boundary:

- cohorts project legal behavior candidates,
- orchestration validates and selects,
- runtime commits to a chosen behavior id,
- reports explain why that specific binding won.

The practical lesson is that `Identity`/`Report` is not navigation-specific. It is a reasonable default split anywhere the project is doing:

- candidate generation,
- legality filtering,
- deterministic selection,
- runtime commit,
- operator-facing explanation.

## 7. Base Variants

To keep this from becoming repetitive boilerplate, dual-reflected systems should converge on a small family of role-shaped base variants.

Recommended default family:

- `Identity`
  - runtime sameness
  - minimal reflected fields
  - stable hashing/deduplication/serialization surface
- `Contract`
  - legality/capability/mutation rules
  - usually trait-reflected first
  - may also bridge to native reflection when runtime validation needs it
- `Product`
  - resolution output or transport bundle
  - may carry runtime pointers/handles and derived values
  - should not be assumed hash-stable by default
- `Report`
  - explanation, diagnostics, ranking reasons, evidence
  - free to be verbose and operator-oriented
  - should not be consumed as runtime identity

That gives authors a clearer question set:

1. What data defines sameness?
2. What data defines legality?
3. What data is only a transport/output bundle?
4. What data only explains why a choice was made?

If the answer to those questions is collapsed into one type, the type is probably doing too much.

## 8. Dual-Reflected Authoring Rule

Treat dual reflection as a boundary pattern, not a default for every type.

Use both reflection systems only when a type genuinely needs all of the following:

- Unreal runtime participation,
- FlightProject schema/introspection participation,
- direct debug/inspection value across both lanes.

When that is true, author the type in this order:

1. define the role (`Identity`, `Contract`, `Product`, or `Report`),
2. define the minimal field set for that role,
3. expose Unreal reflection only for fields the runtime must see,
4. expose trait reflection for fields the schema/tooling layer must inspect,
5. add dump/test utilities before the type spreads across systems.

For orchestration binding selection specifically:

1. `Identity` should stay small enough to drive exact/default/fallback lookup and any future deduplication.
2. `Report` should be free to accumulate richer provenance such as startup-profile policy, ranking evidence, or backend-availability reasons.
3. When provenance has more than one axis, prefer a dedicated reflected sub-struct over concatenated strings.
4. `Product` or plan-step types should compose those roles rather than collapsing them.
