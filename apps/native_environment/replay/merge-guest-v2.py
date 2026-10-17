#!/usr/bin/env python3
"""Independent environment/CPU and event regression on the same merged ABI."""
import ctypes,hashlib,json,os,pathlib,re,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';R=W/'sources/GTOS';C=P/'kernel-candidate-v1';OUT=P/'merge-guest-02';E=W/'artifacts/native-events-20261017-a';G=W/'artifacts/native-getenv-20261017-a';B=W/'artifacts/native-perfetto-time-20261016-a'
T=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin';CC=T/'clang';LD=pathlib.Path('/usr/bin/x86_64-linux-gnu-ld.bfd');OBJDUMP=pathlib.Path('/usr/bin/x86_64-linux-gnu-objdump');PYTHON=pathlib.Path('/usr/bin/python3')
QROOT=W/'toolchains/gtos-qemu8-native-files-a';LIB=QROOT/'root/usr/lib/x86_64-linux-gnu';LOADER=LIB/'ld-linux-x86-64.so.2';QEMU=QROOT/'root/usr/bin/qemu-system-i386';INTEGER=W/'sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a'
TOOLS={CC:'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',LD:'476b24d5cc1fef54f80412d06e7430b2dcf4e2b6f1525ee17e6eaaf16b28a00f',OBJDUMP:'afd9c7433c96cb0f37df99cab98fc860bbbe2bed17f37df66254a8778adfc61d',PYTHON:'298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06',QEMU:'28fa14f1c45fca7422e3ec5737768b33b705de2f31014a76e658d4263464a6a5',LOADER:'c20a2dc8917c755f02b94049356320fe1f62ac7d9f8994731f807d9df39302da'}
state={'complete':False,'merged_environment_CPU_events_pass':False,'public_source_changed':False,'browser_pass':False,'inputs':{},'aliases':{},'objects':{},'kernels':{},'guests':[],'commands':[]};known={};parents={};retained={};authorized=False;code=1

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def resolve(path,cache):
    path=pathlib.Path(os.path.normpath(str(path)));key=str(path.parent)
    if key not in cache:cache[key]=path.parent.resolve(strict=True)
    actual=cache[key]/path.name
    if actual.is_symlink():return actual.resolve(strict=True)
    actual.stat();return actual

def bind(path,digest=None):
    logical=pathlib.Path(path);actual=resolve(logical,parents);key=str(actual);digest=known.get(key) if digest is None else digest
    require((W in actual.parents or pathlib.Path('/usr') in actual.parents) and actual.is_file() and digest is not None,'Unbound/escaped input: '+key)
    if key in state['inputs']:require(state['inputs'][key]==digest,'Digest conflict')
    else:
        require(sha(actual)==digest,'Input changed: '+key);state['inputs'][key]=digest
        if actual.suffix in ('.o','.a','.elf','.bin') and OUT not in actual.parents:
            info=actual.stat();retained[key]=(info.st_size,info.st_mtime_ns)
    state['aliases'][str(logical)]=key

def receipt(path,digest):
    bind(path,digest);record=json.loads(path.read_text());require('failure' not in record,'Failed prerequisite');return record

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def run(name,argv,expected=0,timeout=180):
    argv=[str(value) for value in argv];require(pathlib.Path(argv[0]) in TOOLS and sha(argv[0])==TOOLS[pathlib.Path(argv[0])],'Exact executable required');log=OUT/(name+'.log')
    with log.open('xb') as stream:result=subprocess.run(argv,cwd=R,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode,'log_sha256':sha(log)});emit();require(result.returncode==expected and log.stat().st_size<8*1024*1024,'Failed '+name+': '+str(result.returncode)+'; retained output');print('PASS '+name,flush=True);return log

def dependencies(path,cwd=R):
    text=path.read_text().replace(chr(92)+chr(10),' ');require(text.count(':')==1 and chr(92) not in text and '$' not in text,'Plain full MD syntax');names=shlex.split(text.split(':',1)[1]);require(0<len(names)<10000,'Bounded dependency count')
    return {str(cwd/name):str(resolve(cwd/name,parents)) for name in names}

