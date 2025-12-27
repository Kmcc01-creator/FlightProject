# The Solo Flight Framework: Ergonomic Development in UE5

As a solo developer, efficiency is paramount. This framework leverages the strengths of C++, Blueprints, and Python to create a workflow that minimizes friction and context switching.

## 1. The Triad Roles

| Language | Role | Description | "Solo Dev" Rule |
| :--- | :--- | :--- | :--- |
| **C++** | **The Engine** | High-performance logic, ECS (Mass), Core Systems, Networking. | **"Write specific, reuse generic."** Build robust base classes and expose them to Blueprints. |
| **Blueprints** | **The Game** | Event scripting, UI (UMG), Prototyping, Linking assets. | **"Glue, don't calculate."** Use BP to connect C++ blocks. If math gets complex, move it to C++. |
| **Python** | **The Assistant** | Editor Automation, Asset Pipeline, Bulk Editing, Setup. | **"Never click twice."** If you do a task >3 times a day (e.g., setup map, rename assets), script it. |

## 2. Bridging the Layers

### C++ -> Blueprint (Reflection)
To make C++ accessible to the "Designer" (you in a different hat), use these macros:

*   **Functions:** `UFUNCTION(BlueprintCallable)` allows BP to call C++.
*   **Events:** `UFUNCTION(BlueprintImplementableEvent)` allows C++ to ask BP to do something (e.g., `OnTakeDamage`).
*   **Data:** `UPROPERTY(EditAnywhere, BlueprintReadWrite)` exposes variables.

### Blueprint -> Python (Editor Tools)
You can run Python scripts from a friendly UI using **Editor Utility Widgets (EUW)**.

1.  **Script:** Write your logic in `Content/Python/FlightProject/MyTool.py`.
2.  **UI:** Create an **Editor Utility Widget** Blueprint.
3.  **Link:** Add a Button. On Click -> `Execute Python Script` node.
4.  **Code:** `import FlightProject.MyTool; FlightProject.MyTool.run()`

**Benefit:** You get a dockable "Flight Tools" panel in the editor to run complex setup scripts without touching a terminal.

## 3. Data-Driven Architecture

Avoid "Hardcoding" values in C++ or Blueprints. Use **Data Assets**.

1.  **Define (C++):** Create a `UDataAsset` subclass (e.g., `UFlightSwarmConfig`).
2.  **Populate (Editor):** Create instances of this asset (`DA_EasySwarm`, `DA_HardSwarm`).
3.  **Consume (Code/BP):** Your Spawners/Systems just take a reference to the asset.

*Why?* You can tweak balance numbers (Speed, Health, Count) instantly without recompiling C++ or digging through Blueprint nodes.

## 4. Directory Structure Recommendation

```text
Content/
  ├── Blueprints/       # The "Glue" Logic
  ├── Python/           # The "Automation" Scripts
  │   ├── init_unreal.py
  │   └── FlightProject/
  │       └── SwarmSetup.py
  └── Data/             # The "Tuning" Knobs (Data Assets)
Source/
  └── FlightProject/    # The "Engine" Code
```
