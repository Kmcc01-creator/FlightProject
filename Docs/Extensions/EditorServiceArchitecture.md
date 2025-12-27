# Unreal Editor Service Architecture

## Overview

The Unreal Editor uses a **tiered service architecture** rather than a single server/client model. Multiple local services run on the developer workstation, with optional team-wide services for shared caching and distributed builds.

**Status**: Reference Documentation
**Engine Version**: UE 5.7.1

## Core Concept: Derived Data Cache (DDC)

The DDC is a hierarchical caching system for computed assets (compiled shaders, cooked textures, etc.). Reads cascade through layers until satisfied; writes propagate to all writable layers.

```
┌─────────────────────────────────────────────────────────────────┐
│                        DDC Hierarchy                             │
│         (BaseEngine.ini [DerivedDataBackendGraph])               │
├─────────────────────────────────────────────────────────────────┤
│  Layer         │ Type       │ Scope      │ Default Status        │
├─────────────────────────────────────────────────────────────────┤
│  Pak           │ ReadPak    │ Project    │ Optional .ddp file    │
│  EnginePak     │ ReadPak    │ Engine     │ Optional .ddp file    │
│  ZenLocal      │ Zen        │ Local      │ Auto-launched :8558   │
│  Local         │ FileSystem │ Local      │ Engine/DerivedDataCache│
│  ZenShared     │ Zen        │ Team       │ Disabled (Host=None)  │
│  Shared        │ FileSystem │ Team       │ Disabled (no path)    │
│  Cloud         │ HTTP       │ Global     │ Disabled (Host=None)  │
└─────────────────────────────────────────────────────────────────┘
```

### Configuration Source

From `Engine/Config/BaseEngine.ini`:
```ini
[DerivedDataBackendGraph]
Root=(Type=Hierarchical, Inner=Pak, Inner=EnginePak, Inner=ZenLocal, Inner=Local, Inner=ZenShared, Inner=Shared, Inner=Cloud)

[StorageServers]
Cloud=(Host=None, Namespace="ue.ddc")
Local=(Namespace="ue.ddc")
Shared=(Host=None, Namespace="ue.ddc", EnvHostOverride=UE-ZenSharedDataCacheHost)
```

## Local Services

These services are auto-launched on the developer workstation:

### ZenServer (Unreal Zen Storage Server)

Content-addressable key-value store optimized for DDC operations.

| Property | Value |
|----------|-------|
| Default Port | 8558 |
| Protocol | HTTP |
| Data Path | `Engine/Saved/Zen/` |
| Namespace | `ue.ddc` |

**Log indicators**:
```
LogZenServiceInstance: Unreal Zen Storage Server HTTP service at http://[::1]:8558 status: OK!
LogDerivedDataCache: ZenLocal: Using ZenServer HTTP service at [::1] with namespace ue.ddc status: OK!
```

**Source**: `Engine/Source/Developer/Zen/`

### ShaderCompileWorker

Parallel shader compilation workers (separate processes).

| Property | Value |
|----------|-------|
| Workers | CPU core count (e.g., 8) |
| Working Dir | `Engine/Intermediate/Shaders/WorkingDirectory/` |
| Communication | Shared memory / files |

**Log indicators**:
```
LogShaderCompilers: Using 8 local workers for shader compilation
LogShaderCompilers: Compiling shader autogen file: .../AutogenShaderHeaders.ush
```

**Source**: `Engine/Source/Programs/ShaderCompileWorker/`

### UnrealTraceServer

Profiling and trace data collection service.

| Property | Value |
|----------|-------|
| Control Port | 34322 |
| Channels | cpu, gpu, frame, log, bookmark, screenshot, region |
| Launch | Forked from editor process |

**Log indicators**:
```
LogCore: UTS: Unreal Trace Server launched successfully
LogTrace: Control listening on port 34322
```

**Source**: `Engine/Source/Programs/UnrealTraceServer/`

## Team Infrastructure (Optional)

### ZenShared - Team Zen Server

Centralized Zen instance shared across developer workstations.

**Enable via environment**:
```bash
export UE_ZenSharedDataCacheHost="http://build-server.local:8558"
```

**Enable via command line**:
```bash
./UnrealEditor -ZenSharedDataCacheHost=http://build-server.local:8558
```

**Benefits**:
- Shader compilation shared across team (compile once, use everywhere)
- Asset cooking results cached for all developers
- Significant time savings on large projects

### Shared - Network Filesystem Cache

Traditional filesystem-based DDC on network storage (NFS/SMB).

