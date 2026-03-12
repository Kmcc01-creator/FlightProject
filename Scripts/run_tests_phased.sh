#!/usr/bin/env bash
# Phased automation runner:
#   Phase 1: Generated + spec
#   Phase 2: Architecture / development
#   Phase 3: Multi-case integration
#   Phase 4: Simple unit automation
#   Phase 5: Optional GPU/system pass

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=Scripts/env_common.sh
source "$SCRIPT_DIR/env_common.sh"

HEADLESS_RUNNER="$SCRIPT_DIR/run_tests_headless.sh"
FULL_RUNNER="$SCRIPT_DIR/run_tests_full.sh"

ensure_project_file
ensure_executable "$HEADLESS_RUNNER" "run_tests_headless.sh"
ensure_executable "$FULL_RUNNER" "run_tests_full.sh"

PHASE1_NAME="Phase 1 (Generated + Spec)"
PHASE1_FILTER="FlightProject.Integration.Generative+FlightProject.Integration.SchemaDriven+FlightProject.Integration.Vex.VerticalSlice+FlightProject.Integration.Concurrency+FlightProject.Vex.Parser.Spec+FlightProject.Unit.Safety.MemoryLayout"
PHASE2_NAME="Phase 2 (Architecture / Development)"
PHASE2_FILTER="FlightProject.Vex.Frontend+FlightProject.Vex.Generalization+FlightProject.Vex.Schema+FlightProject.IoUring.Vulkan.Complex"
PHASE3_NAME="Phase 3 (Multi-Case Integration)"
PHASE3_FILTER="FlightProject.Integration.Startup.Sequencing+FlightProject.Integration.Orchestration+FlightProject.Orchestration+FlightProject.Navigation+FlightProject.Functional+FlightProject.Logging.Complex+FlightProject.Logging.Boundaries+FlightProject.Reflection.Complex+FlightProject.Complex.Foundations+FlightProject.Integration.Verse+FlightProject.Integration.AutoRTFM+FlightProject.Integration.Vex.SimdParity"
PHASE4_NAME="Phase 4 (Simple Unit Automation)"
PHASE4_FILTER="FlightProject.Unit.Functional+FlightProject.Unit.Mass+FlightProject.Unit.Navigation+FlightProject.Unit.Orchestration+FlightProject.Unit.Reactive+FlightProject.Unit.Reflection+FlightProject.Unit.Swarm+FlightProject.Unit.Vex+FlightProject.Schema+FlightProject.Logging.Core+FlightProject.Vex.RewriteRegistry+FlightProject.Vex.TreeTraits.IR.PostOrder+FlightProject.Vex.UI"
PHASE5_NAME="Phase 5 (GPU/System)"

RUN_PHASE1=1
RUN_PHASE2=1
RUN_PHASE3=1
RUN_PHASE4=1
RUN_PHASE5=0
ENABLE_TIMESTAMPS=1
TEST_OUTPUT_MODE_VALUE="${TEST_OUTPUT_MODE:-${TEST_STREAM_FILTER:-errors}}"
TEST_COLOR_MODE_VALUE="${TEST_COLOR_MODE:-auto}"
TEST_LOG_PROFILE_VALUE="${TEST_LOG_PROFILE:-focused}"
TEST_EXTRA_LOG_CMDS_VALUE="${TEST_EXTRA_LOG_CMDS:-}"
TEST_INCLUDE_PYTHON_LOGS_VALUE="${TEST_INCLUDE_PYTHON_LOGS:-0}"
TEST_INCLUDE_STARTUP_LOGS_VALUE="${TEST_INCLUDE_STARTUP_LOGS:-0}"
TEST_PRESET_VALUE="${TEST_PRESET:-$FP_TEST_PRESET}"
TEST_BUILD_BEFORE_RUN_VALUE="${TEST_BUILD_BEFORE_RUN:-$FP_TEST_BUILD_BEFORE_RUN}"
TEST_BUILD_CONFIGURATION_VALUE="${TEST_BUILD_CONFIGURATION:-$FP_TEST_BUILD_CONFIGURATION}"
GPU_SCOPE="${TEST_SCOPE:-all}"
PRINT_PLAN_ONLY=0

