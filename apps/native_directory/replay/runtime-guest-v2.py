#!/usr/bin/env python3
"""Canonical scalar kernel plus real native C directory/ThreadChecker consumer."""
import ctypes,hashlib,json,os,pathlib,re,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-directory-20261017-a';R=W/'sources/GTOS'
S=P/'runtime-probe-05';U=P/'runtime-user-04';OUT=P/'runtime-guest-02'
T=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin';CC=T/'clang';LD=T/'ld.lld'
QROOT=W/'toolchains/gtos-qemu8-native-files-a';LIB=QROOT/'root/usr/lib/x86_64-linux-gnu'
LOADER=LIB/'ld-linux-x86-64.so.2';QEMU=QROOT/'root/usr/bin/qemu-system-i386'
INTEGER=W/'sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a'
state={'complete':False,'C_directory_guest_pass':False,'ThreadChecker_guest_pass':False,'browser_pass':False,
       'inputs':{},'aliases':{},'commands':[],'objects':{},'guests':[],'unbound_MD_inputs':{}}
authorized=False;exit_code=1;allowed={}
def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def bind(path,digest):
    logical=pathlib.Path(path);actual=logical.resolve(strict=True);key=str(actual)
    require(actual.is_file() and (W in actual.parents or actual==pathlib.Path('/usr/bin/python3.8')),'Input escaped reviewed roots')
    require(key not in state['inputs'] or state['inputs'][key]==digest,'Conflicting input')
    if key not in state['inputs']:require(sha(actual)==digest,'Pinned input changed: '+key)
    state['inputs'][key]=digest;state['aliases'][str(logical)]=key
