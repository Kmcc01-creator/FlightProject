# GameMode And Bootstrap Boundary

This document defines the intended boundary between Unreal `GameMode` ownership and `UFlightWorldBootstrapSubsystem` ownership in FlightProject.

The short version:

- `GameMode` is a gameplay-framework policy surface
- `UFlightWorldBootstrapSubsystem` is a reusable world-preparation surface
- FlightProject should prefer the bootstrap subsystem for environment and world-service setup
- `GameMode` should stay thin and only retain work that Unreal meaningfully routes through `GameMode`

## 1. Why This Boundary Matters

If startup logic stays in `AFlightGameMode`, FlightProject becomes tightly coupled to:

- one gameplay-framework entry path
- one mode asset hierarchy
- one assumption about how playable worlds start

That works for a prototype, but it fights the project’s direction:

- script-centric workflows
- reusable world orchestration
- editor/runtime repair and ensure flows
- future orchestration-driven startup

The bootstrap subsystem is the better semantic home for reusable world setup because it is:

- world-scoped
- callable from scripts and subsystems
- less coupled to pawn/HUD/player-state rules
- closer to the actual service boundary

## 2. What `GameMode` Is For

In Unreal, `GameMode` is still the right place for:

- default gameplay framework classes
  - default pawn
  - HUD
  - player controller
  - game state
  - spectator/respawn rules
- player/session rules
  - login/start rules
  - spawn/restart policy
  - authority-only gameplay mode decisions
- mode selection
  - which startup profile or runtime mode applies to this world
  - whether the world is a normal play session, Gauntlet path, sandbox path, or another special path
- Unreal-owned framework hooks
  - `StartPlay`
  - `HandleStartingNewPlayer`
  - `RestartPlayer`
  - match/session policy hooks

This is mostly policy and framework routing, not world preparation.

## 3. What `UFlightWorldBootstrapSubsystem` Is For

`UFlightWorldBootstrapSubsystem` should own reusable environment and service initialization for a world.

That includes:

- ensuring world services are ready to operate
- resuming or priming shared runtime systems
- applying environment setup from data/config
- ensuring world helper actors/directors exist
- rebuilding world-authored support state when needed
- exposing re-runnable bootstrap APIs for scripts, PIE tools, level transitions, and orchestration

Current examples already in the subsystem:

- Mass simulation resume
- lighting/environment ensure
- spatial layout director ensure and rebuild

Long-term examples that also fit here:

- orchestration preflight hooks
- world-level validation/repair entrypoints
- map activation hooks from a level-loader flow

## 4. The Practical Split For FlightProject

### `AFlightGameMode` should own

- selecting the startup branch
  - Gauntlet GPU swarm path
  - default sandbox/bootstrap path
  - any future mission/test/sim profile path
- any truly mode-specific gameplay rules
- Unreal framework defaults declared by the mode asset/class
- delegating into bootstrap/orchestration/subsystems

### `UFlightWorldBootstrapSubsystem` should own

- world environment preparation
- reusable bootstrap stages
- idempotent ensure/rebuild flows
- startup work that should also be callable outside `StartPlay`
- world service sequencing that is not inherently player/match specific

### `UFlightOrchestrationSubsystem` should eventually own

- service visibility
- participant visibility
- binding and plan rebuilds
- startup coordination between bootstrap, spawner, and behavior systems

That gives a three-part shape:

- `GameMode` picks the path
- `Bootstrap` prepares the world
- `Orchestration` coordinates the execution surfaces

## 5. What Should Move Out Of `GameMode`

If a task can be described as:

- "ensure the world has X"
- "apply config to world services"
- "rebuild support actors"
- "resume or repair shared runtime state"
- "can be triggered by scripts, level transitions, or editor/runtime tools"

then it should generally not live in `GameMode`.

That work belongs in:

- `UFlightWorldBootstrapSubsystem`
- `UFlightOrchestrationSubsystem`
- a domain subsystem if the concern is domain-owned

## 6. What Must Stay In `GameMode`

If a task depends on Unreal’s gameplay framework semantics, it should stay in `GameMode`.

Examples:

- choosing or overriding default pawn/HUD/controller classes
- authority-only player-start rules
- mode-specific session policy
- deciding that a particular world should bypass normal bootstrap and use a special startup profile

This is the main rule:

`GameMode` decides which startup policy applies.
It should not directly implement the reusable bootstrap stages for that policy.

## 7. GameMode Asset Boundary

The user-facing question included "what is defined/required by game mode assets?"

For FlightProject, the recommended answer is:

GameMode assets/classes should define:

- framework defaults
- mode-specific policy flags
- which startup profile asset or config is selected

They should not become the place where the actual environment and world-service bootstrap logic lives.

Recommended examples of acceptable GameMode-owned configuration:

- `StartupProfileAsset`
- `StartupProfile`
- `GauntletGpuSwarmEntityCount`
- `bSpawnDefaultPlayerPawn`
- `bEnableMissionFlow`

Recommended examples of bootstrap-owned configuration:

- lighting config resolution
- Mass resume policy
- layout rebuild policy
- world-director ensure logic
- orchestration preflight and refresh

## 8. Design Rule

When deciding whether new startup logic belongs in `GameMode` or bootstrap, ask:

1. Is this Unreal gameplay-framework policy, or reusable world preparation?
2. Would we want to call this from scripts, editor automation, level transitions, or non-default startup flows?
3. Does this logic care about players/match/session rules, or just the world being ready?

If it is reusable world preparation, it belongs in the bootstrap subsystem.

## 9. Recommended Near-Term FlightProject Rule

For the current project phase:

- keep `AFlightGameMode::StartPlay()` as a thin trigger
- keep the explicit startup-profile branch decision there for now
- prefer `StartupProfileAsset` over ad hoc map/tag inference
- keep environment/layout/Mass bootstrap in `UFlightWorldBootstrapSubsystem`
- move more coordination into `UFlightOrchestrationSubsystem`
- avoid adding new direct world-setup logic to `GameMode`

That gives a clear bifurcation without forcing a risky framework rewrite.
