# VEX Lexing & Parsing Refactor Specification

Date: March 8, 2026  
Status: Proposed (implementation contract)  
Owners: VEX Frontend, Simulation Runtime, Shader/Backend Team

## 1) Purpose

Define a modular, maintainable frontend architecture for VEX compilation that:

1. Decouples lexing, parsing, semantic analysis, typing, and backend lowering.
2. Supports the typed IR (`FVexIrProgram`) as the shared backend input.
3. Preserves robust diagnostics and source mapping across SIMD/HLSL/Verse backends.
4. Prevents parser churn from breaking schema-driven capability and tier logic.

## 2) Current Pain Points

1. Parser-facing API currently owns too many concerns (tokenization, AST shaping, schema concerns, validation, lowering contracts).
2. Syntax/grammar changes frequently leak into SIMD and backend eligibility behavior.
3. Symbol/schema logic is not isolated enough for independent test coverage.
4. Error handling is brittle; one malformed construct can suppress subsequent diagnostics.
5. Backend-specific requirements risk leaking into core frontend code.

## 3) Design Principles

1. Single Responsibility per stage.
2. Immutable handoff data between stages where feasible.
3. Backends consume IR, not parser internals.
4. Every diagnostic must carry source span identity.
5. Capability/tier policy is schema-driven and testable in isolation.

## 4) Compilation Pipeline

```
Source Text
  -> Lexer
  -> Parser (AST only)
  -> Symbol Resolver (schema binding)
  -> Type + Capability Sema
  -> IR Builder (typed IR + source map)
  -> IR Pass Manager
  -> Backend Lowerers (SIMD / HLSL / Verse / MegaKernel)
```

### Stage Contracts

1. **Lexer**: input `FString`, output token stream with stable token IDs and source spans.
2. **Parser**: input token stream, output AST + parse diagnostics (no schema lookup).
3. **Symbol Resolver**: input AST + schema catalog, output bound symbol/function references.
4. **Sema**: input bound AST, output type facts, capability facts, and tier eligibility facts.
5. **IR Builder**: input typed/bound AST, output `FVexIrProgram` with `SourceTokenIndex` mapping.
6. **IR Passes**: backend-agnostic canonicalization and simplification only.
7. **Lowerers**: backend plugins that transform IR into target code/executors.

## 5) Module Responsibilities

## 5.1 Frontend

1. `FVexLexer`
   - Tokenization only.
   - Owns keyword/operator lexeme normalization.
2. `FVexParser`
   - Grammar and AST construction.
   - No manifest/schema references.
3. `IVexAstVisitor`
   - Read-only traversal interface for analysis/transforms.
   - Keeps AST nodes lightweight.

## 5.2 Semantic Layer

1. `IVexSchemaCatalog`
   - Abstract symbol/function capability access (`bSimdReadAllowed`, `bGpuTier1Allowed`, etc.).
   - Allows unit testing without manifest builders.
2. `FVexSymbolResolver`
   - Resolves AST symbol/function uses to schema entities.
3. `FVexTypeChecker`
   - Computes expression/result types, assignment legality.
4. `FVexCapabilityAnalyzer`
   - Computes backend capability and emits explicit ineligibility reasons.
5. `FVexTierClassifier`
   - Uses sema facts to classify `Literal`, `DFA`, `Full`.

## 5.3 IR Layer

1. `FVexIrBuilder`
   - Emits typed `FVexIrInstruction` with source mapping.
2. `FVexIrPassManager`
   - Runs ordered backend-agnostic passes.
3. `IVexIrPass`
   - Uniform pass interface with diagnostics sink.

## 5.4 Backend Layer

1. `IVexBackendLowerer`
   - Consumes `FVexIrProgram`.
   - Emits either source (HLSL/Verse) or executable plan (SIMD).
2. Backend implementations:
   - `FVexSimdLowerer`
   - `FVexHlslLowerer`
   - `FVexVerseLowerer`
   - `FVexMegaKernelLowerer`

## 6) Parser Strategy

### 6.1 Recommended Pattern

Adopt a Pratt/Parselet operator parser for expression precedence while retaining explicit statement parsing.

Rationale:

1. Less fragile than ad-hoc `Peek/Match` chains.
2. Easier to add new operators/functions without invalidating unrelated grammar.
3. Keeps parser imperative and UE-friendly without introducing heavy parser-combinator runtime overhead.

### 6.2 Visitor vs Combinator

1. **Visitor**: adopt for AST and IR analyses (validation, typing, lowering prep).
2. **Combinator**: do not make it the primary parser model in this phase; use parselets/tables for declarative precedence and associativity.

## 7) Error Recovery and Diagnostics Contract

## 7.1 Recovery Rules

Parser must recover at synchronization tokens:

1. `;` end of statement
2. `}` block end
3. start of directive (`@cpu`, `@gpu`, `@job`, `@thread`, `@async`, `@rate`)

