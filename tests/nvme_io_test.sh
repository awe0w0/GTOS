#!/bin/sh
# Production NVM transfer and FileStore paths with an MMIO/device boundary spy.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
. ./tests/i386-launcher.sh
case ${1:-} in '') mode=all ;; all|build|run) mode=$1;shift ;; *) exit 2 ;; esac
compile_image() {
    directory=$1;width=$2;optimization=$3
    flags="-m$width -O$optimization -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-pie"
    link_flags='-no-pie -Wl,--gc-sections'
    runtime=
    if [ "$width" = 64 ]; then
        flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer"
        output="$directory/nvme-io-O$optimization"
    else
        flags="$flags -ffreestanding -fno-builtin -fno-stack-protector"
        link_flags="$link_flags -nostdlib -static -Wl,-e,_start"
        runtime='tests/settings_runtime.cpp tests/i386_pure_virtual_runtime.cpp'
        output="$directory/nvme-io-i386"
    fi
    for name in lfs lfs_util; do
        "${CC:-gcc}" $flags -std=c99 -nostdinc -Iinclude/storage/littlefs-sdk \
            -DLFS_DEFINES=storage/littlefs_port.h -Iinclude -Iinclude/storage/littlefs \
            -c "src/storage/littlefs/$name.c" -o "$directory/$name.o"
    done
    "${CXX:-g++}" $flags -std=c++11 -DGTOS_NVME_TEST -DGTOS_MEMORY_TEST \
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
        -DLFS_CONFIG=storage/littlefs_port.h -Iinclude \
        tests/nvme_io_test.cpp src/drivers/nvme.cpp src/storage/filestore.cpp \
        src/storage/littlefs_port.cpp "$directory/lfs.o" "$directory/lfs_util.o" \
        $runtime $link_flags -o "$output"
}
compile() {
    for optimization in 0 2; do compile_image "$1" 64 "$optimization"; done
    compile_image "$1" 32 2
}
start() {
    ELF32_I386_LAUNCHER=${GTOS_QEMU_I386:-${ELF32_I386_LAUNCHER:-}}
    choice=$(i386_launcher_choose run) || exit 2
    launcher=$(i386_launcher_path "$choice")
    image_o0=$(staged_binary nvme-io-O0) || exit 2
    image_o2=$(staged_binary nvme-io-O2) || exit 2
    image_i386=$(staged_binary nvme-io-i386) || exit 2
    for image in "$image_o0" "$image_o2" "$image_i386"; do verify_publish_record "$image" || exit 2; done
    GTOS_HOST_LAUNCHER= run_published_binary "$image_o0"
    GTOS_HOST_LAUNCHER= run_published_binary "$image_o2"
    GTOS_HOST_LAUNCHER=$launcher run_published_binary "$image_i386"
}
case $mode in
all)
    [ "$#" -eq 0 ] || exit 2
    work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-nvme-io.XXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    compile "$work"
    for optimization in 0 2; do "$work/nvme-io-O$optimization"; done
    ELF32_I386_LAUNCHER=${GTOS_QEMU_I386:-${ELF32_I386_LAUNCHER:-}}
    choice=$(i386_launcher_choose all) || exit 2
    if i386_launcher_is_native "$choice"; then "$work/nvme-io-i386"
    else "$(i386_launcher_path "$choice")" "$work/nvme-io-i386"; fi
    ;;
build)
    [ "$#" -eq 1 ] || exit 2
    [ ! -e "$1" ] || { echo "NVMe I/O build directory already exists: $1" >&2;exit 2; }
    mkdir "$1"
    compile "$1"
    for name in O0 O2 i386; do stage_binary "nvme-io-$name" "$1/nvme-io-$name"; done
    ;;
run)
    [ "$#" -eq 0 ] || exit 2
    start
    ;;
esac
