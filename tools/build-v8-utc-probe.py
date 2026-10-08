#!/usr/bin/env python3
"""Build the qualified prepared GTOS IA32 V8 UTC methods and CPL3 lifecycle probes."""
import argparse,ast,datetime,hashlib,json,math,os,pathlib,re,shutil,struct,subprocess,sys,time,traceback
repo=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description='Compile actual pinned V8 UTC methods and qualify GTOS CPL3 lifecycle')
p.add_argument('output',type=pathlib.Path)
for name in ('prepared-v8','prepared-sdk','prepared-gen','libcxx-source','clang','memory-cxx'):
    p.add_argument('--'+name,required=True,type=pathlib.Path)
p.add_argument('--build-only',action='store_true',help='Build/link and prove qualified ELF and loaded kernel bytes; do not start guests')
a=p.parse_args()
profile=json.loads((repo/'apps/native_v8_utc_probe/source-lock.json').read_text())

ws=repo.parent.parent;base=repo/'apps/native_v8_utc_probe'
src=a.prepared_v8.absolute()
out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
state=dict(scope='Actual pinned V8 UTC/ToJsTime/TimeTicks methods in production CPL3; not full V8 or browser',all_required_checks_pass=False,native_v8_utc_guest_pass=False,native_isolate_pass=False,full_v8_backend=False,browser_guest_pass=False,video_guest_pass=False,html5_guest_pass=False,production_image_changed=False,capacity_changed=False,commands=[],guests=[],inputs={})
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def emit(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def bind(p):
    p=pathlib.Path(p).absolute();h=sha(p)
    if str(p) in state['inputs']:assert state['inputs'][str(p)]==h,str(p)
    else:
        state['inputs'][str(p)]=h
        try:relative=p.relative_to(ws)
        except ValueError:relative=pathlib.Path('external')/hashlib.sha256(str(p).encode()).hexdigest()/p.name
        dest=out/'source-snapshot'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
def save():emit(out/'status.json',state)
def run(cmd,log,expected=0,timeout=180):
    cmd=[str(x) for x in cmd];start=time.monotonic()
    with log.open('wb') as f:
        try:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
        except subprocess.TimeoutExpired:code=124
    state['commands'].append(dict(argv=cmd,cwd=str(repo),exit_code=code,executable_sha256=sha(cmd[0]),seconds=time.monotonic()-start,log=str(log),log_sha256=sha(log)));save()
    if code!=expected:
        detail=log.read_text(errors='replace')[-5000:];guest=log.parent/'guest.log'
        if guest.is_file():detail+='\n'+guest.read_text(errors='replace')[-5000:]
        raise RuntimeError(str(log)+' exit='+str(code)+'\n'+detail)
    return log.read_text()
def dependencies(dep):
    for x in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
        path=pathlib.Path(x);bind(path if path.is_absolute() else repo/path)
def loaded_sha(p):
    data=pathlib.Path(p).read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01'
    h=struct.unpack_from('<HHIIIIIHHHHHH',data,16);assert h[0]==2 and h[1]==3 and h[2]==1 and h[8]==32
    digest=hashlib.sha256();digest.update(data[:16]);digest.update(struct.pack('<HHII',*h[:4]))
    for i in range(h[9]):
        ph=data[h[4]+i*32:h[4]+(i+1)*32];fields=struct.unpack('<8I',ph);digest.update(ph)
        if fields[0]==1:digest.update(data[fields[1]:fields[1]+fields[4]])
    return digest.hexdigest()

def nums(line):return {k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]+)',line)}
def rtc(line):
    words=[int(v,16) for v in line.split('bytes=',1)[1].split()];assert len(words)==10
    sec,minute,hour,day,month,year,century,fmt,divider,valid=words
    assert valid&128 and divider&0xf0==0x20 and not fmt&0x81
    def number(v):
        if fmt&4:return v
        assert v&15<=9 and v>>4<=9
        return (v>>4)*10+(v&15)
    h=number(hour&127)
    if fmt&2:assert not hour&128;hour=h
    else:assert 1<=h<=12;hour=h%12+(12 if hour&128 else 0)
    return int(datetime.datetime(number(century)*100+number(year),number(month),number(day),hour,number(minute),number(sec),tzinfo=datetime.timezone.utc).timestamp())*1000000
