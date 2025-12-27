# Map Authoring Workflow

_Last updated: October 10, 2025_

This guide captures how we work on FlightProject maps, what currently ships in `Content/Maps`, and options for creating or iterating on levels via the Unreal Editor or scripting/automation. The goal is to make it easy for agents and developers to iterate on environments without breaking packaging or world-partition data.

## Current Maps

| Asset | Purpose | Notes |
| --- | --- | --- |
| `/Game/Maps/PersistentFlightTest` (`Content/Maps/PersistentFlightTest.umap`) | Night-range sandbox used for gameplay, staging, and (temporarily) as the transition/loading map | Contains the spatial layout director, scripted lighting setup, and serves as `GameDefaultMap` & `TransitionMap` per `Config/DefaultEngine.ini`. Consider creating a lightweight loading map to shorten seamless-travel transitions. |

## Preferred Authoring Path (Editor)

1. Launch the editor with `./Scripts/run_editor.sh -Log`.
2. Enable World Partition tools (already active via project plugins) if you plan to work on large streaming levels.
3. Use the `PersistentFlightTest` map as a template:
   - Duplicate it (`PersistentFlightTest_Copy`) when experimenting to avoid overwriting shared assets.
   - Update World Settings (Game Mode, default Pawn/Controller) to match project defaults.
4. Save new maps under `Content/Maps/<YourMap>.umap`. Keep naming consistent (`VerbNoun`) and avoid spaces.
5. Update `Config/DefaultEngine.ini` if the new map becomes the default or transition level. Commit both the `.umap` and associated `.umap.meta`.

**Pain points to watch:**
- World Partition validation (`Window → World Partition → Validate`) is slow on large levels—run it before committing streaming changes.
- Lighting rebuilds for night scenes can be heavy; use GPU Lightmass or defer rebuilds until near-final. Document any manual lighting tweaks in `Docs/EditorTooling.md`.
- Large map saves can touch many packages. Run `git status` after saving to double-check only expected files changed.

## Scripted / Automated Options

While the editor is the fastest way to design playable spaces, we can bootstrap or convert maps using scripting when automation helps:

### Python (Editor Utility)
Use Unreal’s Python API for batch operations.
```python
import unreal

level_lib = unreal.EditorLevelLibrary()
new_map = level_lib.new_level("/Game/Maps/Prototype_Flat")
level_lib.save_current_level()
```
Run scripts via the UE Python console or `UnrealEditor -ExecutePythonScript=<script.py>`. Ideal for generating blank templates, placing test actors, or validating naming conventions.

### Commandlets
Headless workflows can use Unreal’s commandlets through `./Scripts/run_editor.sh -Run=<Commandlet>` or the standalone executable:
```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/FlightProject.uproject" \
  -run=WorldPartitionConvertCommandlet -source=/Game/Maps/PersistentFlightTest \
  -dest=/Game/Maps/PersistentFlightTest_WPValidate
```
Useful for resaving packages, validating World Partition data, or baking HLODs without opening the editor. These tools require the engine tree to be writeable wherever they output derived data.

### Automation Scripts
Add map-specific automation in `Scripts/` if repeatability is key (for example, a script that duplicates the sandbox map, swaps CSV references, and stages a build). Source `env_common.sh` and call Unreal commandlets as above to keep paths consistent.

**Pain points / caveats:**
- Commandlets cannot author complex art layouts; they’re best for conversions or validation.
- Python scripts need the editor to run (even in headless mode) and still write intermediate assets—ensure the target directories are writable.
- Automation that modifies `.umap` files should run in a clean branch; conflicts are common because binary map files do not merge gracefully.

## Testing New Maps

- Use `./Scripts/run_game.sh --map /Game/Maps/<YourMap>` to stage and launch a build with your new level. The script cooks assets and copies CSV data, matching packaging behaviour.
- PIE (Play In Editor) is faster for iteration but does not guarantee packaged behaviour (especially for data-driven systems). Always run a staged build before marking a map done.
- If a new map needs CSV variations (e.g., different spatial layouts), add rows to the CSV files and change `SpatialLayoutScenario` in `DefaultGame.ini` or via developer settings assets.

## Checklist When Promoting a Map to Default/Transition

1. Map asset saved under `Content/Maps/`.
2. Update `GameDefaultMap` / `TransitionMap` in `Config/DefaultEngine.ini`.
3. Confirm lighting and world settings are correct (Game Mode, default classes, replay prefix if needed).
4. Run `./Scripts/run_game.sh` without overrides to ensure staging picks up the new default.
5. Update documentation (this file & `Docs/RebuildAndOverview.md`) indicating the new defaults.

Following this workflow keeps map development predictable whether you are iterating directly in the editor or using automation to prep bulk changes.
