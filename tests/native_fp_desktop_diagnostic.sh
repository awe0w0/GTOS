#!/bin/sh
# Supplementary modern-desktop/AP diagnostic; never strict ownership acceptance.
set -eu
cd "$(dirname "$0")/.."
runtime="$(pwd)/../gtos-runtime"
if ! command -v grub-mkrescue >/dev/null 2>&1 && [ -x "$runtime/bin/grub-mkrescue" ]; then
    PATH="$runtime/bin:$PATH"; export PATH
fi
command -v grub-mkrescue >/dev/null 2>&1 || { echo 'grub-mkrescue required' >&2; exit 77; }
output=${GTOS_FP_DESKTOP_OUTPUT:-"$(pwd)/obj/native-fp-desktop-diagnostic-evidence"}
mkdir -p "$output"; output=$(cd "$output" && pwd)
git rev-parse HEAD > "$output/base-commit.txt"
printf '%s\n' 'QEMU POINTER-LIMITED DIAGNOSTIC. NOT STRICT FP OWNERSHIP ACCEPTANCE.' > "$output/qualification.txt"
for optimization in ${GTOS_FP_OPTIMIZATIONS:-0 2}; do
    case "$optimization" in 0|2) ;; *) echo 'Only O0 and O2 are supported' >&2; exit 2 ;; esac
    build="$output/O$optimization-build"
    evidence="$output/O$optimization"
    test ! -e "$evidence/apps.img" || { echo "Refusing reused evidence disk: $evidence/apps.img" >&2; exit 2; }
    mkdir -p "$build"
    manifest() {
        find src include -type f -print
        printf '%s\n' Makefile tools/kernel-cxxflags tools/audit-kernel-instructions.py \
            tests/native_fp_desktop_diagnostic.cpp tests/native_fp_desktop_diagnostic.s \
            tests/native_fp_desktop_user.cpp tests/native_fp_desktop_user.s \
            tests/native_fp_desktop_qemu.py tests/native_fp_desktop_diagnostic.sh \
            tests/qemu_smoke.py tests/desktop_qemu.py apps/native/start.s apps/native/image.ld
    }
    manifest | LC_ALL=C sort -u | xargs sha256sum > "$build/inputs.sha256"
    make -j"${GTOS_BUILD_JOBS:-4}" OPTIMIZATION="-O$optimization" \
        KERNEL_OBJDIR="$build/obj" \
        FP_DIAGNOSTIC_BINARY="$build/GTOS-native-fp-diagnostic.bin" \
        FP_DIAGNOSTIC_ISO="$build/GTOS-native-fp-diagnostic.iso" \
        "$build/GTOS-native-fp-diagnostic.iso" > "$build/build.log" 2>&1
    manifest | LC_ALL=C sort -u | xargs sha256sum > "$build/inputs-after.sha256"
    cmp "$build/inputs.sha256" "$build/inputs-after.sha256" || {
        echo 'Sources changed during build; rebuild the diagnostic fixture' >&2; exit 1;
    }
    # The same linked test image MUST fail the unmodified production invocation.
    if python3 tools/audit-kernel-instructions.py "$build/GTOS-native-fp-diagnostic.bin" \
        --map "$build/obj/native-fp-desktop-diagnostic/kernel.map" --source-root . \
        > "$build/production-audit-must-reject.log" 2>&1; then
        echo 'Diagnostic CPL0 probe incorrectly accepted by production audit' >&2; exit 1
    fi
    grep -q 'prohibited FP/SIMD' "$build/production-audit-must-reject.log"
    python3 tools/audit-kernel-instructions.py "$build/GTOS-native-fp-diagnostic.bin" \
        --map "$build/obj/native-fp-desktop-diagnostic/kernel.map" --source-root . \
        --allow-test-kernel-symbol native_fp_desktop_pointer_probe_asm \
        > "$build/diagnostic-audit.log"
    sha256sum "$build/GTOS-native-fp-diagnostic.bin" "$build/GTOS-native-fp-diagnostic.iso" \
        > "$build/artifacts.sha256"
    objdump -d "$build/GTOS-native-fp-diagnostic.bin" > "$build/kernel.disassembly.txt"
    python3 tests/native_fp_desktop_qemu.py --diagnostic \
        --iso "$build/GTOS-native-fp-diagnostic.iso" --output "$evidence" \
        > "$build/run.log" 2>&1 || { cat "$build/run.log"; exit 1; }
    cat "$build/run.log"
done
