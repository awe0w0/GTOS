#!/bin/sh
# Builds the cross-boundary consumer: Rust exports compiled for i686-unknown-gtos, a C caller compiled for the same soft-float
# choice the target spec names, and one linked ELF32 image.
#
#   build-rust-abi-probe.sh <out> [int|float|run]
#
#   int    integer, pointer and struct exports: expected to link and be inspected.
#   float  the float export alone: the objects must compile, and the link is recorded rather than hidden, because the soft-float
#          helpers are currently exported from compiler_builtins only under their Rust-mangled names.
#   run    the int exports again, with the C side built to write every returned value out and exit. The objects are the same ones
#          the int build links; only the C side's entry point differs.
#
# The Rust side needs the target's core and compiler_builtins, which are not installed anywhere by default; RUSTC and RUST_LIBS
# name them. The compiler and linker default to the ones a public checkout has.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$repo/obj/rust-abi"}
mode=${2:-int}
case $mode in
    int|float|run) ;;
    *) echo "usage: build-rust-abi-probe.sh <out> [int|float|run]" >&2; exit 2 ;;
esac
mkdir -p "$out"
spec=$repo/apps/native_content_build/rust/i686-unknown-gtos.json
source_dir=$repo/apps/native_content_build/rust/abi
rustc=${RUSTC:-rustc}
libs=${RUST_LIBS:?RUST_LIBS must name the directory that holds libcore.rlib and libcompiler_builtins.rlib}
cc=${CC:-gcc}
ld=${LD:-ld}
nm=${NM:-nm}
# The helpers the Rust side calls: the library the target was built with by default, or one built with its C ABI names exported
# (compiler_builtins' `unmangled-names` feature, see harness-154/build-unmangled-builtins.sh).
builtins=${RUST_BUILTINS:-"$libs/libcompiler_builtins.rlib"}
c_extra=''
case $mode in
    float)
        rust_source=abi_float.rs
        c_source=consumer_float.c
        crate=gtos_abi_float
        ;;
    run)
        rust_source=abi_export.rs
        c_source=consumer.c
        crate=gtos_abi_export
        c_extra='-DGTOS_ABI_RUN'
        ;;
    *)
        rust_source=abi_export.rs
        c_source=consumer.c
        crate=gtos_abi_export
        ;;
esac

"$rustc" -Z unstable-options --target "$spec" --edition 2024 -O --emit=obj --crate-type=lib \
    --crate-name "$crate" \
    --extern core="$libs/libcore.rlib" \
    --extern compiler_builtins="$builtins" \
    -L "$libs" -o "$out/$crate.o" "$source_dir/$rust_source"

# shellcheck disable=SC2086
"$cc" -m32 -msoft-float -mno-sse -mno-mmx -mno-sse2 -std=c11 -Os -Wall -Wextra -Werror -ffreestanding -nostdlib \
    -fno-builtin -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables $c_extra \
    -c "$source_dir/$c_source" -o "$out/consumer.o"

if ! "$ld" -m elf_i386 --build-id=none -z max-page-size=4096 -T "$source_dir/link.ld" \
    -o "$out/abi-consumer.elf" "$out/consumer.o" "$out/$crate.o" "$builtins" 2> "$out/link.err"; then
    printf 'LINK FAILED for mode %s; see %s\n' "$mode" "$out/link.err"
    exit 0
fi
"${READELF:-readelf}" -h -l -s "$out/abi-consumer.elf" > "$out/readelf.txt"
"${OBJDUMP:-objdump}" -d -r "$out/abi-consumer.elf" > "$out/disassembly.txt"
"$nm" -u "$out/abi-consumer.elf" > "$out/undefined.txt"
sha256sum "$out/abi-consumer.elf" > "$out/sha256.txt"
printf 'Built ABI consumer (%s): %s\n' "$mode" "$out/abi-consumer.elf"
