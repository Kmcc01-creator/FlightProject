#!/usr/bin/env bash
# Builds FlightProject C++ targets before launching the editor.
# Usage: ./Scripts/build_targets.sh [Configuration] [--verify] [--no-uba|--use-uba]
# Default configuration: Development

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

CONFIGURATION="Development"
SHOULD_VERIFY=0
FORCE_USE_UBA=0
VERIFY_TEST_PRESET="${VERIFY_TEST_PRESET:-${FP_TEST_PRESET:-triage}}"
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --verify)
            SHOULD_VERIFY=1
            shift
            ;;
        --verify-preset=*)
            VERIFY_TEST_PRESET="${1#*=}"
            shift
            ;;
        --verify-preset)
            if [[ $# -lt 2 ]]; then
                error "--verify-preset expects a value (quiet|triage|startup-debug|full-debug)"
                exit 1
            fi
            VERIFY_TEST_PRESET="$2"
            shift 2
            ;;
        --no-uba)
            EXTRA_ARGS+=("-NoUBA")
            shift
            ;;
        --use-uba)
            FORCE_USE_UBA=1
            shift
            ;;
        Debug|Development|Shipping)
            CONFIGURATION="$1"
            shift
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

BUILD_SH=$(resolve_ue_path "Engine/Build/BatchFiles/Linux/Build.sh")

ensure_project_file
ensure_executable "$BUILD_SH" "Build.sh"

# Keep DotNet state local to the project so sandboxed terminals do not need write access in $HOME.
DOTNET_STATE_DIR="$PROJECT_ROOT/Saved/DotNetCli"
mkdir -p "$DOTNET_STATE_DIR"
export DOTNET_CLI_HOME="${DOTNET_CLI_HOME:-$DOTNET_STATE_DIR}"
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export DOTNET_NOLOGO=1

if [[ -z "${MSBUILDWARNNOTASERROR:-}" ]]; then
    export MSBUILDWARNNOTASERROR="NU1901;NU1902;NU1903"
elif [[ "$MSBUILDWARNNOTASERROR" != *NU1903* ]]; then
    export MSBUILDWARNNOTASERROR="${MSBUILDWARNNOTASERROR};NU1901;NU1902;NU1903"
fi

if [[ -z "${NUGET_AUDIT_MODE:-}" ]]; then
    export NUGET_AUDIT_MODE="none"
fi

# Codex sandbox cannot write UBA cache under ~/.epic by default; disable UBA automatically in that environment.
AUTO_DISABLE_UBA=0
if [[ -n "${CODEX_CI:-}" ]]; then
    AUTO_DISABLE_UBA=1
fi
if [[ "${FP_FORCE_NO_UBA:-}" == "1" ]]; then
    AUTO_DISABLE_UBA=1
elif [[ "${FP_FORCE_NO_UBA:-}" == "0" ]]; then
    AUTO_DISABLE_UBA=0
fi
if (( FORCE_USE_UBA )); then
    AUTO_DISABLE_UBA=0
fi

HAS_NO_UBA_ARG=0
for arg in "${EXTRA_ARGS[@]}"; do
    if [[ "$arg" == "-NoUBA" ]]; then
        HAS_NO_UBA_ARG=1
        break
    fi
done

if (( AUTO_DISABLE_UBA && ! HAS_NO_UBA_ARG )); then
    EXTRA_ARGS+=("-NoUBA")
fi

echo
echo "${c_blue}================================================================${c_reset}"
echo "${c_blue}  FlightProject Build Orchestrator${c_reset}"
echo "${c_blue}================================================================${c_reset}"
log_info "Target:        ${c_cyan}FlightProjectEditor${c_reset}"
log_info "Configuration: ${c_cyan}${CONFIGURATION}${c_reset}"
log_info "Auto-Verify:   $( (( SHOULD_VERIFY )) && echo -e "${c_green}ON${c_reset}" || echo -e "${c_yellow}OFF${c_reset}" )"
if (( SHOULD_VERIFY )); then
    log_info "Verify Preset: ${c_cyan}${VERIFY_TEST_PRESET}${c_reset}"
fi
if (( AUTO_DISABLE_UBA )); then
    log_info "Build Accel:   ${c_yellow}NoUBA (sandbox-safe mode)${c_reset}"
elif (( FORCE_USE_UBA )); then
    log_info "Build Accel:   ${c_green}UBA forced on${c_reset}"
fi
echo "${c_blue}----------------------------------------------------------------${c_reset}"
echo

"$BUILD_SH" FlightProjectEditor Linux "$CONFIGURATION" -project="$PROJECT_FILE" -game -progress "${EXTRA_ARGS[@]}"

if (( SHOULD_VERIFY )); then
    echo
    log_info "Build successful. Initiating integrity verification..."
    TEST_PRESET="$VERIFY_TEST_PRESET" "$SCRIPT_DIR/run_tests_headless.sh" --breaking --timestamps
fi

echo
echo "${c_blue}================================================================${c_reset}"
log_info "${c_green}Build Loop Complete${c_reset}"
echo "${c_blue}================================================================${c_reset}"
echo
