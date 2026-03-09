#!/bin/bash
# Integrity Verification Path: Headless + NullRHI
# Runs the full FlightProject automation tree without requiring a GPU.

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
TEST_COLOR_MODE="${TEST_COLOR_MODE:-auto}" # auto|always|never
TEST_STREAM_FILTER="${TEST_STREAM_FILTER:-all}" # all|errors
TEST_FILTER="${TEST_FILTER:-FlightProject}"
ENABLE_TIMESTAMPS=false

# Parse extra args
EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --breaking)
            TEST_FILTER="FlightProject.Unit.Safety+FlightProject.Integration.SchemaDriven"
            shift
            ;;
        --verse)
            TEST_FILTER="FlightProject.Verse+FlightProject.Integration.Vex.VerticalSlice"
            TEST_LOG_PROFILE="focused"
            EXTRA_ARGS+=("-NoShaderCompile")
            shift
            ;;
        --no-shaders)
            EXTRA_ARGS+=("-NoShaderCompile")
            shift
            ;;
        --timestamps)
            ENABLE_TIMESTAMPS=true
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

mkdir -p "$DDC_PATH"

if [[ -z "$LOG_CMDS" ]]; then
    case "${TEST_LOG_PROFILE,,}" in
        focused|test|ci)
            LOG_CMDS="global error,AutomationTestingLog display,LogAutomationCommandLine display,LogAutomationController display,LogAutomationWorker warning,LogUObjectGlobals error,LogFlightProject display,LogFlightSwarm display,LogFlightVerseSubsystem display"
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
    LOG_CMDS_ARGS+=("-LogCmds=$LOG_CMDS")
fi

UE_CMD=(
    "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
    "$PROJECT_DIR/FlightProject.uproject"
    -ExecCmds="Automation RunTests $TEST_FILTER; quit"
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
    -NullRHI -NoPCH -NoBT -NoSound -NoDDCMaintenance
    -DDC=NoZenLocalFallback -LocalDataCachePath="$DDC_PATH"
    "${LOG_CMDS_ARGS[@]}"
    "${EXTRA_ARGS[@]}"
)

# QoL Coloring
c_reset=$'\033[0m'
c_red=$'\033[1;31m'
c_green=$'\033[1;32m'
c_yellow=$'\033[1;33m'
c_cyan=$'\033[1;36m'
c_blue=$'\033[1;34m'
c_gray=$'\033[0;90m'

get_timestamp() {
    if [[ "$ENABLE_TIMESTAMPS" == true ]]; then
        printf '%s[%s] %s' "$c_gray" "$(date +'%H:%M:%S')" "$c_reset"
    fi
}

echo
echo "${c_blue}================================================================${c_reset}"
echo "${c_blue}  FlightProject Headless Test Runner${c_reset}"
echo "${c_blue}================================================================${c_reset}"
echo "  Filter: ${c_cyan}${TEST_FILTER}${c_reset}"
echo "  Profile: ${c_cyan}${TEST_LOG_PROFILE}${c_reset}"
echo "  Timestamps: ${c_cyan}${ENABLE_TIMESTAMPS}${c_reset}"
echo "${c_blue}----------------------------------------------------------------${c_reset}"
echo

enable_colors=false
case "${TEST_COLOR_MODE,,}" in
    always|on|true|1) enable_colors=true ;;
    never|off|false|0) enable_colors=false ;;
    auto|*) if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then enable_colors=true; fi ;;
esac

is_error_line() {
    local line="$1"
    [[ "$line" == *"Result={Fail}"* || "$line" == *"Fatal error"* || "$line" == *"GIsCriticalError=1"* || "$line" == *"Error: "* ]]
}

is_summary_line() {
    local line="$1"
    [[ "$line" == *"Found "*" automation tests based on "* || "$line" == *"TEST COMPLETE. EXIT CODE:"* ]]
}

should_emit_line() {
    local line="$1"
    if [[ "${TEST_STREAM_FILTER,,}" =~ ^(errors|error|fail|failures)$ ]]; then
        is_error_line "$line" || is_summary_line "$line"
        return $?
    fi
    return 0
}

colorize_line() {
    local line="$1"
    local color=""
    local ts=$(get_timestamp)

    case "$line" in
        *"Result={Fail}"*|*"Fatal error"*|*"Error: "*) color="$c_red" ;;
        *"Result={Success}"*|*"TEST COMPLETE. EXIT CODE: 0"*) color="$c_green" ;;
        *"Skipping "*|*"No automation tests matched"*) color="$c_yellow" ;;
        *"Found "*" automation tests"*) color="$c_cyan" ;;
        *"[RUNNING]"*) color="$c_blue" ;;
    esac

    if should_emit_line "$line"; then
        if [[ -n "$color" ]]; then
            printf '%s%s%s%s\n' "$ts" "$color" "$line" "$c_reset"
        else
            printf '%s%s\n' "$ts" "$line"
        fi
    fi
}

if [[ "$enable_colors" == true ]]; then
    stdbuf -oL -eL "${UE_CMD[@]}" 2>&1 < /dev/null | \
        while IFS= read -r line || [[ -n "$line" ]]; do
            colorize_line "$line"
        done
    ue_exit=${PIPESTATUS[0]}
else
    stdbuf -oL -eL "${UE_CMD[@]}" 2>&1 < /dev/null
    ue_exit=${PIPESTATUS[0]}
fi

echo
echo "${c_blue}----------------------------------------------------------------${c_reset}"
if [[ $ue_exit -eq 0 ]]; then
    echo "  ${c_green}Result: PASS${c_reset}"
else
    echo "  ${c_red}Result: FAIL (Exit Code: $ue_exit)${c_reset}"
fi
echo "${c_blue}================================================================${c_reset}"
echo
exit "$ue_exit"
