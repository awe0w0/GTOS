#!/bin/sh
# The target's Rust runtime library, built from the toolchain's own sources with rustc alone.
#
# What this proves: the C ABI names the port needs (`__addsf3` and friends) really are one cfg away from the same sources the
# toolchain ships, so the port does not need Cargo, network access or a vendored crate to get them; two builds of the same inputs
# produce the same library; and the library the target was built with by default does not carry those names, which is the contrast
# that makes the first fact meaningful.
#
# Needs RUSTC, RUST_LIBS (libcore.rlib), RUST_SRC (the compiler_builtins source directory) and TARGET_SPEC.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${RUST_LIBS:-}" ] || [ -z "${RUST_SRC:-}" ]; then
    echo 'skip rust runtime library: RUST_LIBS or RUST_SRC is not set, so this checkout has no target core or toolchain sources'
    exit 0
fi
: "${RUSTC:?RUSTC must name the target rustc}"
: "${RUST_SRC:?RUST_SRC must name compiler_builtins/src from the toolchain's rust-src}"
spec=${TARGET_SPEC:-"$repo/apps/native_content_build/rust/i686-unknown-gtos.json"}
ar=${AR:-ar}
nm=${NM:-nm}
work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-rustlib.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2" >&2; failures=$((failures + 1)); fi; }

sh "$repo/tools/build-gtos-rustlib.sh" "$work/first" "$spec" "$RUST_LIBS/libcore.rlib" "$RUSTC" "$RUST_SRC" > "$work/first.log" 2>&1 \
    || { sed -n '1,40p' "$work/first.log" >&2; exit 1; }
sh "$repo/tools/build-gtos-rustlib.sh" "$work/second" "$spec" "$RUST_LIBS/libcore.rlib" "$RUSTC" "$RUST_SRC" > "$work/second.log" 2>&1 \
    || { sed -n '1,40p' "$work/second.log" >&2; exit 1; }

cmp -s "$work/first/libcompiler_builtins_unmangled.rlib" "$work/second/libcompiler_builtins_unmangled.rlib"
check $? 'two builds of the same sources produce the same library'

for helper in __addsf3 __gesf2 __fixunssfsi; do
    if grep -Eq " [WT] $helper$" "$work/first/symbols.txt"; then echo "ok $helper is exported under its C ABI name"; else
        echo "FAIL $helper is not exported under its C ABI name" >&2; failures=$((failures + 1)); fi
done

# The contrast: the library the target was built with by default keeps those helpers under Rust-mangled names.
if [ -f "$RUST_LIBS/libcompiler_builtins.rlib" ]; then
    "$nm" --defined-only "$RUST_LIBS/libcompiler_builtins.rlib" > "$work/default-symbols.txt" 2>/dev/null || true
    if grep -qE ' [WT] __addsf3$' "$work/default-symbols.txt"; then
        echo 'FAIL the default library already exports __addsf3, so the contrast this test draws is gone' >&2
        failures=$((failures + 1))
    else
        echo 'ok the default library does not export __addsf3 under its C ABI name'
    fi
else
    echo 'skip the contrast: RUST_LIBS has no libcompiler_builtins.rlib'
fi

# The objects have to be for the target, not the host: the archive's members are ELF32 i386 objects.
# `ls` is not available in the restricted runs this unit is meant for, so the first member comes from a shell glob.
mkdir -p "$work/extract"
member=
(cd "$work/extract" && "$ar" x "$work/first/libcompiler_builtins_unmangled.rlib")
for candidate in "$work/extract"/*.o; do
    [ -e "$candidate" ] || continue
    member=${candidate##*/}
    break
done
if [ -n "$member" ]; then
    readelf -h "$work/extract/$member" > "$work/header.txt" 2>/dev/null || true
    grep -q 'Class:.*ELF32' "$work/header.txt"
    check $? "$member is an ELF32 object"
    grep -q 'Machine:.*Intel 80386' "$work/header.txt"
    check $? "$member is an i386 object"
else
    echo 'FAIL the archive has no object members' >&2
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "rust runtime library: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS rust runtime library: compiler_builtins for i686-unknown-gtos exports its C ABI names, is reproducible, and is a target object'
echo 'Linking a guest image against it, mem* intrinsics from the optional C sources and the rest of the standard library remain untested here'
