#!/bin/bash
# Actual i386 native heap/CPL3/PF/deferred Reap acceptance; no host allocator.
set -euo pipefail
if [ "$#" -lt 2 ]; then
    echo "usage: native_heap_smoke.sh FRESH_ARTIFACT_DIR OFFICIAL_SDK_PREPARE_DIR [ACTUAL_V8_HEAP_PROBE.elf ...]" >&2
    exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
out="$1"; sdk_prepare="$(realpath "$2")"; shift 2
if [ -e "$out" ]; then echo "Refusing existing artifact directory: $out" >&2; exit 2; fi
mkdir -p "$out"; out="$(realpath "$out")"
compiler="${CXX:-g++}"
qemu="${GTOS_QEMU_SYSTEM_I386:-qemu-system-i386}"
grub="${GTOS_GRUB_MKRESCUE:-grub-mkrescue}"
command -v "$qemu" >/dev/null; command -v "$grub" >/dev/null
"$compiler" --version > "$out/gcc-version.txt"
"$qemu" --version > "$out/qemu-version.txt"
python3 -c 'import sys,json; print(json.dumps(dict(executable=sys.executable,version=sys.version)))' > "$out/python-version.json"
gcc_include="$(realpath "$("$compiler" -print-file-name=include)")"
sha256sum "$compiler" "$gcc_include/stddef.h" > "$out/gcc-compiler-inputs.sha256"
"$compiler" -print-prog-name=cc1plus > "$out/gcc-cc1plus-path.txt"
git rev-parse HEAD > "$out/base-commit.txt"
git diff --stat > "$out/working-changes.txt"
cp "$sdk_prepare/manifest.json" "$out/sdk-prepare-manifest.json"
python3 - "$sdk_prepare" "$out" <<'PY'
import hashlib,json,sys
from pathlib import Path
sdk=Path(sys.argv[1]);out=Path(sys.argv[2]);v=json.loads((sdk/'manifest.json').read_text())
assert v['sdk_parse_pass'] and v['native_compiler_target']=='i686-unknown-none-elf'
assert not v['linux_target_sdk_used']
expected={}
for name in ['pinned_sdk_input_sha256','generated_sdk_and_derived_sha256']:
 for p,h in v[name].items():expected[str(Path(p).resolve())]=h
for p,origin in v['remaining_input_origins'].items():expected[str(Path(p).resolve())]=origin['sha256']
for p,h in expected.items():assert hashlib.sha256(Path(p).read_bytes()).hexdigest()==h,p
assert hashlib.sha256(Path(v['compiler']['path']).read_bytes()).hexdigest()==v['compiler']['sha256']
# Recover exact official roots from the proven actual-upstream compiler command.
cmd=next(c['command'] for c in v['checks'] if c['name']=='actual-upstream-memory-calls')
includes=[Path(cmd[i+1]).resolve() for i,x in enumerate(cmd[:-1]) if x=='-isystem']
assert len(includes)==3
libcxx,sdk_include,resource=includes
assert sdk_include==sdk/'sdk'
(out/'sdk-config.txt').write_text('\n'.join([v['compiler']['path'],str(libcxx),str(sdk_include),str(resource)])+'\n')
(out/'sdk-input-expected.json').write_text(json.dumps(expected,indent=2)+'\n')
PY
mapfile -t sdk_configuration < "$out/sdk-config.txt"
clang="${sdk_configuration[0]}"; libcxx="${sdk_configuration[1]}"
sdk="${sdk_configuration[2]}"; resource="${sdk_configuration[3]}"
"$clang" --version > "$out/clang-version.txt"
external=()
for source in "$@"; do
    source="$(realpath "$source")"; test -f "$source"; test "$(wc -c < "$source")" -le 65536
    external+=("$source")
