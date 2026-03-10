#!/bin/bash
# Integrity Verification Path: Headless + NullRHI
# Runs the full FlightProject automation tree without requiring a GPU.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

PROJECT_DIR="${PROJECT_DIR:-$PROJECT_ROOT}"
DDC_PATH="${DDC_PATH:-$PROJECT_DIR/DerivedDataCache}"
LOG_CMDS="${LOG_CMDS:-}"
TEST_LOG_PROFILE="${TEST_LOG_PROFILE:-$FP_TEST_LOG_PROFILE}"
TEST_COLOR_MODE="${TEST_COLOR_MODE:-$FP_SCRIPT_COLOR_MODE}" # auto|always|never
TEST_OUTPUT_MODE="${TEST_OUTPUT_MODE:-${TEST_STREAM_FILTER:-$FP_TEST_OUTPUT_MODE}}" # all|errors|summary|automation
TEST_FILTER="${TEST_FILTER:-FlightProject}"
TEST_EXTRA_LOG_CMDS="${TEST_EXTRA_LOG_CMDS:-$FP_TEST_EXTRA_LOG_CMDS}"
TEST_INCLUDE_PYTHON_LOGS="${TEST_INCLUDE_PYTHON_LOGS:-0}"
TEST_INCLUDE_STARTUP_LOGS="${TEST_INCLUDE_STARTUP_LOGS:-0}"
ENABLE_TIMESTAMPS="${FP_SCRIPT_TIMESTAMPS}"
TEST_PRESET="${TEST_PRESET:-$FP_TEST_PRESET}"

# Parse extra args
EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all-cpu)
            TEST_FILTER="FlightProject.Unit+FlightProject.Integration+FlightProject.Functional"
            shift
            ;;
        --unit)
            TEST_FILTER="FlightProject.Unit"
            shift
            ;;
        --integration)
            TEST_FILTER="FlightProject.Integration"
            shift
            ;;
        --functional)
            TEST_FILTER="FlightProject.Functional"
            shift
            ;;
        --breaking)
            TEST_FILTER="FlightProject.Unit.Safety+FlightProject.Integration.SchemaDriven"
            shift
            ;;
        --verse)
            TEST_FILTER="FlightProject.Functional.Verse+FlightProject.Integration.Verse"
            TEST_LOG_PROFILE="focused"
            EXTRA_ARGS+=("-NoShaderCompile")
            shift
            ;;
        --simd)
            TEST_FILTER="FlightProject.Integration.Vex.SimdParity"
            TEST_LOG_PROFILE="focused"
            EXTRA_ARGS+=("-NoShaderCompile")
            shift
            ;;
        --no-shaders)
            EXTRA_ARGS+=("-NoShaderCompile")
            shift
            ;;
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
        --filter=*)
            TEST_FILTER="${1#*=}"
            shift
            ;;
        --filter)
            if [[ $# -lt 2 ]]; then echo "Error: --filter expects a value"; exit 1; fi
            TEST_FILTER="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

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
    LOG_CMDS=$(build_test_log_cmds "$TEST_LOG_PROFILE" "$TEST_EXTRA_LOG_CMDS")
fi

LOG_CMDS_ARGS=()
if [[ -n "$LOG_CMDS" ]]; then
    LOG_CMDS_ARGS+=("-LogCmds=$LOG_CMDS")
fi

# Headless automation should not depend on persisted editor privacy settings.
# This also avoids an engine crash path when -NoShaderCompile is active.
EDITOR_SETTINGS_ARGS=(
    "-ini:EditorSettings:[/Script/UnrealEd.AnalyticsPrivacySettings]:bSendUsageData=False"
)

UE_CMD=(
    "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
    "$PROJECT_DIR/FlightProject.uproject"
    -ExecCmds="Automation RunTests $TEST_FILTER; quit"
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
    -NullRHI -NoPCH -NoBT -NoSound -NoDDCMaintenance -NoAnalytics -NoTelemetry -EpicAnalyticsDisable -NoCrashHandler
    -DDC=NoZenLocalFallback -LocalDataCachePath="$DDC_PATH"
    "${LOG_CMDS_ARGS[@]}"
    "${EDITOR_SETTINGS_ARGS[@]}"
    "${EXTRA_ARGS[@]}"
)
print_banner "FlightProject Headless Test Runner"
echo "  Filter: ${c_cyan}${TEST_FILTER}${c_reset}"
echo "  Profile: ${c_cyan}${TEST_LOG_PROFILE}${c_reset}"
echo "  Output: ${c_cyan}${TEST_OUTPUT_MODE}${c_reset}"
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
echo

enable_colors=false
if should_use_color "$TEST_COLOR_MODE"; then
    enable_colors=true
fi

AUTOMATION_READY=0

is_error_line() {
    local line="$1"
    [[ "$line" == *"Result={Fail}"* || "$line" == *"Fatal error"* || "$line" == *"GIsCriticalError=1"* || "$line" == *"Error: "* ]]
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
            is_summary_line "$line"
            return $?
            ;;
        automation)
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
