#!/usr/bin/env bash
# Launches UnrealEditor with the FlightProject uproject and per-project editor settings.
# Usage: ./Scripts/run_editor.sh [Extra UnrealEditor args]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

VIDEO_BACKEND="${FP_VIDEO_BACKEND:-auto}"
USE_GAMESCOPE=0
GAMESCOPE_ARGS=()

if [[ -n "${FP_USE_GAMESCOPE:-}" ]]; then
    case "${FP_USE_GAMESCOPE,,}" in
        1|true|yes|on)
            USE_GAMESCOPE=1
            ;;
    esac
fi

if [[ -n "${FP_GAMESCOPE_ARGS:-}" ]]; then
    # shellcheck disable=SC2206 # intentional word splitting so users can pass a string with spaces.
    GAMESCOPE_ARGS=(${FP_GAMESCOPE_ARGS})
fi

EXTRA_EDITOR_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --video-backend)
            if [[ $# -lt 2 ]]; then
                error "--video-backend expects a value (auto|wayland|x11)"
                exit 1
            fi
            VIDEO_BACKEND="$2"
            shift 2
            ;;
        --video-backend=*)
            VIDEO_BACKEND="${1#*=}"
            shift
            ;;
        --wayland)
            VIDEO_BACKEND="wayland"
            shift
            ;;
        --x11)
            VIDEO_BACKEND="x11"
            shift
            ;;
        --gamescope)
            USE_GAMESCOPE=1
            shift
            ;;
        --no-gamescope)
            USE_GAMESCOPE=0
            shift
            ;;
        --gamescope-arg)
            if [[ $# -lt 2 ]]; then
                error "--gamescope-arg expects a value"
                exit 1
            fi
            USE_GAMESCOPE=1
            GAMESCOPE_ARGS+=("$2")
            shift 2
            ;;
        --gamescope-arg=*)
            USE_GAMESCOPE=1
            GAMESCOPE_ARGS+=("${1#*=}")
            shift
            ;;
        --gamescope-args=*)
            USE_GAMESCOPE=1
            IFS=',' read -r -a _fp_split_gamescope_args <<<"${1#*=}"
            for arg in "${_fp_split_gamescope_args[@]}"; do
                [[ -n "$arg" ]] && GAMESCOPE_ARGS+=("$arg")
            done
            shift
            ;;
        --)
            shift
            EXTRA_EDITOR_ARGS+=("$@")
            break
            ;;
        *)
            EXTRA_EDITOR_ARGS+=("$1")
            shift
            ;;
    esac
done

configure_video_backend "$VIDEO_BACKEND"

LAUNCH_PREFIX=()
if (( USE_GAMESCOPE )); then
    if ! command -v gamescope >/dev/null 2>&1; then
        error "gamescope requested but not found in PATH"
        exit 1
    fi
    if [[ ${#GAMESCOPE_ARGS[@]} -eq 0 ]]; then
        GAMESCOPE_ARGS=(--expose-wayland --prefer-vk)
    fi
    LAUNCH_PREFIX=(gamescope "${GAMESCOPE_ARGS[@]}" --)
fi

EDITOR_BIN=$(resolve_ue_path "Engine/Binaries/Linux/UnrealEditor")

ensure_project_file
ensure_executable "$EDITOR_BIN" "UnrealEditor binary"

CMD=("${LAUNCH_PREFIX[@]}" "$EDITOR_BIN" "$PROJECT_FILE")
if [[ ${#EXTRA_EDITOR_ARGS[@]} -gt 0 ]]; then
    CMD+=("${EXTRA_EDITOR_ARGS[@]}")
fi

echo "Launching UnrealEditor with $PROJECT_FILE"
printf '  %q' "${CMD[@]}"
echo

"${CMD[@]}"
