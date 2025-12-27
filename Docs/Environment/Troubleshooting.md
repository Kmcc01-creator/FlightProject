# FlightProject Troubleshooting Log

This note captures the issues we encountered while getting the standalone launch pipeline working and the mitigations applied so far. Update it as we discover new quirks.

## 1. Missing Global Shader Library During Standalone Launch
- **Symptom**: `LogShaderLibrary: Error: Failed to initialize ShaderCodeLibrary... part of the Global shader library is missing` followed by a fatal dialog when launching `run_game.sh`.
- **Root Cause**: We were running the uncooked editor binary, so the required shader archives never got generated.
- **Mitigation**: Extend `run_game.sh` to call `RunUAT.sh BuildCookRun` and launch the staged `Saved/StagedBuilds/Linux*/FlightProject.sh`. This cooks content, builds shader libraries, and keeps the standalone binary stable.

## 2. `DependencyCache.bin` Read Failures
- **Symptom**: `Unable to read .../Intermediate/.../DependencyCache.bin` during the build step.
- **Root Cause**: Stale dependency cache files produced by previous UBT runs can break when switching configurations.
- **Mitigation**: Delete the cache (when present) before invoking `Build.sh`. The script now removes `Intermediate/Build/Linux/x64/FlightProject/<Config>/DependencyCache.bin` automatically.

## 3. AutomationTool NU1901/NU1902/NU1903 Errors
- **Symptom**: AutomationTool failed before generating its own log while building `AutomationUtils.Automation.csproj` and `Gauntlet.Automation.csproj`, reporting security advisories for `Magick.NET-Q16-HDRI-AnyCPU` 14.7.0.
- **Root Cause**: UE 5.6 ships with this package version; the advisories are upstream and Epic has not updated yet. On Arch/CachyOS the system .NET SDK treats the advisories as errors.
- **Mitigation**:
  - Sandbox dotnet state under `Saved/DotNetCli` (`DOTNET_CLI_HOME`) to avoid permission issues and first-run prompts.
  - Default `MSBUILDWARNNOTASERROR` to include `NU1901;NU1902;NU1903`.
  - Force NuGet audit mode off with `NUGET_AUDIT_MODE=none`.
  - .NET SDK 8.0.400+ (and 9/10 previews) now ignores `MSBUILDWARNNOTASERROR` when the project sets `<TreatWarningsAsErrors>`. Drop a `Directory.Build.props` alongside the Unreal Engine root that adds `<WarningsNotAsErrors>NU1901;NU1902;NU1903;NU1904;$(WarningsNotAsErrors)</WarningsNotAsErrors>` (and optionally `<NuGetAudit>false</NuGetAudit>` if you need a temporary escape hatch) so AutomationTool stops failing while still surfacing the advisories.
  - Pre-create an AutomationTool log file (`Saved/Logs/AutomationTool/BuildCookRun-<timestamp>.log`) and pass it via `-log=...` when invoking `RunUAT.sh` so that logging succeeds even if the engine tree is read-only.
  - For source-built engines, make sure `Engine/Binaries/Linux/UnrealPak` exists before packaging. If it's missing, run `Engine/Build/BatchFiles/Linux/Build.sh UnrealPak Linux Development` (the script now does this automatically).
  - Skip rebuilding AutomationTool by passing `-nocompileuat` to `RunUAT.sh`.

## 4. AutomationTool Log Creation Permission Denied
- **Symptom**: `Error while creating log file ".../Engine/Programs/AutomationTool/Saved/Logs/ErrorLog.txt"` due to write protection on the engine tree.
- **Root Cause**: UE tries to write logs inside the engine directory when `uebp_LogFolder` is unset.
- **Mitigation**: Point `uebp_LogFolder` at the project (`Saved/Logs/AutomationTool`). All UAT runs now log under the project directory without touching the engine.

