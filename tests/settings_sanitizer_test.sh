#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-settings-sanitizers.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CXX:-g++}" -std=c++11 -O1 -g -Iinclude -fno-exceptions -fno-rtti \
    -fno-builtin -fno-omit-frame-pointer -fno-pie -no-pie \
    -Wall -Wextra -Werror -fsanitize=address,undefined \
    tests/settings_test.cpp src/storage/settings.cpp src/storage/appstore.cpp \
    src/apps/package.cpp -o "$build/test"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$build/test"
