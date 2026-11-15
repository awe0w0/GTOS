#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for optimization in 0 2; do
    "${CXX:-g++}" -m32 -O"$optimization" -std=c++11 -Iinclude -Wall -Wextra -Werror -Wno-unused-parameter \
        -ffreestanding -fno-pie -no-pie -nostdlib -static -fno-stack-protector -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=undefined -fsanitize-undefined-trap-on-error \
        tests/net_ipv4_test.cpp src/net/ipv4.cpp src/net/etherframe.cpp src/drivers/driver.cpp \
        src/hardwarecommunication/port.cpp -o "$work/net-ipv4"
    echo "IPv4 receive -O$optimization"
    if [ -n "${GTOS_QEMU_I386:-}" ]; then
        "$GTOS_QEMU_I386" "$work/net-ipv4"
    elif command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$work/net-ipv4"
    else
        "$work/net-ipv4"
    fi
done
