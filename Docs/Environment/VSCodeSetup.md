# VS Code Setup for FlightProject

This document covers the VS Code configuration for Unreal Engine 5.7 development on Linux.

## Quick Start

```bash
# Open the workspace (not just the folder)
code /home/kelly/Unreal/Projects/FlightProject/FlightProject.code-workspace
```

## Required Extensions

| Extension | ID | Purpose |
|-----------|-----|---------|
| **clangd** | `llvm-vs-code-extensions.vscode-clangd` | C++ IntelliSense (faster than ms-cpptools) |
| **unreal-clangd** | `boocs.unreal-clangd` | UE-specific clangd configuration |
| **CodeLLDB** | `vadimcn.vscode-lldb` | LLDB debugging on Linux |

### Installing Extensions

```bash
# Marketplace extensions
code --install-extension llvm-vs-code-extensions.vscode-clangd
code --install-extension vadimcn.vscode-lldb

# unreal-clangd (from GitHub - not on open-vsx)
curl -LO https://github.com/boocs/unreal-clangd/releases/latest/download/unreal-clangd-*.vsix
code --install-extension unreal-clangd-*.vsix
```

## Key Files

| File | Purpose |
|------|---------|
| `FlightProject.code-workspace` | Workspace config (tasks, launch, settings) |
| `.vscode/c_cpp_properties.json` | C++ configuration |
| `.vscode/unreal-clangd/compile_commands.json` | Clangd compile database |
| `.clangd` | Clangd behavior settings |
| `.clang-format` | Code formatting (UE style) |

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Shift+B` | Build (Development) - default task |
| `Ctrl+Shift+P` | Command palette |
| `Ctrl+`` | Toggle integrated terminal |
| `F5` | Start debugging |
| `Ctrl+Shift+P` → "Tasks: Run Task" | Run any task |

## Build Tasks

Custom tasks using project scripts:

| Task | Script | Description |
|------|--------|-------------|
| **Build (Development)** | `./Scripts/build_targets.sh Development` | Default build (Ctrl+Shift+B) |
| **Build (Debug)** | `./Scripts/build_targets.sh Debug` | Debug build |
| **Run Editor (Wayland)** | `./Scripts/run_editor.sh --wayland` | Launch editor |
| **Build & Run Editor** | Both sequential | Build then launch |

### Running Tasks

1. **Quick build**: `Ctrl+Shift+B`
2. **Task picker**: `Ctrl+Shift+P` → "Tasks: Run Task" → select task
3. **Terminal**: `` Ctrl+` `` then `./Scripts/build_targets.sh Development`

## IntelliSense Setup

### How It Works

1. **unreal-clangd** generates `compile_commands.json` from UBT response files (`.rsp`)
2. **clangd** uses this to provide accurate IntelliSense
3. UE macros (`UCLASS`, `UPROPERTY`, etc.) are handled via `macroCompletionHelper.h`

### Adding New Source Files

**Compile commands do NOT auto-update** when you add new files. The `.rsp` files that clangd depends on are only regenerated during builds or project generation.

```
Add new .cpp/.h file
        ↓
UBT doesn't know about it yet
        ↓
compile_commands.json is stale
        ↓
clangd shows errors / no IntelliSense for new file
```

**Workflow for new files:**

```bash
# 1. Create the new file
touch Source/FlightProject/Private/MyNewClass.cpp

# 2. Add to module if needed (edit FlightProject.Build.cs)

# 3. Build - this updates .rsp files
./Scripts/build_targets.sh Development

