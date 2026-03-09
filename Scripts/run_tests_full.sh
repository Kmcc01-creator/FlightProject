#!/bin/bash
# System Verification Path: Requires GPU/Vulkan
# Optimized for pure math/benchmarking with explicit Vulkan extension support.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${PROJECT_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"
if [[ -z "${UE_ROOT:-}" ]]; then
	if [[ -d "$PROJECT_DIR/../../UnrealEngine" ]]; then
		UE_ROOT="$(cd "$PROJECT_DIR/../../UnrealEngine" && pwd)"
	else
		UE_ROOT="$HOME/Unreal/UnrealEngine"
	fi
fi
export UE_ROOT
DDC_PATH="${DDC_PATH:-$PROJECT_DIR/DerivedDataCache}"
LOG_CMDS="${LOG_CMDS:-}"
TEST_LOG_PROFILE="${TEST_LOG_PROFILE:-full}"
TEST_SCOPE="${TEST_SCOPE:-benchmark}"
TEST_COLOR_MODE="${TEST_COLOR_MODE:-auto}" # auto|always|never
TEST_STREAM_FILTER="${TEST_STREAM_FILTER:-all}" # all|errors

mkdir -p "$DDC_PATH"

if [[ -z "$LOG_CMDS" ]]; then
    case "${TEST_LOG_PROFILE,,}" in
        focused|test|ci)
            LOG_CMDS="global error,AutomationTestingLog display,LogAutomationCommandLine display,LogAutomationController display,LogAutomationWorker warning,LogUObjectGlobals error,LogFlightProject display"
            ;;
        verbose)
            LOG_CMDS="global warning,AutomationTestingLog display,LogAutomationCommandLine verbose,LogAutomationController verbose,LogAutomationWorker verbose,LogFlightProject verbose"
            ;;
        full|*)
            LOG_CMDS=""
            ;;
    esac
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
        TEST_FILTER="FlightProject.Perf.GpuPerception"
        ;;
    gpu_smoke|gpu)
        TEST_FILTER="FlightProject.Gpu.Spatial.Perception+FlightProject.Gpu.Spatial.Perception.CallbackResolves+FlightProject.Perf.GpuPerception"
        ;;
    all|full)
        TEST_FILTER="FlightProject"
        ;;
    *)
        echo "Unknown TEST_SCOPE='${TEST_SCOPE}'. Expected one of: benchmark, gpu_smoke, all" >&2
        exit 2
        ;;
esac

EXEC_CMDS="Automation RunTests ${TEST_FILTER}; quit"

echo "Using test scope: ${TEST_SCOPE} (ExecCmds='${EXEC_CMDS}')"

UE_CMD=(
    "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
    "$PROJECT_DIR/FlightProject.uproject"
    -ExecCmds="$EXEC_CMDS"
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
    -Vulkan -RenderOffscreen
    -NoDDCMaintenance
    -DDC=Default -LocalDataCachePath="$DDC_PATH"
    -NoSound -NoVerifyGC
    -vulkanextension="VK_KHR_external_semaphore_fd"
    -vulkanextension="VK_KHR_external_semaphore"
    "${LOG_CMDS_ARGS[@]}"
)

enable_colors=false
case "${TEST_COLOR_MODE,,}" in
    always|on|true|1)
        enable_colors=true
        ;;
    never|off|false|0)
        enable_colors=false
        ;;
    auto|*)
        if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
            enable_colors=true
        fi
        ;;
esac

if [[ "$enable_colors" == true ]]; then
    c_reset=$'\033[0m'
    c_red=$'\033[1;31m'
    c_green=$'\033[1;32m'
    c_yellow=$'\033[1;33m'
    c_cyan=$'\033[1;36m'

    is_error_line() {
        local line="$1"
        case "$line" in
            *"Result={Fail}"*|*"Fatal error"*|*"GIsCriticalError=1"*|*"EXIT CODE: -1"*|*"Unhandled Exception"*|*"Error: "*)
                return 0
                ;;
        esac
        return 1
    }

    is_summary_line() {
        local line="$1"
        case "$line" in
            *"Found "*" automation tests based on "*|*"TEST COMPLETE. EXIT CODE:"*)
                return 0
                ;;
        esac
        return 1
    }

    should_emit_line() {
        local line="$1"
        case "${TEST_STREAM_FILTER,,}" in
            errors|error|fail|failures)
                is_error_line "$line" || is_summary_line "$line"
                return $?
                ;;
            all|*)
                return 0
                ;;
        esac
    }

    colorize_line() {
        local line="$1"
        local color=""

        case "$line" in
            *"Result={Fail}"*|*"Fatal error"*|*"GIsCriticalError=1"*|*"EXIT CODE: -1"*|*"Unhandled Exception"*|*"Error: "*)
                color="$c_red"
                ;;
            *"Result={Success}"*|*"EXIT CODE: 0"*|*"TEST COMPLETE. EXIT CODE: 0"*)
                color="$c_green"
                ;;
            *"Skipping "*|*" skipped"*|*"No automation tests matched"*)
                color="$c_yellow"
                ;;
            *"Found "*" automation tests based on "*)
                color="$c_cyan"
                ;;
        esac

        if should_emit_line "$line"; then
            if [[ -n "$color" ]]; then
                printf '%s%s%s\n' "$color" "$line" "$c_reset"
            else
                printf '%s\n' "$line"
            fi
        fi
    }

    echo "ANSI test log coloring: enabled (TEST_COLOR_MODE=${TEST_COLOR_MODE}, TEST_STREAM_FILTER=${TEST_STREAM_FILTER})"
    stdbuf -oL -eL "${UE_CMD[@]}" 2>&1 < /dev/null | \
        while IFS= read -r line || [[ -n "$line" ]]; do
            colorize_line "$line"
        done
    ue_exit=${PIPESTATUS[0]}
    exit "$ue_exit"
else
    if [[ "${TEST_STREAM_FILTER,,}" =~ ^(errors|error|fail|failures)$ ]]; then
        echo "ANSI test log coloring: disabled (TEST_COLOR_MODE=${TEST_COLOR_MODE}, TEST_STREAM_FILTER=${TEST_STREAM_FILTER})"
        stdbuf -oL -eL "${UE_CMD[@]}" 2>&1 < /dev/null | \
            while IFS= read -r line || [[ -n "$line" ]]; do
                case "$line" in
                    *"Result={Fail}"*|*"Fatal error"*|*"GIsCriticalError=1"*|*"EXIT CODE: -1"*|*"Unhandled Exception"*|*"Error: "*|*"Found "*" automation tests based on "*|*"TEST COMPLETE. EXIT CODE:"*)
                        printf '%s\n' "$line"
                        ;;
                esac
            done
        ue_exit=${PIPESTATUS[0]}
        exit "$ue_exit"
    else
        stdbuf -oL -eL "${UE_CMD[@]}" < /dev/null
    fi
fi
