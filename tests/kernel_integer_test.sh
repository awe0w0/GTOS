#!/bin/sh
# Actual compiler policy and linked executable-code audits at both optimization levels.
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cxx=${CXX:-g++}
as=${AS:-as}
ld=${LD:-ld}
python=${PYTHON:-python3}

printf 'Kernel integer-only policy: %s\n' "$kernel_flags"
"$cxx" --version | head -n 1
# Compile real aggregate copies/zeroing, not just an empty option probe.
cat > "$work/copy.cpp" <<'CPP'
struct Image { unsigned words[136]; };
extern "C" void copy_image(Image* out, const Image* in) { *out = *in; }
extern "C" void zero_image(Image* out) {
    for (unsigned i = 0; i != 136; ++i) out->words[i] = 0;
}
CPP
cat > "$work/audit.ld" <<'LD'
ENTRY(copy_image)
SECTIONS {
    . = 0x100000;
    .text : { *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
LD
# These FP-looking bytes are data, even though the link merges them into .text.
cat > "$work/data.s" <<'ASM'
.section .rodata
.global intentional_data_bytes
intentional_data_bytes:
.byte 0xdb, 0xe3, 0x66, 0x0f, 0xef, 0xc0, 0xc5, 0xfc, 0x57, 0xc0
.section .note.GNU-stack,"",@progbits
ASM
"$as" --32 "$work/data.s" -o "$work/data.o"
for optimization in 0 2; do
    "$cxx" -m32 -std=c++11 -O"$optimization" -Werror -ffreestanding -fno-builtin \
        -fno-pie -fno-exceptions -fno-rtti -fno-stack-protector $kernel_flags \
        -c "$work/copy.cpp" -o "$work/copy.o"
    "$ld" -melf_i386 -T "$work/audit.ld" -Map "$work/copy.map" \
        "$work/copy.o" "$work/data.o" -o "$work/copy.bin"
    "$python" tools/audit-kernel-instructions.py --map "$work/copy.map" "$work/copy.bin"
    echo "PASS compiler accepted mandatory flags and integer aggregate code at O$optimization"
    # Separate temporary output roots avoid replacing the regular desktop build
    # or accidentally reusing objects compiled with the other optimization.
    if ! "${MAKE:-make}" --no-print-directory -j"${GTOS_BUILD_JOBS:-2}" \
        OPTIMIZATION="-O$optimization" KERNEL_OBJDIR="$work/O$optimization" \
        KERNEL_BINARY="$work/kernel-O$optimization.bin" "$work/kernel-O$optimization.bin" \
        > "$work/build-O$optimization.log" 2>&1; then
        cat "$work/build-O$optimization.log"
        exit 1
    fi
    grep '^PASS ' "$work/build-O$optimization.log"
    if [ -n "${GTOS_INTEGER_AUDIT_OUTPUT:-}" ]; then
        mkdir -p "$GTOS_INTEGER_AUDIT_OUTPUT"
        cp "$work/build-O$optimization.log" "$GTOS_INTEGER_AUDIT_OUTPUT/O$optimization.log"
        cp "$work/kernel-O$optimization.bin" "$GTOS_INTEGER_AUDIT_OUTPUT/"
        sha256sum "$GTOS_INTEGER_AUDIT_OUTPUT/kernel-O$optimization.bin"
    fi
done

make_fixture() {
    symbol=$1; instruction=$2
    cat > "$work/inject.s" <<ASM
.section .text
.global $symbol
.type $symbol,@function
$symbol:
    $instruction
    ret
.size $symbol,.-$symbol
.section .note.GNU-stack,"",@progbits
ASM
    "$as" --32 "$work/inject.s" -o "$work/inject.o"
    "$ld" -melf_i386 -T "$work/audit.ld" -Map "$work/inject.map" \
        "$work/copy.o" "$work/inject.o" -o "$work/inject.bin"
}
expect_rejected() {
    if "$python" tools/audit-kernel-instructions.py --map "$work/inject.map" \
        "$work/inject.bin" > "$work/rejected.log" 2>&1; then
        echo "FAIL audit admitted injected instruction: $1" >&2
        exit 1
    fi
    grep -q 'prohibited FP/SIMD' "$work/rejected.log" || { cat "$work/rejected.log"; exit 1; }
    printf 'PASS audit rejects %s\n' "$1"
}
# Cover implicit state operations as well as x87/MMX/XMM/YMM/ZMM/opmask operands.
while IFS= read -r instruction; do
    make_fixture kernel_injected_fp "$instruction"
    expect_rejected "$instruction"
done <<'ASM'
fninit
fwait
fldenv (%eax)
fxsave (%eax)
fxrstor (%eax)
ldmxcsr (%eax)
stmxcsr (%eax)
emms
pxor %mm0,%mm0
pxor %xmm0,%xmm0
vpxor %ymm0,%ymm0,%ymm0
vpxord %zmm0,%zmm0,%zmm0
kxnorw %k0,%k0,%k0
xsave (%eax)
xrstor (%eax)
vzeroupper
ASM
make_fixture native_fp_save_asm_lookalike 'fninit'
expect_rejected 'lookalike helper symbol'
# Copied AP bootstrap code resides in rodata, with mixed 16/32-bit code and a
# GDT payload. It is nevertheless code and must not escape the ordinary audit.
cat > "$work/inject.s" <<'ASM'
.section .rodata.ap_trampoline,"a",@progbits
.global gtos_ap_trampoline_start,gtos_ap_trampoline_protected,gtos_ap_trampoline_gdt
.code16
gtos_ap_trampoline_start:
    fninit
.code32
gtos_ap_trampoline_protected:
    ret
gtos_ap_trampoline_gdt:
    .byte 0xdb,0xe3
.section .note.GNU-stack,"",@progbits
ASM
"$as" --32 "$work/inject.s" -o "$work/inject.o"
"$ld" -melf_i386 -T "$work/audit.ld" -Map "$work/inject.map" \
    "$work/copy.o" "$work/inject.o" -o "$work/inject.bin"
expect_rejected '16-bit AP code held in rodata'
make_fixture native_fp_save_asm 'fninit'
"$python" tools/audit-kernel-instructions.py --map "$work/inject.map" "$work/inject.bin"
# An exact helper allowance must stop at its size, even in the same object.
cat >> "$work/inject.s" <<'ASM'
.section .text
.global kernel_after_helper
.type kernel_after_helper,@function
kernel_after_helper:
    pxor %xmm0,%xmm0
    ret
.size kernel_after_helper,.-kernel_after_helper
ASM
"$as" --32 "$work/inject.s" -o "$work/inject.o"
"$ld" -melf_i386 -T "$work/audit.ld" -Map "$work/inject.map" \
    "$work/copy.o" "$work/inject.o" -o "$work/inject.bin"
expect_rejected 'instruction immediately after sized helper'
make_fixture explicit_user_payload 'pxor %xmm0,%xmm0'
expect_rejected 'user payload without explicit allowance'
"$python" tools/audit-kernel-instructions.py --map "$work/inject.map" \
    --allow-user-symbol explicit_user_payload "$work/inject.bin"
# An FP signature can be unused (and therefore absent from machine code), so
# exercise the source-interface gate independently of instruction detection.
mkdir -p "$work/source/include" "$work/source/src"
printf 'double forbidden_kernel_api(double value);\n' > "$work/source/include/api.h"
if "$python" tools/audit-kernel-instructions.py --map "$work/copy.map" \
    --source-root "$work/source" "$work/copy.bin" > "$work/source-rejected.log" 2>&1; then
    echo 'FAIL audit admitted an unused floating-point kernel interface' >&2
    exit 1
fi
grep -q 'prohibited kernel FP interface/intrinsic' "$work/source-rejected.log" || {
    cat "$work/source-rejected.log"; exit 1;
}
echo 'PASS audit rejects unused floating-point kernel interfaces'
make_fixture loader 'fninit'
# Failed post-link audits must delete the target; a second make invocation must
# fail again instead of accepting the leftover ELF as already up to date.
for attempt in 1 2; do
    if "${MAKE:-make}" --no-print-directory OBJECTS="$work/inject.o" \
        KERNEL_OBJDIR="$work" KERNEL_BINARY="$work/make-guard.bin" \
        "$work/make-guard.bin" > "$work/make-rejected.log" 2>&1; then
        echo 'FAIL make admitted an injected kernel instruction' >&2
        exit 1
    fi
    grep -q 'prohibited FP/SIMD' "$work/make-rejected.log" || {
        cat "$work/make-rejected.log"; exit 1;
    }
    test ! -e "$work/make-guard.bin"
done
echo 'PASS rejected kernel builds leave no timestamp-current executable'
echo 'PASS kernel integer-only compiler, O0/O2 linked audit, data exclusion, and negative gates'
