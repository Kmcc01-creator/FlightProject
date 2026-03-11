#!/usr/bin/env bash
# Builds (optional) and launches the FlightProject standalone binary (no editor UI).
# Usage: ./Scripts/run_game.sh [--config Development] [--map /Game/Maps/PersistentFlightTest] [--no-build] [extra game args...]

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

CONFIGURATION="Development"
TARGET_MAP=""
DO_BUILD=1
DO_COOK=1
EXTRA_ARGS=()
VIDEO_BACKEND="${FP_VIDEO_BACKEND:-auto}"
SESSION_WRAPPER="${FP_SESSION_WRAPPER:-auto}"
USE_GAMESCOPE=0
GAMESCOPE_ARGS=()
WINDOW_MODE=""
WINDOW_SIZE=""
WINDOW_RES_X=""
WINDOW_RES_Y=""
WINDOW_LOG_MESSAGE=""
VULKAN_VALIDATION_ARGS=()

parse_window_size() {
    local size="$1"

    if [[ -z "$size" ]]; then
        error "--windowed-size expects WIDTHxHEIGHT (e.g. 1600x900); received empty value"
        exit 1
    fi

    if [[ "$size" =~ ^([0-9]+)x([0-9]+)$ ]]; then
        WINDOW_RES_X="${BASH_REMATCH[1]}"
        WINDOW_RES_Y="${BASH_REMATCH[2]}"

        if (( WINDOW_RES_X < 1 || WINDOW_RES_Y < 1 )); then
            error "--windowed-size values must be positive (got $WINDOW_RES_X x $WINDOW_RES_Y)"
            exit 1
        fi

        WINDOW_LOG_MESSAGE="Windowed launch at ${WINDOW_RES_X}x${WINDOW_RES_Y}"
        return
    fi

    error "--windowed-size expects WIDTHxHEIGHT (e.g. 1600x900); got '$size'"
    exit 1
}

compute_half_window_geometry() {
    local width=""
    local height=""

    if command -v xrandr >/dev/null 2>&1; then
        local xrandr_info
        if xrandr_info=$(xrandr --query 2>/dev/null); then
            local primary_line
            primary_line=$(awk '/ primary / {print $4; exit}' <<<"$xrandr_info")
            if [[ -z "$primary_line" ]]; then
                primary_line=$(awk '/ connected / {print $3; exit}' <<<"$xrandr_info")
            fi

            if [[ -n "$primary_line" ]]; then
                primary_line=${primary_line%%+*}
                if [[ "$primary_line" =~ ^([0-9]+)x([0-9]+)$ ]]; then
                    width="${BASH_REMATCH[1]}"
                    height="${BASH_REMATCH[2]}"
                fi
            fi
        fi
    fi

    if [[ -z "$width" || -z "$height" ]]; then
        width=1920
        height=1080
    fi

    WINDOW_RES_X=$(( (width + 1) / 2 ))
    WINDOW_RES_Y=$(( (height + 1) / 2 ))

    if (( WINDOW_RES_X < 640 )); then
        WINDOW_RES_X=640
    fi

    if (( WINDOW_RES_Y < 360 )); then
        WINDOW_RES_Y=360
    fi

    WINDOW_LOG_MESSAGE="Half-screen windowed launch at ${WINDOW_RES_X}x${WINDOW_RES_Y}"
}

DOTNET_STATE_DIR="$PROJECT_ROOT/Saved/DotNetCli"
mkdir -p "$DOTNET_STATE_DIR"

UE_LOG_DIR="$PROJECT_ROOT/Saved/Logs/AutomationTool"
mkdir -p "$UE_LOG_DIR"
AUTOMATION_LOG_FILE="$UE_LOG_DIR/BuildCookRun-$(date +%Y%m%d-%H%M%S).log"
touch "$AUTOMATION_LOG_FILE"

export uebp_LogFolder="$UE_LOG_DIR"
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

load_gamescope_args GAMESCOPE_ARGS
if is_truthy "${FP_USE_GAMESCOPE:-0}"; then
    USE_GAMESCOPE=1
