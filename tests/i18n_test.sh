#!/bin/sh
# The i18n unit: the generated catalog and the same test body built at both widths, plus the optional strict-sanitizer branch.
#
# Building and starting are two steps when the caller says so. Under an execution allow-list the program a step has just linked
# cannot be started by that step — it was not on the list when the list was made — so `build` compiles, publishes each image and
# records where it went, and `run` starts only those published, read-only images. With no argument the script builds and runs in one
# go, which is what an ordinary host and `make test` do.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
. ./tests/i386-launcher.sh
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: i18n_test.sh [all|build|run]" >&2; exit 2 ;;
esac
kernel_flags=$(cat tools/kernel-cxxflags)
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-i18n.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
i18n_choice=$(i386_launcher_choose "$stage")
if i386_launcher_is_native "$i18n_choice"; then
    i386_launcher=
else
    i386_launcher=$(i386_launcher_path "$i18n_choice")
fi
compile() {
    "${CXX:-g++}" $kernel_flags "$1" -std=c++11 -O2 -Iinclude -mstackrealign -ffreestanding \
      -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector -fno-pie -no-pie \
      -nostdlib -static -Wall -Wextra -Werror -Wl,-e,_start \
      tests/i18n_tests.cpp src/i18n/*.cpp -o "$2"
}

if [ "$stage" = build ] || [ "$stage" = all ]; then
    "${PYTHON:-python3}" tools/i18n_generate.py --check
    compile -m32 "$build/test32"
    compile -m64 "$build/test64"
    if [ "${GTOS_I18N_SANITIZERS:-0}" = 1 ]; then
        "${CXX:-g++}" $kernel_flags -m64 -std=c++11 -O1 -g -Iinclude -DGTOS_I18N_SANITIZE \
          -fno-exceptions -fno-rtti -fno-builtin -fno-pie -no-pie -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-omit-frame-pointer tests/i18n_tests.cpp \
          src/i18n/*.cpp -o "$build/sanitized"
    fi
    if [ -n "${GTOS_I18N_FONT:-}" ]; then
        "${PYTHON:-python3}" tools/i18n_generate.py --check --font "$GTOS_I18N_FONT"
    fi
fi
if [ "$stage" = build ]; then
    stage_binary i18n_32 "$build/test32"
    stage_binary i18n_64 "$build/test64"
    if [ "${GTOS_I18N_SANITIZERS:-0}" = 1 ]; then
        stage_binary i18n_sanitized "$build/sanitized"
    else
        echo 'i18n strict sanitizer branch not requested (GTOS_I18N_SANITIZERS=0)'
    fi
    exit 0
fi
if [ "$stage" = run ]; then
    if [ -n "$i386_launcher" ]; then
        GTOS_HOST_LAUNCHER=$i386_launcher run_host_binary "$(staged_binary i18n_32)"
    else
        run_host_binary "$(staged_binary i18n_32)"
    fi
    run_host_binary "$(staged_binary i18n_64)"
    if [ "${GTOS_I18N_SANITIZERS:-0}" = 1 ]; then
        ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            run_host_binary "$(staged_binary i18n_sanitized)"
    fi
    exit 0
fi

if [ -n "$i386_launcher" ]; then
    GTOS_HOST_LAUNCHER=$i386_launcher run_host_binary "$build/test32"
else
    "$build/test32"
fi
"$build/test64"
if [ "${GTOS_I18N_SANITIZERS:-0}" = 1 ]; then
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build/sanitized"
fi
