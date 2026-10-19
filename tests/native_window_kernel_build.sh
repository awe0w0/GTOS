#!/bin/bash
# Build the GTOS kernel into a caller-provided directory without touching the
# default obj/ tree or the repository images.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo "usage: native_window_kernel_build.sh FRESH_OUTPUT_DIR" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
if [ -e "$out/GTOS.bin" ]; then
  echo "Refusing to reuse a directory that already holds a kernel: $out" >&2
  exit 2
fi
mkdir -p "$out"
cd "$repo"
# Build only the kernel binary: the default target also builds GTOS.iso, which
# needs grub-mkrescue and would touch the repository image.
make -j8 KERNEL_OBJDIR="$out/obj" KERNEL_BINARY="$out/GTOS.bin" \
  CXX=/mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++ \
  PYTHON=/usr/bin/python3 "$out/GTOS.bin" 2>&1 | tee "$out/build.log"
