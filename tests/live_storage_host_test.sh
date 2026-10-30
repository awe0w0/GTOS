#!/bin/sh
# The live storage module: real RAM, AppStore, Settings and ATA with counted ports, at two optimisation levels, building both the
# sanitized 64-bit host module and the freestanding 32-bit one.
#
# Three modes, like the other units that used to build and start in one step: `all` keeps the old behaviour for a normal host,
# `build <dir>` keeps the four images and publishes them through the supply, and `run` starts only the published copies. The 32-bit
# image is a freestanding executable with its own entry, so the run step uses the same launcher choice the rest of the suite uses.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

stage=${1:-all}
# Read the flag file with the shell rather than with cat, so this script needs no external command for its own flags.
IFS= read -r kernel_flags < tools/kernel-cxxflags

compile_ram() { # <optimization> <output>
    "${CXX:-g++}" -m64 -std=c++11 -O"$1" -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
        -DGTOS_LIVE_RAM_ONLY -Iinclude tests/live_storage_host.cpp \
        src/storage/liveblockdevice.cpp src/storage/appstore.cpp \
        src/storage/settings.cpp src/apps/package.cpp -o "$2"
}

compile_i386() { # <optimization> <output>
    ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O"$1" -Iinclude \
        -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector -fno-pie -no-pie \
        -nostdlib -static -Wall -Wextra -Werror -Wl,-e,_start \
        tests/live_storage_host.cpp tests/i386_pure_virtual_runtime.cpp tests/settings_runtime.cpp src/drivers/ata.cpp \
        src/storage/liveblockdevice.cpp src/storage/appstore.cpp \
        src/storage/settings.cpp src/apps/package.cpp -o "$2"
}

run_ram() { # <binary>
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$1"
}

run_i386() { # <binary>
    if command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$1"
    else
        "$1"
    fi
}

case $stage in
all)
    work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-live-storage.XXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    for optimization in 0 2; do
        compile_ram "$optimization" "$work/ram"
        run_ram "$work/ram"
        compile_i386 "$optimization" "$work/full-i386"
        run_i386 "$work/full-i386"
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
        compile_ram "$optimization" "$out/ram-O$optimization"
        stage_binary "live-storage-ram-O$optimization" "$out/ram-O$optimization"
        compile_i386 "$optimization" "$out/full-i386-O$optimization"
        stage_binary "live-storage-i386-O$optimization" "$out/full-i386-O$optimization"
    done
    ;;
run)
    for optimization in 0 2; do
        # Each key is resolved in its own statement: inside a command's arguments a substitution does not propagate its exit status,
        # so a missing supply would start an empty pathname instead of refusing.
        ram=$(staged_binary "live-storage-ram-O$optimization") || exit 2
        printf 'running ram O%s %s\n' "$optimization" "$ram"
        run_ram "$ram"
        image=$(staged_binary "live-storage-i386-O$optimization") || exit 2
        printf 'running i386 O%s %s\n' "$optimization" "$image"
        run_i386 "$image"
    done
    ;;
*)
    echo "usage: $0 [all|build <directory>|run]" >&2
    exit 2
    ;;
esac