def observed(log,unsupported):
    known='UNHANDLED INTERRUPT 0x00000027';irq7=log.count(known);log=log.replace(known,'')
    assert '\nNATIVE V8 UTC SMOKE PASS\n' in log and 'FAILED' not in log and 'FAIL\n' not in log
    assert 'UNHANDLED INTERRUPT' not in log and 'PANIC' not in log
    before=rtc(next(x for x in log.splitlines() if x.startswith('UTC RTC before ')))
    after=rtc(next(x for x in log.splitlines() if x.startswith('UTC RTC after ')))
    assert before<=after
    cases=[];reaps=[];records=[];events=[];final=None
    for line in log.splitlines():
        if line.startswith('UTC RECORD words='):
            words=[int(x,16) for x in line.split('words=',1)[1].split()];assert len(words)==72
            data=struct.pack('<72I',*words);records.append(words);events=[]
        elif line.startswith('UTC EVENT '):events.append(nums(line))
        elif line.startswith('UTC CASE '):
            c=nums(line);w=records[-1];data=struct.pack('<72I',*w)
            assert w[0]==1 and w[1]==c['mode'] and w[3]==0 and w[4]>0
            assert w[6]==0x80000000 and w[7]>0 and w[70]==2
            assert c['dynamic']==3 and c['cost']>0 and c['cs']==0x23 and c['cr3']!=c['kernel_cr3']
            if unsupported:
                assert c['mode']==3 and w[2]==1 and w[8]==1 and w[9]==1 and w[5]==0
                assert c['utc']==c['mono']==c['errors']==1 and c['exit']==0x80000006 and c['vector']==6 and c['pf']==c['cr2']==0
                assert len(events)==2 and events[1]['error']==(-38)&0xffffffff and events[1]['kind']==1
                assert w[10:22]==[0]*12 and w[71]==0
                successful=events[:1]
            else:
                assert w[2]==2 and w[8]==8 and w[9]==9 and w[5]==22 and w[71]==8192
                assert c['utc']==5 and c['mono']>=4 and c['errors']==0 and len(events)==8
                raw=struct.unpack_from('<6I3Q',data,40)
                assert raw[:6]==(1,1,1,15,10000,1000000)
                assert raw[6]-raw[7]>=before and raw[6]-raw[7]<=after+1000000
                if c['mode']==1:assert c['exit']==0x8000000e and c['vector']==14 and c['pf']==6 and c['cr2']==0xbfffcffc
                else:assert c['exit']==(73 if c['mode']==2 else 0) and c['vector']==c['pf']==c['cr2']==0
                assert [e['kind'] for e in events]==[0,1,2,0,0,1,2,0]
                assert events[4]['value']>events[0]['value']
                successful=events
            for i,e in enumerate(successful):
                kind,reserved,value,js=struct.unpack_from('<IIQd',data,88+24*i)
                assert e['index']==i and e['error']==0 and kind==e['kind'] and reserved==0
                assert e['mono']==e['ticks']*11931000000//1193182
                assert value==e['value']+(1 if kind==0 else 0)
                if kind==0:assert js==0 and e['value']==e['mono']
                else:
                    assert e['value']-e['mono']==raw[6]-raw[7]
                    assert math.isfinite(js) and abs(js-value/1000.0)<=math.ulp(value/1000.0)
            c['events']=events;c['record_words']=w;cases.append(c)
        elif line.startswith('UTC REAP '):
            r=nums(line);assert r['free']==r['expected'] and r['peers']==1
            assert r['fp_initialized']==len(reaps)+2 and r['fp_invalidated']==len(reaps)+1 and r['fp_failures']==0
            assert r['fp_saves']>0 and r['fp_restores']>0
            if reaps:assert r['fp_saves']>reaps[-1]['fp_saves'] and r['fp_restores']>reaps[-1]['fp_restores']
            reaps.append(r)
        elif line.startswith('UTC FINAL '):
            final=nums(line);assert final['free']==final['expected']
            assert final['fp_initialized']==final['fp_invalidated']==(2 if unsupported else 5) and final['fp_failures']==0
    assert [c['mode'] for c in cases]==([3] if unsupported else [0,1,2,0])
    assert len(records)==len(cases)==len(reaps) and final
    assert len({c['id'] for c in cases})==len(cases)
    assert final['anchor']==(0 if unsupported else raw[6]-raw[7])
    return dict(cases=cases,reaps=reaps,final=final,irq7_exact_known_tokens=irq7,exact_reap_checks=len(reaps)+1,
        actual_independent_method_events=sum(len(c['events']) for c in cases),actual_js_conversions=0 if unsupported else 4*len(cases))
