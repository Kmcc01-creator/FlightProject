#!/usr/bin/env bash
# Launches UnrealEditor with the FlightProject uproject and per-project editor settings.
# Usage: ./Scripts/run_editor.sh [Extra UnrealEditor args]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

VIDEO_BACKEND="${FP_VIDEO_BACKEND:-auto}"
SESSION_WRAPPER="${FP_SESSION_WRAPPER:-auto}"
USE_GAMESCOPE=0
GAMESCOPE_ARGS=()
ENABLE_TRACE=0
ENABLE_GPU_DEBUG=0
ENABLE_TIMESTAMPS=0
EXTRA_EDITOR_ARGS=()
VULKAN_VALIDATION_ARGS=()

load_gamescope_args GAMESCOPE_ARGS
if is_truthy "${FP_USE_GAMESCOPE:-0}"; then
    USE_GAMESCOPE=1
fi
build_vulkan_validation_args VULKAN_VALIDATION_ARGS

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
        --session-wrapper)
            if [[ $# -lt 2 ]]; then
                error "--session-wrapper expects a value (auto|uwsm|none)"
                exit 1
            fi
            SESSION_WRAPPER="$2"
            shift 2
            ;;
        --session-wrapper=*)
            SESSION_WRAPPER="${1#*=}"
            shift
            ;;
        --uwsm)
            SESSION_WRAPPER="uwsm"
            shift
            ;;
        --no-session-wrapper)
            SESSION_WRAPPER="none"
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
        --timestamps)
            ENABLE_TIMESTAMPS=1
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

echo
echo "${c_blue}================================================================${c_reset}"
echo "${c_blue}  FlightProject Editor Launcher${c_reset}"
echo "${c_blue}================================================================${c_reset}"
log_info "Video Backend: ${c_cyan}${VIDEO_BACKEND}${c_reset}"
log_info "Session:       ${c_cyan}${SESSION_WRAPPER}${c_reset}"
log_info "Trace Enabled: ${c_cyan}$(( ENABLE_TRACE ))${c_reset}"
log_info "GPU Debug:     ${c_cyan}$(( ENABLE_GPU_DEBUG ))${c_reset}"
if (( USE_GAMESCOPE )); then
    log_info "Gamescope:     ${c_green}ON${c_reset}"
fi
echo "${c_blue}----------------------------------------------------------------${c_reset}"
echo

LAUNCH_PREFIX=()
build_launch_prefix LAUNCH_PREFIX "$SESSION_WRAPPER" "$USE_GAMESCOPE" GAMESCOPE_ARGS 1

EDITOR_BIN=$(resolve_ue_path "Engine/Binaries/Linux/UnrealEditor")

ensure_project_file
ensure_executable "$EDITOR_BIN" "UnrealEditor binary"

CMD=("${LAUNCH_PREFIX[@]}" "$EDITOR_BIN" "$PROJECT_FILE")

if (( ENABLE_TRACE )); then
    CMD+=("-trace=cpu,gpu,frame,log,bookmark")
fi

if (( ENABLE_GPU_DEBUG )); then
    CMD+=("-d3ddebug" "-gpucrashdebugging")
fi

if [[ ${#VULKAN_VALIDATION_ARGS[@]} -gt 0 ]]; then
    CMD+=("${VULKAN_VALIDATION_ARGS[@]}")
fi

if [[ ${#EXTRA_EDITOR_ARGS[@]} -gt 0 ]]; then
    CMD+=("${EXTRA_EDITOR_ARGS[@]}")
fi

log_info "Command: ${c_gray}${CMD[*]}${c_reset}"
echo

if (( ENABLE_TIMESTAMPS )); then
    "${CMD[@]}" 2>&1 | while IFS= read -r line; do printf '%s %s\n' "$(date +'%H:%M:%S')" "$line"; done
else
    "${CMD[@]}"
fi