fi
build_vulkan_validation_args VULKAN_VALIDATION_ARGS

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
        --no-cook)
            DO_COOK=0
            shift
            ;;
        --no-build)
            DO_BUILD=0
            shift
            ;;
        --half-window)
            WINDOW_MODE="half"
            shift
            ;;
        --windowed-size)
            if [[ $# -lt 2 ]]; then
                error "--windowed-size expects WIDTHxHEIGHT (e.g. 1600x900)"
                exit 1
            fi
            WINDOW_MODE="explicit"
            WINDOW_SIZE="$2"
            shift 2
            ;;
        --windowed-size=*)
            WINDOW_MODE="explicit"
            WINDOW_SIZE="${1#*=}"
            shift
            ;;
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
        --session-wrapper)
            if [[ $# -lt 2 ]]; then
                error "--session-wrapper expects a value (auto|uwsm|none)"
                exit 1
            fi
            SESSION_WRAPPER="$2"
            shift 2
            ;;
        --session-wrapper=*)
            SESSION_WRAPPER="${1#*=}"
            shift
            ;;
        --uwsm)
            SESSION_WRAPPER="uwsm"
            shift
            ;;
        --no-session-wrapper)
            SESSION_WRAPPER="none"
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
        -h|--help)
            cat <<EOF
Usage: $(basename "$0") [options] [-- FlightProject args]
  --config/-c <Config>    Build configuration (Debug, Development, Shipping). Default: Development.
  --map/-m <AssetPath>    Map to load when launching (e.g. /Game/Maps/PersistentFlightTest). Defaults to GameDefaultMap.
  --no-cook               Skip asset cooking/staging (requires an existing staged build).
  --no-build              Skip compiling the FlightProject target before launch.
  --half-window           Launch the game windowed at roughly half of the primary monitor resolution.
  --windowed-size WxH     Launch the game windowed at an explicit resolution (e.g. 1600x900).
  --video-backend <mode>  Display backend: auto (default), wayland, or x11.
  --wayland               Shortcut for --video-backend wayland.
  --x11                   Shortcut for --video-backend x11.
  --session-wrapper <m>   Launch wrapper: auto (default), uwsm, or none.
  --uwsm                  Shortcut for --session-wrapper uwsm.
  --no-session-wrapper    Disable session wrapper even if FP_SESSION_WRAPPER is set.
  --gamescope             Wrap launch in gamescope (default flags: --backend wayland --expose-wayland on Wayland sessions).
  --no-gamescope          Disable gamescope wrapper even if FP_USE_GAMESCOPE is set.
  --gamescope-arg <arg>   Append an argument to the gamescope invocation (repeatable).
  --gamescope-arg=<arg>   Append an argument to the gamescope invocation.
  --gamescope-args=a,b    Append comma-separated arguments to gamescope.
  --help/-h               Show this help text.
  --                      Stop parsing options; following args are passed to the FlightProject binary.

All remaining arguments after '--' are forwarded to the FlightProject executable.
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
configure_video_backend "$VIDEO_BACKEND"

LAUNCH_PREFIX=()
build_launch_prefix LAUNCH_PREFIX "$SESSION_WRAPPER" "$USE_GAMESCOPE" GAMESCOPE_ARGS 1

DEFAULT_ENGINE_CONFIG="$PROJECT_ROOT/Config/DefaultEngine.ini"
BUILD_SH=$(resolve_ue_path "Engine/Build/BatchFiles/Linux/Build.sh")
ensure_executable "$BUILD_SH" "Build.sh"

ensure_unrealpak() {
    local unrealpak_binary
    unrealpak_binary=$(resolve_ue_path "Engine/Binaries/Linux/UnrealPak")

    if [[ -x "$unrealpak_binary" ]]; then
        return
    fi

    echo "Compiling UnrealPak utility"
    "$BUILD_SH" UnrealPak Linux Development -NoHotReload

    if [[ ! -x "$unrealpak_binary" ]]; then
        error "UnrealPak executable missing after build at $unrealpak_binary"
        exit 1
    fi
}

DEFAULT_MAP=""
if [[ -f "$DEFAULT_ENGINE_CONFIG" ]]; then
    DEFAULT_MAP=$(awk -F'=' '/^[[:space:]]*GameDefaultMap[[:space:]]*=/{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); last=$2} END{if (last) print last}' "$DEFAULT_ENGINE_CONFIG" | tr -d '\r')
fi

if [[ -z "$TARGET_MAP" && -n "$DEFAULT_MAP" ]]; then
    TARGET_MAP="$DEFAULT_MAP"
fi

if (( DO_BUILD )); then
    DEP_CACHE="$PROJECT_ROOT/Intermediate/Build/Linux/x64/FlightProject/$CONFIGURATION/DependencyCache.bin"
    if [[ -f "$DEP_CACHE" ]]; then
        rm -f "$DEP_CACHE"
    fi

    echo "Compiling FlightProject ($CONFIGURATION)"
    "$BUILD_SH" FlightProject Linux "$CONFIGURATION" -project="$PROJECT_FILE" -game -progress
