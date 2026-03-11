#!/usr/bin/env bash
# System Verification Path: Requires GPU/Vulkan
# Runs only GPU-required automation scopes; CPU-safe GPU-domain tests stay in the headless lane.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

PROJECT_DIR="${PROJECT_DIR:-$PROJECT_ROOT}"
DDC_PATH="${DDC_PATH:-$PROJECT_DIR/DerivedDataCache}"
LOG_CMDS="${LOG_CMDS:-}"
TEST_LOG_PROFILE="${TEST_LOG_PROFILE:-$FP_TEST_LOG_PROFILE}"
TEST_PRESET="${TEST_PRESET:-$FP_TEST_PRESET}"
TEST_SCOPE="${TEST_SCOPE:-benchmark}"
TEST_COLOR_MODE="${TEST_COLOR_MODE:-$FP_SCRIPT_COLOR_MODE}" # auto|always|never
TEST_OUTPUT_MODE="${TEST_OUTPUT_MODE:-${TEST_STREAM_FILTER:-errors}}" # all|errors|summary|automation
TEST_FORCE_VULKAN_EXTENSIONS="${TEST_FORCE_VULKAN_EXTENSIONS:-0}"
TEST_VIDEO_BACKEND="${TEST_VIDEO_BACKEND:-${FP_VIDEO_BACKEND:-auto}}" # auto|wayland|x11|dummy
TEST_RENDER_OFFSCREEN="${TEST_RENDER_OFFSCREEN:-1}" # 1|0
TEST_SESSION_WRAPPER="${TEST_SESSION_WRAPPER:-${FP_SESSION_WRAPPER:-auto}}" # auto|uwsm|none
TEST_USE_GAMESCOPE="${TEST_USE_GAMESCOPE:-${FP_USE_GAMESCOPE:-0}}" # 1|0
TEST_GAMESCOPE_ARGS_RAW="${TEST_GAMESCOPE_ARGS:-${FP_GAMESCOPE_ARGS:-}}"
TEST_EXTRA_LOG_CMDS="${TEST_EXTRA_LOG_CMDS:-$FP_TEST_EXTRA_LOG_CMDS}"
TEST_INCLUDE_PYTHON_LOGS="${TEST_INCLUDE_PYTHON_LOGS:-0}"
TEST_INCLUDE_STARTUP_LOGS="${TEST_INCLUDE_STARTUP_LOGS:-0}"
ENABLE_TIMESTAMPS="${FP_SCRIPT_TIMESTAMPS}"
TEST_VK_VALIDATION="${TEST_VK_VALIDATION:-${FP_VK_VALIDATION:-0}}" # 0-5
TEST_VK_GPU_VALIDATION="${TEST_VK_GPU_VALIDATION:-${FP_VK_GPU_VALIDATION:-0}}" # 1|0
TEST_VK_DEBUG_SYNC="${TEST_VK_DEBUG_SYNC:-${FP_VK_DEBUG_SYNC:-0}}" # 1|0
TEST_VK_BEST_PRACTICES="${TEST_VK_BEST_PRACTICES:-${FP_VK_BEST_PRACTICES:-0}}" # 1|0
TEST_VK_DEBUG_UTILS="${TEST_VK_DEBUG_UTILS:-${FP_VK_DEBUG_UTILS:-0}}" # 1|0
TEST_GPU_VALIDATION_PRESET="${TEST_GPU_VALIDATION_PRESET:-local-radv}" # local-radv|off
TEST_VK_DRIVER_FILES="${TEST_VK_DRIVER_FILES:-}"
TEST_VK_ICD_FILENAMES="${TEST_VK_ICD_FILENAMES:-}"
TEST_VK_LOADER_LAYERS_DISABLE="${TEST_VK_LOADER_LAYERS_DISABLE:-}"
DEFAULT_RADV_ICD="/usr/share/vulkan/icd.d/radeon_icd.x86_64.json"
GPU_BENCHMARK_FILTER="FlightProject.Perf.GpuPerception"
GPU_SMOKE_FILTER="FlightProject.Gpu.Spatial.Perception+FlightProject.Gpu.Spatial.Perception.CallbackResolves"
GPU_SWARM_FILTER="FlightProject.Gpu.Swarm.Pipeline.FullIntegration+FlightProject.Gpu.Swarm.Persistence.Stateless+FlightProject.Gpu.Swarm.Persistence.Persistent"
GPU_REQUIRED_FILTER="${GPU_SMOKE_FILTER}+${GPU_SWARM_FILTER}+${GPU_BENCHMARK_FILTER}"
GPU_DOMAIN_FILTER="FlightProject.Gpu.ScriptBridge.DeferredCompletion+FlightProject.Gpu.Reactive"
TEST_GAMESCOPE_ARGS=()
VULKAN_VALIDATION_ARGS=()
AUTOMATION_READY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset=*)
            TEST_PRESET="${1#*=}"
            shift
            ;;
        --preset)
            if [[ $# -lt 2 ]]; then echo "Error: --preset expects a value"; exit 1; fi
            TEST_PRESET="$2"
            shift 2
            ;;
        --profile=*)
            TEST_LOG_PROFILE="${1#*=}"
            shift
            ;;
        --profile)
            if [[ $# -lt 2 ]]; then echo "Error: --profile expects a value"; exit 1; fi
            TEST_LOG_PROFILE="$2"
            shift 2
            ;;
        --output=*)
            TEST_OUTPUT_MODE="${1#*=}"
            shift
            ;;
        --output)
            if [[ $# -lt 2 ]]; then echo "Error: --output expects a value"; exit 1; fi
            TEST_OUTPUT_MODE="$2"
            shift 2
            ;;
        --automation-only)
            TEST_OUTPUT_MODE="automation"
            shift
            ;;
        --summary)
            TEST_OUTPUT_MODE="summary"
            shift
            ;;
        --errors-only)
            TEST_OUTPUT_MODE="errors"
            shift
            ;;
        --all-output)
            TEST_OUTPUT_MODE="all"
            shift
            ;;
        --show-python)
            TEST_INCLUDE_PYTHON_LOGS=1
            shift
            ;;
        --show-startup)
            TEST_INCLUDE_STARTUP_LOGS=1
            shift
            ;;
        --extra-log-cmds=*)
            TEST_EXTRA_LOG_CMDS=$(append_csv_value "$TEST_EXTRA_LOG_CMDS" "${1#*=}")
            shift
            ;;
        --extra-log-cmds)
            if [[ $# -lt 2 ]]; then echo "Error: --extra-log-cmds expects a value"; exit 1; fi
            TEST_EXTRA_LOG_CMDS=$(append_csv_value "$TEST_EXTRA_LOG_CMDS" "$2")
            shift 2
            ;;
        --timestamps)
            ENABLE_TIMESTAMPS=1
            shift
            ;;
        --no-timestamps)
            ENABLE_TIMESTAMPS=0
            shift
            ;;
        --scope=*)
            TEST_SCOPE="${1#*=}"
            shift
            ;;
        --scope)
            if [[ $# -lt 2 ]]; then echo "Error: --scope expects a value"; exit 1; fi
            TEST_SCOPE="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

ensure_project_file
load_gamescope_args TEST_GAMESCOPE_ARGS "$TEST_GAMESCOPE_ARGS_RAW"
build_vulkan_validation_args VULKAN_VALIDATION_ARGS "$TEST_VK_VALIDATION" "$TEST_VK_GPU_VALIDATION" "$TEST_VK_DEBUG_SYNC" "$TEST_VK_BEST_PRACTICES" "$TEST_VK_DEBUG_UTILS"

if [[ -n "$TEST_PRESET" ]]; then
    IFS=$'\t' read -r TEST_LOG_PROFILE TEST_OUTPUT_MODE TEST_INCLUDE_PYTHON_LOGS TEST_INCLUDE_STARTUP_LOGS ENABLE_TIMESTAMPS TEST_EXTRA_LOG_CMDS <<< "$(apply_test_preset_overrides \
        "$TEST_PRESET" \
        "$TEST_LOG_PROFILE" \
        "$TEST_OUTPUT_MODE" \
        "$TEST_INCLUDE_PYTHON_LOGS" \
        "$TEST_INCLUDE_STARTUP_LOGS" \
        "$ENABLE_TIMESTAMPS" \
        "$TEST_EXTRA_LOG_CMDS")"
fi

mkdir -p "$DDC_PATH"

if [[ -z "$LOG_CMDS" ]]; then
    if is_truthy "$TEST_INCLUDE_PYTHON_LOGS"; then
        TEST_EXTRA_LOG_CMDS=$(append_csv_value "$TEST_EXTRA_LOG_CMDS" "LogPython display")
    fi
    TEST_EXTRA_LOG_CMDS=$(append_csv_value "$TEST_EXTRA_LOG_CMDS" "LogVulkanRHI warning")
    TEST_EXTRA_LOG_CMDS=$(append_csv_value "$TEST_EXTRA_LOG_CMDS" "LogTrace warning")
    LOG_CMDS=$(build_test_log_cmds "$TEST_LOG_PROFILE" "$TEST_EXTRA_LOG_CMDS")
fi

LOG_CMDS_ARGS=()
if [[ -n "$LOG_CMDS" ]]; then
    echo "Using test log profile: ${TEST_LOG_PROFILE} (LOG_CMDS='${LOG_CMDS}')"
    LOG_CMDS_ARGS+=("-LogCmds=$LOG_CMDS")
else
    echo "Using test log profile: full (engine default log output)"
fi

case "${TEST_SCOPE,,}" in
    benchmark)
        TEST_FILTER="$GPU_BENCHMARK_FILTER"
        ;;
    gpu_smoke|gpu)
        TEST_FILTER="$GPU_SMOKE_FILTER"
        ;;
    swarm)
        TEST_FILTER="$GPU_SWARM_FILTER"
        ;;
    gpu_domain|domain)
        TEST_FILTER="$GPU_DOMAIN_FILTER"
        ;;
    gpu_required|required|all|full)
        TEST_FILTER="$GPU_REQUIRED_FILTER"
        ;;
    *)
        echo "Unknown TEST_SCOPE='${TEST_SCOPE}'. Expected one of: benchmark, gpu_smoke, swarm, gpu_domain, gpu_required, all" >&2
        exit 2
        ;;
