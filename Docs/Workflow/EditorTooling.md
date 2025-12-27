# FlightProject Editor Tooling & Settings

## Goals
- Keep the UE5 editor readable on high-DPI Linux displays while preserving screen real estate.
- Document project-specific defaults so every developer starts with the same streamlined layout.
- Capture workflows for Mass/StateTree tooling that the base templates don’t expose.

## Baseline Settings (DefaultEditorPerProjectUserSettings.ini)
- `ApplicationScale=0.75`: shrinks the Slate UI to a comfortable size on 1440p/4K monitors.
- `bUseSmallToolBarIcons=True`: compacts toolbar buttons to fit more actions without scrolling.
- `LogFontSize=9`: keeps the Output Log dense enough to scan large Mass traces.
- Word wrap off in Output Log to avoid breaking long Mass or GPU debug lines.

## Recommended Editor Layout
1. **Persistent Tabs**
   - Output Log (docked bottom) for Mass/StateTree debug output.
   - Message Log + Mass Debugger (right-side stack) to inspect processors and fragments.
   - World Partition tab pinned when working in streaming levels.
2. **Viewport Setup**
   - Single perspective viewport by default; enable a floating second viewport when tuning flight cameras.
   - Show FPS / Stat Unit for quick performance snapshots.
3. **Content Browser**
   - Docked left with filters saved for `/Game/Mass/Processors`, `/Game/AI/BehaviorTrees`, `/Game/Data/Vehicles`.
   - Create collections for “Flight Assets” and “Debug Utilities” to group blueprints and data assets.

## Plugin Tooling Highlights
- **Mass Debugger**: Window → Mass Debugger. Inspect entity composition, processor timings, and execution order. Shortcut bound to `Mass_Debugger_Toggle` (Shift+F2) per `DefaultInput.ini`.
- **StateTree Editor**: Enables authoring height-aware AI logic. Turn on “Auto Bind Context” in Project Settings → StateTree for faster setup.
- **Niagara Debuggers**: Window → Niagara → Debugger for GPU script profiling; use when tuning perception/effects compute shaders.

## Useful Editor Preferences (Per-User)
- General → Appearance → Use Small Tool Bar Icons (matches project default).
- Enable “Allow Explicit Console Commands” for quick Mass command entry.
- Level Editor → Play → Auto Restore Windows to reopen Mass debugger after PIE.
- Customize keyboard shortcuts (e.g., toggle Output Log, Mass debugger) under Editor Preferences → Shortcuts.

## Workflow Tips
- Capture Mass profiler snapshots often (`Mass_Profiler_Snapshot`) and archive under `/Saved/MassSnapshots` for regression comparison.
- Use `stat gpu` + RenderDoc plugin when iterating on RDG compute shaders.
- Validate World Partition streaming (World Partition → Validate) after adjusting flight corridors or HLOD tiles.

## Extending Tooling
- Store Editor Utility Widgets in `/Content/EditorUtilities` for tasks like batch drone spawning or Mass cleanup.
- Promote stable utilities into a project plugin when they become core to workflows.

## Quick Reference
- Project config: `Config/DefaultEditorPerProjectUserSettings.ini`
- Supporting docs: `Docs/RebuildAndOverview.md`, `Docs/ProjectSpecification.md`
- Update this file whenever default editor settings or workflows change.
