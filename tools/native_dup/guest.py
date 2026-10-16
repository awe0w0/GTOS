#!/usr/bin/env python3
"""Canonical-only real CPL3 DUP/ATA/owner cleanup and SSE2 context gate."""
import ctypes,hashlib,json,os,pathlib,re,subprocess,sys,traceback
HERE=pathlib.Path(__file__).resolve().parent;R=HERE.parent.parent;W=R.parent.parent
S=R/'tests/native_dup';DS=R
require_output=re.compile(r'native-dup-[0-9]{8}T[0-9]{6}Z-[0-9]+')
if len(sys.argv)!=2:raise RuntimeError('Use the reviewed run.py launcher')
OUT=pathlib.Path(sys.argv[1])
if OUT.parent!=W/'artifacts' or not require_output.fullmatch(OUT.name) or OUT.resolve()!=OUT:
    raise RuntimeError('Unexpected output path')
MANIFEST=HERE/'inputs.json'
MANIFEST_SHA='e8afc6739907fd1fd68b922b56a12c20b31d78020d7cab13c39a888e033cb17d'
T=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin';CC=T/'clang';LD=T/'ld.lld'
QROOT=W/'toolchains/gtos-qemu8-native-files-a';LIB=QROOT/'root/usr/lib/x86_64-linux-gnu'
LOADER=LIB/'ld-linux-x86-64.so.2';QEMU=QROOT/'root/usr/bin/qemu-system-i386'
INTEGER=W/'sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a'
state={'complete':False,'native_DUP_CPL3_guest_pass':False,'canonical_only':True,'C_directory_guest_pass':False,'browser_pass':False,
       'inputs':{},'aliases':{},'commands':[],'objects':{},'guests':[]}
authorized=False;exit_code=1;allowed={}
def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(path):
    value=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
    return value.hexdigest()
def bind(path,digest=None):
    logical=pathlib.Path(path);actual=logical.resolve(strict=True);value=sha(actual)
    require(digest is None or value==digest,'Pinned input changed: '+str(path))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)]==value,'Bound input changed: '+str(path))
    state['inputs'][str(actual)]=value;state['aliases'][str(logical)]=str(actual)
