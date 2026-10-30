#!/bin/sh
# Compiler, linker and inspection only: one Rust library compiled for i686-unknown-gtos, one C caller compiled for the same
# soft-float choice, one linked ELF32 image. No guest program is started, and nothing here claims GTOS accepts the image.
#
# What this can prove: the C declarations and the Rust definitions agree well enough to link with no undefined symbols, both sides
# assert the same struct layout, the call sites exist, and no x87 instruction appears. It also records, instead of hiding, where the
# float boundary currently stops: the soft-float helpers exist in compiler_builtins only under their Rust-mangled names, so a C
# caller's unmangled reference is unresolved. That is a real gap, asserted by name.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${RUST_LIBS:-}" ]; then
    echo 'skip rust ABI consumer: RUST_LIBS is not set, so this checkout has no i686-unknown-gtos core to build against'
    exit 0
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-rust-abi.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2" >&2; failures=$((failures + 1)); fi; }

# The builder is a shell script and the repository mount is not executable, so it is read by an interpreter, not exec'd.
sh "$repo/tools/build-rust-abi-probe.sh" "$work/first" int > "$work/first.log" 2>&1 || { sed -n '1,200p' "$work/first.log" >&2; exit 1; }
sh "$repo/tools/build-rust-abi-probe.sh" "$work/second" int > "$work/second.log" 2>&1 || { sed -n '1,200p' "$work/second.log" >&2; exit 1; }

cmp -s "$work/first/abi-consumer.elf" "$work/second/abi-consumer.elf"
check $? 'two builds of the same sources produce the same image'

if [ -s "$work/first/undefined.txt" ]; then
    echo 'FAIL the linked image has undefined symbols:' >&2
    sed 's/^/    /' "$work/first/undefined.txt" >&2
    failures=$((failures + 1))
else
    echo 'ok the linked image has no undefined symbols'
fi

grep -q 'Class:.*ELF32' "$work/first/readelf.txt"; check $? 'the image is ELF32'
grep -q 'Machine:.*Intel 80386' "$work/first/readelf.txt"; check $? 'the image is i386'
grep -q 'Type:.*EXEC' "$work/first/readelf.txt"; check $? 'the image is a static executable'
grep -q 'Entry point address: *0x40000000' "$work/first/readelf.txt"; check $? 'the image enters at 0x40000000'
if grep -q 'INTERP' "$work/first/readelf.txt"; then
    echo 'FAIL the image asks for an interpreter' >&2; failures=$((failures + 1))
else
    echo 'ok the image asks for no interpreter'
fi

for symbol in _start gtos_abi_add gtos_abi_pair gtos_abi_store; do
    if grep -Eq "\b$symbol\b" "$work/first/readelf.txt"; then echo "ok $symbol is defined in the image"; else
        echo "FAIL $symbol is missing from the image" >&2; failures=$((failures + 1)); fi
done

calls=$(grep -Ec 'call.*<gtos_abi_(add|pair|store)>' "$work/first/disassembly.txt" || true)
if [ "$calls" -ge 3 ]; then echo "ok the C side calls all three integer Rust functions ($calls call sites)"; else
    echo "FAIL the C side has only $calls Rust call sites" >&2; failures=$((failures + 1)); fi

# No x87 instruction anywhere: the target spec asks for soft float, so the C side is compiled with -msoft-float and the Rust side
# with the spec's +soft-float. Codegen that reached for x87 would mean the two sides disagree about how a float travels.
if grep -Eq '\b(fld|fst|fstp|fadd|fsub|fmul|fdiv|fild|fistp)[a-z]*\b' "$work/first/disassembly.txt"; then
    echo 'FAIL the image contains x87 instructions, so the soft-float choice did not take' >&2
    failures=$((failures + 1))
else
    echo 'ok the image contains no x87 instructions'
fi
if grep -Eq '__mulsf3|__addsf3|__floatunsisf' "$work/first/disassembly.txt"; then
    echo 'FAIL the integer image references a soft-float helper' >&2; failures=$((failures + 1))
