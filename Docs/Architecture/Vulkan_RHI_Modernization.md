# Vulkan RHI Modernization: "Vulkan 2.0" Baseline

This document details the amendments made to the Unreal Engine 5.6/7 Vulkan RHI to support Linux-native high-performance development.

## 1. API Elevation (Vulkan 1.3)

We have formally elevated the Linux Vulkan baseline to **API Version 1.3**.

*   **File:** `VulkanLinuxPlatform.h`
*   **Impact:** Simplifies the RHI backend by making features like **Dynamic Rendering**, **Timeline Semaphores**, and **Synchronization2** core requirements. It eliminates long `pNext` chains for previously optional extensions.

## 2. Anti-Stutter & Fluidity (Proton Parity)

To achieve the "Fluid Editor" experience characteristic of the Steam Deck and Proton, we have enabled modern pipeline extensions by default on Linux.

*   **Shader Objects (`VK_EXT_shader_object`):** Allows the engine to bypass full PSO compilation hitches by using granular shader states.
*   **Pipeline Libraries (`VK_EXT_graphics_pipeline_library`):** Enables asynchronous pre-compilation of pipeline parts.
*   **Extended Dynamic State:** Reduces the total number of unique PSOs required by moving more state (viewport, scissor, blend constants) into dynamic command-buffer calls.

## 3. Compositor Bypass (Direct Mode)

For low-latency simulation and specialized hardware support, we implemented a foundational **Direct Mode** path.

*   **Extension:** `VK_KHR_display` and `VK_EXT_acquire_drm_display`.
*   **Activation:** Launch the engine with the `-VulkanDirectMode` flag.
*   **Implementation:** `FVulkanLinuxPlatform::CreateDirectSurface` bypasses SDL3 and the Wayland/X11 compositor to acquire a DRM connector directly. The swapchain automatically overrides its dimensions to match the physical `VkExtent2D` of the acquired display.

## 4. Architectural Reference: IDIC (Rust)

Our modernization strategy is heavily informed by the **idic** reference project (a managed Vulkan 2.0 implementation in Rust).

### Key Insights Adopted:
1.  **Metadata-Driven Design:** Moving toward spec-compliant function loading and resource tracking derived from `vk.xml`.
2.  **Managed Sync:** Using the `GpuWorkFuture` pattern (translated to our `FSyncFdState`) to treat GPU completion as a trackable, asynchronous event.
3.  **Resource Pooling:** Applying `idic`-style pooling for Timeline Semaphores and FDs to minimize allocation overhead.

*Reference Path:* `WaylandUnreal/idic_reference/` (Local copy of the idic crates).

## 5. Build Environment Optimization

To focus development on the Linux powerhouse, we have streamlined the build process:
*   **Blacklisted Platforms:** `IOS`, `TVOS`, and `Mac` are explicitly disabled in `DefaultEngine.ini` to prevent plugin discovery overhead.
*   **UBT Refinement:** Adaptive non-unity builds are prioritized for the core RHI files to ensure fast iteration on our patches.
