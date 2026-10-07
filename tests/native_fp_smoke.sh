#!/bin/sh
# Enabled legacy FP ownership: real GRUB/i386 guests and independent CPL3 users.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
runtime="$(pwd)/../gtos-runtime"
emulator=${GTOS_FP_EMULATOR:-qemu}
bochs_root=${GTOS_BOCHS_ROOT:-"$(pwd)/../gtos-bochs/root"}
case "$emulator" in qemu|bochs) ;; *) echo "Unsupported FP emulator: $emulator" >&2; exit 2 ;; esac
if [ "$emulator" = bochs ] && { [ "${GTOS_FP_ALLOW_EMULATOR_POINTER_LIMITATION:-0}" != 0 ] || [ "${GTOS_FP_ALLOW_EMULATOR_XM_LIMITATION:-0}" != 0 ] || [ "${GTOS_FP_ALLOW_EMULATOR_MXCSR_LIMITATION:-0}" != 0 ]; }; then
    echo "Bochs proof is strict; emulator-waiver flags are not accepted" >&2; exit 2
fi
qemu=${GTOS_QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
grub=${GTOS_GRUB_MKRESCUE:-grub-mkrescue}
if [ -x "$runtime/root/usr/bin/qemu-system-x86_64" ] && [ "$qemu" = qemu-system-x86_64 ]; then
    qemu="$runtime/root/usr/bin/qemu-system-x86_64"
    export LD_LIBRARY_PATH="$runtime/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QEMU_MODULE_DIR="$runtime/root/usr/lib/x86_64-linux-gnu/qemu"
    GTOS_QEMU_DATA_DIR="$runtime/root/usr/share/qemu"
fi
if ! command -v "$grub" >/dev/null 2>&1 && [ -x "$runtime/bin/grub-mkrescue" ]; then grub="$runtime/bin/grub-mkrescue"; fi
if [ "$emulator" = qemu ]; then
command -v "$qemu" >/dev/null 2>&1 || { echo 'qemu-system-x86_64 required (i386 guest, AVX-capable virtual CPU)' >&2; exit 77; }
else
    test -x "$bochs_root/usr/bin/bochs-bin" || { echo "Unpacked official Bochs package required at $bochs_root" >&2; exit 77; }
fi
command -v "$grub" >/dev/null 2>&1 || { echo 'grub-mkrescue required' >&2; exit 77; }
output=${GTOS_FP_TEST_OUTPUT:-"$work/evidence"}
mkdir -p "$output"
output=$(cd "$output" && pwd)
if [ "$emulator" = qemu ]; then "$qemu" --version > "$output/qemu-version.txt"; fi
printf '%s\n' "$emulator" > "$output/emulator.txt"
git rev-parse HEAD > "$output/base-commit.txt"
git diff --stat > "$output/working-changes.txt"
git status --short > "$output/working-status.txt"
{
    ${CXX:-g++} --version
    as --version
    ld --version
    python3 --version
} > "$output/toolchain-version.txt"
cp tests/native_fp_smoke_qemu.py "$work/native_fp_smoke_qemu.py"
cp tests/native_fp_bochs.py "$work/native_fp_bochs.py"
flags=$(cat tools/kernel-cxxflags)
printf '%s\n' "$flags" > "$output/kernel-flags.txt"
for optimization in ${GTOS_FP_OPTIMIZATIONS:-0 2}; do
    mkdir -p "$work/O$optimization"; objects="$work/O$optimization"
    sources="src/gdt.cpp src/multitasking.cpp src/syscalls.cpp src/hardwarecommunication/interrupts.cpp
        src/hardwarecommunication/port.cpp src/process/native_runtime.cpp src/process/resources.cpp src/process/native_fp.cpp
        src/process/elf32.cpp src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp
        src/memory/bootstrap.cpp tests/native_fp_smoke.cpp"
    source_manifest() {
        {
            find include -type f -print
            printf '%s\n' $sources src/hardwarecommunication/interruptstubs.s src/process/native_fp.s \
                tests/native_fp_loader.s tests/native_fp_user.s tests/native_process_smoke.ld \
                tests/native_fp_smoke.sh tests/native_fp_smoke_qemu.py tests/native_fp_bochs.py tools/kernel-cxxflags \
                tools/audit-kernel-instructions.py
        } | LC_ALL=C sort -u | xargs sha256sum
    }
    source_manifest > "$output/O$optimization-inputs.sha256"
    for source in $sources; do
        ${CXX:-g++} -m32 -std=c++11 -O"$optimization" $flags -ffreestanding -nostdlib -fno-builtin \
            -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -fno-threadsafe-statics \
            -fno-use-cxa-atexit -fno-asynchronous-unwind-tables -Iinclude -Wno-write-strings \
            -Wall -Wextra -Werror -c "$source" -o "$objects/$(basename "$source" .cpp).o"
    done
    as --32 --defsym NATIVE_FP_KERNEL_MISUSE=0 tests/native_fp_loader.s -o "$objects/loader.o"
    as --32 tests/native_fp_user.s -o "$objects/user.o"
    as --32 src/hardwarecommunication/interruptstubs.s -o "$objects/stubs.o"
    as --32 src/process/native_fp.s -o "$objects/native_fp_asm.o"
    ld -melf_i386 -T tests/native_process_smoke.ld -Map "$objects/native.map" -o "$objects/native.bin" "$objects"/*.o
    python3 tools/audit-kernel-instructions.py "$objects/native.bin" --map "$objects/native.map" \
        --allow-user-symbol native_fp_user_start > "$output/O$optimization-audit.txt"
    sha256sum "$objects/native.bin" > "$output/O$optimization-elf.sha256"
    cp "$objects/native.bin" "$output/O$optimization-native.bin"
    cp "$objects/native.map" "$output/O$optimization-native.map"
    objdump -d "$objects/native.bin" > "$output/O$optimization-native.disassembly.txt"
    # Deliberate CPL0 FP exists only in this separately audited negative binary.
    guard="$work/guard-O$optimization"; mkdir -p "$guard"; cp "$objects"/*.o "$guard/"
    as --32 --defsym NATIVE_FP_KERNEL_MISUSE=1 tests/native_fp_loader.s -o "$guard/loader.o"
    ld -melf_i386 -T tests/native_process_smoke.ld -Map "$guard/native.map" -o "$guard/native.bin" "$guard"/*.o
    if python3 tools/audit-kernel-instructions.py "$guard/native.bin" --map "$guard/native.map" \
        --allow-user-symbol native_fp_user_start > "$output/O$optimization-guard-audit.txt" 2>&1; then
        echo 'FAIL: deliberately injected kernel FP escaped linked audit' >&2; exit 1
    fi
    test "$(grep -c 'prohibited FP/SIMD: fninit' "$output/O$optimization-guard-audit.txt")" -eq 1
    test "$(wc -l < "$output/O$optimization-guard-audit.txt")" -eq 1
    sha256sum "$guard/native.bin" > "$output/O$optimization-guard-elf.sha256"
    cp "$guard/native.bin" "$output/O$optimization-guard.bin"
    cp "$guard/native.map" "$output/O$optimization-guard.map"
    objdump -d "$guard/native.bin" > "$output/O$optimization-guard.disassembly.txt"
    source_manifest > "$objects/inputs-after.sha256"
    cmp "$output/O$optimization-inputs.sha256" "$objects/inputs-after.sha256" || {
        echo 'FAIL: source input changed during build; rebuild this fixture' >&2; exit 1;
    }
    mkdir -p "$work/iso/boot/grub"
    cp "$objects/native.bin" "$work/iso/boot/native.bin"
    run() {
        case_name=$1; memory=$2; cpus=$3; cpu=$4; suffix=$5
        name="O$optimization-$case_name-$memory-smp$cpus-$suffix"
        if [ -n "${GTOS_FP_CASE_FILTER:-}" ]; then
            case "$name" in *"$GTOS_FP_CASE_FILTER"*) ;; *) return ;; esac
        fi
        if [ "$case_name" = kernel-misuse ]; then
            cp "$guard/native.bin" "$work/iso/boot/native.bin"
        else
            cp "$objects/native.bin" "$work/iso/boot/native.bin"
        fi
        pointer_option=""; pointer_qemu=""; xm_option=""; xm_qemu=""; mxcsr_option=""; mxcsr_qemu=""
        if [ "${GTOS_FP_ALLOW_EMULATOR_POINTER_LIMITATION:-0}" = 1 ]; then
            pointer_option="allow-emulator-pointer-limit=1"; pointer_qemu="--allow-pointer-limit"
        fi
        if [ "${GTOS_FP_ALLOW_EMULATOR_XM_LIMITATION:-0}" = 1 ]; then
            xm_option="allow-emulator-xm-limit=1"; xm_qemu="--allow-xm-limit"
        fi
        if [ "${GTOS_FP_ALLOW_EMULATOR_MXCSR_LIMITATION:-0}" = 1 ]; then
            mxcsr_option="allow-emulator-mxcsr-limit=1"; mxcsr_qemu="--allow-mxcsr-limit"
        fi
        printf 'set timeout=0\nset default=0\nmenuentry "Native FP ownership" {\n multiboot /boot/native.bin case=%s %s %s %s\n boot\n}\n' "$case_name" "$pointer_option" "$xm_option" "$mxcsr_option" > "$work/iso/boot/grub/grub.cfg"
        "$grub" --output="$work/native.iso" "$work/iso" > "$work/grub.log" 2>&1 || { cat "$work/grub.log"; exit 1; }
        mkdir -p "$output/$name"
        cp "$work/native.iso" "$output/$name/native.iso"
        cp "$work/iso/boot/grub/grub.cfg" "$output/$name/grub.cfg"
        panic=""; if [ "$case_name" = kernel-misuse ]; then panic=--panic; fi
        if [ "$emulator" = bochs ]; then
            bochs_exclude=""
            case "$case_name:$suffix" in
                sse3:intel) bochs_cpu=corei5_lynnfield_750 ;;
                sse3:amd) bochs_cpu=zambezi ;;
                sse2:*|no-sse3:*) bochs_cpu=p4_willamette ;;
                no-fxsr:*) bochs_cpu=p4_willamette; bochs_exclude="sse sse2" ;;
                no-sse2:*) bochs_cpu=p4_willamette; bochs_exclude=sse2 ;;
                *) bochs_cpu=corei7_sandy_bridge_2600k ;;
            esac
            python3 "$work/native_fp_bochs.py" --root "$bochs_root" \
                --iso "$output/$name/native.iso" --output "$output/$name" --cpu "$bochs_cpu" \
                --memory "${memory%M}" --cpus "$cpus" --exclude-features "$bochs_exclude" --timeout "${GTOS_BOCHS_TIMEOUT:-360}" $panic
        else
            python3 "$work/native_fp_smoke_qemu.py" --qemu "$qemu" ${GTOS_QEMU_DATA_DIR:+--data-dir "$GTOS_QEMU_DATA_DIR"} \
                --iso "$work/native.iso" --output "$output/$name" --cpu "$cpu" --memory "$memory" --cpus "$cpus" $panic $pointer_qemu $xm_qemu $mxcsr_qemu
        fi
        if grep -q 'NATIVE FP DIAGNOSTIC PASS' "$output/$name/guest.log"; then
            printf 'DIAGNOSTIC PASS, EMULATOR GAPS REMAIN: %s\n' "$name"
        else
            printf 'PASS: %s\n' "$name"
        fi
    }
    run avx 32M 1 max max
    run avx 64M 4 max max
    run avx 128M 1 max max
    run sse3 64M 4 Nehalem intel
    run sse3 64M 4 Opteron_G3 amd
    run sse2 64M 4 max,-pni sse2-only
    run no-fxsr 64M 4 max,-fxsr masked-fxsr
    run no-sse2 64M 4 max,-sse2 masked-sse2
    run no-sse3 64M 4 max,-pni masked-sse3
    run osxsave 64M 4 max inherited
    run kernel-misuse 64M 4 max guard
done
