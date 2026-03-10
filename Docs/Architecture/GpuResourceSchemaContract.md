# GPU Resource Schema Contract

This document defines how FlightProject should think about buffers, images, access patterns, synchronization, and backend binding through the reflection and schema system.

The goal is not to turn the project into a handwritten Vulkan engine.
The goal is to give reflection, SchemaIR, RDG/RHI, and orchestration a shared contract for GPU resource intent.

For project organization and ownership boundaries, see `Docs/Architecture/ProjectOrganization.md`.
For the current world-scoped execution/report boundary, see `Docs/Architecture/OrchestrationSubsystem.md`.
For target normalization and policy derivation, see `Docs/Architecture/TargetCapabilitySchema.md`.
For the existing mutation-centric schema frame, see `Docs/Architecture/VexStateMutationSchemaFrame.md`.
For the current Vulkan 1.3 modernization baseline, see `Docs/Architecture/Vulkan_RHI_Modernization.md`.

## 1. Why This Contract Exists

FlightProject is already operating in a mixed GPU model:

- reflection can emit HLSL-facing struct layouts and symbol metadata
- VEX lowering can target GPU storage and mutation semantics
- runtime systems already own persistent GPU resources
- Unreal RDG/RHI already provides most resource construction, lifetime, and transition machinery
- narrow subsystems already step down into raw Vulkan for external synchronization and interop

Without a stable resource contract, those layers drift toward:

- stringly shader bindings
- hidden assumptions about buffer vs image use
- ad hoc transition logic
- backend-specific policy leaking upward into gameplay code
- raw Vulkan usage spreading beyond the few places where it is actually justified

The contract in this document is intended to prevent that.

## 2. Core Thesis

Treat GPU resources as schema-bound execution contracts, not only as backend objects.

In practice:

- reflection should describe what kind of resource a field or system requires
- schema should describe what accesses are legal
- RDG/RHI should realize normal resource construction, transitions, and pass wiring
- raw Vulkan should only appear where Unreal does not expose the required backend feature directly

This keeps the project aligned with its broader pattern:

`current state + contract -> projected next states -> validation -> selection -> commit -> report`

For GPU work, that becomes:

`resource contract -> legal access patterns -> candidate execution graph -> validated transitions -> committed passes -> reported bindings/state`

## 3. The Resource Model We Actually Need

The important split is not simply "buffer vs image."
The useful split is:

1. resource identity
2. resource capability
3. resource state
4. synchronization and visibility
5. ownership and reporting

### 3.1 Resource Identity

Identity answers:

- what logical resource is this?
- what domain owns it?
- what semantic role does it serve?

Examples:

- `SwarmStateBuffer`
- `PerceptionLattice`
- `CloudField`
- `OrchestrationDebugOverlay`
- `ReadbackTelemetryBuffer`

Identity should remain stable across backend realization.

### 3.2 Resource Capability

Capability answers what the resource is allowed to be used for.

This is where Vulkan-style concepts such as `TRANSFER_SRC`, `TRANSFER_DST`, `STORAGE`, `SAMPLED`, and attachment usage matter.

Examples of capability classes FlightProject should model explicitly:

- upload/staging
- readback
- uniform/parameter
- structured state buffer
- storage buffer
- indirect/dispatch argument buffer
- sampled image
- storage image
- transient attachment
- persistent extracted render target
- external/exportable synchronization-adjacent resource

Capability is not the same as current state.
It is the legal envelope for future states.

### 3.3 Resource State

State answers how the resource is being used right now.

Examples:

- host upload
- transfer source
- transfer destination
- shader sampled read
- storage read/write
- color attachment write
- depth attachment read/write
- presentable surface
- external wait/signal participation

For images, this also includes layout-style state.
Layouts should be treated as state, not identity.

### 3.4 Synchronization and Visibility

Synchronization answers how one legal use becomes visible to the next.

This is the place where the user intuition of "src/dst flags" becomes precise:

- source stage and access
- destination stage and access
- visibility of prior writes
- legality of later reads or writes
- queue/domain handoff when relevant

This is why Vulkan feels strict but coherent.
The API is forcing the engine to say:

- who wrote
- what they wrote
- when it becomes visible
- who is allowed to read or write next

### 3.5 Ownership and Reporting

Ownership answers who is authoritative for:

- descriptor truth
- runtime allocation
- transition scheduling
- commit timing
- diagnostics and failure reporting

Recommended split:

- reflection/schema owns semantic contract
- the runtime system owns concrete resource realization
- RDG/RHI owns ordinary scheduling and transition realization
- orchestration owns world-scoped visibility and reports