def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(argv,name,timeout=120,expected=0):
    argv=[str(v) for v in argv];log=OUT/(name+'.log')
    with log.open('xb') as stream:
        result=subprocess.run(argv,cwd=OUT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
                              stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    state['commands'].append({'argv':argv,'returncode':result.returncode,'log':str(log),'sha256':sha(log)})
    emit();require(result.returncode==expected,'Command failed: '+name)
    return log.read_text(errors='replace')
def dependencies(dep,obj):
    text=dep.read_text().replace(chr(92)+chr(10),' ')
    require(text.startswith(str(obj)+': ') and chr(92) not in text and '$' not in text,'Unexpected dependency format')
    paths=text.split(':',1)[1].split();require(0<len(paths)<500 and len(paths)==len(set(paths)),'Unexpected dependency count')
    found={}
    for path in paths:
        logical=pathlib.Path(path);actual=logical.resolve(strict=True)
        require(logical.is_absolute() and (S in actual.parents or DS in actual.parents or R in actual.parents),'Dependency escaped canonical sources')
        require(str(actual) in allowed,'Unqualified full MD input; stop before CXX: '+str(actual))
        bind(logical,allowed[str(actual)]);found[str(logical)]=str(actual)
    return found
try:
    require(sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(HERE/'run.py')
            and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
            and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    for path,digest in guard['executable_files'].items():bind(path,digest)
    bind(MANIFEST,MANIFEST_SHA);pins=json.loads(MANIFEST.read_text())
    require(pins['schema']==1 and pins['scope']=='canonical-native-dup','Reviewed source manifest required')
    for base,entries in ((R,pins['repository']),(W,pins['workspace'])):
        for relative,digest in entries.items():
            path=base/relative;actual=path.resolve(strict=True)
            require(not pathlib.Path(relative).is_absolute() and '..' not in pathlib.Path(relative).parts
                and base in actual.parents and actual==path,'Input path/alias escaped manifest root')
            require(str(actual) not in allowed,'Duplicate manifest input')
            allowed[str(actual)]=digest;bind(path,digest)
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),
                 'TMPDIR':str(OUT/'tmp'),'QEMU_MODULE_DIR':str(LIB/'qemu')}
    qprefix=[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu')]
    state['stage']='bind_qemu_runtime';emit()
    listing=run(qprefix+['--list',QEMU],'qemu-libraries')
    require('not found' not in listing,'Unresolved QEMU dependency')
    for line in listing.splitlines():
        words=line.split()
        if '=>' in words:
            path=pathlib.Path(words[words.index('=>')+1]);require(QROOT in path.resolve(strict=True).parents,'Mixed QEMU runtime');bind(path,allowed[str(path.resolve(strict=True))])
        elif words and words[0].startswith('/'):bind(words[0],allowed[str(pathlib.Path(words[0]).resolve(strict=True))])
    bind(LIB/'qemu/accel-tcg-i386.so',allowed[str((LIB/'qemu/accel-tcg-i386.so').resolve(strict=True))])
    for name,digest in {'bios-256k.bin':'1a9ea4f17bcfb27bda5728e2c21d9fa074cf7947b9b4910bb28213a735c96735',
                       'multiboot.bin':'13a3f9f119797b8776105f020f36572ce6a20fa2c60d3b8112818bfaefcdd348',
                       'multiboot_dma.bin':'e01667420ee78a318ea44627d2dd68058d3377d024e6eb7237700afe2e86507d'}.items():bind(QROOT/'firmware'/name,digest)
    bind(INTEGER,'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528')
    flags=['-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-nostdinc','-nostdinc++','-std=c++11',
           '-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize','-fno-pic','-fno-pie',
           '-fno-stack-protector','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit',
           '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections',
           '-fstack-usage','-Wall','-Wextra','-Werror','-Wno-write-strings','-I'+str(R/'include'),'-I'+str(R/'tests'),
           '-I'+str(R/'include/storage/littlefs-sdk'),'-I'+str(R/'include/storage/littlefs')]
    user_flags=[flag for flag in flags if flag not in ('-mgeneral-regs-only','-mno-sse','-msoft-float','-fno-tree-vectorize')]+[
        '-D__GTOS__=1','-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-femulated-tls']
    kernel_sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp',
        'src/hardwarecommunication/port.cpp','src/process/native_runtime.cpp','src/process/native_realtime.cpp',
        'src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp',
        'src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp',
        'src/process/native_files.cpp','src/storage/filestore.cpp','src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','src/storage/littlefs_port.cpp','src/drivers/ata.cpp']
    for opt in ('0','2'):
        level=OUT/('O'+opt);level.mkdir();state['stage']='compile_O'+opt;emit()
        sources=[('user',S/'user.cpp'),('kernel-test',S/'kernel.cpp'),
                 ('memory',R/'apps/native_clock_probe/memory.cc')]
        for name in kernel_sources:
            source=R/name
            sources.append((pathlib.Path(name).stem,source))
        for name,source in sources:
            obj=level/(name+'.o');pre=level/(name+'.pre.d');dep=level/(name+'.d')
            actual=str(source.resolve(strict=True));require(actual in allowed,'Selected source lacks reviewed SHA');bind(source,allowed[actual])
            # Canonical GDT intentionally reads/writes the packed descriptor as
            # uint8_t[this], so Clang cannot see field-name references. Keep all
            # other warnings/errors and every production source byte unchanged.
            own=(user_flags if name=='user' else flags)+(['-Wno-unused-private-field'] if name=='gdt' else [])
            if source.suffix=='.c':own=[flag for flag in flags if flag not in ('-x','c++','-std=c++11','-nostdinc++','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit')]+['-x','c','-std=c99','-DLFS_DEFINES=storage/littlefs_port.h']
            run([CC]+own+['-O'+opt,'-M','-MT',obj,'-MF',pre,source],'O'+opt+'-'+name+'-M')
            expected=dependencies(pre,obj)
            run([CC]+own+['-O'+opt,'-MD','-MF',dep,'-c',source,'-o',obj],'O'+opt+'-'+name+'-CXX')
            require(dependencies(dep,obj)==expected,'Full MD changed after CXX')
            state['objects'][str(obj)]={'sha256':sha(obj),'MD_sha256':sha(dep),'dependencies':expected};emit()
        assembly=[('user-start',R/'apps/native_clock_probe/start.s'),('loader',R/'tests/native_process_loader.s'),
                  ('fp-asm',R/'src/process/native_fp.s'),('stubs',R/'src/hardwarecommunication/interruptstubs.s')]
        for name,source in assembly:
            bind(source,allowed[str(source.resolve(strict=True))]);run([CC,'--target=i686-unknown-none-elf','-c',source,'-o',level/(name+'.o')],'O'+opt+'-'+name)
        bind(S/'user.ld',allowed[str(S/'user.ld')]);bind(R/'tests/native_process_smoke.ld',allowed[str(R/'tests/native_process_smoke.ld')])
        run([LD,'-m','elf_i386','--gc-sections','-T',S/'user.ld','-o',level/'user.elf',
             level/'user-start.o',level/'user.o',level/'memory.o',INTEGER],'O'+opt+'-user-link')
        objects=[level/(name+'.o') for name,_ in sources if name!='user']+[level/(name+'.o') for name,_ in assembly if name!='user-start']
        run([LD,'-m','elf_i386','--gc-sections','-T',R/'tests/native_process_smoke.ld','-Map='+str(level/'kernel.map'),
             '-o',level/'kernel.bin']+objects+[INTEGER],'O'+opt+'-kernel-link')
        for memory,cpus in (('32M','1'),('64M','4')):
            name='O'+opt+'-'+memory+'-smp'+cpus;log=OUT/(name+'-guest.log')
            disk=OUT/(name+'.raw')
            with disk.open('xb') as stream:
                for n in range(2064):stream.write(bytes([0xA5+n if n<8 else 0x5A+n-2056 if n>=2056 else 255])*512)
            state['stage']='guest_'+name;emit()
            run(qprefix+[QEMU,'-L',QROOT/'firmware','-bios',QROOT/'firmware/bios-256k.bin',
                '-machine','pc','-accel','tcg','-m',memory,'-smp',cpus,'-nodefaults',
                '-kernel',level/'kernel.bin','-initrd',level/'user.elf','-drive','file='+str(disk)+',format=raw,if=ide,index=0','-nic','none','-display','none',
                '-monitor','none','-serial','none','-debugcon','file:'+str(log),
                '-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],name,120,33)
            data=disk.read_bytes();require(len(data)==2064*512 and all(data[n*512:(n+1)*512]==bytes([0xA5+n])*512 for n in range(8))
                and all(data[n*512:(n+1)*512]==bytes([0x5A+n-2056])*512 for n in range(2056,2064)),'Dedicated disk canaries changed')
            require(log.stat().st_size<1024*1024,'Unbounded guest log')
            text=log.read_text();require('\nCANONICAL DUP GUEST PASS\n' in text and 'FAIL' not in text,'Guest failed')
            finals=[{k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]+)',line)} for line in text.splitlines() if line.startswith('DUP FINAL ')]
            reaps=[{k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]+)',line)} for line in text.splitlines() if line.startswith('DUP REAP ')]
            require(len(finals)==1,'Exactly one final summary required');final=finals[0]
            require(all(final[key]==value for key,value in {'calls':671,'success':567,'errors':104,'peak':64,
                'reclaims':9,'released':34,'failures':1,'initialized':32,'invalidated':32,'fp_failures':0}.items())
                and final['free']==final['expected'] and final['saves']>0 and final['restores']>0,'Exact actual CPL3 DUP/FP/reclaim totals missing')
            require(len(reaps)==8 and [row['leaked'] for row in reaps]==[4]*7+[5]
                and all(row['released']==row['leaked'] and row['free']==row['expected'] and row['error']==(0xFFFFFFFB if i==5 else 0)
                    for i,row in enumerate(reaps)),'Exact error-Reap/retained-peer physical baselines missing')
            state['guests'].append({'name':name,'log_sha256':sha(log),'reaps':reaps,'final':final,
                'kernel_sha256':sha(level/'kernel.bin'),'user_sha256':sha(level/'user.elf'),'disk_sha256':sha(disk)});emit()
            print('PASS '+name,flush=True)
    state['stage']='final_input_alias_checks';emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Input alias changed')
    state.update(stage='completed',complete=True,native_DUP_CPL3_guest_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,
        scope='Canonical DUP0x4740 only. Real NativeRuntime/NativeFiles/FileStore/littlefs/ATA, request boundaries, owner/global64 capacity, shared worker cursor, directory/temp aliases, normal/fault/error reclaim, exact peer/final physical baselines and32 SSE2 contexts per guest. O0/O2 x32M1/64M4 BSP-only. No private environment/CPU clock/LLVM providers; no C dirent/unistd wrappers, V8/browser/video/HTML5, or production image update.')
    emit();print(json.dumps({'native_DUP_CPL3_guest_pass':True,'canonical_only':True,'guests':len(state['guests'])}),flush=True);exit_code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(exit_code)
