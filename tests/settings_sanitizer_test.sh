#!/bin/sh
# The settings unit under AddressSanitizer and UndefinedBehaviorSanitizer.
#
# Three modes. `all` compiles into a temporary directory, runs the result and deletes it, which is what a normal host wants. `build
# <dir>` compiles into a directory the caller keeps and publishes the image through the supply, and `run` starts the published copy
# through the same launcher every other unit uses. The split exists because a program that was created inside a step cannot be
# started inside that step under an execution allow-list, so under one this unit could only ever fail; with the split it is measured
# the same way the probe units are, and the sanitizer run is still the run rather than a compile-only claim.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

stage=${1:-all}
# Read the flag file with the shell rather than with cat, so this script needs no external command to learn its own flags.
IFS= read -r kernel_flags < tools/kernel-cxxflags

compile() { # <output>
    ${CXX:-g++} $kernel_flags -std=c++11 -O1 -g -Iinclude -fno-exceptions -fno-rtti \
        -fno-builtin -fno-omit-frame-pointer -fno-pie -no-pie \
        -Wall -Wextra -Werror -fsanitize=address,undefined \
        tests/settings_test.cpp src/storage/settings.cpp src/storage/appstore.cpp \
        src/apps/package.cpp -o "$1"
}

run_checked() { # <binary>
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$1"
}

case $stage in
all)
    build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-settings-sanitizers.XXXXXX")
    trap 'rm -rf "$build"' EXIT HUP INT TERM
    compile "$build/test"
    run_checked "$build/test"
    ;;
build)
    out=${2:-}
    if [ -z "$out" ]; then
        echo "usage: $0 build <output-directory>" >&2
        exit 2
    fi
    mkdir -p "$out"
    compile "$out/settings-sanitizer"
    stage_binary settings-sanitizer "$out/settings-sanitizer"
    ;;
run)
    # Resolved in a separate statement on purpose: an assignment's exit status is the substitution's, but inside a command's
    # arguments it is not, so `run_checked "$(staged_binary ...)"` would start an empty pathname with status 127 instead of
    # refusing the way the contract says.
    staged=$(staged_binary settings-sanitizer) || exit 2
    run_checked "$staged"
    ;;
*)
    echo "usage: $0 [all|build <directory>|run]" >&2
    exit 2
    ;;
esac
