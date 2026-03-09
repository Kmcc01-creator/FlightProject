#!/usr/bin/env bash
# Phased automation runner:
#   Phase 1: Complex/generated + spec
#   Phase 2: Simple automation groups
#   Phase 3: Optional GPU/system pass

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

HEADLESS_RUNNER="$SCRIPT_DIR/run_tests_headless.sh"
FULL_RUNNER="$SCRIPT_DIR/run_tests_full.sh"

ensure_project_file
ensure_executable "$HEADLESS_RUNNER" "run_tests_headless.sh"
ensure_executable "$FULL_RUNNER" "run_tests_full.sh"

PHASE1_FILTER="FlightProject.Integration.SchemaDriven+FlightProject.Integration.Vex.VerticalSlice+FlightProject.Integration.Concurrency+FlightProject.Unit.Safety.MemoryLayout+FlightProject.Vex.Parser.Spec"
PHASE2_FILTER="FlightProject.Schema+FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder+FlightProject.Vex.Parsing+FlightProject.Vex.Simd+FlightProject.Vex.UI+FlightProject.Verse+FlightProject.Verse.Bytecode+FlightProject.AutoRTFM+FlightProject.Gpu.Reactive+FlightProject.Logging+FlightProject.Swarm.Pipeline+FlightProject.Spatial.GpuPerception+FlightProject.Benchmark.GpuPerception+FlightProject.Reactive+FlightProject.Reflection+FlightProject.Functional"

RUN_PHASE1=1
RUN_PHASE2=1
RUN_PHASE3=0
ENABLE_TIMESTAMPS=1
TEST_STREAM_FILTER_VALUE="${TEST_STREAM_FILTER:-errors}"
TEST_COLOR_MODE_VALUE="${TEST_COLOR_MODE:-auto}"
GPU_SCOPE="${TEST_SCOPE:-all}"

usage() {
    cat <<'USAGE'
Usage: ./Scripts/run_tests_phased.sh [options]

Options:
  --with-gpu              Run Phase 3 GPU/system suite after Phases 1+2.
  --gpu-scope <scope>     Scope for run_tests_full.sh (benchmark|gpu_smoke|all). Default: all.
  --phase1-only           Run only Phase 1.
  --phase2-only           Run only Phase 2.
  --phase3-only           Run only Phase 3 (implies --with-gpu).
  --skip-phase1           Skip Phase 1.
  --skip-phase2           Skip Phase 2.
  --skip-phase3           Skip Phase 3.
  --stream <mode>         Log stream mode for child scripts (errors|all). Default: errors.
  --timestamps            Enable timestamps in child scripts (default).
  --no-timestamps         Disable timestamps in child scripts.
  -h, --help              Show this help text.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-gpu)
            RUN_PHASE3=1
            shift
            ;;
        --gpu-scope)
            if [[ $# -lt 2 ]]; then
                error "--gpu-scope expects a value (benchmark|gpu_smoke|all)"
                exit 1
            fi
            GPU_SCOPE="$2"
            shift 2
            ;;
        --phase1-only)
            RUN_PHASE1=1
            RUN_PHASE2=0
            RUN_PHASE3=0
            shift
            ;;
        --phase2-only)
            RUN_PHASE1=0
            RUN_PHASE2=1
            RUN_PHASE3=0
            shift
            ;;
        --phase3-only)
            RUN_PHASE1=0
            RUN_PHASE2=0
            RUN_PHASE3=1
            shift
            ;;
        --skip-phase1)
            RUN_PHASE1=0
            shift
            ;;
        --skip-phase2)
            RUN_PHASE2=0
            shift
            ;;
        --skip-phase3)
            RUN_PHASE3=0
            shift
            ;;
        --stream)
            if [[ $# -lt 2 ]]; then
                error "--stream expects a value (errors|all)"
                exit 1
            fi
            TEST_STREAM_FILTER_VALUE="$2"
            shift 2
            ;;
        --timestamps)
            ENABLE_TIMESTAMPS=1
            shift
            ;;
        --no-timestamps)
            ENABLE_TIMESTAMPS=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if [[ "$RUN_PHASE1" -eq 0 && "$RUN_PHASE2" -eq 0 && "$RUN_PHASE3" -eq 0 ]]; then
    error "No phases selected."
    usage
    exit 1
