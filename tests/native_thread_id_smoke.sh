#!/usr/bin/env bash
# Static-qualified genuine compiler TLS/ThreadId inputs, then real GTOS CPL3.
set -euo pipefail
if [ "$#" -ne 15 ]; then
    echo "usage: native_thread_id_smoke.sh FRESH_DIR O0_MODE0.elf ... O0_MODE6.elf OZ_MODE0.elf ... OZ_MODE6.elf" >&2
    exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 - "$repo" "$@" <<'PY'
import hashlib,json,os,re,shutil,struct,subprocess,sys,time
from pathlib import Path
repo=Path(sys.argv[1]);out=Path(sys.argv[2]);args=sys.argv[3:]
if sys.version_info<(3,9):raise SystemExit('Qualified Python>=3.9 required')
if out.exists():raise SystemExit('Refusing to reuse evidence directory: '+str(out))
out.mkdir(parents=True);out=out.resolve()
assert len(args)==14
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def emit(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
cxx=Path(os.environ.get('CXX',''))
if not cxx.is_absolute() or not cxx.is_file():raise SystemExit('CXX must be an absolute qualified compiler wrapper')
qemu=Path(shutil.which(os.environ.get('GTOS_QEMU_SYSTEM_I386','qemu-system-i386')) or '')
grub=Path(shutil.which(os.environ.get('GTOS_GRUB_MKRESCUE','grub-mkrescue')) or '')
assert qemu.is_file() and grub.is_file()
commands=[]
envkeys=('PATH','CXX','LD_LIBRARY_PATH','GTOS_QEMU_SYSTEM_I386','GTOS_QEMU_DATA_DIR','GTOS_GRUB_MKRESCUE','GTOS_GRUB_MODULES_DIR','GTOS_RUNTIME_ROOT','TMPDIR')
def run(argv,log,timeout=None,qemu_run=False):
    argv=[str(x) for x in argv];started=time.time()
    row=dict(argv=argv,cwd=str(repo),environment={k:os.environ[k] for k in envkeys if k in os.environ},executable_sha256=sha(argv[0]),log=str(log))
    try:
        with log.open('wb') as stream:r=subprocess.run(argv,cwd=repo,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
        code=r.returncode
    except subprocess.TimeoutExpired:code=124
    row.update(exit_code=code,elapsed_seconds=time.time()-started);commands.append(row);emit(out/'commands.json',commands)
    if code!=(33 if qemu_run else 0):raise RuntimeError('Command failed '+str(code)+': '+str(log)+'\n'+log.read_text(errors='replace')[-6000:])
    return code
for tool,name in ((cxx,'gcc'),(qemu,'qemu'),(grub,'grub')):run([tool,'--version'],out/(name+'-version.txt'))
emit(out/'python-version.json',dict(executable=sys.executable,version=sys.version))
# Read exact current worktree reference, never invoke Git or modify its index.
gitref=(repo/'.git').read_text().strip();gitdir=Path(gitref.removeprefix('gitdir: ').strip())
head=(gitdir/'HEAD').read_text().strip()
if head.startswith('ref: '):
    common=(gitdir/(gitdir/'commondir').read_text().strip()).resolve()
    head=(common/head[5:]).read_text().strip()
(out/'base-commit.txt').write_text(head+'\n')
def elf_geometry(path):
    data=Path(path).read_bytes()
    assert data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,3)
    po=struct.unpack_from('<I',data,28)[0];ps,pc=struct.unpack_from('<HH',data,42);assert ps==32
    segments=[];pages=set();tables=set()
    for index in range(pc):
        ty,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',data,po+index*ps)
        if ty!=1:continue
        assert off+fs<=len(data) and fs<=ms and 0<ms and va+ms<=0xC0000000
        segments.append(dict(offset=off,va=va,file_bytes=fs,memory_bytes=ms,flags=fl))
        pages.update(range(va//4096,(va+ms+4095)//4096));tables.update(range(va>>22,((va+ms-1)>>22)+1))
    tables.add(0xBFFFD000>>22)
    assert (0x80000000>>22) not in tables and len(pages)+2+16<=256
    return data,segments,dict(actual_load_pages=len(pages),user_stack_pages=2,static_pdes=sorted(tables),static_frame_cost=len(pages)+2+len(tables)+1,dynamic_data_frames=16,dynamic_new_table_frames=1,dynamic_frame_cost=17)
def file_words(data,segments,va,count,writable=None):
    matching=[s for s in segments if s['va']<=va and va+count*4<=s['va']+s['file_bytes']]
    assert len(matching)==1;s=matching[0]
    if writable is not None:assert bool(s['flags']&2)==writable and not (s['flags']&1)
    return struct.unpack_from('<'+str(count)+'I',data,s['offset']+va-s['va'])
external={};bindings=[];qualified_input_hashes={}
for i,arg in enumerate(args):
    opt='O0' if i<7 else 'Oz';mode=i%7;elf=Path(arg).resolve();mf=elf.parent/'manifest.json';m=json.loads(mf.read_text());pf=Path(m['whole_stack_status']);proof=json.loads(pf.read_text())
    assert m['tls_mode']==mode and m['optimization']==opt and m['role_nonce']==0xA9
    assert head=='d726387c2e5af7e6e15aaf6f38cfea5b9bc3d2c7' and m['source_base']==head
    assert m['native_compiler_target']=='i686-unknown-none-elf' and m['linux_target_sdk_used'] is False and m['capacity_changed'] is False
    assert (m['boot_demo_file_limit_bytes'],m['user_page_budget'],m['user_stack_bytes'],m['user_stack_usable_bytes'])==(65536,256,8192,8176)
    assert m['build_bound_source_before_after_identical'] and m['source_sha256'] and m['compiled_input_sha256']
    assert m['actual_target_identity']['gtos'] and not any(m['actual_target_identity'][k] for k in ('linux','unix','posix'))
    bound={str((repo/p).resolve()):h for p,h in m['source_sha256'].items()}
    for mapping in (bound,m['compiled_input_sha256'],m['pinned_sdk_input_sha256'],m['generated_sdk_and_derived_sha256'],proof['input_hashes']):
        for p,h in mapping.items():
            assert sha(p)==h,('Changed formally qualified input',p)
            assert p not in qualified_input_hashes or qualified_input_hashes[p]==h
            qualified_input_hashes[p]=h
    assert m['probe_record_address']==0x40030000 and m['probe_record_bytes']==512 and m['probe_record_symbol']=='native_tls_record'
    assert m['native_build_pass'] and m['source_provenance_pass'] and m['stack_call_chain_qualified'] and m['qualification_complete'] and m['guest_pass'] is False
    assert proof['qualification_pass'] and proof['full_stack_callchain_qualified'] and not proof['unknowns']
    assert proof['input_hashes'][str(mf)]==sha(mf) and sha(elf)==m['stripped_sha256']==proof['elf_sha256'] and elf.stat().st_size<=65536
    controls=m['control_inventory'];assert len(controls)==5 and sorted(c['logical_role'] for c in controls)==list(range(5))
    assert sorted(c['actual_slot'] for c in controls)==list(range(5)) and len({c['control_va'] for c in controls})==5
    for c in controls:assert c['size']>0 and c['alignment']>0 and c['alignment']&(c['alignment']-1)==0
    data,segments,geometry=elf_geometry(elf)
    assert file_words(data,segments,0x40030000,8,True)==(1,512,1,mode,0,0,0,0xA9)
    role_contract={0:(4,4),1:(257,64),2:(64,16),3:(64,4096),4:(65504,16)}
    first_control=min(c['control_va'] for c in controls)
    for c in controls:
        assert (c['size'],c['alignment'])==role_contract[c['logical_role']]
        assert c['control_va']==first_control+16*c['actual_slot']
        assert file_words(data,segments,c['control_va'],4,True)==(c['size'],c['alignment'],0,c['template_va'])
        if c['logical_role'] in (0,1,4):assert c['template_va']==0
        else:
            values=file_words(data,segments,c['template_va'],16,False)
            raw=struct.pack('<16I',*values)
            assert raw==bytes((0xA5,0x69,0x37) if c['logical_role']==2 else (0xC4,0x1B,0xA9))+bytes(61)
    external[opt,mode]=(elf,m)
    bindings.append(dict(optimization=opt,mode=mode,elf=str(elf),sha256=sha(elf),manifest=str(mf),manifest_sha256=sha(mf),whole_stack_status=str(pf),whole_stack_status_sha256=sha(pf),control_inventory=controls,actual_elf_geometry=geometry))
emit(out/'actual-input-preflight.json',dict(all_formal_static_bindings_verified=True,qualified_input_count=len(qualified_input_hashes),bindings=bindings))
emit(out/'qualified-input-hashes.json',qualified_input_hashes)
(out/'external-inputs.txt').write_text(''.join(str(Path(a).resolve())+'\n' for a in args))
sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp','src/process/native_runtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp','tests/native_thread_id_smoke.cpp']
assembly=['tests/native_process_loader.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
helpers='apps/native_clock_probe/memory.cc'
inputs={p for p in (repo/'include').rglob('*') if p.is_file()}
inputs.update(repo/p for p in sources+assembly+[helpers,'src/process/resources_png.inc','tests/native_process_smoke.cpp','tests/native_process_probe_expectations.h','tests/native_process_smoke.ld','tests/native_thread_id_smoke.sh','apps/native_thread_id_probe/record.h','apps/v8_thread_id_probe/emutls.h','apps/v8_thread_id_probe/emutls.cc','tools/kernel-cxxflags','tools/audit-kernel-instructions.py'])
for b in bindings:inputs.update(Path(b[k]) for k in ('elf','manifest','whole_stack_status'))
inputs.update(Path(p) for p in qualified_input_hashes)
sourcehash={str(p):sha(p) for p in sorted(inputs)};(out/'source-inputs.sha256').write_text(''.join(h+'  '+p+'\n' for p,h in sourcehash.items()))
tools={n:dict(path=shutil.which(n),sha256=sha(shutil.which(n))) for n in ('as','ld','nm','objcopy')}
emit(out/'tool-inputs.json',dict(compiler=dict(path=str(cxx),sha256=sha(cxx)),tools=tools,qemu=dict(path=str(qemu),sha256=sha(qemu)),grub=dict(path=str(grub),sha256=sha(grub))))
flags=['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-pie','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix','-Wall','-Wextra','-Werror']+(repo/'tools/kernel-cxxflags').read_text().split()
def annotation(path,mode,nonce):
    original=Path(path).read_bytes();data=bytearray(original);assert original[:7]==b'\x7fELF\x01\x01\x01'
    po=struct.unpack_from('<I',original,28)[0];ps,pc=struct.unpack_from('<HH',original,42);assert ps==32
    offsets=[];code=[]
    for i in range(pc):
        ty,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',original,po+i*ps)
        if ty!=1:continue
        assert off+fs<=len(original)
        if fl&1:code.append((off,fs))
        if va<=0x40030000 and 0x40030000+512<=va+fs:
            assert fl&2 and not fl&1;offsets.append(off+0x40030000-va)
    assert len(offsets)==1;record=offsets[0];assert struct.unpack_from('<4I',data,record)==(1,512,1,mode)
    pos=record+28;assert struct.unpack_from('<I',data,pos)[0]==0xA9;struct.pack_into('<I',data,pos,nonce)
    diffs=[i for i,(x,y) in enumerate(zip(original,data)) if x!=y];assert all(pos<=i<pos+4 for i in diffs)
    oldcode=[hashlib.sha256(original[o:o+n]).hexdigest() for o,n in code];newcode=[hashlib.sha256(data[o:o+n]).hexdigest() for o,n in code];assert oldcode==newcode
    return dict(mode=mode,nonce=nonce,record_va=0x40030000,record_file_offset=record,allowed_write_offset=pos,allowed_write_bytes=4,old_word=0xA9,new_word=nonce,actual_changed_offsets=diffs,original_sha256=sha(path),predicted_runtime_copy_sha256=hashlib.sha256(data).hexdigest(),executable_segment_sha256=oldcode,machine_code_unchanged=True)
guests=[]
for kopt,aopt,memory,cpus in ((0,'O0','32M',1),(2,'Oz','64M',4)):
    d=out/('kernelO'+str(kopt)+'-app'+aopt);(d/'kernel').mkdir(parents=True);(d/'iso/boot/grub').mkdir(parents=True)
    rows=[];copies=[];annotations=[]
    for module in range(8):
        mode=module if module<7 else 3;nonce=0xA9 if module<7 else 0x37;elf,m=external[aopt,mode]
        controls=sorted(m['control_inventory'],key=lambda c:c['logical_role'])
        row='{'+str(mode)+'U,'+str(nonce)+'U,5U,{'+','.join('{'+','.join(str(x)+'U' for x in (c['control_va'],c['size'],c['alignment'],c['template_va'],c['logical_role'],c['actual_slot'],(0x53*c['logical_role'])&255))+'}' for c in controls)+'}}'
        rows.append(row)
        target=d/'iso/boot'/('tls-'+str(module)+'.elf');shutil.copyfile(elf,target);assert sha(target)==sha(elf)
        copies.append(dict(module=module,mode=mode,nonce=nonce,source=str(elf),copy=str(target),sha256=sha(target)))
        annotations.append(annotation(target,mode,nonce))
    (d/'thread_id_inputs.h').write_text('static const TlsExpectedInput nativeTlsInputs[8] = {\n'+',\n'.join(rows)+'\n};\n')
    emit(d/'input-copies.json',copies);emit(d/'runtime-input-annotations.json',annotations)
    for src in sources:
        obj=d/'kernel'/(Path(src).stem+'.o')
        run([cxx]+flags+['-O'+str(kopt),'-Wno-write-strings','-I'+str(d),'-MD','-MF',obj.with_suffix('.d'),'-c',src,'-o',obj],d/(Path(src).stem+'.log'))
    run([cxx]+flags+['-O'+str(kopt),'-c',helpers,'-o',d/'kernel/byte_helpers.o'],d/'byte-helpers.log')
    for src,name in zip(assembly,['loader.o','native_fp.asm.o','stubs.o']):run([tools['as']['path'],'--32',src,'-o',d/'kernel'/name],d/(name+'.log'))
    run([tools['ld']['path'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin']+sorted((d/'kernel').glob('*.o')),d/'link.log')
    run([tools['nm']['path'],'-u',d/'kernel.bin'],d/'undefined.txt');assert not (d/'undefined.txt').read_text().strip()
    run([tools['nm']['path'],'-n',d/'kernel.bin'],d/'symbols.txt');assert 'NativeThreadIdUnusedBaseline' not in (d/'symbols.txt').read_text()
    run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'scalar-audit.txt')
    shutil.copyfile(d/'kernel.bin',d/'iso/boot/native.bin')
    (d/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native compiler TLS" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/tls-'+str(m)+'.elf\n' for m in range(8))+' boot\n}\n')
    run([grub,'--output='+str(d/'thread-id.iso'),d/'iso'],d/'grub.log')
    argv=[qemu]
    if os.environ.get('GTOS_QEMU_DATA_DIR'):argv+=['-L',os.environ['GTOS_QEMU_DATA_DIR']]
    argv+=['-machine','pc','-accel','tcg','-m',memory,'-smp',str(cpus),'-cdrom',d/'thread-id.iso','-boot','d','-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(d/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot']
    emit(d/'qemu-invocation.json',dict(argv=[str(a) for a in argv],cwd=str(repo),environment={k:os.environ[k] for k in envkeys if k in os.environ},qemu_sha256=sha(qemu),iso_sha256=sha(d/'thread-id.iso'),kernel_sha256=sha(d/'kernel.bin'),expectation_header_sha256=sha(d/'thread_id_inputs.h'),input_copies=copies,runtime_annotations=annotations))
    run(argv,d/'qemu.log',120,qemu_run=True)
    log=(d/'guest.log').read_text(errors='replace');assert '\nNATIVE THREAD ID SMOKE PASS\n' in log
    cases=[];first=[];controls=[];records=[];survivors=[]
    for line in log.splitlines():
        if line.startswith('TLS RECORD '):
            words=[int(x,16) for x in line.split('words=',1)[1].split()];assert len(words)==128;records.append(dict(phase=int(line.split('phase=')[1].split()[0],16),words=words))
        elif line.startswith(('TLS CASE ','TLS FIRST_TRY ','TLS CONTROL ','TLS SURVIVORS ')):
            v={k:int(x,16) for k,x in re.findall(r'(\w+)=([0-9A-F]+)',line)}
            if line.startswith('TLS CASE '):
                assert v['cs']==0x23 and v['nonce']==0xA9 and v['dynamic']==17 and v['yields']>0 and v['calls']>0
                assert v['stage']==(2 if v['mode']==3 else 3)
                assert v['retained']==v['static']+(0 if v['mode']==0 else 17)
                actual_geometry=next(b['actual_elf_geometry'] for b in bindings if b['optimization']==aopt and b['mode']==v['mode'])
                assert v['static']==actual_geometry['static_frame_cost']
                if v['mode']==2:assert v['cr2']==0xBFFFCFFC and v['pf']==6 and v['exit']==0x8000000e
                else:
                    assert v['pf']==0 and v['exit']==({3:73,4:0x4A000006,5:0x4A000003,6:0x4A000003}.get(v['mode'],0))
                cases.append(v)
            elif line.startswith('TLS FIRST_TRY '):assert v['live']==1 and v['dynamic']==17 and v['retained']==v['static']+17;first.append(v)
            elif line.startswith('TLS CONTROL '):controls.append(v)
            else:
                assert v['cpu_before']!=v['cpu_after'] and v['cpu_calls']==0 and v['cpu_yields']==0 and v['cpu_live']==1 and v['peer_live']==1
                assert v['peer_yields_after']>v['peer_yields_before'] and v['cpu_run_after']>v['cpu_run_before']
                assert v['ring0_after']!=v['ring0_before'] and v['boot_after']>v['boot_before'] and ((v['ticks_after']-v['ticks_before'])&0xffffffff)>=8;survivors.append(v)
    assert [c['mode'] for c in cases]==list(range(7))+[0] and [c['ordinal'] for c in cases]==list(range(8))
    assert len(first)==9 and len(survivors)==8 and len(records)>=26
    baselines=re.findall(r'^TLS (?:REAP|FINAL) free=([0-9A-F]+) expected=([0-9A-F]+)',log,re.M);assert len(baselines)==9 and all(x==y for x,y in baselines)
    emit(d/'observations.json',dict(kernel_optimization=kopt,app_optimization=aopt,memory=memory,cpus=cpus,cases=cases,first_try_allocations=first,compiler_control_snapshots=controls,record_snapshots=records,survivors=survivors,exact_baseline_checks=len(baselines),guest_log_sha256=sha(d/'guest.log')))
    guests.append(dict(kernel_optimization=kopt,app_optimization=aopt,memory=memory,cpus=cpus,guest=str(d/'guest.log'),observations=str(d/'observations.json'),victim_cases=len(cases),tls_admissions=len(first),process_reaps=10,exact_baseline_checks=len(baselines)))
    assert all(sha(p)==h for p,h in sourcehash.items()),'Source/input changed during evidence'
    print('PASS genuine ThreadId kernelO'+str(kopt)+'/app'+aopt+' '+memory+'/smp'+str(cpus)+' victims8/TLSadmissions9/processReaps10/baselines9',flush=True)
status=dict(all_required_checks_pass=True,source_before_after_identical=True,source_base=head,actual_boots=2,actual_victim_cases=sum(g['victim_cases'] for g in guests),actual_tls_admissions=sum(g['tls_admissions'] for g in guests),actual_process_reaps=sum(g['process_reaps'] for g in guests),exact_baseline_checks=sum(g['exact_baseline_checks'] for g in guests),guests=guests,formal_input_count=14,kernel_compiler='GCC13',app_compiler='Clang24',optimization_configurations='GCCkernelO0/ClangappO0 32M1 and GCCkernelO2/ClangappOz64M4; not Cartesian',first_try_allocation_independently_observed=True,timer_preemption_scope='CPU-bound peer has no syscalls/yields and increasing runTicks; Yield-heavy TLS tasks can legitimately report zero IRQ samples',normal_finalize_storage_only=True,abrupt_reap_is_not_destructors=True,nonce_annotation_scope='Private CreateElf load copy only nonce field offset28, all ISO originals byteexact, executableLOADunchanged',request_exit_scope='Mode3 can be cancelled during healthy stage2 hold; no stage3 or post-hold consumer-call completion claim',same_pas_threads=False,native_gs_tls=False,tls_destructors=False,capacity_changed=False,full_v8=False,browser=False,video=False,html5=False,jit=False)
emit(out/'status.json',status);print('Evidence: '+str(out/'status.json'),flush=True)
PY