#!/bin/bash
# Build the reclaim-diagnostic kernel and its two diagnostic CPL3 payloads into a
# caller-provided directory. Nothing in the repository obj/ tree, the repository
# images, or the default fault/peer modules is touched.
#
#   native_window_reclaim_build.sh FRESH_OUT
#
# FRESH_OUT must not exist. The kernel is built from the unchanged sources with
# only -DGTOS_NATIVE_RECLAIM=1, so the ordinary audited kernel is untouched.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo "usage: native_window_reclaim_build.sh FRESH_OUT" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
case "$out" in
  */../*|*/..|../*|..) echo "Refusing a path containing ..: $out" >&2; exit 2 ;;
esac
if [ -e "$out" ]; then
  echo "Refusing to reuse an existing output directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
cd "$repo"
make -j8 KERNEL_OBJDIR="$out/obj" \
  RECLAIM_BINARY="$out/GTOS-native-reclaim.bin" \
  RECLAIM_ISO="$out/GTOS-native-reclaim.iso" \
  CXX=/mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++ \
  AS=/usr/bin/as LD=/usr/bin/ld PYTHON=/usr/bin/python3 \
  "$out/GTOS-native-reclaim.iso" 2>&1 | tee "$out/build.log"