## 4. What Unreal Already Gives Us

The project should default to Unreal's rendering abstractions, not bypass them by reflex.

Unreal already provides the important mid-level mechanisms:

- shader parameter structs and global shader binding surfaces
- `FRDGBuilder` for pass graph construction
- RDG buffer and texture descriptors
- pooled render targets and extracted persistent resources
- `ERHIAccess`-style access-state expression
- render-graph-managed transitions and hazard resolution
- RHI surfaces for backend-agnostic resource creation and extraction

This is already the project's dominant working model.

Current examples in FlightProject:

- `UFlightSwarmSubsystem` manages persistent GPU textures, extracted targets, and compute-driven field/state resources
- `UFlightSimpleSCSLShaderPipelineSubsystem` uses Unreal render hooks and RDG-style pass composition for lightweight rendering
- the GPU perception path uses RDG buffers, compute passes, extraction, and standard Unreal resource wiring

That is the correct default direction.

## 5. When Raw Vulkan Is Actually Justified

Raw Vulkan is justified only when the engine surface does not model the feature we need at the required granularity.

Current examples that fit this exception:

- external semaphore export/import
- external fence FD interop
- queue-level Vulkan synchronization details
- native extension registration and function loading

FlightProject already has narrow escape-hatch code for this in the Vulkan/io_uring interop path.
That is the right pattern:

- backend-specific
- isolated
- capability-gated
- optional
- not the primary authoring surface

Recommended rule:

- if the task can be expressed as normal pass/resource work, stay in RDG/RHI
- if the task requires Vulkan-native extension semantics Unreal does not expose, use a backend adapter
- do not push raw Vulkan handles upward into gameplay, orchestration, or schema code

## 6. Direct Answer: Unreal or Raw Vulkan?

The answer is: mostly Unreal, with raw Vulkan as a narrow backend escape hatch.

### 6.1 Use Unreal Mechanisms By Default

Use Unreal when the problem is:

- creating buffers or textures
- expressing transient vs persistent render resources
- binding shader parameters
- composing compute or render passes
- managing ordinary transitions and visibility
- extracting resources across frames
- building world-facing rendering or simulation systems

This should cover the majority of FlightProject GPU work.

### 6.2 Use Raw Vulkan Only For Missing Backend Features

Use raw Vulkan when the problem is:

- external synchronization objects
- native Vulkan extension behavior
- FD export/import
- backend-specific queue or semaphore interop
- device/instance capability details Unreal does not surface sufficiently

Even then, wrap it behind a narrow interface owned by the backend boundary.

### 6.3 Do Not Use Raw Vulkan Just Because It Feels Lower Level

Raw Vulkan is not automatically more architectural.
In many cases it is simply a faster path to duplicated lifetime logic, duplicated transition logic, and backend leakage.

The project should only descend below RDG/RHI when there is a concrete missing engine abstraction or a backend-specific contract that must be preserved exactly.

## 7. Subpasses, Attachments, and "Lower Granularity" Rendering

Classic Vulkan subpasses are not the first abstraction FlightProject should optimize around.

The project's current direction is closer to:

- Vulkan 1.3 baseline
- dynamic rendering
- synchronization2
- compute-heavy simulation
- RDG-managed passes
- explicit resource extraction and reuse

That means the more valuable abstractions are:

- buffers
- images
- storage/read/sample roles
- transient vs persistent lifetime
- access-state transitions
- async and external synchronization boundaries

If we later discover that a specific rendering path truly benefits from subpass-style attachment reuse, that decision should be made from a proven renderer need, not from nostalgia for raw Vulkan structure.

Recommended rule:

- model attachment intent in schema
- realize normal attachment work through Unreal pass construction first
- only consider lower-level engine Vulkan changes if the RDG/RHI path proves insufficient

## 8. Reflection and Schema Responsibilities

Reflection should not generate entire shader programs by default.
It should generate the resource contract surface that shaders and backends rely on.

That includes:

- struct layout definitions
- layout hashes
- resource binding identifiers
- access legality metadata
- residency and lifetime metadata
- domain affinity
- backend aliases
- generated `*.ush` include fragments

This is consistent with the existing reflection direction in FlightProject:

- reflection already emits HLSL-oriented type/layout surfaces
- schema already frames symbol legality, mutation, and residency
- runtime already consumes generated contracts in a few narrow places

The next step is to extend that model from "field mutation contract" to "GPU resource contract."

## 9. Proposed Contract Layers

### 9.1 Layer A: Reflected Resource Intent

