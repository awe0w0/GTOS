#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-elf32.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
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
        "${CXX:-g++}" $machine -std=c++11 -O"$optimization" -ffreestanding -nostdlib \
            -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
            -fno-threadsafe-statics -Wall -Wextra -Werror -Iinclude -I"$build" \
            -Wl,-e,_start -Wl,--build-id=none tests/elf32_tests.cpp src/process/elf32.cpp \
            -o "$build/test_${target}_O$optimization"
        echo "ELF32 $target freestanding -O$optimization"
        if [ "$target" = i386 ] && command -v qemu-i386 >/dev/null 2>&1; then
            qemu-i386 "$build/test_${target}_O$optimization"
        elif [ "$target" = i386 ] && [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
            LD_LIBRARY_PATH=../gtos-runtime/root/usr/lib/x86_64-linux-gnu \
                ../gtos-runtime/root/usr/bin/qemu-i386 "$build/test_${target}_O$optimization"
        else
            "$build/test_${target}_O$optimization"
        fi
    done
done
# Required by default. Set ELF32_SANITIZERS=0 only for toolchains without runtimes;
# the explicit message keeps that run from being mistaken for sanitizer coverage.
if [ "${ELF32_SANITIZERS:-1}" = 1 ]; then
    "${CXX:-g++}" -std=c++11 -O1 -g -fno-builtin -fno-exceptions -fno-rtti \
        -fno-omit-frame-pointer -fno-pie -no-pie -Wall -Wextra -Werror \
        -fsanitize=address,undefined -DELF32_HOSTED -DELF32_SANITIZED \
        -Iinclude -I"$build" tests/elf32_tests.cpp src/process/elf32.cpp -o "$build/sanitized"
    echo 'ELF32 host ASan/UBSan with poisoned truncation tails'
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$build/sanitized"
else
    echo 'ELF32 sanitizers explicitly skipped (ELF32_SANITIZERS=0)'
fi
