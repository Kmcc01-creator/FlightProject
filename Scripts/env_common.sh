#!/usr/bin/env bash
# Shared environment helpers for FlightProject scripts.
# shellcheck shell=bash

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(realpath "$SCRIPT_DIR/..")
PROJECT_FILE="$PROJECT_ROOT/FlightProject.uproject"
UE_ROOT_DEFAULT="$HOME/Unreal/UnrealEngine"
DEFAULT_SDL_DYNAMIC_API_PATHS=(
    "/usr/lib/libSDL3.so.0"
    "/usr/lib64/libSDL3.so.0"
    "/usr/lib/libSDL3.so"
    "/usr/lib64/libSDL3.so"
    "/usr/lib/libSDL2.so"
    "/usr/lib64/libSDL2.so"
    "/usr/lib/libSDL2-2.0.so.0"
    "/usr/lib64/libSDL2-2.0.so.0"
)

export UE_ROOT="${UE_ROOT:-$UE_ROOT_DEFAULT}"

error() {
    echo "[Error] $*" >&2
}

ensure_project_file() {
    if [[ ! -f "$PROJECT_FILE" ]]; then
        error "Project descriptor missing at $PROJECT_FILE"
        exit 1
    fi
}

ensure_file_exists() {
    local path="$1"
    local description="${2:-$path}"

    if [[ ! -f "$path" ]]; then
        error "$description not found at $path"
        exit 1
    fi
}

ensure_executable() {
    local path="$1"
    local description="${2:-$path}"

    if [[ ! -x "$path" ]]; then
        error "$description not found or not executable at $path"
        exit 1
    fi
}

resolve_ue_path() {
    local relative="$1"
    echo "$UE_ROOT/$relative"
}

find_sdl_dynamic_api() {
    if [[ -n "${FP_SDL_DYNAMIC_API:-}" && -f "$FP_SDL_DYNAMIC_API" ]]; then
        echo "$FP_SDL_DYNAMIC_API"
        return 0
    fi

    for candidate in "${DEFAULT_SDL_DYNAMIC_API_PATHS[@]}"; do
        if [[ -f "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done

    if command -v sdl3-config >/dev/null 2>&1; then
        local libdir
        libdir=$(sdl3-config --libdir 2>/dev/null || true)
        if [[ -n "$libdir" ]]; then
            local from_config="$libdir/libSDL3.so.0"
            if [[ -f "$from_config" ]]; then
                echo "$from_config"
                return 0
            fi
        fi
    fi

    if command -v sdl2-config >/dev/null 2>&1; then
        local libdir
        libdir=$(sdl2-config --libdir 2>/dev/null || true)
        if [[ -n "$libdir" ]]; then
            local from_config="$libdir/libSDL2.so"
            if [[ -f "$from_config" ]]; then
                echo "$from_config"
                return 0
            fi
        fi
    fi

    return 1
}

configure_video_backend() {
    local backend="${1:-auto}"

    case "$backend" in
        wayland)
            export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
            if [[ -z "${SDL_DYNAMIC_API:-}" ]]; then
                local sdl_path
                if sdl_path=$(find_sdl_dynamic_api); then
                    export SDL_DYNAMIC_API="$sdl_path"
                fi
            fi
            export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland;xcb}"
            export GDK_BACKEND="${GDK_BACKEND:-wayland,x11}"
            export CLUTTER_BACKEND="${CLUTTER_BACKEND:-wayland}"
            # libdecor helps with popup/tooltip positioning on Hyprland
            export SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR="${SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR:-1}"
            export SDL_VIDEO_WAYLAND_PREFER_LIBDECOR="${SDL_VIDEO_WAYLAND_PREFER_LIBDECOR:-1}"
            ;;
        x11)
            export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
            ;;
        auto)
            ;;
        *)
            error "Unknown video backend '$backend' (expected auto|wayland|x11)"
            exit 1
            ;;
    esac
}