esac

EXEC_CMDS="Automation RunTests ${TEST_FILTER}; quit"

print_banner "FlightProject GPU Test Runner"
echo "  Scope: ${c_cyan}${TEST_SCOPE}${c_reset}"
echo "  Filter: ${c_cyan}${TEST_FILTER}${c_reset}"
echo "  Profile: ${c_cyan}${TEST_LOG_PROFILE}${c_reset}"
echo "  Output: ${c_cyan}${TEST_OUTPUT_MODE}${c_reset}"
echo "  Session Wrapper: ${c_cyan}${TEST_SESSION_WRAPPER}${c_reset}"
echo "  Gamescope: ${c_cyan}${TEST_USE_GAMESCOPE}${c_reset}"
echo "  Vulkan Validation: ${c_cyan}${TEST_VK_VALIDATION}${c_reset}"
if [[ -n "$TEST_PRESET" ]]; then
    echo "  Preset: ${c_cyan}${TEST_PRESET}${c_reset}"
fi
echo "  Timestamps: ${c_cyan}$( is_truthy "$ENABLE_TIMESTAMPS" && echo true || echo false )${c_reset}"
if is_truthy "$TEST_INCLUDE_PYTHON_LOGS"; then
    echo "  Python Logs: ${c_cyan}enabled${c_reset}"
