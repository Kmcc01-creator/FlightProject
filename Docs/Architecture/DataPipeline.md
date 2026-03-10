# Data Pipeline

This document describes how FlightProject should think about gameplay-data ingress.

CSV is the current practical default.
It is not the architectural definition of the system.

The stable concept should be:

- typed row or descriptor contracts
- a binding layer that loads or constructs those contracts
- runtime caches owned by the appropriate subsystem
- consumers that depend on typed data, not on file format specifics

That direction fits the current project vision of schema-bound execution, explicit contracts, and binding before execution.

## 1. Short Version

FlightProject currently uses CSV files under `Content/Data` because they are fast to author, diff, and iterate on.

That is useful and should remain supported.

But the long-term data pipeline should treat CSV as one ingress format among several possible sources, including:

- CSV
- JSON
- serialized asset-backed descriptors
- generated manifests or registry-issued data
- programmatic population when that is the cleanest source of truth

The runtime should care about typed contracts and resolved values, not whether those values came from a comma-separated file.

## 2. Current Reality

Today the project mostly works like this:

```text
Content/Data/*.csv
    -> UFlightProjectDeveloperSettings (paths + selected rows)
        -> UFlightDataSubsystem
            -> typed cached rows
                -> bootstrap, swarm spawn, waypoint path, spatial layout, pawn lighting
```

This is still a good practical development loop.

Current examples:

| Source | Typed Contract | Current Use |
| --- | --- | --- |
| `Content/Data/FlightLightingConfig.csv` | `FFlightLightingConfigRow` | lighting/bootstrap policy |
| `Content/Data/FlightAutopilotConfig.csv` | `FFlightAutopilotConfigRow` | swarm autopilot defaults and waypoint loop shape |
| `Content/Data/FlightSpatialLayout.csv` | `FFlightSpatialLayoutRow` | spawned spatial layout rows |
| procedural anchor sheet(s) | `FFlightProceduralAnchorRow` | nav buoy and swarm anchor overrides |

The important part is not "CSV exists."
The important part is that the runtime already consumes typed rows.

## 3. Recommended Architectural Frame

The data pipeline should follow the same shape emerging elsewhere in FlightProject:

```text
description source
    -> typed contract
        -> binding / load / merge
            -> runtime cache
                -> execution consumer
                    -> report
```

In this frame:

- the source may be CSV, JSON, asset serialization, or generated code/data
- the typed contract is the durable semantic surface
- the binding layer resolves source data into runtime-usable form
- runtime consumers should not parse source formats directly
- reports should explain missing, defaulted, overridden, or invalid data

## 4. What Should Stay Stable

### 4.1 Typed Row / Descriptor Contracts

The stable part of the system should be the typed contracts:

- `FFlightLightingConfigRow`
- `FFlightAutopilotConfigRow`
- `FFlightSpatialLayoutRow`
- `FFlightProceduralAnchorRow`
- future row/descriptor types

These should be treated as semantic boundaries, not as "CSV structs."

That distinction matters because the same contract may later be populated from:

- a CSV import
- a JSON document
- a cooked asset
- an editor repair/generation pass
- a registry-generated descriptor

### 4.2 `UFlightDataSubsystem`

`UFlightDataSubsystem` should remain the main game-instance boundary for resolved gameplay-data ingress.

Its job should be:

- load or construct typed contracts
- cache resolved data
- expose typed query methods
- report missing or invalid sources
- shield runtime consumers from raw source-format handling
- arbitrate source selection, binding, and override resolution for general gameplay data contracts

Its job should not be:

- permanently assume CSV is the only ingress mechanism
- make every consumer understand file layout details
- hide ambiguous fallback behavior without reporting it
- become a general-purpose live runtime blackboard
- become the owner of world-scoped active truth, legality, or execution state

Practical interpretation:

- use `UFlightDataSubsystem` for authored or resolved data contracts
- use `UFlightOrchestrationSubsystem` for world-scoped active visibility, legality, bindings, and plans
- use domain subsystems and registries for execution-owned live state

### 4.3 Consumer Boundaries

Runtime consumers should continue asking for typed values:

- `UFlightWorldBootstrapSubsystem`
- `UFlightSwarmSpawnerSubsystem`
- `AFlightWaypointPath`
- `AFlightSpatialLayoutDirector`
- `AFlightNavBuoyRegion`
- `AFlightSpawnSwarmAnchor`
- other startup/orchestration consumers

That is the right boundary even if the backing source changes later.

## 5. CSV's Role Going Forward

CSV still makes sense for several FlightProject use cases:

- tunable numeric tables
- scenario/layout rows
- lightweight designer iteration
- easy diffs in source control
- quick export/import workflows

So the correct move is not "replace CSV everywhere."

The correct move is:

- keep CSV where it is a good authoring surface
- stop treating CSV as the only legitimate source
- design the pipeline so new ingress formats can populate the same contracts cleanly

## 6. Other Ingress Formats That Now Make Sense

### 6.1 JSON

JSON is a good candidate when:

- nested structure matters
- arrays or optional sections become awkward in CSV
- machine-generated outputs need to round-trip cleanly
- reports or manifests should double as editable config

