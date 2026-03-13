# TODO: VexVerse

This file tracks TODOs that sit at the VEX compiler, Verse runtime, backend-selection, and execution-commit boundary.

## Current TODOs

### 1. Promote Fragment Requirement Reports To Compiled MassQueryContract

Priority: Medium  
Status: Active  
Owner/Surface: schema binding, compile artifacts, Mass query validation

The current fragment requirement surface is intentionally report-first: compile artifacts and orchestration now expose fragment families, symbol read/write sets, and whether the current direct processor path can support them. Promote that from a summary/report surface into a compiled `MassQueryContract` so processor-query mismatch can be validated before execution rather than only observed through runtime routing support.

Relevant surfaces: [CompiledFragmentRequirementReporting.md](../Architecture/CompiledFragmentRequirementReporting.md), [VerseSubsystemModularization.md](../Architecture/VerseSubsystemModularization.md), [FlightCompileArtifacts.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Vex/FlightCompileArtifacts.h), [UFlightVexBehaviorProcessor.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Mass/UFlightVexBehaviorProcessor.cpp), [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp).

## Exit Condition

- policy context is passed from real compile callers, not only explicit/manual test contexts
- GPU-oriented execution reports success/fallback through the same execution-truth contract as CPU/VM lanes
- reports distinguish selected backend, committed backend, and fallback reason without ambiguity across remaining GPU/reporting-only surfaces

## Completed / Archived

- Completed (2026-03-12): real scripting compile callers no longer have to fall back to the default empty compile-policy context. [FlightScriptingLibrary.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightScriptingLibrary.cpp) now exposes policy-aware compile entrypoints for cohort/profile selection plus explicit/manual policy injection, including schema-bound C++ helpers for reflected struct and raw type-key targets. Verified through [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) with `FlightProject.Functional.Verse.CompilePolicyIntegration`.
- Completed (2026-03-12): GPU-oriented runtime submission now upgrades `CommittedBackend` only after a terminal bridge result instead of treating submission as equivalent to proof. [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp) now records pending GPU submission state, updates commit/orchestration truth on terminal completion, and allows direct GPU dispatch only when `GpuKernel` is the selected lane. Verified through [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) with `FlightProject.Functional.Verse.GpuTerminalCommit`.
- Completed (2026-03-12): `CompileVex(...)` now resolves authored behavior compile policy, carries the selected policy through behavior/report metadata, and uses it to influence backend preference, fallback allowance, generated-only acceptance, and required-symbol/contract expectations in [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp). Compile artifact and orchestration reports now surface selected policy plus explicit commit detail in [FlightCompileArtifacts.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/FlightCompileArtifacts.cpp) and [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp). Verified with focused Verse/VEX policy coverage plus Phase 3 and Phase 4 reruns.
- Completed (2026-03-12): backend dispatch parity and committed-backend truthfulness now share runtime resolver logic in [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp). Struct, bulk, and direct execution surfaces now honor the recorded selected backend when that lane is executable, committed direct-backend reporting resolves through the same logic, and debug surfaces now expose [DescribeBulkExecutionBackend](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h) and [DescribeDirectExecutionBackend](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h). Verified with targeted headless VEX/Verse coverage plus Phase 3 and Phase 4 reruns.
- Completed (2026-03-12): Verse lowering expectation alignment was resolved in source, not by weakening tests. Vector-constructor inference and IR lowering now agree on `vec2`/`vec3`/`vec4` in [VexParser.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/Frontend/VexParser.cpp) and [FlightVexIr.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/FlightVexIr.cpp), and the previously failing [FlightVexControlFlowTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexControlFlowTests.cpp) and [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) now pass under headless validation.