Reflection should answer:

- what logical resource does this field or system participate in?
- is it a buffer or image?
- what value shape does it carry?
- is it sampled, written, uploaded, read back, or attached?
- is it transient, persistent, extracted, or external?
- which execution domains may touch it?

Illustrative metadata classes:

- resource kind
- element/value type
- dimensionality
- access policy
- residency policy
- lifetime policy
- extraction policy
- external interop requirement

### 9.2 Layer B: Schema Contract Rows

Schema should normalize reflection and policy into explicit contract rows.

Illustrative row families:

1. `FFlightGpuResourceContractRow`
   - logical resource id
   - owner domain
   - resource kind
   - value/format description
   - dimensions or extent class
   - transient vs persistent policy
   - extraction policy
   - backend eligibility

2. `FFlightGpuAccessRuleRow`
   - resource id
   - legal reader/writer domain
   - access class
   - stage class
   - read/write mode
   - required determinism or ordering policy

3. `FFlightGpuTransitionRuleRow`
   - resource id
   - source use class
   - destination use class
   - required barrier class
   - layout/state notes
   - whether queue/domain handoff is allowed

4. `FFlightGpuExternalInteropRow`
   - resource or sync object id
   - required backend
   - required extension
   - import/export capability
   - fallback behavior if unsupported

These do not need to map one-to-one to current implementation immediately.
They define the contract boundary we want.

### 9.3 Layer C: Generated Shader Contract Artifacts

The schema/reflection layer should be able to generate:

- `*.ush` struct definitions
- binding constants or resource aliases
- layout hash constants
- access helper macros
- optional generated comments or diagnostics for bound resources

This should support authored HLSL, not replace it.

Recommended model:

- handwritten HLSL owns algorithmic structure
- generated include fragments own binding and layout contract
- SchemaIR or VEX lowering may generate small safe code fragments where appropriate

### 9.4 Layer D: Runtime Realization

Runtime systems should realize the contract through:

- RDG resource descriptors
- RHI creation or extraction
- shader parameter binding
- pass graph scheduling
- optional backend-specific interop adapters

World systems such as swarm, perception, and lightweight rendering should remain the owners of concrete runtime resources.

### 9.5 Layer E: Orchestration Visibility

Orchestration should report:

- active resource participants
- active capability profile
- selected backend path
- persistent vs transient resource counts
- external interop availability
- current legality or degradation notes

Orchestration should not own low-level resource transitions.

## 10. Resource Kinds FlightProject Should Model First

The initial contract surface should focus on resource shapes already present or clearly upcoming in the project.

### 10.1 Structured State Buffers

Use for:

- swarm entity state
- compact per-agent data
- behavior-facing state columns

Questions to encode:

- element type
- writable domains
- staging/upload policy
- extraction/readback need

### 10.2 Storage Buffers

Use for:

- compute-generated state
- counters and append-like outputs
- debug or telemetry records

Questions to encode:

- write frequency
- readback timing
- indirect or argument usage

### 10.3 Sampled Images

Use for:

- field sampling
- visualization inputs
- post-process inputs

Questions to encode:

- dimensionality
- sampled-only vs sampled-plus-write variants
- persistent vs transient lifetime

### 10.4 Storage Images

Use for:

- compute-written field textures
- simulation lattice/cloud propagation
- intermediate render/compute outputs

Questions to encode:

- write stages
- later sample/read stages
- clear/rebuild semantics
- format restrictions

### 10.5 Attachments and Transient Render Targets

Use for:

- post-process intermediates
- debug overlays
- visualization passes

Questions to encode:

- attachment role
- transient lifetime
- later sampling/extraction needs

### 10.6 Upload, Staging, and Readback Resources

Use for:

- CPU-to-GPU data population
- GPU-to-CPU telemetry
- validation captures

Questions to encode:

- host visibility needs
- transfer roles
- readback latency tolerance

## 11. Access Classes FlightProject Should Normalize

The schema should stop treating "read/write" as a vague bool and normalize access into real classes.

Recommended initial access classes:

- host read
- host write
- transfer source
- transfer destination
- shader sampled read
- shader storage read
- shader storage write
- color attachment read
- color attachment write
- depth/stencil read
- depth/stencil write
- indirect argument read
- present/external surface use
- external import/export sync participation

These access classes should then map to backend realization.

## 12. Common Transition Patterns To Treat As First-Class

The first version of the contract should model a small number of important patterns well.

### 12.1 Upload Then GPU Consume

Pattern:

- host writes staging/upload resource
- transfer copies into GPU-local resource
- later shader or attachment stages consume it

Typical users:

- startup data
- generated behavior tables
- initial field/state population

### 12.2 Compute Write Then Sample

Pattern:

- compute writes storage image or buffer
- later render or compute stage samples/reads it

Typical users:

- swarm field propagation
- perception products
- debug field visualization

### 12.3 Attachment Write Then Post-Process Read

Pattern:

- render target is written
- post-process or visualization pass samples it later

Typical users:

- lightweight rendering adapters
- debug overlays

### 12.4 GPU Write Then CPU Readback

Pattern:

- compute/render writes resource
- copy/readback path transfers to CPU-visible memory
- diagnostics or automation consumes it

Typical users:

- performance telemetry
- reproducibility captures
- validation data export

### 12.5 GPU Work Then External Sync

Pattern:

- engine-managed GPU work completes
- native Vulkan sync object is exported or observed
- external async system proceeds

Typical users:

- io_uring/Vulkan interop

This pattern is precisely where raw Vulkan remains justified.

## 13. Proposed Design Rule For SchemaIR

SchemaIR should reason about GPU resources the same way it should reason about mutation:

- logical intent first
- legality second
- storage/binding later
- backend commit last

That means a GPU-aware SchemaIR node should be able to answer:

- what logical resource is touched?
- is the operation read, write, copy, sample, or attach?
- is the access legal in this execution domain?
- what state transition is required?
- is the target backend path available?
- if not, what fallback or denial is correct?

This is a much better fit for FlightProject than discovering those facts ad hoc during lowering.

## 14. Decision Table: Unreal vs Raw Vulkan

### Use Unreal RDG/RHI When

- the work is ordinary render or compute pass composition
- the resource is a normal buffer or texture
- transitions can be represented through normal pass dependencies and access declarations
- the resource lifetime is transient or standard persistent extraction
- the goal is project portability and maintainability

### Use Raw Vulkan When

- the feature depends on Vulkan-native extension semantics
- Unreal does not expose the required native capability
- import/export of native sync or handles is required
- queue-level backend interop must be preserved exactly

### Do Not Descend To Raw Vulkan When

- the only reason is "more control"
- the resource could be modeled with RDG/RHI already
- the desired optimization has not been proven to require backend-specific code
- the lower-level path would force backend details into gameplay or schema layers

## 15. Reporting and Diagnostics

If the schema becomes resource-aware, reporting should improve with it.

Recommended diagnostics:

- active resource contract ids
- resolved backend path per resource or domain
- persistent vs transient classification
- legal access summary
- transition failures or degraded paths
- external interop availability
- fallback reasons when a preferred path is unavailable

This should surface in orchestration and automation reports, not only in Vulkan logs.

## 16. Phased Implementation Plan

### Phase 1: Contract Metadata

- extend reflection metadata to describe resource kind, access, residency, and lifetime
- define initial schema rows for resource contracts and access rules
- keep realization logic unchanged where possible

### Phase 2: Generated Shader Contract Surface

- generate `*.ush` resource and layout contract includes
- emit layout/resource hashes and binding aliases
- validate generated contract parity in automation

### Phase 3: Runtime Realization Alignment

Current landed slice:

- schema can now resolve a structured-buffer contract by logical resource id
- schema can validate native runtime type shape against reflected contract stride and layout hash
- `UFlightSwarmSubsystem` now consumes the `Swarm.DroidStateBuffer` contract when creating the persistent RHI/RDG state buffer and RDG sort scratch buffer
- scripting and automation can now validate reflected runtime GPU contracts directly

Still TODO:

- expand contract-driven realization beyond the current buffer-first `FDroidState` path
- add image, sampled/storage image, and transient-attachment descriptor helpers
- normalize common transition patterns beyond the current structured-buffer slice
- make persistent extraction and readback/report paths more schema-aware across multiple resource kinds
- reduce locality around one reflected type and one resource id so the system generalizes to additional GPU contracts cleanly

### Phase 4: Narrow Backend Escape Contracts

- formalize which resource/sync cases require Vulkan-native access
- keep those cases behind isolated backend adapters
- report capability gating and fallback explicitly

## 17. Recommended Working Policy

For FlightProject, the correct default policy is:

- reflection/schema describes intent, legality, and resource shape
- authored HLSL and RDG passes remain the normal execution surface
- orchestration reports active bindings, capability, and degradation
- raw Vulkan stays isolated to backend-specific interop or missing engine features

This preserves the project's architectural direction while still respecting the realities of modern Vulkan resource and synchronization design.
