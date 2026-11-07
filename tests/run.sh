#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

# An ordinary make test keeps the no-argument behavior. Under the guard the two invocations share a completed publication, and
# the run invocation keeps its writable output outside the build directory.
phase=${GTOS_SUITE_STAGE:-all}
case $phase in
all)
    generation=$(suite_generation "${GTOS_SUITE_OUT_ROOT:-$PWD/out}") || exit 1
    suite_build=$generation/build
    suite_run=$generation/run
    mkdir "$suite_build" "$suite_run"
    echo "suite generation: $generation"
    ;;
build)
    mkdir "${GTOS_SUITE_BUILD_DIR:?build needs a fresh GTOS_SUITE_BUILD_DIR}"
    suite_build=$(realpath "$GTOS_SUITE_BUILD_DIR")
    ;;
run)
    suite_build=$(realpath "${GTOS_SUITE_BUILD_DIR:?run needs GTOS_SUITE_BUILD_DIR}")
    [ -f "$suite_build/complete" ] || { echo 'suite build did not complete' >&2; exit 2; }
    suite_run=$(realpath -m "${GTOS_SUITE_RUN_DIR:?run needs a fresh GTOS_SUITE_RUN_DIR}")
    case $suite_run in
        "$suite_build"|"$suite_build"/*) echo 'suite run directory overlaps the build directory' >&2; exit 2 ;;
    esac
    mkdir "$suite_run"
    ;;
*)
    echo "unknown GTOS_SUITE_STAGE: $phase" >&2
    exit 2
    ;;
esac

during_run() {
    if [ "$phase" != build ]; then "$@"; fi
}

with_supply() (
    directory=$1
    shift
    export GTOS_HOST_PUBLISH_ROOT=$directory/publish
    export GTOS_PUBLISH_MANIFEST=$directory/manifest.txt
    export GTOS_PUBLISH_RECORD=$directory/record.txt
    "$@"
)

# Most units already have the same build/run interface. Optional stage arguments point into their own build or run directories.
staged_unit() (
    script=$1
    if [ "$phase" = all ]; then
        "$script"
        exit
    fi
    name=${script##*/}
    directory=$suite_build/${name%.sh}
    if [ "$phase" = build ]; then
        mkdir -p "$directory/publish"
        if [ "${2:-}" = objects ]; then
            with_supply "$directory" "$script" build "$directory/objects"
        else
            with_supply "$directory" "$script" build
        fi
    elif [ "${3:-}" = logs ]; then
        with_supply "$directory" "$script" run "$suite_run/${name%.sh}"
    else
        with_supply "$directory" "$script" run
    fi
)

during_run python3 -m unittest discover -s tests -p 'qemu_runtime_test.py'
staged_unit ./tests/ata_wait_test.sh objects logs
during_run ./tests/kernel_integer_test.sh
during_run ./tests/memory_test.sh
during_run ./tests/memory_bootstrap_test.sh
staged_unit ./tests/live_storage_host_test.sh objects
staged_unit ./tests/boot_log_host_test.sh objects
during_run ./tests/paging_test.sh
during_run ./tests/process_memory_test.sh
during_run ./tests/native_process_test.sh
during_run ./tests/native_fp_test.sh
(
    export GTOS_FAKE_QEMU_DIR=${GTOS_FAKE_QEMU_DIR:-$suite_build/fp-qmp-fixture}
    staged_unit ./tests/native_fp_qmp_test.sh
)
staged_unit ./tests/elf32_test.sh
during_run ./tests/i386_launcher_test.sh
during_run ./tests/host_publish_identity_test.sh

if [ "$phase" != run ]; then
    mkdir -p "$suite_build/host-supply/publish" "$suite_build/runtime-unit/publish" "$suite_build/surface/publish"
    with_supply "$suite_build/host-supply" ./tests/host_runner_test.sh prepare
    ./tests/runtime_test.sh --build "$suite_build/runtime-unit" > /dev/null
    runtime_image=$(with_supply "$suite_build/runtime-unit" \
        ./tests/runtime_test.sh --publish "$suite_build/runtime-unit/runtime_test.elf")
    printf '%s\n' "$runtime_image" > "$suite_build/runtime-unit/image.txt"
    with_supply "$suite_build/surface" ./tests/native_surface_host_test.sh build "$suite_build/surface/objects" > /dev/null