done
printf '%s\n' "${external[@]}" > "$out/external-inputs.txt"
if [ "${#external[@]}" -ne 0 ] && [ "${#external[@]}" -ne 6 ]; then echo 'Require zero raw-only or six formal consumer variants' >&2; exit 2; fi
python3 - "$out" "${external[@]}" <<'PY'
import hashlib,json,struct,sys
from pathlib import Path
assert sys.version_info>=(3,9)
out=Path(sys.argv[1]);bindings=[]
for mode,name in enumerate(sys.argv[2:]):
 p=Path(name);m_path=p.parent/'manifest.json';m=json.loads(m_path.read_text());data=p.read_bytes()
 for key in ['native_build_pass','source_provenance_pass','stack_call_chain_qualified','qualification_complete',
             'boot_file_admission_pass','actual_elf32_validation_pass']:assert m[key],(mode,key)
 proof_path=Path(m['whole_stack_status']).resolve();proof=json.loads(proof_path.read_text())
 assert proof['qualification_pass'] and proof['full_stack_callchain_qualified'] and not proof['unknowns']
 manifest_sha=hashlib.sha256(m_path.read_bytes()).hexdigest()
 assert proof['input_hashes'][str(m_path.resolve())]==manifest_sha
 assert proof['elf_sha256']==m['stripped_sha256']
 assert m['heap_mode']==mode and Path(m['stripped_elf']).resolve()==p
 assert hashlib.sha256(data).hexdigest()==m['stripped_sha256']
 assert m['native_compiler_target']=='i686-unknown-none-elf' and not m['linux_target_sdk_used']
 assert not m['capacity_changed'] and m['boot_demo_file_limit_bytes']==65536 and m['user_page_budget']==256
 assert m['user_stack_usable_bytes']==8176 and m['probe_record_address']==0x40020000 and m['probe_record_bytes']==40
 assert all(c['exit_code']==0 for c in m['checks'])
 for group in ['compiled_input_sha256','source_sha256']:
  for source,h in m[group].items():assert hashlib.sha256(Path(source).read_bytes()).hexdigest()==h,(mode,source)
 header=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
 assert header[0][:7]==b'\x7fELF\x01\x01\x01' and header[1:4]==(2,3,1)
 found=[]
 for i in range(header[10]):
  typ,offset,address,physical,file_size,memory_size,flags,alignment=struct.unpack_from('<IIIIIIII',data,header[5]+i*header[9])
  if typ==1 and address<=0x40020000 and 0x40020000-address+40<=file_size:
   record=struct.unpack_from('<10I',data,offset+0x40020000-address)
   assert record[:3]==(1,mode,0);found.append(record)
 assert len(found)==1
 bindings.append(dict(mode=mode,elf=str(p),sha256=m['stripped_sha256'],manifest=str(m_path),
  manifest_sha256=manifest_sha,whole_stack_status=str(proof_path),
  whole_stack_status_sha256=hashlib.sha256(proof_path.read_bytes()).hexdigest(),compiler='Clang24',optimization='Oz',
  formal_static_proof_verified=True))
(out/'actual-input-preflight.json').write_text(json.dumps(dict(bindings=bindings,formal_upstream_inputs=len(bindings)==6),indent=2)+'\n')
PY
source_manifest() {
    {
        find include -type f -print
        printf '%s\n' "$compiler" "$gcc_include/stddef.h" "$clang"
        printf '%s\n' src/gdt.cpp src/multitasking.cpp src/syscalls.cpp \
            src/hardwarecommunication/interrupts.cpp src/hardwarecommunication/port.cpp \
            src/hardwarecommunication/interruptstubs.s src/process/native_runtime.cpp src/process/native_realtime.cpp \
            src/process/resources.cpp src/process/resources_png.inc src/process/native_surface.cpp \
            src/process/native_fp.cpp src/process/native_fp.s src/process/elf32.cpp \
            src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp src/memory/bootstrap.cpp \
            tests/native_heap_smoke.cpp tests/native_heap_smoke.sh tests/native_process_smoke.cpp \
            tests/native_process_probe_expectations.h tests/native_process_loader.s tests/native_process_smoke.ld \
            tools/kernel-cxxflags tools/audit-kernel-instructions.py "$sdk_prepare/manifest.json"
        find apps/native_heap apps/native_heap_probe "$sdk" -type f -print
        if [ "${#external[@]}" -gt 0 ]; then
            printf '%s\n' "${external[@]}"
            for source in "${external[@]}"; do printf '%s\n' "$(dirname "$source")/manifest.json"; done
            python3 - "$out/actual-input-preflight.json" <<'PY'
import json,sys
for binding in json.load(open(sys.argv[1]))['bindings']:print(binding['whole_stack_status'])
PY
        fi
    } | LC_ALL=C sort -u | xargs sha256sum
}
source_manifest > "$out/source-inputs.sha256"
kernel_flags=$(cat tools/kernel-cxxflags)
raw_flags=(-m32 -std=c++20 -ffreestanding -nostdlib -fno-builtin -fno-exceptions -fno-rtti
    -fno-stack-protector -fno-pie -fno-threadsafe-statics -fno-use-cxa-atexit
    -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fstack-usage
    -nostdinc -nostdinc++ -isystem "$sdk" -isystem "$resource" -Iinclude -Iapps/native_heap
    -D__GTOS__=1 -U__linux__ -U__linux -U__gnu_linux__ -U__unix__ -U__unix -Ulinux -Uunix -Wall -Wextra -Werror)
