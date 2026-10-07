#!/usr/bin/env bash
# Real clock int80/CPL3/PIT/fault/reap evidence, with an independent IRQ observer.
set -euo pipefail
if [ "$#" -lt 1 ]; then
    echo "usage: native_clock_smoke.sh FRESH_ARTIFACT_DIR [--locate] [FORMAL_MODE0.elf ... FORMAL_MODE3.elf]" >&2
    exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 - "$repo" "$@" <<'PY'
import hashlib,json,os,re,shutil,struct,subprocess,sys,time
from pathlib import Path
repo=Path(sys.argv[1]);out=Path(sys.argv[2]);args=sys.argv[3:]
locate=bool(args and args[0]=='--locate')
if locate:args=args[1:]
if out.exists():raise SystemExit('Refusing to reuse artifact directory: '+str(out))
out.mkdir(parents=True);out=out.resolve()
if len(args) not in (0,4):raise SystemExit('Expected zero raw-only or four ordered formal clock consumers')
cxx=Path(os.environ.get('CXX',''))
if not cxx.is_absolute() or not cxx.is_file():raise SystemExit('CXX must name the absolute qualified compiler wrapper')
qemu=Path(shutil.which(os.environ.get('GTOS_QEMU_SYSTEM_I386','qemu-system-i386')) or '')
grub=Path(shutil.which(os.environ.get('GTOS_GRUB_MKRESCUE','grub-mkrescue')) or '')
if not qemu.is_file() or not grub.is_file():raise SystemExit('Qualified QEMU/GRUB unavailable')
if sys.version_info < (3,9):raise SystemExit('Qualified Python >=3.9 required')
commands=[]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def emit(path,value):path.write_text(json.dumps(value,indent=2)+'\n')

def annotation(path,kind,mode,scenario):
    original=Path(path).read_bytes();data=bytearray(original)
    assert original[:7]==b'\x7fELF\x01\x01\x01'
    phoff=struct.unpack_from('<I',original,28)[0]
    phsize,phcount=struct.unpack_from('<HH',original,42)
    assert phsize==32
    records=[];executable=[]
    for i in range(phcount):
        typ,offset,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',original,phoff+i*phsize)
        if typ!=1:continue
        assert offset+filesz<=len(original)
        if flags&1:executable.append((offset,filesz))
        if va<=0x40020000 and 0x40020000+192<=va+filesz:
            assert flags&2 and not flags&1
            records.append(offset+0x40020000-va)
    assert len(records)==1
    record=records[0];assert struct.unpack_from('<6I',original,record)==(1,0 if kind=='RAW' else 1,mode,0,0,0)
    start=record+20;struct.pack_into('<I',data,start,scenario)
    differences=[i for i,(a,b) in enumerate(zip(original,data)) if a!=b]
    assert all(start<=i<start+4 for i in differences)
    executable_before=[hashlib.sha256(original[o:o+s]).hexdigest() for o,s in executable]
    executable_after=[hashlib.sha256(data[o:o+s]).hexdigest() for o,s in executable]
    assert executable_before==executable_after
    return dict(kind=kind,mode=mode,scenario=scenario,source_sha256=sha(path),record_va=0x40020000,
        record_file_offset=record,record_bytes=192,annotation_field_offset=20,allowed_file_write_offset=start,
        allowed_file_write_bytes=4,old_word=0,new_word=scenario,actual_changed_file_offsets=differences,
        predicted_runtime_copy_sha256=hashlib.sha256(data).hexdigest(),executable_segment_sha256=executable_before,
        machine_code_unchanged=True,scope='ISO input byteexact; kernel CreateElf input is a copy annotated only in the4B scenario data field')
