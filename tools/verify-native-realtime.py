#!/usr/bin/env python3
"""Build and verify real IA32 CMOS/PIT UTC syscalls in an isolated guest."""
import argparse, calendar, datetime, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time, traceback
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--locate', action='store_true')
parser.add_argument('--emulator', choices=('qemu', 'bochs', 'both'), default='qemu')
parser.add_argument('--bochs-root', type=pathlib.Path)
parser.add_argument('--bochs-runner', type=pathlib.Path)
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[1]
out = args.output.resolve()
assert not out.exists(), 'Choose a fresh evidence directory'
out.mkdir(parents=True)
state = dict(scope='Actual CMOS/PIT UTC read-only provider and original monotonic ABI in production CPL3; not V8 Isolate/browser',
    all_required_checks_pass=False, browser_guest_pass=False, native_isolate_pass=False, capacity_changed=False,
    production_image_changed=False, inputs={}, commands=[], guests=[])
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save():
    (out/'status.json').write_text(json.dumps(state, indent=2)+'\n')
def bind(path):
    path = pathlib.Path(path).resolve()
    state['inputs'][str(path)] = sha(path)
def run(argv, log, timeout=120, expected=0, env=None):
    argv = [str(v) for v in argv]
    start = time.monotonic()
    with log.open('wb') as stream:
        try: code = subprocess.run(argv, cwd=repo, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired: code = 124
    state['commands'].append(dict(argv=argv, cwd=str(repo), executable_sha256=sha(argv[0]), exit_code=code,
        seconds=time.monotonic()-start, log=str(log), log_sha256=sha(log),
        environment_overrides={k:env[k] for k in ('TZ','LD_LIBRARY_PATH') if env and k in env}))
    save()
    assert code == expected, str(log)+' exit='+str(code)+'\n'+log.read_text(errors='replace')[-5000:]
    return log.read_text()
def tool(name):
    result = shutil.which(name)
    assert result, 'Missing tool '+name
    return pathlib.Path(result).resolve()
def numbers(line):
    return {k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]+)',line)}
def rtc_epoch(line):
    raw = [int(v,16) for v in line.split('bytes=',1)[1].split()]
    assert len(raw)==10
    second, minute, hour, day, month, year, century, fmt, divider, valid = raw
    assert valid&128 and divider&0xF0==0x20 and not fmt&0x81
    def number(v):
        if fmt&4: return v
        assert v&15<=9 and v>>4<=9
        return (v>>4)*10+(v&15)
    second,minute,day,month,year,century = [number(v) for v in (second,minute,day,month,year,century)]
    h = number(hour&127)
    if fmt&2: assert not hour&128; hour=h
    else: assert 1<=h<=12; hour=h%12+(12 if hour&128 else 0)
    fields = (century*100+year, month, day, hour, minute, second)
    return calendar.timegm(datetime.datetime(*fields).timetuple())*1000000
def observed(text, unsupported):
    known = 'UNHANDLED INTERRUPT 0x00000027'
    irq7 = text.count(known)
    text = text.replace(known, '')
    assert '\nNATIVE REALTIME SMOKE PASS\n' in text and 'FAILED' not in text
    assert 'PANIC' not in text and 'UNHANDLED' not in text and 'DIAGNOSTIC PASS' not in text
    cases = [numbers(v) for v in text.splitlines() if v.startswith('REALTIME CASE ')]
    reaps = [numbers(v) for v in text.splitlines() if v.startswith('REALTIME REAP ')]
    final = [numbers(v) for v in text.splitlines() if v.startswith('REALTIME FINAL ')]
    assert [v['mode'] for v in cases] == ([3] if unsupported else [0,1,2])
    assert len(reaps)==len(cases) and len(final)==1
    f=final[0]
    assert all(v['free']==v['expected'] for v in reaps+[f])
    assert f['cases']==len(cases) and f['calls']==f['preserved'] and f['errors']==(1 if unsupported else 63) and f['irqs']>0
    for case in cases:
        assert case['cs']==0x23 and case['checks']>=(3 if unsupported else 57) and case['cost']>0
        assert case['retained']==case['cost']+(0 if unsupported else 3)
        if case['mode']==1:
            assert (case['vector'],case['address'],case['pf'],case['exit'])==(14,0xBFFFC000,6,0x8000000E)
        else:
            assert case['vector']==case['address']==case['pf']==0
            assert case['exit']==(73 if case['mode']==2 else 0)
        assert case['rejected']==(1 if unsupported else 18) and case['sentinels']==(64 if unsupported else 1072)
    samples=[numbers(v) for v in text.splitlines() if v.startswith('REALTIME SAMPLE ')]
    assert f['calls']==f['errors']+len(samples)
    assert len(re.findall(r'^NATIVE USER FAULT id=',text,re.M))==(0 if unsupported else 1)
    calendars=[rtc_epoch(v) for v in text.splitlines() if v.startswith('REALTIME RTC ')]
    assert len(calendars)==2 and 946684800000000<=calendars[0]<=calendars[1]<=946684830000000
    if unsupported:
        assert not samples and f['anchor']==0 and f['calls']==f['errors']
    else:
        assert len(samples)>=12 and calendars[0]<=f['anchor']<=calendars[1]
        for sample in samples:
            assert sample['mono']==sample['ticks']*11931000000//1193182
            assert sample['epoch']-sample['mono']==f['anchor']
        assert samples[-1]['epoch']>samples[0]['epoch']
    return dict(cases=cases,reaps=reaps,final=f,successful_samples=len(samples),
        independent_python_calendar_and_irq_oracle=True,known_irq7_notifications=irq7,exact_reap_checks=len(reaps)+1)