def stack_proof(directory,disassembly):
    frames={}
    for p in sorted(directory.glob('*.su')):
        for line in p.read_text().splitlines():
            function,size,kind=line.rsplit('\t',2)
            assert kind in ('static','dynamic,bounded'),line
            name=function.rsplit(':',1)[-1]
            if 'memcpy(' in function:name='memcpy'
            if 'memset(' in function:name='memset'
            frames[name]=max(frames.get(name,0),int(size))
    aliases={};groups={}
    for line in (directory/'symbols.log').read_text().splitlines():
        match=re.match(r'^([0-9a-fA-F]+) [TtWw] (\S+)$',line)
        if match:groups.setdefault(int(match.group(1),16),[]).append(match.group(2))
    for address,names in groups.items():
        known=[name for name in names if name in frames]
        for name in names:
            if name not in frames and known:
                frames[name]=max(frames[x] for x in known)
                aliases[name]=dict(address=address,real_compiler_symbols=known)
    graph={};indirect={};current=None
    for line in disassembly.splitlines():
        h=re.match(r'^[0-9a-f]+ <([^>]+)>:',line)
        if h:current=h.group(1);graph.setdefault(current,set())
        ins=re.match(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+([a-z][a-z0-9]*)\s*(.*)',line)
        if not ins or ins.group(1) not in ('call','calll','jmp','jmpl'):continue
        assert current,'Transfer outside symbol '+line
        if '*' in ins.group(2):
            indirect.setdefault(current,[]).append(line)
            continue
        t=re.search(r'<([^>]+)>',ins.group(2));assert t,'Unknown transfer '+line
        target=t.group(1).split('+0x',1)[0]
        if target!=current or ins.group(1).startswith('call'):graph[current].add(target)
    active=set();done={}
    def visit(node):
        assert node in graph and node not in active,'Missing/recursive direct target '+node
        if node in done:return done[node]
        assert node not in indirect,'Reachable unbound indirect transfer '+str(indirect[node])
        active.add(node)
        assert node=='_start' or node in frames,'Missing real compiler frame '+node
        suffix=max([visit(x) for x in graph[node]] or [0])
        bound=frames.get(node,0)+32+suffix
        active.remove(node);done[node]=bound
        return bound
    bound=visit('_start')+64
    assert bound<=8176
    return dict(method='Longest actual direct acyclic path reachable from ELF _start of real bounded compiler frames +32/edge +64 entry; all reachable indirect transfers rejected',
        bound=bound,limit=8176,frames=frames,elf_proven_frame_aliases=aliases,reachable_symbols=sorted(done),
        retained_unreachable_indirect={k:v for k,v in indirect.items() if k not in done},
        direct_graph={k:sorted(v) for k,v in graph.items()})
try:
    bind(pathlib.Path(__file__))
    roots={'prepared-v8':src,'prepared-sdk':a.prepared_sdk.absolute(),'prepared-gen':a.prepared_gen.absolute(),
        'libcxx':a.libcxx_source.absolute()/'include','clang-resource':a.clang.absolute().parent.parent/'lib/clang/24/include'}
    for root,files in profile['files'].items():
        for relative,h in files.items():
            path=roots[root]/relative;assert sha(path)==h,'Changed prepared prerequisite '+str(path);bind(path)
    for relative,h in profile['repository_inputs'].items():
        path=repo/relative;assert sha(path)==h,'Changed qualified kernel input '+relative;bind(path)
    for relative,h in profile['module_files'].items():assert sha(base/relative)==h,'Changed UTC module input '+relative

    lockpath=repo/'apps/native_v8_runtime/source-lock.json';bind(lockpath);lock=json.loads(lockpath.read_text())
    clang=a.clang.absolute();cxx=a.memory_cxx.absolute()
    assert sha(clang)==lock['clang_sha256']
    assert subprocess.check_output([str(cxx),'-dumpfullversion'],text=True).strip()=='13.3.0'
    bins={n:pathlib.Path(shutil.which(n)).absolute() for n in ('as','ld','nm','readelf','objdump','objcopy','grub-mkrescue','qemu-system-i386')}
    state['tools']={n:dict(path=str(v),sha256=sha(v)) for n,v in dict(bins,clang=clang,cxx=cxx).items()}
    state['source_base']=run([pathlib.Path(shutil.which('git')),'rev-parse','HEAD'],out/'base.log').strip()
    state['qualified_kernel_source_base']='94c3cec724da071433dbc56ae39bfbf0954b699f'
    for f in base.iterdir():
        if f.is_file():bind(f)
    flags=lock['qualified_native_flags']+['-DLIBC_NAMESPACE=__llvm_libc_cr','-fno-builtin','-fno-jump-tables',
        '-isystem',str(src/'tools/gtos_isolate_diagnostic/sdk'),'-isystem',str(a.libcxx_source.absolute()/'include'),
        '-isystem',str(a.prepared_sdk.absolute()),'-isystem',str(clang.parent.parent/'lib/clang/24/include'),
        '-I',str(src/'third_party/llvm-libc/src'),'-I',str(repo/'apps/native_v8_runtime/include'),'-I',str(base),'-I',str(src),'-I',str(a.prepared_gen.absolute()),'-I',str(src/'include'),'-I',str(src/'third_party/abseil-cpp')]
    common=['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti',
        '-fno-stack-protector','-fno-pie','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables',
        '-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-Itests','-I'+str(base),'-I'+str(src),
        '-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix','-Wall','-Wextra','-Werror']+(repo/'tools/kernel-cxxflags').read_text().split()
    sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
        'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp',
        'src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp']
    assembly=['tests/native_process_loader.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
    for f in (repo/'include').rglob('*'):
        if f.is_file():bind(f)
    for name in sources+assembly+['tests/native_process_smoke.cpp','tests/native_process_probe_expectations.h','tests/native_process_smoke.ld','tools/kernel-cxxflags',
        'src/process/resources_png.inc','tools/audit-kernel-instructions.py','apps/native_v8_runtime/runtime/memory.cc','apps/native_process_info_probe/start.s']:bind(repo/name)
    for opt in (0,2):
        level=out/('O'+str(opt));level.mkdir();elfs=[]
        library=level/'library';library.mkdir()
        for name in ('time.cc','gtos-clock-bridge.cc'):
            inputfile=src/'src/base/platform'/name if name=='time.cc' else repo/'apps/native_v8_runtime/runtime'/name
            bind(inputfile);obj=library/(name.replace('.cc','.o'))
            run([clang,*flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',inputfile,'-o',obj],obj.with_suffix('.log'))
            dependencies(obj.with_suffix('.d'))
        for mode in range(4):
            app=level/('mode'+str(mode))
            app.mkdir()
            run([clang,*flags,'-O'+str(opt),'-DGTOS_UTC_MODE='+str(mode),'-MD','-MF',app/'main.d','-c',base/'main.cc','-o',app/'main.o'],app/'main.log');dependencies(app/'main.d')
            run([cxx,*common,'-O'+str(opt),'-c','apps/native_v8_runtime/runtime/memory.cc','-o',app/'memory.o'],app/'memory.log')
            run([bins['as'],'--32','apps/native_process_info_probe/start.s','-o',app/'start.o'],app/'start.log')
            run([bins['ld'],'-melf_i386','--gc-sections','-T',base/'linker.ld','-Map',app/'probe.map','-o',app/'probe.elf',
                app/'start.o',app/'main.o',app/'memory.o',library/'time.o',library/'gtos-clock-bridge.o'],app/'link.log')
            assert not run([bins['nm'],'-u',app/'probe.elf'],app/'undefined.log').strip()
            symbols=run([bins['nm'],'-n',app/'probe.elf'],app/'symbols.log');assert re.search(r'^40020000 [Dd] native_utc_record$',symbols,re.M)
            run([bins['readelf'],'-h','-l',app/'probe.elf'],app/'elf.log')
            for su in library.glob('*.su'):shutil.copyfile(su,app/su.name)
            dis=run([bins['objdump'],'-d',app/'probe.elf'],app/'disassembly.log');assert not re.search(r'\b(?:ymm|zmm|mm[0-7])\b',dis)
            emit(app/'stack-bound.json',stack_proof(app,dis))
            run([bins['objcopy'],'--strip-all',app/'probe.elf',app/'probe.stripped.elf'],app/'strip.log')
            assert (app/'probe.stripped.elf').stat().st_size<=65536
            assert sha(app/'probe.stripped.elf')==profile['qualified_elfs'][f'O{opt}/mode{mode}/probe.stripped.elf'],'ELF differs from actual guest qualification'
            elfs.append(app/'probe.stripped.elf')
        for unsupported in [0,1]:
            d=level/('unsupported'+str(unsupported));(d/'kernel').mkdir(parents=True);(d/'iso/boot/grub').mkdir(parents=True)
            for f in [repo/n for n in sources]+[base/'kernel.cpp']:
                obj=d/'kernel'/(f.stem+'.o')
                run([cxx,*common,'-O'+str(opt),'-DGTOS_UTC_UNSUPPORTED='+str(unsupported),'-Wno-write-strings','-MD','-MF',obj.with_suffix('.d'),'-c',f,'-o',obj],obj.with_suffix('.log'));dependencies(obj.with_suffix('.d'))
            run([cxx,*common,'-O'+str(opt),'-c','apps/native_v8_runtime/runtime/memory.cc','-o',d/'kernel/byte_helpers.o'],d/'byte_helpers.log')
            for f,n in zip(assembly,['loader.o','native_fp_asm.o','stubs.o']):run([bins['as'],'--32',f,'-o',d/'kernel'/n],d/(n+'.log'))
            run([bins['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin',*sorted((d/'kernel').glob('*.o'))],d/'link.log')
            assert not run([bins['nm'],'-u',d/'kernel.bin'],d/'undefined.log').strip()
            run([pathlib.Path(sys.executable),'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'integer-audit.log')
            assert loaded_sha(d/'kernel.bin')==profile['qualified_loaded_kernel_sha256'][f'O{opt}/unsupported{unsupported}/kernel.bin'],'Loaded kernel differs from actual guest qualification'
            if a.build_only:continue
            shutil.copyfile(d/'kernel.bin',d/'iso/boot/native.bin')
            for mode,elf in enumerate(elfs):shutil.copyfile(elf,d/'iso/boot'/('utc'+str(mode)+'.elf'))
            (d/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Actual V8 UTC" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/utc'+str(mode)+'.elf\n' for mode in range(4))+' boot\n}\n')
            run([bins['grub-mkrescue'],'--output='+str(d/'native.iso'),d/'iso'],d/'grub.log')
            for mem,cpus in ((32,1),(64,4)):
                guest=d/(str(mem)+'M-'+str(cpus));guest.mkdir()
                run([bins['qemu-system-i386'],'-L',os.environ['GTOS_QEMU_DATA_DIR'],'-machine','pc','-accel','tcg','-cpu','max',
                    '-rtc','base=2000-01-01T00:00:00,clock=vm','-m',str(mem),'-smp',str(cpus),'-cdrom',d/'native.iso','-boot','d',
                    '-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest/'guest.log'),
                    '-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],guest/'host.log',33)
                evidence=observed((guest/'guest.log').read_text(),bool(unsupported));emit(guest/'observations.json',evidence)
                state['guests'].append(dict(opt=opt,unsupported=unsupported,memory=mem,cpus=cpus,evidence=evidence,guest_log_sha256=sha(guest/'guest.log')))
                print('PASS V8 UTC '+str((opt,unsupported,mem,cpus)),flush=True)
    assert all(sha(p)==h for p,h in state['inputs'].items()),'Source changed during qualification'
    state.update(all_required_checks_pass=True,native_v8_utc_guest_pass=not a.build_only,source_before_after_identical=True,qualification_reused=a.build_only,exact_qualified_elf_bytes=True,exact_qualified_loaded_kernel_bytes=True,nonloaded_kernel_symbols_path_dependent=True,previous_qualification=profile['qualification'],exact_reap_checks=sum(g['evidence']['exact_reap_checks'] for g in state['guests']),
        actual_victim_cases=sum(len(g['evidence']['cases']) for g in state['guests']))
except BaseException as e:
    state['failure']=str(e);(out/'exception.log').write_text(traceback.format_exc())
finally:
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    print(json.dumps({k:v for k,v in state.items() if k not in ('inputs','commands','guests','reused_actual_compilations')},indent=2),flush=True)
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