def run(argv,log,timeout=None):
    argv=[str(x) for x in argv];started=time.time()
    invocation=dict(argv=argv,cwd=str(repo),environment={k:os.environ[k] for k in sorted(os.environ) if k in ('PATH','CXX','LD_LIBRARY_PATH','GTOS_QEMU_SYSTEM_I386','GTOS_QEMU_DATA_DIR','GTOS_GRUB_MKRESCUE','GTOS_GRUB_MODULES_DIR','GTOS_RUNTIME_ROOT','TMPDIR')},executable_sha256=sha(argv[0]),log=str(log))
    try:
        with log.open('wb') as f:completed=subprocess.run(argv,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
        code=completed.returncode
    except subprocess.TimeoutExpired:code=124
    invocation.update(exit_code=code,elapsed_seconds=time.time()-started);commands.append(invocation)
    emit(out/'commands.json',commands)
    if code not in (0,33):raise RuntimeError('Command failed ('+str(code)+'): '+str(log)+'\n'+log.read_text(errors='replace')[-5000:])
    return code
for tool,name in ((cxx,'gcc'),(qemu,'qemu'),(grub,'grub')):run([tool,'--version'],out/(name+'-version.txt'))
emit(out/'python-version.json',dict(executable=sys.executable,version=sys.version))
# Read the worktree metadata only; no Git command, mutation or cleanup.
gitref=(repo/'.git').read_text().strip()
gitdir=Path(gitref.removeprefix('gitdir: ').strip()) if gitref.startswith('gitdir: ') else repo/'.git'
head=(gitdir/'HEAD').read_text().strip()
if head.startswith('ref: '):
    common=(gitdir/(gitdir/'commondir').read_text().strip()).resolve() if (gitdir/'commondir').exists() else gitdir
    refpath=common/head[5:]
    if not refpath.exists():raise SystemExit('Cannot read exact source base reference without Git')
    head=refpath.read_text().strip()
(out/'base-commit.txt').write_text(head+'\n')
external=[];bindings=[]
for mode,arg in enumerate(args):
    elf=Path(arg).resolve();mfile=elf.parent/'manifest.json';m=json.loads(mfile.read_text());proofpath=Path(m['whole_stack_status']);proof=json.loads(proofpath.read_text())
    assert elf.stat().st_size<=65536 and m.get('clock_mode')==mode
    assert m['native_build_pass'] and m['source_provenance_pass'] and m['stack_call_chain_qualified'] and m['qualification_complete']
    assert m['probe_record_address']==0x40020000 and m['probe_record_bytes']==192 and m['probe_record_symbol']=='native_clock_record'
    assert m.get('guest_pass') is False and proof['qualification_pass'] and proof['full_stack_callchain_qualified'] and not proof['unknowns']
    assert proof['input_hashes'][str(mfile.resolve())]==sha(mfile)
    assert sha(elf)==m['stripped_sha256']==proof['elf_sha256']
    external.append(elf);bindings.append(dict(mode=mode,elf=str(elf),sha256=sha(elf),manifest=str(mfile),manifest_sha256=sha(mfile),whole_stack_status=str(proofpath),whole_stack_status_sha256=sha(proofpath),formal_static_proof_verified=True))
emit(out/'actual-input-preflight.json',dict(formal_upstream_inputs=bool(external),bindings=bindings))
(out/'external-inputs.txt').write_text(''.join(str(e)+'\n' for e in external))
sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp','src/process/native_runtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp','tests/native_clock_smoke.cpp']
asm=['tests/native_process_loader.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
inputs=set((repo/'include').rglob('*'));inputs={p for p in inputs if p.is_file()}
inputs.update(repo/p for p in sources+asm+['src/process/resources_png.inc','tests/native_process_smoke.cpp','tests/native_process_probe_expectations.h','tests/native_process_smoke.ld','tests/native_clock_smoke.sh','tools/kernel-cxxflags','tools/audit-kernel-instructions.py'])
inputs.update(p for p in (repo/'apps/native_clock_probe').rglob('*') if p.is_file())
for e in external:inputs.update([e,e.parent/'manifest.json',Path(json.loads((e.parent/'manifest.json').read_text())['whole_stack_status'])])
sourcehash={str(p):sha(p) for p in sorted(inputs)}
(out/'source-inputs.sha256').write_text(''.join(h+'  '+p+'\n' for p,h in sourcehash.items()))
tools={name:dict(path=shutil.which(name),sha256=sha(shutil.which(name))) for name in ('as','ld','nm','objcopy','readelf','objdump')}
emit(out/'tool-inputs.json',dict(compiler=dict(path=str(cxx),sha256=sha(cxx)),tools=tools,qemu=dict(path=str(qemu),sha256=sha(qemu)),grub=dict(path=str(grub),sha256=sha(grub))))
kflags=(repo/'tools/kernel-cxxflags').read_text().split()
common=['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-pie','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix','-Wall','-Wextra','-Werror']+kflags
F=1193182;P=11931*1000000;U=(1<<64)-1;I=(1<<63)-1
maxN=lambda allowed:((allowed+1)*F-1)//P
# Failed localization measured80 real startup IRQs with headroom16. Use256
# coherent bootstrap ticks, then wait for real delivery to cross each boundary.
headroom=256
seeds={0:0,1:(1<<31)-headroom,2:(1<<32)-headroom,3:((1<<32)*F-1)//P-headroom,4:maxN(I-2)-headroom,5:maxN(U)-headroom}
emit(out/'independent-seeds.json',dict(formula='floor(N*11931000000/1193182), remainder from exact host bigint divmod',seeds={str(k):dict(N=str(n),microseconds=str(n*P//F),fraction=n*P%F,legacy_low32=n&0xffffffff) for k,n in seeds.items()}))
levels=[2] if locate else [0,2]
raw={};kernel_cache={};guests=[]
for opt in levels:
    level=out/('O'+str(opt));(level/'probes').mkdir(parents=True)
    for mode in range(4):
        d=level/'probes'/('mode'+str(mode));d.mkdir()
        for source in ('main.cpp','memory.cc'):
            obj=d/(source.split('.')[0]+'.o')
            run([cxx]+common+['-O'+str(opt),'-DGTOS_CLOCK_PROBE_MODE='+str(mode),'-MD','-MF',str(obj.with_suffix('.d')),'-c','apps/native_clock_probe/'+source,'-o',obj],d/(source+'.log'))
        run([tools['as']['path'],'--32','apps/native_clock_probe/start.s','-o',d/'start.o'],d/'assembly.log')
        run([tools['ld']['path'],'-melf_i386','--gc-sections','-T','apps/native_clock_probe/linker.ld','-Map',d/'probe.map','-o',d/'probe.elf',d/'start.o',d/'main.o',d/'memory.o'],d/'link.log')
        run([tools['nm']['path'],'-u',d/'probe.elf'],d/'undefined.txt');assert not (d/'undefined.txt').read_text().strip()
        run([tools['nm']['path'],'-n',d/'probe.elf'],d/'symbols.txt');assert re.search(r'^40020000 [Dd] native_clock_record$',(d/'symbols.txt').read_text(),re.M)
        run([tools['readelf']['path'],'-h','-l',d/'probe.elf'],d/'readelf.txt')
        run([tools['objdump']['path'],'-d',d/'probe.elf'],d/'disassembly.txt')
        assert not re.search(r'\b(?:call|jmp)\s+\*',(d/'disassembly.txt').read_text())
        run([Path(sys.executable),'tools/audit-kernel-instructions.py','--map',d/'probe.map',d/'probe.elf'],d/'scalar-audit.txt')
        usage=[]
        for su in d.glob('*.su'):
            for line in su.read_text().splitlines():
                name,size,kind=line.rsplit('\t',2);assert kind in ('static','dynamic,bounded');usage.append(dict(function=name,bytes=int(size),kind=kind))
        bound=sum(u['bytes']+32 for u in usage)+64;assert usage and bound<=8176
        emit(d/'stack-bound.json',dict(method='all static/bounded function frames plus32each+entry64; scalar noindirect raw source has no recursion',bound=bound,limit=8176,functions=usage))
        run([tools['objcopy']['path'],'--strip-all',d/'probe.elf',d/'probe.stripped.elf'],d/'strip.log')
        assert (d/'probe.stripped.elf').stat().st_size<=65536
        raw[opt,mode]=d/'probe.stripped.elf'
    scenarios=[0,1,2,3,5]+([4] if external else [])
    for scenario in scenarios:
        n=seeds[scenario];us,fraction=divmod(n*P,F)
        d=level/('scenario'+str(scenario));(d/'kernel').mkdir(parents=True);(d/'iso/boot/grub').mkdir(parents=True)
        seed=d/'clock_seed.h';seed.write_text('#define CLOCK_SCENARIO '+str(scenario)+'U\n#define CLOCK_SEED_TICKS '+str(n)+'ULL\n#define CLOCK_SEED_US '+str(us)+'ULL\n#define CLOCK_SEED_FRACTION '+str(fraction)+'U\n')
        for source in sources:
            obj=d/'kernel'/(Path(source).stem+'.o')
            run([cxx]+common+['-O'+str(opt),'-Wno-write-strings','-I'+str(d),'-MD','-MF',str(obj.with_suffix('.d')),'-c',source,'-o',obj],d/(Path(source).stem+'.log'))
        run([cxx]+common+['-O'+str(opt),'-c','apps/native_clock_probe/memory.cc','-o',d/'kernel/byte_helpers.o'],d/'kernel-byte-helpers.log')
        for source,name in zip(asm,['loader.o','native_fp.asm.o','stubs.o']):run([tools['as']['path'],'--32',source,'-o',d/'kernel'/name],d/(name+'.log'))
        run([tools['ld']['path'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin']+sorted((d/'kernel').glob('*.o')),d/'link.log')
        run([tools['nm']['path'],'-n',d/'kernel.bin'],d/'kernel-symbols.txt');assert 'NativeClockUnusedBaseline' not in (d/'kernel-symbols.txt').read_text()
        run([tools['nm']['path'],'-u',d/'kernel.bin'],d/'kernel-undefined.txt');assert not (d/'kernel-undefined.txt').read_text().strip()
        run([Path(sys.executable),'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'kernel-scalar-audit.txt')
        shutil.copyfile(d/'kernel.bin',d/'iso/boot/native.bin')
        copies=[]
        for mode in range(4):
            target=d/'iso/boot'/('raw-'+str(mode)+'.elf');shutil.copyfile(raw[opt,mode],target);assert sha(target)==sha(raw[opt,mode]);copies.append(dict(kind='RAW',mode=mode,source=str(raw[opt,mode]),copy=str(target),sha256=sha(target)))
        for mode,e in enumerate(external):
            target=d/'iso/boot'/('v8-'+str(mode)+'.elf');shutil.copyfile(e,target);assert sha(target)==sha(e);copies.append(dict(kind='V8',mode=mode,source=str(e),copy=str(target),sha256=sha(target)))
        emit(d/'input-copies.json',copies)
        annotations=[annotation(c['copy'],c['kind'],c['mode'],scenario) for c in copies]
        emit(d/'runtime-input-annotations.json',annotations)
        cfg='set timeout=0\nset default=0\nmenuentry "Native coarse clock" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/raw-'+str(m)+'.elf\n' for m in range(4))+''.join(' module /boot/v8-'+str(m)+'.elf\n' for m in range(len(external)))+' boot\n}\n'
        (d/'iso/boot/grub/grub.cfg').write_text(cfg)
        run([grub,'--output='+str(d/'clock.iso'),d/'iso'],d/'grub.log')
        configs=[('64M',4)] if locate and scenario==0 else ([('32M',1)] if scenario or locate else [('32M',1),('64M',4)])
        for memory,cpus in configs:
            case=d/(memory+'-smp'+str(cpus));case.mkdir()
            argv=[qemu]
            if os.environ.get('GTOS_QEMU_DATA_DIR'):argv+=['-L',os.environ['GTOS_QEMU_DATA_DIR']]
            argv+=['-machine','pc','-accel','tcg','-m',memory,'-smp',str(cpus),'-cdrom',d/'clock.iso','-boot','d','-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(case/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot']
            emit(case/'qemu-invocation.json',dict(argv=[str(x) for x in argv],cwd=str(repo),environment={k:os.environ[k] for k in sorted(os.environ) if k in ('PATH','LD_LIBRARY_PATH','GTOS_QEMU_SYSTEM_I386','GTOS_QEMU_DATA_DIR','TMPDIR')},qemu_sha256=sha(qemu),iso_sha256=sha(d/'clock.iso'),kernel_sha256=sha(d/'kernel.bin'),seed_sha256=sha(seed),input_copies=copies,runtime_annotations=annotations))
            code=run(argv,case/'qemu.log',60);(case/'qemu-exit.txt').write_text(str(code)+'\n')
            log=(case/'guest.log').read_text(errors='replace')
            if code!=33 or '\nNATIVE CLOCK SMOKE PASS\n' not in log:raise RuntimeError('Guest failed: '+str(case)+'\n'+log[-12000:])
            successful=[];failed=[];legacy=[];trace=[];records=[];last_record=None
            for line in log.splitlines():
                if line.startswith('CLOCK CALL '):
                    parsed=dict(re.findall(r'(\w+)=([0-9A-F]+)',line));N=int(parsed['n'],16);status=int(parsed['status'],16)
                    assert parsed['free_before']==parsed['free_after'] and parsed['preserved']=='00000001'
                    if status==0:
                        words=[int(v,16) for v in line.split('payload=',1)[1].split('free_before=',1)[0].split()]
                        assert len(words)==12 and words[:6]==[1,1,1,1,7,10000] and words[10:]==[F,11931]
                        actual_us=words[6]|words[7]<<32;actual_N=words[8]|words[9]<<32
                        assert actual_N==N and actual_us==N*P//F
                        successful.append(dict(N=str(N),microseconds=str(actual_us),payload_words=words))
                    else:
                        assert status in [(-22)&0xffffffff,(-14)&0xffffffff,(-38)&0xffffffff,(-75)&0xffffffff];failed.append(parsed)
                elif line.startswith('CLOCK LEGACY '):
                    v=dict(re.findall(r'(\w+)=([0-9A-F]+)',line));assert int(v['eax'],16)==int(v['n'],16)&0xffffffff;legacy.append(v)
                elif line.startswith('CLOCK RECORD RAW words='):
                    words=[int(x,16) for x in line.split('words=',1)[1].split()];assert len(words)==48
                    last_record=words;records.append(words)
                elif line.startswith(('RAW CLOCK CASE ','V8 CLOCK CASE ')):
                    v={k:int(x,16) for k,x in re.findall(r'(\w+)=([0-9A-F]+)',line)};v['kind']=line.split()[0]
                    assert v['scenario']==scenario and v['cs']==0x23 and v['dynamic']==3 and v['retained']==v['static']+3
                    if v['mode']==1:assert v['cr2']==0xBFFFCFFC and v['pf']==6 and v['exit']==0x8000000e
                    elif v['mode']==2:assert v['pf']==0 and v['exit']==73
                    elif v['mode']==3 and v['kind']=='V8':assert v['exit']==0x49000001 and v['pf']==0
                    else:assert v['exit']==0 and v['pf']==0
                    assert last_record and last_record[0]==1 and last_record[1]==(0 if v['kind']=='RAW' else 1)
                    assert last_record[2]==v['mode'] and last_record[5]==scenario
                    first=last_record[12:24];last=last_record[24:36]
                    assert first[:6]==[1,1,1,1,7,10000] and first[10:]==[F,11931]
                    assert last[:6]==[1,1,1,1,7,10000] and last[10:]==[F,11931]
                    first_us=first[6]|first[7]<<32;first_n=first[8]|first[9]<<32
                    last_us=last[6]|last[7]<<32;last_n=last[8]|last[9]<<32
                    elapsed=last_record[8]|last_record[9]<<32
                    assert first_us==first_n*P//F and last_us==last_n*P//F and first_n<=last_n
                    assert last_record[36]==0 and last_record[40]==0x80000000 and last_record[41] and last_record[42]==2
                    if v['kind']=='RAW' and v['mode']==0 and 1<=scenario<=3:
                        boundary=(1<<31) if scenario==1 else (1<<32)
                        assert (first_us if scenario==3 else first_n)<boundary<=(last_us if scenario==3 else last_n)
                    if v['kind']=='V8' and v['mode']<3:assert elapsed>=20000 and elapsed==last_us-first_us
                    if v['kind']=='V8' and v['mode']==3:
                        assert first_us<=I-2<last_us and last_record[6]==0x49000001 and last_record[37]==0 and last_record[46]==1
                    if v['kind']=='RAW' and v['mode']==3:
                        assert last_record[6]==((-75)&0xffffffff) and last_record[37]==((-75)&0xffffffff) and last_record[39]>=16 and last_record[46]==1
                    v.update(record_words=last_record,first_N=str(first_n),last_N=str(last_n),first_microseconds=str(first_us),last_microseconds=str(last_us),elapsed_microseconds=str(elapsed))
                    trace.append(v)
                elif line.startswith('CLOCK KERNEL '):
                    v=dict(re.findall(r'(\w+)=([0-9A-F]+)',line));words=[int(x,16) for x in line.split('payload=',1)[1].split()]
                    if int(v['status'],16)==0:
                        N=words[8]|words[9]<<32;usvalue=words[6]|words[7]<<32
                        assert int(v['n_before'],16)<=N<=int(v['n_after'],16) and usvalue==N*P//F
                    else:assert int(v['status'],16)==(-75)&0xffffffff and words==[0xA5A5A5A5]*12
            expected=([('RAW',m) for m in range(3)]+([('V8',m) for m in range(3)] if external else [])) if scenario==0 else (([('RAW',0)]+([('V8',0)] if external else [])) if scenario<4 else ([('V8',3)] if scenario==4 else [('RAW',3)]))
            assert [(t['kind'],t['mode']) for t in trace]==expected and successful and legacy
            baselines=re.findall(r'^CLOCK (?:REAP|FINAL) free=([0-9A-F]+) expected=([0-9A-F]+)',log,re.M)
            assert len(baselines)==len(trace)+1 and all(a==b for a,b in baselines)
            assert log.count('CLOCK SURVIVORS ')==len(trace) and 'CLOCK PRIVATE false_dispatch=00000001 null_timer=00000001 if0=00000001' in log
            emit(case/'observations.json',dict(optimization=opt,optimization_scope='GCC13 kernel/raw only; immutable genuine ELF compiler flags from bound manifest',scenario=scenario,memory=memory,cpus=cpus,successful_queries=successful,failed_queries=failed,legacy_queries=legacy,cases=trace,stage_records_words=records,exact_reap_checks=len(baselines),rational_units_verified=True,independent_irq_observer=True,guest_log_sha256=sha(case/'guest.log')))
            guests.append(dict(optimization=opt,scenario=scenario,memory=memory,cpus=cpus,guest=str(case/'guest.log'),observations=str(case/'observations.json'),cases=trace,exact_reap_checks=len(baselines)))
            print('PASS clock O'+str(opt)+' scenario'+str(scenario)+' '+memory+'/smp'+str(cpus)+' cases='+str(len(trace)),flush=True)
    assert all(sha(Path(p))==h for p,h in sourcehash.items()),'Source/input changed during clock evidence'
status=dict(all_required_checks_pass=True,scope='Actual production coarse PIT uint64 clock/CPL3/checkedcopy/fault/reap with independent realIRQ observer; raw or immutable genuine TimeTicks inputs',source_before_after_identical=True,formal_upstream_guest_acceptance=bool(external),actual_v8_module_count=len(external),raw_only_localization=locate,kernel_and_raw_compiler='GCC13',optimization_scope='O0/O2 are GCC13 kernel/raw; genuine immutable Clang24 -Oz ELFs retain their bound compiler manifests',actual_v8_compiler='Clang24' if external else None,source_base=head,guests=guests,actual_case_count=sum(len(g['cases']) for g in guests),exact_reap_checks=sum(g['exact_reap_checks'] for g in guests),runtime_input_annotation_scope='Byteexact formal/static ISO ELF, only4B scenario field annotated in kernel CreateElf copy; machine code unchanged',bootstrap_headroom_irqs=headroom,boundary_scope='RAW0 first-before/last-after actual rollover; sameboot genuine0 consumes post-rollover uint64; genuine3 first-valid/last-invalid signed-bound fatal',coarse_only=True,high_resolution=False,full_v8_backend=False,browser_guest_pass=False,media_guest_pass=False,html5_guest_pass=False,capacity_changed=False)
emit(out/'status.json',status);print('Evidence: '+str(out/'status.json'),flush=True)
PY