# 4. Update clangd's compile database
Ctrl+Shift+P → "Unreal Clangd: Update Compile Commands"
```

### Updating Compile Commands

| Method | When to Use |
|--------|-------------|
| `Ctrl+Shift+P` → "Unreal Clangd: Update Compile Commands" | After build, quick refresh |
| `./Scripts/generate_project_files.sh` | Full project regeneration |
| `./Scripts/build_targets.sh Development` | Build updates .rsp files |

The `.rsp` (response) files live in `.vscode/compileCommands_FlightProject/` and are created by UBT during:
- Project file generation (`GenerateProjectFiles.sh`)
- Build (`Build.sh`)

unreal-clangd can detect when `.rsp` files change and may prompt you to update.

### Troubleshooting IntelliSense

| Issue | Solution |
|-------|----------|
| Red squiggles everywhere | Run "Update Compile Commands" |
| Macros not recognized | Check `.vscode/unreal-clangd/macroCompletionHelper.h` exists |
| clangd not starting | `Ctrl+Shift+P` → "clangd: Restart language server" |
| Wrong includes | Verify `compile_commands.json` points to correct rsp files |

## Debugging

### Launch Configurations

The workspace includes launch configs for:
- `Launch FlightProjectEditor (Development)` - Editor with project
- `Launch FlightProject (Development)` - Standalone game
- Debug/DebugGame/Test/Shipping variants

### Debugging Steps

1. Set breakpoints in code
2. `F5` or Run → Start Debugging
3. Select launch configuration
4. Wait for build (if preLaunchTask configured)

### LLDB Tips

```
# In debug console
p MyVariable              # Print variable
expr MyVariable = 5       # Modify variable
bt                        # Backtrace
frame select 3            # Jump to frame
```

## Workspace Structure

```
FlightProject.code-workspace
├── folders:
│   ├── FlightProject (.)           # Project root
│   └── UE5 (/home/kelly/Unreal/UnrealEngine)  # Engine source
├── settings:
│   ├── clangd.path                 # /usr/bin/clangd
│   ├── clangd.arguments            # Background index, compile commands dir
│   └── C_Cpp.intelliSenseEngine    # Disabled (using clangd)
├── tasks:
│   ├── Build (Development)         # Default build
│   ├── Run Editor (Wayland)        # Launch editor
│   └── [UBT tasks...]              # Raw UBT commands
└── launch:
    └── [Debug configurations...]
```

## Configuration Files

### .clangd

Controls clangd behavior:

```yaml
CompileFlags:
  Add: ["-xc++", "-std=c++20"]
  Remove: ["-forward-unknown-to-host-compiler"]

Diagnostics:
  Suppress: ["pp_including_mainfile_in_preamble"]

InlayHints:
  ParameterNames: Yes
  DeducedTypes: Yes
```

### .clang-format

UE coding standard (used by clangd for formatting):
- Tabs (not spaces)
- Allman braces
- 120 column limit

Format selection: `Ctrl+K Ctrl+F`
Format file: `Ctrl+Shift+I`

## Terminal Workflow

For quick iteration, use the integrated terminal:

```bash
# Open terminal
Ctrl+`

# Build
./Scripts/build_targets.sh Development

# Run editor
./Scripts/run_editor.sh --wayland

# Build and run
./Scripts/build_targets.sh Development && ./Scripts/run_editor.sh --wayland
```

## Common Issues

### "Microsoft Cpp extension is disabled"

This is expected - we use clangd instead. The warning comes from unreal-clangd checking for conflicts.

### Compile commands not found

```bash
# Regenerate
Ctrl+Shift+P → "Unreal Clangd: Update Compile Commands"

# Or manually
./Scripts/generate_project_files.sh
```

### Extension not found on marketplace

Your VS Code uses open-vsx (common on Arch/CachyOS). Install from VSIX:

```bash
# Get latest release URL from GitHub
curl -LO <vsix-url>
code --install-extension <file>.vsix
```

### Workspace settings reset

unreal-clangd may backup/restore settings. Check:
- `.vscode/unreal-clangd/.code-workspace.backup`

If settings are wrong, restore from backup or re-add clangd settings manually.

## Performance Tips

1. **Background indexing**: clangd indexes in background - first load is slow
2. **Limit index scope**: `.clangd` can exclude paths if needed
3. **j=8**: Parallel indexing (adjust for CPU cores)
4. **Restart clangd**: If memory usage grows, restart language server

## See Also

- [BuildAndRegen.md](BuildAndRegen.md) - Build scripts documentation
- [LinuxSetup.md](LinuxSetup.md) - Linux/Wayland configuration
- [Scripting/EditorAutomation.md](../Scripting/EditorAutomation.md) - Python automation
