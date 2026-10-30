#!/bin/sh
# The ELF32 unit: the same parser driven by a real i386 image, at both widths and both optimisation levels, with the host ASan/UBSan
# case required by default.
#
# Building and starting are two steps when the caller says so. Under an execution allow-list the image a step has just linked cannot
# be started by that step — it was not on the list when the list was made — so `build` compiles, publishes each image and records
# where it went, and `run` starts only those published, read-only images. With no argument the script does both in one go, which is
# what an ordinary host and the repository's own `make test` do.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: elf32_test.sh [all|build|run]" >&2; exit 2 ;;
esac
kernel_flags=$(cat tools/kernel-cxxflags)
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-elf32.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

# The i386 executor is named, never looked up: an i386 static ELF cannot start on this host, and a CI that changes its execution
# architecture because some emulator happens to be on PATH is not reporting the same thing twice.
if [ -n "${ELF32_I386_LAUNCHER:-}" ]; then
    i386_launcher=$ELF32_I386_LAUNCHER
elif [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
    i386_launcher=../gtos-runtime/root/usr/bin/qemu-i386
else
    i386_launcher=
fi

if [ "$stage" = build ] || [ "$stage" = all ]; then
    "${AS:-as}" --32 tests/elf32_fixture.s -o "$build/fixture.o"
    "${LD:-ld}" -melf_i386 --build-id=none -T tests/elf32_fixture.ld "$build/fixture.o" -o "$build/fixture.elf"
    "${PYTHON:-python3}" - "$build/fixture.elf" "$build/elf32_fixture.h" <<'PY'
import pathlib, sys
payload = pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[2]).write_text('static const unsigned char elf32Fixture[] = {\n' +
    ',\n'.join(','.join('0x%02x' % byte for byte in payload[start:start+16])
              for start in range(0, len(payload), 16)) + '\n};\n')
PY
    for target in host i386; do
        machine=
        if [ "$target" = i386 ]; then machine=-m32; fi
        for optimization in 0 2; do
            "${CXX:-g++}" $kernel_flags $machine -std=c++11 -O"$optimization" -ffreestanding -nostdlib \
                -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
                -fno-threadsafe-statics -Wall -Wextra -Werror -Iinclude -I"$build" \
                -Wl,-e,_start -Wl,--build-id=none tests/elf32_tests.cpp src/process/elf32.cpp \
                -o "$build/test_${target}_O$optimization"
        done
    done
    if [ "${ELF32_SANITIZERS:-1}" = 1 ]; then
        "${CXX:-g++}" $kernel_flags -std=c++11 -O1 -g -fno-builtin -fno-exceptions -fno-rtti \
            -fno-omit-frame-pointer -fno-pie -no-pie -Wall -Wextra -Werror \
            -fsanitize=address,undefined -DELF32_HOSTED -DELF32_SANITIZED \
            -Iinclude -I"$build" tests/elf32_tests.cpp src/process/elf32.cpp -o "$build/sanitized"
    fi
fi
if [ "$stage" = build ]; then
    for target in host i386; do
        for optimization in 0 2; do
            stage_binary "elf32_${target}_O${optimization}" "$build/test_${target}_O$optimization"
        done
    done
    if [ "${ELF32_SANITIZERS:-1}" = 1 ]; then
        stage_binary elf32_sanitized "$build/sanitized"
    else
        echo 'ELF32 sanitizers explicitly skipped (ELF32_SANITIZERS=0)'
    fi
    exit 0
fi
if [ "$stage" = run ]; then
    if [ -z "$i386_launcher" ]; then
        echo 'ELF32 i386 executor is not available: set ELF32_I386_LAUNCHER to a real qemu-i386' >&2
        exit 2
    fi
    for target in host i386; do
        for optimization in 0 2; do
            image=$(staged_binary "elf32_${target}_O${optimization}")
            echo "ELF32 $target freestanding -O$optimization"
            if [ "$target" = i386 ]; then
                GTOS_HOST_LAUNCHER=$i386_launcher run_host_binary "$image"
            else
                run_host_binary "$image"
            fi
        done
    done
    if [ "${ELF32_SANITIZERS:-1}" = 1 ]; then
        image=$(staged_binary elf32_sanitized)
        echo 'ELF32 host ASan/UBSan with poisoned truncation tails'
        UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 run_host_binary "$image"
    fi
    exit 0
fi

# all: build, then run the images this run produced (an ordinary host, no allow-list).
for target in host i386; do
    for optimization in 0 2; do
        echo "ELF32 $target freestanding -O$optimization"
        if [ "$target" = i386 ]; then
            if [ -z "$i386_launcher" ]; then
                echo 'ELF32 i386 executor is not available: set ELF32_I386_LAUNCHER to a real qemu-i386' >&2
                exit 2
            fi
            LD_LIBRARY_PATH=${ELF32_I386_LIBRARY_PATH:-../gtos-runtime/root/usr/lib/x86_64-linux-gnu} \
                "$i386_launcher" "$build/test_${target}_O$optimization"
        else
            "$build/test_${target}_O$optimization"
        fi
    done
done
if [ "${ELF32_SANITIZERS:-1}" = 1 ]; then
    echo 'ELF32 host ASan/UBSan with poisoned truncation tails'
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$build/sanitized"
else
    echo 'ELF32 sanitizers explicitly skipped (ELF32_SANITIZERS=0)'
fi
