#!/bin/sh
# Real ATA batch transfers and cache at both optimizations, plus freestanding i386.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
. ./tests/i386-launcher.sh
case ${1:-} in '') mode=all ;; all|build|run) mode=$1;shift ;; *) exit 2 ;; esac
compile() {
    for optimization in 0 2; do
        "${CXX:-g++}" -m64 -O"$optimization" -std=c++11 -Wall -Wextra -Werror \
            -DGTOS_ATA_HOST_TEST -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
            -DLFS_CONFIG=storage/littlefs_port.h -Iinclude \
            tests/ata_read_test.cpp src/drivers/ata.cpp src/storage/littlefs_port.cpp \
            -o "$1/ata-read-O$optimization"
    done
    "${CXX:-g++}" -m32 -O2 -std=c++11 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector -fno-pie -no-pie \
        -nostdlib -static -Wl,-e,_start -DLFS_CONFIG=storage/littlefs_port.h -Iinclude \
        tests/ata_read_test.cpp src/drivers/ata.cpp \
        src/storage/littlefs_port.cpp tests/settings_runtime.cpp tests/i386_pure_virtual_runtime.cpp \
        -o "$1/ata-read-i386"
}
start() {
    ELF32_I386_LAUNCHER=${GTOS_QEMU_I386:-${ELF32_I386_LAUNCHER:-}}
    choice=$(i386_launcher_choose run) || exit 2
    launcher=$(i386_launcher_path "$choice")
    image_o0=$(staged_binary ata-read-O0) || exit 2
    image_o2=$(staged_binary ata-read-O2) || exit 2
    image_i386=$(staged_binary ata-read-i386) || exit 2
    for image in "$image_o0" "$image_o2" "$image_i386"; do verify_publish_record "$image" || exit 2; done
    GTOS_HOST_LAUNCHER= run_published_binary "$image_o0"
    GTOS_HOST_LAUNCHER= run_published_binary "$image_o2"
    GTOS_HOST_LAUNCHER=$launcher run_published_binary "$image_i386"
}
case $mode in
all)
    [ "$#" -eq 0 ] || exit 2
    work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-ata-read.XXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    compile "$work"
    for optimization in 0 2; do "$work/ata-read-O$optimization"; done
    ELF32_I386_LAUNCHER=${GTOS_QEMU_I386:-${ELF32_I386_LAUNCHER:-}}
    choice=$(i386_launcher_choose all) || exit 2
    if i386_launcher_is_native "$choice"; then "$work/ata-read-i386"
    else "$(i386_launcher_path "$choice")" "$work/ata-read-i386"; fi
    ;;
build)
    [ "$#" -eq 1 ] || exit 2
    [ ! -e "$1" ] || { echo "ATA read build directory already exists: $1" >&2;exit 2; }
    mkdir "$1"
    compile "$1"
    for name in O0 O2 i386; do stage_binary "ata-read-$name" "$1/ata-read-$name"; done
    ;;
run)
    [ "$#" -eq 0 ] || exit 2
    start
    ;;
esac
