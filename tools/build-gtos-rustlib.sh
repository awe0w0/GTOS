#!/bin/sh
# Builds compiler_builtins for a target with its C ABI names exported.
#
# Why this exists: the library that a plain rustc build of the standard library produces exports the soft-float helpers only under
# Rust-mangled names. Chromium's GTOS port needs the C names, because the object code it links is C and C++ as well as Rust. In
# compiler_builtins the C names sit behind the `unmangled-names` cargo feature (library/compiler-builtins/compiler-builtins/
# Cargo.toml), which rustc can be told about directly, so this needs no Cargo, no network and no vendored crate.
#
#   build-gtos-rustlib.sh <out-dir> <target-spec.json> <core-rlib> <rustc> <compiler_builtins/src>
#
# The output is one rlib plus a symbol listing. Whether it is usable is decided by the caller: the symbols it looks for are
# `__addsf3`, `__gesf2` and `__fixunssfsi`, and they are weak definitions, which is what a C reference links against.
set -eu
out=${1:?out-dir}
spec=${2:?target spec}
core=${3:?core rlib}
rustc=${4:?rustc}
src=${5:?compiler_builtins source directory}
ar=${AR:-ar}
nm=${NM:-nm}
mkdir -p "$out"
"$rustc" -Z unstable-options --target "$spec" --edition 2024 -O --crate-type=rlib --crate-name compiler_builtins \
    -C panic=abort -C embed-bitcode=no \
    --cfg 'feature="compiler-builtins"' --cfg 'feature="unmangled-names"' --cfg 'feature="arch"' \
    --extern core="$core" -o "$out/libcompiler_builtins_unmangled.rlib" "$src/lib.rs"
"$nm" --defined-only "$out/libcompiler_builtins_unmangled.rlib" > "$out/symbols.txt"
sha256sum "$out/libcompiler_builtins_unmangled.rlib" > "$out/sha256.txt"
"$ar" t "$out/libcompiler_builtins_unmangled.rlib" > "$out/members.txt"
printf 'built %s\n' "$out/libcompiler_builtins_unmangled.rlib"
printf 'C ABI helpers defined: '
grep -cE ' [WT] __(add|sub|mul|div|gesf2|gtsf2|lesf2|ltsf2|eqsf2|nesf2|fixunssfsi|fixsfsi|floatsisf|floatunsisf)$' \
    "$out/symbols.txt" || printf '0\n'
