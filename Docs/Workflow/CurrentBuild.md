# Current Build & Test Documentation

This document outlines the build system configuration, test execution patterns, and strategies for structured development within FlightProject.

## 1. Test Execution & Discovery
We successfully verified the entire FlightProject suite using Unreal's Automation Framework. 

### Key Learning: Discovery Context
Tests defined with `EAutomationTestFlags::ClientContext` are **not** discovered when running via `UnrealEditor-Cmd`. To enable discovery in commandlet/CI environments, tests must use:
- `EAutomationTestFlags::EditorContext`

### Core Commands
**Build Command:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
stdbuf -oL -eL ./Scripts/build_targets.sh Development < /dev/null
```

**Full Suite Test Command:**
```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine
stdbuf -oL -eL $UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd \
    /home/kelly/Unreal/Projects/FlightProject/FlightProject.uproject \
    -ExecCmds="Automation RunTests FlightProject; quit" \
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
    -NullRHI -NoPCH -NoBT -NoDDC -NoDDCMaintenance < /dev/null
```
*Note: `quit` ensures immediate exit. `-NoDDC` and `-NoDDCMaintenance` bypass lengthy cache verification, reducing turnaround time from minutes to seconds.*

## 2. Documentation Links
- [Project README](Projects/FlightProject/Docs/README.md): High-level project overview and setup.
- [Workflow Guide](Projects/FlightProject/Docs/Workflow/README.md): Development cycles and CI patterns.
- [Architecture Docs](Projects/FlightProject/Docs/Architecture/): Deep technical design specifications.

## 3. Structured Testing Strategy
As the project grows, we organize tests into a hierarchical structure:

### Directory Organization
- `Source/FlightProject/Private/Tests/Unit/Core/`: Reflection, RowTypes, Result traits.
- `Source/FlightProject/Private/Tests/Unit/Mass/`: Fragment composition, Optics queries.
- `Source/FlightProject/Private/Tests/Integration/UI/`: Slate ReactiveUI bindings.
- `Source/FlightProject/Private/Tests/Integration/IoUring/`: GPU bridge and async completion.

### Test Categories
- **Smoke Tests (`SmokeFilter`)**: Fast, trait-level verification (run every build).
- **Engine Tests (`EngineFilter`)**: Integration tests requiring a full World/Subsystem state.

## 4. Build Environments & Contexts

### Context Types
- **EditorContext**: Required for tests run via `UnrealEditor-Cmd`.
- **ClientContext**: Tests behavior in the standalone game client.

### Build Variations
- **Development**: `WITH_DEV_AUTOMATION_TESTS=1`. Includes debug symbols and test registry.
- **Shipping**: `WITH_DEV_AUTOMATION_TESTS=0`. Minimal overhead, no tests included.

## 5. Performance & Environment Configuration

### Shader Compilation Optimization
To handle massive shader queues (e.g., global recompiles), we maximize core utilization in `DefaultEngine.ini`:
- **Core Allocation**: 15 out of 16 logical threads (on 8-core CPU) are assigned to workers.
- **Batching**: `MaxShaderJobBatchSize` set to 16 to reduce worker launch overhead.
- **Priority**: Workers run at `Below Normal` priority to keep the Editor UI responsive.

### Editor Responsiveness (Linux/Wayland)
Specific tuning in `LinuxEngine.ini` for high-performance development:
- **UI Refresh**: `Slate.TickRate=120` for high-Hz display smoothness.
- **Background Ticking**: `Editor.Performance.ThrottleUnfocused=0` to prevent "wake-up" lag.
- **Task Graph**: 12 worker threads allocated for runtime tasks, 4 high-priority threads reserved for UI/Main loop.
- **Anti-Aliasing**: Forced TAA (`r.AntiAliasingMethod=2`) and enabled `r.Editor.MovingGizmoTAA` to fix "jaggy" widgets.

### Display & Resolution
- **Native Resolution**: Forced `r.setres=1920x1200` to align with hardware and prevent Wayland compositor clipping.
- **High DPI**: Recommended to **Disable High DPI Support** in Editor Preferences when running on Wayland to avoid coordinate scaling conflicts.

## 6. Pragma & Macro Usage
- **Optimization Guards**: Use `PRAGMA_DISABLE_OPTIMIZATION` around sensitive templates if Clang 20 recursion depth becomes an issue.
- **Macro Semicolons**: Custom macros like `FLIGHT_REFLECT_FIELDS` provide trailing semicolons for standard C++ compatibility.
