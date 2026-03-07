# io_uring GPU Integration

Zero-syscall GPU completion notification using Linux io_uring and Vulkan exportable semaphores.

## Overview

This system enables the game thread to receive GPU work completion notifications without polling or syscalls in steady state. It bridges Vulkan's timeline semaphores to Linux's io_uring via exportable binary semaphores and sync_fd.

```
Game Thread                          GPU                         io_uring
    │                                 │                              │
    ├─► Build RDG graph               │                              │
    ├─► GraphBuilder.Execute()        │                              │
    │                                 ├─► Compute work running       │
    ├─► SignalGpuCompletion(id, cb)   │                              │
    │   └─► Creates binary semaphore  │                              │
    │   └─► Chains wait on timeline   │                              │
    │   └─► Exports sync_fd ──────────┼──────────────────────────────┤
    │                                 │                              │
    │   (no syscalls in steady state) │                              │
    │                                 ├─► Work completes             │
    │                                 ├─► Semaphore signals          │
    │                                 │                              │
    │◄────────────────────────────────┼──────────────────────────────┤
    │   CQE arrives, callback fires   │                              │
```

## Architecture

### Plugin Structure

```
Plugins/
├── FlightVulkanExtensions/     (LoadingPhase: PostConfigInit)
│   └── Requests VK_KHR_external_semaphore_fd before RHI creation
│
└── FlightGpuCompute/           (LoadingPhase: PostConfigInit)
    ├── Shaders/
    │   ├── FlightTestCompute.usf      - Test/validation shader
    │   └── FlightHorizonScan.usf      - Radial obstacle detection
    └── Source/
        ├── FlightTestComputeShader.h/cpp
        └── FlightHorizonScanShader.h/cpp
```

### Core Classes

| Class | Location | Purpose |
|-------|----------|---------|
| `FRing` | IoUring/FlightIoRing.h | Native io_uring wrapper (direct syscalls) |
| `UFlightIoUringSubsystem` | IoUring/ | World subsystem for ring management |
| `UFlightGpuIoUringBridge` | IoUring/ | GPU semaphore → sync_fd → io_uring |
| `FExportableSemaphore` | IoUring/ | Binary semaphore with SYNC_FD export |
| `UFlightGpuPerceptionSubsystem` | IoUring/ | High-level async GPU queries |

### Vulkan Semaphore Flow

Per Vulkan spec, `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT` can only be used with **binary** semaphores, not timeline. The solution:

1. UE5 uses a timeline semaphore for GPU work ordering
2. We create a binary semaphore per job
3. Submit a batch that waits on UE's timeline value, then signals our binary
4. Export sync_fd from the binary semaphore
5. io_uring polls the sync_fd
6. When GPU signals the binary semaphore, sync_fd becomes readable

```cpp
// Simplified flow in FExportableSemaphore
VkTimelineSemaphoreSubmitInfo TimelineSubmitInfo{};
TimelineSubmitInfo.waitSemaphoreValueCount = 1;
TimelineSubmitInfo.pWaitSemaphoreValues = &UeTimelineValue;

VkSubmitInfo SubmitInfo{};
SubmitInfo.pNext = &TimelineSubmitInfo;
SubmitInfo.waitSemaphoreCount = 1;
SubmitInfo.pWaitSemaphores = &UeTimelineSemaphore;    // Timeline
SubmitInfo.signalSemaphoreCount = 1;
SubmitInfo.pSignalSemaphores = &OurBinarySemaphore;   // Binary

vkQueueSubmit(Queue, 1, &SubmitInfo, VK_NULL_HANDLE);

// Now export sync_fd from the binary semaphore
VkSemaphoreGetFdInfoKHR FdInfo{};
FdInfo.semaphore = OurBinarySemaphore;
FdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
vkGetSemaphoreFdKHR(Device, &FdInfo, &SyncFd);
```

## Usage

### Low-Level: Direct Bridge Access

```cpp
// Get the bridge
auto* Bridge = GetWorld()->GetSubsystem<UFlightGpuIoUringBridge>();

// Dispatch GPU work via RDG
ENQUEUE_RENDER_COMMAND(MyCompute)(
    [](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);

        // Add your compute passes...
        auto* Params = GraphBuilder.AllocParameters<FMyShader::FParameters>();
        // ...setup params...
        FComputeShaderUtils::AddPass(GraphBuilder, ...);

        GraphBuilder.Execute();
    });

// Signal that you want notification when GPU work completes
int64 TrackingId = GenerateUniqueId();
Bridge->SignalGpuCompletion(TrackingId, [this, TrackingId]()
{
    // This callback fires when GPU work is done
    // Zero syscalls in steady state!
    UE_LOG(LogMySystem, Log, TEXT("GPU work %lld complete"), TrackingId);
    ProcessResults();
});
```

