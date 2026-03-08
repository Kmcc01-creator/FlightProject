#!/usr/bin/env bash
# Shared environment helpers for FlightProject scripts.
# shellcheck shell=bash

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(realpath "$SCRIPT_DIR/..")
PROJECT_FILE="$PROJECT_ROOT/FlightProject.uproject"
UE_ROOT_DEFAULT="$HOME/Unreal/UnrealEngine"
DEFAULT_SDL_DYNAMIC_API_PATHS=(
    "/usr/lib/libSDL3.so.0"
    "/usr/lib64/libSDL3.so.0"
    "/usr/lib/libSDL3.so"
    "/usr/lib64/libSDL3.so"
)

export UE_ROOT="${UE_ROOT:-$UE_ROOT_DEFAULT}"
export FP_DEBUG="${FP_DEBUG:-0}"

# --- Colors ---
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
    c_reset=$'\033[0m'
    c_red=$'\033[1;31m'
    c_green=$'\033[1;32m'
    c_yellow=$'\033[1;33m'
    c_cyan=$'\033[1;36m'
    c_blue=$'\033[1;34m'
    c_gray=$'\033[0;90m'
else
    c_reset=""
    c_red=""
    c_green=""
    c_yellow=""
    c_cyan=""
    c_blue=""
    c_gray=""
fi

# --- Logging ---
error() { echo "${c_red}[Error]${c_reset} $*" >&2; }
log_info() { echo "${c_blue}[Info]${c_reset} $*"; }
log_warn() { echo "${c_yellow}[Warn]${c_reset} $*"; }
log_success() { echo "${c_green}[Ok]${c_reset}   $*"; }
log_debug() { if (( FP_DEBUG )); then echo "${c_gray}[Debug]${c_reset} $*"; fi; }

# --- Resource Verification ---
ensure_project_file() {
    if [[ ! -f "$PROJECT_FILE" ]]; then
        error "Project descriptor missing at $PROJECT_FILE"
        exit 1
    fi
}

ensure_executable() {
    local path="$1"
    local description="${2:-$path}"
    if [[ ! -x "$path" ]]; then
        error "$description not found or not executable at $path"
        exit 1
    fi
}

# --- Capability Detection ---
check_linux_capabilities() {
    # Check for io_uring support (Kernel 5.1+ required, 5.10+ recommended)
    local kernel_ver
    kernel_ver=$(uname -r | cut -d. -f1,2)
    if (( $(echo "$kernel_ver < 5.10" | bc -l) )); then
        log_warn "Kernel version $kernel_ver is below recommended 5.10 for io_uring performance."
    else
        log_debug "Kernel $kernel_ver: io_uring path optimized."
    fi

    # Check for Gamescope
    if ! command -v gamescope >/dev/null 2>&1; then
        log_debug "Gamescope not found. HDR and Latency-reduction modes will be disabled."
    fi
}

# --- Build Acceleration ---
setup_ccache() {
    if command -v ccache >/dev/null 2>&1; then
        export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
        log_debug "ccache enabled: $CCACHE_DIR"
    fi
}

# --- Process Management ---
ue-kill-zombies() {
    log_info "Cleaning up hanging Unreal processes..."
    pkill -9 -f "UnrealEditor" || true
    pkill -9 -f "UnrealBuildTool" || true
    pkill -9 -f "UnrealTraceServer" || true
    log_success "Cleanup complete."
}

# --- Initialization ---
setup_ccache
check_linux_capabilities

resolve_ue_path() { echo "$UE_ROOT/$1"; }

configure_video_backend() {
    local backend="${1:-auto}"
    case "$backend" in
        wayland)
            export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
            unset SDL_DYNAMIC_API
            export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland;xcb}"
            export GDK_BACKEND="${GDK_BACKEND:-wayland,x11}"
            export CLUTTER_BACKEND="${CLUTTER_BACKEND:-wayland}"
            export SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR="${SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR:-1}"
            export SDL_VIDEO_WAYLAND_PREFER_LIBDECOR="${SDL_VIDEO_WAYLAND_PREFER_LIBDECOR:-1}"
            export SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR="1"
            export SDL_HINT_WAVE_WINDOW_ANIMATIONS="0"
            ;;
        x11) export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}" ;;
    esac
}

export -f ue-kill-zombies
