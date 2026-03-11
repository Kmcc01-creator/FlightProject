# FlightProject Architecture & Workflow Guide

Status: older high-level workflow reference from an earlier Mass/ECS transition phase.

For the current architectural source of truth, prefer:

- [../Architecture/Overview.md](../Architecture/Overview.md)
- [../Architecture/CurrentProjectVision.md](../Architecture/CurrentProjectVision.md)
- [../Workflow/GameplaySystems.md](../Workflow/GameplaySystems.md)
- [CurrentExtensionDirections.md](CurrentExtensionDirections.md)

## 1. Architectural Best Practices (UE 5.7+)

### Thinking Beyond the Actor Model
In Unreal Engine 5.7, especially for high-count simulation (Swarm tech), we shift away from the classic "Monolithic Actor" model toward a **Mass/ECS (Entity Component System)** approach.

| Aspect | Classic Actor Model (Old) | Mass/ECS Model (New) |
| :--- | :--- | :--- |
| **Concept** | `AActor` contains Logic, Data, and View. | Logic, Data, and View are separated. |
| **Memory** | High overhead per instance. | Minimal overhead (packed structs). |
| **Scaling** | Hundreds of active agents. | Tens of thousands of active entities. |

### The "View vs. Simulation" Split
We "explode" the traditional Actor into three distinct layers:

1.  **Data (Fragments):**
    *   Pure C++ structs inheriting from `FMassFragment`.
    *   Contains *only* state (e.g., `Velocity`, `Health`, `TeamID`).
    *   No functions or logic.
2.  **Logic (Processors):**
    *   Classes inheriting from `UMassProcessor`.
    *   Iterate over chunks of entities matching a specific query (e.g., "All entities with `Velocity` and `Transform`").
    *   Highly cache-efficient.
3.  **View (Representation):**
    *   Visuals are decoupled from the simulation.
    *   **Far:** Instanced Static Meshes (ISM/HISM) for distant entities (cheap).
    *   **Near:** Temporary `AActor` spawned only when close to the camera (High LOD).

---

## 2. Composition Model: Game Features

To improve modularity and avoid monolithic classes (like a giant `FlightGameMode`), we use **Game Feature Plugins**.

### The Pattern
Instead of hard-coding features (like Swarms) into the main `FlightProject` module:

1.  **Encapsulate:** Create a Game Feature Plugin (e.g., `SwarmEncounter`).
2.  **Move:** Place subsystems (e.g., `FlightSwarmSpawnerSubsystem`), assets, and mechanics inside this plugin.
3.  **Hook:** The plugin "hooks" into the core game via the `GameFeatureData` asset using **Actions**:
    *   `AddComponents`: Adds a manager component to the `GameState` only when the plugin is active.
    *   `AddCheats`: Registers console commands.

### Benefits
*   **Isolation:** Disable the plugin, and the memory/logic overhead vanishes.
*   **Iterative Testing:** Test mechanics (Racing, Combat, Swarms) in isolation.
*   **Teamflow:** Reduces merge conflicts in core classes.

---

## 3. Editor Workflow & Layout (Linux)

For simulation development, prioritizing data visibility over visual aesthetics is key.

### Recommended "Control Center" Layout
*   **Bottom Dock:**
    *   **Output Log:** Essential for `UE_LOG` monitoring.
    *   **Message Log:** Critical errors.
    *   **Python/Cmd Console:** For scripted automation.
*   **Right Dock:**
    *   **Mass Gameplay Debugger:** *Crucial* for inspecting ECS entities (Standard Details panel usually won't show them).
    *   **StateTree Debugger:** Real-time visualization of AI decision making.
*   **Left Dock:**
    *   **Content Browser:** Use "Columns" view for better Linux file visibility.

### Layout Persistence
UE5 on Linux stores layouts in:
`Saved/Config/Linux/EditorLayout.ini`

*Tip:* Once a layout is perfect, back it up to `Config/Layouts/` in the repo to restore it after editor updates or resets.

### Scripted Startup (Python)
Use Python to automate daily setup (opening maps, setting view modes).
Example `Scripts/EditorStartup.py`:
```python
import unreal
# Load the standard test map
unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/TestMaps/L_SwarmTest_01")
print("FlightProject Environment Loaded")
```