try:
    cxx=pathlib.Path(os.environ['CXX']).resolve();assert cxx.is_file()
    qemu=tool(os.environ.get('GTOS_QEMU_SYSTEM_I386','qemu-system-i386'))
    grub=tool(os.environ.get('GTOS_GRUB_MKRESCUE','grub-mkrescue'))
    assembler,linker,objcopy,nm,objdump=map(tool,('as','ld','objcopy','nm','objdump'))
    for p in (cxx,qemu,grub,assembler,linker,objcopy,nm,objdump,pathlib.Path(__file__)):bind(p)
    sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp',
        'src/hardwarecommunication/port.cpp','src/process/native_runtime.cpp','src/process/native_realtime.cpp',
        'src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp',
        'src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp',
        'tests/native_realtime_smoke.cpp','apps/native_clock_probe/memory.cc']
    assembly=['tests/native_process_loader.s','tests/native_process_user.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
    for p in (repo/'include').rglob('*'):
        if p.is_file():bind(p)
    for name in sources+assembly+['src/process/resources_png.inc','tests/native_process_smoke.cpp',
        'tests/native_process_probe_expectations.h','tests/native_process_smoke.ld','tools/kernel-cxxflags',
        'tools/audit-kernel-instructions.py','apps/native_realtime_probe/main.cpp','apps/native_realtime_probe/record.h',
        'apps/native_realtime_probe/start.s','apps/native_realtime_probe/linker.ld']:bind(repo/name)
    if args.emulator in ('bochs','both'):
        assert args.bochs_root and args.bochs_runner
        bind(args.bochs_runner)
        bind(args.bochs_root/'usr/bin/bochs-bin')
    state['source_base']=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    common=['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti',
        '-fno-stack-protector','-fno-pie','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables',
        '-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-D__GTOS__=1','-U__linux__','-U__unix__',
        '-Ulinux','-Uunix','-Wall','-Wextra','-Werror']+(repo/'tools/kernel-cxxflags').read_text().split()
    for opt in ([2] if args.locate else [0,2]):
        level=out/('O'+str(opt));level.mkdir()
        elfs=[]
        for mode in range(4):
            d=level/('probe'+str(mode));d.mkdir()
            run([cxx,*common,'-O'+str(opt),'-DGTOS_REALTIME_PROBE_MODE='+str(mode),'-c',
                'apps/native_realtime_probe/main.cpp','-o',d/'main.o'],d/'main-build.log')
            run([cxx,*common,'-O'+str(opt),'-c','apps/native_clock_probe/memory.cc','-o',d/'memory.o'],d/'memory-build.log')
            run([assembler,'--32','apps/native_realtime_probe/start.s','-o',d/'start.o'],d/'assembly.log')
            run([linker,'-melf_i386','--gc-sections','-T','apps/native_realtime_probe/linker.ld','-Map',d/'probe.map',
                '-o',d/'probe.elf',d/'start.o',d/'main.o',d/'memory.o'],d/'link.log')
            assert not run([nm,'-u',d/'probe.elf'],d/'undefined.log').strip()
            dis=run([objdump,'-d',d/'probe.elf'],d/'disassembly.log')
            assert not re.search(r'\b(?:call|jmp)\s+\*',dis)
            frames=[]
            for p in d.glob('*.su'):
                for line in p.read_text().splitlines():
                    name,size,kind=line.rsplit('\t',2);assert kind in ('static','dynamic,bounded')
                    frames.append(dict(function=name,bytes=int(size),kind=kind))
            bound=sum(v['bytes']+32 for v in frames)+64;assert frames and bound<=8176
            (d/'stack.json').write_text(json.dumps(dict(bound=bound,limit=8176,functions=frames,
                method='sum all static/bounded frames plus32 each and entry64; source/disassembly no indirect calls or recursion'),indent=2)+'\n')
            run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'probe.map',d/'probe.elf'],d/'scalar.log')
            run([objcopy,'--strip-all',d/'probe.elf',d/'probe.stripped.elf'],d/'strip.log')
            assert (d/'probe.stripped.elf').stat().st_size<=65536
            elfs.append(d/'probe.stripped.elf')
        for unsupported in ([0] if args.locate else [0,1]):
            d=level/('unsupported'+str(unsupported));(d/'kernel').mkdir(parents=True);(d/'iso/boot/grub').mkdir(parents=True)
            for name in sources:
                obj=d/'kernel'/(pathlib.Path(name).stem+'.o')
                run([cxx,*common,'-O'+str(opt),'-Wno-write-strings','-DGTOS_REALTIME_UNSUPPORTED='+str(unsupported),
                    '-c',name,'-o',obj],d/(pathlib.Path(name).stem+'-build.log'))
            for name,dest in zip(assembly,['loader.o','user.o','native_fp_asm.o','stubs.o']):
                run([assembler,'--32',name,'-o',d/'kernel'/dest],d/(dest+'.log'))
            run([linker,'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map',
                '-o',d/'kernel.bin',*sorted((d/'kernel').glob('*.o'))],d/'kernel-link.log')
            assert not run([nm,'-u',d/'kernel.bin'],d/'kernel-undefined.log').strip()
            run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,
                '--allow-user-range','native_user_start:native_user_end',d/'kernel.bin'],d/'kernel-scalar.log')
            shutil.copyfile(d/'kernel.bin',d/'iso/boot/native.bin')
            for mode,elf in enumerate(elfs):shutil.copyfile(elf,d/'iso/boot'/('probe'+str(mode)+'.elf'))
            cfg='set timeout=0\nset default=0\nmenuentry "Native realtime UTC" {\n multiboot /boot/native.bin\n'
            cfg+=''.join(' module /boot/probe'+str(mode)+'.elf\n' for mode in range(4))+' boot\n}\n'
            (d/'iso/boot/grub/grub.cfg').write_text(cfg)
            run([grub,'--output='+str(d/'native.iso'),d/'iso'],d/'grub.log')
            for mem,cpus in ([(32,1)] if args.locate else [(32,1),(64,4)]):
                for emulator in (['qemu','bochs'] if args.emulator=='both' else [args.emulator]):
                    guest=d/(emulator+'-'+str(mem)+'M-'+str(cpus));guest.mkdir()
                    if emulator=='qemu':
                        cmd=[qemu,'-L',os.environ.get('GTOS_QEMU_DATA_DIR','/usr/share/qemu'),'-machine','pc','-accel','tcg','-cpu','pentium3',
                            '-rtc','base=2000-01-01T00:00:00,clock=vm','-m',str(mem),'-smp',str(cpus),'-cdrom',d/'native.iso',
                            '-boot','d','-nic','none','-display','none','-monitor','none','-serial','none',
                            '-debugcon','file:'+str(guest/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot']
                        run(cmd,guest/'host.log',60,33)
                    else:
                        env=dict(os.environ,TZ='UTC',LD_LIBRARY_PATH=str(args.bochs_root/'usr/lib/x86_64-linux-gnu'))
                        run([sys.executable,args.bochs_runner,'--root',args.bochs_root,'--iso',d/'native.iso',
                            '--output',guest,'--cpu','p4_willamette','--memory',mem,'--cpus',cpus,'--ips',10000000*cpus],
                            guest/'host.log',200,env=env)
                    evidence=observed((guest/'guest.log').read_text(),unsupported)
                    state['guests'].append(dict(optimization=opt,unsupported_rtc=bool(unsupported),emulator=emulator,
                        memory_mib=mem,cpus=cpus,iso_sha256=sha(d/'native.iso'),kernel_sha256=sha(d/'kernel.bin'),
                        guest_log=str(guest/'guest.log'),guest_log_sha256=sha(guest/'guest.log'),evidence=evidence))
                    print('PASS REALTIME '+str((opt,unsupported,emulator,mem,cpus)),flush=True);save()
    state['source_before_after_identical']=all(sha(p)==h for p,h in state['inputs'].items())
    assert state['source_before_after_identical']
    state['all_required_checks_pass']=True
except BaseException as error:
    state['failure']=str(error);(out/'exception.log').write_text(traceback.format_exc())
finally:
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    print(json.dumps({k:v for k,v in state.items() if k not in ('inputs','commands','guests')},indent=2))
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
