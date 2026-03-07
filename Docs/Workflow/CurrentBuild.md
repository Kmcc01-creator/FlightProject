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

## 5. Pragma & Macro Usage
- **Optimization Guards**: Use `PRAGMA_DISABLE_OPTIMIZATION` around sensitive templates if Clang 20 recursion depth becomes an issue.
- **Macro Semicolons**: Custom macros like `FLIGHT_REFLECT_FIELDS` provide trailing semicolons for standard C++ compatibility.
