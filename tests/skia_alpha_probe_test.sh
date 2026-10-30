#!/bin/sh
# The Skia alpha unit: the host-side pixel arithmetic, built at both optimisation levels with the sanitizers the real suite uses.
#
# Building and starting are two steps when the caller says so. Under an execution allow-list the program a step has just linked
# cannot be started by that step — it was not on the list when the list was made — so `build` compiles and publishes each image,
# and `run` starts only those published, read-only images. With no argument the script builds and runs in one go, which is what an
# ordinary host and the repository's own `make test` do.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
. ./tests/host-runner.sh
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: skia_alpha_probe_test.sh [all|build|run]" >&2; exit 2 ;;
esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if [ "$stage" = build ] || [ "$stage" = all ]; then
    for opt in 0 2; do
        "${CXX:-g++}" -std=c++11 "-O$opt" -g -Wall -Wextra -Werror \
            -fsanitize=address,undefined -fno-pie -no-pie \
            "$repo/apps/skia_alpha_probe/skia_pixel.cc" \
            "$repo/apps/skia_alpha_probe/skia_alpha_host.cpp" -o "$work/host-O$opt"
    done
fi
if [ "$stage" = build ]; then
    for opt in 0 2; do
        stage_binary "skia_host_O$opt" "$work/host-O$opt"
    done
    exit 0
fi
if [ "$stage" = run ]; then
    for opt in 0 2; do
        image=$(staged_binary "skia_host_O$opt")
        run_host_binary "$image"
    done
    printf 'Skia alpha host O0/O2 ASan/UBSan passed; GTOS guest acceptance is separate.\n'
    exit 0
fi

for opt in 0 2; do
    "$work/host-O$opt"
done
printf 'Skia alpha host O0/O2 ASan/UBSan passed; GTOS guest acceptance is separate.\n'
