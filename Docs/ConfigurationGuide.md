# FlightProject Configuration Guide

_Last updated: October 10, 2025_

This guide summarizes how configuration is organized in FlightProject, which systems consume each file, and the safest way to update settings without fighting Unreal Engine’s auto-generated overrides. It also highlights privacy and write-permission considerations so agents know where they can edit by hand and where the editor should do the work.

## Default Config Inventory

| File | Scope | Key Settings | Primary Consumers |
| --- | --- | --- | --- |
| `Config/DefaultEngine.ini` | Project-wide engine behaviour | Map defaults (`GameDefaultMap=/Game/Maps/PersistentFlightTest`, `TransitionMap=/Engine/Maps/Entry`), renderer flags (async compute, ray tracing, Nanite), navigation defaults, streaming/HLOD, platform RHI targets | Engine startup, renderer, World Partition runtime, Mass/AI subsystems |
| `Config/DefaultGame.ini` | Gameplay defaults | Project metadata, default pawn/controller/HUD, developer settings (`FlightProjectDeveloperSettings` paths/rows), packaging rule `+DirectoriesToAlwaysCook=(Path="/Game/Data")` | `UFlightProjectDeveloperSettings`, `FlightDataSubsystem`, packaging pipeline, network manager |
| `Config/DefaultInput.ini` | Input mappings | Enhanced Input axis/action mappings for flight controls, Mass debugger shortcuts, console key overrides | `UEnhancedInputComponent`, player controller bindings, editor PIE |
| `Config/DefaultMass.ini` | Mass simulation | Processing phases (`Flight.PrePhysics`/`DuringPhysics`/`PostPhysics`), tick rates, LOD limits | Mass runtime in `FlightGameMode`, Mass processors |
| `Config/DefaultEditorPerProjectUserSettings.ini` | Editor UX defaults shared across machines | Slate scale, toolbar icons, Output Log font, color themes | Unreal Editor UI; per-developer slate preferences when the project opens |
| `Config/DefaultEditor.ini` | Reserved for project editor rules | (Currently empty; use for future editor-only defaults that must be versioned) | Unreal Editor (future use) |

> **Note:** Unreal mirrors these files into `Saved/Config/<Platform>/` at runtime. The `Default*.ini` files in `Config/` are the authoritative, version-controlled source that survives clean checkouts.

## How Runtime Systems Use Config

- `UFlightProjectDeveloperSettings` reads its defaults from `DefaultGame.ini` and exposes them in Project Settings → Flight Project. `FlightDataSubsystem` then maps those content-relative paths to CSV data under `Content/Data`.
- `FlightGameMode` uses `DefaultEngine.ini` for default map selection and pulls developer settings to build lighting/autopilot layouts.
- `Scripts/run_game.sh` respects map settings in `DefaultEngine.ini` when staging builds, and expects the packaging rule in `DefaultGame.ini` so CSVs travel with staged output.
- Enhanced Input relies on `DefaultInput.ini` for baseline key bindings; designers can layer on runtime changes via Input Mapping Contexts without editing this file.
- Mass processors (UE 5.6 built-ins and future project code) look up phase names configured in `DefaultMass.ini`.

## Editing Workflow & Best Practices

1. **Prefer the Unreal Editor for broad settings** (Project Settings or Editor Preferences). Unreal will merge changes back into `Config/Default*.ini` as long as you edit defaults, not per-user overrides.
2. **Manual edits** are safe when:
   - Using the correct array syntax (`+Setting=`, `-Setting=`) to add/remove entries.
   - Keeping values ASCII and matching Unreal’s formatting (e.g., floats with six decimals).
   - Validating with a quick editor launch (`./Scripts/run_editor.sh -Log`) so Unreal rebuilds derived config in `Saved/Config`.
3. **Never hand-edit `Saved/Config/`** for long-term changes—those files are regenerated and ignored by Git. Move durable settings into `Config/`.
4. **Check permissions**: project scripts stage builds without touching the engine tree, so keep overrides inside the project directory (e.g., packaging rules, shader mappings). Avoid modifying `Engine/Config`.
5. **Document significant config changes** in `Docs/ConfigurationGuide.md` (this document) or the relevant system doc (DataPipeline, RebuildAndOverview) so contributors know why a value deviates from UE defaults.

## Privacy & Write-Safety Considerations

- **Avoid secrets in config.** Project files are version-controlled; don’t store access tokens, personal data, or machine-specific credentials in `Default*.ini`. Use environment variables or per-user `Saved/Config` overrides instead.
- **Dotnet/AutomationTool state** is redirected to `Saved/DotNetCli` by scripts, keeping the engine tree read-only and isolating developer machines. No personal data is written there by default.
- **Logs** live under `Saved/Logs`. They may contain usernames or hardware info; periodically clean this directory before sharing builds outside the team.
- **Per-user editor settings** (`Saved/Config/`) are not checked in and can safely hold local display or accessibility tweaks. Do not rely on them for gameplay-critical settings.

## Entry Points for Configuration Changes

- **Maps & Flow**: Update `DefaultEngine.ini` when switching default maps or transition levels. If a new transition map asset ships, point `TransitionMap` at `/Game/Maps/<YourMap>` and commit the asset.
- **Packaging**: Add directories through Project Settings → Packaging, then verify `DefaultGame.ini` records the change under `[/Script/UnrealEd.ProjectPackagingSettings]`.
- **Shader directories**: `FFlightProjectModule` registers `/Shaders`; new shader files should live under `Shaders/`. Keep developer settings (`ComputeShaderDirectory`) in sync if paths change.
- **Input**: Use Enhanced Input assets for runtime binding changes. Only adjust `DefaultInput.ini` when the project-wide default needs to shift.
- **Mass/AI**: Define new phases or tweak tick rates in `DefaultMass.ini`, then update Mass processors or documentation accordingly.

## Working with the Editor & CLI

- `Scripts/env_common.sh` centralizes `UE_ROOT` and resolves engine paths—always source it (or use the helper scripts) before running Unreal tooling to avoid permission issues.
- `Scripts/run_game.sh` handles staging/cooking; rerun without `--no-cook` after config changes that affect packaging, shader directories, or map selection.
- Use `git diff Config` after edits to confirm only intended settings changed; the editor can update multiple keys at once.

## Checklist for New Configuration Entries

1. Determine the correct `Default*.ini` file (engine, game, input, mass, editor).
2. Add the setting with the proper array syntax and comment if context is non-obvious.
3. Launch the editor or run the relevant script to validate the change.
4. Update this guide (or the domain-specific doc) describing the new setting.
5. Notify teammates if the change impacts packaging, staging, or runtime data sources.

Keeping configuration changes deliberate and documented ensures the automation scripts continue to run cleanly and that staged builds behave the same on every machine.