heap_flags=(--target=i686-unknown-none-elf -std=c++20 -fsized-deallocation -ffreestanding -fno-builtin
    -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic -fno-pie -fno-threadsafe-statics
    -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fstack-usage
    -mno-mmx -mno-sse -mno-sse2 -nostdinc -nostdinc++ -isystem "$libcxx" -isystem "$sdk"
    -isystem "$resource" -Iinclude -Iapps/native_heap -D__GTOS__=1 -Wall -Wextra -Werror)
run_probe() {
    local log="$1"; shift
    printf '%q ' "$@" >> "$probe/commands.txt"; printf '\n' >> "$probe/commands.txt"
    "$@" >> "$log" 2>&1
}
for optimization in 0 2; do
    level="$out/O$optimization"
    mkdir -p "$level/kernel" "$level/probes" "$level/iso/boot/grub"
    sources=(src/gdt.cpp src/multitasking.cpp src/syscalls.cpp src/hardwarecommunication/interrupts.cpp
        src/hardwarecommunication/port.cpp src/process/native_runtime.cpp src/process/native_realtime.cpp src/process/resources.cpp
        src/process/native_surface.cpp src/process/native_fp.cpp src/process/elf32.cpp
        src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp
        src/memory/bootstrap.cpp tests/native_heap_smoke.cpp)
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
    if grep -q 'NativeHeapUnusedBaseline' "$level/kernel-symbols.txt"; then echo 'FAIL unused old entry retained'; exit 1; fi
    python3 tools/audit-kernel-instructions.py --map "$level/kernel.map" --source-root "$repo" \
        "$level/kernel.bin" > "$level/kernel-scalar-audit.txt"
    cp "$level/kernel.bin" "$level/iso/boot/native.bin"
    for mode in 0 1 2 3 4 5 6 7; do
        probe="$level/probes/mode$mode"; mkdir -p "$probe"
        run_probe "$probe/build.log" "$compiler" "${raw_flags[@]}" -O"$optimization" -DGTOS_HEAP_PROBE_MODE="$mode" $kernel_flags \
            -MD -MF "$probe/main.d" -c apps/native_heap_probe/main.cpp -o "$probe/main.o"
        run_probe "$probe/build.log" "$clang" "${heap_flags[@]}" -O"$optimization" -MD -MF "$probe/heap.d" \
            -c apps/native_heap/heap.cc -o "$probe/heap.o"
        run_probe "$probe/build.log" "$compiler" "${raw_flags[@]}" -O"$optimization" $kernel_flags -MD -MF "$probe/memory.d" \
            -c apps/native_heap_probe/memory.cc -o "$probe/memory.o"
        run_probe "$probe/build.log" as --32 apps/native_heap_probe/start.s -o "$probe/start.o"
        run_probe "$probe/build.log" ld -melf_i386 --gc-sections -T apps/native_heap_probe/linker.ld -Map "$probe/probe.map" \
            -o "$probe/probe.elf" "$probe/start.o" "$probe/main.o" "$probe/heap.o" "$probe/memory.o"
        run_probe "$probe/symbols.txt" nm -n "$probe/probe.elf"
        run_probe "$probe/undefined.txt" nm -u "$probe/probe.elf"; test ! -s "$probe/undefined.txt"
        run_probe "$probe/build.log" objcopy --strip-all "$probe/probe.elf" "$probe/probe.stripped.elf"
        test "$(wc -c < "$probe/probe.stripped.elf")" -le 65536
        run_probe "$probe/readelf.txt" readelf -h -l "$probe/probe.elf"
        run_probe "$probe/disassembly.txt" objdump -d --no-show-raw-insn "$probe/probe.elf"
        run_probe "$probe/scalar-audit.txt" python3 tools/audit-kernel-instructions.py --map "$probe/probe.map" "$probe/probe.elf"
        cp "$probe/probe.stripped.elf" "$level/iso/boot/mode$mode.elf"
    done
    python3 - "$repo" "$out" "$level" "$libcxx" "$resource" "$gcc_include" <<'PY'
