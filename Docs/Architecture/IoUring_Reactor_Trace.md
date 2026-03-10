# Linux Architecture: io_uring, Managed Reactor, and Telemetry

This document outlines the high-performance I/O and synchronization architecture implemented for Unreal Engine 5.6/7 on Linux (Arch/Hyprland).

## 1. The Managed io_uring Reactor (`FVulkanIoUringReactor`)

To solve the inherent thread-safety limitations of `io_uring` and provide a stable GPU-to-CPU signaling path, we implemented a **Message Pump** architecture.

### Core Pattern: Single-Threaded Owner
The `io_uring` ring (SQ/CQ) is exclusively owned and pumped by the **Game Thread** (via `Tick()`). This eliminates cross-thread races without requiring expensive mutex contention.

*   **Producer (Any Thread):** `ArmSyncPoint()` enqueues an `FSyncRequest` into an MPSC queue (`TQueue`).
*   **Consumer (Game Thread):** `Tick()` dequeues requests, prepares `IORING_OP_POLL_ADD` SQEs, and submits them to the kernel.
*   **Stability (Robust Drain):** During `Shutdown()`, the reactor enters a quiescence state, waiting for active producers to finish before draining all queues and finalizing pending CQEs.

### GPU Sync Bridge
We leverage **Vulkan Timeline Semaphores** bridged to **Linux Sync Files (`sync_fd`)**.
1.  Submit a wait on the timeline value and signal a binary semaphore.
2.  Export the binary semaphore state as a `sync_fd` (`VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT`).
3.  The Reactor polls this FD via `io_uring`, providing a zero-syscall completion notification to the engine's Task Graph.

## 2. High-Performance Telemetry (`FFlightIoUringTraceSink`)

The Trace Sink provides a non-blocking transport for Unreal Engine's `TraceLog` system, ensuring that high-frequency telemetry does not stall the simulation.

### Slab-Copy & Fixed Buffers
*   **Zero-Copy I/O:** Uses `IORING_REGISTER_BUFFERS` and `IORING_OP_WRITE_FIXED`.
*   **Slab Management:** Data is copied from Unreal's TLS buffers into reactor-owned memory slabs. This decouples the Trace worker thread from I/O latency.
*   **File Integrity:** Tracks a monotonic `FileOffset` to prevent write-overlapping common in asynchronous streams.

### Threading Model
A dedicated background thread owns the Trace `io_uring` ring. It uses a **Wake Event** (`FEvent`) to sleep when no data is present, achieving sub-millisecond response times upon a new slab enqueue.

## 3. Testing & Validation
Functionality is verified via the `FlightProject.IoUring.Vulkan.Complex` suite:
*   **Reactor.BurstSubmission:** Validates that 50+ concurrent sync points are handled without drops or stalls.
*   **Trace.HighFrequencySlabs:** Verifies sustained throughput and lifecycle stability of the slab pipeline.
