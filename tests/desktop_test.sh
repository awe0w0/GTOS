#!/bin/sh
# The desktop unit: the catalog inventory, then the same test body built at both widths.
#
# The building and the starting are two steps when the caller says so. Under an execution allow-list the program a step has just
# linked cannot be started by that step — it was not on the list when the list was made — so `build` compiles, publishes each image
# and records where it went, and `run` starts only those published images. With no argument the script builds and runs in one go,
# which is what an ordinary host does and what the repository's own `make test` does.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
kernel_flags=$(cat tools/kernel-cxxflags)
stage=${1:-all}
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-desktop.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
compile() {
    "${CXX:-g++}" $kernel_flags "$1" -std=c++11 -O2 -Iinclude -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST \
      -mstackrealign -ffreestanding -fno-use-cxa-atexit -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector \
      -fno-pie -no-pie -nostdlib -static -Wall -Wextra \
      -Werror -Wl,-e,_start tests/desktop_tests.cpp src/drivers/framebuffer.cpp \
      src/gui/modern_geometry.cpp src/gui/modern_painter.cpp src/gui/modern_desktop.cpp \
      src/gui/modern_render.cpp src/common/boot_log.cpp src/apps/package.cpp src/apps/vm.cpp \
      src/storage/appstore.cpp src/storage/settings.cpp src/i18n/*.cpp \
      src/process/native_windows.cpp src/process/native_surface.cpp -o "$2"
}

if [ "$stage" = build ] || [ "$stage" = all ]; then
    python3 tests/desktop_catalog_test.py
    compile -m32 "$build/test32"
    compile -m64 "$build/test64"
fi
if [ "$stage" = build ]; then
    stage_binary desktop32 "$build/test32"
    stage_binary desktop64 "$build/test64"
    exit 0
fi
if [ "$stage" = run ]; then
    # The 32-bit image has one executor and the 64-bit image has another: an i386 static ELF is loaded by the user-mode i386
    # emulator, and the native image is started directly. Both are the read-only copies published by the build step.
    desktop32=$(staged_binary desktop32)
    desktop64=$(staged_binary desktop64)
    qemu-i386 "$desktop32"
    run_host_binary "$desktop64"
    exit 0
fi
if [ "$stage" != all ]; then
    echo "usage: $0 [build|run]" >&2
    exit 2
fi

runtime="$(pwd)/../gtos-runtime/root"
if [ -x "$runtime/usr/bin/qemu-i386" ]; then
    LD_LIBRARY_PATH="$runtime/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$runtime/usr/bin/qemu-i386" "$build/test32"
elif command -v qemu-i386 >/dev/null 2>&1; then
    qemu-i386 "$build/test32"
else
    status=0
    "$build/test32" || status=$?
    if [ "$status" -eq 126 ] && [ "$(uname -m)" = x86_64 ]; then
        compile -m64 "$build/test64"
        run_host_binary "$build/test64"
        exit 0
    else
        [ "$status" -eq 0 ] || exit "$status"
    fi
fi

run_host_binary "$build/test64"
if [ "${GTOS_DESKTOP_SANITIZERS:-0}" = 1 ]; then
    "${CXX:-g++}" $kernel_flags -m64 -std=c++11 -O1 -g -Iinclude \
      -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST -DGTOS_DESKTOP_SANITIZE -fno-exceptions -fno-rtti \
      -fno-builtin -fno-pie -no-pie -Wall -Wextra -Werror \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      tests/desktop_tests.cpp src/drivers/framebuffer.cpp src/gui/modern_geometry.cpp \
      src/gui/modern_painter.cpp src/gui/modern_desktop.cpp src/gui/modern_render.cpp \
      src/common/boot_log.cpp src/apps/package.cpp src/apps/vm.cpp src/storage/appstore.cpp src/storage/settings.cpp src/i18n/*.cpp \
      src/process/native_windows.cpp src/process/native_surface.cpp -o "$build/sanitized"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 run_host_binary "$build/sanitized"
fi
