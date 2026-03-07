#!/usr/bin/env bash
# Launches UnrealEditor with the FlightProject uproject and per-project editor settings.
# Usage: ./Scripts/run_editor.sh [Extra UnrealEditor args]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

VIDEO_BACKEND="${FP_VIDEO_BACKEND:-auto}"
USE_GAMESCOPE=0
GAMESCOPE_ARGS=()
ENABLE_TRACE=0
ENABLE_GPU_DEBUG=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --video-backend)
            if [[ $# -lt 2 ]]; then
                error "--video-backend expects a value (auto|wayland|x11)"
                exit 1
            fi
            VIDEO_BACKEND="$2"
            shift 2
            ;;
        --video-backend=*)
            VIDEO_BACKEND="${1#*=}"
            shift
            ;;
        --wayland)
            VIDEO_BACKEND="wayland"
            shift
            ;;
        --x11)
            VIDEO_BACKEND="x11"
            shift
            ;;
        --trace)
            ENABLE_TRACE=1
            shift
            ;;
        --debug-gpu)
            ENABLE_GPU_DEBUG=1
            shift
            ;;
        --gamescope)
            USE_GAMESCOPE=1
            shift
            ;;
        --no-gamescope)
            USE_GAMESCOPE=0
            shift
            ;;
        --gamescope-arg)
            if [[ $# -lt 2 ]]; then
                error "--gamescope-arg expects a value"
                exit 1
            fi
            USE_GAMESCOPE=1
            GAMESCOPE_ARGS+=("$2")
            shift 2
            ;;
        --)
            shift
            EXTRA_EDITOR_ARGS+=("$@")
            break
            ;;
        *)
            EXTRA_EDITOR_ARGS+=("$1")
            shift
            ;;
    esac
done

configure_video_backend "$VIDEO_BACKEND"

LAUNCH_PREFIX=()
if (( USE_GAMESCOPE )); then
    if ! command -v gamescope >/dev/null 2>&1; then
        error "gamescope requested but not found in PATH"
        exit 1
    fi
    if [[ ${#GAMESCOPE_ARGS[@]} -eq 0 ]]; then
        GAMESCOPE_ARGS=(--expose-wayland --prefer-vk)
    fi
    LAUNCH_PREFIX=(gamescope "${GAMESCOPE_ARGS[@]}" --)
fi

EDITOR_BIN=$(resolve_ue_path "Engine/Binaries/Linux/UnrealEditor")

ensure_project_file
ensure_executable "$EDITOR_BIN" "UnrealEditor binary"

CMD=("${LAUNCH_PREFIX[@]}" "$EDITOR_BIN" "$PROJECT_FILE")

if (( ENABLE_TRACE )); then
    CMD+=("-trace=cpu,gpu,frame,log,bookmark")
fi

if (( ENABLE_GPU_DEBUG )); then
    # -d3ddebug is the generic flag for RHI validation, -gpucrashdebugging for breadcrumbs
    CMD+=("-d3ddebug" "-gpucrashdebugging")
fi

if [[ ${#EXTRA_EDITOR_ARGS[@]:-0} -gt 0 ]]; then
    CMD+=("${EXTRA_EDITOR_ARGS[@]}")
fi

echo "Launching UnrealEditor with $PROJECT_FILE"
printf '  %q' "${CMD[@]}"
echo

"${CMD[@]}"
