#!/usr/bin/env bash
# Builds FlightProject C++ targets before launching the editor.
# Usage: ./Scripts/build_targets.sh [Configuration]
# Default configuration: Development

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

CONFIGURATION=${1:-Development}
BUILD_SH=$(resolve_ue_path "Engine/Build/BatchFiles/Linux/Build.sh")

ensure_project_file
ensure_executable "$BUILD_SH" "Build.sh"

echo "Building FlightProjectEditor ($CONFIGURATION)"
"$BUILD_SH" FlightProjectEditor Linux "$CONFIGURATION" -project="$PROJECT_FILE" -game -progress
