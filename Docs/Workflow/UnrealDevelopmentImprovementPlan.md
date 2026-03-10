# FlightProject Unreal Development Improvement Plan

_Drafted: March 10, 2026._

This document turns the current architectural direction and repo state into a concrete improvement plan for FlightProject's Unreal Engine development workflow.

It is not a replacement for the architecture docs.
It is the execution frame that should guide the next sequence of implementation and validation work.

Primary references:

- `Docs/Architecture/ProjectOrganization.md`
- `Docs/Architecture/WorldExecutionModel.md`
- `Docs/Architecture/GameModeBootstrapBoundary.md`
- `Docs/Architecture/OrchestrationSubsystem.md`
- `Docs/Architecture/CurrentProjectVision.md`
- `Docs/Architecture/VexStateMutationSchemaFrame.md`
- `Docs/Workflow/OrchestrationImplementationPlan.md`
- `Docs/Workflow/CurrentBuild.md`
- `Docs/Architecture/EditorAssetShim.md`

## 1. Objective

Improve FlightProject development by making five things true at the same time:

1. the default Linux build and headless validation path are trustworthy;
2. startup sequencing matches the intended `GameMode -> Bootstrap -> Orchestration` boundary;
3. behavior execution is driven by orchestration-issued bindings instead of hardcoded globals;
4. Unreal-specific authoring seams are handled by repeatable shims and repair flows;
5. GPU automation reaches discovery reliably and is clearly separated from environment failures.

## 2. Current Snapshot (March 10, 2026)

Current reality from `Docs/Workflow/CurrentBuild.md`, `Docs/Workflow/CurrentFocus.md`, and the active source tree:

- `./Scripts/build_targets.sh Development --no-uba` is compiling successfully.
- `./Scripts/build_targets.sh Development --no-uba --verify` is also succeeding against the current verify subset.
- Headless phased validation is green on the default `triage` preset.
- GPU-required validation is blocked before automation discovery by Vulkan device creation failure.
- `UFlightOrchestrationSubsystem` exists and already reports services, paths, anchors, spatial fields, and compiled behaviors.
- `AFlightGameMode::StartPlay()` is still the trigger surface, but the default sandbox path now delegates through bootstrap and explicit orchestration rebuild calls.
- A light startup sequencing complex automation suite now exists in Phase 1.
- `UFlightVexBehaviorProcessor` now resolves orchestration-issued bindings per chunk/cohort before falling back, and spawned swarm batches carry cohort identity into Mass.
- swarm anchors now contribute legality policy into orchestration cohorts:
  - preferred behavior,
  - allowed IDs,
  - denied IDs,
  - required contracts.
- `UFlightDataSubsystem` now hosts a behavior compile policy contract surface:
  - behavior ID / cohort / profile targeting,
  - domain preference,
  - fallback and generated-only policy,
  - required symbols and contracts.
- the remaining behavior-binding TODO is startup-profile-aware legality and richer binding-selection reporting.

This means the project does not need a new direction.
It needs the current direction finished and stabilized.

## 2.1 Milestone Status

Current milestone status:

- Milestone 0: complete
  - headless parser and logging-boundary regressions fixed
  - phased headless validation green
  - `--verify` green
- Milestone 1: materially complete
  - startup boundary cleaned up for the default sandbox path
  - explicit orchestration rebuild surface added
  - light startup sequencing automation added
- Milestone 2: in progress
  - processor/runtime path now consumes orchestration-issued bindings
  - anchor-aware legality has landed
  - behavior compile policy rows now exist as authored data contracts
  - startup-profile-aware legality and binding-selection reporting remain TODO

## 3. Operating Principles

All work in this plan should follow these rules:

- Keep `AFlightGameMode` as a thin gameplay-framework policy surface.
- Keep reusable world-preparation work in `UFlightWorldBootstrapSubsystem`.
- Keep world-scoped coordination, bindings, plans, and reports in `UFlightOrchestrationSubsystem`.
- Prefer explicit contracts, bindings, and reports over implicit runtime assumptions.
- Treat editor automation as a shim boundary, not as the semantic owner of runtime logic.
- Separate code regressions from platform/environment failures in CI and scripts.

## 4. Workstreams

The plan is organized into six workstreams.
They should be executed mostly in order because the earlier tracks reduce ambiguity for the later ones.

### Workstream A: Baseline Stability

Purpose:
Make the default development loop credible before deeper architectural migration continues.

