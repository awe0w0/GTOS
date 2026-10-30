#!/bin/sh
# ATA wait/wakeup unit: the same translation unit built twice, -O0 and -O2, each with ASan and UBSan, then started.
#
#   ata_wait_test.sh                     build and start both images in one throwaway directory (what an ordinary host does)
#   ata_wait_test.sh build <dir>         build both images, publish them through the supply, start nothing
#   ata_wait_test.sh run <run-dir>       start only the published copies, one log per image
#
# The split exists because a program that is created inside a step cannot be started inside that step under an execution
# allow-list: the list is fixed before the step begins. The build step's published copies are what the run step starts.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
# stage_binary publishes an image and records its identity; staged_binary finds that record again in the run step.
. "$repo/tests/host-runner.sh"
mode=all
case "${1:-}" in
    build|run) mode=$1; shift ;;
esac

usage() {
    echo "usage: ata_wait_test.sh" >&2
    echo "       ata_wait_test.sh build <dir>" >&2
    echo "       ata_wait_test.sh run <run-dir>" >&2
    exit 2
}

# compile_one <optimization> <output>
compile_one() {
    "${CXX:-g++}" -m64 -O"$1" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
        -Iinclude tests/ata_wait_test.cpp -o "$2"
}

all() {
    work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-ata-wait.XXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    for optimization in 0 2; do
        compile_one "$optimization" "$work/ata-wait-$optimization"
        "$work/ata-wait-$optimization"
    done
}

build() {
    if [ "$#" -ne 1 ]; then usage; fi
    out=$1
    mkdir -p "$out"
    out=$(realpath "$out")
    for optimization in 0 2; do
        compile_one "$optimization" "$out/ata-wait-$optimization"
        # Publishing is separate from compiling: the run step can only start what was published, and what was published is
        # read-only and recorded with its digest.
        stage_binary "ata-wait-O$optimization" "$out/ata-wait-$optimization"
    done
}

run() {
    if [ "$#" -ne 1 ]; then usage; fi
    run_dir=$1
    mkdir -p "$run_dir"
    run_dir=$(realpath "$run_dir")
    status=0
    for optimization in 0 2; do
        # The image is resolved in its own statement: a substitution inside a command's arguments does not propagate its exit
        # status, so a missing record would otherwise look like a program that failed.
        image=$(staged_binary "ata-wait-O$optimization") || exit 2
        this_status=0
        "$image" > "$run_dir/run-O$optimization.log" 2>&1 || this_status=$?
        printf 'O%s exit %s\n' "$optimization" "$this_status"
        if [ "$this_status" -ne 0 ]; then
            status=$this_status
        fi
    done
    return "$status"
}

case "$mode" in
    all) all ;;
    build) build "$@" ;;
    run) run "$@" ;;
esac
