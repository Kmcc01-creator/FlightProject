#!/usr/bin/env bash
# Launches UnrealEditor with the FlightProject uproject and per-project editor settings.
# Usage: ./Scripts/run_editor.sh [Extra UnrealEditor args]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

EDITOR_BIN=$(resolve_ue_path "Engine/Binaries/Linux/UnrealEditor")

ensure_project_file
ensure_executable "$EDITOR_BIN" "UnrealEditor binary"

echo "Launching UnrealEditor with $PROJECT_FILE"
"$EDITOR_BIN" "$PROJECT_FILE" "$@"
