#!/usr/bin/env bash
# FlightProject helper for regenerating IDE files on Linux/Arch systems.
# Usage: ./Scripts/generate_project_files.sh [-f]
#   -f : forward the -Force flag to overwrite existing project files.
# Notes:
#   * UE_ROOT defaults to ~/Unreal/UnrealEngine; export UE_ROOT before running
#     to use a different engine checkout.
#   * This script skips Setup.sh, assuming the engine is already bootstrapped.
#   * Extend ARGS to target additional platforms or toolchains as needed.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

GENERATOR_SH=$(resolve_ue_path "GenerateProjectFiles.sh")

ensure_file_exists "$GENERATOR_SH" "GenerateProjectFiles.sh"
ensure_project_file

ARGS=(
    -project="$PROJECT_FILE"
    -game
    -engine
    -Linux
)

if [[ ${1:-} == "-f" ]]; then
    ARGS+=(-Force)
fi

echo "Using UE_ROOT=$UE_ROOT"
echo "Invoking GenerateProjectFiles for $PROJECT_FILE"

"$GENERATOR_SH" "${ARGS[@]}"

echo "Generation finished. Consider running UnrealEditor to validate the module." 
