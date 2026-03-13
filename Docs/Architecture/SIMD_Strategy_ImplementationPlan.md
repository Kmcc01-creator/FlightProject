# SIMD/HLSL Strategy Implementation Plan

For the current contract boundary between vector-shaped execution and provable hardware SIMD backends, start with [SimdBackendContract.md](SimdBackendContract.md).
For schema-bound vector packing on arbitrary symbols, see [VexArbitrarySymbolVectorContract.md](VexArbitrarySymbolVectorContract.md).
For the Unreal/clang implementation structure behind explicit ISA kernels, see [ClangSimdBackendImplementation.md](ClangSimdBackendImplementation.md).

This plan turns the architecture in `SIMD_HLSL_INTRINSICS.md` into concrete implementation work for FlightProject.

## Objectives

1. Unify Tier-1 lowering across CPU SIMD and GPU HLSL from a shared capability schema.
2. Remove silent fallback behavior by making SIMD eligibility explicit at compile time.
3. Eliminate avoidable gather/scatter overhead in bulk execution.
4. Add CPU/GPU numeric parity checks to prevent simulation desync.

## Current Weak Links

1. Tier-1 classification is broader than current SIMD backend capability.
2. SIMD compile failures are under-specified unless manually inspected.
3. CPU bulk path still relies on AoS `FDroidState` gather/scatter before Mass fragment writeback.
4. GPU path does not yet leverage wave intrinsics or per-tier register pressure policy.
5. No systematic CPU-vs-GPU precision validation for shared math ops.

## Phase 1: Backend Hardening (Now)

### Scope

1. Add strict `FVexSimdExecutor::CanCompileProgram(...)` gating.
2. Restrict SIMD lowering to known-safe branchless intrinsics and symbols.
3. Surface SIMD eligibility diagnostics in `CompileVex`.

### Touchpoints

1. `Source/FlightProject/Public/Vex/FlightVexSimdExecutor.h`
2. `Source/FlightProject/Private/Vex/FlightVexSimdExecutor.cpp`
3. `Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h`
4. `Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp`

### Exit Criteria

1. Tier-1 scripts either compile SIMD plan or emit explicit reason.
2. Unsupported symbols/functions fail fast for SIMD path and use native fallback safely.

## Phase 2: Shared Capability Schema

### Scope

1. Extend symbol/function schema with:
   - `bSimdReadAllowed`
   - `bSimdWriteAllowed`
   - `bGpuTier1Allowed`
   - alignment requirement (`Align16`, `Align32`, `Any`)
   - deterministic math profile (`Fast`, `Precise`, `StrictParity`)
2. Use schema as single source of truth for both CPU and HLSL lowerers.

### Touchpoints

1. `Source/FlightProject/Public/Vex/FlightVexParser.h`
2. `Source/FlightProject/Private/Vex/FlightVexParser.cpp`
3. `Source/FlightProject/Public/Vex/FlightVexLowering.h/.cpp`
4. `Docs/Architecture/SCSL_FieldResidencySchemaContract.md`

### Exit Criteria

1. No hardcoded SIMD/HLSL capability tables in executors.
2. Tier selection includes schema-backed backend compatibility checks.

## Phase 3: Unified Lowering IR

### Scope

1. Introduce a typed mid-level VEX IR for arithmetic, symbol IO, and intrinsic calls.
2. Implement two backend passes:
   - CPU pass: SIMD pack schedule + register allocation hints
   - GPU pass: HLSL intrinsic mapping + wave-op eligibility tags
3. Preserve source span mapping for diagnostics.

### Touchpoints

1. `Source/FlightProject/Private/Vex/` (new IR module files)
2. `Source/FlightProject/Private/Vex/FlightVexSimdExecutor.cpp`
3. HLSL generation files in `Shaders/Private` and any codegen drivers

### Exit Criteria

1. CPU/GPU backends consume the same IR contract.
2. Tier-1 optimization logic no longer duplicated in two unrelated codepaths.

## Phase 4: Direct Fragment SIMD

### Scope

1. Add SoA chunk views over Mass fragments for Tier-1 symbol sets.
2. Execute SIMD directly on fragment memory (no `ModifiedDroids` copy loop).
3. Maintain fallback path for unsupported chunk layouts.

### Touchpoints

1. `Source/FlightProject/Private/Mass/UFlightVexBehaviorProcessor.cpp`
2. `Source/FlightProject/Public/Swarm/SwarmSimulationTypes.h`
3. Mass fragment declarations for `@position`, `@shield`, `@velocity`

### Exit Criteria

1. Bulk Tier-1 path performs direct fragment loads/stores for supported symbols.
2. Measured reduction in per-frame copy overhead at representative entity counts.

## Phase 5: Validation and Performance Governance

### Scope

1. Add vertical-slice parity tests for key ops: `+ - * / sin cos exp pow normalize`.
2. Add tolerance profiles by op/category (`Strict`, `Relaxed`) with deterministic seeds.
3. Emit per-script compile telemetry:
   - SIMD eligible/ineligible reason
   - estimated register pressure
   - expected backend path (`SIMD`, `Native`, `VM`, `GPU`)

### Touchpoints

1. `Source/FlightProject/Private/Tests/FlightVexVerticalSliceTests.cpp`
2. `Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp`
3. compile diagnostic/reporting path in `UFlightVerseSubsystem`

### Exit Criteria

1. CPU/GPU parity tests run in CI for Tier-1 function set.
2. Regressions in backend routing or numeric drift are caught automatically.

## Proposed Rollout Sequence

1. Land Phase 1 immediately (safe hardening and diagnostics).
2. Build Phase 2 schema extension behind flags, then migrate Tier logic.
3. Introduce IR in parallel with existing lowerers, then switch backends over.
4. Land direct fragment SIMD for the first symbol subset (`@shield`, `@position.x`).
5. Enforce parity/perf gates before enabling advanced Tier-1 GPU intrinsics by default.

## Risks and Mitigations

1. Risk: Overly strict SIMD gating reduces acceleration coverage.
   Mitigation: track ineligibility reasons and iterate coverage based on real script corpus.
2. Risk: CPU/GPU parity failures due to math library differences.
   Mitigation: define per-op tolerance and strict-mode math profile in schema.
3. Risk: Mass fragment alignment assumptions break on some archetypes.
   Mitigation: runtime alignment checks with automatic fallback to native scalar path.
