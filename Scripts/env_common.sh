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
export FP_SCRIPT_COLOR_MODE="${FP_SCRIPT_COLOR_MODE:-auto}"
export FP_SCRIPT_TIMESTAMPS="${FP_SCRIPT_TIMESTAMPS:-0}"
export FP_TEST_LOG_PROFILE="${FP_TEST_LOG_PROFILE:-full}"
export FP_TEST_OUTPUT_MODE="${FP_TEST_OUTPUT_MODE:-all}"
export FP_TEST_EXTRA_LOG_CMDS="${FP_TEST_EXTRA_LOG_CMDS:-}"
export FP_TEST_PRESET="${FP_TEST_PRESET:-}"

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

is_truthy() {
    case "${1:-0}" in
        1|true|TRUE|True|yes|YES|Yes|on|ON|On)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

should_use_color() {
    local mode="${1:-$FP_SCRIPT_COLOR_MODE}"
    case "${mode,,}" in
        always|on|true|1)
            return 0
            ;;
        never|off|false|0)
            return 1
            ;;
        auto|*)
            [[ -t 1 && -z "${NO_COLOR:-}" ]]
            return $?
            ;;
    esac
}

render_timestamp_prefix() {
    local enabled="${1:-$FP_SCRIPT_TIMESTAMPS}"
    if is_truthy "$enabled"; then
        printf '%s[%s] %s' "$c_gray" "$(date +'%H:%M:%S')" "$c_reset"
    fi
}

print_banner() {
    local title="$1"
    echo
    echo "${c_blue}================================================================${c_reset}"
    echo "${c_blue}  ${title}${c_reset}"
    echo "${c_blue}================================================================${c_reset}"
}

print_rule() {
    echo "${c_blue}----------------------------------------------------------------${c_reset}"
}

append_csv_value() {
    local current="$1"
    local value="$2"

    if [[ -z "$value" ]]; then
        printf '%s' "$current"
        return 0
    fi

    if [[ -z "$current" ]]; then
        printf '%s' "$value"
    else
        printf '%s,%s' "$current" "$value"
    fi
}

build_test_log_cmds() {
    local profile="${1:-$FP_TEST_LOG_PROFILE}"
    local extra="${2:-$FP_TEST_EXTRA_LOG_CMDS}"
    local log_cmds=""

    case "${profile,,}" in
        minimal|quiet)
            log_cmds="global error,AutomationTestingLog display,LogAutomationCommandLine display,LogAutomationController display,LogAutomationWorker error,LogUObjectGlobals error"
            ;;
        focused|test|ci)
            log_cmds="global error,AutomationTestingLog display,LogAutomationCommandLine display,LogAutomationController display,LogAutomationWorker warning,LogUObjectGlobals error,LogFlightProject display,LogFlightSwarm display,LogFlightVerseSubsystem display"
            ;;
        python)
            log_cmds="global error,AutomationTestingLog display,LogAutomationCommandLine display,LogAutomationController display,LogAutomationWorker warning,LogUObjectGlobals error,LogFlightProject display,LogFlightSwarm display,LogFlightVerseSubsystem display,LogPython display"
            ;;
        verbose)
            log_cmds="global warning,AutomationTestingLog display,LogAutomationCommandLine verbose,LogAutomationController verbose,LogAutomationWorker verbose,LogFlightProject verbose,LogPython display"
            ;;
        full|*)
            log_cmds=""
            ;;
    esac

    if [[ -n "$extra" ]]; then
        log_cmds=$(append_csv_value "$log_cmds" "$extra")
    fi

    printf '%s' "$log_cmds"
}

test_preset_exists() {
    case "${1,,}" in
        quiet|triage|startup-debug|full-debug)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

resolve_test_preset_value() {
    local preset="${1,,}"
    local field="$2"

    case "$preset" in
        quiet)
            case "$field" in
                profile) printf '%s' "minimal" ;;
                output) printf '%s' "summary" ;;
                python|startup) printf '%s' "0" ;;
                timestamps) printf '%s' "0" ;;
                extra) printf '%s' "" ;;
                desc) printf '%s' "summary-only output with minimal Unreal log categories" ;;
            esac
            ;;
        triage)
            case "$field" in
                profile) printf '%s' "focused" ;;
                output) printf '%s' "errors" ;;
                python|startup) printf '%s' "0" ;;
                timestamps) printf '%s' "1" ;;
                extra) printf '%s' "" ;;
                desc) printf '%s' "error-focused output for normal automation triage" ;;
            esac
            ;;
        startup-debug)
            case "$field" in
                profile) printf '%s' "python" ;;
                output) printf '%s' "automation" ;;
                python|startup|timestamps) printf '%s' "1" ;;
                extra) printf '%s' "" ;;
                desc) printf '%s' "automation view plus Python and startup diagnostics" ;;
            esac
            ;;
        full-debug)
            case "$field" in
                profile) printf '%s' "verbose" ;;
                output) printf '%s' "all" ;;
                python|startup|timestamps) printf '%s' "1" ;;
                extra) printf '%s' "" ;;
                desc) printf '%s' "maximum runner visibility with verbose Unreal logging" ;;
            esac
            ;;
        *)
            printf '%s' ""
            ;;
    esac
}

apply_test_preset_overrides() {
    local preset="${1:-}"
    local current_profile="${2:-}"
    local current_output="${3:-}"
    local current_python="${4:-0}"
    local current_startup="${5:-0}"
    local current_timestamps="${6:-0}"
    local current_extra="${7:-}"

    if [[ -z "$preset" ]]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$current_profile" \
            "$current_output" \
            "$current_python" \
            "$current_startup" \
            "$current_timestamps" \
            "$current_extra"
        return 0
    fi

    if ! test_preset_exists "$preset"; then
        error "Unknown test preset '$preset' (expected: quiet, triage, startup-debug, full-debug)"
        return 1
    fi

    local preset_profile
    local preset_output
    local preset_python
    local preset_startup
    local preset_timestamps
    local preset_extra

    preset_profile=$(resolve_test_preset_value "$preset" "profile")
    preset_output=$(resolve_test_preset_value "$preset" "output")
    preset_python=$(resolve_test_preset_value "$preset" "python")
    preset_startup=$(resolve_test_preset_value "$preset" "startup")
    preset_timestamps=$(resolve_test_preset_value "$preset" "timestamps")
    preset_extra=$(resolve_test_preset_value "$preset" "extra")

    current_extra=$(append_csv_value "$preset_extra" "$current_extra")

    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${preset_profile:-$current_profile}" \
        "${preset_output:-$current_output}" \
        "${preset_python:-$current_python}" \
        "${preset_startup:-$current_startup}" \
        "${preset_timestamps:-$current_timestamps}" \
        "$current_extra"
}

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
export -f is_truthy
export -f should_use_color
export -f render_timestamp_prefix
export -f print_banner
export -f print_rule
export -f append_csv_value
export -f build_test_log_cmds
export -f test_preset_exists
export -f resolve_test_preset_value
export -f apply_test_preset_overrides
