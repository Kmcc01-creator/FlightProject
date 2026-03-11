# Reflection And Generative Testing Recovery Plan

Comparison basis: current workspace vs `origin/main` snapshot of
`Source/FlightProject/Public/Core/FlightReflection.h` on March 11, 2026.

## Why This Document Exists

We now have two useful references:

1. the current working-tree version of [FlightReflection.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Core/FlightReflection.h), which contains the intended runtime registration additions and attribute work but is structurally broken
2. the last pushed `origin/main` version of `FlightReflection.h`, which is syntactically coherent but lacks the new discovery and auto-registration path we were trying to add

The goal is not to "fix the current file in place" by hand-editing typos. The goal is to restore a compilable, testable reflection core and then re-introduce the new runtime discovery features in a controlled order.

## Comparison Summary

### `origin/main` strengths

- macro line continuations are valid
- template declarations are syntactically correct
- attribute querying surface is coherent
- `FLIGHT_FIELD_ATTR` and `FLIGHT_REFLECT_FIELDS_ATTR` are usable as a compile-time trait layer
- the file includes a broader set of utility code that was not obviously damaged

### current working-tree strengths

- adds `FTypeRegistry::GetAllTypes(...)`
- adds `SerializeFn` to `FTypeInfo`
- adds `TTypeAutoReg<T>` and macro-driven registration intent
- keeps the same trait/attribute direction needed by schema generation and generative testing

### current working-tree failures

- multiple literal `ttemplate` typos
- broken macros because line continuations were lost
- `FLIGHT_REFLECT_BODY`, `FLIGHT_FIELD`, `FLIGHT_REFLECT_FIELDS`, `FLIGHT_FIELD_ATTR`, `FLIGHT_REFLECT_FIELDS_ATTR`, and `FLIGHT_REFLECT_FIELDS_VEX` are not structurally safe
- auto-registration is injected with a fragile `PREPROCESSOR_JOIN(..., __LINE__)` pattern
- the file is no longer a safe base for any generative testing work

## Recovery Principles

1. restore `FlightReflection.h` to a syntactically stable baseline first
2. keep the compile-time trait surface stable while rebuilding runtime registration
3. reconcile reflection discovery with VEX schema lookup through explicit runtime keys, not type-name guessing
4. only bring the complex automation suites back online after the reflection and schema bridge is deterministic

## Proposed Implementation Plan

### Phase 0: Re-baseline `FlightReflection.h`

Goal: get one clean, compileable reflection header before adding more behavior.

Steps:

- start from the last pushed `origin/main` structure for the macro and template sections
- preserve only the minimum working-tree additions that are clearly required now:
  - `FTypeRegistry::GetAllTypes(...)`
  - `FTypeInfo::SerializeFn`
  - runtime auto-registration helper
- repair every macro with explicit trailing `\`
- remove all corrupted `ttemplate` tokens
- ensure `FLIGHT_REFLECT_BODY(TypeName)` expands to a self-contained statement list ending in a semicolon-safe form

Target outcome:

- `FlightReflection.h` builds cleanly under C++23
- existing reflection tests compile again before any generative tests are considered authoritative

### Phase 1: Replace Fragile Static Injection

Goal: keep auto-registration without line-based symbol collisions.

Current problem:

- `PREPROCESSOR_JOIN(bReg_Flight_, __LINE__)` is fragile and not a sustainable uniqueness scheme

Replacement direction:

- move registration emission into an anonymous namespace helper
- use `__COUNTER__` instead of `__LINE__`
- keep the registration variable `[[maybe_unused]]`

Recommended macro shape:

```cpp
#define FLIGHT_REFLECT_FIELDS_ATTR(TypeName, ...) \
    template<> \
    struct ::Flight::Reflection::TReflectionTraits<TypeName> \
    { \
        using FlightReflectSelf = TypeName; \
        static constexpr bool IsReflectable = true; \
        using Type = TypeName; \
        using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
        static constexpr const char* Name = #TypeName; \
    }; \
    namespace { \
        [[maybe_unused]] const bool PREPROCESSOR_JOIN(kFlightAutoReg_, __COUNTER__) = \
            ::Flight::Reflection::Detail::TTypeAutoReg<TypeName>::bRegistered; \
    }
