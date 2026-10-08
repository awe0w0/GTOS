#!/bin/bash
# Real production runtime int80/CPL3/PF/reap acceptance. No host policy shim.
set -euo pipefail
if [ "$#" -lt 1 ]; then
    echo "usage: native_vm_smoke.sh FRESH_ARTIFACT_DIR [ACTUAL_V8_PROBE.elf ...]" >&2
    exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
out="$1"; shift
if [ -e "$out" ]; then echo "Refusing to reuse artifact directory: $out" >&2; exit 2; fi
mkdir -p "$out"
out="$(realpath "$out")"
compiler="${CXX:-g++}"
qemu="${GTOS_QEMU_SYSTEM_I386:-qemu-system-i386}"
grub="${GTOS_GRUB_MKRESCUE:-grub-mkrescue}"
command -v "$qemu" >/dev/null
command -v "$grub" >/dev/null
"$compiler" --version > "$out/compiler.txt"
"$qemu" --version > "$out/qemu-version.txt"
git rev-parse HEAD > "$out/base-commit.txt"
git diff --stat > "$out/working-changes.txt"
external=()
for source in "$@"; do
    source="$(realpath "$source")"
    test -f "$source"
    test "$(wc -c < "$source")" -le 65536
    external+=("$source")
done
printf '%s\n' "${external[@]}" > "$out/external-inputs.txt"
source_manifest() {
    {
        find include -type f -print
        printf '%s\n' src/gdt.cpp src/multitasking.cpp src/syscalls.cpp \
            src/hardwarecommunication/interrupts.cpp src/hardwarecommunication/port.cpp \
            src/hardwarecommunication/interruptstubs.s src/process/native_runtime.cpp src/process/native_realtime.cpp \
            src/process/resources.cpp src/process/resources_png.inc src/process/native_surface.cpp \
            src/process/native_fp.cpp src/process/native_fp.s src/process/elf32.cpp \
            src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp src/memory/bootstrap.cpp \
            tests/native_vm_smoke.cpp tests/native_vm_smoke.sh tests/native_process_smoke.cpp \
            tests/native_process_probe_expectations.h tests/native_process_loader.s tests/native_process_smoke.ld \
            tools/kernel-cxxflags tools/audit-kernel-instructions.py
        find apps/native_vm_probe -type f -print
        if [ "${#external[@]}" -gt 0 ]; then printf '%s\n' "${external[@]}"; fi
    } | LC_ALL=C sort -u | xargs sha256sum
}
source_manifest > "$out/source-inputs.sha256"
kernel_flags=$(cat tools/kernel-cxxflags)
for optimization in 0 2; do
    level="$out/O$optimization"
    mkdir -p "$level/kernel" "$level/probes" "$level/iso/boot/grub"
    sources=(src/gdt.cpp src/multitasking.cpp src/syscalls.cpp src/hardwarecommunication/interrupts.cpp
        src/hardwarecommunication/port.cpp src/process/native_runtime.cpp src/process/native_realtime.cpp src/process/resources.cpp
        src/process/native_surface.cpp src/process/native_fp.cpp src/process/elf32.cpp
        src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp
        src/memory/bootstrap.cpp tests/native_vm_smoke.cpp)
    for source in "${sources[@]}"; do
        "$compiler" -m32 -std=c++11 -O"$optimization" -ffreestanding -nostdlib -fno-builtin \
            -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -fno-threadsafe-statics \
            -fno-use-cxa-atexit -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
            -Iinclude -Wno-write-strings -Wall -Wextra -Werror $kernel_flags -c "$source" \
            -o "$level/kernel/$(basename "$source" .cpp).o" >> "$level/kernel-build.log" 2>&1
    done
    as --32 tests/native_process_loader.s -o "$level/kernel/loader.o"
    as --32 src/process/native_fp.s -o "$level/kernel/native_fp.asm.o"
    as --32 src/hardwarecommunication/interruptstubs.s -o "$level/kernel/stubs.o"
    ld -melf_i386 --gc-sections -T tests/native_process_smoke.ld -Map "$level/kernel.map" \
        -o "$level/kernel.bin" "$level/kernel"/*.o
    nm "$level/kernel.bin" > "$level/kernel-symbols.txt"
    if grep -q 'NativeVmUnusedBaseline' "$level/kernel-symbols.txt"; then
        echo 'FAIL: unused original smoke entry retained' >&2; exit 1
    fi
    python3 tools/audit-kernel-instructions.py --map "$level/kernel.map" --source-root "$repo" \
        "$level/kernel.bin" > "$level/kernel-scalar-audit.txt"
    cp "$level/kernel.bin" "$level/iso/boot/native.bin"
    for mode in 0 1 2 3 4 5 6; do
        probe="$level/probes/mode$mode"
        mkdir -p "$probe"
        "$compiler" -m32 -std=c++11 -O"$optimization" -ffreestanding -nostdlib -fno-builtin \
            -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -fno-threadsafe-statics \
            -fno-use-cxa-atexit -fno-asynchronous-unwind-tables -fstack-usage \
            -ffunction-sections -fdata-sections -Iinclude -Wall -Wextra -Werror \
            -DGTOS_VM_PROBE_MODE="$mode" $kernel_flags -c apps/native_vm_probe/main.cpp \
            -o "$probe/main.o" > "$probe/build.log" 2>&1
        as --32 apps/native_vm_probe/start.s -o "$probe/start.o"
        ld -melf_i386 --gc-sections -T apps/native_vm_probe/linker.ld -Map "$probe/probe.map" \
            -o "$probe/probe.elf" "$probe/start.o" "$probe/main.o"
        nm -n "$probe/probe.elf" > "$probe/symbols.txt"
        objcopy --strip-all "$probe/probe.elf" "$probe/probe.stripped.elf"
        test "$(wc -c < "$probe/probe.stripped.elf")" -le 65536
        readelf -h -l "$probe/probe.elf" > "$probe/readelf.txt"
        python3 tools/audit-kernel-instructions.py --map "$probe/probe.map" "$probe/probe.elf" > "$probe/scalar-audit.txt"
        cp "$probe/probe.stripped.elf" "$level/iso/boot/mode$mode.elf"
    done
    index=0
    for source in "${external[@]}"; do
        cp "$source" "$level/iso/boot/v8-$index.elf"
        sha256sum "$source" "$level/iso/boot/v8-$index.elf" > "$level/v8-$index.sha256"
        index=$((index + 1))
    done
    {
        printf 'set timeout=0\nset default=0\nmenuentry "Native VM acceptance" {\n multiboot /boot/native.bin\n'
        for mode in 0 1 2 3 4 5 6; do printf ' module /boot/mode%s.elf\n' "$mode"; done
        for ((index=0; index<${#external[@]}; ++index)); do printf ' module /boot/v8-%s.elf\n' "$index"; done
        printf ' boot\n}\n'
    } > "$level/iso/boot/grub/grub.cfg"
    "$grub" --output="$level/native-vm.iso" "$level/iso" > "$level/grub.log" 2>&1
    for configuration in 32M:1 64M:4 128M:1; do
        memory="${configuration%:*}"; cpus="${configuration#*:}"
        case_dir="$level/$memory-smp$cpus"; mkdir -p "$case_dir"
        set +e
        timeout 60 "$qemu" ${GTOS_QEMU_DATA_DIR:+-L "$GTOS_QEMU_DATA_DIR"} -machine pc -accel tcg \
            -m "$memory" -smp "$cpus" -cdrom "$level/native-vm.iso" -boot d -nic none \
            -display none -monitor none -serial none -debugcon "file:$case_dir/guest.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot > "$case_dir/qemu.log" 2>&1
        result=$?
        set -e
        printf '%s\n' "$result" > "$case_dir/qemu-exit.txt"
        if [ "$result" -ne 33 ] || ! grep -q '^NATIVE VM SMOKE PASS$' "$case_dir/guest.log"; then
            cat "$case_dir/qemu.log" "$case_dir/guest.log"
            echo "FAIL native VM O$optimization $memory/smp$cpus QEMU=$result" >&2; exit 1
        fi
        printf 'PASS native VM O%s %s/smp%s raw_modes=7 actual_v8_modules=%s\n' \
            "$optimization" "$memory" "$cpus" "${#external[@]}"
    done
    source_manifest > "$level/source-after.sha256"
    cmp "$out/source-inputs.sha256" "$level/source-after.sha256" || {
        echo 'FAIL: native VM sources changed during execution' >&2; exit 1
    }
    sha256sum "$level/kernel.bin" "$level/native-vm.iso" "$level/iso/boot/"*.elf > "$level/binary-hashes.txt"
done
python3 - "$out" "${#external[@]}" <<'PY'
import json
from pathlib import Path
import sys
out = Path(sys.argv[1]); count = int(sys.argv[2]); cases = []
for optimization in [0, 2]:
    for memory, cpus in [('32M', 1), ('64M', 4), ('128M', 1)]:
        case = out / ('O' + str(optimization)) / (memory + '-smp' + str(cpus))
        log = (case / 'guest.log').read_text()
        assert (case / 'qemu-exit.txt').read_text().strip() == '33'
        assert 'NATIVE VM SMOKE PASS\n' in log and log.count('RAW VM CASE mode=') == 7
        assert log.count('V8 VM CASE mode=') == count
        cases.append(dict(optimization=optimization, memory=memory, cpus=cpus,
            raw_modes=7, actual_v8_modules=count, guest=str(case / 'guest.log')))
status = dict(all_required_checks_pass=True, scope='Actual production i386 CPL3 int80 VM/PF/reap guests; no host hardware hook',
    cases=cases, unused_baseline_discarded=True, kernel_and_raw_native_scalar_audits_pass=True,
    actual_v8_module_count=count, source_inputs_sha256=str(out / 'source-inputs.sha256'))
(out / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
print('Evidence:', out / 'status.json')
PY
