# Current Focus: Vertical Slice + Verse Bridge

## Active Goals
1. Implement the vertical-slice complex test from schema manifest rows (`vexSymbolRequirements`).
2. Advance Verse bridge from native fallback execution to real Verse VM procedure/native registration.
3. Keep parser + mixed validation buckets green as the baseline safety gate.
4. Keep triage output high-signal (`focused` profile + error filtering + ANSI coloring).

## Current Status (2026-03-09)
- Development build is green (`./Scripts/build_targets.sh Development` succeeded).
- Non-parser hardening landed:
  - schema-driven command typo and silent-pass behavior fixed.
  - `uint32` schema mapping corrected (`@status` now validates as `int` in VEX symbol contract).
  - `CompileVex` now enforces required symbols and produces executable native fallback behavior for supported scripts.
  - focused hardening bucket passes.
- Parser stabilization landed:
  - function-call, dot operator, and pipe-node AST support implemented.
  - parser semantic/type inference coverage expanded and aligned with tests.
  - parser bucket is green (`FlightProject.Schema.Vex.Parser`, `17/17`).
  - mixed bucket is green (schema-driven + manifest + Verse + parser).
  - extended mixed + vertical-slice bucket is green (`32/32`).
  - operator diagnostics added for invalid extract operator and invalid single ampersand tokenization.
- Vertical-slice integration test landed:
  - `FlightProject.Integration.Vex.VerticalSlice` now generates manifest-driven contract and negative residency checks.
  - compile contract assertions are validated per symbol via scripting APIs, including executable fallback state.
- Verse online validation bucket is green (`10/10`):
  - `FlightProject.Verse.CompileContract`
  - `FlightProject.Verse.Subsystem`
  - `FlightProject.Integration.Vex.VerticalSlice`
  - `FlightProject.Integration.Concurrency`
- Verse bridge phase 2 landed:
  - executable VM procedure wrapper path is active (`VProcedure` + `VFunction::Invoke`).
  - generated Verse source is compiler-validated before behavior activation.
  - native fallback execution remains as a safety path.
- Verse assembler scaffold phase started:
  - experimental `IAssemblerPass` is now registered from `FlightProject` module startup.
  - codegen/link hooks are active for future bytecode emission work.

## Immediate Next Steps
1. Expand vertical-slice generation to include explicit affinity-negative rows as schemas add GameThread symbols.
2. Implement minimal bytecode emission in the project-owned `IAssemblerPass` scaffold (literal/move/return subset).
3. Add parser + mixed-bucket test commands as required pre-merge gates in CI notes.
4. Add a dedicated headless bucket including `FlightProject.Integration.Vex.VerticalSlice`.
5. Add schema snapshot replay + retention tooling (`schema_edits.jl` rebuild, history prune policy) and wire it into UI workflow docs/tests.

## Risks / Watch Items
- Parser changes can still perturb lowering semantics; keep parser and mixed buckets mandatory after parser edits.
- False confidence from `NullRHI` contexts for GPU behavior; keep expected-skip semantics explicit.
- Shared automation output can obscure true failures; continue focused/error-filtered runs for triage.
