# Data Pipeline (CSV)

## Overview
FlightProject reads gameplay tuning data from plain-text CSV files that live under `Content/Data`. At startup the `UFlightDataSubsystem` (game instance subsystem) imports the sheets into transient data tables and pushes the resulting rows into runtime systems (lighting, navigation lighting, drone autopilot).

```
Content/Data/*.csv  -->  FlightProjectDeveloperSettings (paths + row IDs)
                       --> FlightDataSubsystem
                               --> Lighting setup (Bootstrap), Mass Spawning (SwarmEncounter), pawn nav lights
```

## Current Tables
File | Row Struct | Description
---- | ---------- | -----------
`Content/Data/FlightLightingConfig.csv` | `FFlightLightingConfigRow` | Directional-light intensity, color, rotation and sky light parameters.
`Content/Data/FlightAutopilotConfig.csv` | `FFlightAutopilotConfigRow` | Spline radius/altitude defaults, drone count/speed, navigation light color.
`Content/Data/FlightSpatialLayout.csv` | `FFlightSpatialLayoutRow` | Spatial layout authored for the free-form "NightRange" test level.

## Configuration
- Paths: `Config/DefaultGame.ini` → `[ /Script/FlightProject.FlightProjectDeveloperSettings ]`.
- Runtime: `FlightDataSubsystem` reads the configured files when the game instance initializes.

## Consumers
- **`UFlightWorldBootstrapSubsystem`**: Pulls lighting config to setup Day/Night cycle.
- **`UFlightSwarmSpawnerSubsystem` (Plugin)**: Pulls autopilot config (Speed, Count) to seed Mass Entity spawning.
- **`FlightWaypointPath`**: Rebuilds its loop with CSV-provided radius/altitude.
- **`FlightSpatialLayoutDirector`**: Spawns developer-art geometry.
- **`FlightVehiclePawn`**: Applies navigation-light intensity/radius/color on `BeginPlay`.

## Known Pitfalls
1. **Cooking**: CSVs are loaded from `FPaths::ProjectContentDir()`. Ensure `/Game/Data` is in `+DirectoriesToAlwaysCook`.
2. **Row Mismatch**: Check `FlightProject.log` for warnings about missing rows; defaults will be used if keys are invalid.
3. **Locale**: Use US locale (dot decimal separator) for CSV exports.

## Extending the Pipeline
Whenever you add a new sheet:
1. Create a `USTRUCT : FTableRowBase` in `FlightDataTypes.h`.
2. Extend `UFlightProjectDeveloperSettings` with path/row entries.
3. Teach `FlightDataSubsystem` to load and cache the struct.
4. Apply the values where relevant (e.g., Mass Processors).