#!/usr/bin/env python3
"""Offline scalar kernel and actual CPL3 event/wait/thread lifecycle guests."""
import ctypes,hashlib,json,os,pathlib,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-events-20261017-a';R=W/'sources/GTOS'
S=P/'probe-v2';C=P/'candidate-v2';OUT=P/'guest-02'
T=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin';CC=T/'clang';LD=T/'ld.lld'
QROOT=W/'toolchains/gtos-qemu8-native-files-a';LIB=QROOT/'root/usr/lib/x86_64-linux-gnu'
LOADER=LIB/'ld-linux-x86-64.so.2';QEMU=QROOT/'root/usr/bin/qemu-system-i386'
INTEGER=W/'sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a'
PINS_SHA='77793490dbad016ebab7643418294935de5e84332c73716b51855555df8428d9'
state={'complete':False,'native_events_CPL3_pass':False,'browser_pass':False,'inputs':{},'aliases':{},'objects':{},'commands':[],'guests':[],'unbound_MD_inputs':{}}
authorized=False;allowed={};exit_code=1
def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def bind(path,digest):
    logical=pathlib.Path(path);actual=logical.resolve(strict=True);key=str(actual)
    require(actual.is_file() and (W in actual.parents or actual==pathlib.Path('/usr/bin/python3.8')),'Input root escaped')
    require(key not in state['inputs'] or state['inputs'][key]==digest,'Conflicting input pin')
    if key not in state['inputs']:require(sha(actual)==digest,'Pinned input changed: '+key)
    state['inputs'][key]=digest;state['aliases'][str(logical)]=key