Likely uses:

- startup-profile-adjacent policy payloads
- richer route/topology descriptors
- generated validation or repair inputs

### 6.2 Serialized Assets

Asset-backed or UObject-backed descriptors make sense when:

- Unreal needs to discover, reference, edit, or cook the data directly
- designers benefit from editor-native authoring
- the data participates in content references or soft-object paths

Likely uses:

- startup profiles
- complex authored world policies
- content-driven orchestration descriptors

### 6.3 Registry / Generated Population

Generated or registry-issued data makes sense when:

- reflection already defines the authoritative shape
- schema or manifest generation is the true source of meaning
- duplicating the same information into CSV would add drift risk

Likely uses:

- schema manifests
- symbol registries
- capability reports
- future orchestration-visible descriptors

### 6.4 Programmatic Population

Some data should simply be constructed in code when:

- the source is procedural
- the runtime context is the only meaningful input
- persistence would add maintenance cost without real value

Likely uses:

- temporary scenario defaults
- derived route candidates
- generated fallback layout descriptors

## 7. Recommended Source Taxonomy

FlightProject should think in terms of source classes, not one universal format:

| Source Class | Best Fit |
| --- | --- |
| flat tuning tables | CSV |
| nested or machine-generated config | JSON |
| editor-authored/cookable data | asset/serialized UObject |
| reflection/schema truth | registry or manifest generation |
| runtime-derived defaults | programmatic construction |

The key is that each of these should still resolve into explicit typed contracts before runtime execution consumes them.

## 8. Binding And Merge Direction

As the system grows, the pipeline should support layered binding rather than one flat source.

The likely shape is:

```text
base authored data
    -> profile overrides
        -> scenario/world overrides
            -> runtime/generated overrides
                -> resolved typed contract
```

That becomes especially useful for:

- startup-profile-aware legality
- navigation topology policy
- anchor or cohort overrides
- environment-specific CI/test profiles

The important rule is:

- merge at the contract layer
- report what won and why

Do not push merge semantics down into each individual consumer.

## 8.1 Blackboard Warning

It is tempting to grow `UFlightDataSubsystem` into a convenient shared-state surface for "whatever systems need to exchange."

That would be the wrong direction.

Why:

- `UFlightDataSubsystem` is `UGameInstance`-scoped, not world-scoped
- blackboard-style shared state hides ownership and encourages ambient coupling
- active runtime facts belong with the system that owns meaning or with orchestration when the fact is world-coordination truth

Recommended rule:

- if the data is authored, loaded, merged, selected, or defaulted, `UFlightDataSubsystem` is a good home
- if the data is live, world-specific, execution-owned, or frame-relevant, it should live elsewhere

## 9. Reports And Validation

The pipeline should leave behind machine-readable evidence whenever practical.

Examples:

- missing source path
- failed parse
- invalid row selection
- fallback/default value usage
- override precedence result
- profile or scenario mismatch

That is consistent with the rest of the project direction:

- bind explicitly
- validate before commit
- preserve reports

Near-term TODO:

- the new `FFlightBehaviorCompilePolicyRow` contract should stop being only a loaded data surface
- `UFlightVerseSubsystem::CompileVex(...)` should resolve and apply the best matching policy row
- orchestration/report surfaces should expose which compile policy row was selected and which parts of that policy were actually honored

## 10. Current Pitfalls

### 10.1 CSV-Specific Operational Caveats

These still apply while CSV remains the current default:

1. **Cooking**
   - CSVs loaded from project content paths still need correct cooking/staging coverage.
2. **Row mismatch**
   - Missing selected rows should emit explicit diagnostics and visible fallback behavior.
3. **Locale**
   - CSV export/import still assumes dot-decimal formatting.

### 10.2 Architectural Pitfall

The larger pitfall is treating row structs as permanently married to CSV.

That would make future work harder in exactly the places the project is evolving:

- schema-driven descriptors
- richer orchestration policy
- startup profiles
- navigation legality
- generated manifests

## 11. Recommended Extension Rule

When adding new gameplay data, use this decision order:

1. What is the stable typed contract?
2. Does Unreal need to author/cook/reference it as an asset?
3. Is the data flat-table friendly, or does it require nested structure?
4. Is the real source of truth authored text, generated schema, or runtime construction?
5. What report should explain load/merge/override behavior?

Only after that should you choose the ingress format.

## 12. Practical Guidance For New Data

When adding a new data boundary:

1. Define the typed contract first.
2. Add a resolver/binding path in `UFlightDataSubsystem` or another clearly owned subsystem.
3. Keep source-format parsing contained to the ingress layer.
4. Expose typed queries to runtime consumers.
5. Add diagnostics or validation artifacts when fallback/default behavior is possible.

If CSV is the right format, use CSV.
If JSON, asset serialization, or generated descriptors are cleaner, use those instead.

## 13. Short Policy Statement

FlightProject should be:

- contract-first
- source-format-flexible
- explicit about binding and override behavior
- typed at runtime boundaries
- report-oriented when defaults or failures occur

CSV remains a valid and important tool.
It just should not define the whole architecture anymore.
