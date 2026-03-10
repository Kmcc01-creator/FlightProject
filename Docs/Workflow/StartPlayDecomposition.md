# StartPlay Decomposition Plan

_Drafted: October 21, 2025 — Initial implementation landed the same day._

Status: historical implementation note.
Use `Docs/Architecture/GameModeBootstrapBoundary.md`, `Docs/Architecture/WorldExecutionModel.md`, and `Docs/Workflow/OrchestrationImplementationPlan.md` for the current startup/bootstrap direction.

## Why change?
Originally `AFlightGameMode::StartPlay` performed four heavyweight responsibilities in sequence:

1. Resume Mass simulation (`InitializeMassRuntime`).
2. Author night-time lighting based on CSV data (`SetupNightEnvironment`).
3. Ensure a spatial layout director exists (`BuildSpatialTestRange`).
4. Spawn the entire autonomous drone swarm (`SpawnAutonomousFlights` and helpers).

Keeping all of this logic in the game mode makes it difficult to:

- Share bootstrap behavior across future game modes, editor previews, or PIE workflows.
- Test lighting, layout, or swarm subsystems in isolation (everything is tightly coupled to StartPlay).
- Re-run individual bootstrap stages at runtime (e.g., respawn swarms after mission reset).
- Profile and parallelize work; StartPlay is long and opaque.

This plan decomposes StartPlay into modular subsystems/components that orchestrate their own lifecycles and expose clean APIs for future tooling.

For the explicit ownership split between `AFlightGameMode` and `UFlightWorldBootstrapSubsystem`, see `Docs/Architecture/GameModeBootstrapBoundary.md`.

## Target architecture
```
               ┌─────────────────────────────┐
               │ UFlightWorldBootstrapSubsystem │
               ├─────────────────────────────┤
               │ - ResumeMassSimulation()      │
               │ - EnsureNightEnvironment()    │
               │ - EnsureSpatialLayout()       │
               └──────────────┬───────────────┘
                              │ (OnWorldBeginPlay)
                              ▼
               ┌─────────────────────────────┐
               │ UFlightSwarmSpawnerSubsystem│
               ├─────────────────────────────┤
               │ - CacheAutopilotProfile()   │
               │ - EnsureWaypointPath()      │
               │ - HarvestSpawnAnchors()     │
               │ - SpawnOrRespawnDrones()    │
               └─────────────────────────────┘
```

- **`UFlightWorldBootstrapSubsystem` (world subsystem)** handles all environment/bootstrap work that should happen exactly once per world. It binds to `FWorldDelegates::OnWorldBeginPlay` (and can expose manual `RebuildEnvironment()` for runtime refresh).
- **`UFlightSwarmSpawnerSubsystem` (world subsystem)** owns drone swarm orchestration. It depends on the data subsystem, nav graph hub, and future mission scripts. The subsystem can be triggered on world begin play or by other systems (e.g., mission reset, debug console).
- `AFlightGameMode::StartPlay` shrinks to selecting an explicit startup profile asset/config, delegating to these subsystems, and performing any mode-specific work.

### Current Implementation Note

The current codebase has completed the decomposition of responsibilities, but the main trigger surface is still `AFlightGameMode::StartPlay()`, not a world-begin-play delegate owned by the bootstrap subsystem.

For the planned migration of that trigger into a dedicated coordination layer, see `Docs/Workflow/OrchestrationImplementationPlan.md`.

In practice, the ordering is:

1. subsystem initialization
2. `AFlightGameMode::StartPlay()`
3. `UFlightWorldBootstrapSubsystem::RunBootstrap()`
4. orchestration rebuild before spawn (default sandbox path)
5. swarm spawn or GPU-swarm initialization path
6. orchestration rebuild after spawn (default sandbox path)

That still fits the decomposition goal, but it is important not to confuse the target architecture with the exact current trigger path.

## Subsystem responsibilities

### UFlightWorldBootstrapSubsystem
- **Mass Simulation** – Resume `UMassSimulationSubsystem` if present; optionally respect future configuration (pause/autostart flags).
- **Lighting Setup** – Pull a lighting profile from `UFlightDataSubsystem`, locate or spawn directional & sky lights, apply settings, and recapture skylight.
- **Spatial Layout** – Ensure one `AFlightSpatialLayoutDirector` exists (spawn if missing) and trigger an initial `RebuildLayout`. Later iterations can subscribe to layout change events or streaming level loads.
- **Extensibility hooks** – Provide multicast delegates (`OnEnvironmentReady`, `OnLayoutReady`) so other systems (hegetic interface, nav graph visuals) can react after setup.