fi
if is_truthy "$TEST_INCLUDE_STARTUP_LOGS"; then
    echo "  Startup Logs: ${c_cyan}enabled${c_reset}"
fi
print_rule

case "${TEST_GPU_VALIDATION_PRESET,,}" in
    local-radv|default)
        if [[ -z "${TEST_VK_DRIVER_FILES}" && -z "${TEST_VK_ICD_FILENAMES}" && -f "${DEFAULT_RADV_ICD}" ]]; then
            TEST_VK_DRIVER_FILES="${DEFAULT_RADV_ICD}"
        fi
        if [[ -z "${TEST_VK_LOADER_LAYERS_DISABLE}" ]]; then
            TEST_VK_LOADER_LAYERS_DISABLE="~implicit~"
        fi
        echo "Using GPU validation preset: local-radv"
        ;;
    off|none|disabled)
        echo "Using GPU validation preset: off"
        ;;
    *)
        echo "Unknown TEST_GPU_VALIDATION_PRESET='${TEST_GPU_VALIDATION_PRESET}'. Expected one of: local-radv, off" >&2
        exit 2
        ;;
esac

case "${TEST_VIDEO_BACKEND,,}" in
    auto)
        echo "Using video backend: auto (SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-<unset>})"
        ;;
    wayland|x11)
        configure_video_backend "${TEST_VIDEO_BACKEND,,}"
        echo "Forcing video backend: ${SDL_VIDEODRIVER:-${TEST_VIDEO_BACKEND,,}}"
        ;;
    dummy)
        export SDL_VIDEODRIVER="dummy"
        echo "Forcing video backend: ${SDL_VIDEODRIVER}"
        ;;
    *)
        echo "Unknown TEST_VIDEO_BACKEND='${TEST_VIDEO_BACKEND}'. Expected one of: auto, wayland, x11, dummy" >&2
        exit 2
        ;;
esac

if [[ -n "${TEST_VK_DRIVER_FILES}" ]]; then
    export VK_DRIVER_FILES="${TEST_VK_DRIVER_FILES}"
    echo "Forcing VK_DRIVER_FILES=${VK_DRIVER_FILES}"
else
    echo "Using VK_DRIVER_FILES=${VK_DRIVER_FILES:-<unset>}"
fi

if [[ -n "${TEST_VK_ICD_FILENAMES}" ]]; then
    export VK_ICD_FILENAMES="${TEST_VK_ICD_FILENAMES}"
    echo "Forcing VK_ICD_FILENAMES=${VK_ICD_FILENAMES}"
else
    echo "Using VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-<unset>}"
fi

if [[ -n "${TEST_VK_LOADER_LAYERS_DISABLE}" ]]; then
    export VK_LOADER_LAYERS_DISABLE="${TEST_VK_LOADER_LAYERS_DISABLE}"
    echo "Forcing VK_LOADER_LAYERS_DISABLE=${VK_LOADER_LAYERS_DISABLE}"
else
    echo "Using VK_LOADER_LAYERS_DISABLE=${VK_LOADER_LAYERS_DISABLE:-<unset>}"
fi

RENDER_ARGS=()
GRAPHICAL_SESSION_REQUIRED=1
case "${TEST_RENDER_OFFSCREEN,,}" in
    1|true|yes|on)
        echo "Render mode: offscreen"
        RENDER_ARGS+=("-RenderOffscreen")
        GRAPHICAL_SESSION_REQUIRED=0
        ;;
    0|false|no|off)
        echo "Render mode: onscreen/window-backed"
        ;;
    *)
        echo "Unknown TEST_RENDER_OFFSCREEN='${TEST_RENDER_OFFSCREEN}'. Expected 1 or 0" >&2
        exit 2
        ;;
esac

VULKAN_EXTENSION_ARGS=()
case "${TEST_FORCE_VULKAN_EXTENSIONS,,}" in
    1|true|yes|on)
        echo "Forcing Vulkan semaphore extensions via command line"
        VULKAN_EXTENSION_ARGS+=("-vulkanextension=VK_KHR_external_semaphore_fd")
        VULKAN_EXTENSION_ARGS+=("-vulkanextension=VK_KHR_external_semaphore")
        ;;
    *)
        echo "Using project/plugin Vulkan extension registration only"
        ;;
esac