def rows(lines,prefix):
    return [{key:int(value,16) for key,value in re.findall(r'(\w+)=([0-9A-F]+)',line)} for line in lines if line.startswith(prefix)]

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-merge-guest-v2.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    root=receipt(P/'root-guest-03/status.json','05df29b1b27b72401e959b8a680cd6418fe9fcc647763c60bf9193dcdcca3549');require(all(root[key] for key in ('complete','native_actual_expanded_root_pass','merged_candidate_Task_Runtime_ABI','full_M_MD','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged','outputs_unchanged')) and root['actual_original_root_launches']==4,'Terminal same merged ABI root proof')
    env=receipt(G/'guest-01/status.json','d8e0c0be4fc2d2177f771938279198b59c4f93a34b7296c03c2cff1b26d5f150');require(all(env[key] for key in ('complete','environment_snapshot_guest_pass','getenv_SSE2_guest_pass','bound_inputs_unchanged','aliases_unchanged')),'Original real SDK environment/CPU fixture proof')
    event=receipt(E/'guest-02/status.json','ca8c5b0444299a53bb9b7c5a811e2760fd63d0458dcd773c14a90c522324cd0d');require(all(event[key] for key in ('complete','native_events_CPL3_pass','bound_inputs_unchanged','aliases_unchanged')),'Original real event fixture proof')
    known.update(root['inputs'])
    for old in (env,event):
        for path,digest in old['inputs'].items():
            if path not in known:known[path]=digest
    original_observer=G/'probe/tests/cpu_guest_kernel.cpp';changed_observer=P/'merge-envcpu-kernel-v2.cpp';bind(original_observer,'1d7c6443e4b1a6e637a1066d50ae9583b57b752616ffe153bb4479541a19abe4');known[str(changed_observer)]='140fcbdc540b68942e154310e2532edc5fc2174c760d1ce5cbb5b6efd4cb91c7';bind(changed_observer)
    normalized=[];inside=False;guard_count=0
    for line in changed_observer.read_text().splitlines(True):
        if line=='        { InterruptGuard guard;\n':require(not inside,'No nested diagnostic guard');inside=True;guard_count+=1
        elif inside and line=='        }\n':inside=False
        elif inside:require(line.startswith('            printf'),'Only existing print statements may be guarded');normalized.append(line[4:])
        else:normalized.append(line)
    require(not inside and guard_count==3 and ''.join(normalized)==original_observer.read_text(),'Every original assertion, sample, workload, wait, scheduling and recovery statement preserved; only3 complete boot diagnostic records guarded');state['original_observer_all_non_logging_logic_preserved']=True
    for relative,digest in root['kernel_candidate_sources'].items():bind(C/relative,digest)
    for path,digest in TOOLS.items():bind(path,digest)
    for name in ('home','tmp','crash','integer'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp'),'QEMU_MODULE_DIR':str(LIB/'qemu')}
    state['stage']='same_candidate_core_full_MD_revalidation';emit();names=['memory','native_runtime','native_events','gdt','multitasking','syscalls','interrupts','port','native_realtime','resources','native_surface','native_fp','elf32','process_address_space','paging','physical','bootstrap'];core=[]
    for name in names:
        directory=P/('root-guest-02' if name in names[:3] else 'root-guest-03');obj=directory/(name+'.o');dep=directory/(name+'.d');item=root['objects'][str(obj)];bind(obj,item['sha256']);bind(dep,item['MD_sha256']);found=dependencies(dep);require(found==item['dependencies'],'Actual complete reused core MD')
        for logical,actual in found.items():require(root['inputs'].get(actual)==known.get(actual),'Same independently qualified core origin');bind(logical,root['inputs'][actual])
        core.append(obj);state['objects'][str(obj)]={**item,'reused':True}
    for name in ('loader','fp-asm','stubs'):
        obj=P/'guest-05'/(name+'.o');item=root['objects'][str(obj)];bind(obj,item['sha256']);bind(item['source'],root['inputs'][item['source']]);core.append(obj);state['objects'][str(obj)]={**item,'reused':True}
    require(len(core)==20 and len(root['objects'])==21,'Exact17 core CXX plus3 unchanged ASM, excluding old observer')
    listing=run('qemu-library-list',[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu'),'--list',QEMU]).read_text();require('not found' not in listing,'QEMU runtime resolves')
    for line in listing.splitlines():
        words=line.split()
        if '=>' in words:path=pathlib.Path(words[words.index('=>')+1]);require(QROOT in resolve(path,parents).parents,'No mixed host runtime');bind(path,root['inputs'][str(resolve(path,parents))])
        elif words and words[0].startswith('/'):bind(words[0],root['inputs'][str(resolve(words[0],parents))])
    for name in ('bios-256k.bin','multiboot.bin','multiboot_dma.bin'):path=QROOT/'firmware'/name;bind(path,root['inputs'][str(path)])
    path=LIB/'qemu/accel-tcg-i386.so';bind(path,root['inputs'][str(resolve(path,parents))])
    state['stage']='strict_original_integer_archive_member_extraction';emit();pins=receipt(P/'integer-member-pins-01.json','f21ed00a66c0cd83b435538cf321cffaeb6860518e05aea9b64883c47d223326');require(pins['ObjectCount']==14 and pins['ArchiveSHA256']=='f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528','Pinned integer archive identity');bind(INTEGER,pins['ArchiveSHA256']);data=INTEGER.read_bytes();require(len(data)<1024*1024 and data[:8]==b'!<arch>\n','Bounded regular archive only');at=8;table=None;integer=[];expected={item['Name']:item for item in pins['Members']};seen=set()
    while at<len(data):
        require(at+60<=len(data),'Bounded ar header');head=data[at:at+60];require(head[58:]==b'`\n','Regular ar trailer');label=head[:16].decode('ascii').strip();length=int(head[48:58].decode('ascii').strip());start=at+60;end=start+length;require(0<=length and end<=len(data),'Bounded ar payload')
        if label=='//':require(table is None,'One extended-name table');table=data[start:end]
        elif label!='/':
            require(re.fullmatch(r'/[0-9]+',label) is not None and table is not None,'Reviewed GNU long-name form');offset=int(label[1:]);require(0<=offset<len(table),'Bounded member name offset');stop=table.find(b'/\n',offset);require(stop>offset,'Terminated member name');name=table[offset:stop].decode('ascii');require(re.fullmatch(r'libscalar_integer\.[A-Za-z0-9_]+\.o',name) is not None and name in expected and name not in seen,'Exact unique integer member');payload=data[start:end];pin=expected[name];require(length==pin['Bytes'] and hashlib.sha256(payload).hexdigest()==pin['SHA256'] and payload[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',payload,16)==(1,3),'Actual pinned ELF32/i386 member');target=OUT/'integer'/name
            with target.open('xb') as stream:stream.write(payload)
            integer.append(target);seen.add(name);state['objects'][str(target)]={'sha256':sha(target),'original_archive_member':name,'reused':False}
        at=end+(length&1)
    require(at==len(data) and seen==set(expected) and len(integer)==14,'Entire ordinary archive parsed without omissions');state['integer_archive_original_members']=14
    script=R/'tests/native_process_smoke.ld';audit=R/'tools/audit-kernel-instructions.py';bind(script,root['inputs'][str(script)]);bind(audit,'fa5fd0e5903f92e4587f42ee3dd000d75291ee76922f29ad59c5cf007caeddfa')
    state['stage']='qualify_original_O2_user_ELFs_and_real_provider_MD';emit();users={'envcpu':(G/'guest-01/O2/user.elf','bb6f1254c8423b68ab27341a7be3dc52fb68df81efe14c2032138adf2992ab9a',env),'events':(E/'guest-02/O2/user.elf','49b8d9355feb129c20915401e0f5b91973776b9e6cde09d6a94f8d8deccc08e0',event)}
    for label,(elf,digest,old) in users.items():
        require(sum(row.get('user_sha256')==digest for row in old['guests'])==2,'Original O2 user ELF acceptance identity');bind(elf,digest);require(elf.read_bytes()[:7]==b'\x7fELF\x01\x01\x01','Actual user ELF32')
        directory=elf.parent
        for name in ('user','memory') if label=='envcpu' else ('user','user-memory'):
            obj=directory/(name+'.o');dep=pathlib.Path(str(obj)+'.d');item=old['objects'][str(obj)]
            if label=='envcpu':dep=directory/(name+'.d')
            bind(obj,item['sha256']);bind(dep,item['MD_sha256']);found=dependencies(dep,R if label=='envcpu' else E/'guest-02');require(found==item['dependencies'],'Original O2 user full MD')
            for logical,actual in found.items():require(known.get(actual)==old['inputs'].get(actual),'Old/new C ABI source identity conflict');bind(logical,old['inputs'][actual])
    for source,old in ((R/'apps/native_clock_probe/start.s',env),(G/'probe/tests/cpu_guest_user.ld',env),(E/'probe-v2/user.ld',event)):
        bind(source,old['inputs'][str(source)]);require(source.suffix!='.s' or not re.search(r'^\s*\.(?:include|incbin)\b',source.read_text(),re.M),'Standalone original user startup ASM')
    for directory,digest,selected in ((G/'providers-02','4d36b0f775ae5a9d2dafcd7413b8480183d01fb09d64b97ccf8ffa9de301c3f3',('getenv','environ_internal','environment')),(B/'progress-02','360086d599f59126f3b148a31bd76d8fe6a855d1dcef01601e86828f03324ba9',('progress_reporter',))):
        provider=receipt(directory/'status.json',digest);require(all(provider[key] for key in ('diagnostic_complete','objects_qualified','complete_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged','MD_aliases_unchanged')) and not provider['failed_rows'] and not provider['unbound_MD_inputs'],'Genuine provider complete qualification')
        for name in selected:
            item=provider['objects'][name];bind(item['path'],item['sha256']);bind(item['MD'],item['MD_sha256']);found=dependencies(pathlib.Path(item['MD']),directory/'build');require(len(set(found.values()))==item['MD_unique'],'Provider full MD count')
            for logical,actual in found.items():require(provider['MD_aliases'].get(logical)==actual,'Provider MD alias');require(actual not in known or known[actual]==provider['inputs'][actual],'Provider snapshot conflict');known[actual]=provider['inputs'][actual];bind(logical,provider['inputs'][actual])
    flags=['-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-nostdinc','-nostdinc++','-std=c++11','-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize','-fno-pic','-fno-pie','-fno-stack-protector','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-write-strings','-I'+str(C/'include'),'-I'+str(R/'include'),'-I'+str(R/'tests'),'-I'+str(G/'probe/tests'),'-O2','-fcrash-diagnostics-dir='+str(OUT/'crash')]
    for label,source,digest in (('envcpu',changed_observer,'140fcbdc540b68942e154310e2532edc5fc2174c760d1ce5cbb5b6efd4cb91c7'),('events',E/'probe-v2/kernel.cpp','1d7871e1e039c1222fdb5c8187e095d4a0e1c6cb610cc1afcd183940d741e79b')):
        state['stage']='compile_real_'+label+'_observer';emit();bind(source,digest);pre=OUT/(label+'.pre.d');dep=OUT/(label+'.d');obj=OUT/(label+'.o');run(label+'-M',[CC,*flags,'-M','-MT',label,'-MF',pre,source]);before=dependencies(pre)
        for logical in before:bind(logical)
        require(str(C/'include/process/native_runtime.h') in before.values() and str(C/'include/multitasking.h') in before.values(),'Observer consumes the actual new Task/Runtime ABI');run(label+'-CXX',[CC,*flags,'-MD','-MF',dep,'-c',source,'-o',obj]);require(dependencies(dep)==before,'New observer complete M=MD');state['objects'][str(obj)]={'sha256':sha(obj),'MD_sha256':sha(dep),'dependencies':before,'reused':False}
        kernel=OUT/(label+'.bin');mapping=OUT/(label+'.map');run(label+'-link',[LD,'-melf_i386','--gc-sections','-T',script,'-Map',mapping,'-o',kernel,obj,*core,*integer]);require('Archive member included' not in mapping.read_text() and len([line for line in mapping.read_text().splitlines() if line.startswith('LOAD ')])==35,'All35 real object inputs, no opaque archive LOAD');run(label+'-ISA',[PYTHON,'-I','-B','-S',audit,'--objdump',OBJDUMP,'--map',mapping,'--source-root',R,kernel]);state['kernels'][label]={'path':str(kernel),'sha256':sha(kernel),'map_sha256':sha(mapping),'full_instruction_audit_pass':True};emit()
        for memory,cpus in (('32M','1'),('64M','4')):
            name=label+'-'+memory+'-smp'+cpus;guest=OUT/(name+'-guest.log');elf,user_sha,old=users[label];state['stage']='native_'+name;emit();require(sha(elf)==user_sha and sha(kernel)==state['kernels'][label]['sha256'],'Guest images unchanged')
            run(name,[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu'),QEMU,'-L',QROOT/'firmware','-bios',QROOT/'firmware/bios-256k.bin','-machine','pc','-accel','tcg','-cpu','max','-m',memory,'-smp',cpus,'-nodefaults','-kernel',kernel,'-initrd',elf,'-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],expected=33,timeout=180)
            require(guest.stat().st_size<1024*1024,'Bounded guest output');text=guest.read_text();lines=text.splitlines();require('FAILED' not in text and 'GUEST FAIL' not in text and 'NATIVE EVENTS FAIL' not in text,'No assertion failure');item={'name':name,'kernel_sha256':sha(kernel),'user_sha256':user_sha,'log_sha256':sha(guest)}
            if label=='envcpu':
                require(all(lines.count(value)==1 for value in ('CPU CLOCK GUEST PASS','ENVIRONMENT SNAPSHOT GUEST PASS','GETENV SSE2 GUEST PASS')),'All actual original CPU/environment/SDK gates');environment_row=rows(lines,'ENV FINAL ');fp=rows(lines,'CPU SSE2 ');sdk=rows(lines,'ENV SDK ');samples=rows(lines,'CPU SAMPLE ');stops=rows(lines,'CPU STOP ');reaps=rows(lines,'CPU REAP ');final=rows(lines,'CPU FINAL ')
                require(len(environment_row)==len(fp)==len(final)==1 and environment_row[0]['calls']>200 and environment_row[0]['errors']==126 and environment_row[0]['bootstraps']==9,'Original environment boundary/init coverage');require(fp[0]['initialized']==fp[0]['invalidated']==32 and fp[0]['saves']>0 and fp[0]['restores']>0 and fp[0]['failures']==0 and [row['case'] for row in sdk]==[0,2,3,4,5,6,7,7] and all(row['ctor']==1 for row in sdk),'Actual32 FP contexts and eight SDK/color/constructor cases')
                for sample in samples:require(((sample['us_hi']<<32)|sample['us_lo'])==sample['ticks']*11931000000//1193182 and sample['scope'] in (1,2) and sample['tid'] and sample['pid'],'Independent rational PIT running-time observation')
                require([row['mode'] for row in stops]==[0]*7+[2] and [row['exit'] for row in stops]==[0]*7+[0x8000000e] and len(reaps)==8 and all(row['free']==row['expected'] for row in reaps) and final[0]['peak']==4 and final[0]['errors']==112 and final[0]['free']==final[0]['expected'] and len({row['tid'] for row in samples})==32 and any(row['scope']==2 and row['ticks']>=6 for row in samples),'Original capacity/identity/worker/fault/reuse/exact recovery gates');item.update(environment=environment_row[0],fp=fp[0],SDK_cases=sdk,samples=samples,stops=stops,reaps=reaps,final=final[0])
            else:
                require(lines.count('NATIVE EVENTS PASS')==1 and 'EVENT LIMITS objects=00000020 handles=00000040 overflow=00000001' in text and 'EVENT RESLEEP isolated=00000001' in text,'Full original native event/reset/rights/resleep gates');final=rows(lines,'EVENT FINAL ');require(len(final)==1 and final[0]['parks']>=8 and final[0]['immediate']>=20 and final[0]['fp_init']==final[0]['fp_invalid']==9 and final[0]['free']==final[0]['expected'] and final[0]['handles']==final[0]['objects']==0,'Event parking/FP/fault/peer/frame/alias/object recovery');item['final']=final[0]
            state['guests'].append(item);emit()
    state['stage']='terminal_all_input_alias_cache_output_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()),'Input/alias changed');require(all((pathlib.Path(path).stat().st_size,pathlib.Path(path).stat().st_mtime_ns)==info for path,info in retained.items()),'Old cache metadata changed');require(all(sha(path)==row['sha256'] for path,row in state['objects'].items()) and all(sha(row['path'])==row['sha256'] for row in state['kernels'].values()),'New/reused object/kernel changed');require(len(state['guests'])==4,'All four independent combined guests required')
    state.update(complete=True,merged_environment_CPU_events_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,old_cache_metadata_unchanged=True,outputs_unchanged=True,reused_candidate_core_CXX_objects=17,reused_hashed_ASM_objects=3,recompiled_genuine_observers=2,original_integer_members_explicitly_audited=14,stage='completed',scope='Current events/file-dupe plus original environment and CPU-clock candidate, same Task/Runtime ABI as actual four-run root. O2 CPU observer adds IF0 only around3 existing complete boot print records to prevent original syscall-sample text interleaving; removal of these3 scopes and indentation exactly restores original source. Every original assertion/workload/wait/sample/reap preserved. Original event observer unchanged. Genuine O2 user ELFs, startup/linker data and provider full MD rebound.32M1/64M4 CPU/environment32-context and event9-context suites retain original rational/SDK constructor/color/bounds/worker/peer/group fault/reuse/physical/FP/alias/object recovery. All35 explicit objects per kernel audited, no archive exemption. BSP only. No public promotion, whole FilePath/PMA class, flush, Content/browser/web/video/HTML5 or production image acceptance.');emit();print('PASS independent merged CPU/environment and event regression',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
