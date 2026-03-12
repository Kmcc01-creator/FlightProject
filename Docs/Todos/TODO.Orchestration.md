# TODO: Orchestration

This file tracks TODOs for the world-scoped coordination surface, report fidelity, and execution-plan truthfulness.

## Current TODOs

### 1. Orchestration/Runtime Truth Alignment

Priority: High  
Status: Active  
Owner/Surface: orchestration report and execution-plan truthfulness

Ensure orchestration only advertises behaviors as executable when the runtime can actually commit the chosen execution path, especially for GPU-oriented behaviors.

Relevant surfaces: [OrchestrationSubsystem.md](../Architecture/OrchestrationSubsystem.md), [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp), [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp).

### 2. Binding Evidence Depth

Priority: Medium  
Status: Active  
Owner/Surface: binding provenance and report schema

Keep pushing binding provenance beyond source/rule/fallback into structured ranking and evidence data so startup-profile policy, contract filtering, and backend-availability inputs do not collapse back into string explanations.

Relevant surfaces: [CurrentFocus.md](../Workflow/CurrentFocus.md), [FlightBehaviorBinding.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Public/Orchestration/FlightBehaviorBinding.h), [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp).

### 3. Startup-Profile-Aware Binding Legality

Priority: High  
Status: Active  
Owner/Surface: cohort binding selection policy

Extend current anchor-aware/default binding selection so startup profile inputs materially participate in legality and ranking.

Relevant surfaces: [CurrentFocus.md](../Workflow/CurrentFocus.md), [OrchestrationImplementationPlan.md](../Workflow/OrchestrationImplementationPlan.md), [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp).

### 4. Spawn-Visibility Evolution

Priority: Medium  
Status: Active  
Owner/Surface: participant registration model

Continue moving from world-scan ingestion toward explicit registration where practical, especially for anchor/spawner coordination surfaces.

Relevant surfaces: [OrchestrationSubsystem.md](../Architecture/OrchestrationSubsystem.md), [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp), [FlightSwarmSpawnerSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Plugins/GameFeatures/SwarmEncounter/Source/SwarmEncounter/Private/FlightSwarmSpawnerSubsystem.cpp).

## Exit Condition

- execution plans are grounded in real executable runtime commitments
- binding reports preserve structured selection evidence
- participant visibility relies less on fallback world scanning and more on explicit registration/owned registries

## Completed / Archived

- None yet.