LAUNCH_PREFIX=()
build_launch_prefix LAUNCH_PREFIX "$TEST_SESSION_WRAPPER" "$TEST_USE_GAMESCOPE" TEST_GAMESCOPE_ARGS "$GRAPHICAL_SESSION_REQUIRED"
if [[ ${#LAUNCH_PREFIX[@]} -gt 0 ]]; then
    printf 'Launch prefix:'
    printf ' %q' "${LAUNCH_PREFIX[@]}"
    printf '\n'
else
    echo "Launch prefix: direct"
fi

if [[ ${#VULKAN_VALIDATION_ARGS[@]} -gt 0 ]]; then
    printf 'Vulkan validation args:'
    printf ' %q' "${VULKAN_VALIDATION_ARGS[@]}"
    printf '\n'
else
    echo "Vulkan validation args: disabled"
fi

UE_CMD=(
    "${LAUNCH_PREFIX[@]}"
    "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
    "$PROJECT_DIR/FlightProject.uproject"
    -ExecCmds="$EXEC_CMDS"
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
    -Vulkan
    "${RENDER_ARGS[@]}"
    -NoDDCMaintenance
    -DDC=Default -LocalDataCachePath="$DDC_PATH"
    -NoSound -NoVerifyGC
    "${VULKAN_VALIDATION_ARGS[@]}"
    "${VULKAN_EXTENSION_ARGS[@]}"
    "${LOG_CMDS_ARGS[@]}"
)

enable_colors=false
if should_use_color "$TEST_COLOR_MODE"; then
    enable_colors=true
fi

is_error_line() {
    local line="$1"
    [[ "$line" == *"Result={Fail}"* || "$line" == *"Fatal error"* || "$line" == *"GIsCriticalError=1"* || "$line" == *"EXIT CODE: -1"* || "$line" == *"Unhandled Exception"* || "$line" == *"Error: "* ]]
}

is_summary_line() {
    local line="$1"
    [[ "$line" == *"Found "*" automation tests based on "* || "$line" == *"TEST COMPLETE. EXIT CODE:"* || "$line" == *"Test Completed. Result="* || "$line" == *"No automation tests matched"* ]]
}

is_automation_line() {
    local line="$1"
    [[ "$line" == *"Automation:"* || "$line" == *"LogAutomation"* || "$line" == *"AutomationTestingLog:"* ]]
}

is_python_line() {
    local line="$1"
    [[ "$line" == *"LogPython:"* ]]
}

update_output_state() {
    local line="$1"
    if [[ "$line" == *"Ready to start automation"* || "$line" == *"Automation: RunTests="* || "$line" == *"Found "*" automation tests based on "* ]]; then
        AUTOMATION_READY=1
    fi
}

should_emit_line() {
    local line="$1"
    case "${TEST_OUTPUT_MODE,,}" in
        errors|error|fail|failures)
            is_error_line "$line" || is_summary_line "$line"
            return $?
            ;;
        summary)
            is_error_line "$line" || is_summary_line "$line"
            return $?
            ;;
        automation)
            if is_error_line "$line"; then
                return 0
            fi
            if is_summary_line "$line"; then
                return 0
            fi
            if [[ "$AUTOMATION_READY" -eq 1 ]] && is_automation_line "$line"; then
                return 0
            fi
            if is_truthy "$TEST_INCLUDE_PYTHON_LOGS" && is_python_line "$line"; then
                return 0
            fi
            if is_truthy "$TEST_INCLUDE_STARTUP_LOGS" && [[ "$AUTOMATION_READY" -eq 0 ]]; then
                return 0
            fi
            return 1
            ;;
        all|*)
            return 0
            ;;
    esac
}

emit_line() {
    local line="$1"
    local color=""
    local ts
    ts=$(render_timestamp_prefix "$ENABLE_TIMESTAMPS")

    case "$line" in
        *"Result={Fail}"*|*"Fatal error"*|*"Error: "*) color="$c_red" ;;
        *"Result={Success}"*|*"TEST COMPLETE. EXIT CODE: 0"*) color="$c_green" ;;
        *"Skipping "*|*"No automation tests matched"*) color="$c_yellow" ;;
        *"Found "*" automation tests"*) color="$c_cyan" ;;
        *"[RUNNING]"*) color="$c_blue" ;;
    esac

    update_output_state "$line"

    if should_emit_line "$line"; then
        if [[ "$enable_colors" == true && -n "$color" ]]; then
            printf '%s%s%s%s\n' "$ts" "$color" "$line" "$c_reset"
        else
            printf '%s%s\n' "$ts" "$line"
        fi
    fi
}

echo "ANSI test log coloring: $( [[ "$enable_colors" == true ]] && echo enabled || echo disabled ) (TEST_COLOR_MODE=${TEST_COLOR_MODE}, TEST_OUTPUT_MODE=${TEST_OUTPUT_MODE})"
stdbuf -oL -eL "${UE_CMD[@]}" 2>&1 < /dev/null | \
    while IFS= read -r line || [[ -n "$line" ]]; do
        emit_line "$line"
    done
ue_exit=${PIPESTATUS[0]}

echo
print_rule
if [[ $ue_exit -eq 0 ]]; then
    echo "  ${c_green}Result: PASS${c_reset}"
else
    echo "  ${c_red}Result: FAIL (Exit Code: $ue_exit)${c_reset}"
fi
echo "${c_blue}================================================================${c_reset}"
echo
exit "$ue_exit"
