# VEX Schema Validation

## Purpose
FlightProject now treats VEX symbol bindings as part of the schema manifest contract (`vexSymbolRequirements`), not as hardcoded editor-only replacements.

This aligns with Phase 1 roadmap goals in `AGENTS.md` for expanding contract coverage to scripting boundaries (`FVexSymbolRow`).

## Contract Source

Canonical symbol contracts are declared in C++:

- `FFlightVexSymbolRow` in `Source/FlightProject/Public/Schema/FlightRequirementSchema.h`
- Manifest generation in `Source/FlightProject/Private/Schema/FlightRequirementRegistry.cpp`

Current default contract entries include:

- `@position` -> `PosI` / `Droid.Position`
- `@velocity` -> `VelI` / `Droid.Velocity`
- `@shield` -> `ShieldI` / `Droid.Shield`
- `@status` -> `StatusI` / `Droid.Status`

## Runtime Tooling Paths

### C++ editor tool path
- `SSwarmOrchestrator` now reads symbol mappings from schema manifest data.
- `Flight::Vex` parser front-end now tokenizes source and builds a lightweight statement AST before shader injection.
- Expression parsing uses precedence-aware AST construction and supports function-call, dot-member, pipe, and vector-literal forms.
- Diagnostics are line/column aware (unknown symbols, malformed `if (...)`, unmatched braces/parentheses, unsupported tokens).
- Semantic validation now enforces:
  - read-only symbol assignment errors
  - assignment type mismatch errors
  - `if` condition bool-like checks
- Parser API contract enforcement now supports required-symbol checks through `ParseAndValidate(..., bRequireAllRequiredSymbols=true)`.
- AST nodes carry inferred type annotations used during semantic validation.
- Unknown symbols are detected and surfaced as warnings with real token source positions.

### Python scripting path
- `Content/Python/FlightProject/SchemaTools.py` validates VEX symbol contract consistency as part of `ensure_manifest_requirements`.
- `Content/Python/FlightProject/VexTools.py` provides source-level helpers for authoring and diagnostics.

## Python Usage

```python
from FlightProject import VexTools

# List schema-declared symbols
VexTools.get_contract_symbols()

# Validate ad-hoc source against schema
VexTools.validate_source("@velocity += normalize(@position) * 20.0;")

# Emit json report with unknown symbols and lowered targets
VexTools.export_validation_report("@velocity += @foo;")
```

Report output path:

- `Saved/Flight/Schema/vex_validation_report.json`

## Suggested Next Capability (Roadmap-Consistent)

Move from "shape validation" to "compliance validation" by adding automation that asserts:

1. Unknown-symbol warnings are emitted for invalid tokens.
2. Required symbol mappings are non-empty for both HLSL and Verse targets.
3. Generated preview output reflects contract updates without code edits.

Also track compile-contract behavior in Verse bridge:
4. `CompileVex` fails when required schema symbols are missing.
5. `CompileVex` reports executable state truthfully: supported scripts compile to VM-procedure execution (with native fallback safety path), unsupported paths report non-executable diagnostics.

Related draft for SCSL field/page schema:

- `Docs/Architecture/SCSL_FieldResidencySchemaContract.md`

## Current Parser Tests

- `FlightProject.Schema.Vex.Parser.ValidSource`
- `FlightProject.Schema.Vex.Parser.Diagnostics`
- `FlightProject.Schema.Vex.Parser.Semantics.ReadOnlySymbol`
- `FlightProject.Schema.Vex.Parser.Semantics.TypeMismatch`
- `FlightProject.Schema.Vex.Parser.Semantics.IfConditionBoolLike`
- `FlightProject.Schema.Vex.Parser.Ast.Precedence`
- `FlightProject.Schema.Vex.Parser.Ast.FunctionCall`
- `FlightProject.Schema.Vex.Parser.Ast.DirectionalPipes`
- `FlightProject.Schema.Vex.Parser.Ast.PipeDisambiguation`
- `FlightProject.Schema.Vex.Parser.Ast.LogicalAndOperator`
- `FlightProject.Schema.Vex.Parser.Ast.MaximalMunchPipeTokens`
- `FlightProject.Schema.Vex.Parser.DependencyDetection`
- `FlightProject.Schema.Vex.Parser.NPRBuiltins`
- `FlightProject.Schema.Vex.Parser.Optimization`
- `FlightProject.Schema.Vex.Parser.MegaKernel`
- `FlightProject.Schema.Vex.Parser.Semantics.PipeResidency`
- `FlightProject.Schema.Vex.Parser.Diagnostics.InvalidExtractOperator`
- `FlightProject.Schema.Vex.Parser.Diagnostics.InvalidSingleAmpersand`

As of 2026-03-09, the parser bucket (`FlightProject.Schema.Vex.Parser`) is passing in headless focused runs (`17/17`), and the extended mixed schema/Verse/parser/vertical-slice bucket is passing (`32/32`).

## Vertical Slice Integration Coverage

- `FlightProject.Integration.Vex.VerticalSlice` now executes a manifest-driven complex test pass that:
  - validates parse + lowering symbol mapping for each declared VEX symbol
  - asserts invalid residency failures for non-shared symbols
  - verifies compile metadata registration (`@rate`) through `UFlightScriptingLibrary::CompileVex`
  - verifies executable compile state/diagnostics for runtime-supported scripts
