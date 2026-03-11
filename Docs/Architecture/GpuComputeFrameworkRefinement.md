# GPU Compute Framework Refinement

This document captures the current architectural considerations for refining FlightProject's GPU compute frameworks.

The goal is not to prescribe one narrow implementation.
The goal is to define the decision frame, the ownership boundaries, and the staged path forward so new GPU work does not become another set of isolated high-performance islands.

For world ownership and execution layering, see `Docs/Architecture/WorldExecutionModel.md`.
For the current world-scoped coordination surface, see `Docs/Architecture/OrchestrationSubsystem.md`.
For the current mega-kernel concept, see `Docs/Architecture/MegaKernelOrchestration.md`.
For resource-level GPU contracts, see `Docs/Architecture/GpuResourceSchemaContract.md`.
For the existing Linux async completion bridge, see `Docs/Architecture/IoUringGpuIntegration.md`.

## 1. Current Situation

FlightProject already has several meaningful GPU-facing systems:

- persistent swarm simulation buffers and textures owned through Unreal RDG/RHI
- a mega-kernel synthesis subsystem driven by orchestration visibility
- an async GPU perception path with io_uring-backed completion signaling
- schema/reflection infrastructure that can describe GPU-capable symbols and resource contracts

These systems are individually useful.
They are not yet operating as one coherent execution model.

The current gap is not simply "more GPU code."
The current gap is:

- runtime backend commit is still reporting-first rather than execution-authoritative
- some GPU paths are RDG-native while others are callback/readback oriented
- sampling and context-building seams are still too subsystem-specific
- orchestration can describe GPU intent more cleanly than the runtime can currently commit it

## 2. Core Thesis

FlightProject should treat GPU compute refinement primarily as a scheduling, residency, and contract problem.

That means the key question is not:

- "should VEX expose one more GPU builtin?"

The key questions are:

- what latency class does this work belong to?
- what resources are authoritative and where do they live?
- who is allowed to schedule the work?
- when is a result legal to consume?
- how is that decision reported and tested?

In FlightProject terms:

`current resources + execution contract -> candidate GPU schedules -> validation -> committed schedule -> reported latency/ownership/result`

## 3. Scheduling Model

The most important early decision is how FlightProject classifies GPU work by consumption timing.

The recommendation is to formalize three execution classes.

### 3.1 Same-Frame GPU

Producer and consumer execute in one RDG-owned timeline for the current frame.

Use this when:

- the consumer is another GPU pass in the same simulation/render schedule
- correctness benefits from immediate availability
- both sides can live under one Unreal render-graph authority

Examples:

- perception field generation feeding navigation-context build
- navigation-context build feeding swarm force/integration
- same-frame compute-generated data used by a render pass that already runs in the same frame graph

### 3.2 Next-Frame GPU

Producer completes in one frame and the consumer intentionally reads the last completed result in a later frame.

Use this as the default for:

- cross-subsystem GPU communication
- work that may run asynchronously or opportunistically
- systems where cadence and freshness can vary independently

This should be treated as a normal, valid runtime mode rather than as a degraded fallback.

### 3.3 CPU-Observed

GPU work completes and then becomes visible to CPU logic through readback, callback, or mirrored telemetry.

Use this for:

- gameplay systems that genuinely require CPU visibility
- editor/debug tooling
- automated validation that needs explicit host assertions
- external completion integrations such as io_uring

This is a necessary class.
It should not define the fast path.

## 4. Default Policy

FlightProject should be frame-lag tolerant by default and same-frame opportunistic by design.

That means:

- correctness should not require same-frame GPU consumption unless a narrow system explicitly opts into it
- same-frame GPU consumption should be used when the producer and consumer can naturally share one RDG schedule
- CPU readback should remain a slow-path observability and integration surface, not the primary runtime dependency

This gives the project room to:

- render every frame while compute updates at a different cadence
- reuse last-completed GPU context when fresh results are not required every frame
- preserve correctness across a wide range of RHI/device scheduling behavior

## 5. Compute And Render Coexistence

The project should think of "rendering and compute side-by-side" as a scheduling problem, not as a special exception.

