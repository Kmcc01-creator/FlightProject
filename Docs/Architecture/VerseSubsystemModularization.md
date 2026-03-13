## Verse Subsystem Modularization

This note defines how `UFlightVerseSubsystem` should be modularized without losing its role as the world-scoped VEX/Verse facade.

For the current orchestration/runtime boundary, see [OrchestrationSubsystem.md](OrchestrationSubsystem.md).
For the arbitrary-symbol vector/storage direction, see [VexArbitrarySymbolVectorContract.md](VexArbitrarySymbolVectorContract.md).
For compiled fragment dependency reporting and the next query-contract step, see [CompiledFragmentRequirementReporting.md](CompiledFragmentRequirementReporting.md).

### Problem

`UFlightVerseSubsystem` currently owns too many implementation concerns in one place:

- behavior registry and lifecycle
- compile pipeline and policy resolution
- storage-host classification and execution
- direct Mass/GPU bridging
- composite behavior registration and execution
- selector guard evaluation
- Verse VM bridge and native thunk registration
- commit/report mirroring

The subsystem should remain the world-owned facade, but it should stop being the sole implementation container.

### Design Rule

Keep `UFlightVerseSubsystem` as:

- the world-facing API surface
- the owner of behavior state and lifecycle
- the coordination point that delegates to narrower collaborators

Move implementation-heavy logic into dedicated Verse runtime files and helpers.

### Recommended Module Split

#### 1. Behavior Types

Candidate files:

- `Public/Verse/FlightVerseBehaviorTypes.h`

Own:

- `FVerseBehavior`
- `FBehaviorExecutionResult`
- `FBehaviorCapabilityEnvelope`
- selector guard and branch structs
- storage-host records and enums

#### 2. Compile Pipeline

Candidate files:

- `Private/Verse/FlightVerseCompilePipeline.cpp`

Own:

- `CompileVex(...)`
- compile policy resolution
- IR/backend selection handoff
- compile artifact finalization
- VM procedure creation

#### 3. Storage Hosts

Candidate files:

- `Private/Verse/FlightVerseStorageHosts.cpp`

Own:

- storage-host classification
- schema AoS/accessor execution
- direct host routing
- GPU host submission bridge
- runtime symbol reads tied to resolved storage hosts

This is the primary seam for fragment/buffer generalization.

#### 4. Mass Host Bridge

Candidate files:

- `Private/Verse/FlightVerseMassHostBridge.cpp`

Own:

- Mass fragment runtime schema adaptation
- fragment read/write helpers
- direct fragment host materialization
- later `FMassFragmentHostBundle`

This is where the runtime should grow from fixed `Transform + DroidState` assumptions into arbitrary fragment bundles.

The current project state now includes:

- `FMassFragmentHostBundle`
- resolved symbol access over Mass host bundles
- compiled fragment requirement reporting derived from schema binding

That means the next Mass-bridge pressure is no longer only host materialization.
It is also query-contract alignment:

- what fragments a behavior requires semantically
- what the processor query actually exposes
- what the current direct path can route honestly

That is the seam where a compiled `MassQueryContract` should land.

#### 5. Composite Execution

Candidate files:

- `Private/Verse/FlightVerseCompositeExecution.cpp`

Own:

- composite registration
- capability-envelope aggregation
- sequence/selector execution
- selector guard evaluation and branch evidence

#### 6. Reporting

Candidate files:

- `Private/Verse/FlightVerseReporting.cpp`

Own:

- commit truth updates
- selected-vs-committed backend explanations
- compile/orchestration report mirroring

#### 7. Verse VM Bridge

Candidate files:

- `Private/Verse/FlightVerseVmBridge.cpp`

Own:

- native Verse function registration
- VM entry thunk creation
- running-context integration
- native symbol thunk dispatch

### Refactor Policy

Modularization should proceed in narrow slices:

1. extract implementation without changing public API
2. keep `Behaviors` ownership in `UFlightVerseSubsystem`
3. avoid introducing new world subsystems for internal helpers
4. move toward generic host bundles only after storage-host code is isolated

### First Slice

The first recommended refactor slice is:

- add `FlightVerseStorageHosts.cpp`
- move storage-host classification and execution there
- move Mass fragment runtime bridging there or into a paired `FlightVerseMassHostBridge.cpp`
- keep the subsystem header unchanged except for any minimal include or type cleanup

This slice is intentionally structural. It should not change runtime semantics.

### Why This Order

The current architectural pressure is coming from fragment/buffer generalization, not from the public subsystem API itself.

So the first extraction should target the code that answers:

- what storage host does this behavior need?
- how is that host materialized?
- how are fragment or buffer-backed symbols actually read and written?

Once that code is isolated, introducing `FMassFragmentHostBundle` and later `FGpuBufferHostBundle` becomes a local change instead of another expansion of `UFlightVerseSubsystem.cpp`.