### High-Level: Perception Subsystem

```cpp
auto* Perception = GetWorld()->GetSubsystem<UFlightGpuPerceptionSubsystem>();

// Build request
FFlightPerceptionRequest Request;
Request.EntityPositions.Reserve(SwarmSize);
for (const auto& Entity : Swarm)
{
    Request.EntityPositions.Add(FVector4f(
        Entity.Location.X,
        Entity.Location.Y,
        Entity.Location.Z,
        ScanRadius
    ));
}
Request.ObstacleMinBounds = ObstacleMins;
Request.ObstacleMaxBounds = ObstacleMaxs;

// Submit - callback fires when GPU done
Perception->SubmitPerceptionRequest(Request,
    [this](const FFlightPerceptionResult& Result)
    {
        if (Result.bSuccess)
        {
            UE_LOG(LogSwarm, Log, TEXT("Perception query %lld: %.2f ms"),
                Result.RequestId, Result.GpuTimeMs);

            for (int32 i = 0; i < Result.ObstacleCounts.Num(); ++i)
            {
                // Process per-entity obstacle counts
                UpdateEntityThreatLevel(i, Result.ObstacleCounts[i]);
            }
        }
    });
```

### Blueprint Usage

```cpp
// Get subsystem
auto* Perception = GetWorld()->GetSubsystem<UFlightGpuPerceptionSubsystem>();

// Bind to delegate for results
Perception->OnPerceptionResultReady.AddDynamic(this, &UMyComponent::OnPerceptionReady);

// Submit via Blueprint-friendly API
FFlightPerceptionRequest Request;
// ... fill request ...
Perception->SubmitPerceptionRequestBP(Request);
```

### Testing

```cpp
// Quick validation test
auto* Perception = GetWorld()->GetSubsystem<UFlightGpuPerceptionSubsystem>();
Perception->RunTestQuery(100, 50);  // 100 entities, 50 obstacles

// Or attach UFlightGpuIoUringIntegrationTest component to any actor
// Runs full test suite on BeginPlay
```

## When to Use

### Good Use Cases

| Scenario | Why io_uring? |
|----------|---------------|
| Swarm perception (1000+ entities) | Batch GPU work, async results |
| Horizon scanning / visibility | Fire-and-forget spatial queries |
| GPU readback coordination | Know exactly when buffer is safe to read |
| Multi-frame compute | Long-running work spanning frames |
| Procedural generation | GPU terrain/vegetation, async callback |

### When NOT to Use

| Scenario | Why Not? |
|----------|----------|
| Single-frame immediate results | Need result THIS frame, just wait |
| Trivial computation | Overhead exceeds benefit |
| Latency-critical paths | Async adds 1+ frame latency |
| Non-Linux platforms | io_uring is Linux-only |

## Performance Characteristics

- **Steady-state syscalls**: Zero (CQ polling is userspace memory read)
- **Per-job overhead**: One binary semaphore + one sync_fd + one POLL_ADD SQE
- **Latency**: Typically 1-2 frames from submit to callback
- **Throughput**: Tested at 100+ concurrent GPU jobs

## Requirements

### Vulkan Extensions

Requested automatically by FlightVulkanExtensions plugin:

- `VK_KHR_external_semaphore` (device)
- `VK_KHR_external_semaphore_fd` (device)
- `VK_KHR_external_semaphore_capabilities` (instance)

**CRITICAL (Commandlet Mode):** When running in `-unattended` or commandlet mode (e.g., automation tests), the RHI often initializes before the plugin can register extensions. You **MUST** pass these explicitly via the command line to ensure `io_uring` synchronization works:
```bash
-vulkanextension="VK_KHR_external_semaphore_fd" -vulkanextension="VK_KHR_external_semaphore"
```

Note: `VK_KHR_timeline_semaphore` is already enabled by UE 5.7.

### Kernel Requirements

- Linux kernel 5.1+ (for io_uring)
- Kernel 5.6+ recommended (for IORING_FEAT_FAST_POLL)

## Debugging

### Log Categories

```
LogFlightIoUring        - io_uring ring operations
LogFlightGpuBridge      - GPU bridge lifecycle and completions
LogFlightPerception     - Perception subsystem requests
LogFlightExportableSemaphore - Semaphore creation/export
```

### Statistics

```cpp
auto* Bridge = GetWorld()->GetSubsystem<UFlightGpuIoUringBridge>();
UE_LOG(LogTemp, Log, TEXT("Submissions: %lld, Completions: %lld, Pending: %d"),
    Bridge->GetTotalSubmissions(),
    Bridge->GetTotalCompletions(),
    Bridge->GetPendingCount());

auto* Perception = GetWorld()->GetSubsystem<UFlightGpuPerceptionSubsystem>();
UE_LOG(LogTemp, Log, TEXT("Avg GPU time: %.2f ms"),
    Perception->GetAverageGpuTimeMs());
```

### Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| "Exportable semaphores not available" | Extension not loaded | Check FlightVulkanExtensions plugin enabled |
| Callbacks never fire | io_uring not polling | Ensure `ProcessCompletions()` called each frame |
| "vkGetSemaphoreFdKHR not available" | Extension missing | Verify Vulkan driver supports external semaphore |
| Crash in callback touching actors | Callback on wrong thread | Use `AsyncTask(GameThread, ...)` to marshal |
| Data race on statistics | Counter not atomic | Use `std::atomic<int64>` with `fetch_add`/`load` |
| Leaked sync_fd/semaphore | Duplicate TrackingId | Check `Contains()` and cleanup before insert |

## Thread Safety

### Critical: io_uring Callbacks Execute on Worker Thread

The io_uring subsystem processes completions on a worker thread, not the game thread. 

1. **Marshal to Game Thread**: User-supplied callbacks that modify game state must be marshaled back to the game thread.
2. **Render Thread Readback**: Accessing RHI buffers (`GDynamicRHI->RHILockBuffer`) **MUST** occur on the Rendering Thread.

```cpp
// CORRECT Readback Pattern
void MySubsystem::OnGpuWorkComplete(TRefCountPtr<FRDGPooledBuffer> Buffer) {
    // 1. Jump to Render Thread to lock/read buffer
    ENQUEUE_RENDER_COMMAND(Readback)([Buffer](FRHICommandListImmediate& RHICmdList) {
        void* Data = GDynamicRHI->RHILockBuffer(RHICmdList, Buffer->GetRHI(), ...);
        // ... copy data to local TArray ...
        GDynamicRHI->RHIUnlockBuffer(RHICmdList, Buffer->GetRHI());
        
        // 2. Jump to Game Thread to apply results
        AsyncTask(ENamedThreads::GameThread, [Results]() {
            // ... apply to simulation ...
        });
    });
}
```

### Atomic Counters for Cross-Thread Statistics

Counters accessed from both io_uring worker and game thread must be atomic:

```cpp
// Header
std::atomic<int64> TotalSubmissions{0};
std::atomic<int64> TotalCompletions{0};

// Increment (from worker thread)
TotalCompletions.fetch_add(1, std::memory_order_relaxed);

// Read (from game thread)
int64 GetTotalCompletions() const {
    return TotalCompletions.load(std::memory_order_relaxed);
}
```

### Duplicate TrackingId Handling

If a caller reuses a TrackingId before the previous request completes, the old state must be cleaned up to prevent resource leaks:

```cpp
{
    FScopeLock Lock(&PendingSyncStatesMutex);

    if (PendingSyncStates.Contains(TrackingId))
    {
        UE_LOG(..., TEXT("Duplicate TrackingId - cleaning up old entry"));
        CleanupPendingState_Locked(TrackingId);  // Closes fd, removes from map
    }

    PendingSyncStates.Add(TrackingId, State);
}
```

## Design Philosophy

Inspired by the patterns in `~/m2/systems` (Rust):

- **Direct syscalls**: No libc wrappers for io_uring operations
- **Kernel structs in code**: Avoids header version mismatches
- **Zero-copy where possible**: mmap'd rings, no intermediate buffers
- **SINGLE_ISSUER + COOP_TASKRUN**: Optimal io_uring flags for game thread

## Files

| File | Purpose |
|------|---------|
| `IoUring/FlightIoRing.h/cpp` | Native io_uring wrapper |
| `IoUring/FlightIoUringSubsystem.h/cpp` | UE subsystem for ring management |
| `IoUring/FlightGpuIoUringBridge.h/cpp` | GPU→io_uring bridge |
| `IoUring/FlightExportableSemaphore.h/cpp` | Binary semaphore with sync_fd |
| `IoUring/FlightGpuPerceptionSubsystem.h/cpp` | High-level perception API |
| `IoUring/FlightGpuIoUringIntegrationTest.h/cpp` | Test suite |
| `Shaders/Private/FlightTestCompute.usf` | Test compute shader |
| `Shaders/Private/FlightHorizonScan.usf` | Obstacle detection shader |

## See Also

- `WaylandUnreal/docs/IoUring_GPU_Integration.md` - Original design notes
- `~/m2/systems` - Rust reference implementation
- [io_uring documentation](https://kernel.dk/io_uring.pdf)
- [VK_KHR_external_semaphore_fd](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_external_semaphore_fd.html)