Recommended interpretation:

- render cadence and compute cadence should be independently configurable
- rendering should be allowed to consume the most recent legal GPU state, even if compute did not update that exact frame
- compute should be allowed to run every frame, every other frame, or on an explicit budgeted cadence depending on the workload

This is already close to how the swarm path behaves conceptually:

- simulation owns persistent GPU state
- render adapters consume extracted persistent resources
- the render path does not need CPU mediation to access those resources

The long-term target is to generalize that pattern beyond the current swarm-specific path.

## 6. Ownership Boundaries

The project should keep a strict separation between who describes work, who owns resources, and who executes passes.

### 6.1 Orchestration Owns Visibility And Selection

`UFlightOrchestrationSubsystem` should own:

- visible participants
- visible services
- contract resolution
- behavior/cohort/domain selection
- reporting of why a GPU path was or was not chosen

It should not own:

- RDG buffers or textures
- pass construction
- RHI transitions
- backend-native synchronization details

### 6.2 Domain Subsystems Own Runtime Resources And Passes

Runtime GPU subsystems such as swarm, perception, or future navigation GPU services should own:

- persistent/extracted GPU resources
- pass graph construction
- cadence and scheduling hooks
- commit-time validation of runtime availability

### 6.3 Reflection And Schema Own Contract Truth

Schema and reflection should remain the source of truth for:

- logical symbol identity
- storage kind
- resource identity
- legal residency and affinity
- backend binding names
- execution-domain legality

### 6.4 Backend Escape Hatches Stay Narrow

Raw Vulkan or platform-native interop should remain behind backend-specific adapters.

Use those escape hatches only when Unreal RDG/RHI does not expose the required feature:

- external semaphore/fence interop
- queue-native signaling details
- platform-specific async completion bridges

They should not become the baseline semantic model for ordinary GPU scheduling.

## 7. RDG-First Runtime Direction

The default runtime direction should remain RDG-first.

That means:

- same-frame GPU fast paths should be expressed as one RDG-owned schedule whenever possible
- persistent multi-frame resources should be extracted and re-registered through Unreal's existing resource model
- ordinary synchronization should be expressed through pass ordering, resource access, extraction, and RDG/RHI state expression

The current io_uring/Vulkan completion bridge is valuable, but it should be interpreted as:

- a slow-path completion and integration surface
- a backend-specific async notification mechanism
- not the required baseline for every GPU compute feature

## 8. Fast-Path Design Rule

When FlightProject adds a new GPU fast path, the preferred design order should be:

1. define the schema/resource contract
2. define the latency class
3. define the runtime owner that will realize the resources
4. define how orchestration selects and reports the path
5. only then add language or lowering features needed to target it

This order matters.

If the project adds frontend syntax before latency and ownership are defined, it tends to create:

- parser-recognized but non-executable semantics
- subsystem-specific builtins with no generic runtime seam
- unclear rules for when a result is legal to consume

## 9. Implications For VEX And The Mega-Kernel

The mega-kernel should evolve from a hardcoded symbol bridge into a schema-driven execution surface.

Near-term implications:

- GPU execution should stop being inferred mainly from `@gpu` syntax alone
- backend selection and compile policy need to become runtime-committed facts
- mega-kernel ABI should be expanded through schema/resource binding rather than handwritten symbol maps
- `EntityIndex` and resource/buffer context should become explicit parts of the GPU call contract

The important design heuristic is:

- add one generic external sampling/storage seam, not one bespoke builtin per subsystem

For example:

- a generic spatial-field or external-buffer sampling operation is preferable to a perception-only special case
- `sample_perception()` can still exist as surface syntax, but it should lower through a durable generic contract

## 10. Implications For Navigation And Reactive Context

Navigation is already relatively mature on the orchestration and CPU-lowering side:

- candidate visibility exists
- legality and route selection exist
- commit lowering into runtime Mass/path-follow state exists
- field-sample symbols are already reflected

The missing vertical slice is GPU-native context building.

The preferred route is:

- perception or spatial-field data remains GPU-resident
- a context-building GPU pass or mega-kernel stage derives navigation-context values
- swarm force/integration or related consumers read that context directly on the GPU
- CPU readback remains optional for tooling, debugging, and CPU-only gameplay decisions

## 11. VerseVM And Scriptable GPU Boundaries

VerseVM should be treated as a CPU-driven control plane, not as the place where data-parallel GPU semantics are directly realized.

That distinction matters.

The project should not force Verse to pretend it is:

- an RDG pass builder
- a Vulkan queue scheduler
- a shader binding surface
- a direct GPU memory mutation API

Instead, Verse should be able to:

- decide when GPU work should be requested
- choose or influence policy
- submit typed intents
- suspend and resume around GPU completion
- fall back to CPU-visible logic when required

The actual GPU work should remain owned by native runtime systems and GPU-lowered execution surfaces.

### 11.1 Recommended Split

Recommended role split:

- VerseVM
  control flow, sequencing, gameplay-facing policy, async suspension/resume
- native GPU script bridge
  typed submission and completion boundary between Verse and the runtime GPU domain
- world/runtime GPU subsystem
  resource ownership, RDG scheduling, cadence, validation, and commit
- VEX/native GPU kernels
  actual data-parallel execution over schema-bound resources

This should be interpreted as:

- Verse = control plane
- GPU runtime = data plane

### 11.2 Why A Native Adapter Boundary Makes Sense

A native C++ adapter boundary is the right direction because it gives the project:

- one place to validate script-issued GPU intents
- one place to map script requests onto real world/runtime resources
- one place to decide same-frame vs next-frame vs CPU-observed realization
- one place to expose truthful handles, futures, diagnostics, and downgrade reasons back to Verse and orchestration

Without that boundary, the project tends to drift toward:

- Verse thunks that know too much about concrete runtime resources
- ad hoc per-feature native functions
- no common model for submission handles, waiting, or failure reporting

### 11.3 Recommended Boundary Shape

The Verse-facing surface should expose typed GPU intents, not raw backend objects.

Good script-facing concepts:

- submit a GPU kernel or context-build task
- await completion
- poll status
- request a CPU-visible mirror when explicitly needed
- bind to named logical resources or orchestration-visible contracts

Bad script-facing concepts:

- raw `FRDGBufferRef`
- raw `FRDGTextureRef`
- raw Vulkan handles
- handwritten string-based shader parameter binding
- direct mutation of backend-owned resource state from Verse

### 11.4 Submission Model

The preferred model is:

1. Verse issues a typed GPU invocation request.
2. The native GPU bridge validates contracts, latency class, and runtime availability.
3. The owning GPU subsystem commits the actual work.
4. Verse receives a submission handle or awaitable token.
5. Verse either continues immediately or suspends until completion.

That gives the project a clean place to define:

- legality
- downgrade behavior
- observability
- error reporting
- cancellation or invalidation rules

### 11.5 Async Semantics

The scriptable GPU boundary should support both fire-and-forget and awaitable execution.

Recommended cases:

- fire-and-forget
  Verse submits work that is consumed later by GPU-native systems and does not need immediate CPU-visible completion
- awaitable
  Verse explicitly suspends until a native submission handle reaches a legal completion point
- mirrored
  Verse requests or consumes a CPU-visible mirror of a GPU result when gameplay or tooling truly requires host visibility

The important rule is:

- awaiting GPU work should suspend on a typed handle or placeholder owned by the native bridge
- the bridge should fulfill that placeholder based on real runtime completion
- CPU readback should not be implied unless the requested contract actually requires a CPU mirror

### 11.6 Verse Should Not Be The GPU Authoring Surface

Verse can be a scriptable control surface for GPU work without becoming the main authoring surface for GPU execution itself.

The preferred authoring split is:

- Verse authors:
  policy, sequencing, fallback, waiting, orchestration-visible intent
- VEX/schema/native runtime authors:
  kernel behavior, storage legality, resource binding, execution residency, and GPU-side context generation

In practice:

- Verse should say "build navigation context for this cohort with this policy"
- native runtime should decide how that request becomes real GPU work
- VEX/native GPU kernels should perform the actual per-entity or per-resource execution

