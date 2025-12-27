# DevNote 002: Linux Performance & Rendering Configuration

**Date:** Friday, December 26, 2025
**File:** `Config/Linux/LinuxEngine.ini`

## 1. Overview
The standard `DefaultEngine.ini` contains settings optimized for Windows (DX12/SM6). On Linux, particularly with NVIDIA/AMD drivers under X11 or Wayland, these high-end features often lead to editor unresponsiveness, UI lag, or driver crashes. 

We use `Config/Linux/LinuxEngine.ini` to override these defaults with a "Performance-First" configuration.

---

## 2. Configuration Breakdown

### Rendering Settings (`[/Script/Engine.RendererSettings]`)

| Setting | Value | Reasoning |
| :--- | :--- | :--- |
| `r.RayTracing` | `False` | RayTracing on Linux is often unstable and heavily impacts Slate UI performance. |
| `r.Shadow.Virtual.Enable` | `0` | Virtual Shadow Maps (VSM) are high-fidelity but very GPU intensive. Reverting to standard shadows improves FPS significantly. |
| `r.DynamicGlobalIlluminationMethod` | `0` | Disables Lumen. Lumen is great for fidelity but causes significant overhead in the Editor viewport on Linux. |
| `r.ReflectionMethod` | `0` | Disables Lumen Reflections. |
| `r.Nanite` | `0` | **Temporary.** Disabling Nanite can resolve issues where the Mesh Shader pipeline conflicts with certain Linux Vulkan drivers. |

### Platform Settings (`[/Script/LinuxTargetPlatform.LinuxTargetSettings]`)

*   **RHI Change:** We downgraded from `SF_VULKAN_SM6` to `SF_VULKAN_SM5`.
*   **Reasoning:** While SM6 provides the latest features, SM5 is the "Gold Standard" for stability on Linux. Most simulation tasks (Mass/ECS) do not require SM6 features, making this a safe trade-off for a responsive workspace.

---

## 3. Workflow Impacts
*   **Editor Responsiveness:** The UI should be snappier and context menus should appear without delay.
*   **GPU Usage:** Lower idle power consumption while the editor is open.
*   **Visual Fidelity:** You will notice flatter lighting and lack of advanced reflections. **This is acceptable for core simulation development.**

---

## 4. Future Reversion Path
If we need to perform "Beauty Passes" or visual polish:
1.  Temporarily comment out the overrides in `LinuxEngine.ini`.
2.  Switch `TargetedRHIs` back to `SF_VULKAN_SM6`.
3.  Ensure your GPU drivers (NVIDIA 550+ or latest Mesa) are up to date.

*Note: Always keep a backup of this file, as Unreal updates may try to reset platform-specific overrides.*
