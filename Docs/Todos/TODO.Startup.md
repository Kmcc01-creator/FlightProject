# TODO: Startup

This file tracks TODOs around startup policy, `GameMode` thinning, bootstrap/orchestration sequencing, and startup test depth.

## Current TODOs

### 1. GameMode Thinning Follow-Through

Priority: Medium  
Status: Active  
Owner/Surface: `AFlightGameMode` policy boundary

Move remaining debug/demo helpers out of `AFlightGameMode` so the class stays focused on gameplay-framework policy and startup selection.

Relevant surfaces: [GameModeBootstrapBoundary.md](../Architecture/GameModeBootstrapBoundary.md), [FlightGameMode.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/FlightGameMode.h), [FlightGameMode.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightGameMode.cpp).

### 2. Bootstrap/Orchestration Contract Hardening

Priority: High  
Status: Active  
Owner/Surface: default sandbox startup path

Keep the default sandbox path explicit and reusable while avoiding drift back toward ad hoc startup work in `GameMode`.

Relevant surfaces: [WorldExecutionModel.md](../Architecture/WorldExecutionModel.md), [GameModeBootstrapBoundary.md](../Architecture/GameModeBootstrapBoundary.md), [FlightWorldBootstrapSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightWorldBootstrapSubsystem.cpp), [FlightGameMode.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightGameMode.cpp).

## Exit Condition

- `GameMode` remains a thin policy surface
- startup-profile-driven behavior is observable and testable
- startup sequencing has dedicated integration coverage beyond the lightweight callable/report slice

## Completed / Archived

- Completed (2026-03-12): startup policy resolution and startup report assembly now live in [FlightStartupCoordinatorSubsystem.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Orchestration/FlightStartupCoordinatorSubsystem.h) and [FlightStartupCoordinatorSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightStartupCoordinatorSubsystem.cpp), leaving [FlightGameMode.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/FlightGameMode.h) and [FlightGameMode.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightGameMode.cpp) as a thinner gameplay-framework config/trigger surface instead of the owner of startup request building or startup report shaping.
- Completed (2026-03-12): the `DefaultSandbox` and `GauntletGpuSwarm` startup transactions now run through [FlightStartupCoordinatorSubsystem.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Orchestration/FlightStartupCoordinatorSubsystem.h) and [FlightStartupCoordinatorSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightStartupCoordinatorSubsystem.cpp), so `AFlightGameMode` now delegates the cross-system execution sequence instead of owning `bootstrap -> orchestration -> spawn -> reconcile` directly.
- Completed (2026-03-12): startup profile selection is now surfaced directly in orchestration reports through [FlightStartupCoordinatorSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightStartupCoordinatorSubsystem.cpp), [FlightOrchestrationReport.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Orchestration/FlightOrchestrationReport.h), and [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp), including active profile, resolution source, asset path/configuration, and GameMode presence in the orchestration JSON surface.
- Completed (2026-03-12): startup sequencing automation now includes a dedicated `DefaultSandboxStartupSequenceWorldFixture` in [FlightStartupSequencingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStartupSequencingAutomationTests.cpp) that drives the real `AFlightGameMode` startup sequence against a GameInstance-backed automation world and asserts post-spawn swarm/orchestration/cohort outcomes.
- Completed (2026-03-12): orchestration rebuild sequencing regression no longer reproduces after preserving execution-plan generation/timestamp through visibility reset in [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp). Verified by `FlightProject.Integration.Startup.Sequencing.OrchestrationRebuildAdvancesPlan` in [FlightStartupSequencingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStartupSequencingAutomationTests.cpp).
