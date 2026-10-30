#!/bin/sh
# The boot-log boundary and oracle tests at two optimisation levels, under both sanitizers.
#
# Three modes, for the same reason the settings unit has them: `all` compiles into a temporary directory and runs the results there,
# which is what a normal host wants; `build <dir>` keeps the two images and publishes them through the supply; `run` starts only the
# published copies. A program created inside a step cannot be started inside that step under an execution allow-list, so without the
# split this unit could only fail there, and the two optimisation levels are the point of the unit rather than a detail.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

stage=${1:-all}

compile() { # <optimization> <output>
    "${CXX:-g++}" -m64 -std=c++11 -O"$1" -Wall -Wextra -Werror \
        -DGTOS_MEMORY_TEST -fsanitize=address,undefined -fno-omit-frame-pointer \
        -fno-pie -no-pie -Iinclude tests/boot_log_host.cpp \
        src/common/boot_log.cpp -o "$2"
}

run_checked() { # <binary>
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$1"
}

case $stage in
all)
    work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-boot-log.XXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    for optimization in 0 2; do
        compile "$optimization" "$work/boot-log"
        run_checked "$work/boot-log"
    done
    ;;
build)
    out=${2:-}
    if [ -z "$out" ]; then
        echo "usage: $0 build <output-directory>" >&2
        exit 2
    fi
    mkdir -p "$out"
    for optimization in 0 2; do
        compile "$optimization" "$out/boot-log-O$optimization"
        stage_binary "boot-log-O$optimization" "$out/boot-log-O$optimization"
    done
    ;;
run)
    for optimization in 0 2; do
        # Resolved in its own statement: a substitution inside a command's arguments does not propagate its exit status, so the
        # refusal would turn into a start of an empty pathname instead of status 2.
        image=$(staged_binary "boot-log-O$optimization") || exit 2
        printf 'running O%s %s\n' "$optimization" "$image"
        run_checked "$image"
    done
    ;;
*)
    echo "usage: $0 [all|build <directory>|run]" >&2
    exit 2
    ;;
esac