fi

STAGED_LAUNCH_PATH=""
STAGED_ROOT=""
declare -a STAGE_PLATFORM_CANDIDATES=(
    "LinuxNoEditor"
    "Linux"
)

resolve_staged_launcher() {
    local platform_dir="$1"
    local stage_root="$PROJECT_ROOT/Saved/StagedBuilds/$platform_dir"
    local project_root="$stage_root/FlightProject"

    if [[ ! -d "$project_root" ]]; then
        return 1
    fi

    local launch_sh="$project_root/FlightProject.sh"
    if [[ -x "$launch_sh" ]]; then
        STAGED_LAUNCH_PATH="$launch_sh"
        STAGED_ROOT="$project_root"
        return 0
    fi

    local binaries_dir="$project_root/Binaries/Linux"
    if [[ -d "$binaries_dir" ]]; then
        local binary
        while IFS= read -r binary; do
            STAGED_LAUNCH_PATH="$binary"
            STAGED_ROOT="$project_root"
            return 0
        done < <(find "$binaries_dir" -maxdepth 1 -type f -perm -u+x -name "FlightProject*" | sort)
    fi
    return 1
}

maybe_stage_build=0

for platform in "${STAGE_PLATFORM_CANDIDATES[@]}"; do
    if resolve_staged_launcher "$platform"; then
        break
    fi
done

if [[ -z "$STAGED_LAUNCH_PATH" ]]; then
    maybe_stage_build=1
fi

if (( DO_COOK )) && (( DO_BUILD )); then
    maybe_stage_build=1
fi

if (( DO_COOK )) && [[ -n "$TARGET_MAP" ]]; then
    maybe_stage_build=1
fi

if (( DO_COOK && maybe_stage_build )); then
    RUN_UAT=$(resolve_ue_path "Engine/Build/BatchFiles/RunUAT.sh")
    ensure_executable "$RUN_UAT" "RunUAT.sh"
    ensure_unrealpak

    echo "Cooking and staging FlightProject ($CONFIGURATION) for Linux"
    UAT_CMD=(
        "$RUN_UAT"
        BuildCookRun
        -nocompileuat
        -log="$AUTOMATION_LOG_FILE"
        -project="$PROJECT_FILE"
        -noP4
        -platform=Linux
        -clientconfig="$CONFIGURATION"
        -cook
        -stage
        -pak
        -skipbuild
        -unattended
    )

    if [[ -n "$TARGET_MAP" ]]; then
        UAT_CMD+=("-map=$TARGET_MAP")
    fi

    "${UAT_CMD[@]}"

    STAGED_LAUNCH_PATH=""
    STAGED_ROOT=""
    for platform in "${STAGE_PLATFORM_CANDIDATES[@]}"; do
        if resolve_staged_launcher "$platform"; then
            break
        fi
    done
fi

if [[ -z "$STAGED_LAUNCH_PATH" ]]; then
    if (( DO_COOK )); then
        error "Unable to locate staged Linux build. Try re-running without --no-cook to force staging."
        exit 1
    else
        error "No staged Linux build found under Saved/StagedBuilds and --no-cook was specified."
        exit 1
    fi
fi

if [[ "$WINDOW_MODE" == "explicit" ]]; then
    parse_window_size "$WINDOW_SIZE"
elif [[ "$WINDOW_MODE" == "half" ]]; then
    compute_half_window_geometry
fi

CMD=("${LAUNCH_PREFIX[@]}" "$STAGED_LAUNCH_PATH" "-log")

if [[ ${#VULKAN_VALIDATION_ARGS[@]} -gt 0 ]]; then
    CMD+=("${VULKAN_VALIDATION_ARGS[@]}")
fi

if [[ -n "$WINDOW_RES_X" && -n "$WINDOW_RES_Y" ]]; then
    CMD+=("-windowed" "-ResX=$WINDOW_RES_X" "-ResY=$WINDOW_RES_Y")
fi

if [[ -n "$TARGET_MAP" ]]; then
    CMD+=("$TARGET_MAP")
fi

if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    CMD+=("${EXTRA_ARGS[@]}")
fi

echo "Launching standalone game:"
printf '  %q' "${CMD[@]}"
echo
if [[ -n "$WINDOW_LOG_MESSAGE" ]]; then
    echo "$WINDOW_LOG_MESSAGE"
fi

if [[ -n "$STAGED_ROOT" && -d "$STAGED_ROOT" ]]; then
    (cd "$STAGED_ROOT" && "${CMD[@]}")
else
    "${CMD[@]}"
fi
