# Current Focus: Generalizing the VEX Compiler

## Active Goals
1. **General System Authoring**: Decouple the VEX compiler and `FVexSymbolRegistry` from `FDroidState`. VEX should be able to compile against arbitrary C++ types that use the `FLIGHT_REFLECT_BODY` macro, enabling general system authoring.
2. **Module Decoupling**: Complete the split of the monolithic `FlightProject` bridge into `FlightVerseRuntime` (runtime thunks/registry) and `FlightVerseDev` (assembler/compiler) for better architectural isolation.
3. **VEX Feature Expansion**: Implement missing intrinsic maths (`Sin`, `Cos`, `Normalize`, `VectorCompose`) in the generalized schema-driven native registry.
4. **CI/RENDER Stability**: Proceed with software-Vulkan (lavapipe) automation tests to bypass engine-level analytics shader assertions.

## Current Status (2026-03-10)
- **Editor & GC Stability Landed**:
  - Silenced fatal `LogGarbage` warnings by properly implementing `Super::AddReferencedObjects` in `UFlightVerseSubsystem`.
  - Resolved `NumEvents` bound parameter ensures on startup for `FFlightSwarmForceCS` and `FFlightSwarmPredictiveCS`.
- **Automated Schema Asset Authoring Landed**:
  - Implemented `EnsureNiagaraSystemContract` via `UFlightScriptingLibrary` to programmatically author and save missing User Parameters and Data Interfaces to target `uassets` during editor startup.
- **VVM Assembler & Complex IR Landed**:
  - `FVexVvmAssembler` now supports multi-block IR (`if-else`, `while`).
  - Verified with `ComplexControlFlow` test (5-iteration loop incrementing state).
- **Schema-Driven Native Registry Landed**:
  - Dynamic thunks for `@symbols` and custom intrinsics (e.g. `square`).
  - Decoupled `UFlightVerseSubsystem` from hardcoded symbol names via `FVexSymbolRegistry`.

## Risks / Watch Items
- **Reflection Performance**: Ensuring generalized symbol lookups via C++ trait reflection do not incur unacceptable overhead compared to the hardcoded `FDroidState` accessors.
- **VVM Bytecode Fidelity**: Ensuring our IR Jumps map correctly to VVM's stack-based branching.
- **Module Cycles**: Maintaining strict decoupling between the Core Meta module and the VEX/Verse higher-level systems.