fi
if [ "$phase" != build ]; then
    with_supply "$suite_build/host-supply" ./tests/host_runner_test.sh verify
    IFS= read -r runtime_image < "$suite_build/runtime-unit/image.txt"
    with_supply "$suite_build/runtime-unit" env \
        GTOS_HOST_LAUNCHER="${GTOS_SUITE_I386_EXECUTOR:-${GTOS_HOST_LAUNCHER:-}}" \
        ./tests/runtime_test.sh --run-all "$runtime_image"
    with_supply "$suite_build/surface" env SURFACE_BUILD_DIR="$suite_build/surface/objects" \
        ./tests/native_surface_host_test.sh run "$suite_run/surface"
fi

during_run ./tests/suite_generation_test.sh
during_run ./tests/stage_contract_test.sh
during_run ./tests/browser_probe_test.sh
during_run ./tests/rust_abi_probe_test.sh
if [ -n "${RUST_LIBS:-}" ] && [ "$phase" != all ]; then
    (
        export GTOS_HOST_LAUNCHER=${GTOS_SUITE_I386_EXECUTOR:?Rust ABI run needs the named suite i386 executor}
        export GTOS_ABI_RUN_LOG=${suite_run:-}/rust-abi.log
        staged_unit ./tests/rust_abi_run_test.sh objects
    )
else
    during_run ./tests/rust_abi_run_test.sh
fi
during_run python3 -B ./tests/rust_abi_judge_test.py . "${suite_run:-}/abi-judge"
during_run ./tests/rust_builtins_test.sh
during_run ./tests/gtos_port_mapping_test.sh
during_run ./tests/port_candidate_test.sh
during_run ./tests/port_candidate_apply_test.sh
during_run ./tests/suite_wiring_test.sh
during_run ./tests/rust_target_spec_test.sh
staged_unit ./tests/skia_alpha_probe_test.sh
during_run python3 -m unittest discover -s tests -p 'browser_artifact_test.py'
during_run ./tests/keymap_test.sh
during_run ./tests/cpu_test.sh
during_run ./tests/cpu_startup_test.sh
during_run ./tests/cpu_work_queue_test.sh
(
    export GTOS_CPU_MEMORY_WORK=${GTOS_CPU_MEMORY_WORK:-$suite_build/cpu-memory-objects}
    export GTOS_CPU_MEMORY_RUN_LOG=${GTOS_CPU_MEMORY_RUN_LOG:-${suite_run:-}/cpu-memory.log}
    staged_unit ./tests/cpu_memory_types_test.sh
)
during_run ./tests/package_vm.sh
staged_unit ./tests/i18n_test.sh
during_run ./tests/settings_test.sh
staged_unit ./tests/settings_sanitizer_test.sh objects
during_run ./tests/desktop_sanitizer_sources_test.sh

if [ "$phase" != run ]; then
    mkdir -p "$suite_build/desktop/publish"
    with_supply "$suite_build/desktop" ./tests/desktop_test.sh build
    if [ "$phase" = build ]; then
        ./tests/desktop_two_stage_test.sh build "$suite_build/desktop-contract"
    fi
fi
if [ "$phase" != build ]; then
    with_supply "$suite_build/desktop" ./tests/desktop_test.sh run
    if [ "$phase" = run ]; then
        DESKTOP_TEST_BUILD_DIR=$suite_build/desktop-contract ./tests/desktop_two_stage_test.sh run "$suite_run/desktop-contract"
    else
        ./tests/desktop_two_stage_test.sh
    fi
fi

during_run python3 -m unittest discover -s tests -p 'package*_test.py'
during_run python3 -m unittest discover -s tests -p 'storage_tool_test.py'
during_run python3 -m unittest discover -s tests -p 'settings_tool_test.py'
during_run ./tests/storage_test.sh
during_run ./tests/storage_recovery_test.sh
during_run python3 -B ./tests/port_candidate_r19_test.py . "${suite_run:-}/port-candidate-r19"
if [ "$phase" = build ]; then
    : > "$suite_build/complete"
    echo 'Deterministic module test publications complete'
else
    echo 'All deterministic module tests passed'
fi