### 11.7 Script Bridge Responsibilities

The native GPU script bridge should own:

- validation of script-issued GPU requests
- translation from script intent to runtime invocation
- creation and tracking of submission handles
- await/poll integration for Verse async behavior
- reporting of downgrade or rejection reasons

It should not own:

- long-term authoritative resource allocation
- broad world-scoped planning
- direct orchestration visibility ownership
- backend-native synchronization details beyond what is required for completion integration

### 11.8 Runtime Owner Responsibilities

The actual owning runtime subsystem should own:

- RDG pass construction
- extracted persistent GPU resources
- pass ordering and transitions
- cadence and scheduling policy
- CPU mirror realization when explicitly required

That owner may be:

- an existing subsystem such as swarm or perception
- a future dedicated GPU navigation/context subsystem
- a narrow execution service attached to one domain

The important requirement is that Verse should call into an owner through the native bridge, not try to become the owner.

### 11.9 Scriptable GPU Contracts

The GPU script bridge should operate on explicit contracts such as:

- program identity
- cohort identity
- required resource contracts
- latency class
- CPU mirror policy
- cadence or freshness policy
- fallback allowance

Those contracts should be visible in reports so the system can explain:

- what Verse requested
- what runtime path was actually chosen
- whether the request ran same-frame, next-frame, or CPU-observed
- why a preferred GPU path was downgraded or rejected

### 11.10 Suggested Interface Direction

The exact API may change, but the design should look like a typed submission boundary rather than a raw backend bridge.

Conceptually:

- `SubmitGpuInvocation(...)`
- `PollGpuInvocation(...)`
- `AwaitGpuInvocation(...)`
- `RequestCpuMirror(...)`
- `DescribeGpuInvocation(...)`

These names are illustrative.
The important part is the shape:

- typed request in
- typed handle out
- explicit completion path
- explicit reporting path

### 11.10A Boundary Operators And SchemaIR

Early VEX pipeline operators such as `|`, `<|`, and `|>` should be interpreted carefully when FlightProject begins using them across host/device or subsystem boundaries.

Recommended interpretation:

- `|`
  composition within one legal execution context
- `<|`
  import from an upstream boundary into the current execution context
- `|>`
  export from the current execution context to a downstream boundary

The important rule is:

- `<|` and `|>` should not mean "perform an immediate copy now"

They should mean:

- boundary-transition intent
- residency and visibility intent
- contract-crossing intent
- possible latency-class implication

In other words, these operators should be understood as declarative boundary markers rather than as hardcoded transfer primitives.

#### Why This Matters

If the project defines `<|` as "host to device copy" and `|>` as "device to host copy" too early, it will immediately create several problems:

- the operators become tied to one backend realization
- GPU-to-GPU boundary crossings become awkward or impossible to express cleanly
- the language begins to imply immediate synchronization where runtime may only be able to guarantee same-frame, next-frame, or mirrored visibility
- orchestration and runtime lose the chance to choose the best legal realization

FlightProject should preserve those choices as long as possible.

#### Recommended Integration Strategy

The recommended flow is:

1. parser recognizes `|`, `<|`, and `|>` distinctly
2. VexIR or an early boundary-aware IR preserves those distinctions
3. SchemaIR resolves what boundary is being crossed and under what contract
4. orchestration/runtime selection determines the legal realization
5. final lowering maps the boundary operator to one of several concrete runtime forms

Possible final realizations include:

- same-domain value flow
- GPU-to-GPU resource dependency or extraction/re-registration
- host-to-GPU submission/binding intent
- GPU-to-host mirror/readback intent
- awaitable completion boundary for script-facing control flow

#### Recommended IR Interpretation

Boundary operators should survive long enough in the pipeline to carry semantic information such as:

- source contract
- destination contract
- resource identity
- execution-domain expectation
- latency class
- CPU mirror requirement
- whether the boundary is awaitable or fire-and-forget

That implies that low-level arithmetic-only IR is not enough by itself.

A boundary-aware layer should exist where the compiler can still distinguish:

- pure functional composition
- resource import
- resource export
- cross-domain context handoff