Primary targets:

- `Source/FlightProject/Private/Tests/FlightVexParserSpec.cpp`
- `Source/FlightProject/Private/Tests/FlightLoggingBoundaryAutomationTests.cpp`
- `Scripts/run_tests_phased.sh`
- `Scripts/build_targets.sh`
- `Docs/Workflow/CurrentBuild.md`

Concrete tasks:

1. Fix the Verse lowering expectation mismatch in `FlightVexParserSpec`.
2. Fix duplicate or missing message behavior in the two logging-boundary tests.
3. Re-run phase 1 and phase 2 through the `triage` preset and capture the new baseline.
4. Keep `build_targets.sh --verify` aligned with the real fast gate used by contributors.

Exit gate:

- phased headless validation is green on the default `triage` preset;
- verify remains green;
- current build docs reflect the true pass/fail state and commands.

Suggested owner:

- Runtime + test infrastructure

### Workstream B: Startup Sequencing Cleanup

Purpose:
Make startup behavior match the documented `GameMode` and bootstrap boundary instead of relying on increasingly sticky inline `StartPlay` logic.

Primary targets:

- `Source/FlightProject/Private/FlightGameMode.cpp`
- `Source/FlightProject/Private/FlightWorldBootstrapSubsystem.cpp`
- `Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp`
- `Docs/Workflow/StartPlayDecomposition.md`
- `Docs/Workflow/OrchestrationImplementationPlan.md`

Concrete tasks:

1. Move `StartPlay()` closer to a policy-only trigger surface.
2. Ensure the default sandbox path runs:
   - bootstrap;
   - orchestration visibility rebuild;
   - orchestration plan rebuild;
   - initial swarm spawn;
   - post-spawn orchestration refresh.
3. Keep the Gauntlet GPU swarm path explicit and isolated from reusable world bootstrap work.
4. Expose any missing bootstrap completion hooks needed by orchestration and spawn sequencing.

Exit gate:

- `StartPlay()` chooses startup policy and delegates;
- world bootstrap owns reusable preparation;
- orchestration is called explicitly before and after spawn in the default path;
- no new reusable world setup logic is added directly to `GameMode`.

Suggested owner:

- Runtime startup/orchestration

### Workstream C: Orchestration-Owned Behavior Binding

Purpose:
Replace the current global behavior assumption with explicit cohort-to-behavior binding and execution-plan consumption.

Primary targets:

- `Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp`
- `Source/FlightProject/Public/Orchestration/FlightBehaviorBinding.h`
- `Source/FlightProject/Public/Orchestration/FlightExecutionPlan.h`
- `Source/FlightProject/Private/Mass/UFlightVexBehaviorProcessor.cpp`
- `Source/FlightProject/Private/Verse/`
- `Docs/Workflow/SchemaIrImplementationPlan.md`

Concrete tasks:

1. Keep explicit cohort records from visible anchors, paths, and available behavior metadata as the canonical binding input.
2. Preserve the current anchor-aware legality path:
   - preferred behavior,
   - allowed IDs,
   - denied IDs,
   - required contracts.
3. Thread the new behavior compile policy contract from `UFlightDataSubsystem` into `UFlightVerseSubsystem::CompileVex(...)` so authored policy can influence preferred domain, fallback allowance, and required symbol/contract checks.
4. Add startup-profile-aware legality so profile policy can further constrain or redirect cohort behavior selection.
5. Publish reduced behavior records from `UFlightVerseSubsystem` into orchestration as the canonical source of runtime-usable behavior facts.
6. Keep `UFlightVexBehaviorProcessor` consuming orchestration bindings with a constrained fallback only for worlds with no orchestration data.
7. Expand automation/reporting so behavior selection explains why a binding was chosen or rejected, including selected compile policy where relevant.

Exit gate:

- no hardcoded global behavior ID in the processor path;
- orchestration owns active cohort bindings;
- behavior execution consumes orchestration outputs;
- test coverage exists for both bound and fallback paths.

Suggested owner:

- Runtime orchestration + VEX/Verse integration

### Workstream D: Editor and Asset Automation Hardening

Purpose:
Keep Unreal-native authoring friction behind idempotent scriptable shims rather than manual editor procedures.

Primary targets:

- `Content/Python/FlightProject/AssetTools.py`
- `Content/Python/create_startup_profiles.py`
- `Source/FlightProject/Public/FlightScriptingLibrary.h`
- `Source/FlightProject/Private/FlightScriptingLibrary.cpp`
- `Docs/Architecture/EditorAssetShim.md`

