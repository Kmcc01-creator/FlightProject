# TODO: VexVerse

This file tracks TODOs that sit at the VEX compiler, Verse runtime, backend-selection, and execution-commit boundary.

## Current TODOs

### 1. Compile Policy Context Adoption

Priority: Medium  
Status: Active  
Owner/Surface: startup/orchestration compile call-sites and policy context propagation

Adopt the new compile-policy context at real runtime call-sites so policy matching can use cohort/profile selectors instead of falling back to behavior-id-only/default resolution.

Relevant surfaces: [UFlightVerseSubsystem.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h), [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp), [FlightScriptingLibrary.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightScriptingLibrary.cpp), [CurrentFocus.md](../Workflow/CurrentFocus.md).

### 2. GPU Execution Commitment

Priority: High  
Status: Active  
Owner/Surface: GPU-oriented runtime execution path

Stop treating GPU-oriented submission as equivalent to a committed executable runtime path until the GPU path reports success through the same execution-truth contract used by CPU/VM lanes.

Relevant surfaces: [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp), [CurrentFocus.md](../Workflow/CurrentFocus.md), [GpuResourceSchemaContract.md](../Architecture/GpuResourceSchemaContract.md).

## Exit Condition

- policy context is passed from real compile callers, not only explicit/manual test contexts
- GPU-oriented execution reports success/fallback through the same execution-truth contract as CPU/VM lanes
- reports distinguish selected backend, committed backend, and fallback reason without ambiguity across remaining GPU/reporting-only surfaces

## Completed / Archived

- Completed (2026-03-12): `CompileVex(...)` now resolves authored behavior compile policy, carries the selected policy through behavior/report metadata, and uses it to influence backend preference, fallback allowance, generated-only acceptance, and required-symbol/contract expectations in [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp). Compile artifact and orchestration reports now surface selected policy plus explicit commit detail in [FlightCompileArtifacts.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/FlightCompileArtifacts.cpp) and [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp). Verified with focused Verse/VEX policy coverage plus Phase 3 and Phase 4 reruns.
- Completed (2026-03-12): backend dispatch parity and committed-backend truthfulness now share runtime resolver logic in [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp). Struct, bulk, and direct execution surfaces now honor the recorded selected backend when that lane is executable, committed direct-backend reporting resolves through the same logic, and debug surfaces now expose [DescribeBulkExecutionBackend](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h) and [DescribeDirectExecutionBackend](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Verse/UFlightVerseSubsystem.h). Verified with targeted headless VEX/Verse coverage plus Phase 3 and Phase 4 reruns.
- Completed (2026-03-12): Verse lowering expectation alignment was resolved in source, not by weakening tests. Vector-constructor inference and IR lowering now agree on `vec2`/`vec3`/`vec4` in [VexParser.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/Frontend/VexParser.cpp) and [FlightVexIr.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/FlightVexIr.cpp), and the previously failing [FlightVexControlFlowTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexControlFlowTests.cpp) and [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) now pass under headless validation.
