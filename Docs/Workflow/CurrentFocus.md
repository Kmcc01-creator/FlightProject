# Current Focus: Orchestration Binding And Generalized VEX Runtime

## Active Goals
1. **Orchestration Binding**: Deepen the new orchestration-issued binding path so runtime behavior selection moves from exact/default and anchor-aware legality into richer startup-profile-aware binding policy and reporting.
2. **General System Authoring**: Continue decoupling the VEX compiler and `FVexSymbolRegistry` from `FDroidState` so VEX can compile against arbitrary reflected C++ types.
3. **Startup And Integration Coverage**: Keep the new startup sequencing surface stable and deepen coverage from light report/bootstrap tests into fuller startup-profile and post-spawn integration tests.
4. **CI/RENDER Stability**: Establish a software-Vulkan (lavapipe) lane so GPU automation can reach discovery even when hardware Vulkan is unavailable or unstable.
5. **Navigation Redesign Investigation**: Investigate whether the current nav/probe/mesh system should be redesigned or reimplemented so it fits the project vision of explicit contracts, projected future states, validation before commit, and meaningful reports.

## Current Status (2026-03-10)
- **Milestone 0 Landed**:
  - Restored the default headless development loop to green.
  - Fixed the stale Verse-lowering parser expectation and the logging-boundary bridge regressions.
  - `./Scripts/build_targets.sh Development --no-uba --verify` and `TEST_PRESET=triage ./Scripts/run_tests_phased.sh --timestamps` are both passing again.
- **Milestone 1 Landed**:
  - `AFlightGameMode::StartPlay()` now delegates the default sandbox path through:
    - reusable world bootstrap,
    - orchestration rebuild before spawn,
    - initial swarm spawn,
    - orchestration rebuild after spawn.
  - The Gauntlet GPU swarm path remains explicit and isolated from reusable bootstrap/orchestration work.
  - `UFlightOrchestrationSubsystem` now exposes a single `Rebuild()` entrypoint used by scripting and debug surfaces.
- **Light Startup Sequencing Coverage Landed**:
  - Added `FlightProject.Integration.Startup.Sequencing` as a complex automation suite.
  - Current coverage is intentionally lightweight:
    - bootstrap completion signaling,
    - orchestration rebuild validity,
    - startup report JSON surface.
  - This is the seed for heavier startup-profile and post-spawn coverage, not the final fixture strategy.
- **Milestone 2 Deeper Slice Landed**:
  - `UFlightVexBehaviorProcessor` now resolves behavior selection per chunk/cohort through `UFlightOrchestrationSubsystem` before falling back.
  - spawned Mass swarm batches carry a shared cohort identity (`Swarm.Default` or `SwarmAnchor.*`) so the processor consumes orchestration-visible cohorts instead of a hardcoded global behavior ID.
  - swarm anchors now project authored legality into orchestration cohorts:
    - preferred behavior ID,
    - allowed behavior IDs,
    - denied behavior IDs,
    - required behavior contracts.
  - orchestration now picks the legal executable behavior for each cohort instead of relying primarily on one default binding.
  - added `FlightProject.Orchestration.Bindings` automation coverage for:
    - default fallback,
    - exact-binding precedence,
    - executable-plan preference,
    - anchor preference legality,
    - anchor contract legality.
- **Behavior Compile Policy Contract Landed**:
  - `UFlightDataSubsystem` now hosts a typed `FFlightBehaviorCompilePolicyRow` contract for authored compile/execution policy inputs.
  - policy matching now supports behavior ID, cohort name, startup/profile name, specificity, and priority.
  - this policy is intentionally still authored intent, not compile/runtime truth.
  - next TODO:
    - thread policy resolution into `UFlightVerseSubsystem::CompileVex(...)`,
    - use it to influence preferred domain, fallback allowance, and required symbol/contract checks,
    - expose the selected policy in orchestration/report surfaces.
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
- **Binding Policy Next Gap**: anchor-aware legality now exists, but startup-profile-aware legality and richer "why this binding was chosen" reporting are still TODO.
- **Compile Policy Integration Gap**: the behavior compile policy contract now exists in `UFlightDataSubsystem`, but the actual VEX/Verse compile path and orchestration reports do not consume it yet.
- **Navigation Architecture Gap**: the current nav/probe/mesh direction still needs an explicit design pass to decide whether it should remain incremental or be reimplemented around clearer orchestration/spatial contracts.
- **Startup Test Depth**: The new startup sequencing automation proves the light callable/report surfaces, but it does not yet assert full `StartPlay()` behavior against a dedicated world/profile fixture.
- **Reflection Performance**: Ensuring generalized symbol lookups via C++ trait reflection do not incur unacceptable overhead compared to the hardcoded `FDroidState` accessors.
- **VVM Bytecode Fidelity**: Ensuring our IR Jumps map correctly to VVM's stack-based branching.
- **Module Cycles**: Maintaining strict decoupling between the Core Meta module and the VEX/Verse higher-level systems.
- **GPU Environment Noise**: Headless runs still report Vulkan bridge startup errors in this environment even though the non-GPU phases are passing.
