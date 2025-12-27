# Composable Level Loader (UE5) – Concept Notes

_Drafted: October 21, 2025_

## Goals
- Decouple level loading from `DefaultEngine.ini` hardcoding so missions/tools can trigger transitions programmatically.
- Offer a reusable loading “experience” that supports async streaming (level instances, data layers) and can be driven from C++ or Blueprints.
- Expose scripting hooks (C++, Blueprint, Python, Editor Utility Widgets) so agents can prototype or automate level sequences without recompiling.
- Keep designers aligned: a lightweight loading map + configurable UI overlay + telemetry to measure load times.

## Runtime architecture

### 1. `UFlightLevelLoaderSubsystem` (GameInstance subsystem)
Responsibilities:
- Maintain the canonical list of loadable maps (`TArray<FFlightLevelDescriptor>`). Populate from data assets or CSV.
- Provide API surface:
  - `RequestTransition(LevelId, const FFlightLevelLoadParams&)`
  - `PreloadStreamingLevel(LevelId)`
  - `OnLevelWillUnload` / `OnLevelDidLoad` delegates (BlueprintAssignable).
- Orchestrate loading flow:
  1. Spawn/activate loading screen map (persistent) or widget overlay.
  2. Kick async load using `UGameplayStatics::OpenLevel` or `ULevelStreamingDynamic`.
  3. Track progress using `FLatentActionInfo` or `FStreamableHandle`.
  4. Broadcast metrics (start/end timestamps, bytes streamed).

Implementation sketch:
```cpp
struct FFlightLevelDescriptor
{
    FName LevelId;
    FSoftObjectPath MapAsset;
    FText DisplayName;
    TArray<FName> RequiredDataLayers;
    bool bIsStreamingLevel = false;
};
```

### 2. Loading experience assets
- **Loading Map (`/Game/Maps/Loading`)** – Minimal persistent level containing only the camera, lighting, and a widget component.
- **Widget (`WBP_LevelLoadingOverlay`)** – Consumes loader subsystem events, shows progress bar, mission text, tips. Expose BP events to customize theming per mission.
- **Sound/FX hooks** – Optional Niagara/Audio to keep the scene lively during loads.

### 3. Mission integration
- Mission scripting (GOAP or otherwise) asks `UFlightLevelLoaderSubsystem` for transitions.
- When the destination map requires nav graph or swarm bootstrapping, the loader can coordinate with `UFlightWorldBootstrapSubsystem` to pre-run environment setup before gameplay resumes.

## Editor & scripting workflows

### Python helpers
- Script to register maps:
```python
import unreal
loader = unreal.FlightLevelLoaderBlueprintLibrary.get_loader()
loader.register_map("NightRange", "/Game/Maps/PersistentFlightTest")
```
- Batch convert: read a CSV (`Content/Data/FlightLevels.csv`) and populate descriptors.

### Editor Utility Widget
- Create `EUW_LevelSequenceBuilder`:
  - List all registered maps (query subsystem via `EditorSubsystem` analog).
  - Let designers compose sequences (e.g., Intro → NightRange → Training) and save to data assets.
  - Provide one-click “Play From Start” button that runs PIE and steps through loader transitions.

### Automation script (`Scripts/run_level_sequence.sh`)
- Wraps `UnrealEditor` with `-ExecutePythonScript` to:
  1. Load the level sequence asset.
  2. Programmatically call loader subsystem to traverse levels.
  3. Log timings to `Saved/Logs/LevelLoader`.

## Data-driven configuration

Consider adding `Content/Data/FlightLevels.csv`:
```
LevelId,MapAsset,DisplayName,RequiredDataLayers,LoadingTip
NightRange,/Game/Maps/PersistentFlightTest,"Night Test Range","NavMesh,Lighting","Stay above cloud deck for autopilot accuracy."
PrototypeArena,/Game/Maps/PrototypeArena,"Prototype Arena","", "Check new nav graph overlays."
```

During startup `UFlightLevelLoaderSubsystem` can parse this data via `UFlightDataSubsystem` (future shared loader utility).

## Roadmap
1. **Prototype** – Implement `UFlightLevelLoaderSubsystem` with basic OpenLevel support and a placeholder widget. Add console commands (`Flight.Levels.List`, `Flight.Levels.Load <LevelId>`).
2. **Loading map** – Author `/Game/Maps/Loading` with a widget component pointing to `WBP_LevelLoadingOverlay`. Update `DefaultEngine.ini` transition map after validation.
3. **Data asset** – Create `UFlightLevelRegistry` data asset storing descriptors; allow CSV import/export for automation.
4. **Editor tooling** – Ship `EUW_LevelSequenceBuilder` + sample Python script for batch operations.
5. **Mission hooks** – Integrate with mission scripting (GOAP) so objectives can trigger level transitions; log metrics via `UFlightAnalyticsSubsystem`.
6. **Bootstrap integration** – Ensure the loader notifies `UFlightWorldBootstrapSubsystem` and `UFlightSwarmSpawnerSubsystem` (see `Docs/StartPlayDecomposition.md`) after each transition so lighting/layout/drone orchestration re-run automatically.

## Open questions
- Should level transitions be purely map swaps or support World Partition data layer toggles within a persistent map? (Subsystem can abstract both.)
- Do we want seamless travel (server-client) support now, or after single-player flow matures?
- How far should we go integrating with the hegetic interface (e.g., show nav graph preview in loading screen)? Good stretch goal once loader MVP is stable.

This plan keeps the loader modular, scriptable, and ready for future automation without locking us into StartPlay logic.