else
    echo 'ok the integer image needs no soft-float helper'
fi

# The float boundary, recorded rather than assumed. Two cases:
#   plain   the library the target was built with exports the helpers only under Rust-mangled names, so the C reference is
#           unresolved; the test asserts which names the link reports.
#   fixed   a library built with compiler_builtins' `unmangled-names` feature makes the same link succeed; the test then asserts
#           the helpers are defined and the call site is real. RUST_BUILTINS_UNMANGLED says where that library is.
sh "$repo/tools/build-rust-abi-probe.sh" "$work/float" float > "$work/float.log" 2>&1 || { sed -n '1,200p' "$work/float.log" >&2; exit 1; }
if grep -q 'LINK FAILED' "$work/float.log"; then echo 'ok the float boundary does not link against the plain library'; else
    echo 'FAIL the float boundary linked against the plain library, so the gap this test records has changed' >&2; failures=$((failures + 1)); fi
# The measured set, not a guess: `weight * 2.0` folds into an addition, and the saturating `as u32` conversion is what brings in
# the comparisons. A future compiler changing this set is exactly what this check should report.
for helper in __addsf3 __gesf2 __gtsf2 __fixunssfsi; do
    if grep -q "$helper" "$work/float/link.err"; then echo "ok the float link names the missing helper $helper"; else
        echo "FAIL the float link does not name $helper" >&2; failures=$((failures + 1)); fi
done

if [ -n "${RUST_BUILTINS_UNMANGLED:-}" ]; then
    RUST_BUILTINS=$RUST_BUILTINS_UNMANGLED sh "$repo/tools/build-rust-abi-probe.sh" "$work/float-fixed" float \
        > "$work/float-fixed.log" 2>&1 || { sed -n '1,200p' "$work/float-fixed.log" >&2; exit 1; }
    if grep -q 'LINK FAILED' "$work/float-fixed.log"; then
        echo 'FAIL the float boundary still does not link with the unmangled library:' >&2
        sed -n '1,20p' "$work/float-fixed/link.err" >&2
        failures=$((failures + 1))
    else
        echo 'ok the float boundary links once the helpers carry their C ABI names'
    fi
    if [ -s "$work/float-fixed/undefined.txt" ]; then
        echo 'FAIL the fixed float image has undefined symbols:' >&2
        sed 's/^/    /' "$work/float-fixed/undefined.txt" >&2
        failures=$((failures + 1))
    else
        echo 'ok the fixed float image has no undefined symbols'
    fi
    # A weak definition satisfies the reference; the call has to reach it, so the helper must be defined in the image. readelf
    # prints the binding as its own column, which is why the pattern looks for WEAK/GLOBAL and the name at the end.
    if grep -Eq '(WEAK|GLOBAL).* (__addsf3)$' "$work/float-fixed/readelf.txt"; then echo 'ok __addsf3 is defined in the fixed float image'; else
        echo 'FAIL __addsf3 is not defined in the fixed float image' >&2; failures=$((failures + 1)); fi
    if grep -Eq 'call.*<gtos_abi_scale>' "$work/float-fixed/disassembly.txt"; then echo 'ok the C side calls gtos_abi_scale'; else
        echo 'FAIL the C side does not call gtos_abi_scale' >&2; failures=$((failures + 1)); fi
    if grep -Eq '\b(fld|fst|fstp|fadd|fsub|fmul|fdiv|fild|fistp)[a-z]*\b' "$work/float-fixed/disassembly.txt"; then
        echo 'FAIL the fixed float image contains x87 instructions' >&2; failures=$((failures + 1))
    else
        echo 'ok the fixed float image contains no x87 instructions'
    fi
else
    echo 'skip the unmangled-library float case: RUST_BUILTINS_UNMANGLED is not set'
fi

if [ "$failures" -ne 0 ]; then
    echo "rust ABI consumer: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS rust ABI consumer: integer/pointer/struct image links with no undefined symbols and no x87; the float boundary names its missing helpers against the plain library'
echo 'Guest execution, C++ objects, varargs and by-value struct passing remain untested by this script'