def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(argv,name,timeout=180,expected=0):
    argv=[str(v) for v in argv];log=OUT/(name+'.log')
    with log.open('xb') as stream:
        result=subprocess.run(argv,cwd=OUT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    require(log.stat().st_size<8*1024*1024,'Unbounded tool log')
    state['commands'].append({'argv':argv,'returncode':result.returncode,'log':str(log),'sha256':sha(log)})
    emit();require(result.returncode==expected,'Command failed: '+name);return log.read_text(errors='replace')
def deps(dep,obj):
    text=dep.read_text().replace(chr(92)+chr(10),' ')
    require(text.startswith(str(obj)+': ') and chr(92) not in text and '$' not in text,'Unexpected dependency syntax')
    names=text.split(':',1)[1].split();require(0<len(names)<500 and len(names)==len(set(names)),'Unexpected dependency count')
    found={};unknown=[]
    for name in names:
        path=pathlib.Path(name);actual=path.resolve(strict=True);key=str(actual)
        require(path.is_absolute() and (R in actual.parents or S in actual.parents),'Dependency escaped canonical/test sources')
        if key not in allowed:state['unbound_MD_inputs'][key]=sha(actual);unknown.append(key)
        else:bind(path,allowed[key])
        found[str(path)]=key
    emit();require(not unknown,'Unqualified full MD input; stop before CXX');return found
try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-runtime-guest-v2.py') and guard['landlock_applied']
        and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    for path,digest in guard['executable_files'].items():bind(path,digest)
    prior=W/'artifacts/native-dup-20261016T213527Z-569/status.json';bind(prior,'230e5df0382cae1cc61177440cf8a023b44f9822f02305aabf3fef3a5de0663f')
    value=json.loads(prior.read_text());require(value['complete'] and value['native_DUP_CPL3_guest_pass'] and value['bound_inputs_unchanged'] and value['aliases_unchanged'] and 'failure' not in value,'Canonical DUP gate required');allowed.update(value['inputs'])
    bind(P/'runtime-kernel-pins-04.json','f73681d0cbef5af3e24c588778c86fd41bf4b0b5d599f9d7a4bf729483b25efb')
    pins=json.loads((P/'runtime-kernel-pins-04.json').read_text());require(len(pins['files'])==2,'Exact kernel fixture pins')
    for path,digest in pins['files'].items():require(str(path).startswith(str(S)+'/'),'Unexpected test input');allowed[path]=digest;bind(path,digest)
    bind(U/'status.json','626332b78b7545d2eba4fb7e16a7526039525c841971c5b806b82348263eee39');user=json.loads((U/'status.json').read_text())
    require(user['complete'] and user['consumer_link_pass'] and user['C_headers_pass'] and user['bound_inputs_unchanged'] and user['aliases_unchanged'] and user['MD_aliases_unchanged'] and 'failure' not in user,'Real primitive-C runtime link qualification required')
    for opt in ('0','2'):
        item=user['ELFs']['O'+opt];path=pathlib.Path(item['path']);require(path==U/('user-O'+opt+'.elf'),'Fixed native executable');bind(path,item['sha256'])
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
    for name,digest in {'bios-256k.bin':'1a9ea4f17bcfb27bda5728e2c21d9fa074cf7947b9b4910bb28213a735c96735',
        'multiboot.bin':'13a3f9f119797b8776105f020f36572ce6a20fa2c60d3b8112818bfaefcdd348','multiboot_dma.bin':'e01667420ee78a318ea44627d2dd68058d3377d024e6eb7237700afe2e86507d'}.items():bind(QROOT/'firmware'/name,digest)
    bind(INTEGER,'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528')
    flags=['-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-nostdinc','-nostdinc++','-std=c++11','-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize',
        '-fno-pic','-fno-pie','-fno-stack-protector','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-write-strings','-I'+str(R/'include'),'-I'+str(R/'tests'),'-I'+str(S),
        '-I'+str(R/'include/storage/littlefs-sdk'),'-I'+str(R/'include/storage/littlefs')]
    kernel_sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
        'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp',
        'src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp','src/process/native_files.cpp',
        'src/storage/filestore.cpp','src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','src/storage/littlefs_port.cpp','src/drivers/ata.cpp']
    for opt in ('0','2'):
        level=OUT/('O'+opt);level.mkdir();state['stage']='compile_O'+opt;emit()
        sources=[('kernel-test',S/'kernel.cpp'),('memory',R/'apps/native_clock_probe/memory.cc')]+[(pathlib.Path(name).stem,R/name) for name in kernel_sources]
        for name,source in sources:
            obj=level/(name+'.o');pre=level/(name+'.pre.d');dep=level/(name+'.d');key=str(source.resolve(strict=True));require(key in allowed,'Source lacks qualification');bind(source,allowed[key])
            own=flags+(['-Wno-unused-private-field'] if name=='gdt' else [])
            if source.suffix=='.c':own=[flag for flag in flags if flag not in ('-x','c++','-std=c++11','-nostdinc++','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit')]+['-x','c','-std=c99','-DLFS_DEFINES=storage/littlefs_port.h']
            run([CC]+own+['-O'+opt,'-M','-MT',obj,'-MF',pre,source],'O'+opt+'-'+name+'-M');expected=deps(pre,obj)
            run([CC]+own+['-O'+opt,'-MD','-MF',dep,'-c',source,'-o',obj],'O'+opt+'-'+name+'-CXX');require(deps(dep,obj)==expected,'Full MD changed after CXX')
            state['objects'][str(obj)]={'sha256':sha(obj),'MD_sha256':sha(dep),'dependencies':expected};emit()
        assembly=[('loader',R/'tests/native_process_loader.s'),('fp-asm',R/'src/process/native_fp.s'),('stubs',R/'src/hardwarecommunication/interruptstubs.s')]
        for name,source in assembly:
            bind(source,allowed[str(source.resolve(strict=True))]);run([CC,'--target=i686-unknown-none-elf','-c',source,'-o',level/(name+'.o')],'O'+opt+'-'+name)
        script=R/'tests/native_process_smoke.ld';bind(script,allowed[str(script.resolve(strict=True))])
        objects=[level/(name+'.o') for name,_ in sources+assembly]
        run([LD,'-m','elf_i386','--gc-sections','-T',script,'-Map='+str(level/'kernel.map'),'-o',level/'kernel.bin']+objects+[INTEGER],'O'+opt+'-kernel-link')
        for memory,cpus in (('32M','1'),('64M','4')):
            name='O'+opt+'-'+memory+'-smp'+cpus;log=OUT/(name+'-guest.log');disk=OUT/(name+'.raw')
            with disk.open('xb') as stream:
                for n in range(2064):stream.write(bytes([0xA5+n if n<8 else 0x5A+n-2056 if n>=2056 else 255])*512)
            state['stage']='guest_'+name;emit()
            run(qprefix+[QEMU,'-L',QROOT/'firmware','-bios',QROOT/'firmware/bios-256k.bin','-machine','pc','-accel','tcg','-m',memory,'-smp',cpus,'-nodefaults',
                '-kernel',level/'kernel.bin','-initrd',U/('user-O'+opt+'.elf'),'-drive','file='+str(disk)+',format=raw,if=ide,index=0','-nic','none','-display','none',
                '-monitor','none','-serial','none','-debugcon','file:'+str(log),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],name,180,33)
            data=disk.read_bytes();require(len(data)==2064*512 and all(data[n*512:(n+1)*512]==bytes([0xA5+n])*512 for n in range(8))
                and all(data[n*512:(n+1)*512]==bytes([0x5A+n-2056])*512 for n in range(2056,2064)),'Dedicated disk canaries changed')
            require(log.stat().st_size<1024*1024,'Unbounded guest log');text=log.read_text();require('\nNATIVE DIRECTORY RUNTIME PASS\n' in text and 'FAIL' not in text,'Guest failed')
            rows=lambda prefix:[{k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]+)',line)} for line in text.splitlines() if line.startswith(prefix)]
            final=rows('DIRECTORY FINAL ');stops=rows('DIRECTORY STOP ');reaps=rows('DIRECTORY REAP ');baselines=rows('DIRECTORY BASELINE ');counts=rows('DIRECTORY COUNTS ')
            require(len(final)==1 and len(stops)==4 and len(reaps)==4 and len(baselines)==1 and len(counts)==4,'Exact runtime summaries required');f=final[0];baseline=baselines[0]
            require(baseline['initial']>baseline['admitted']>baseline['ready'] and f['free']==baseline['initial'],'Explicit peer startup allocation and final full recovery required')
            require(all(row['reaped']==(3 if i==3 else 1) and row['open']==1 and row['free']==row['expected']==baseline['ready'] for i,row in enumerate(counts)),'Exact stable-peer resource counters required')
            require(all(f[k]==v for k,v in {'calls':292,'success':272,'errors':20,'close_errors':4,'heap_opens':0,'released':21,'fp_init':16,'fp_invalid':16,'fp_failures':0}.items()) and f['free']==f['expected'],'Actual runtime totals missing')
            require(all(row['released']==5 and row['free']==row['expected'] and row['error']==(0xFFFFFFFB if i==1 else 0) for i,row in enumerate(reaps)),'Exact owner/physical/error recovery missing')
            require([row['mode'] for row in stops]==[0,0,0,2] and all(not row['line'] and row['checks']>100 for row in stops) and [row['tls_ctor'] for row in stops]==[4,4,4,3] and [row['tls_dtor'] for row in stops]==[4,4,4,0],'Real TLS normal/fault execution missing')
            state['guests'].append({'name':name,'log_sha256':sha(log),'stops':stops,'reaps':reaps,'baseline':baseline,'counts':counts,'final':f,'kernel_sha256':sha(level/'kernel.bin'),'user_sha256':sha(U/('user-O'+opt+'.elf')),'disk_sha256':sha(disk)});emit();print('PASS '+name,flush=True)
    state['stage']='final_input_alias_checks';emit();require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Input alias changed')
    state.update(stage='completed',complete=True,C_directory_guest_pass=True,ThreadChecker_guest_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,
        scope='Canonical kernel; actual GN-owned directory/ThreadChecker and133 retained accepted primitive-C RTL inputs. Real malloc/FILE/errno/TLS/native threads/ATA, independent streams, descriptor sharing/capacity/errors, allocation failure, final-close I/O failure, normal/fault owner cleanup and exact physical baselines. Four O0/O2 x32M1/64M4 guests; BSP-only. No Content/browser/video/HTML5 execution, production image update or new cold certification of retained RTL.')
    emit();print(json.dumps({'C_directory_guest_pass':True,'ThreadChecker_guest_pass':True,'guests':len(state['guests'])}),flush=True);exit_code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(exit_code)