import hashlib,json,re,shlex,subprocess,sys
from pathlib import Path
repo,out,level,libcxx,resource,gcc_include=map(Path,sys.argv[1:]);expected=json.loads((out/'sdk-input-expected.json').read_text());inputs={};stacks=[]
for dep in (level/'probes').rglob('*.d'):
 content=dep.read_text().replace('\\\n',' ');paths=shlex.split(content.split(':',1)[1])
 for value in paths:
  p=Path(value);p=(p if p.is_absolute() else repo/p).resolve();h=hashlib.sha256(p.read_bytes()).hexdigest()
  if p.is_relative_to(repo):origin='GTOS repository source/header'
  elif str(p) in expected:
   assert h==expected[str(p)],p
   origin='Proven official SDK/pin/resource input'
  elif p.is_relative_to(libcxx):
   rel=p.relative_to(libcxx.parent)
   original=subprocess.check_output(['git','-C',str(libcxx.parent),'show','97b436da4c33663581d394f4ee0a5977fc38c2f4:'+rel.as_posix()])
   assert original==p.read_bytes(),p
   origin='Exact pinned libc++ header additionally admitted'
  elif p==gcc_include/'stddef.h':origin='Exact GCC13 builtin target type header from compiler -print-file-name=include'
  elif p.is_relative_to(resource):origin='Exact Clang24 compiler resource header'
  else:raise AssertionError('Unexpected sysroot/input '+str(p))
  inputs[str(p)]=dict(sha256=h,origin=origin)
for probe in sorted((level/'probes').iterdir()):
 functions=[]
 for usage in probe.glob('*.su'):
  for line in usage.read_text().splitlines():
   fields=line.split('\t');assert len(fields)==3,line
   assert fields[2] in ('static','dynamic,bounded'),line
   functions.append(dict(source=str(usage),function=fields[0],bytes=int(fields[1])))
 assert functions
 # All static/bounded function frames are counted even if GC removed them. These raw
 # C/heap paths have no recursion or function pointers; add 32-byte call/alignment
 # margin per frame and 64 bytes for the entry boundary. This exceeds each chain.
 bound=sum(f['bytes']+32 for f in functions)+64
 assert bound<=8176,(probe,bound)
 assert not re.search(r'\b(?:call|jmp)\s+\*',(probe/'disassembly.txt').read_text()),'unexpected indirect raw call/jump'
 stacks.append(dict(mode=int(probe.name[4:]),sum_all_bounded_functions_bound=bound,limit=8176,functions=functions))
