#!/bin/sh
# New live module only: real RAM/AppStore/Settings and real ATA with counted ports.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-live-storage.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags=$(cat tools/kernel-cxxflags)
for optimization in 0 2; do
    "${CXX:-g++}" -m64 -std=c++11 -O"$optimization" -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
        -DGTOS_LIVE_RAM_ONLY -Iinclude tests/live_storage_host.cpp \
        src/storage/liveblockdevice.cpp src/storage/appstore.cpp \
        src/storage/settings.cpp src/apps/package.cpp -o "$work/ram"
    "$work/ram"
    "${CXX:-g++}" $flags -m32 -std=c++11 -O"$optimization" -Iinclude \
        -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector -fno-pie -no-pie \
        -nostdlib -static -Wall -Wextra -Werror -Wl,-e,_start \
        tests/live_storage_host.cpp tests/settings_runtime.cpp src/drivers/ata.cpp \
        src/storage/liveblockdevice.cpp src/storage/appstore.cpp \
        src/storage/settings.cpp src/apps/package.cpp -o "$work/full-i386"
    if command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$work/full-i386"
    else
        "$work/full-i386"
    fi
done