usage() {
    cat <<'USAGE'
Usage: ./Scripts/run_tests_phased.sh [options]

Options:
  --with-gpu              Run Phase 5 GPU/system suite after headless phases.
  --gpu-scope <scope>     Scope for run_tests_full.sh (benchmark|gpu_smoke|all). Default: all.
  --phase1-only           Run only Phase 1.
  --phase2-only           Run only Phase 2.
  --phase3-only           Run only Phase 3.
  --phase4-only           Run only Phase 4.
  --phase5-only           Run only Phase 5 (implies --with-gpu).
  --skip-phase1           Skip Phase 1.
  --skip-phase2           Skip Phase 2.
  --skip-phase3           Skip Phase 3.
  --skip-phase4           Skip Phase 4.
  --skip-phase5           Skip Phase 5.
  --stream <mode>         Compatibility alias for --output.
  --output <mode>         Output mode for child scripts (errors|summary|automation|all). Default: errors.
  --profile <profile>     Log profile for child scripts (minimal|focused|python|verbose|full). Default: focused.
  --preset <name>         Named child-runner preset (quiet|triage|startup-debug|full-debug).
  --show-python           Include LogPython lines in child output/log categories.
  --show-startup          Include pre-automation startup lines in automation output mode.
  --extra-log-cmds <csv>  Append extra Unreal -LogCmds entries for child runs.
  --print-plan            Print resolved phase filters and build settings, then exit.
  --dry-run               Alias for --print-plan.
  --build                 Build targets before running tests (default).
  --no-build              Skip the shared pre-test build step.
  --build-config <cfg>    Build configuration for the pre-test build. Default: Development.
  --timestamps            Enable timestamps in child scripts (default).
  --no-timestamps         Disable timestamps in child scripts.
  -h, --help              Show this help text.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-gpu)
            RUN_PHASE5=1
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
            RUN_PHASE4=0
            RUN_PHASE5=0
            shift
            ;;
        --phase2-only)
            RUN_PHASE1=0
            RUN_PHASE2=1
            RUN_PHASE3=0
            RUN_PHASE4=0
            RUN_PHASE5=0
            shift
            ;;
        --phase3-only)
            RUN_PHASE1=0
            RUN_PHASE2=0
            RUN_PHASE3=1
            RUN_PHASE4=0
            RUN_PHASE5=0
            shift
            ;;
        --phase4-only)
            RUN_PHASE1=0
            RUN_PHASE2=0
            RUN_PHASE3=0
            RUN_PHASE4=1
            RUN_PHASE5=0
            shift
            ;;
        --phase5-only)
            RUN_PHASE1=0
            RUN_PHASE2=0
            RUN_PHASE3=0
            RUN_PHASE4=0
            RUN_PHASE5=1
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
        --skip-phase4)
            RUN_PHASE4=0
            shift
            ;;
        --skip-phase5)
            RUN_PHASE5=0
            shift
            ;;
        --stream)
            if [[ $# -lt 2 ]]; then
                error "--stream expects a value"
                exit 1
            fi
            TEST_OUTPUT_MODE_VALUE="$2"
            shift 2
            ;;
        --output=*)
            TEST_OUTPUT_MODE_VALUE="${1#*=}"
            shift
            ;;
        --preset=*)
            TEST_PRESET_VALUE="${1#*=}"
            shift
            ;;
        --output)
            if [[ $# -lt 2 ]]; then
                error "--output expects a value (errors|summary|automation|all)"
                exit 1
            fi
            TEST_OUTPUT_MODE_VALUE="$2"
            shift 2
            ;;
        --preset)
            if [[ $# -lt 2 ]]; then
                error "--preset expects a value (quiet|triage|startup-debug|full-debug)"
                exit 1
            fi
            TEST_PRESET_VALUE="$2"
            shift 2
            ;;
        --profile=*)
            TEST_LOG_PROFILE_VALUE="${1#*=}"
            shift
            ;;
        --profile)
            if [[ $# -lt 2 ]]; then
                error "--profile expects a value (minimal|focused|python|verbose|full)"
                exit 1
            fi
            TEST_LOG_PROFILE_VALUE="$2"
            shift 2
            ;;
        --show-python)
            TEST_INCLUDE_PYTHON_LOGS_VALUE=1
            shift
            ;;
        --show-startup)
            TEST_INCLUDE_STARTUP_LOGS_VALUE=1
            shift
            ;;
        --extra-log-cmds=*)
            TEST_EXTRA_LOG_CMDS_VALUE=$(append_csv_value "$TEST_EXTRA_LOG_CMDS_VALUE" "${1#*=}")
            shift
            ;;
        --extra-log-cmds)
            if [[ $# -lt 2 ]]; then
                error "--extra-log-cmds expects a comma-separated value"
                exit 1
            fi
            TEST_EXTRA_LOG_CMDS_VALUE=$(append_csv_value "$TEST_EXTRA_LOG_CMDS_VALUE" "$2")
            shift 2
            ;;
        --print-plan|--dry-run)
            PRINT_PLAN_ONLY=1
            shift
            ;;
        --build)
            TEST_BUILD_BEFORE_RUN_VALUE=1
            shift
            ;;
        --no-build)
            TEST_BUILD_BEFORE_RUN_VALUE=0
            shift
            ;;
        --build-config=*)
            TEST_BUILD_CONFIGURATION_VALUE="${1#*=}"
            shift
            ;;
        --build-config)
            if [[ $# -lt 2 ]]; then
                error "--build-config expects a value (Debug|Development|Shipping)"
                exit 1
            fi
            TEST_BUILD_CONFIGURATION_VALUE="$2"
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

if [[ -n "$TEST_PRESET_VALUE" ]]; then
    IFS=$'\t' read -r TEST_LOG_PROFILE_VALUE TEST_OUTPUT_MODE_VALUE TEST_INCLUDE_PYTHON_LOGS_VALUE TEST_INCLUDE_STARTUP_LOGS_VALUE ENABLE_TIMESTAMPS TEST_EXTRA_LOG_CMDS_VALUE <<< "$(apply_test_preset_overrides \
        "$TEST_PRESET_VALUE" \
        "$TEST_LOG_PROFILE_VALUE" \
        "$TEST_OUTPUT_MODE_VALUE" \
        "$TEST_INCLUDE_PYTHON_LOGS_VALUE" \
        "$TEST_INCLUDE_STARTUP_LOGS_VALUE" \
        "$ENABLE_TIMESTAMPS" \
        "$TEST_EXTRA_LOG_CMDS_VALUE")"
fi

if [[ "$RUN_PHASE1" -eq 0 && "$RUN_PHASE2" -eq 0 && "$RUN_PHASE3" -eq 0 && "$RUN_PHASE4" -eq 0 && "$RUN_PHASE5" -eq 0 ]]; then
    error "No phases selected."
    usage
    exit 1
fi

TEST_BUILD_BEFORE_RUN="$TEST_BUILD_BEFORE_RUN_VALUE"
TEST_BUILD_CONFIGURATION="$TEST_BUILD_CONFIGURATION_VALUE"

print_phase_plan() {
    echo
    echo "${c_blue}================================================================${c_reset}"
    echo "${c_blue}  FlightProject Phased Test Plan${c_reset}"
    echo "${c_blue}================================================================${c_reset}"
    log_info "Output mode:      ${TEST_OUTPUT_MODE_VALUE}"
    log_info "Log profile:      ${TEST_LOG_PROFILE_VALUE}"
    log_info "Pre-test build:   $( is_truthy "$TEST_BUILD_BEFORE_RUN_VALUE" && echo yes || echo no )"
    log_info "Build config:     ${TEST_BUILD_CONFIGURATION_VALUE}"
    log_info "GPU scope:        ${GPU_SCOPE}"
    if [[ -n "$TEST_PRESET_VALUE" ]]; then
        log_info "Preset:           ${TEST_PRESET_VALUE}"
    fi
    echo "${c_blue}----------------------------------------------------------------${c_reset}"
    if [[ "$RUN_PHASE1" -eq 1 ]]; then echo "  ${PHASE1_NAME}: ${PHASE1_FILTER}"; fi
    if [[ "$RUN_PHASE2" -eq 1 ]]; then echo "  ${PHASE2_NAME}: ${PHASE2_FILTER}"; fi
    if [[ "$RUN_PHASE3" -eq 1 ]]; then echo "  ${PHASE3_NAME}: ${PHASE3_FILTER}"; fi
    if [[ "$RUN_PHASE4" -eq 1 ]]; then echo "  ${PHASE4_NAME}: ${PHASE4_FILTER}"; fi
    if [[ "$RUN_PHASE5" -eq 1 ]]; then echo "  ${PHASE5_NAME}: scope=${GPU_SCOPE}"; fi
    echo "${c_blue}================================================================${c_reset}"
    echo
}

if [[ "$PRINT_PLAN_ONLY" -eq 1 ]]; then
    print_phase_plan
    exit 0
fi

run_pretest_build_if_enabled "run_tests_phased.sh"

TS_ARGS=()
if [[ "$ENABLE_TIMESTAMPS" -eq 1 ]]; then
    TS_ARGS+=(--timestamps)
fi

PHASE1_STATUS=0
PHASE2_STATUS=0
PHASE3_STATUS=0
PHASE4_STATUS=0
PHASE5_STATUS=0

run_headless_phase() {
    local phase_name="$1"
    local phase_filter="$2"
    log_info "Starting ${phase_name}"
    set +e
    TEST_OUTPUT_MODE="$TEST_OUTPUT_MODE_VALUE" TEST_COLOR_MODE="$TEST_COLOR_MODE_VALUE" \
        TEST_LOG_PROFILE="$TEST_LOG_PROFILE_VALUE" TEST_EXTRA_LOG_CMDS="$TEST_EXTRA_LOG_CMDS_VALUE" \
        TEST_INCLUDE_PYTHON_LOGS="$TEST_INCLUDE_PYTHON_LOGS_VALUE" TEST_INCLUDE_STARTUP_LOGS="$TEST_INCLUDE_STARTUP_LOGS_VALUE" \
        TEST_BUILD_BEFORE_RUN=0 \
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
    log_info "Starting ${PHASE5_NAME} (scope=${GPU_SCOPE})"
    local gpu_stream_filter="all"
    case "${TEST_OUTPUT_MODE_VALUE,,}" in
        errors|error|fail|failures|summary)
            gpu_stream_filter="errors"
            ;;
    esac
    set +e
    TEST_SCOPE="$GPU_SCOPE" TEST_STREAM_FILTER="$gpu_stream_filter" TEST_COLOR_MODE="$TEST_COLOR_MODE_VALUE" \
        TEST_LOG_PROFILE="$TEST_LOG_PROFILE_VALUE" TEST_PRESET="$TEST_PRESET_VALUE" \
        TEST_BUILD_BEFORE_RUN=0 \
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
log_info "Phase 4 enabled: $( [[ "$RUN_PHASE4" -eq 1 ]] && echo yes || echo no )"
log_info "Phase 5 enabled: $( [[ "$RUN_PHASE5" -eq 1 ]] && echo yes || echo no )"
log_info "Output mode:      ${TEST_OUTPUT_MODE_VALUE}"
log_info "Log profile:      ${TEST_LOG_PROFILE_VALUE}"
log_info "Pre-test build:   $( is_truthy "$TEST_BUILD_BEFORE_RUN_VALUE" && echo yes || echo no )"
log_info "Build config:     ${TEST_BUILD_CONFIGURATION_VALUE}"
if [[ -n "$TEST_PRESET_VALUE" ]]; then
    log_info "Preset:           ${TEST_PRESET_VALUE}"
fi
echo "${c_blue}----------------------------------------------------------------${c_reset}"
echo

if [[ "$RUN_PHASE1" -eq 1 ]]; then
    if run_headless_phase "$PHASE1_NAME" "$PHASE1_FILTER"; then
        PHASE1_STATUS=0
    else
        PHASE1_STATUS=$?
    fi
fi

if [[ "$RUN_PHASE2" -eq 1 ]]; then
    if run_headless_phase "$PHASE2_NAME" "$PHASE2_FILTER"; then
        PHASE2_STATUS=0
    else
        PHASE2_STATUS=$?
    fi
fi

if [[ "$RUN_PHASE3" -eq 1 ]]; then
    if run_headless_phase "$PHASE3_NAME" "$PHASE3_FILTER"; then
        PHASE3_STATUS=0
    else
        PHASE3_STATUS=$?
    fi
fi

if [[ "$RUN_PHASE4" -eq 1 ]]; then
    if run_headless_phase "$PHASE4_NAME" "$PHASE4_FILTER"; then
        PHASE4_STATUS=0
    else
        PHASE4_STATUS=$?
    fi
fi

if [[ "$RUN_PHASE5" -eq 1 ]]; then
    if run_gpu_phase; then
        PHASE5_STATUS=0
    else
        PHASE5_STATUS=$?
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
if [[ "$RUN_PHASE4" -eq 1 ]]; then
    if [[ "$PHASE4_STATUS" -eq 0 ]]; then log_success "Phase 4: PASS"; else error "Phase 4: FAIL"; fi
fi
if [[ "$RUN_PHASE5" -eq 1 ]]; then
    if [[ "$PHASE5_STATUS" -eq 0 ]]; then log_success "Phase 5: PASS"; else error "Phase 5: FAIL"; fi
fi
echo "${c_blue}================================================================${c_reset}"
echo

if [[ "$PHASE1_STATUS" -ne 0 || "$PHASE2_STATUS" -ne 0 || "$PHASE3_STATUS" -ne 0 || "$PHASE4_STATUS" -ne 0 || "$PHASE5_STATUS" -ne 0 ]]; then
    exit 1
fi

exit 0
