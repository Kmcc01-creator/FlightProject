# Editor Tools & Automation Guide

Status: practical reference for editor utility workflows.

For the current scripting/tooling context, also see:

- [../Scripting/README.md](../Scripting/README.md)
- [PythonReflectionGuide.md](PythonReflectionGuide.md)
- [CurrentExtensionDirections.md](CurrentExtensionDirections.md)

## 1. Creating an Editor Utility Widget (EUW)

An Editor Utility Widget allows you to create custom UI panels in the Unreal Editor to run your scripts.

### Step-by-Step
1.  **Enable Plugin:** Go to `Edit` -> `Plugins`. Ensure **"Editor Scripting Utilities"** is enabled.
2.  **Create Asset:**
    *   Right-click in `Content Browser` (e.g., in `Content/EditorTools`).
    *   Select **Editor Utilities** -> **Editor Utility Widget**.
    *   Name it `EUW_FlightTools`.
3.  **Design UI:**
    *   Double-click `EUW_FlightTools`.
    *   In the **Designer** tab (Palette), drag a **Button** onto the Canvas.
    *   Add a **Text** block on top of the button saying "Setup Swarm".
4.  **Add Logic:**
    *   Click the Button. In the **Details** panel (Events), click **On Clicked**.
    *   In the Graph, search for the node **"Execute Python Script"**.
    *   Connect the pins.
    *   **Command:** `import FlightProject.SwarmSetup; FlightProject.SwarmSetup.run_setup()`
5.  **Run:**
    *   Right-click `EUW_FlightTools` in Content Browser -> **Run Editor Utility Widget**.
    *   Dock the window anywhere you like.

---

## 2. Scripting Blueprint Creation with Python

You can use Python to **create** new Blueprint Assets, but you generally **cannot** edit their node graphs (wires/logic) via Python.

### Capability Matrix
| Task | Possible in Python? | API Method |
| :--- | :--- | :--- |
| Create new BP Asset | ✅ Yes | `unreal.AssetTools.create_asset(..., factory=BlueprintFactory)` |
| Set Parent Class | ✅ Yes | `factory.set_editor_property("ParentClass", MyBaseClass)` |
| Add Components | ⚠️ Sort of | Possible via Subobject Editor API (complex/unstable). |
| Add Nodes/Wires | ❌ **No** | Graph API is C++ only. |

### Example: Creating a Blueprint Factory Script
This script creates a new Blueprint inheriting from `FlightAIPawn`.

```python
import unreal

def create_drone_blueprint(name="BP_NewDrone", path="/Game/Blueprints"):
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    
    # 1. Setup Factory
    factory = unreal.BlueprintFactory()
    # Load the C++ class we want to inherit from
    # Note: "_C" suffix is ONLY for Blueprint classes. C++ classes use just the path.
    parent_class = unreal.load_class(None, "/Script/FlightProject.FlightAIPawn")
    factory.set_editor_property("ParentClass", parent_class)

    # 2. Create Asset
    # Check if exists first...
    if not unreal.EditorAssetLibrary.does_asset_exist(f"{path}/{name}"):
        new_bp = asset_tools.create_asset(name, path, unreal.Blueprint, factory)
        unreal.EditorAssetLibrary.save_loaded_asset(new_bp)
        unreal.log(f"Created {name}")
    else:
        unreal.log_warning(f"{name} already exists.")
```
