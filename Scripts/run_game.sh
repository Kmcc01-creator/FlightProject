#!/usr/bin/env bash
# Builds (optional) and launches the FlightProject game in standalone mode (no editor UI).
# Usage: ./Scripts/run_game.sh [--config Development] [--map /Game/Maps/PersistentFlightTest] [--no-build] [extra Unreal args...]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

CONFIGURATION="Development"
TARGET_MAP=""
DO_BUILD=1
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--config)
            if [[ $# -lt 2 ]]; then
                error "--config expects a value (Debug/Development/Shipping)"
                exit 1
            fi
            CONFIGURATION="$2"
            shift 2
            ;;
        -m|--map)
            if [[ $# -lt 2 ]]; then
                error "--map expects an Unreal asset path (e.g. /Game/Maps/PersistentFlightTest)"
                exit 1
            fi
            TARGET_MAP="$2"
            shift 2
            ;;
        --no-build)
            DO_BUILD=0
            shift
            ;;
        -h|--help)
            cat <<EOF
Usage: $(basename "$0") [options] [-- UnrealEditor args]
  --config/-c <Config>    Build configuration (Debug, Development, Shipping). Default: Development.
  --map/-m <AssetPath>    Map to load when launching (e.g. /Game/Maps/PersistentFlightTest).
  --no-build              Skip compiling the FlightProject target before launch.
  --help/-h               Show this help text.
  --                      Stop parsing options; following args are passed to UnrealEditor.

All remaining arguments after '--' are forwarded to the Unreal executable.
EOF
            exit 0
            ;;
        --)
            shift
            EXTRA_ARGS+=("$@")
            break
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

ensure_project_file

EDITOR_BIN=$(resolve_ue_path "Engine/Binaries/Linux/UnrealEditor")
ensure_executable "$EDITOR_BIN" "UnrealEditor binary"

if (( DO_BUILD )); then
    BUILD_SH=$(resolve_ue_path "Engine/Build/BatchFiles/Linux/Build.sh")
    ensure_executable "$BUILD_SH" "Build.sh"

    echo "Compiling FlightProject ($CONFIGURATION)"
    "$BUILD_SH" FlightProject Linux "$CONFIGURATION" -project="$PROJECT_FILE" -game -progress
fi

CMD=("$EDITOR_BIN" "$PROJECT_FILE" -game -log)

if [[ -n "$TARGET_MAP" ]]; then
    CMD+=("$TARGET_MAP")
fi

if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    CMD+=("${EXTRA_ARGS[@]}")
fi

echo "Launching standalone game:"
printf '  %q' "${CMD[@]}"
echo
"${CMD[@]}"
