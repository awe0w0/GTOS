#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
output=${GTOS_PROCESS_MEMORY_TEST_OUTPUT:-}
if [ -n "$output" ]; then
    if [ -e "$output" ]; then echo "Refusing to reuse test output: $output" >&2; exit 2; fi
    mkdir -p "$output"
    output=$(cd "$output" && pwd)
    ${CXX:-g++} --version > "$output/compiler.txt"
    git rev-parse HEAD > "$output/base-commit.txt"
fi
source_manifest() {
    sha256sum tests/process_memory_test.cpp tests/process_memory_test.sh \
        include/memory/process_address_space.h include/memory/paging.h include/memory/physical.h \
        include/memory/bootstrap.h include/memory/criticalsection.h include/common/types.h \
        src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp src/memory/bootstrap.cpp \
        tools/kernel-cxxflags
}
source_manifest > "$build/source-inputs.sha256"
if [ -n "$output" ]; then cp "$build/source-inputs.sha256" "$output/source-inputs.sha256"; fi
compile() {
    ${CXX:-g++} $kernel_flags -m32 -std=gnu++11 -O"$optimization" -ffreestanding -nostdlib \
        -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
        -fno-threadsafe-statics -Wall -Wextra -Werror \
        -DGTOS_MEMORY_TEST -DGTOS_PAGING_TEST -DGTOS_PROCESS_MEMORY_TEST \
        -Iinclude -Wl,-e,_start -Wl,--build-id=none \
        tests/process_memory_test.cpp src/memory/process_address_space.cpp \
        src/memory/paging.cpp src/memory/physical.cpp src/memory/bootstrap.cpp \
        -o "$build/process_memory_test_O$optimization"
}
run_binary() {
    if command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$build/process_memory_test_O$optimization"
    elif [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
        LD_LIBRARY_PATH=../gtos-runtime/root/usr/lib/x86_64-linux-gnu \
            ../gtos-runtime/root/usr/bin/qemu-i386 "$build/process_memory_test_O$optimization"
    else
        "$build/process_memory_test_O$optimization"
    fi
}
for optimization in 0 2; do
    if [ -n "$output" ]; then
        if compile > "$output/build-O$optimization.log" 2>&1; then :; else
            result=$?; cat "$output/build-O$optimization.log"; exit "$result"
        fi
        cp "$build/process_memory_test_O$optimization" "$output/"
        readelf -h "$build/process_memory_test_O$optimization" > "$output/readelf-O$optimization.log"
        if run_binary > "$output/run-O$optimization.log" 2>&1; then result=0; else result=$?; fi
        printf '%s\n' "$result" > "$output/exit-O$optimization.txt"
        cat "$output/run-O$optimization.log"
        if [ "$result" -ne 0 ]; then exit "$result"; fi
    else
        compile
        run_binary
    fi
    source_manifest > "$build/source-after.sha256"
    cmp "$build/source-inputs.sha256" "$build/source-after.sha256" || {
        echo 'FAIL: memory test source changed during execution' >&2; exit 1;
    }
done
if [ -n "$output" ]; then
    sha256sum "$output/process_memory_test_O0" "$output/process_memory_test_O2" > "$output/binary-hashes.txt"
    printf '%s\n' '{"all_required_checks_pass":true,"architecture":"actual ELF32 i386 host","optimizations":["O0","O2"],"sanitizers":[],"scope":"real process-address-space and allocator; hardware register/invlpg hooks only; no guest proof"}' > "$output/status.json"
fi