```

This pattern should be mirrored for `FLIGHT_REFLECT_FIELDS_VEX`.

### Phase 2: Make Reflection Discovery And VEX Schema Resolution Share A Runtime Identity

Goal: remove the current type-name guessing between `FTypeRegistry` and `FVexSymbolRegistry`.

Current problem:

- `FTypeRegistry` is name-driven
- `FVexSymbolRegistry` is runtime-key/native-struct driven
- `FlightStructuralParityTests.cpp` currently tries to bridge them through nonexistent string APIs

Recommended change:

- extend `FTypeRegistry::FTypeInfo` with:
  - `const void* RuntimeKey`
  - optional `const UScriptStruct* NativeStruct`
- have `TTypeAutoReg<T>` register the same runtime key convention used by `TTypeVexRegistry<T>::GetTypeKey()`
- add a small resolver helper:
  - reflection type info -> VEX schema runtime key -> `FVexTypeSchema`

Target outcome:

- structural parity can resolve schemas without special cases for `FDroidState`
- reflection discovery and schema discovery refer to the same identity object

### Phase 3: Rebuild Structural Parity As A Schema-Driven Test

Goal: turn `FFlightStructuralParityTest` into a real schema audit instead of a manifest-only symbol check.

Rewrite plan:

- `GetTests()` should enumerate reflectable types from `FTypeRegistry`
- `RunTest()` should:
  - resolve `FTypeInfo`
  - resolve `FVexTypeSchema` from the runtime key
  - skip types that are reflectable but intentionally non-VEX
  - build parser definitions from the schema, optionally merged with ambient manifest definitions
  - generate one probe per VEX symbol
  - run:
    - `ParseAndValidate(...)`
    - `FVexSchemaBinder::BindProgram(...)`
    - `LowerToHLSL(...)`
- only assert HLSL identifier presence when the schema actually declares one

Edge-case handling:

- nested reflectable fields without `Attr::VexSymbol` should not produce direct VEX probes
- unmapped fields should be skipped explicitly, not silently treated as success
- vector and bool fields should be probed, but unsupported lowering/value-type cases should produce a clear test message

### Phase 4: Harden Manifest-Driven Complex Automation

Goal: make `FFlightGenerativeManifestTest` trustworthy and cheap to diagnose.

Fixes:

- validate `Parts.Num()` per command type before indexing
- assert both `bExpectedEnabled` and `bExpectedMounted`
- add an explicit failure path for unknown command prefixes
- keep asset loading optional in commandlet/headless mode, but always validate path legality
- keep the generated command format stable and documented

Target outcome:

- bad manifest rows fail with actionable messages instead of parser accidents
- plugin rows no longer under-test mounted state

### Phase 5: Verification Sequence

Do not re-enable everything at once.

Recommended order:

1. compile `FlightReflection.h` consumers
2. run reflection-focused tests already in tree
3. run schema orchestrator and SchemaIR tests
4. run structural parity complex automation
5. run generative manifest complex automation

Suggested focused commands once implementation starts:

```bash
./Scripts/build_targets.sh Development
./Scripts/run_tests_headless.sh --preset=triage --filter="FlightProject.Vex.Schema.SchemaIr+FlightProject.Vex.Schema.Orchestrator+FlightProject.Integration.Generative.StructuralParity+FlightProject.Integration.Generative.ProjectManifest"
```

## File-Level Execution Plan

### [FlightReflection.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Core/FlightReflection.h)

- restore macro correctness from the pushed baseline
- re-add `FTypeRegistry` enhancements carefully
- replace line-based auto-registration
- keep `Attr` and `TAttributedFieldDescriptor` API compatible with current schema orchestrator usage

### [FlightVexSchemaOrchestrator.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Vex/FlightVexSchemaOrchestrator.h)

- after the header is stable, re-audit `GetAttr<...>` usage against the repaired descriptor API
- keep full qualification for reflection attrs
- do not widen type support until the reflection core is compiling again

### [FlightStructuralParityTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStructuralParityTests.cpp)

- replace string-based schema lookup with runtime-key-based lookup
- swap manifest-only validation for schema-binding validation
- add explicit skip reporting for reflectable-but-non-VEX types

### [FlightGenerativeManifestTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightGenerativeManifestTests.cpp)

- harden command parsing
- validate mounted state
- add unknown-command failure coverage

### [UFlightVerseSubsystem.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h)

- no large changes required for the recovery itself
- keep namespace qualification fixes in place
- only touch if the reflection header API repair changes dependent types

## Recommended Immediate Next Move

The first implementation slice should be:

1. reconstruct `FlightReflection.h` from the pushed baseline plus the minimal registry additions
2. make the macros and template declarations compile-clean
3. build before changing either generative automation test

That keeps the recovery on the actual dependency chain instead of trying to debug test behavior on top of a broken reflection core.

## Phase 3: Classification And Provider Metadata

Phase 0 through Phase 2 recover the reflection core and remove the most brittle test-local bootstrap logic.
The next step is different in character: reduce authoring cost for new schema-backed types so contributors do
not need to understand the registry internals before they can participate.

### Phase 3 Goal

Treat reflected type metadata as the entry point for schema discovery.
A developer should be able to define a reflectable type, add `VexSymbol` field attributes, and have the type:

- discovered by reflection automation
- resolved through the VEX schema bridge
- audited by structural parity tests
- compiled without extra test-local bootstrap code

### Classification Model

We should make the reflected-type contract explicit instead of inferring intent from partial registry state.

Recommended classification states:

- `NotVexCapable`
  - reflected type participates in reflection and generative discovery, but has no VEX-facing contract
- `VexCapableAuto`
  - reflected type has enough metadata for the bridge to materialize a schema lazily from reflection alone
- `VexCapableManual`
  - reflected type is intentionally VEX-facing, but requires a custom provider because storage/layout policy is not fully implied by field reflection

The classification should live in `FTypeRegistry::FTypeInfo` and be derived as close to registration time as
possible. That keeps tests and runtime code from re-implementing the same policy in different places.

### Provider Metadata Model

Classification alone is not enough. The registry also needs a provider surface that explains how to cross the
boundary from reflected type metadata to an executable `FVexTypeSchema`.

Recommended metadata on `FTypeInfo`:

- shared runtime identity:
  - `RuntimeKey`
  - lazy native-struct resolver
- schema-capability metadata:
  - `VexCapability`
  - `EnsureVexSchemaRegisteredFn`
- optional future extension:
  - provider diagnostics callback or status object for explaining missing/invalid schema generation

The current implementation slice starts with the minimum viable provider surface:

- shared runtime key between reflection and `TTypeVexRegistry<T>`
- lazy native-struct resolution
- `EnsureVexSchemaRegisteredFn` for reflected types that are auto-schema capable

That is enough to let `FVexSymbolRegistry::GetSchemaForReflectedType(...)` attempt lazy schema resolution
without test-local registration code.

### Authoring Policy

The intended authoring workflow should now be:

1. define a reflectable type
2. mark VEX-facing fields with `Attr::VexSymbol`
3. let reflection registration classify the type automatically
4. allow schema resolution to self-materialize through the reflected metadata path

Manual provider registration should remain available, but it should be reserved for cases like:

- Mass fragment bindings
- accessor-backed storage
- GPU buffer contracts
- types whose storage policy cannot be inferred from field descriptors alone

### Testing Implications

Structural parity should move from "best effort probe loop" to a classification-aware audit.

For each reflected type:

- if classification is `NotVexCapable`, skip with an explicit reason
- if classification is `VexCapableAuto`, require reflected metadata to resolve a schema through the provider path
- if classification is `VexCapableManual`, require either a custom provider or an explicit diagnostic explaining why the type is not yet materializable

That gives automation output that is actionable for contributors instead of silently skipping or relying on
hidden bootstrap lists.

### Initial Implementation Slice

The first slice of Phase 3 should be intentionally narrow:

1. add classification/provider metadata to `FTypeRegistry::FTypeInfo`
2. derive "auto-schema capable" from `VexSymbol` field metadata for `FLIGHT_REFLECT_FIELDS_ATTR(...)`
3. keep `FLIGHT_REFLECT_FIELDS_VEX(...)` as an explicit opt-in for guaranteed VEX capability
4. route reflected-type schema resolution through `FVexSymbolRegistry::GetSchemaForReflectedType(...)`
5. remove hardcoded registration/bootstrap lists from structural parity where reflected metadata is now sufficient

This slice is complete when:

- reflected VEX-capable types can resolve schemas by runtime key without test-local bootstrap code
- structural parity discovers those schemas through reflected metadata alone
- runtime/native-struct identity remains lazy enough to avoid static-init package asserts

### Follow-On Work

Once the classification/provider slice is stable, the next Phase 3 tasks should be:

1. expand provider metadata beyond "ensure registered" into richer status/diagnostic reporting
2. add at least one manual-provider framework path, preferably Mass-backed storage
3. expose a developer-facing audit/report that lists:
   - reflected types
   - runtime key presence
   - VEX capability classification
   - schema resolution result
   - symbol count and storage summary
4. document the low-click authoring workflow for adding a new VEX-capable type

### Verification Gates

Phase 3 should be considered healthy only when all of the following are true:

- a new reflected type with `VexSymbol` attributes resolves through the shared reflected-type schema path
- structural parity no longer depends on a hardcoded VEX bootstrap list for auto-schema-capable types
- schema resolution remains safe during static registration and headless startup
- automation output distinguishes:
  - intentionally non-VEX types
  - VEX-capable types missing providers
  - invalid schema materialization
  - successful schema resolution

This phase is primarily about lowering cognitive overhead.
The underlying architecture already has the necessary semantic pieces; the work here is to make those pieces
discoverable, self-describing, and cheap to use.
