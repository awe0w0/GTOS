#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-ata-wait.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for optimization in 0 2; do
    "${CXX:-g++}" -m64 -O"$optimization" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
        -Iinclude tests/ata_wait_test.cpp -o "$work/ata-wait"
    "$work/ata-wait"
done
