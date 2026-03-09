# Logging, Reflection, and Functional Boundary

This note captures how FlightProject should think about three closely related runtime systems:

- structured logging
- trait-based reflection
- functional/result-oriented utility code

These systems are intentionally cross-cutting.
They are not gameplay domains.
They are foundation/runtime services.

That also means they need stronger boundaries than they currently have.

## Why This Note Exists

Recent work on logging tests, reflection normalization, and schema/codegen behavior exposed a common problem:

- the systems are useful together
- but they currently know too much about each other
- and some runtime/editor/UI concerns leaked into what should be reusable foundation code

The goal is not to make them isolated for isolation's sake.
The goal is to define which layer owns which responsibility so test failures point to the right subsystem.

## 1. Logging Boundary

FlightProject should treat logging as three distinct layers.

### 1.1 Native Flight Logging

This is the project-owned logging surface.

Responsibilities:

- emit structured domain events
- carry typed/structured context
- support result/functional logging helpers
- support sink fan-out
- stay usable in runtime, automation, and non-UI contexts

This layer should be the preferred entrypoint for FlightProject-specific logic that wants structured logging semantics.

Examples:

- domain/service logs
- validation reports
- compiler/lowering diagnostics that need structured context
- task/job/execution diagnostics

### 1.2 Unreal Logging Bridge

This is an adapter layer, not the core logger.

Responsibilities:

- forward selected FlightProject logs into Unreal's log device system
- preserve category and verbosity mapping
- make FlightProject logs visible in existing Unreal tooling and automation output

This layer should not become the source of truth for structured logging.
It is a compatibility/output bridge.

### 1.3 Reactive Log Capture and UI

This is an observability consumer layer.

Responsibilities:

- capture Unreal log traffic
- maintain log buffers and filtering state
- feed editor/runtime diagnostics views
- colorize and classify automation output

This layer should not define the semantics of the logger itself.
It is downstream observability infrastructure.

## 2. Reflection Boundary

The reflection system is not just a debug helper.
It is now part of FlightProject's semantic contract surface.

Responsibilities:

- describe type layout and fields
- support serialization and patch/diff behavior
- expose field attributes
- support schema/codegen surfaces such as VEX/HLSL/Verse symbol metadata
- support traversal/visitor patterns for tooling and diagnostics

Practical interpretation:

- reflection is a runtime metadata system
- schema/codegen consumes it
- logging may format through it
- but logging should not own reflection policy

## 3. Functional Boundary

The functional helpers are utility composition tools.
They should make dataflow, validation, and async/result propagation easier.

Responsibilities:

- `TResult` and error propagation
- lazy chains and transformation pipelines
- async continuation helpers
- compositional helpers used by reactive/runtime code

This layer should remain free of UI/editor concerns.
It can integrate with logging by adapters such as `TapLog`, but it should not become logging-specific.

## 4. Desired Dependency Shape

The preferred direction is:

```text
Functional
    -> no dependency on logging UI or editor tooling

Reflection
    -> may use functional helpers
    -> no dependency on logging UI or editor tooling

Flight Logging Core
    -> may use reflection and functional helpers
    -> should not depend on editor tabs/widgets

Unreal Log Bridge
    -> depends on Unreal log output APIs
    -> hangs off logging core as a sink

Reactive Log Capture / Log Viewer UI
    -> consumes Unreal log output and/or logging sinks
    -> should not be required by the logging core
```

## 5. Current Coupling To Reduce

The current codebase still has some coupling that should be treated as transitional:

- `FlightLogging.h` depends on `UI/FlightLogTypes.h`
- the internal log sink writes directly into the global log capture buffer
- recursion guard state is shared between logger dispatch and log capture
- logging tests currently mix logger behavior, UI/VEX UI behavior, and bridge behavior in a single suite

These choices are understandable for an early integrated prototype.
They are not the clean long-term boundary.

## 6. Recommendation On A Dedicated Module

A dedicated module is reasonable, but not as the first move.

Recommended order:

1. stabilize behavioral boundaries with automation tests
2. stop editor/UI dependencies from leaking into the core contracts
3. separate bridge/consumer code from foundation code
4. only then extract a dedicated runtime module if it still improves build/runtime clarity

If extraction happens, the likely split is:

- foundation/runtime module:
  logging core, reflection, functional helpers, row types, codegen-facing metadata helpers
- observability/UI layer:
  log capture, log viewer, editor tabs, menu plumbing

Do not extract a new module while the core logger still conceptually depends on UI capture types.
That would just move the coupling, not reduce it.

## 7. Automation Testing Direction

These systems need more than smoke tests.
They need cross-system behavioral tests.

High-value complex automation coverage:

- logging core -> Unreal bridge -> capture visibility
- reflective logging with const/reference-qualified inputs
- serialization/diff/apply for nested reflectable data
- reflection attribute surfaces driving codegen/schema rows
- functional `TResult` logging adapters
- reactive/functional state flows using reflectable types

The main point is to test boundaries, not just isolated helpers.

## 8. Current TODOs

- TODO: reduce `FlightLogging` dependence on `UI/FlightLogTypes` and log-capture internals
- TODO: split foundation logging concerns from reactive log viewer concerns
- TODO: add complex automation coverage for logging/reflection/functional boundary behavior
- TODO: revisit a dedicated runtime module only after the above coupling is reduced
