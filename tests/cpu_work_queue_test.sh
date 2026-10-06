#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
queue_include=${GTOS_CPU_WORK_QUEUE_INCLUDE:-include}
queue_source=${GTOS_CPU_WORK_QUEUE_SOURCE:-src/hardwarecommunication/cpu_work_queue.cpp}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for optimization in 0 2; do
    for include_first in 0 1; do
        define=""
        if [ "$include_first" = 1 ]; then define="-DGTOS_QUEUE_INCLUDE_FIRST"; fi
        ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O"$optimization" $define -I"$queue_include" -Iinclude -Wall -Wextra -Werror \
            -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-exceptions -fno-rtti \
            -c tests/cpu_work_queue_include.cpp -o "$work/include-order.o"
    done
    ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O"$optimization" -I"$queue_include" -Iinclude -Wall -Wextra -Werror \
        -ffreestanding -fno-pie -no-pie -nostdlib -static -fno-stack-protector \
        -fno-builtin -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
        -fno-threadsafe-statics -mno-sse -mno-mmx -msoft-float \
        tests/cpu_work_queue_tests.cpp "$queue_source" \
        -o "$work/cpu-work-queue-tests"
    echo "CPU work queue: -O$optimization"
    if [ -n "${GTOS_QEMU_I386:-}" ]; then
        "$GTOS_QEMU_I386" "$work/cpu-work-queue-tests"
    elif command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$work/cpu-work-queue-tests"
    else
        "$work/cpu-work-queue-tests"
    fi
done
