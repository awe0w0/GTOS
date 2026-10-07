#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
python3 tests/desktop_catalog_test.py
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-desktop.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
compile() {
    "${CXX:-g++}" $kernel_flags "$1" -std=c++11 -O2 -Iinclude -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST \
      -mstackrealign -ffreestanding -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector \
      -fno-pie -no-pie -nostdlib -static -Wall -Wextra \
      -Werror -Wl,-e,_start tests/desktop_tests.cpp src/drivers/framebuffer.cpp \
      src/gui/modern_geometry.cpp src/gui/modern_painter.cpp src/gui/modern_desktop.cpp \
      src/gui/modern_render.cpp src/common/boot_log.cpp src/apps/package.cpp src/apps/vm.cpp src/storage/appstore.cpp src/storage/settings.cpp src/i18n/*.cpp -o "$2"
}
compile -m32 "$build/test32"
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
        "$build/test64"
    else
        [ "$status" -eq 0 ] || exit "$status"
    fi
fi

compile -m64 "$build/test64"
"$build/test64"
if [ "${GTOS_DESKTOP_SANITIZERS:-0}" = 1 ]; then
    "${CXX:-g++}" $kernel_flags -m64 -std=c++11 -O1 -g -Iinclude \
      -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST -DGTOS_DESKTOP_SANITIZE -fno-exceptions -fno-rtti \
      -fno-builtin -fno-pie -no-pie -Wall -Wextra -Werror \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      tests/desktop_tests.cpp src/drivers/framebuffer.cpp src/gui/modern_geometry.cpp \
      src/gui/modern_painter.cpp src/gui/modern_desktop.cpp src/gui/modern_render.cpp \
      src/common/boot_log.cpp src/apps/package.cpp src/apps/vm.cpp src/storage/appstore.cpp src/storage/settings.cpp src/i18n/*.cpp -o "$build/sanitized"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build/sanitized"
fi
