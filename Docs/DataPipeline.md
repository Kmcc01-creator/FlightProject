# Data Pipeline (CSV)

## Overview
FlightProject reads gameplay tuning data from plain-text CSV files that live under `Content/Data`. At startup the `UFlightDataSubsystem` (game instance subsystem) imports the sheets into transient data tables and pushes the resulting rows into runtime systems (lighting, navigation lighting, drone autopilot). This keeps knobs in spreadsheets instead of hard-coded constants and allows designers to iterate without recompiling C++.

```
Content/Data/*.csv  -->  FlightProjectDeveloperSettings (paths + row IDs)
                       --> FlightDataSubsystem
                               --> Lighting setup, autonomous drones, pawn nav lights
```

## Current Tables
File | Row Struct | Description
---- | ---------- | -----------
`Content/Data/FlightLightingConfig.csv` | `FFlightLightingConfigRow` | Directional-light intensity, color, rotation and sky light parameters for the “night ops” demo.
`Content/Data/FlightAutopilotConfig.csv` | `FFlightAutopilotConfigRow` | Spline radius/altitude defaults, drone count/speed, navigation light color, radius and falloff options.
`Content/Data/FlightSpatialLayout.csv` | `FFlightSpatialLayoutRow` | Spatial layout authored for the free-form “NightRange” test level (nav probes, obstacles, landmarks, navigation lighting).

Each CSV must contain a `Name` column whose value matches the `LightingConfigRow` / `AutopilotConfigRow` in `Config/DefaultGame.ini`. Additional rows can be appended (e.g. `SunsetDemo`, `StormyNight`) and switched by updating the developer settings.

## Configuration
- Paths: `Config/DefaultGame.ini` → `[ /Script/FlightProject.FlightProjectDeveloperSettings ]` stores content-relative CSV paths (`Data/...csv`) and row names to load.
- Runtime: `FlightDataSubsystem` reads the configured files when the game instance initializes. Missing files/rows log errors (see `FlightDataSubsystem.cpp`).
- Consumers:
  - `FlightGameMode` pulls lighting and autopilot rows to set night lighting, spawn the waypoint spline, and seed drones.
  - `FlightVehiclePawn` applies navigation-light intensity/radius/color on `BeginPlay`.
  - `FlightAIPawn` sets autopilot speed and nav light overrides per drone.
  - `FlightWaypointPath` rebuilds its loop with CSV-provided radius/altitude.
  - `FlightSpatialLayoutDirector` spawns developer-art geometry (via `AFlightSpatialTestEntity`) for spatial testing, nav probes, and illuminated landmarks.

## Known Pitfalls
1. **Packaging / Cooking** – CSVs are loaded from `FPaths::ProjectContentDir()`. In cooked builds loose files are excluded unless explicitly cooked. Make sure `Content/Data` is added to *Project Settings → Packaging → Additional Asset Directories to Cook* (or another equivalent rule). Otherwise, shipping builds will fall back to the hard-coded defaults and log errors.
2. **Import errors hidden in array** – `UDataTable::CreateTableFromCSVString` returns an array of error strings. Check the log for each entry; the subsystem logs and aborts if the array is non-empty.
3. **Row mismatch** – An empty or misspelled row name results in no data being applied and hard-coded defaults being used. The subsystem warns but gameplay will revert to legacy values, so QA should watch for warning spam.
4. **Designer edits outside Content** – Because we read from `Content/Data`, storing CSVs in other directories requires updating the developer settings. Keep all spreadsheet exports under that folder so the cook picks them up.
5. **Localization/decimal formats** – CSV parsing assumes `.` as decimal separator. Everyone should export sheets using US locale or pure UTF‑8 CSV to avoid comma-as-decimal issues.

## Extending the Pipeline
- **Flight Models**: migrate lift/drag/turn-rate coefficients (currently inside `FlightMovementComponent`) into a `FlightVehicleConfig.csv`.
- **Mass/AI Parameters**: expose altitude thresholds, Mass phase timings, or future perception weights to spreadsheets.
- **Behavior Trees / Data Assets**: the CSV approach is ideal for simple numeric tuning. For richer data you can swap in `UDataTable` assets authored in-editor (follow the same developer-settings pattern).
- **Spatial Layout Variants**: add additional scenarios (e.g. `StormRange`, `IndustrialYard`) to `FlightSpatialLayout.csv` and reference them by swapping `SpatialLayoutScenario` in developer settings.
- **Load-time Validation**: add schema checks (min/max ranges, required columns) in `FlightDataSubsystem` once we have more gameplay systems reading from the tables.

Whenever you add a new sheet:
1. Create a `USTRUCT : FTableRowBase` in `FlightDataTypes.h`.
2. Extend `UFlightProjectDeveloperSettings` with path/row entries.
3. Teach `FlightDataSubsystem` to load and cache the struct.
4. Apply the values where relevant (game mode, pawn, Mass processors, etc.).
5. Update this document so the pipeline stays discoverable.
