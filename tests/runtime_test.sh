#!/bin/sh
# The NativeRuntime host unit: the production runtime, its own entry, and the seam that stands in for the machine.
#
# No script built this unit before, so its Create capacity, per-stage refusal, automatic rollback and slot-reuse measurements
# existed as source with no runner and no recorded result. The link set is the one the unit was designed with (packet 147): the
# production units it drives, its own translation units, the host boundary and the GDT seam, the assembly entry that hands
# control to it, and the repository's link script that places the image at 1 MiB where the identity map covers it. The three
# objects that recipe took from a producer directory are compiled here from the same sources in this tree, so the unit is built
# from the current source rather than from an artifact of an earlier wave.
#
# The unit takes exactly one case selector and runs one case per process: a successful Activate publishes an owner that has no
# destroy path, so cases cannot share a process. The selector is required; a run without one, or with two, is refused by the
# unit itself with status 90, and that refusal is checked rather than assumed.
#
#   tests/runtime_test.sh                      build, then run every case in its own process
#   tests/runtime_test.sh --build <dir>        build only, print the image identity
#   tests/runtime_test.sh --publish <image>    publish the image read-only and print its published path
#   tests/runtime_test.sh --run <image> <case> start one case from an image the caller already published
#   tests/runtime_test.sh --run-all <image>    run every case from a published image
#
# The image is i386 and freestanding, so it is started through the fixed executor its build identity calls for; see
# tests/host-runner.sh. Nothing here decides that a case may be skipped, and the unit's exit status is the script's.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

optimization=${OPTIMIZATION:--O2}
# The tools are named through the environment the way the Makefile names them, so a closed CI PATH supplies them by name.
cxx=${CXX:-g++}
assembler=${AS:-as}
linker=${LD:-ld}
production_units="src/process/native_runtime.cpp src/gdt.cpp src/process/native_fp.cpp src/multitasking.cpp
src/process/native_realtime.cpp src/memory/physical.cpp src/memory/process_address_space.cpp src/memory/paging.cpp
src/process/native_surface.cpp src/process/native_windows.cpp src/process/elf32.cpp src/process/resources.cpp"
test_units="tests/runtime_test.cpp tests/runtime_boundary.cpp tests/gdt_host_seam.cpp"
seam_macros="GTOS_MEMORY_TEST GTOS_NATIVE_RUNTIME_HOST_TEST GTOS_PAGING_TEST GTOS_PROCESS_MEMORY_TEST
GTOS_CPU_TEST GTOS_SURFACE_HOST_TEST GTOS_WINDOW_HOST_TEST"
case_count=12

identity() {
    printf 'BUILT %s %s %s\n' "$1" "$(wc -c < "$1")" "$(sha256sum "$1" | cut -d' ' -f1)"
}

build() {
    out=$1
    mkdir -p "$out"
    base_flags=$(sed -n 's/^BASE_CXXFLAGS := //p' Makefile | sed "s|\$(OPTIMIZATION)|$optimization|")
    integer_flags=$(cat tools/kernel-cxxflags)
    seam_flags=
    for macro in $seam_macros; do seam_flags="$seam_flags -D$macro"; done
    # shellcheck disable=SC2086
    cxxflags="$base_flags $integer_flags $seam_flags"
    objects=
    for unit in $production_units $test_units; do
        object=$out/$(basename "$unit" .cpp).o
        # shellcheck disable=SC2086
        "$cxx" $cxxflags -c "$unit" -o "$object"
        objects="$objects $object"
    done
    for unit in tests/runtime_start.s src/process/native_fp.s; do
        object=$out/$(basename "$unit" .s).asm.o
        "$assembler" --32 "$unit" -o "$object"
        objects="$objects $object"
    done
    # shellcheck disable=SC2086
    "$linker" -m elf_i386 -T tests/runtime_host.ld -Map "$out/runtime_test.map" -o "$out/runtime_test.elf" $objects
    identity "$out/runtime_test.elf"
}

run_one() {
    image=$1
    case_number=$2
    echo "CASE $case_number"
    status=0
    run_host_binary "$image" "$case_number" || status=$?
    echo "CASE $case_number exit $status"
    return $status
}

run_all() {
    image=$1
    failures=0
    # The selector is the unit's contract, so both refusals are checked: a run with no case and a run with two.
    negative_status=0
    run_host_binary "$image" >/dev/null 2>&1 || negative_status=$?
    echo "SELECTOR none exit $negative_status"
    if [ "$negative_status" -ne 90 ]; then
        echo "the unit did not refuse a missing selector with 90" >&2
        failures=$((failures + 1))
    fi
    negative_status=0
    run_host_binary "$image" 1 2 >/dev/null 2>&1 || negative_status=$?
    echo "SELECTOR two-arguments exit $negative_status"
    if [ "$negative_status" -ne 90 ]; then
        echo "the unit did not refuse two selectors with 90" >&2
        failures=$((failures + 1))
    fi
    case_number=1
    while [ "$case_number" -le "$case_count" ]; do
        run_one "$image" "$case_number" || failures=$((failures + 1))
        case_number=$((case_number + 1))
    done
    if [ "$failures" -ne 0 ]; then
        echo "runtime unit: $failures case(s) failed" >&2
        return 1
    fi
    echo "runtime unit PASS: $case_count cases, no failures"
}

case "${1:-}" in
    --build)
        [ "$#" -eq 2 ] || { echo "usage: $0 --build <dir>" >&2; exit 2; }
        build "$2"
        ;;
    --publish)
        [ "$#" -eq 2 ] || { echo "usage: $0 --publish <image>" >&2; exit 2; }
        publish_host_binary "$2"
        ;;
    --run)
        [ "$#" -eq 3 ] || { echo "usage: $0 --run <image> <case>" >&2; exit 2; }
        run_one "$2" "$3"
        ;;
    --run-all)
        [ "$#" -eq 2 ] || { echo "usage: $0 --run-all <image>" >&2; exit 2; }
        run_all "$2"
        ;;
    --help|-h)
        sed -n '2,20p' "$0"
        ;;
    '')
        work=${TMPDIR:-/tmp}/gtos-runtime.$$
        trap 'rm -rf "$work"' EXIT INT TERM
        build "$work"
        run_all "$work/runtime_test.elf"
        ;;
    *)
        echo "usage: $0 [--build <dir> | --publish <image> | --run <image> <case> | --run-all <image>]" >&2
        exit 2
        ;;
esac