def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(argv,name,expected=0,timeout=180):
    argv=[str(a) for a in argv];log=OUT/(name+'.log')
    with log.open('xb') as stream:
        r=subprocess.run(argv,cwd=OUT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    require(log.stat().st_size<4*1024*1024,'Unbounded tool log')
    state['commands'].append({'argv':argv,'returncode':r.returncode,'log':str(log),'sha256':sha(log)});emit()
    require(r.returncode==expected,'Command failed: '+name);return log.read_text(errors='replace')
def deps(path,obj):
    value=path.read_text().replace(chr(92)+chr(10),' ')
    require(value.startswith(str(obj)+': ') and chr(92) not in value and '$' not in value,'Unexpected dependency syntax')
    names=value.split(':',1)[1].split();require(0<len(names)<500 and len(names)==len(set(names)),'Unexpected dependency count');found={};unknown=[]
    for name in names:
        logical=pathlib.Path(name);actual=logical.resolve(strict=True);key=str(actual)
        require(logical.is_absolute() and (R in actual.parents or S in actual.parents or C in actual.parents),'Dependency escaped sources')
        if key not in allowed:state['unbound_MD_inputs'][key]=sha(actual);unknown.append(key)
        else:bind(logical,allowed[key])
        found[str(logical)]=key
    emit();require(not unknown,'Unqualified full-M input; stop before CXX');return found
try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed no-site child required')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-guest-v2.py') and guard['landlock_applied'] and guard['network_disabled']
        and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    for path,digest in guard['executable_files'].items():bind(path,digest)
    prior=W/'artifacts/native-directory-20261017-a/runtime-guest-02/status.json'
    bind(prior,'cc2585a304e0ff62adf70bde8267bcd258191aab69844cbec311fbca7f7b51c8')
    old=json.loads(prior.read_text());require(old['complete'] and old['C_directory_guest_pass'] and old['ThreadChecker_guest_pass'] and old['bound_inputs_unchanged'] and old['aliases_unchanged'] and 'failure' not in old,'Qualified canonical scalar kernel required');allowed.update(old['inputs'])
    pins=P/'source-pins-02.json';bind(pins,PINS_SHA);own=json.loads(pins.read_text())
    require(own['canonical_dev']=='7391ed6817e09c29b71d29fbef22fe2e94153173' and len(own['files'])==10,'Exact authored source set')
    for path,digest in own['files'].items():
        require(C in pathlib.Path(path).parents or S in pathlib.Path(path).parents,'Authored pin escaped reviewed roots');allowed[path]=digest;bind(path,digest)
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp'),'QEMU_MODULE_DIR':str(LIB/'qemu')}
    qprefix=[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu')]
    listing=run(qprefix+['--list',QEMU],'qemu-libraries');require('not found' not in listing,'Unresolved QEMU dependency')
    for line in listing.splitlines():
        words=line.split()
        if '=>' in words:
            path=pathlib.Path(words[words.index('=>')+1]);require(QROOT in path.resolve(strict=True).parents,'Mixed QEMU runtime');bind(path,allowed[str(path.resolve(strict=True))])
        elif words and words[0].startswith('/'):bind(words[0],allowed[str(pathlib.Path(words[0]).resolve(strict=True))])
    path=LIB/'qemu/accel-tcg-i386.so';bind(path,allowed[str(path.resolve(strict=True))])
    for name,digest in {'bios-256k.bin':'1a9ea4f17bcfb27bda5728e2c21d9fa074cf7947b9b4910bb28213a735c96735','multiboot.bin':'13a3f9f119797b8776105f020f36572ce6a20fa2c60d3b8112818bfaefcdd348','multiboot_dma.bin':'e01667420ee78a318ea44627d2dd68058d3377d024e6eb7237700afe2e86507d'}.items():bind(QROOT/'firmware'/name,digest)
    bind(INTEGER,'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528')
    flags=['--target=i686-unknown-none-elf','-ffreestanding','-nostdinc','-nostdinc++','-std=c++11','-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize',
        '-fno-pic','-fno-pie','-fno-stack-protector','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-write-strings','-I'+str(C/'include'),'-I'+str(R/'include'),'-I'+str(S)]
    kernel_names=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
        'src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp',
        'src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp']
    def compile_one(source,obj,name,opt,extra=()):
        source=pathlib.Path(source);key=str(source.resolve(strict=True));require(key in allowed,'Unqualified source');bind(source,allowed[key])
        pre=pathlib.Path(str(obj)+'.pre.d');dep=pathlib.Path(str(obj)+'.d');ownflags=flags+list(extra)+['-O'+opt]
        run([CC]+ownflags+['-M','-MT',obj,'-MF',pre,source],name+'-M');expected=deps(pre,obj)
        run([CC]+ownflags+['-MD','-MF',dep,'-c',source,'-o',obj],name+'-CXX');require(deps(dep,obj)==expected,'Full MD changed after compilation')
        state['objects'][str(obj)]={'sha256':sha(obj),'MD_sha256':sha(dep),'dependencies':expected};emit()
    for opt in ('0','2'):
        level=OUT/('O'+opt);level.mkdir();state['stage']='compile_O'+opt;emit()
        memory=R/'apps/native_clock_probe/memory.cc';user=level/'user.o';user_memory=level/'user-memory.o'
        compile_one(S/'user.cpp',user,'O'+opt+'-user',opt);compile_one(memory,user_memory,'O'+opt+'-user-memory',opt)
        bind(S/'user.ld',own['files'][str(S/'user.ld')]);elf=level/'user.elf'
        run([LD,'-m','elf_i386','--gc-sections','-T',S/'user.ld','-Map='+str(level/'user.map'),'-o',elf,user,user_memory,INTEGER],'O'+opt+'-user-link')
        require(elf.read_bytes()[:7]==b'\x7fELF\x01\x01\x01','Real ELF32 user image')
        sources=[('kernel-test',S/'kernel.cpp'),('memory',memory),('native_runtime',C/'src/process/native_runtime.cpp'),('native_events',C/'src/process/native_events.cpp')]+[(pathlib.Path(name).stem,R/name) for name in kernel_names]
        for name,source in sources:compile_one(source,level/(name+'.o'),'O'+opt+'-'+name,opt,('-Wno-unused-private-field',) if name=='gdt' else ())
        assembly=[('loader',R/'tests/native_process_loader.s'),('fp-asm',R/'src/process/native_fp.s'),('stubs',R/'src/hardwarecommunication/interruptstubs.s')]
        for name,source in assembly:
            bind(source,allowed[str(source.resolve(strict=True))]);run([CC,'--target=i686-unknown-none-elf','-c',source,'-o',level/(name+'.o')],'O'+opt+'-'+name)
        script=R/'tests/native_process_smoke.ld';bind(script,allowed[str(script.resolve(strict=True))])
        run([LD,'-m','elf_i386','--gc-sections','-T',script,'-Map='+str(level/'kernel.map'),'-o',level/'kernel.bin']+[level/(name+'.o') for name,_ in sources+assembly]+[INTEGER],'O'+opt+'-kernel-link')
        for memory_size,cpus in (('32M','1'),('64M','4')):
            name='O'+opt+'-'+memory_size+'-smp'+cpus;log=OUT/(name+'-guest.log');state['stage']='guest_'+name;emit()
            run(qprefix+[QEMU,'-L',QROOT/'firmware','-bios',QROOT/'firmware/bios-256k.bin','-machine','pc','-accel','tcg','-m',memory_size,'-smp',cpus,'-nodefaults',
                '-kernel',level/'kernel.bin','-initrd',elf,'-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(log),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],name,33,120)
            require(log.stat().st_size<1024*1024,'Unbounded guest log');text=log.read_text()
            require('\nNATIVE EVENTS PASS\n' in text and 'FAIL' not in text and 'EVENT LIMITS objects=00000020 handles=00000040 overflow=00000001' in text and 'EVENT RESLEEP isolated=00000001' in text,'Actual native event guest failed')
            state['guests'].append({'name':name,'log_sha256':sha(log),'kernel_sha256':sha(level/'kernel.bin'),'user_sha256':sha(elf),'guest_summary':text});emit();print('PASS '+name,flush=True)
    state['stage']='final_input_alias_checks';emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Input alias changed')
    state.update(stage='completed',complete=True,native_events_CPL3_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,
        scope='Real native event ABI and kernel blocking/wakeup on BSP. O0/O2 x32M1/64M4, IA32 CPL3 requests, real threads, manual/auto reset and alias rights, wait-array snapshot after page release, close cancellation, real PIT rollover timeout, owner fault/group reap, surviving peer alias and exact final physical/handle/object recovery. No complete MessagePump/Mojo/Content/browser/video/HTML5 execution or production image update.')
    emit();print(json.dumps({'native_events_CPL3_pass':True,'guests':len(state['guests'])}),flush=True);exit_code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(exit_code)