**Enable via environment**:
```bash
export UE_SharedDataCachePath="/mnt/studio-ddc"
```

**Configuration**:
```ini
[StorageServers]
Shared=(Host=http://shared-zen.local:8558, Namespace="ue.ddc")
```

### Cloud - Epic Cloud DDC

Epic's hosted cloud DDC for distributed teams.

**Requirements**:
- Epic partner/licensee authentication
- Network connectivity to Epic infrastructure

**Enable via configuration**:
```ini
[StorageServers]
Cloud=(Host=https://jupiter.epicgames.com, Namespace="ue.ddc")
```

### UBA/Horde - Distributed Build System

Unreal Build Accelerator with Horde orchestration for build farms.

**Log indicators** (when disabled):
```
LogUbaHorde: UBA/Horde Configuration [Uba.Provider.Horde]: Not Enabled
LogShaderCompilers: No distributed shader compiler controller found
```

**When enabled**: Distributes shader compilation and asset cooking across build farm nodes.

## Service Interaction Diagram

```
Developer Workstation                    Team Infrastructure (Optional)
┌─────────────────────────────────┐     ┌─────────────────────────────┐
│  UnrealEditor                   │     │  ZenShared Server           │
│  ├── ZenServer (:8558)          │────▶│  (shared DDC)               │
│  ├── ShaderCompileWorker (×8)   │     └─────────────────────────────┘
│  ├── UnrealTraceServer (:34322) │     ┌─────────────────────────────┐
│  └── DDC Hierarchy              │     │  Horde Build Farm           │
│      ├── ZenLocal ✓             │────▶│  (distributed compilation)  │
│      ├── Local ✓                │     └─────────────────────────────┘
│      ├── ZenShared ✗            │     ┌─────────────────────────────┐
│      ├── Shared ✗               │     │  Cloud DDC                  │
│      └── Cloud ✗                │────▶│  (Epic infrastructure)      │
└─────────────────────────────────┘     └─────────────────────────────┘
```

## Performance Characteristics

From startup logs:
```
LogDerivedDataCache: ../../../Engine/DerivedDataCache:
  Latency=0.01ms
  RandomReadSpeed=314.14MBs
  RandomWriteSpeed=676.24MBs
  Assigned SpeedClass 'Local'
```

Speed classes affect cache behavior:
- **Local**: Fast storage (SSD/NVMe) - preferred for writes
- **Fast**: Network storage with low latency
- **Slow**: High-latency network/cloud storage - read-only preferred

## FlightProject Considerations

For team development on FlightProject:

1. **Solo Development** (current): Default configuration is optimal
   - ZenLocal handles all DDC operations
   - 8 shader workers for parallel compilation

2. **Small Team**: Consider ZenShared
   - Shared shader compilation saves hours on initial builds
   - Mass Entity shader variants benefit from caching

3. **CI/CD Integration**: UBA/Horde for automated builds
   - Distribute shader compilation across build agents
   - Cache cooked content for faster iteration

## Related Source Files

| Module | Path | Purpose |
|--------|------|---------|
| DDC Core | `Engine/Source/Developer/DerivedDataCache/` | Cache hierarchy implementation |
| Zen | `Engine/Source/Developer/Zen/` | ZenServer client/launcher |
| Shader Compiler | `Engine/Source/Programs/ShaderCompileWorker/` | Shader compilation worker |
| Trace Server | `Engine/Source/Programs/UnrealTraceServer/` | Profiling service |
| Storage Widgets | `Engine/Source/Editor/StorageServerWidgets/` | Editor UI for Zen status |

## Configuration Reference

### Environment Variables

| Variable | Purpose |
|----------|---------|
| `UE-LocalDataCachePath` | Override local DDC path |
| `UE-SharedDataCachePath` | Network filesystem DDC path |
| `UE-ZenSharedDataCacheHost` | Team Zen server URL |
| `UE-CloudDataCacheHost` | Cloud DDC URL |

### Command Line Switches

| Switch | Purpose |
|--------|---------|
| `-DDC=None` | Disable DDC (programs only) |
| `-DDC=Cold` | Fresh cache (no inherited data) |
| `-LocalDataCachePath=` | Override local DDC path |
| `-ZenSharedDataCacheHost=` | Team Zen server URL |

## Related Documentation

- `../Architecture/EngineIntegration.md` - Engine module usage
- `../Environment/BuildAndRegen.md` - Build commands
- `../Environment/Configuration.md` - INI configuration