### UFlightSwarmSpawnerSubsystem
- **Autopilot Profile Cache** – Pull `FFlightAutopilotConfigRow` once, expose it to other systems, and watch for future runtime overrides.
- **Waypoint Path Management** – Reuse or spawn `AFlightWaypointPath`, keeping it updated if the autopilot profile changes. Long term, this subsystem can broker nav-graph-backed routes instead of splines.
- **Anchor Harvesting** – Gather `AFlightSpawnSwarmAnchor` actors (with future support for registration callbacks so streaming levels don’t require full-world iteration).
- **Spawn Ordering** – Translate anchor definitions into spawn orders (distance, speed overrides) or fall back to autopilot defaults.
- **Spawn Execution** – Spawn `AFlightAIPawn` instances, apply autopilot settings, and emit analytics (count, anchor usage, timings). Expose `RespawnAllDrones()` for mission resets.
- **Integration points** – Raise `OnSwarmSpawned` events so mission scripts or hegetic visualizations can link to the new drones.

## Updated StartPlay flow
```cpp
void AFlightGameMode::StartPlay()
{
    Super::StartPlay();

    UE_LOG(LogFlightGameMode, Log, TEXT("StartPlay for map '%s'"), *GetWorld()->GetMapName());

    UFlightScriptingLibrary::RunBootstrap(this);
    UFlightScriptingLibrary::RebuildOrchestration(this);
    UFlightScriptingLibrary::SpawnInitialSwarm(this);
    UFlightScriptingLibrary::RebuildOrchestration(this);
}
```

The current path is still `GameMode`-triggered rather than delegate-owned, but the reusable work now sits behind bootstrap/orchestration surfaces instead of being embedded directly in `StartPlay()`.

## Migration steps

1. **Introduce `UFlightWorldBootstrapSubsystem`** ✅ _Done Oct 21, 2025_
   - Move `InitializeMassRuntime`, `SetupNightEnvironment`, and `BuildSpatialTestRange` logic into private methods.
   - Add `ExecuteBootstrap` and optionally split into exposed `ResumeMassSimulation`, `RebuildEnvironment`, `EnsureSpatialLayout`.
   - Update `AFlightGameMode::StartPlay` to call the subsystem (feature gated for now).

2. **Introduce `UFlightSwarmSpawnerSubsystem`** ✅ _Done Oct 21, 2025_
   - Relocate `SpawnAutonomousFlights` (and helper `FindOrCreateWaypointPath`) into the subsystem.
   - Expose `SpawnInitialSwarm` and `RespawnSwarm` convenience functions.
   - Emit analytics delegates (`OnDroneSpawned`, `OnSwarmSpawned`).

3. **Add registration hooks**
   - Let `AFlightSpawnSwarmAnchor` register/unregister with the swarm subsystem so future streaming levels avoid world iteration.
   - Optionally let `AFlightNavBuoyRegion` signal layout updates to the world bootstrap subsystem.

4. **Trim `AFlightGameMode`** ✅ _Done Oct 21, 2025_
   - Replace inline logic with subsystem calls.
   - Keep StartPlay logging and any mode-specific behavior (e.g., mission scripting).

5. **Follow-up opportunities**
   - Cache autopilot profiles in a shared `UFlightAutopilotProfile` object used by both player and AI pawns.
   - Move lighting setup into a reusable `AFlightEnvironmentDirector` actor if designers want level-authored overrides.
   - Allow mission scripts to request swarm respawn or layout rebuilds via subsystem APIs.

## Open questions
- Should lighting setup remain entirely data-driven, or do we want per-map overrides exposed via actors/LevelInstance metadata?
- Do we want the swarm subsystem to own nav-graph route generation immediately, or wait for the nav hub to support path queries?
- How should these subsystems behave in the editor (construction scripts, PIE simulations) versus cooked builds?
- How should the bootstrap behave when levels transition via `UFlightLevelLoaderSubsystem` (see `Docs/LevelLoaderComposable.md`)? The loader should invoke the bootstrap subsystem when a new playable map is activated so lighting/layout/swarms rebuild automatically.

Answering these will shape implementation details, but this decomposition keeps StartPlay slim and gives us clear, testable units for environment and swarm orchestration.
