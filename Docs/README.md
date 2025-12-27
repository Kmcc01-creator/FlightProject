# FlightProject Documentation

## Overview

FlightProject is an autonomous flight simulation sandbox built on Unreal Engine 5.7.1, featuring large-scale swarm support and GPU-driven systems.

## Documentation Structure

### [Environment/](Environment/) - Platform & Build

Development environment setup, build scripts, and platform-specific configuration.

| Document | Description |
|----------|-------------|
| [LinuxSetup.md](Environment/LinuxSetup.md) | Linux/CachyOS/Wayland/Hyprland configuration |
| [BuildAndRegen.md](Environment/BuildAndRegen.md) | Project regeneration and build commands |
| [VSCodeSetup.md](Environment/VSCodeSetup.md) | VS Code, clangd, tasks, and debugging |
| [Configuration.md](Environment/Configuration.md) | INI file inventory and editing workflow |
| [Troubleshooting.md](Environment/Troubleshooting.md) | Common issues and mitigations |

### [Architecture/](Architecture/) - Project Design

Technical architecture, engine integration, and system design.

| Document | Description |
|----------|-------------|
| [Overview.md](Architecture/Overview.md) | Project vision, goals, and roadmap |
| [EngineIntegration.md](Architecture/EngineIntegration.md) | UE systems usage and module dependencies |
| [MassECS.md](Architecture/MassECS.md) | Mass Entity fragments, processors, spawning |
| [Navigation.md](Architecture/Navigation.md) | Nav graph design and buoy regions |
| [DataPipeline.md](Architecture/DataPipeline.md) | CSV data loading and configuration |
| [FlightControls.md](Architecture/FlightControls.md) | Input system and movement component |
| [PluginArchitecture.md](Architecture/PluginArchitecture.md) | Plugin types and usage patterns |

### [Workflow/](Workflow/) - Development Process

Day-to-day development workflows and runtime systems.

| Document | Description |
|----------|-------------|
| [GameplaySystems.md](Workflow/GameplaySystems.md) | Runtime bootstrap and system interactions |
| [MapWorkflow.md](Workflow/MapWorkflow.md) | Level authoring and testing |
| [EditorTooling.md](Workflow/EditorTooling.md) | Editor setup and recommended layout |
| [StartPlayDecomposition.md](Workflow/StartPlayDecomposition.md) | Bootstrap subsystem migration (historical) |

### [Scripting/](Scripting/) - Automation & Rapid Iteration

Python, Blueprint, and data-driven development workflows.

| Document | Description |
|----------|-------------|
| [DevelopmentCycle.md](Scripting/DevelopmentCycle.md) | Python → Blueprint → C++ iteration loop |
| [EditorAutomation.md](Scripting/EditorAutomation.md) | Python tools, Editor Utility Widgets |
| [DataDrivenDesign.md](Scripting/DataDrivenDesign.md) | CSV workflows, Data Assets, runtime tuning |
| [PythonAPI.md](Scripting/PythonAPI.md) | Exposing C++ to Python (reflection) |

### [Future/](Future/) - Roadmap & Proposals

Planned features and design proposals.

| Document | Description |
|----------|-------------|
| [LevelLoader.md](Future/LevelLoader.md) | Composable level loading subsystem |
| [HegeticInterface.md](Future/HegeticInterface.md) | Data-to-visual telemetry framework |

### [Extensions/](Extensions/) - Experimental Integration

Exploratory work on external framework integration and custom tooling.

| Document | Description |
|----------|-------------|
| [EditorServiceArchitecture.md](Extensions/EditorServiceArchitecture.md) | DDC hierarchy, ZenServer, and team infrastructure |
| [M2FrameworkIntegration.md](Extensions/M2FrameworkIntegration.md) | m2 compiler infrastructure for UE debugging utilities |
| [MacrokidBehavioralSynthesis.md](Extensions/MacrokidBehavioralSynthesis.md) | macrokid_learning + neon_void → StateTree/Mass AI architecture |

## Quick Reference

### Key Directories

```
/home/kelly/Unreal/
├── UnrealEngine/                  # UE 5.7.1 source build
│   └── Engine/Binaries/Linux/     # Editor binary
└── Projects/FlightProject/
    ├── Source/FlightProject/
    │   ├── Public/                # Headers
    │   ├── Private/               # Implementation
    │   └── Public/Mass/           # ECS fragments & processors
    ├── Config/                    # INI configuration
    ├── Content/Data/              # CSV data files
    ├── Scripts/                   # Build & run scripts
    ├── Shaders/                   # Custom RDG shaders
    └── Docs/                      # This documentation
```

### Common Commands

```bash
# Set engine root (required)
export UE_ROOT=/home/kelly/Unreal/UnrealEngine

# Generate project files
./Scripts/generate_project_files.sh

# Build
./Scripts/build_targets.sh Development

# Run editor
./Scripts/run_editor.sh --wayland

# Cook and run game
./Scripts/run_game.sh --wayland --gamescope
```

### Core Classes

| Class | Purpose |
|-------|---------|
| `AFlightVehiclePawn` | Player-controlled aircraft |
| `AFlightAIPawn` | Autonomous drone (Actor-based) |
| `UFlightMovementComponent` | Custom flight physics |
| `UFlightSwarmSpawnerSubsystem` | Drone spawning orchestration |
| `UFlightDataSubsystem` | CSV data loading |
| `UFlightPathFollowProcessor` | Mass ECS path following |

## Two-Aspect Organization

This documentation is organized around two primary aspects:

1. **Environment** - How to build, run, and configure the project on Linux/CachyOS
2. **Architecture** - What systems exist, how they integrate with UE, and how to extend them

This separation allows environment setup to be understood independently of project-specific code, and vice versa.
