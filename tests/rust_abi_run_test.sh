#!/bin/sh
# The run stage of the cross-boundary ABI consumer: the same Rust objects the probe links are linked once more with the C side's
# reporting entry point, published read-only, and started. What the Rust functions returned, and what the pointer-out function
# stored, is then read from the program's own output and compared with a reference computed here from the documented semantics.
#
# This is host-ABI execution: the image is started by an i386 user-mode emulator, so its write and exit calls are the host's. It
# says the two compilers agree about the i386 calling convention, struct layout and function-pointer table for real, by value.
# It does not say GTOS accepts the image, which is a separate claim with its own evidence.
#
#   rust_abi_run_test.sh build <dir>   compile and publish; prints the STAGED line
#   rust_abi_run_test.sh run           start the published image and judge its values
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
. ./tests/i386-launcher.sh
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: rust_abi_run_test.sh [all|build <dir>|run]" >&2; exit 2 ;;
esac
python=${PYTHON:-python3}
build=${2:-}
if [ "$stage" = all ]; then
    if [ -z "${RUST_LIBS:-}" ]; then
        echo 'skip rust ABI run: RUST_LIBS is not set, so this checkout has no i686-unknown-gtos core to build against'
        exit 0
    fi
    # An ordinary host run: one temporary generation for the objects, the published image and the output. The image is still
    # published and still started through the same runner, so the ordinary entry exercises the same identity check.
    root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-rust-abi-run.XXXXXX")
    trap 'rm -rf "$root"' EXIT HUP INT TERM
    mkdir -p "$root/publish"
    GTOS_HOST_PUBLISH_ROOT=$root/publish GTOS_PUBLISH_MANIFEST=$root/manifest.txt \
        GTOS_PUBLISH_RECORD=$root/record.txt RUST_LIBS=$RUST_LIBS RUSTC=${RUSTC:-rustc} \
        sh "$0" build "$root/build" > "$root/build.log" 2>&1 || { sed -n '1,120p' "$root/build.log" >&2; exit 1; }
    choice=$(i386_launcher_choose all) || exit 2
    launcher=''
    if ! i386_launcher_is_native "$choice"; then
        launcher=$(i386_launcher_path "$choice")
        GTOS_HOST_LAUNCHER=$launcher
        export GTOS_HOST_LAUNCHER
    fi
    GTOS_HOST_PUBLISH_ROOT=$root/publish GTOS_PUBLISH_MANIFEST=$root/manifest.txt \
        GTOS_PUBLISH_RECORD=$root/record.txt GTOS_ABI_RUN_LOG=$root/run.log \
        sh "$0" run
    exit 0
fi
if [ "$stage" = build ] || [ "$stage" = all ]; then
    if [ -z "${RUST_LIBS:-}" ]; then
        echo 'skip rust ABI run: RUST_LIBS is not set, so this checkout has no i686-unknown-gtos core to build against'
        exit 0
    fi
    if [ -e "$build" ]; then
        echo "rust_abi_run_test.sh: refusing to reuse the artifact directory $build" >&2
        exit 2
    fi
    mkdir -p "$build"
    RUSTC=${RUSTC:-rustc} RUST_LIBS=$RUST_LIBS \
        sh tools/build-rust-abi-probe.sh "$build" run > "$build/build.log" 2>&1 || {
        sed -n '1,200p' "$build/build.log" >&2
        exit 1
    }
    if [ ! -f "$build/abi-consumer.elf" ]; then
        echo "rust_abi_run_test.sh: no image was linked; see $build/build.log" >&2
        exit 1
    fi
    stage_binary abi-run-image "$build/abi-consumer.elf"
    exit 0
fi
if [ "$stage" = run ]; then
    if [ -z "${GTOS_PUBLISH_RECORD:-}" ] || [ ! -f "${GTOS_PUBLISH_RECORD:-}" ]; then
        echo "rust_abi_run_test.sh: the run stage needs a publish record; without it the image has no identity" >&2
        exit 2
    fi
    image=$(staged_binary abi-run-image) || exit 2
    log=${GTOS_ABI_RUN_LOG:-}
    if [ -z "$log" ]; then
        echo "rust_abi_run_test.sh: the run stage needs GTOS_ABI_RUN_LOG to hold the output it judges" >&2
        exit 2
    fi
    mkdir -p "$(dirname "$log")"
    run_host_binary "$image" > "$log" 2>&1 || {
        status=$?
        echo "rust_abi_run_test.sh: the image was not started or did not exit cleanly (status $status); see $log" >&2
        exit "$status"
    }
    "$python" -B tests/rust_abi_run_check.py --log "$log"
    exit 0
fi
echo "usage: rust_abi_run_test.sh [build <dir>|run]" >&2
exit 2
