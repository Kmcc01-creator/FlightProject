#!/bin/bash
# System Verification Path: Requires GPU/Vulkan
# Optimized for pure math/benchmarking with explicit Vulkan extension support.

export UE_ROOT=$HOME/Unreal/UnrealEngine
PROJECT_DIR=$HOME/Unreal/Projects/FlightProject

stdbuf -oL -eL $UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd \
    $PROJECT_DIR/FlightProject.uproject \
    -ExecCmds="Automation RunTests FlightProject.Benchmark.GpuPerception; quit" \
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput \
    -Vulkan -RenderOffscreen \
    -NoDDC -NoDDCMaintenance \
    -NoSound -NoVerifyGC \
    -vulkanextension="VK_KHR_external_semaphore_fd" \
    -vulkanextension="VK_KHR_external_semaphore" < /dev/null