This could appear as:

- dedicated VexIR ops
- explicit SchemaIR nodes
- or a mixed lowering stage that preserves boundary metadata until planning is complete

The exact representation can vary.
The important part is not losing the boundary meaning too early.

#### SchemaIR Should Be The Legality Gate

SchemaIR is the right place to answer questions such as:

- what boundary is actually being crossed?
- is the crossing host-to-GPU, GPU-to-host, or GPU-to-GPU?
- what named resource or contract is involved?
- is same-frame required, preferred, or unnecessary?
- is a CPU-visible mirror legal or required?
- does this operator describe value movement, context visibility, or submission/commit intent?

This is more important than deciding the final backend primitive early.

The parser gives the syntax.
SchemaIR should supply the legality and contract meaning.

#### Design Rule

Boundary operators should bind to named contracts, resources, or visible orchestration concepts rather than to anonymous transfer semantics.

Good direction:

- "import this navigation context under a visible field/resource contract"
- "export this GPU-derived result as a CPU-visible mirror under an explicit policy"

Bad direction:

- "copy this value to the device now"
- "read this GPU value back now"

The former keeps the system compatible with:

- same-frame GPU realization
- next-frame GPU realization
- CPU-observed mirrors
- backend-specific scheduling choices

The latter prematurely collapses all of those into one imperative transfer story.

#### Interaction With Verse And Scriptable GPU Work

When Verse eventually sequences GPU-aware VEX or SchemaIR work, boundary operators should still follow the same rule:

- they describe boundary intent
- the native GPU bridge and runtime owner decide the concrete realization

That keeps the script boundary honest:

- Verse can request or await a boundary crossing
- native runtime determines whether that becomes an RDG dependency, an extracted resource handoff, or a CPU-observed mirror

#### Working Recommendation

For FlightProject's GPU framework refinement, the recommended interpretation is:

- keep `<|` and `|>` as explicit boundary-intent operators
- preserve them through boundary-aware IR or SchemaIR
- use SchemaIR to validate the legal crossing and required contract
- defer concrete transfer/synchronization realization until runtime/backend commit

This keeps boundary syntax useful without letting it hardcode the wrong execution model too early.

### 11.11 Interaction With Orchestration

Orchestration should remain above the script bridge.

That means:

- orchestration can expose what GPU-capable services and contracts are visible
- Verse can issue requests that reference those visible contracts
- the bridge and runtime owner still decide whether runtime commit is legal right now

This preserves a clean hierarchy:

- orchestration selects and reports
- Verse sequences and requests
- runtime owner commits
- GPU kernels execute

### 11.12 Current Implementation Direction

The current codebase already points toward this split:

- Verse has native thunk and async placeholder concepts
- the GPU completion path already exists as a native subsystem concern
- the generalized GPU host in the Verse subsystem is still only a placeholder

That should be read as a signal that the next step is not "teach Verse to own GPU execution."
The next step is:

- define the native GPU script bridge explicitly
- route Verse async semantics through real submission handles
- keep concrete GPU scheduling in the domain owners

## 12. Contracts To Add Or Strengthen

The refinement path should add explicit runtime contracts for:

- latency class
- cadence
- freshness budget
- same-frame preference vs requirement
- CPU mirror requirement
- async-compute preference
- resource producer and resource consumer identity

These do not all need to be exposed to user-facing authoring immediately.
They should exist in the runtime/planning model early so the system can report truthful decisions.

Examples of useful fields:

- `LatencyClass = SameFrameGpu | NextFrameGpu | CpuObserved`
- `Cadence = EveryFrame | EveryNFrames | Budgeted`
- `LatencyBudgetFrames`
- `bCanRunOnAsyncCompute`
- `bRequiresCpuMirror`
- `bSameFramePreferred`
- `bSameFrameRequired`

Additional useful script-boundary fields:

- `bAwaitableFromVerse`
- `bScriptSubmissionAllowed`
- `bRequiresRuntimeOwner`
- `SubmissionHandleKind`

## 13. Testing And Observability

The scheduling model has direct implications for test design.

