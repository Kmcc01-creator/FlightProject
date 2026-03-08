#!/usr/bin/env bash
# Builds FlightProject C++ targets before launching the editor.
# Usage: ./Scripts/build_targets.sh [Configuration] [--verify]
# Default configuration: Development

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

CONFIGURATION="Development"
SHOULD_VERIFY=0
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --verify)
            SHOULD_VERIFY=1
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

echo
echo "${c_blue}================================================================${c_reset}"
echo "${c_blue}  FlightProject Build Orchestrator${c_reset}"
echo "${c_blue}================================================================${c_reset}"
log_info "Target:        ${c_cyan}FlightProjectEditor${c_reset}"
log_info "Configuration: ${c_cyan}${CONFIGURATION}${c_reset}"
log_info "Auto-Verify:   $( (( SHOULD_VERIFY )) && echo -e "${c_green}ON${c_reset}" || echo -e "${c_yellow}OFF${c_reset}" )"
echo "${c_blue}----------------------------------------------------------------${c_reset}"
echo

"$BUILD_SH" FlightProjectEditor Linux "$CONFIGURATION" -project="$PROJECT_FILE" -game -progress "${EXTRA_ARGS[@]}"

if (( SHOULD_VERIFY )); then
    echo
    log_info "Build successful. Initiating integrity verification..."
    "$SCRIPT_DIR/run_tests_headless.sh" --breaking --timestamps
fi

echo
echo "${c_blue}================================================================${c_reset}"
log_info "${c_green}Build Loop Complete${c_reset}"
echo "${c_blue}================================================================${c_reset}"
echo
