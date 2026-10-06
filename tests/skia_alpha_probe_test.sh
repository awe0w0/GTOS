#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for opt in 0 2; do
    "${CXX:-g++}" -std=c++11 "-O$opt" -g -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-pie -no-pie \
        "$repo/apps/skia_alpha_probe/skia_pixel.cc" \
        "$repo/apps/skia_alpha_probe/skia_alpha_host.cpp" -o "$work/host-O$opt"
    "$work/host-O$opt"
done
printf 'Skia alpha host O0/O2 ASan/UBSan passed; GTOS guest acceptance is separate.\n'
