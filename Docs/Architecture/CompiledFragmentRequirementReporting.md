# Compiled Fragment Requirement Reporting

This note records the current fragment-requirement reporting surface for VEX behaviors and the next step toward a compiled `MassQueryContract`.

For the VEX mutation/storage frame, see [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md).
For Verse runtime modularization and Mass host bundles, see [VerseSubsystemModularization.md](VerseSubsystemModularization.md).
For optic-like ECS/query projection, see [EcsViewsAsOpticLikeProjections.md](EcsViewsAsOpticLikeProjections.md).

## 1. Problem

FlightProject now compiles schema-bound fragment dependency facts from VEX behaviors.
What still needs to improve is the operational contract between:

- what a behavior semantically requires
- what a Mass processor query is configured to provide
- what the current direct execution path can actually route

Before this reporting slice, that mismatch was visible only indirectly through runtime behavior and hardcoded processor assumptions.

## 2. Current Surface

The compile pipeline now emits structured fragment requirement reports per behavior.

Recommended interpretation:

- schema binding owns the semantic fragment requirement facts
- compile artifacts preserve those facts in a reportable form
- orchestration mirrors a summary for operator inspection
- direct processor support is reported honestly against the fragment families we currently route

This gives the project an explicit answer to:

```text
what fragments does this behavior require
what symbols are read or written through each fragment
can the current direct processor path satisfy that fragment family today
```

## 3. Current Data Model

The compiled report is keyed by fragment family and should be read as a semantic requirement report, not yet as a complete execution contract.

Current fields:

- `FragmentType`
- `StorageKind`
- `ReadSymbols`
- `WrittenSymbols`
- `bSupportedByCurrentDirectProcessor`
- `CurrentDirectProcessorSupportReason`

Important rule:

- support reporting is intentionally scoped to the processor/runtime path that exists today
- it is not a generic claim that the fragment family is universally executable on all lanes

## 4. Why This Matters

This report creates a clean bridge between schema binding and runtime query evolution.

It makes three truths inspectable:

1. semantic dependency truth
2. current processor-routing truth
3. mismatch evidence between those two truths

That is useful because the project is now deriving direct fragment-view descriptors from compiled schema bindings, but the Mass query itself is still declared statically in processor code.

## 5. Current Boundary

The current boundary is deliberate:

- compiled fragment requirements are structured and reportable
- orchestration exposes summaries for inspection
- direct support truth is only marked positive for fragment families the current processor/runtime path actually routes

That means this surface is honest, but not complete.

It should not be confused with:

- a compiled Mass query contract
- a processor query generator
- a full legality proof for arbitrary fragment families

## 6. Recommended Next Step

Promote the current fragment requirement report into a compiled `MassQueryContract` surface.

Recommended contract responsibilities:

- express required fragment families and access mode
- distinguish required vs optional fragment participation
- validate processor query configuration against compiled behavior needs
- allow mismatch reporting before execution
- become the input to future query derivation or query selection

Recommended interpretation:

```text
schema binding
    -> fragment requirement report
    -> compiled MassQueryContract
    -> processor/query validation
    -> execution
    -> report
```

## 7. Design Rule

Keep these layers distinct:

- `fragment requirement report`
  what the behavior semantically touches
- `MassQueryContract`
  what a processor must provide for legal execution
- `host bundle`
  how those requirements are realized in concrete storage

That separation prevents schema facts, query legality, and runtime storage realization from collapsing back into one ad hoc mechanism.

## 8. Final Position

Compiled fragment requirement reporting is the right near-term bridge.
It surfaces semantic dependency truth and current routing support without overstating what the runtime can do.

The next step is not to add more summary strings.
The next step is to compile those requirements into a real `MassQueryContract` that can be validated before execution.
