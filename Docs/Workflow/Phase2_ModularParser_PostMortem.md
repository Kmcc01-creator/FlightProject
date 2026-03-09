# Phase 2 & 3 Post-Mortem: Modular VEX & IR Unification

**Date**: March 9, 2026  
**Status**: Completed  
**Owner**: VEX Infrastructure Team

## 1. Executive Summary
Successfully modularized the VEX frontend and introduced a unified Typed IR layer. This refactor successfully decoupled AST concerns from backend lowering, enabling a "Direct Fragment SIMD" pilot that operates on raw Mass SoA memory. However, headless testing revealed critical engine-level shader assertions that required significant guarding.

## 2. Key Accomplishments
- **Modular Frontend**: Split `FlightVexParser.cpp` into `VexLexer`, `VexParser`, `VexToken`, and `VexAst`.
- **Unified IR**: HLSL and Verse lowering now share the `FVexIrProgram` contract, ensuring mathematical parity.
- **SIMD SoA Optimization**: `FVexSimdExecutor::ExecuteDirect` eliminates POD gather/scatter overhead for Tier 1 behaviors.
- **Schema-Driven Rules**: Hardcoded capability allowlists moved to the `FVexSymbolDefinition` manifest.

## 3. Headless Testing Challenges
During verification via `run_tests_headless.sh`, we encountered a persistent `SIGSEGV` in `FShaderCompilerStats::GetTotalShadersCompiled()`.

### Findings:
- **NullRHI Assertion**: In `-NullRHI` mode, the `GGlobalShaderMap` is not initialized.
- **Engine Analytics Trigger**: Even with custom project guards (`!IsRunningCommandlet()`), engine-level analytics and user-activity tracking subsystems still attempt to query shader stats during editor-context initialization.
- **Fatal Error**: The use of `TNotNull<FGlobalShaderMap>` in `FShaderCompilerStats` results in a fatal error when the map is null, bypassing standard commandlet safety.

### Mitigations Applied:
- Wrapped all project-level `GetGlobalShaderMap` calls in `if (!IsRunningCommandlet())`.
- Guarded `FSceneViewExtensions::NewExtension` to prevent RDG registration in headless mode.
- Added `-NoShaderCompile` to headless runner to further reduce engine shader overhead.

## 4. Lessons Learned
1. **Vertical Consistency is Hard**: Mathematical parity between CPU (SIMD) and GPU (HLSL) must be enforced at the IR level, as AST lowering is too high-level to catch subtle divergence.
2. **Headless Limitations**: Commandlets in UE 5.x are increasingly integrated with systems that assume a valid shader environment. Complex shader-dependent integration tests may require a "Mesa/Software-GL" or GPU-enabled CI environment rather than `-NullRHI`.
3. **SoA is the Future**: The `ExecuteDirect` pilot showed that operating on raw fragment views is significantly cleaner and more efficient than translating to `FDroidState` structs.

## 5. Residual Risks
- **Mega-Kernel Outlier**: `LowerMegaKernel` remains on the legacy AST path and is a potential source of regression.
- **Alignment Sensitivity**: `ExecuteDirect` is highly sensitive to the memory alignment of Mass fragments. Production-ready runtime validation is still required.
