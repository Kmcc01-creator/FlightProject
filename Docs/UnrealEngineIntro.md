# Unreal Engine 5: Introductory Review & FlightProject Integration

This document provides a foundational overview of Unreal Engine 5's core systems, development workflows, and specific configurations used within the **FlightProject** workspace.

---

## 1. Project Configuration & Environment

Unreal Engine separates configuration into two primary scopes. Understanding this distinction is critical for team collaboration.

### Project Settings vs. Editor Preferences

| Feature | Project Settings | Editor Preferences |
| :--- | :--- | :--- |
| **Menu Path** | `Edit > Project Settings` | `Edit > Editor Preferences` |
| **Scope** | Global game behavior (Rendering, Physics, Input). | Personal local workflow (Theme, IDE, UI Layout). |
| **Storage** | `Config/Default*.ini` (e.g., `DefaultEngine.ini`). | `Saved/Config/Linux/EditorSettings.ini` (Local). |
| **Version Control** | **Must be committed** to Git/Perforce. | **Ignore/Exclude** from version control. |

### FlightProject Specific Optimizations (Linux/Wayland)
Located in `Config/Linux/LinuxEngine.ini` and `DefaultEngine.ini`:
*   **High DPI Support**: It is recommended to **Disable High DPI Support** in Editor Preferences when running on Wayland to avoid coordinate scaling conflicts.
*   **Editor Responsiveness**: 
    *   `Slate.TickRate=120` for high-Hz display smoothness.
    *   `Editor.Performance.ThrottleUnfocused=0` to prevent "wake-up" lag.
*   **Shader Compilation**: Optimized for 15 threads with `MaxShaderJobBatchSize=16` to handle massive shader queues efficiently.

---

## 2. World & Game State Management

The gameplay framework is a modular architecture designed to separate game rules, state tracking, and player interaction.

| Class | Location | Persistence | Purpose |
| :--- | :--- | :--- | :--- |
| **GameMode** | Server Only | Per Level | The "Rulebook": Defines rules for spawning, win/loss, and class defaults. |
| **GameState** | Server & Clients | Per Level | The "Scoreboard": Tracks shared state (Timer, Team Score, Global Progress). |
| **PlayerState** | Server & Clients | Per Level | The "Stats": Tracks individual player data (Score, Name, Ping). |
| **GameInstance** | Server & Clients | **Entire Session** | The "Long-Term Memory": Persists data across level changes (Settings, XP). |
| **World** | Server & Clients | Per Level | The "Container": Manages all actors, systems, and level life-cycles. |

---

## 3. Asset Spawning at Runtime

Spawning actors is a fundamental task that can be performed via C++ or Blueprints.

### C++: `UWorld::SpawnActor`
The template version is preferred for automatic casting and type safety.

```cpp
#include "Engine/World.h"
#include "MyActor.h"

// Define transform and parameters
FVector Location(0, 0, 100);
FRotator Rotation(0, 0, 0);
FActorSpawnParameters SpawnParams;
SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

// Spawn
AMyActor* NewActor = GetWorld()->SpawnActor<AMyActor>(AMyActor::StaticClass(), Location, Rotation, SpawnParams);
```

### Blueprint: `Spawn Actor from Class`
1.  **Node**: `Spawn Actor from Class`.
2.  **Transform**: Right-click the **Spawn Transform** pin and select **Split Struct Pin** to expose Location/Rotation.
3.  **Variables**: Use the "Expose on Spawn" setting in variable details to set initial values directly on the spawning node.

---

## 4. FlightProject Testing & Discovery

As noted in `Docs/Workflow/CurrentBuild.md`, testing requires specific context for command-line discovery.

### Automation Discovery
To ensure tests are visible to the `UnrealEditor-Cmd` automation runner (used in CI), they must be flagged with:
*   `EAutomationTestFlags::EditorContext`

### Core Test Categories
*   **Smoke Tests**: Fast, trait-level verification (Reflection, RowTypes).
*   **Engine Tests**: Integration tests requiring a full World/Subsystem state.

---

## 5. Key References
- [Current Focus](Projects/FlightProject/Docs/Workflow/CurrentFocus.md): Active technical roadmap.
- [Build Help](Projects/FlightProject/Docs/Workflow/CurrentBuild.md): Environment and command reference.
- [Architectural Specs](Projects/FlightProject/Docs/Architecture/): Deep design documentation.
