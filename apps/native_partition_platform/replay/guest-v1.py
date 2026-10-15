#!/usr/bin/env python3
"""Reuse the accepted kernel core and observe real V8 workloads only in GTOS."""
import ctypes
import hashlib
import json
import os
import pathlib
import shlex
import struct
import subprocess
import sys
import traceback

W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-partition-consumer-20261016-a'
LP=W/'artifacts/native-v8-launch-20261015-a'
HP=W/'artifacts/native-large-heap-20261015-a'
ADMISSION=W/'artifacts/native-v8-admission-20261015-a'
OUT=P/'guest-01'
REPO=W/'sources/GTOS'
BASE=W/'artifacts/native-standard-stream-guest-20261012-a/qemu-07'
PROOF=W/'artifacts/native-debug-shared-20261015-a'
PROVIDER=W/'artifacts/native-os-file-20261015-a/provider-03'
REUSE=W/'artifacts/native-output-20261013-a/kernel-reuse.json'
ELF=P/'link-01/native_embedder.elf'
RUNTIME=W/'toolchains/gtos-qemu8-native-files-a'
CLANG=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
NM=CLANG.parent/'llvm-nm'
LD=pathlib.Path('/usr/bin/x86_64-linux-gnu-ld.bfd')
OBJDUMP=pathlib.Path('/usr/bin/x86_64-linux-gnu-objdump')
PYTHON=pathlib.Path('/usr/bin/python3')
QEMU=RUNTIME/'root/usr/bin/qemu-system-i386'
LOADER=RUNTIME/'root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'
TOOLS={CLANG:'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',
 NM:'629e43f674d383d8c54d43f3254f80895a380c05a4afcdec27bb03dbb72b74bc',
 LD:'476b24d5cc1fef54f80412d06e7430b2dcf4e2b6f1525ee17e6eaaf16b28a00f',
 OBJDUMP:'afd9c7433c96cb0f37df99cab98fc860bbbe2bed17f37df66254a8778adfc61d',
 PYTHON:'298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06',
 QEMU:'28fa14f1c45fca7422e3ec5737768b33b705de2f31014a76e658d4263464a6a5',
 LOADER:'c20a2dc8917c755f02b94049356320fe1f62ac7d9f8994731f807d9df39302da'}
PINS={PROOF/'full-link-58/status.json':'294c992c20a93eb465d1c8a878b4ccd1b14b68936829a76c9493a253453fcbf1',
 PROOF/'guests-02/status.json':'a363bbdee9543b5612db48fbf965fd848eab282a7e179214c0938d82fba48c0c',
 PROVIDER/'status.json':'de0fa8442e1831fcc9d471d5f0c580f66e06ba86b6697ecdf112efa57f32f893',
 BASE/'status.json':'4f3c1334c60b8eaa9512f6d34e2746827b52d132141a8374c166c9ea576b222f',
 REUSE:'fc0519f9fb4426ca8e8a69969b5dbdcdb80c8264af26c373582f435391e9db60',
 LP/'guest-04/status.json':'5231d19d2b8b756b22a18430e5d8bf88524351a861a2937e737e19ce4bfba5f1',
 P/'link-01/status.json':'188e170b3653569f2609d38569af1d9e2311747b5e5106d8ff42c88bab47e120',
 P/'kernel-v1.cpp':'614ddedb687074549ccf587101f3a1899b1b812362966b4e46ef8cd3033d9c98',
 ELF:'599dda92de566d035b042f485d0e218a9497502ec3cd7071a0228cc3faccc7b6',
 REPO/'tools/audit-kernel-instructions.py':'fa5fd0e5903f92e4587f42ee3dd000d75291ee76922f29ad59c5cf007caeddfa'}
FLAGS=['-x','c++','--target=i686-unknown-none-elf','-std=c++11','-ffreestanding','-nostdlib','-nostdinc',
 '-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-pie','-fno-pic',
 '-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables',
 '-ffunction-sections','-fdata-sections','-fstack-usage','-I'+str(LP/'patch-v2/include'),'-I'+str(LP/'patch/include'),'-Iinclude',
 '-Iinclude/storage/littlefs-sdk','-Iinclude/storage/littlefs','-I'+str(REPO),'-I'+str(OUT),
 '-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix','-Wall','-Wextra','-Werror','-O2',
 '-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-Wno-write-strings',
 '-fcrash-diagnostics-dir='+str(OUT/'tmp')]
state={'all_required_checks_pass':False,'native_admission_run':False,'native_isolate_pass':False,
 'browser_guest_pass':False,'production_image_changed':False,'inputs':{},'input_symlinks':{},'commands':[],'objects':{}}
known={};authorized=False

def require(value,message):
    if not value: raise RuntimeError(message)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()

def bind(path,digest=None):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents or pathlib.Path('/usr') in actual.parents,'Input escaped reviewed roots')
    # Explicitly bound current/prior-admission inputs precede older seed maps.
    digest=state['inputs'].get(str(actual),known.get(str(actual))) if digest is None else digest
    require(digest is not None and sha(actual)==digest,'Unbound/changed input: '+str(actual))
    previous=state['inputs'].get(str(actual));require(previous is None or previous==digest,'Hash conflict')
    state['inputs'][str(actual)]=digest
    if logical!=actual: state['input_symlinks'][str(logical)]=str(actual)

def emit(): (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def run(name,argv,expected=0):
    argv=[str(value) for value in argv]
    require(pathlib.Path(argv[0]) in TOOLS and sha(argv[0])==TOOLS[pathlib.Path(argv[0])],'Unreviewed executable')
    with (OUT/(name+'.log')).open('xb') as stream:
        result=subprocess.run(argv,cwd=REPO,env=environment,shell=False,close_fds=True,
            stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=600 if name=='guest' else 300)
    state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode});emit()
    require(result.returncode==expected,'Failed '+name+'; inspect retained output');print('PASS '+name,flush=True)

def deps(path):
    text=path.read_text().replace('\\\n',' ');require(text.count(':')==1,'Unsupported dependency syntax')
    return {pathlib.Path(os.path.normpath(str(REPO/pathlib.Path(item)))) for item in shlex.split(text.split(':',1)[1])}

def row(line,prefix):
    require(line.startswith(prefix),'Wrong evidence prefix');fields={}
    for item in line[len(prefix):].split():
        name,value=item.split('=');require(name not in fields,'Duplicate evidence field');fields[name]=int(value,16)
    return fields

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-guest-v1.py')
        and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
        and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    for path,digest in PINS.items(): bind(path,digest)
    for path,digest in TOOLS.items(): bind(path,digest)
    full=json.loads((P/'link-01/status.json').read_text())
    guest=json.loads((LP/'guest-04/status.json').read_text())
    base=json.loads((BASE/'status.json').read_text());provider=json.loads((PROVIDER/'status.json').read_text())
    require(full['actual_full_link_pass'] and full['bound_inputs_unchanged'] and full['old_cache_metadata_unchanged']
        and 'failure' not in full,'Real full ELF not qualified')
    require(guest['all_required_checks_pass'] and guest['native_isolate_pass'] and guest['bound_inputs_unchanged']
        and guest['full_M_MD'] and guest['kernel_isa_audit_pass'] and 'failure' not in guest,'Accepted native kernel core required')
    require(base['all_required_checks_pass'] and provider['all_compile_pass'] and provider['bound_inputs_unchanged']
        and 'failure' not in base and 'failure' not in provider,'Cached kernel not qualified')
    for proof in (base,provider,guest): known.update(proof['inputs'])
    for item in provider['canonical_kernel_header_equivalences']: known[item['canonical']]=item['sha256']
    for path,digest in guest['inputs'].items(): bind(path,digest)
    for path,digest in base['inputs'].items():
        if path.startswith(('/usr/lib/',str(RUNTIME)+'/')): bind(path,digest)
    for name in ('home','tmp'): (OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp'),
        'QEMU_MODULE_DIR':str(RUNTIME/'root/usr/lib/x86_64-linux-gnu/qemu')}
    run('entry-symbols',[NM,'--defined-only',ELF]);symbols={}
    for line in (OUT/'entry-symbols.log').read_text().splitlines():
        words=line.split()
        if len(words)==3 and words[2] in ('native_v8_integration_stage','native_v8_integration_result','_ZN12_GLOBAL__N_116largeAllocationsE',
                                         'native_v8_workload_mask','native_v8_gc_callbacks','native_pa_mask',
                                         'native_pa_expected_write_bytes','native_pa_process_id','native_pa_thread_id'):
            require(words[2] not in symbols and words[1] in ('D','B','d','b'),'Invalid entry symbol');symbols[words[2]]=int(words[0],16)
    image=ELF.read_bytes();require(image[:7]==b'\x7fELF\x01\x01\x01' and len(image)>=52,'ELF32 required')
    phoff=struct.unpack_from('<I',image,28)[0];phsize,phcount=struct.unpack_from('<HH',image,42)
    require(phsize==32 and 0<phcount<=8 and phoff+phcount*32<=len(image),'Bounded PHDR required');ranges=[]
    for index in range(phcount):
        kind,offset,address,physical,filesize,memsize,flags,align=struct.unpack_from('<8I',image,phoff+index*32)
        if kind==1 and flags==6: ranges.append((address,address+memsize))
    require(len(symbols)==9 and len(set(symbols.values()))==9 and all(value%4==0 and any(begin<=value and value+(320 if name=='_ZN12_GLOBAL__N_116largeAllocationsE' else 4)<=end for begin,end in ranges) for name,value in symbols.items()),'Entry/heap/workload/platform state outside writable segment')
    header=OUT/'entry-symbols.h';header.write_text('#define ENTRY_STAGE_VA '+hex(symbols['native_v8_integration_stage'])+'U\n#define ENTRY_RESULT_VA '+hex(symbols['native_v8_integration_result'])+'U\n')
    with header.open('a') as stream:
        stream.write('#define LARGE_ALLOCS_VA '+hex(symbols['_ZN12_GLOBAL__N_116largeAllocationsE'])+'U\n')
        stream.write('#define WORKLOAD_MASK_VA '+hex(symbols['native_v8_workload_mask'])+'U\n')
        stream.write('#define GC_CALLBACKS_VA '+hex(symbols['native_v8_gc_callbacks'])+'U\n')
        for macro,name in (('PA_MASK_VA','native_pa_mask'),('PA_BYTES_VA','native_pa_expected_write_bytes'),
                           ('PA_PROCESS_VA','native_pa_process_id'),('PA_THREAD_VA','native_pa_thread_id')):
            stream.write('#define '+macro+' '+hex(symbols[name])+'U\n')
    known[str(header)]=sha(header);bind(header);state['entry_symbols']=symbols
    compile_sources={'kernel':P/'kernel-v1.cpp'}
    for name,source in compile_sources.items():
        pre=OUT/(name+'.pre.d');post=OUT/(name+'.d');obj=OUT/(name+'.o')
        run(name+'-dependencies',[CLANG,*FLAGS,'-M','-MF',pre,'-MT',name,source]);before=deps(pre)
        for path in before: bind(path)
        run(name+'-compile',[CLANG,*FLAGS,'-MD','-MF',post,'-c',source,'-o',obj]);require(deps(post)==before,'M/MD closure differs')
        state['objects'][name]={'path':str(obj),'sha256':sha(obj),'source':str(source),'full_dependencies':len(before)};emit()
    reuse=json.loads(REUSE.read_text());require(len(reuse)==25,'Expected25 retained objects');replacements={}
    for item in provider['objects']:
        if item['source']=='patch/filestore-v1.cpp': replacements['filestore.o']=item
        if item['source']=='patch/native_files-v1.cpp': replacements['native_files.o']=item
    require(set(replacements)=={'filestore.o','native_files.o'},'Coherent file pair missing')
    objects=[]
    retained={}
    for name,digest in reuse.items():
        require(pathlib.PurePosixPath(name).name==name and name.endswith('.o'),'Invalid object identity')
        if name[:-2] in ('elf32','process_address_space','native_runtime','syscalls','interrupts'):
            item=guest['objects'][name[:-2]];bind(item['path'],item['sha256']);objects.append(item['path'])
        elif name in replacements:
            item=replacements[name];bind(item['path'],item['sha256']);objects.append(item['path'])
        else:
            require(base['objects']['O2-kernel-'+name]==digest,'Retained object changed')
            path=BASE/'O2'/name;bind(path,digest);objects.append(path)
    require(len(objects)==25 and len(set(objects))==25,'Object closure mismatch');bind(REPO/'tests/native_process_smoke.ld')
    previous=[item for item in guest['commands'] if item['name']=='link']
    require(len(previous)==1 and previous[0]['returncode']==0 and [str(path) for path in objects]==previous[0]['argv'][9:-1],
        'Static kernel core must match accepted ordered25 objects')
    for path in objects:
        info=pathlib.Path(path).stat();retained[str(path)]=(info.st_size,info.st_mtime_ns)
    kernel=OUT/'kernel.bin';mapfile=OUT/'kernel.map'
    run('link',[LD,'-melf_i386','--gc-sections','-T',REPO/'tests/native_process_smoke.ld','-Map',mapfile,'-o',kernel,*objects,OUT/'kernel.o'])
    run('isa',[PYTHON,'-I','-B','-S',REPO/'tools/audit-kernel-instructions.py','--objdump',OBJDUMP,'--map',mapfile,'--source-root',REPO,kernel])
    state['guests']=[]
    for memory,cpus in (('32M','4'),('64M','1')):
        label=memory+'-smp'+cpus;log=OUT/(label+'-guest.log')
        run(label,[LOADER,'--library-path',str(RUNTIME/'root/usr/lib/x86_64-linux-gnu')+':'+str(RUNTIME/'root/lib/x86_64-linux-gnu'),
        QEMU,'-L',RUNTIME/'firmware','-machine','pc','-accel','tcg','-cpu','max','-m',memory,'-smp',cpus,
        '-rtc','base=2026-10-15T04:05:59,clock=vm','-kernel',kernel,'-initrd',ELF,
        '-icount','shift=0,align=off,sleep=off','-nic','none','-display','none','-monitor','none','-serial','none',
        '-debugcon','file:'+str(log),'-global','isa-debugcon.iobase=0xe9',
        '-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],33)
        lines=log.read_text().splitlines()
        require(lines.count('GTOS REAL V8 LARGE HEAP RUN PASS')==1 and not any(line.startswith('FAILED ') or line=='GTOS REAL V8 LARGE HEAP RUN FAIL' for line in lines),'Actual lifecycle failed')
        executions=[row(line,'V8 EXECUTION ') for line in lines if line.startswith('V8 EXECUTION ')]
        reaps=[row(line,'V8 REAP ') for line in lines if line.startswith('V8 REAP ')]
        initial=[row(line,'V8 INITIAL ') for line in lines if line.startswith('V8 INITIAL ')]
        totals=[row(line,'V8 LARGE TOTAL ') for line in lines if line.startswith('V8 LARGE TOTAL ')]
        workloads=[row(line,'V8 WORKLOAD ') for line in lines if line.startswith('V8 WORKLOAD ')]
        platform=[row(line,'PA PLATFORM ') for line in lines if line.startswith('PA PLATFORM ')]
        exact=''.join(chr(65+index%26) for index in range(255))
        crossing=''.join(chr(97+index%26) for index in range(257))
        require(lines.count(exact)==1 and lines.count(crossing)==1
                and 'GTOS_PA_SUPPRESSED_MUST_NOT_APPEAR' not in log.read_text(),
                'Exact/cross-boundary diagnostics or suppression failed')
        blocks=[row(line,'V8 LARGE BLOCK ') for line in lines if line.startswith('V8 LARGE BLOCK ')]
        require(len(executions)==len(reaps)==len(initial)==len(totals)==1 and reaps[0]['free']==reaps[0]['baseline']
        and len(blocks)==totals[0]['blocks'] and sum(item['pages'] for item in blocks)==totals[0]['pages']
        and executions[0]['stage']==4 and executions[0]['result']==0 and executions[0]['exit']==0
        and executions[0]['vector']==0 and executions[0]['peer']>0 and len(workloads)==1
        and workloads[0]['mask']==127 and workloads[0]['gc']>0 and totals[0]['blocks']==0
        and len(platform)==1 and platform[0]['mask']==63 and platform[0]['process']>0
        and platform[0]['process']==platform[0]['thread'] and platform[0]['expected']==514
        and platform[0]['written']==514,'Actual platform/workloads/GC/exit/reclaim success missing')
        state['guests'].append({'configuration':label,'initial':initial[0],'execution':executions[0],
            'blocks':blocks,'workload':workloads[0],'platform':platform[0],'reap':reaps[0],'log_sha256':sha(log)});emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Inputs changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['input_symlinks'].items()),'Input alias changed')
    require(all((pathlib.Path(path).stat().st_size,pathlib.Path(path).stat().st_mtime_ns)==info for path,info in retained.items()),'Accepted kernel cache changed')
    state.update(all_required_checks_pass=True,native_admission_run=True,native_isolate_pass=True,
        bound_inputs_unchanged=True,input_symlinks_unchanged=True,full_M_MD=True,kernel_isa_audit_pass=True,
        actual_full_elf_sha256=sha(ELF),large_heap_semantics_qualified=False,
        candidate_runtime_body=True,workload_pass=True,platform_consumer_pass=True,reused_kernel_core_objects=25,
        diagnostics_exact_bytes_and_suppression=True,fatal_process_cleanup_qualified=False,
        reused_kernel_cache_metadata_unchanged=True,browser_guest_pass=False,diagnostic_only=False);emit()
except BaseException as error:
    if authorized: state['failure']=str(error);state['traceback']=traceback.format_exc();emit()
    traceback.print_exc();sys.exit(1)