## 5. Transition Map Missing During Seamless Travel
- **Symptom**: Level loads referenced `/Game/Maps/Loading`, but the asset picker showed it as missing and seamless travel stalled or logged warnings.
- **Root Cause**: `Config/DefaultEngine.ini` set `TransitionMap=/Game/Maps/Loading`, yet the level asset was never authored.
- **Mitigation**: Pointed `TransitionMap` at `/Game/Maps/PersistentFlightTest` (October 10, 2025) so seamless travel no longer references a missing map. Replace with a lighter `/Game/Maps/Loading` when the asset exists.

## 6. CSV Data Not Cooked Into Staged Builds
- **Symptom**: Staged builds fall back to hard-coded lighting/autopilot defaults, and `FlightDataSubsystem` warns that CSV rows failed to load.
- **Root Cause**: The `Content/Data` directory was not listed under *Project Settings → Packaging → Additional Asset Directories to Cook*, so CSVs were omitted when staged.
- **Mitigation**: Added `+DirectoriesToAlwaysCook=(Path="/Game/Data")` to `DefaultGame.ini` (Oct 10, 2025). Re-stage builds if the CSV warning reappears, especially after adding new data directories.

## 7. Custom Shader Directory Unregistered
- **Symptom**: Future RDG/compute shader work failed to compile or package found no shader sources.
- **Root Cause**: `UFlightProjectDeveloperSettings` pointed `ComputeShaderDirectory` at `/Shaders`, but the module never registered a shader directory mapping and no project `Shaders/` folder existed.
- **Mitigation**: Added a project-level `Shaders/` directory and registered the mapping in `FFlightProjectModule::StartupModule()` via `FShaderDirectories::AddShaderSourceDirectoryMapping` (October 10, 2025). New shader files should now compile as long as they live under `Shaders/`.

## 8. Tooltip/Popup Ghost Boxes on Wayland/Hyprland

- **Symptom**: Empty gray boxes appear in the editor where tooltips or dropdown menus should render. Moving windows leaves ghost artifacts. Popups may appear in wrong positions.
- **Root Cause**: UE 5.7's SDL3 Wayland backend combined with Hyprland's popup handling creates rendering artifacts for transient windows (tooltips, menus, dropdowns).
- **Mitigations Applied**:
  1. Enabled libdecor in `Scripts/env_common.sh`:
     ```bash
     export SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=1
     ```
  2. Added Hyprland window rules in `~/.config/hypr/config/windowrules.conf`:
     ```
     windowrulev2 = noblur, class:^(UnrealEditor)$
     windowrulev2 = noshadow, class:^(UnrealEditor)$
     windowrulev2 = float, class:^(UnrealEditor)$, title:^()$
     windowrulev2 = stayfocused, class:^(UnrealEditor)$, title:^()$
     windowrulev2 = noinitialfocus, class:^(UnrealEditor)$, title:^()$
     windowrulev2 = noanim, class:^(UnrealEditor)$, title:^()$
     windowrulev2 = minsize 1 1, class:^(UnrealEditor)$
     windowrulev2 = nofocus, class:^(UnrealEditor)$, floating:1, title:^()$
     windowrulev2 = renderunfocused, class:^(UnrealEditor)$
     ```
  3. **Disabled tooltips entirely** as a workaround in `Config/DefaultEngine.ini`:
     ```ini
     [ConsoleVariables]
     Slate.EnableTooltips=0
     ```
- **Runtime Toggle**: You can re-enable tooltips at runtime via console:
  ```
  Slate.EnableTooltips 1
  ```
- **Alternative**: Run with `--x11` flag via XWayland (loses native Wayland benefits but avoids popup issues).

## Outstanding Questions
- Keep an eye on Epic updates that bump Magick.NET; once upstream resolves the advisories we can relax the environment overrides.
- Monitor whether staged builds can be cached between branches without forcing a fresh cook (current script always stages when the target map changes).
- Watch for SDL3/Hyprland popup fixes upstream; may be able to re-enable tooltips in future UE or Hyprland versions.