Concrete tasks:

1. Preserve startup profile asset creation as a script-first path and keep validation around those assets.
2. Add the next editor shim targets called out in `EditorAssetShim.md`:
   - Niagara semantic repair;
   - level-authoring helpers;
   - project health command;
   - headless repair entrypoint.
3. Make every repair path return structured issues that CI or startup scripts can surface.
4. Keep generated payloads and reports outside normal `Content/` paths unless Unreal must own them as assets.

Exit gate:

- common asset repair/setup flows are idempotent;
- startup assets, Mass traits, and future Niagara/world repair steps are scriptable;
- validation reports identify semantic drift, not just missing assets.

Suggested owner:

- Tools/editor automation

### Workstream E: GPU and Vulkan Validation Lane

Purpose:
Turn the current GPU automation blocker into a diagnosed platform lane instead of a vague failure bucket.

Primary targets:

- `Scripts/run_tests_full.sh`
- `Docs/Workflow/CurrentBuild.md`
- `Docs/Workflow/TestingValidationPlan.md`
- Linux Vulkan environment/config docs

Concrete tasks:

1. Split GPU failures into:
   - device creation failure;
   - startup/shader failure;
   - automation discovery failure;
   - test execution failure.
2. Add or document a software-Vulkan or lavapipe validation path for CI where hardware Vulkan is unstable or unavailable.
3. Keep hardware Vulkan as a separate lane for real render/device validation.
4. Update docs and script output so contributors can tell immediately whether a failure is code or environment.

Exit gate:

- GPU/system validation reaches automation discovery in at least one supported runner path;
- hardware-only failures are isolated from general CI health;
- script output clearly labels the failure phase.

Suggested owner:

- Platform/build/test infrastructure

### Workstream F: Contributor Workflow Tightening

Purpose:
Reduce ambiguity for contributors so the preferred development loop matches the actual architecture and test gates.

Primary targets:

- `Scripts/README.md`
- `Docs/README.md`
- `Docs/Workflow/CurrentBuild.md`
- `Docs/Workflow/CurrentFocus.md`

Concrete tasks:

1. Keep a single documented default loop for build, verify, phased tests, and GPU follow-up.
2. Make sure the docs point to orchestration/startup policy as the current runtime direction.
3. Keep the current known failures, caveats, and blocked paths date-stamped.
4. Avoid stale guidance that implies `GameMode` is still the right home for reusable world setup.

Exit gate:

- contributor docs match the actual commands and architecture;
- the default dev loop is short, repeatable, and truthful;
- blocked paths are clearly labeled with dates and evidence.

Suggested owner:

- Shared across runtime + tooling

## 5. Milestone Order

This is the recommended execution order.

### Milestone 0: Restore Trust In The Default Loop

Contains:

- Workstream A
- documentation refresh from Workstream F needed to reflect the corrected baseline

Definition of done:

- default build passes;
- phased headless validation passes;
- current build docs are updated with the new baseline.

Status:

- complete

### Milestone 1: Finish The Startup Boundary

Contains:

- Workstream B

Definition of done:

- `GameMode` is visibly thinner;
- bootstrap and orchestration own the reusable startup sequence;
- orchestration refresh points are part of normal startup.

Status:

- complete for the current light integration slice
- follow-up work remains for deeper startup fixtures and stronger post-spawn assertions

### Milestone 2: Make Orchestration The Runtime Binding Surface

Contains:

- Workstream C

Definition of done:

- processor behavior selection comes from orchestration-issued bindings;
- fallback behavior selection is no longer the primary model;
- startup-profile-aware legality is represented in the binding decision path.

Status:

- in progress
- exact/default binding consumption and anchor-aware legality are landed
- data-authored behavior compile policy contract is landed
- startup-profile-aware legality and richer selection reporting remain TODO

### Milestone 3: Harden Editor-Time Repair And Validation

Contains:

- Workstream D

Definition of done:

- asset and world repair flows are idempotent, scriptable, and visible in validation reports.

### Milestone 4: Rebuild GPU Validation As A Real Lane

Contains:

- Workstream E
- follow-up documentation updates in Workstream F

Definition of done:

- GPU automation reaches discovery in at least one supported configuration;
- environment-blocked failures are explicit.

## 6. Suggested 8-Week Schedule