### 13.1 Same-Frame GPU Tests

These should prove:

- producer and consumer are wired into one legal graph
- resource transitions and access patterns are valid
- consumer sees current-frame data

### 13.2 Next-Frame GPU Tests

These should prove:

- frame-to-frame extraction and reuse are stable
- consumers read the last committed result rather than undefined state
- cadence changes do not break correctness

### 13.3 CPU-Observed Tests

These should prove:

- readback timing is truthful
- callbacks are tied to completed GPU work
- mirrored CPU state is clearly identified as delayed or observational

### 13.4 Verse Script-Bridge Tests

These should prove:

- Verse can submit a typed GPU request without direct backend access
- awaitable requests suspend and resume on real submission completion
- rejected or downgraded requests report truthful reasons
- CPU mirror requests remain explicit rather than implicit

### 13.5 Reporting Requirements

Reports should explain:

- chosen backend
- committed execution domain
- latency class
- cadence
- why same-frame was used or rejected
- whether a CPU mirror exists
- why runtime commit downgraded from a preferred path

Script-boundary reports should also explain:

- what the script requested
- what runtime owner accepted the request
- whether the request was awaitable
- what submission/completion class was used

## 14. Recommended Staged Path

### Stage 1: Runtime Commit

Make backend selection and compile policy affect actual runtime dispatch.

Success criteria:

- selected backend is not reporting-only
- orchestration reports committed domain truthfully
- GPU execution is rejected or selected for explicit runtime reasons rather than implied heuristics

### Stage 2: Native GPU Script Bridge

Introduce an explicit typed submission/await boundary between Verse and the runtime GPU domain.

Success criteria:

- Verse does not require direct backend/resource access to request GPU work
- submission handles and await semantics are explicit
- downgrade and rejection reasons are surfaced cleanly

### Stage 3: Generic GPU Context And Sampling Seam

Introduce a generic contract for GPU-resident context resources and external sampling.

Success criteria:

- new GPU contexts are not added as handwritten subsystem-specific symbol hacks
- schema can describe the relevant resource identities and bindings

### Stage 4: RDG-Native Fast Path

Build one real same-frame or next-frame GPU vertical slice under one owning subsystem boundary.

Recommended first slice:

- GPU perception or spatial-field data
- GPU navigation-context build
- GPU swarm-force consumption

### Stage 5: Slow-Path CPU Observation

Keep CPU readback, io_uring completion, and editor/debug tooling as explicit mirrors of the fast path rather than as the fast path itself.

### Stage 6: Mega-Kernel Generalization

After the runtime contracts are stable, generalize mega-kernel synthesis to:

- dynamic schema-driven symbol/resource maps
- explicit entity indexing
- broader resource kinds
- truthful reporting of included resources, latency assumptions, and store-back rules

## 15. Pitfalls To Avoid

- Do not treat raw Vulkan interop as the default answer to normal scheduling problems.
- Do not let orchestration own concrete GPU runtime resources.
- Do not make async compute a correctness requirement.
- Do not add subsystem-specific GPU builtins before the generic sampling/resource seam exists.
- Do not rely on CPU readback to define the semantics of an intended GPU fast path.
- Do not equate "same-frame" with "must use backend-native interop"; Unreal RDG should remain the first scheduling tool.
- Do not let Verse become a disguised low-level GPU API.
- Do not expose raw RDG or Vulkan objects directly to script-facing surfaces.

## 16. Working Recommendation

The current working recommendation for FlightProject is:

- use RDG-first scheduling as the default execution model
- make frame-lag tolerance the default semantic assumption
- reserve same-frame GPU consumption for tightly coupled producer/consumer chains that naturally share one graph owner
- keep CPU readback and io_uring as valuable slow-path integration tools
- treat Verse as the control plane for scriptable GPU interaction
- introduce a native typed GPU script bridge rather than direct backend exposure to Verse
- move future GPU framework work through contract definition and runtime commit before expanding the VEX surface area

That path keeps the project aligned with its existing architecture:

- orchestration selects
- Verse sequences and requests
- domain subsystems execute
- schema validates
- runtime commits
- reports explain why