(level/'raw-sdk-closure.json').write_text(json.dumps(dict(all_actual_inputs_qualified=True,inputs=inputs),indent=2)+'\n')
(level/'raw-conservative-stack.json').write_text(json.dumps(dict(all_pass=True,no_recursion_or_function_pointer_in_raw_sources=True,probes=stacks),indent=2)+'\n')
PY
    index=0
    for source in "${external[@]}"; do
        cp "$source" "$level/iso/boot/v8-$index.elf"
        sha256sum "$source" "$level/iso/boot/v8-$index.elf" > "$level/v8-$index.sha256"
        index=$((index + 1))
    done
    {
        printf 'set timeout=0\nset default=0\nmenuentry "Native heap acceptance" {\n multiboot /boot/native.bin\n'
        for mode in 0 1 2 3 4 5 6 7; do printf ' module /boot/mode%s.elf\n' "$mode"; done
        for ((index=0; index<${#external[@]}; ++index)); do printf ' module /boot/v8-%s.elf\n' "$index"; done
        printf ' boot\n}\n'
    } > "$level/iso/boot/grub/grub.cfg"
    "$grub" --output="$level/native-heap.iso" "$level/iso" > "$level/grub.log" 2>&1
    for configuration in 32M:1 64M:4 128M:1; do
        memory="${configuration%:*}"; cpus="${configuration#*:}"
        case_dir="$level/$memory-smp$cpus"; mkdir -p "$case_dir"
        set +e
        timeout 60 "$qemu" ${GTOS_QEMU_DATA_DIR:+-L "$GTOS_QEMU_DATA_DIR"} -machine pc -accel tcg \
            -m "$memory" -smp "$cpus" -cdrom "$level/native-heap.iso" -boot d -nic none \
            -display none -monitor none -serial none -debugcon "file:$case_dir/guest.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot > "$case_dir/qemu.log" 2>&1
        result=$?
        set -e
        printf '%s\n' "$result" > "$case_dir/qemu-exit.txt"
        if [ "$result" -ne 33 ] || ! grep -q '^NATIVE HEAP SMOKE PASS$' "$case_dir/guest.log"; then
            cat "$case_dir/qemu.log" "$case_dir/guest.log"
            echo "FAIL native heap O$optimization $memory/smp$cpus QEMU=$result" >&2; exit 1
        fi
        printf 'PASS native heap kernel_GCC_O%s raw_GCC_O%s heap_Clang_O%s %s/smp%s raw_modes=8 actual_v8_modules=%s\n' \
            "$optimization" "$optimization" "$optimization" "$memory" "$cpus" "${#external[@]}"
    done
    source_manifest > "$level/source-after.sha256"
    cmp "$out/source-inputs.sha256" "$level/source-after.sha256" || { echo 'FAIL sources changed'; exit 1; }
    sha256sum "$level/kernel.bin" "$level/native-heap.iso" "$level/iso/boot/"*.elf > "$level/binary-hashes.txt"
done
python3 - "$out" "${#external[@]}" <<'PY'
import hashlib,json,re,sys
from pathlib import Path
out=Path(sys.argv[1]);count=int(sys.argv[2]);cases=[]
for optimization in [0,2]:
 closure=json.loads((out/('O'+str(optimization))/'raw-sdk-closure.json').read_text())
 for p,info in closure['inputs'].items():assert hashlib.sha256(Path(p).read_bytes()).hexdigest()==info['sha256'],p
for optimization in [0,2]:
 for memory,cpus in [('32M',1),('64M',4),('128M',1)]:
  case=out/('O'+str(optimization))/(memory+'-smp'+str(cpus));log=(case/'guest.log').read_text()
  assert (case/'qemu-exit.txt').read_text().strip()=='33' and 'NATIVE HEAP SMOKE PASS\n' in log
  assert log.count('RAW HEAP CASE mode=')==8 and log.count('V8 HEAP CASE mode=')==count
  survivor=re.search(r'HEAP SURVIVORS kernel_cr3=([0-9A-F]{8}) initial_free=([0-9A-F]{8}) survivor_free=([0-9A-F]{8})',log)
  assert survivor
  kernel_cr3,initial_free,survivor_free=[int(value,16) for value in survivor.groups()]
  reaps=re.findall(r'HEAP REAP free=([0-9A-F]{8}) expected=([0-9A-F]{8})',log)
  assert len(reaps)==8+count and all(int(actual,16)==int(expected,16)==survivor_free for actual,expected in reaps)
  final=re.search(r'HEAP FINAL free=([0-9A-F]{8}) expected=([0-9A-F]{8})',log)
  assert final and int(final[1],16)==int(final[2],16)==initial_free
  traces=[]
  for line in log.splitlines():
   if ' HEAP CASE mode=' not in line:continue
   kind=line.split()[0];fields={k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]{8})',line)}
   assert fields['cs']==0x23 and fields['cr3']!=kernel_cr3 and fields['base']==0x80000000 and fields['handle'] and fields['checks']
   assert fields['retained']==fields['static']+fields['dynamic']
   assert fields['dynamic']==(0 if fields['mode']==3 else 17)
   if fields['mode']==1:assert fields['cr2']==0xBFFFCFFC and fields['error']&7==6 and fields['exit']==0x8000000e
   elif kind=='RAW' and fields['mode']==7:assert fields['cr2']==0x40000000 and fields['error']&7==7 and fields['exit']==0x8000000e
   elif fields['mode']==2:assert fields['exit']==73 and fields['cr2']==0
   elif fields['mode']==5 or kind=='RAW' and fields['mode']==6:
    assert fields['exit']==(0x48000010 if kind=='V8' else 0x48000002) and fields['cr2']==0
   else:assert fields['exit']==0 and fields['stage']==2 and fields['cr2']==0
   traces.append(dict(kind=kind,**fields))
  assert [t['mode'] for t in traces if t['kind']=='RAW']==list(range(8))
  assert [t['mode'] for t in traces if t['kind']=='V8']==list(range(count))
  cases.append(dict(kernel_compiler='GCC13',kernel_optimization=optimization,raw_main_compiler='GCC13',
   raw_main_optimization=optimization,raw_heap_compiler='Clang24',raw_heap_optimization=optimization,
   actual_v8_compiler='Clang24' if count else None,actual_v8_optimization='Oz' if count else None,
   memory=memory,cpus=cpus,kernel_cr3=kernel_cr3,initial_free=initial_free,survivor_free=survivor_free,
   exact_reap_checks=len(reaps)+1,guest=str(case/'guest.log'),traces=traces))
status=dict(all_required_checks_pass=True,scope='Actual production i386 heap/CPL3/fault/reap; raw C ABI and optional real upstream V8 memory consumer',
 capacity_changed=False,raw_modes=8,actual_v8_module_count=count,formal_upstream_guest_acceptance=count==6,cases=cases,
 source_inputs_sha256=str(out/'source-inputs.sha256'),unused_baseline_discarded=True,
 full_v8_backend=False,browser_guest_pass=False,media_guest_pass=False,html5_guest_pass=False)
(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
print('Evidence:',out/'status.json')
PY
