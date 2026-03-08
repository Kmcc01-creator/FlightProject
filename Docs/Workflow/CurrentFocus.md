# Current Focus: Parser Track + Non-Parser Hardening

## Active Goals
1. Complete VEX parser/compiler lexeme handling (function calls, dot operator, pipe nodes) in the parser workstream.
2. Keep non-parser code standards hardening moving in parallel (schema contracts, Verse compile reporting, test integrity).
3. Preserve green focused automation for non-parser buckets while parser work is in flight.
4. Keep triage output high-signal (`focused` profile + error filtering + ANSI coloring).

## Current Status (2026-03-08)
- Development build is green (`./Scripts/build_targets.sh Development` succeeded).
- Non-parser hardening landed:
  - schema-driven command typo and silent-pass behavior fixed.
  - `uint32` schema mapping corrected (`@status` now validates as `int` in VEX symbol contract).
  - `CompileVex` now enforces required symbols and reports non-executable VM state truthfully.
  - focused hardening bucket passes.
- Parser workstream remains active; broader parser suite still has known failures:
  - `FlightProject.Schema.Vex.Parser.Semantics.IfConditionBoolLike`
  - `FlightProject.Schema.Vex.Parser.Semantics.TypeMismatch`
  - `FlightProject.Schema.Vex.Parser.Diagnostics`

## Immediate Next Steps
1. Land parser lexeme-node changes and re-baseline `FlightProject.Schema.Vex.Parser.*`.
2. Begin vertical-slice complex test implementation from manifest-generated scenarios.
3. Add first executable Verse-native registration path and one execution assertion (outside parser internals).
4. Strengthen remaining world-context acquisition sites in tests (`FlightVexVerseTests` still uses index-based lookup).

## Risks / Watch Items
- Parser merges can invalidate assumptions in non-parser contract tests; keep parser and non-parser buckets separate during integration.
- False confidence from `NullRHI` contexts for GPU behavior; keep expected-skip semantics explicit.
- Shared automation output can obscure true failures; continue focused/error-filtered runs for triage.
