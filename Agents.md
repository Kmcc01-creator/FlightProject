# FlightProject Agent Guide

_Last updated: October 10, 2025_

This quick-start is for Codex-style automation agents working inside the `FlightProject` Unreal Engine 5 workspace. It captures current project quirks, expected workflows, and open follow-ups so each run begins with the same context.

## Environment Snapshot
- **Engine root**: default to `$HOME/Unreal/UnrealEngine` unless `UE_ROOT` is exported.
- **Primary targets**: `FlightProject` (runtime) and `FlightProjectEditor` (editor); module resides under `Source/FlightProject`.
- **Maps**: `Content/Maps/PersistentFlightTest.umap` exists and currently serves as both `GameDefaultMap` and `TransitionMap` in `DefaultEngine.ini`. See `Docs/MapWorkflow.md` for iteration tips; swap to a dedicated `/Game/Maps/Loading` once designed.
- **Data pipeline**: CSV configuration in `Content/Data/*` drives lighting, autopilot, and spatial layout. `FlightDataSubsystem` loads these at runtime using paths from `UFlightProjectDeveloperSettings`.
- **Shader plans**: Developer settings point `ComputeShaderDirectory` at `/Shaders`; the module now registers the `Shaders/` directory so custom RDG/compute shaders can compile when added.
- **Config reference**: See `Docs/ConfigurationGuide.md` for a full inventory of `Config/Default*.ini` files, editing workflow, and privacy notes.

## Preferred Tooling
- `./Scripts/generate_project_files.sh [-f]` – wraps `GenerateProjectFiles.sh`.
- `./Scripts/build_targets.sh [Config]` – compiles C++ targets (defaults to Development).
- `./Scripts/run_editor.sh [args]` – launches Unreal Editor with the project file.
- `./Scripts/run_game.sh [options]` – builds (optional), cooks, stages, and launches the standalone build. Ensures shader libraries are generated; uses AutomationTool with logging fixes already in place.
- `Docs/*.md` – project build notes, data pipeline description, troubleshooting log, and editor workflow references.

## Active Follow-Ups (track before feature work)
1. **Loading map polish** – Replace the `/Engine/Maps/Entry` placeholder with a bespoke `Content/Maps/Loading.umap` when custom visuals are ready.

## Diagnostics Checklist
- **Level/streaming issues**: Verify the staged build logs; missing `TransitionMap` will emit warnings during seamless travel. Check `Saved/StagedBuilds/Linux*/` output from `run_game.sh`.
- **Shader errors**: If `LogShaderLibrary` reports missing archives, re-run `run_game.sh` without `--no-cook`. For custom shaders, confirm directory mapping once implemented.
- **Data load failures**: Watch for `FlightDataSubsystem` warnings about CSV paths or rows when running staged builds—lack of packaging rules is the usual culprit.

## Agent Workflow Tips
- Set `UE_ROOT` explicitly in scripts or command invocations if the engine lives outside the default path.
- Use `rg` for code search; the repository is large and `rg` is fastest in this sandbox.
- Respect existing logs and docs (especially `Docs/Troubleshooting.md`) before making changes; many mitigations are already scripted.
- When updating configs, prefer Unreal’s ini merge semantics and note changes in documentation for other teammates.
- Capture new issues (symptoms, root causes, mitigations) in `Docs/Troubleshooting.md` and flag open work with concrete dates for continuity.

## Communication Notes
- The project tracks changes as of October 10, 2025 (United States locale). When referencing “today”, state absolute dates to avoid confusion.
- Summaries should mention any modified files by path; tests are optional unless behavior changes.
- Avoid destructive git operations; the workspace may contain user edits.

Stick to this playbook to keep automation runs predictable and to surface project gaps quickly. Update `Agents.md` whenever workflows change or new blockers appear.