On parse failure:

1. emit diagnostic,
2. create an error placeholder node for local continuity,
3. synchronize and continue.

## 7.2 Diagnostic Envelope

All stages emit to a shared diagnostic object:

1. `Stage` (`Lexer`, `Parser`, `Resolver`, `Sema`, `IR`, `Backend`)
2. `Severity`
3. `Message`
4. `SourceSpanId` and line/column projection
5. optional `BackendTag` (`SIMD`, `HLSL`, `Verse`, `MegaKernel`)

## 7.3 Source Mapping

1. Lexer assigns stable token IDs and span offsets.
2. AST nodes carry primary token ID.
3. IR instructions preserve originating token ID (already represented by `SourceTokenIndex`).
4. Backend errors map instruction -> token -> source range.

## 8) Backend Specialization Boundary

Core frontend may not contain:

1. Wave intrinsic selection logic,
2. SIMD lane packing policies,
3. backend register-pressure tuning details.

Those belong in backend lowerers and backend-specific IR passes:

1. `HLSL`: wave intrinsics, occupancy tuning, subgroup constraints.
2. `SIMD`: vector width, load/store alignment strategy, direct-fragment execution strategy.
3. `Verse`: VM compatibility shaping and runtime fallback metadata.

## 9) Unreal Engine Integration & Ownership

## 9.1 Ownership Model

`UFlightVerseSubsystem` owns one pipeline object:

1. `TUniquePtr<FVexCompilerPipeline> CompilerPipeline;`

Shared immutable dependencies:

1. `TSharedPtr<const IVexSchemaCatalog, ESPMode::ThreadSafe> SchemaCatalog;`
2. `TSharedPtr<const FVexBackendRegistry, ESPMode::ThreadSafe> BackendRegistry;`

Per-compile transient artifacts:

1. `FVexCompileRequest` and `FVexCompileResult` allocated on stack or request-local arena.

## 9.2 Thread Safety

1. Pipeline stages are re-entrant and stateless where practical.
2. Any caches are read-mostly and protected for concurrent compile calls.
3. UObject access stays at subsystem boundary; frontend stages remain plain C++ objects.

## 10) Public API Shape (Transition-Friendly)

Maintain compatibility while introducing modular internals:

1. Keep `ParseAndValidate(...)` as façade API during migration.
2. Internally route to `FVexCompilerPipeline::CompileFrontend(...)`.
3. Keep `LowerToHLSL/LowerToVerse/LowerMegaKernel` façade endpoints but implement via backend registry on IR.

## 11) Proposed File Structure

```
Source/FlightProject/Public/Vex/Frontend/VexSource.h
Source/FlightProject/Public/Vex/Frontend/VexToken.h
Source/FlightProject/Public/Vex/Frontend/VexLexer.h
Source/FlightProject/Public/Vex/Frontend/VexParser.h
Source/FlightProject/Public/Vex/Frontend/VexAst.h
Source/FlightProject/Public/Vex/Frontend/VexAstVisitor.h

Source/FlightProject/Public/Vex/Sema/VexDiagnostics.h
Source/FlightProject/Public/Vex/Sema/VexSourceMap.h
Source/FlightProject/Public/Vex/Sema/VexSchemaCatalog.h
Source/FlightProject/Public/Vex/Sema/VexSymbolResolver.h
Source/FlightProject/Public/Vex/Sema/VexTypeChecker.h
Source/FlightProject/Public/Vex/Sema/VexCapabilityAnalyzer.h
Source/FlightProject/Public/Vex/Sema/VexTierClassifier.h

Source/FlightProject/Public/Vex/IR/VexIr.h
Source/FlightProject/Public/Vex/IR/VexIrBuilder.h
Source/FlightProject/Public/Vex/IR/VexIrPass.h
Source/FlightProject/Public/Vex/IR/VexIrPassManager.h

Source/FlightProject/Public/Vex/Backends/VexBackendLowerer.h
Source/FlightProject/Public/Vex/Backends/VexBackendRegistry.h
Source/FlightProject/Public/Vex/Backends/VexSimdLowerer.h
Source/FlightProject/Public/Vex/Backends/VexHlslLowerer.h
Source/FlightProject/Public/Vex/Backends/VexVerseLowerer.h
Source/FlightProject/Public/Vex/Backends/VexMegaKernelLowerer.h

Source/FlightProject/Public/Vex/Pipeline/VexCompileRequest.h
Source/FlightProject/Public/Vex/Pipeline/VexCompileResult.h
Source/FlightProject/Public/Vex/Pipeline/VexCompilerPipeline.h

Source/FlightProject/Private/Vex/Frontend/*.cpp
Source/FlightProject/Private/Vex/Sema/*.cpp
Source/FlightProject/Private/Vex/IR/*.cpp
Source/FlightProject/Private/Vex/Backends/*.cpp
Source/FlightProject/Private/Vex/Pipeline/*.cpp
```

## 12) Migration Plan

## Phase A: Frontend Split Without Behavior Change

1. Move token/AST types into dedicated frontend headers.
2. Implement `FVexLexer` and `FVexParser` classes behind current façade.
3. Keep current test suite green.

## Phase B: Sema Extraction

1. Introduce `IVexSchemaCatalog` and resolver/type/capability passes.
2. Remove schema/capability checks from parser.
3. Move tier classification to sema-backed facts.

## Phase C: IR-Centric Lowering

1. Route backend lowering via `FVexIrProgram`.
2. Preserve source mapping and diagnostics.
3. Keep fallback behavior unchanged.

## Phase D: Cleanup

1. Deprecate parser monolith interfaces once all call sites use pipeline.
2. Remove duplicate validation logic from executors where sema guarantees exist.

## 13) Validation & Test Plan

1. Lexer tests: token classes, numeric literals, directives, malformed lexemes.
2. Parser tests: precedence, associativity, attributes, directives, recovery behavior.
3. Resolver tests: manifest/schema symbol binding and unknown symbol diagnostics.
4. Type tests: scalar/vector legality and function signatures.
5. Capability tests: SIMD/GPU eligibility reasons from schema flags.
6. Source map tests: backend diagnostic points to correct original span.
7. Regression suite: existing `FlightVexParserTests`, `FlightVexSimdTests`, and vertical slice tests.

## 14) Non-Goals (This Refactor)

1. Redesigning VEX language syntax.
2. Introducing Tier 2/3 control-flow lowering beyond existing behavior.
3. Replacing existing runtime execution paths in one step.

## 15) Acceptance Criteria

1. Parser module no longer depends on schema/manifest logic.
2. Capability checks run in dedicated sema stage and are unit-tested.
3. All backend diagnostics include source mapping to original VEX code.
4. Existing compile entrypoints remain API-compatible during transition.
5. Refactor lands without regressions in current parser/IR/SIMD tests.

## 16) m2 Paradigm Alignment Contract

This section binds the frontend refactor to FlightProject's m2-inspired paradigms (self-similar trees, optics, rewrite algebra, and functional composition).

## 16.1 Self-Similar Tree Representation

Frontend and IR nodes must expose a uniform tree interface:

1. stable node identity (`NodeId` / token anchor),
2. child enumeration (`GetChildren` equivalent),
3. immutable-friendly update path (copy-on-write or replace-by-id),
4. non-recursive traversal support for deep scripts.

Required outcome:

1. AST and IR can be traversed by generic tree walkers without backend-specific code.
2. Existing iterative post-order optimization style remains available as a first-class utility.

## 16.2 Pattern Rewrites As First-Class Rules

Rewrite logic must live in explicit rule sets, not ad-hoc conditionals:

1. rule shape: `Pattern + Guard -> Replacement`,
2. deterministic pass ordering,
3. bounded fixpoint execution with convergence diagnostics,
4. optional rule tags for backend applicability (`Core`, `SIMD`, `GPU`, `Verse`).

Required outcome:

1. Constant folding and identity simplification are expressed through reusable rewrite rule registries.
2. Tier-specific rewrites can be toggled without touching parser code.

## 16.3 Optics-Oriented Access

Analysis and transform code should use optic-like traversal surfaces:

1. prism-style matching for variant node kinds,
2. traversal-style iteration over all expression children,
3. lens-style focused update helpers for node fields.

Required outcome:

1. validation/type/capability passes avoid brittle manual switch ladders,
2. AST/IR transforms compose via small reusable optics primitives.

## 16.4 Functional Compilation Flow

Pipeline stage composition should use explicit functional result semantics:

1. stage output encoded as `TResult<StageOutput, FVexDiagnosticSet>` (or equivalent),
2. `AndThen`-style chaining for short-circuiting hard failures,
3. `Map`/`MapErr` for pure transformations and diagnostics enrichment.

Required outcome:

1. no hidden control flow between stages,
2. consistent error propagation from lexer to backend lowering.

## 16.5 Algebraic Laws and Validation

Core tree/optic/rewrite operations should be validated with law-driven tests:

1. traversal order stability (same input graph => same visit sequence),
2. rewrite idempotence for canonical forms,
3. optic round-trip behavior on focused updates,
4. deterministic fixpoint termination within configured pass budget.

Required outcome:

1. optimizer behavior remains deterministic across platforms/builds,
2. regressions in rewrite semantics are caught by dedicated law tests.

## 16.6 Mapping To Existing Flight Systems

The refactor should align with existing project patterns:

1. `FlightVexOptics` for VEX pattern/rewrite style.
2. `FlightFunctional` for `TResult`-driven stage composition.
3. `FlightPropertyOptics` / `FlightMassOptics` as precedent for tree/pattern APIs.

This alignment is mandatory for new frontend modules unless a documented exception is approved.
