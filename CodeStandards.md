# FlightProject Code Standards (Initial Baseline)

This document establishes an initial code-quality baseline for FlightProject using current review findings and immediate follow-up actions.

## Scope

- C++ runtime and tooling code in `Source/FlightProject`.
- Schema/codegen + VEX + Verse VM integration surfaces.
- Test architecture for parser, schema, concurrency, and vertical-slice validation.

## Current Findings

Status key:
- `Done`: implemented and validated in current branch.
- `In Progress`: active workstream (not complete yet).
- `Open`: not started.

### High Severity

1. `Done` - Schema-driven affinity checks were effectively disabled due a typo and permissive default return.
- `GetTests()` emits `INVALID_AFFINITY`.
- `RunTest()` checks `INVALID_AFFIDINITY`.
- Unknown test types still return success.
- Fix: command type is now validated and unknown types fail fast.
- References: `Source/FlightProject/Private/Tests/FlightSchemaDrivenTests.cpp:43`, `:61`, `:103`.

2. `Done` - VEX parser implementation and declared/tested capability were reconciled.
- Parser now constructs function-call, dot-access, and pipe nodes with precedence-aware parsing.
- Requirement analysis is aligned with emitted AST nodes.
- Parser suite and mixed validation bucket are green after these changes.
- References: `Source/FlightProject/Private/Vex/FlightVexParser.cpp:618`, `:979`, `:1030`; `Source/FlightProject/Private/Tests/FlightVexParserTests.cpp:203`.

3. `Done` - Required-symbol contract enforcement now exists in both parser API path and compile pipeline.
- `ParseAndValidate(..., bRequireAllRequiredSymbols=true)` now populates `MissingRequiredSymbols` and emits errors.
- `UFlightVerseSubsystem::CompileVex` still enforces required symbols as pipeline contract guard.
- References: `Source/FlightProject/Public/Vex/FlightVexParser.h:235`; `Source/FlightProject/Private/Vex/FlightVexParser.cpp:1030`, `:1134`; `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp:65`.

4. `Done` - Schema type mapping likely misclassified `uint32` VEX fields as `float`.
- `FDroidState::Status` is `uint32`.
- `GetVexValueType` has no `uint32` branch.
- Fix: `uint32` now maps to VEX `Int` until explicit unsigned support is added.
- Regression checks added for `@status`.
- References: `Source/FlightProject/Public/Core/FlightHlslReflection.h:33`; `Source/FlightProject/Public/Swarm/SwarmSimulationTypes.h:184`; `Source/FlightProject/Private/Tests/FlightSchemaTests.cpp:57`.

5. `Done (Phase 2)` - VEX runtime bridge now executes through a real Verse VM procedure path, with native fallback retained.
- `CompileVex` validates generated Verse source, allocates a VM-native entrypoint, and emits a VM `VProcedure` wrapper for runtime invocation.
- `ExecuteBehavior` invokes compiled behavior via `VFunction::Invoke` in VM context, then falls back to native execution on VM failure.
- Remaining gap: source-to-bytecode assembly from generated Verse text requires an engine-provided `IAssemblerPass` implementation in this checkout.
- References: `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`; `Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h:48`.

### Medium Severity

6. `Done` - Concurrency spec world lookup and async coverage were hardened.
- Replaced `GetWorldContexts()[0]` usage in spec setup with safe world-context scanning (`Editor`/`PIE`).
- Replaced async placeholder with compile/execute-state assertions tied to Verse metadata.
- References: `Source/FlightProject/Private/Tests/FlightConcurrencySpec.cpp:13`, `:70`.

7. `Done` - Mega-kernel lowerer now hoists shared symbols and uses local aliases in generated dispatch bodies.
- Parser mega-kernel test now passes against implementation output.
- References: `Source/FlightProject/Private/Vex/FlightVexParser.cpp:1291`; `Source/FlightProject/Private/Tests/FlightVexParserTests.cpp:352`.

### Low Severity

8. `Done` - Unknown-symbol diagnostics now use actual token line/column locations.
- Reference: `Source/FlightProject/Private/Vex/FlightVexParser.cpp:1119`.

9. `Open` - Python symbol lowering uses raw string replacement.
- Prefix collisions and comment mutation are possible.
- Reference: `Content/Python/FlightProject/VexTools.py:72`.

## Assumptions and Gaps

- Findings were produced from source inspection plus targeted automation runs for schema/Verse/concurrency/parser/mixed buckets; a full project-wide automation run was not part of this pass.
- The branch contained active in-flight edits during review.

## Initial Standards Baseline

1. Schema is the source of truth.
- Every generated symbol must carry: `valueType`, `residency`, `affinity`, `hlslIdentifier`, `verseIdentifier`.

2. No silent-pass tests.
- Unknown test command/type must fail explicitly.

3. Feature claims require matching proof.
- Parser features must have one passing AST test, one lowering test, and one negative semantic test.

4. VM bridge must report real state.
- `CompileVex` and `ExecuteBehavior` must expose real compilation/execution outcomes, not previews.

5. Reduce acquisition boilerplate.
- Centralize world/subsystem acquisition patterns for scripting APIs.

## Vertical Slice Complex Test (Planned)

1. Generate dynamic test cases from manifest `vexSymbolRequirements`.
2. For each symbol: parse valid source, lower to HLSL/Verse, assert mapped identifiers.
3. For non-shared symbols: assert invalid residency scripts fail semantically.
4. For game-thread affinity: assert `@job` misuse fails semantically.
5. Compile through `UFlightScriptingLibrary::CompileVex`, then assert behavior metadata (`rate`/`frame`) is registered.

Status:
- `Implemented (Phase 1)`: `FlightProject.Integration.Vex.VerticalSlice` now covers manifest-driven contract checks, lowering checks, invalid residency negatives, and compile metadata assertions.
- `Next`: expand generation to include affinity negatives as schema rows add game-thread-restricted symbols.

## Next Steps (Execution Plan)

1. Parser track integration (owned by parser workstream):
- `Done`: lexeme-type handling for function calls, dot operator, and pipe nodes landed.
- `Done`: requirement analysis reconciled with actual AST node generation.
- `Done`: parser bucket is green; mixed bucket is green.

2. Verse bridge MVP hardening (non-parser):
- `Done`: explicit compile-state enum (`GeneratedOnly`, `VmCompiled`, `VmCompileFailed`) added to behavior metadata.
- `Done`: compile-state query helpers exposed via `UFlightScriptingLibrary`.
- `Done`: first executable native fallback path implemented with execution assertions in `FlightProject.Verse.CompileContract` and vertical-slice coverage.
- `Next`: replace VM wrapper emission with direct source-to-procedure assembly when `IAssemblerPass` is available.

3. Concurrency reliability:
- `Done`: replaced brittle world acquisition in `FlightConcurrencySpec` with robust helper.
- `Done`: async coverage now asserts compile-state and non-executable execution behavior.

4. Vertical-slice complex test:
- `Done (Phase 1)`: implemented complex manifest-driven suite (`FlightProject.Integration.Vex.VerticalSlice`) with parse/lower/compile contract checks.
- Add it to headless focused CI bucket.
- Use parser/mixed bucket green baseline as gate before adding broader permutations.

5. Low-severity cleanup:
- Source-positioned unknown-symbol diagnostics.
- Token-aware Python lowering (avoid raw string replacement collisions).

## Adoption Notes

- This file is an initial baseline, not a frozen standard.
- As gaps close, move fulfilled items from findings into enforced policy and automation gates.
