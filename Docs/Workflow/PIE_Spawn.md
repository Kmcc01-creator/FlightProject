# PIE Spawn Analysis and Tracing Reference

## Scope
This document captures findings from the PIE trace artifact:

- `/home/kelly/Unreal/Projects/FlightProject/Saved/Flight/Observations/pie_entity_trace_20260307_200900.json`
- Generated at `2026-03-07T20:09:00.005Z`
- `worldName=PersistentFlightTest`, `worldType=PIE`, `eventCount=19`

Goal: determine where startup spawn events originate, and document how PIE tracing works in FlightProject.

## Key Findings from the Trace

### 1) All captured events are runtime startup spawns
- Every event is `eventType=SpawnedActor`.
- No `InitialActor` rows are present in this artifact.
- `wasLoaded=false` for all rows.
- All events occur at startup (`worldSeconds` from `0` to `0.008333...`).
- All rows point at PIE persistent level path:
  - `/Game/Maps/UEDPIE_0_PersistentFlightTest.PersistentFlightTest:PersistentLevel`

Interpretation: these are not map-authored actors being loaded from content. They are framework/subsystem boot actors created during PIE initialization.

### 2) Source distribution
- `SpawnedAtRuntime`: 14
- `SpawnedWithOwner`: 5

Module distribution:
- `/Script/Engine`: 12
- `/Script/FlightProject`: 1
- `/Script/GameplayDebugger`: 2
- `/Script/MassGameplayDebug`: 1
- `/Script/MassRepresentation`: 1
- `/Script/NavigationSystem`: 1
- `/Script/SmartObjectsModule`: 1

### 3) Origins by actor class

Likely expected engine/session bootstrap actors:
- `/Script/FlightProject.FlightGameMode` (`FlightGameMode_0`)
- `/Script/Engine.GameSession` (`GameSession_0`)
- `/Script/Engine.GameStateBase` (`GameStateBase_0`)
- `/Script/Engine.GameNetworkManager` (`GameNetworkManager_0`)
- `/Script/Engine.PlayerController` (`PlayerController_0`)
- `/Script/Engine.PlayerState` (`PlayerState_0`)
- `/Script/Engine.PlayerCameraManager` (`PlayerCameraManager_0`)
- `/Script/Engine.DefaultPawn` (`DefaultPawn_0`)
- `/Script/Engine.SpectatorPawn` (`SpectatorPawn_0`)
- `/Script/Engine.HUD` (`HUD_0`)
- `/Script/NavigationSystem.AbstractNavData` (`AbstractNavData_0`)
- `/Script/Engine.DefaultPhysicsVolume` (`DefaultPhysicsVolume_0`)
- `/Script/Engine.ParticleEventManager` (`ParticleEventManager_0`)

Debug/auxiliary subsystems (often surprising but expected when enabled):
- `/Script/GameplayDebugger.GameplayDebuggerPlayerManager`
- `/Script/GameplayDebugger.GameplayDebuggerCategoryReplicator`
- `/Script/MassRepresentation.MassVisualizer`
- `/Script/MassGameplayDebug.MassDebugVisualizer`
- `/Script/SmartObjectsModule.SmartObjectSubsystemRenderingActor`
- `/Script/Engine.Actor` with name `ChaosDebugDrawActor`

### 4) Why these appear in this project
- Gameplay debugger is enabled in config (`bEnableDebuggerPlugin=True`), which triggers gameplay debugger runtime support and manager/replicator spawning.
- Mass and SmartObjects plugins can spawn helper visualization/debug actors at runtime.
- Chaos debug draw can create a transient actor named `ChaosDebugDrawActor`.
- Engine gameplay bootstrap spawns controller/session/state/pawn/HUD flow during player start.

## Important FlightProject-specific observation
In `AFlightGameMode::StartPlay`, the Gauntlet branch sets:

- `DefaultPawnClass = nullptr;`
- `HUDClass = nullptr;`

However this happens in `StartPlay`, while some player/bootstrap spawning already occurs in engine startup flow around login/restart.

Result: setting those in `StartPlay` is too late to guarantee no `DefaultPawn`/`HUD` spawn in all cases.

## PIE Tracing Functionality (How it works)

### C++ capture layer
`UFlightPIEObservationSubsystem` (`Source/FlightProject/Private/Diagnostics/FlightPIEObservationSubsystem.cpp`)

- World type gate: only active for `EWorldType::PIE`.
- On initialize:
  - checks `Flight.Trace.PIEActors.Enabled`
  - starts timer
  - binds `AddOnActorSpawnedHandler`
  - captures current actors via iterator (`InitialActor` events)
- On each spawn:
  - records actor name/class/module/owner/instigator/level/package
  - annotates `sourceHint` (`LoadedFromMapOrAsset`, `SpawnedWithOwner`, `SpawnedAtRuntime`)
- On deinitialize:
  - removes spawn handler
  - if auto-export enabled, writes JSON to:
    - `Saved/Flight/Observations/pie_entity_trace_<timestamp>.json`

### CVar controls
- `Flight.Trace.PIEActors.Enabled` (default `1`)
- `Flight.Trace.PIEActors.AutoExport` (default `1`)
- `Flight.Trace.PIEActors.LogEachEvent` (default `0`)

### C++ scripting bridge
`UFlightScriptingLibrary` provides:
- `ExportPIEEntityTrace(WorldContextObject, RelativeOutputPath)`
- `GetPIEEntityTraceEventCount(WorldContextObject)`

These require a valid PIE world context; editor-world context will not find the PIE subsystem.

### Python tooling layer
`Content/Python/FlightProject/PIETrace.py`

Capabilities:
- Resolves PIE world (`UnrealEditorSubsystem.get_game_world` + fallbacks)
- Arms cvars (`configure_tracing`, `arm_default_observability`)
- Manual export (`export_trace`)
- Event count query (`get_event_count`)
- Auto report on PIE close:
  - delegate mode via `EditorUtilitySubsystem.on_begin_pie/on_end_pie` when available
  - ticker fallback mode when delegates are unavailable in startup context

`Content/Python/init_unreal.py` calls:
- `PIETrace.ensure_blutility_loaded()`
- `PIETrace.arm_default_observability(auto_export=True, log_each_event=False)`

Standard workflow:
1. Launch editor.
2. Hit Play (PIE).
3. Stop PIE.
4. Read JSON path from log and inspect artifact under `Saved/Flight/Observations`.

## Practical interpretation for unexpected PIE actors
If your concern is "entities we did not author in the persistent map":
- This specific trace indicates startup/system actors, mostly engine + debug/plugin helpers.
- It does not show map-loaded authored actors (`wasLoaded=true` is absent in this artifact).

If you want to reduce startup noise in traces, first targets are:
- Gameplay debugger plugin path
- Mass/SmartObject debug visualizers
- Chaos debug draw helper

## Next diagnostics to isolate project-intentional spawns
1. Record one trace with debug-oriented systems disabled and diff against this baseline.
2. Keep `LogEachEvent=0` for normal runs; enable temporarily for high-resolution timeline runs.
3. Add post-processing filters by `actorModulePath` and class allow/deny lists to separate framework noise from project-domain actors.

## Related Docs
- `Docs/Scripting/EditorAutomation.md` for startup script and Python command reference.
- `Docs/Scripting/VexSchemaValidation.md` for schema-driven VEX symbol tooling.
