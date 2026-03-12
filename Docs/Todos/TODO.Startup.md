# TODO: Startup

This file tracks TODOs around startup policy, `GameMode` thinning, bootstrap/orchestration sequencing, and startup test depth.

## Current TODOs

### 1. GameMode Thinning Follow-Through

Priority: Medium  
Status: Active  
Owner/Surface: `AFlightGameMode` policy boundary

Move remaining debug/demo helpers out of `AFlightGameMode` so the class stays focused on gameplay-framework policy and startup selection.

Relevant surfaces: [GameModeBootstrapBoundary.md](../Architecture/GameModeBootstrapBoundary.md), [FlightGameMode.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/FlightGameMode.h), [FlightGameMode.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightGameMode.cpp).

### 2. Startup Coverage Depth

Priority: High  
Status: Active  
Owner/Surface: startup sequencing automation

Add dedicated startup-profile/world fixtures that exercise real `StartPlay()` sequencing and assert post-spawn orchestration/cohort outcomes, not only lightweight callable/report surfaces.

Relevant surfaces: [CurrentFocus.md](../Workflow/CurrentFocus.md), [FlightStartupSequencingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStartupSequencingAutomationTests.cpp).

### 3. Startup Profile/Report Integration

Priority: Medium  
Status: Planned  
Owner/Surface: startup observability and orchestration-adjacent reporting

Expose the active startup profile more directly in orchestration or adjacent startup-facing reports so profile-driven behavior is observable without reading `GameMode` logs alone.

Relevant surfaces: [FlightGameMode.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightGameMode.cpp), [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp), [CurrentBuild.md](../Workflow/CurrentBuild.md).

### 4. Bootstrap/Orchestration Contract Hardening

Priority: High  
Status: Active  
Owner/Surface: default sandbox startup path

Keep the default sandbox path explicit and reusable while avoiding drift back toward ad hoc startup work in `GameMode`.

Relevant surfaces: [WorldExecutionModel.md](../Architecture/WorldExecutionModel.md), [GameModeBootstrapBoundary.md](../Architecture/GameModeBootstrapBoundary.md), [FlightWorldBootstrapSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightWorldBootstrapSubsystem.cpp), [FlightGameMode.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/FlightGameMode.cpp).

## Exit Condition

- `GameMode` remains a thin policy surface
- startup-profile-driven behavior is observable and testable
- startup sequencing has dedicated integration coverage beyond the current lightweight callable/report slice

## Completed / Archived

- Completed (2026-03-12): orchestration rebuild sequencing regression no longer reproduces after preserving execution-plan generation/timestamp through visibility reset in [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp). Verified by `FlightProject.Integration.Startup.Sequencing.OrchestrationRebuildAdvancesPlan` in [FlightStartupSequencingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStartupSequencingAutomationTests.cpp).
