#!/usr/bin/env bash
# Swarm Development Editor Launcher
# Launches the editor with optimized Vulkan extensions for io_uring and Wayland support.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

VIDEO_BACKEND="${FP_VIDEO_BACKEND:-wayland}"
ENABLE_TRACE=0
ENABLE_GPU_DEBUG=0
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --trace)
            ENABLE_TRACE=1
            shift
            ;;
        --debug-gpu)
            ENABLE_GPU_DEBUG=1
            shift
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

configure_video_backend "$VIDEO_BACKEND"

EDITOR_BIN=$(resolve_ue_path "Engine/Binaries/Linux/UnrealEditor")

ensure_project_file
ensure_executable "$EDITOR_BIN" "UnrealEditor binary"

CMD=("$EDITOR_BIN" "$PROJECT_FILE" /Game/Maps/PersistentFlightTest -Vulkan)

# Critical extensions for zero-syscall io_uring integration
CMD+=("-vulkanextension=VK_KHR_external_semaphore_fd")
CMD+=("-vulkanextension=VK_KHR_external_semaphore")
CMD+=("-vulkanextension=VK_KHR_external_memory_fd")
CMD+=("-vulkanextension=VK_KHR_external_memory")

if (( ENABLE_TRACE )); then
    CMD+=("-trace=cpu,gpu,frame,log,bookmark")
fi

if (( ENABLE_GPU_DEBUG )); then
    CMD+=("-d3ddebug" "-gpucrashdebugging")
fi

if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    CMD+=("${EXTRA_ARGS[@]}")
fi

echo "Launching Swarm Development Editor (Wayland=$([[ "$SDL_VIDEODRIVER" == "wayland" ]] && echo "Yes" || echo "No"))"
printf '  %q' "${CMD[@]}"
echo

exec "${CMD[@]}"
