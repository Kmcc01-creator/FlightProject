#!/usr/bin/env bash
# Reports local CPU SIMD capabilities and a practical FlightProject verification baseline.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

OUTPUT_MODE="text"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --json)
            OUTPUT_MODE="json"
            shift
            ;;
        *)
            error "Unknown option: $1"
            error "Usage: ./Scripts/check_simd_baseline.sh [--json]"
            exit 1
            ;;
    esac
done

ARCH=$(uname -m)
LSCPU_OUTPUT=$(lscpu 2>/dev/null || true)
FLAGS_LINE=$(grep -m1 '^flags' /proc/cpuinfo 2>/dev/null || true)
CPU_FLAGS=""
if [[ "$FLAGS_LINE" == *:* ]]; then
    CPU_FLAGS=${FLAGS_LINE#*:}
fi
CPU_FLAGS=$(xargs <<<"$CPU_FLAGS")
read -r -a CPU_FLAG_ARRAY <<<"$CPU_FLAGS"

COMPILER=""
COMPILER_PROBE=""
if command -v clang >/dev/null 2>&1; then
    COMPILER=$(command -v clang)
    COMPILER_PROBE=$("$COMPILER" -march=native -### -x c /dev/null -c 2>&1 || true)
elif command -v gcc >/dev/null 2>&1; then
    COMPILER=$(command -v gcc)
    COMPILER_PROBE=$("$COMPILER" -march=native -Q --help=target 2>/dev/null || true)
fi

has_flag() {
    local needle="$1"
    [[ " $CPU_FLAGS " == *" $needle "* ]]
}

first_match() {
    local text="$1"
    local pattern="$2"
    grep -oE "$pattern" <<<"$text" | head -n1 || true
}

json_escape() {
    local value="$1"
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}
    value=${value//$'\r'/\\r}
    value=${value//$'\t'/\\t}
    printf '%s' "$value"
}

json_string() {
    printf '"%s"' "$(json_escape "$1")"
}

json_bool() {
    if [[ "$1" == "1" ]]; then
        printf 'true'
    else
        printf 'false'
    fi
}

emit_json_string_array() {
    local array_name="$1"
    local -n array_ref="$array_name"
    printf '['
    local first=1
    local item=""
    for item in "${array_ref[@]}"; do
        if (( ! first )); then
            printf ','
        fi
        first=0
        json_string "$item"
    done
    printf ']'
}

target_cpu=$(first_match "$COMPILER_PROBE" '"-target-cpu" "[^"]+"')
target_cpu=${target_cpu#\"-target-cpu\" }
target_cpu=${target_cpu//\"/}

compiler_vector_profile="Unknown"
verification_baseline="Unknown"
host_optimized_target="Unknown"
notes=()
recommendations=()
vendor_id=""
model_name=""

if [[ "$ARCH" == "x86_64" ]]; then
    if has_flag avx2; then
        verification_baseline="x86_64 AVX2/FMA"
        compiler_vector_profile="AVX2-class x86 baseline is available"
    elif has_flag sse4_2; then
        verification_baseline="x86_64 SSE4.2"
        compiler_vector_profile="SSE4.2-class x86 baseline is available"
    else
        verification_baseline="x86_64 scalar-only"
        compiler_vector_profile="No modern x86 SIMD baseline detected beyond legacy SSE"
    fi

    if has_flag avx512f && has_flag avx512bw && has_flag avx512vl; then
        host_optimized_target="x86_64 AVX-512 (F/BW/VL)"
        notes+=("Host advertises AVX-512 foundation plus BW/VL; this is suitable as an extended verification lane, not the default portability baseline.")
    elif has_flag avx2; then
        host_optimized_target="x86_64 AVX2/FMA"
    else
        host_optimized_target="x86_64 SSE4.2"
    fi

    if has_flag avx512vnni || has_flag avx_vnni; then
        notes+=("Host also advertises VNNI-class dot-product features.")
    fi
    if has_flag avx512bf16; then
        notes+=("Host advertises BF16 support for future specialized numeric lanes.")
    fi
    if has_flag xsave && has_flag avx; then
        notes+=("Kernel-exposed flags indicate OS-enabled AVX state management.")
    fi
    recommendations+=("Use AVX2/FMA as the primary x86_64 verification baseline.")
    recommendations+=("Use AVX-512 on this host as an extended or experimental verification lane.")
    recommendations+=("Keep FlightProject's current 4-lane executor classified as VectorShape until ISA-specific lowerers exist.")
elif [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
    verification_baseline="arm64 Neon128x4"
    host_optimized_target="arm64 Neon128x4"
    compiler_vector_profile="AArch64 implies Neon-class vector support"
    notes+=("Add SVE/SVE2 probing separately if FlightProject later grows ARM-specific hardware SIMD backends.")
    recommendations+=("Use Neon128x4 as the primary ARM verification baseline.")
else
    verification_baseline="$ARCH scalar-only"
    host_optimized_target="$ARCH scalar-only"
    compiler_vector_profile="Unknown architecture family for FlightProject SIMD policy"
    recommendations+=("Treat this host as scalar-only until a platform-specific SIMD contract is added.")
fi

flightproject_vector_shape="NativeVector4xFloat"
flightproject_hw_baseline="$verification_baseline"

if [[ -n "$LSCPU_OUTPUT" ]]; then
    model_name=$(grep -m1 '^Model name:' <<<"$LSCPU_OUTPUT" | cut -d: -f2- | xargs || true)
    vendor_id=$(grep -m1 '^Vendor ID:' <<<"$LSCPU_OUTPUT" | cut -d: -f2- | xargs || true)
fi

if [[ "$OUTPUT_MODE" == "json" ]]; then
    printf '{\n'
    printf '  "architecture": %s,\n' "$(json_string "$ARCH")"
    printf '  "vendor": %s,\n' "$(json_string "$vendor_id")"
    printf '  "model": %s,\n' "$(json_string "$model_name")"
    printf '  "compiler": {\n'
    printf '    "path": %s,\n' "$(json_string "$COMPILER")"
    printf '    "targetCpu": %s,\n' "$(json_string "$target_cpu")"
    printf '    "vectorProfile": %s\n' "$(json_string "$compiler_vector_profile")"
    printf '  },\n'
    printf '  "flightProject": {\n'
    printf '    "vectorShapeModel": %s,\n' "$(json_string "$flightproject_vector_shape")"
    printf '    "verificationBaseline": %s,\n' "$(json_string "$verification_baseline")"
    printf '    "hostOptimizedTarget": %s,\n' "$(json_string "$host_optimized_target")"
    printf '    "hardwareSimdTarget": %s\n' "$(json_string "$flightproject_hw_baseline")"
    printf '  },\n'
    printf '  "cpuFlags": '
    emit_json_string_array CPU_FLAG_ARRAY
    printf ',\n'
    printf '  "features": {\n'
    printf '    "sse42": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" sse4_2 "* ]] && echo 1 || echo 0)")"
    printf '    "avx": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx "* ]] && echo 1 || echo 0)")"
    printf '    "avx2": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx2 "* ]] && echo 1 || echo 0)")"
    printf '    "avx512f": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx512f "* ]] && echo 1 || echo 0)")"
    printf '    "avx512bw": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx512bw "* ]] && echo 1 || echo 0)")"
    printf '    "avx512vl": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx512vl "* ]] && echo 1 || echo 0)")"
    printf '    "avx512vnni": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx512_vnni "* || " $CPU_FLAGS " == *" avx512vnni "* ]] && echo 1 || echo 0)")"
    printf '    "avxVnni": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx_vnni "* ]] && echo 1 || echo 0)")"
    printf '    "avx512bf16": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" avx512_bf16 "* || " $CPU_FLAGS " == *" avx512bf16 "* ]] && echo 1 || echo 0)")"
    printf '    "fma": %s,\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" fma "* ]] && echo 1 || echo 0)")"
    printf '    "xsave": %s\n' "$(json_bool "$([[ " $CPU_FLAGS " == *" xsave "* ]] && echo 1 || echo 0)")"
    printf '  },\n'
    printf '  "recommendations": '
    emit_json_string_array recommendations
    printf ',\n'
    printf '  "notes": '
    emit_json_string_array notes
    printf '\n}\n'
    exit 0
fi

print_banner "FlightProject SIMD Baseline Probe"
log_info "Architecture:            ${c_cyan}${ARCH}${c_reset}"
[[ -n "$vendor_id" ]] && log_info "Vendor:                  ${c_cyan}${vendor_id}${c_reset}"
[[ -n "$model_name" ]] && log_info "Model:                   ${c_cyan}${model_name}${c_reset}"
if [[ -n "$COMPILER" ]]; then
    log_info "Compiler Probe:          ${c_cyan}${COMPILER}${c_reset}"
    [[ -n "$target_cpu" ]] && log_info "Compiler Target CPU:     ${c_cyan}${target_cpu}${c_reset}"
fi
print_rule

echo "Detected CPU flags:"
printf '  %s\n' "$CPU_FLAGS"
print_rule

echo "FlightProject baseline:"
printf '  %-24s %s\n' "Vector shape model:" "$flightproject_vector_shape"
printf '  %-24s %s\n' "Verification baseline:" "$verification_baseline"
printf '  %-24s %s\n' "Host-optimized target:" "$host_optimized_target"
printf '  %-24s %s\n' "Hardware SIMD target:" "$flightproject_hw_baseline"
printf '  %-24s %s\n' "Compiler view:" "$compiler_vector_profile"
print_rule

echo "Recommended interpretation:"
for recommendation in "${recommendations[@]}"; do
    printf '  %s\n' "$recommendation"
done

if (( ${#notes[@]} > 0 )); then
    print_rule
    echo "Notes:"
    for note in "${notes[@]}"; do
        printf '  %s\n' "$note"
    done
fi
