#!/bin/sh
# Build a read-only pre-paging guest and compare explicitly selected firmware.
set -eu
cd "$(dirname "$0")/.."
runtime="$(pwd)/../gtos-runtime"
bochs_root=${GTOS_BOCHS_ROOT:-"$(pwd)/../gtos-bochs/root"}
output=${GTOS_FP_FIRMWARE_OUTPUT:-"$(pwd)/../gtos-fp-firmware-probe"}
mkdir -p "$output"
output=$(cd "$output" && pwd)
grub=${GTOS_GRUB_MKRESCUE:-"$runtime/bin/grub-mkrescue"}
sources="tests/native_fp_firmware_probe.cpp tests/native_fp_firmware_probe_loader.s
    tests/native_fp_firmware_probe.sh tests/native_fp_firmware_probe_bochs.py
    tests/native_fp_bochs.py tests/native_process_smoke.ld
    src/hardwarecommunication/cpu_memory_types.cpp
    include/hardwarecommunication/cpu_memory_types.h include/common/types.h
    tools/kernel-cxxflags tools/audit-kernel-instructions.py"
printf '%s\n' $sources | LC_ALL=C sort | xargs sha256sum > "$output/sources-before.sha256"
mkdir -p "$output/source-snapshot"
for source in $sources; do
    mkdir -p "$output/source-snapshot/$(dirname "$source")"
    cp "$source" "$output/source-snapshot/$source"
done
git rev-parse HEAD > "$output/base-commit.txt"
git status --short > "$output/working-status.txt"
{
    ${CXX:-g++} --version
    as --version
    ld --version
    python3 --version
} > "$output/toolchain-version.txt"
flags=$(cat tools/kernel-cxxflags)
printf '%s\n' "$flags" > "$output/kernel-flags.txt"
mkdir -p "$output/objects" "$output/iso/boot/grub"
for source in tests/native_fp_firmware_probe.cpp src/hardwarecommunication/cpu_memory_types.cpp; do
    ${CXX:-g++} -m32 -std=c++11 -O2 $flags -ffreestanding -nostdlib -fno-builtin \
        -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -fno-threadsafe-statics \
        -fno-use-cxa-atexit -fno-asynchronous-unwind-tables -Iinclude \
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
        -c "$source" -o "$output/objects/$(basename "$source" .cpp).o"
done
as --32 tests/native_fp_firmware_probe_loader.s -o "$output/objects/loader.o"
ld -melf_i386 -T tests/native_process_smoke.ld --gc-sections \
    --undefined=native_fp_firmware_probe_header -Map "$output/probe.map" \
    -o "$output/probe.bin" "$output/objects/loader.o" \
    "$output/objects/native_fp_firmware_probe.o" "$output/objects/cpu_memory_types.o"
python3 tools/audit-kernel-instructions.py "$output/probe.bin" --map "$output/probe.map" > "$output/integer-audit.txt"
objdump -d "$output/probe.bin" > "$output/probe.disassembly.txt"
# Garbage collection removes uncalled production AP preparation, so the linked
# fixture contains no WRMSR, WBINVD/INVD or control-register destination at all.
if grep -E '\b(wrmsr|wbinvd|invd|clts|lmsw|xsetbv)\b|\bmov[^[:space:]]*[[:space:]]+[^,]+,%cr[0-9]+' "$output/probe.disassembly.txt"; then
    echo 'FAIL: diagnostic ELF contains a control/MSR/cache write' >&2; exit 1
fi
printf 'PASS: no WRMSR, WBINVD, INVD, CLTS, LMSW, XSETBV or CR-destination instructions\n' > "$output/read-only-audit.txt"
cp "$output/probe.bin" "$output/iso/boot/probe.bin"
printf 'set timeout=0\nset default=0\nmenuentry "Firmware memory-type observation" {\n multiboot /boot/probe.bin\n boot\n}\n' > "$output/iso/boot/grub/grub.cfg"
"$grub" --output="$output/probe.iso" "$output/iso" > "$output/grub.log" 2>&1
sha256sum "$output/probe.bin" "$output/probe.iso" > "$output/artifacts.sha256"
result=0
for cpus in ${GTOS_FP_FIRMWARE_CPUS:-1 4}; do
    for firmware in ${GTOS_FP_FIRMWARE_VARIANTS:-stock seabios}; do
        case "$firmware" in
            stock) bios="$bochs_root/usr/share/bochs/BIOS-bochs-latest" ;;
            seabios) bios="$runtime/root/usr/share/seabios/bios.bin" ;;
            *) echo "Unknown firmware: $firmware" >&2; exit 2 ;;
        esac
        python3 tests/native_fp_firmware_probe_bochs.py --root "$bochs_root" \
            --bios "$bios" --iso "$output/probe.iso" \
            --cpus "$cpus" --ips "${GTOS_FP_FIRMWARE_IPS:-300000000}" \
            --timeout "${GTOS_FP_FIRMWARE_TIMEOUT:-180}" \
            --output "$output/$firmware-${cpus}cpu" || result=1
    done
done
printf '%s\n' $sources | LC_ALL=C sort | xargs sha256sum > "$output/sources-after.sha256"
cmp "$output/sources-before.sha256" "$output/sources-after.sha256"
exit "$result"
