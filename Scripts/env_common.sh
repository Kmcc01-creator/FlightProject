#!/usr/bin/env bash
# Shared environment helpers for FlightProject scripts.
# shellcheck shell=bash

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(realpath "$SCRIPT_DIR/..")
PROJECT_FILE="$PROJECT_ROOT/FlightProject.uproject"
UE_ROOT_DEFAULT="$HOME/Unreal/UnrealEngine"

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