fi

TS_ARGS=()
if [[ "$ENABLE_TIMESTAMPS" -eq 1 ]]; then
    TS_ARGS+=(--timestamps)
fi

PHASE1_STATUS=0
PHASE2_STATUS=0
PHASE3_STATUS=0

run_headless_phase() {
    local phase_name="$1"
    local phase_filter="$2"
    log_info "Starting ${phase_name}"
    set +e
    TEST_STREAM_FILTER="$TEST_STREAM_FILTER_VALUE" TEST_COLOR_MODE="$TEST_COLOR_MODE_VALUE" \
        "$HEADLESS_RUNNER" "${TS_ARGS[@]}" --filter="$phase_filter"
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        log_success "${phase_name} passed"
    else
        error "${phase_name} failed (exit $status)"
    fi
    return "$status"
}

run_gpu_phase() {
    log_info "Starting Phase 3 (GPU/System, scope=${GPU_SCOPE})"
    set +e
    TEST_SCOPE="$GPU_SCOPE" TEST_STREAM_FILTER="$TEST_STREAM_FILTER_VALUE" TEST_COLOR_MODE="$TEST_COLOR_MODE_VALUE" \
        "$FULL_RUNNER"
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        log_success "Phase 3 passed"
    else
        error "Phase 3 failed (exit $status)"
    fi
    return "$status"
}

echo
echo "${c_blue}================================================================${c_reset}"
echo "${c_blue}  FlightProject Phased Test Runner${c_reset}"
echo "${c_blue}================================================================${c_reset}"
log_info "Phase 1 enabled: $( [[ "$RUN_PHASE1" -eq 1 ]] && echo yes || echo no )"
log_info "Phase 2 enabled: $( [[ "$RUN_PHASE2" -eq 1 ]] && echo yes || echo no )"
log_info "Phase 3 enabled: $( [[ "$RUN_PHASE3" -eq 1 ]] && echo yes || echo no )"
log_info "Stream filter:    ${TEST_STREAM_FILTER_VALUE}"
echo "${c_blue}----------------------------------------------------------------${c_reset}"
echo

if [[ "$RUN_PHASE1" -eq 1 ]]; then
    if run_headless_phase "Phase 1 (Complex/Generated + Spec)" "$PHASE1_FILTER"; then
        PHASE1_STATUS=0
    else
        PHASE1_STATUS=$?
    fi
fi

if [[ "$RUN_PHASE2" -eq 1 ]]; then
    if run_headless_phase "Phase 2 (Simple Automation)" "$PHASE2_FILTER"; then
        PHASE2_STATUS=0
    else
        PHASE2_STATUS=$?
    fi
fi

if [[ "$RUN_PHASE3" -eq 1 ]]; then
    if run_gpu_phase; then
        PHASE3_STATUS=0
    else
        PHASE3_STATUS=$?
    fi
fi

echo
echo "${c_blue}================================================================${c_reset}"
echo "${c_blue}  Phased Test Summary${c_reset}"
echo "${c_blue}================================================================${c_reset}"
if [[ "$RUN_PHASE1" -eq 1 ]]; then
    if [[ "$PHASE1_STATUS" -eq 0 ]]; then log_success "Phase 1: PASS"; else error "Phase 1: FAIL"; fi
fi
if [[ "$RUN_PHASE2" -eq 1 ]]; then
    if [[ "$PHASE2_STATUS" -eq 0 ]]; then log_success "Phase 2: PASS"; else error "Phase 2: FAIL"; fi
fi
if [[ "$RUN_PHASE3" -eq 1 ]]; then
    if [[ "$PHASE3_STATUS" -eq 0 ]]; then log_success "Phase 3: PASS"; else error "Phase 3: FAIL"; fi
fi
echo "${c_blue}================================================================${c_reset}"
echo

if [[ "$PHASE1_STATUS" -ne 0 || "$PHASE2_STATUS" -ne 0 || "$PHASE3_STATUS" -ne 0 ]]; then
    exit 1
fi

exit 0

