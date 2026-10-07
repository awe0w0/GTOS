#!/bin/sh
# New boot-log boundary/oracle tests only; no existing regression workloads.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-boot-log.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for optimization in 0 2; do
    "${CXX:-g++}" -m64 -std=c++11 -O"$optimization" -Wall -Wextra -Werror \
        -DGTOS_MEMORY_TEST -fsanitize=address,undefined -fno-omit-frame-pointer \
        -fno-pie -no-pie -Iinclude tests/boot_log_host.cpp \
        src/common/boot_log.cpp -o "$work/boot-log"
    "$work/boot-log"
done
