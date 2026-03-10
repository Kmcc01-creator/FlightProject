# SchemaIR Implementation Plan

This document turns the SchemaIR exploration into a concrete file-by-file implementation plan against the current FlightProject source tree.

For the architectural rationale, see `Docs/Workflow/SchemaIrImplementationExploration.md`.

## 1. Goal

Add a schema-bound semantic layer to the VEX compiler without destabilizing the current AST, low-level IR, or Verse/SIMD execution paths.

The first goal is not to rewrite the compiler.
The first goal is to create a stable schema boundary that:

- removes fake `UScriptStruct*` type-key usage
- separates logical symbol meaning from storage binding
- prepares AST-to-SchemaIR binding
- keeps current backends functioning during migration

## 2. Current Code Surfaces

The implementation will build on these existing files:

- `Source/FlightProject/Public/Vex/FlightVexSchema.h`
- `Source/FlightProject/Public/Vex/FlightVexBackendCapabilities.h`
- `Source/FlightProject/Public/Vex/FlightVexSymbolRegistry.h`
- `Source/FlightProject/Private/Vex/FlightVexSymbolRegistry.cpp`
- `Source/FlightProject/Private/Vex/FlightVexBackendCapabilities.cpp`
- `Source/FlightProject/Public/Vex/FlightCompileArtifacts.h`
- `Source/FlightProject/Private/Vex/FlightCompileArtifacts.cpp`
- `Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h`
- `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`
- `Source/FlightProject/Public/Core/FlightReflection.h`
- `Source/FlightProject/Public/Core/FlightHlslReflection.h`
- `Source/FlightProject/Public/Vex/Frontend/VexAst.h`
- `Source/FlightProject/Public/Vex/FlightVexIr.h`
- `Source/FlightProject/Private/Vex/FlightVexLowering.cpp`
- `Source/FlightProject/Private/Mass/UFlightVexBehaviorProcessor.cpp`

## 3. New Files

Recommended first file set:

- `Source/FlightProject/Public/Vex/FlightVexSchemaTypes.h`
- `Source/FlightProject/Public/Vex/FlightVexSchemaOrchestrator.h`
- `Source/FlightProject/Private/Vex/FlightVexSchemaOrchestrator.cpp`
- `Source/FlightProject/Public/Vex/FlightVexSchemaIr.h`
- `Source/FlightProject/Private/Vex/FlightVexSchemaBinder.cpp`
- `Source/FlightProject/Private/Tests/FlightVexSchemaOrchestratorTests.cpp`
- `Source/FlightProject/Private/Tests/FlightVexSchemaIrTests.cpp`

Optional phase-two additions:

- `Source/FlightProject/Private/Vex/FlightVexSchemaIrLowering.cpp`
- `Source/FlightProject/Public/Vex/FlightVexCompileContext.h`

## 4. Phase-One File Changes

### 4.1 `Public/Vex/FlightVexSchemaTypes.h`

Add small, shared schema-contract types:

- `FVexTypeId`
- `EVexStorageKind`
- `FVexStorageBinding`
- `FVexBackendBinding`
- `FVexLogicalSymbolSchema`

Design rule:

- keep this file plain-data oriented
- do not put registry logic here

### 4.2 `Public/Vex/FlightVexSchema.h`

Refactor `FVexTypeSchema` to use the new shared schema types.

Current state to preserve:

- type name
- size
- alignment
- symbol lookup

New fields to add:

- explicit type identity
- logical symbol table
- storage bindings
- backend bindings
- populated layout/version contract

Recommended direction:

- keep accessors available during migration
- stop treating them as the entire schema surface

### 4.3 `Public/Vex/FlightVexSymbolRegistry.h`

Keep the registry, but narrow its role.

Current role:

- owns schema map
- builds schema from reflection

Target role:

- runtime cache and lookup surface
- thin registration helper

Changes:

- add or delegate to `FVexSchemaOrchestrator`
- expose schema lookup by explicit type id/runtime key
- stop leaning on `UScriptStruct*` as a disguised generic key

### 4.4 `Private/Vex/FlightVexSymbolRegistry.cpp`

Keep this file small.

Prefer:

- lookup
- insertion
- thin delegation

Avoid:

- growing more merge and construction policy here

### 4.5 `Public/Vex/FlightVexSchemaOrchestrator.h`

Add the main schema construction API.

Recommended first methods:

```cpp
class FVexSchemaOrchestrator
{
public:
    static FVexTypeId MakeTypeId(const void* RuntimeKey, FName StableName, UScriptStruct* NativeStruct);

    template<typename T>
    static FVexTypeSchema BuildSchemaFromReflection();

    static FVexTypeSchema MergeManifestRequirements(
        FVexTypeSchema BaseSchema,
        const TArray<FFlightVexSymbolRow>& Rows);

    static uint32 ComputeSchemaLayoutHash(const FVexTypeSchema& Schema);
};
```

### 4.6 `Private/Vex/FlightVexSchemaOrchestrator.cpp`

Implement:

- schema construction from reflection descriptors
- layout hash computation
- manifest merge rules
- offset fast-path eligibility rules

This is the right place to centralize the `MemberOffset` vs storage-kind policy.

### 4.7 `Public/Core/FlightReflection.h`

Do not rewrite reflection.
Only make the minimum changes needed to support safer storage binding.

Recommended changes:

- keep member-pointer access as-is
- add an explicit "offset eligible" rule
- avoid making generic member-pointer-derived offset the only path

Recommended design rule:

- access semantics come from member pointer
- offset is optional metadata

### 4.8 `Public/Core/FlightHlslReflection.h`

Retarget reflection-derived symbol generation to align with the new schema types.

Short term:

- keep `GenerateVexSymbolsFromStruct(...)`

Longer term:

- add a path that generates `FVexLogicalSymbolSchema` or feeds `FVexSchemaOrchestrator`

## 5. Phase-Two File Changes

### 5.1 `Public/Verse/UFlightVerseSubsystem.h`

Add a new compile entry surface that accepts a real type/schema key.

Recommended direction:

```cpp
bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, const void* TypeKey);
```

Compatibility options:

- keep the old overload temporarily
- route it internally through schema-orchestrator resolution

### 5.2 `Private/Verse/UFlightVerseSubsystem.cpp`

Update `CompileVex(...)` to:

- resolve schema from explicit type key
- stop using `UScriptStruct*` as a generic schema key
- build compile-time symbol information from schema logical symbols
- resolve the best matching authored behavior compile policy from `UFlightDataSubsystem`
- apply policy inputs where legal:
  - preferred execution domain
  - fallback allowance
  - required symbol/contract expectations
- record selected policy in compile diagnostics or artifact/report output

Keep phase-one behavior:

- current AST parse/validate
- current tiering
- current low-level IR compile
- current execution paths

The change here should be boundary cleanup, not backend replacement.

Current landed state:

- schema-bound `CompileVex(...)` now evaluates explicit backend capability profiles after schema binding
- compile artifact reports now include `selectedBackend` and per-backend report entries
- schema binding now carries backend legality and per-symbol backend diagnostics
- current runtime dispatch still follows the pre-existing executable path logic

Short-term TODO:

- use reported backend selection to start gating actual runtime dispatch
- teach `CompileVex(...)` and `ExecuteBehavior(...)` to honor the chosen backend when that backend is both legal and executable today
- keep this step incremental:
  - do not route into non-executable backends only because they scored well in reporting
  - preserve explicit fallback and report why a selected backend was downgraded at commit time

### 5.3 `Public/Vex/FlightVexSchemaIr.h`

Add the first SchemaIR types.

Recommended initial contents:

- `FVexSchemaIrProgram`
- `FVexSchemaIrStatement`
- `FVexSchemaIrExpression`
- `FVexBoundSymbolRef`
- `FVexBoundLocal`
- `FVexSemanticIssue`

Phase-one rule:

- keep SchemaIR small
- solve symbol binding and type resolution first

### 5.4 `Private/Vex/FlightVexSchemaBinder.cpp`

Add the AST-to-SchemaIR binder.

First scope:

- scalar symbols
- local declarations
- basic arithmetic
- assignments
- conditions

Later scope:

- vector composition
- pipe semantics
- task captures
- backend capability annotation

## 6. Phase-Three File Changes

### 6.1 `Public/Vex/FlightVexIr.h`

Do not replace the low-level IR initially.

Recommended change:

- keep current `FVexIrProgram`
- add a new lowering entry from SchemaIR later if needed

Avoid:

- overloading the current IR with schema-only semantic state

### 6.2 `Private/Vex/FlightVexLowering.cpp`

Retarget top-level lowering flow gradually.

Current path:

- AST -> low-level IR -> HLSL or Verse

Target path:

- AST -> SchemaIR -> low-level IR -> backend

Short-term approach:

- add the SchemaIR binding stage
- keep low-level IR backend lowering intact

### 6.3 `Private/Vex/Frontend/VexParser.cpp`

Keep parser semantic validation focused on source correctness.

Over time, move type/schema-heavy decisions out of the parser and into SchemaIR binding.

Examples that should migrate later:

- backend binding assumptions
- storage-shape assumptions
- schema-specific field legality beyond pure source semantics

## 7. Runtime And Orchestration Follow-On

### 7.1 `Private/Mass/UFlightVexBehaviorProcessor.cpp`

Do not change this in phase one except to avoid further coupling.

Landed change:

- the old `BehaviorID = 1` path has been replaced with orchestration-issued behavior/schema bindings

Remaining follow-on:

- keep runtime consumers moving from fallback/global assumptions toward explicit schema-selected execution domains and reports

### 7.2 `Public/Orchestration/*` and `Private/Orchestration/*`

Phase-two-plus orchestration work:

- world-scoped visibility of active schemas
- cohort-to-behavior-to-schema bindings
- reporting of active execution domains per schema-bound behavior

## 8. Member Pointer And Offset Policy In Implementation

This needs a concrete coding rule.

Recommended implementation rule:

- keep member pointers in reflection descriptors for typed access
- only emit `AosOffset` storage when eligibility is explicit

Recommended eligibility checks:

- owner is standard-layout
- field is a non-static data member
- schema path is contiguous AoS host memory
- field token is available where offset is captured

When those conditions are not satisfied:

- emit `Accessor` or another storage kind
- do not fabricate an offset

Recommended coding implication:

- if the macro layer has the field token, prefer capturing offset there
- do not make generic member-pointer offset derivation the only or required path

## 9. Tests

Add these tests first:

- schema identity and stable-name tests
- schema construction from reflected custom structs
- layout-hash stability tests
- manifest merge precedence tests
- offset-eligible vs accessor-only storage tests
- AST-to-SchemaIR scalar symbol binding tests
- compile path tests for custom reflected structs using explicit type keys

Relevant new test files:

- `Private/Tests/FlightVexSchemaOrchestratorTests.cpp`
- `Private/Tests/FlightVexSchemaIrTests.cpp`

## 10. Suggested Execution Order

1. add shared schema types
2. add schema orchestrator
3. refactor schema registry to use the orchestrator
4. add explicit compile-time type-key path
5. add SchemaIR types
6. add AST-to-SchemaIR binder
7. add backend capability profiles and compile-time backend selection/reporting
8. bridge low-level IR compilation from SchemaIR
9. teach compile/runtime dispatch to honor schema-selected executable backends
10. move runtime and orchestration consumers onto schema-issued bindings

This order establishes the new boundary without requiring a risky compiler rewrite in one pass.
