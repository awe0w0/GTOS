#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-settings.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
compile() {
    "${CXX:-g++}" "$1" -std=c++11 -O2 -Iinclude -fno-exceptions -fno-rtti \
        -fno-builtin -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
        -Wall -Wextra -Werror -Wl,-e,_start \
        tests/settings_test.cpp tests/settings_runtime.cpp \
        src/storage/settings.cpp src/storage/appstore.cpp src/apps/package.cpp -o "$2"
}
compile -m32 "$build/test32"
if command -v qemu-i386 >/dev/null 2>&1; then
    qemu-i386 "$build/test32"
else
    status=0
    "$build/test32" || status=$?
    if [ "$status" -eq 126 ] && [ "$(uname -m)" = x86_64 ]; then
        echo 'i386 execution unsupported; running same settings sources as x86_64' >&2
        compile -m64 "$build/test64"
        "$build/test64"
    else
        exit "$status"
    fi
fi
