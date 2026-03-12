# Current Focus: Orchestration Binding And Generalized VEX Runtime

## Active Goals
1. **Orchestration Binding**: Deepen the new orchestration-issued binding path so runtime behavior selection moves from exact/default and anchor-aware legality into richer startup-profile-aware binding policy and reporting.
2. **General System Authoring**: Continue decoupling the VEX compiler and `FVexSymbolRegistry` from `FDroidState` so VEX can compile against arbitrary reflected C++ types.
3. **Backend Selection To Runtime Commit**: Move from compile-time backend capability reporting into real runtime/backend commit rules so reported backend selection starts controlling executable dispatch when legal and available.
4. **Startup And Integration Coverage**: Keep the new startup sequencing surface stable and deepen coverage from light report/bootstrap tests into fuller startup-profile and post-spawn integration tests.
5. **CI/RENDER Stability**: Establish a software-Vulkan (lavapipe) lane so GPU automation can reach discovery even when hardware Vulkan is unavailable or unstable.
6. **Navigation Redesign Investigation**: Investigate whether the current nav/probe/mesh system should be redesigned or reimplemented so it fits the project vision of explicit contracts, projected future states, validation before commit, and meaningful reports.
7. **GPU Resource Contract Generalization**: Keep the new runtime GPU contract seam moving from the current buffer-first `FDroidState` slice toward images, transient attachments, and additional reflected resource types.
8. **Dual-Reflection Formalization**: Turn the current navigation-commit prototype into a clearer authoring pattern with explicit `Identity` vs `Report` roles and reusable base variants for dual-reflected systems.

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
- **Dual-Reflection Formalization Expanded (2026-03-12)**:
  - the `Identity` vs `Report` split is no longer only a navigation-commit prototype;
  - orchestration binding selection now uses the same pattern through `FFlightBehaviorBinding::FIdentity` and `FFlightBehaviorBinding::FReport`;
  - binding provenance is now being pushed one level further into structured `Selection` data so source, rule, and fallback can evolve independently;
  - current next step is to add structured ranking/evidence data for binding selection so startup-profile policy, contract filtering, and backend-availability inputs do not regress into stringly explanation fields;
  - after that, keep pushing explanation-only fields out of runtime selection carriers and into explicit report/provenance types as additional projection boundaries are formalized.
- **Behavior Compile Policy Integration Landed (2026-03-12)**:
  - `CompileVex(...)` now resolves behavior compile policy through explicit compile context or `UFlightDataSubsystem`.
  - selected policy metadata now flows through:
    - `UFlightVerseSubsystem::FVerseBehavior`,
    - compile artifact reports,
    - orchestration behavior reports.
  - current policy effects now include:
    - preferred-domain steering of selected backend,
    - fallback allowance,
    - generated-only acceptance,
    - required-symbol validation,
    - required-contract propagation into orchestration legality surfaces.
  - GPU-preferred policy is now explicit about current truth:
    - selected backend may remain `GpuKernel`,
    - committed backend stays unproven/`Unknown` until a real GPU commit path exists,
    - orchestration no longer treats `resolvedDomain == Gpu` as executable by itself.
- **Editor & GC Stability Landed**:
  - Silenced fatal `LogGarbage` warnings by properly implementing `Super::AddReferencedObjects` in `UFlightVerseSubsystem`.
  - Resolved `NumEvents` bound parameter ensures on startup for `FFlightSwarmForceCS` and `FFlightSwarmPredictiveCS`.
- **Automated Schema Asset Authoring Landed**:
  - Implemented `EnsureNiagaraSystemContract` via `UFlightScriptingLibrary` to programmatically author and save missing User Parameters and Data Interfaces to target `uassets` during editor startup.
- **GPU Resource Contract Runtime Alignment Landed**:
  - the reflection/schema GPU contract path is no longer only generated HLSL and manifest data;
  - schema can now resolve a structured-buffer contract by logical resource id and validate it against native runtime stride/layout;
  - `UFlightSwarmSubsystem` now consumes the `Swarm.DroidStateBuffer` contract when creating the persistent state buffer and RDG sort scratch buffer;
  - `UFlightScriptingLibrary::ValidateReflectedGpuContracts()` now exposes a scriptable validation surface for the reflected runtime GPU contracts.
- **VVM Assembler & Complex IR Landed**:
  - `FVexVvmAssembler` now supports multi-block IR (`if-else`, `while`).
  - Verified with `ComplexControlFlow` test (5-iteration loop incrementing state).
- **Schema-Driven Native Registry Landed**:
  - Dynamic thunks for `@symbols` and custom intrinsics (e.g. `square`).
  - Decoupled `UFlightVerseSubsystem` from hardcoded symbol names via `FVexSymbolRegistry`.
- **Backend Capability Selection Landed**:
  - schema binding now feeds explicit backend capability evaluation for `NativeScalar`, `NativeSimd`, `VerseVm`, and future `GpuKernel` paths.
  - compile artifact reports now include `selectedBackend` plus per-backend decision/reason entries.
  - `CompileVex(...)` now records backend legality and per-symbol backend diagnostics after schema binding.
  - this slice is intentionally reporting-first; the runtime dispatch path has not been fully re-gated yet.
- **Backend Dispatch / Policy Next TODO**:
  - adopt compile-policy context at real startup/orchestration call-sites so cohort/profile selectors influence compile truth outside explicit/manual compile calls.
  - keep tightening GPU runtime commitment so selected `GpuKernel` can move from reporting/pending into a real terminal-state commit contract.

## Risks / Watch Items
- **GPU Commit Gap**: selected `GpuKernel` can now be reported explicitly, but runtime still lacks a terminal-state GPU commit path that upgrades committed backend from `Unknown` to a proven GPU execution result.
- **Binding Policy Next Gap**: anchor-aware legality now exists, but startup-profile-aware legality and richer "why this binding was chosen" reporting are still TODO.
- **Binding Provenance Depth Gap**: binding `Report` is now structured enough for source/rule/fallback, but it still lacks full ranking evidence, startup-profile policy inputs, and backend-availability justification.
- **Compile Policy Call-Site Gap**: `CompileVex(...)` now consumes policy and reports it, but most production compile callers still use default behavior-id-only context instead of passing richer cohort/profile selectors.
- **GPU Resource Generalization Gap**: the new runtime GPU contract seam is still buffer-first and `FDroidState`-first; images, transient attachments, and additional reflected resource types are still TODO.
- **Navigation Architecture Gap**: the current nav/probe/mesh direction still needs an explicit design pass to decide whether it should remain incremental or be reimplemented around clearer orchestration/spatial contracts.
- **Startup Test Depth**: The new startup sequencing automation proves the light callable/report surfaces, but it does not yet assert full `StartPlay()` behavior against a dedicated world/profile fixture.
- **Reflection Performance**: Ensuring generalized symbol lookups via C++ trait reflection do not incur unacceptable overhead compared to the hardcoded `FDroidState` accessors.
- **Reflection Role Drift**: Preventing `Identity`, `Contract`, `Product`, and `Report` concerns from collapsing back into one type as dual-reflected systems spread.
- **VVM Bytecode Fidelity**: Ensuring our IR Jumps map correctly to VVM's stack-based branching.
- **Module Cycles**: Maintaining strict decoupling between the Core Meta module and the VEX/Verse higher-level systems.
- **GPU Environment Noise**: Headless runs still report Vulkan bridge startup errors in this environment even though the non-GPU phases are passing.