This is a sequencing suggestion, not a rigid deadline contract.

Weeks 1-2:

- complete Milestone 0;
- stop carrying the three known headless failures;
- refresh build/test docs.

Weeks 2-4:

- complete Milestone 1;
- wire orchestration rebuilds into the default startup sequence.

Weeks 4-6:

- continue Milestone 2;
- thread behavior compile policy into the real compile path;
- extend binding legality from anchors into startup-profile policy;
- add reporting that explains per-cohort behavior choice.

Weeks 6-7:

- complete Milestone 3;
- expand editor shims and health/repair surfaces.

Weeks 7-8:

- complete Milestone 4;
- establish a clear GPU validation lane and update scripts/docs accordingly.

## 7. Verification Matrix

Every milestone should end with explicit evidence.

### Required recurring checks

- `./Scripts/build_targets.sh Development --no-uba`
- `./Scripts/build_targets.sh Development --no-uba --verify`
- `TEST_PRESET=triage ./Scripts/run_tests_phased.sh --timestamps`

### Milestone-specific checks

Milestone 0:

- targeted reruns for parser and logging boundary tests

Milestone 1:

- startup-path run confirming bootstrap/orchestration ordering
- orchestration report dump before and after spawn

Milestone 2:

- automation proving processor behavior comes from orchestration bindings
- automation proving anchor legality affects binding resolution
- debug report or JSON export showing cohort-to-behavior resolution, compile-policy selection, and remaining profile-aware TODOs

Milestone 3:

- headless or editor-driven repair pass producing structured issues
- validation showing semantic repair success rather than mere asset existence

Milestone 4:

- `TEST_SCOPE=all ./Scripts/run_tests_full.sh`
- software-Vulkan or lavapipe lane if hardware Vulkan is not available

## 8. Non-Goals

The following should not be pulled into the near-term plan unless they directly unblock the milestones above:

- full orchestration ownership of Mass entity enumeration;
- large render-graph ownership changes unrelated to validation or startup;
- broad plugin/module reshuffles unrelated to current startup, orchestration, or VEX runtime seams;
- forcing every authored concept into a UObject asset form;
- adding more generic abstractions before the existing runtime seams are made explicit.

## 9. Deferred Design Investigation: Navigation Probe / Mesh System

This is a deliberate follow-on TODO, not part of the current milestone critical path.

The current navigation stack mixes nav probes, buoy/graph authoring, mesh-like spatial interpretation, and world-specific runtime assumptions.
That may be acceptable as a temporary sandbox path, but it has not yet been re-evaluated against the current project vision.

The investigation goal is to decide whether the nav/probe/mesh system should:

- remain an incremental evolution of the current hub/probe path;
- be redesigned around explicit registries, contracts, legality, and reports;
- or be reimplemented as a cleaner orchestration/spatial service that projects candidate routes or traversal futures before commit.

Questions that should drive that investigation:

1. What is the actual canonical navigation primitive for FlightProject:
   - spline path,
   - probe cloud,
   - graph,
   - mesh/volume,
   - or a layered combination?
2. Which layer owns legality:
   - obstacle clearance,
   - altitude/airspace rules,
   - mission/profile constraints,
   - swarm cohort constraints?
3. How should nav state enter orchestration:
   - as visible participants,
   - as projected route candidates,
   - as field constraints,
   - or as a dedicated provider surface?
4. What reports should explain why one route/topology was selected over another?
5. Which existing systems are temporary scaffolding versus architecture we intend to keep:
   - `UFlightNavGraphDataHubSubsystem`,
   - `AFlightNavBuoyRegion`,
   - CSV-authored nav probe layouts,
   - current spatial test range authoring?

Expected output of the investigation:

- a recommendation to keep, redesign, or replace the current nav/probe/mesh path;
- a proposed canonical runtime boundary;
- migration notes for existing authored content and startup/bootstrap integration;
- validation/test implications for any future reimplementation.

## 10. Success Criteria

This plan is successful when the repo looks different in operational terms, not just architectural intent.

Success means:

- contributors can build and run the default validation path without guessing what is broken;
- startup flow is visibly aligned with the project boundary docs;
- orchestration is the real binding/report surface, not only a debug observer;
- editor-only Unreal friction is hidden behind repeatable shims;
- GPU automation failures are diagnosable by lane and phase.

If those conditions are true, FlightProject will be easier to extend without drifting away from the current architecture.
