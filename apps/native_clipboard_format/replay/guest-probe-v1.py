#!/usr/bin/env python3
"""Run the real format class and its actual CHECK termination in GTOS CPL3."""
import ctypes,hashlib,json,os,pathlib,re,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-clipboard-format-20261018-a';B=W/'artifacts/native-pma-20261017-a';R=W/'sources/GTOS';C=B/'kernel-candidate-v1';OUT=P/'guest-01';SOURCE=P/'clipboard-kernel-v1.cpp'
T=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin';CC=T/'clang';LD=pathlib.Path('/usr/bin/x86_64-linux-gnu-ld.bfd');OBJDUMP=pathlib.Path('/usr/bin/x86_64-linux-gnu-objdump');PYTHON=pathlib.Path('/usr/bin/python3')
QROOT=W/'toolchains/gtos-qemu8-native-files-a';LIB=QROOT/'root/usr/lib/x86_64-linux-gnu';LOADER=LIB/'ld-linux-x86-64.so.2';QEMU=QROOT/'root/usr/bin/qemu-system-i386'
LINK_SHA='00a1404d4177975589c9072335e21fc578aeb11622380f35dc5699b6b7bf2bd0';ISA_SHA='39679642eae38c2880a9bed51adaff40747f1f5896542c61fe82f1ce10eb64ae';SOURCE_SHA='ab9add5faa19abfb56a4046a4f4a3dca11cdeab8fa06bdfaa9c34ceaf5a76517'
TOOLS={CC:'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',LD:'476b24d5cc1fef54f80412d06e7430b2dcf4e2b6f1525ee17e6eaaf16b28a00f',OBJDUMP:'afd9c7433c96cb0f37df99cab98fc860bbbe2bed17f37df66254a8778adfc61d',PYTHON:'298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06',QEMU:'28fa14f1c45fca7422e3ec5737768b33b705de2f31014a76e658d4263464a6a5',LOADER:'c20a2dc8917c755f02b94049356320fe1f62ac7d9f8994731f807d9df39302da'}
state={'complete':False,'native_clipboard_format_pass':False,'desktop_clipboard_service_pass':False,'native_Content_pass':False,'browser_pass':False,'production_image_changed':False,'inputs':{},'aliases':{},'objects':{},'kernels':{},'commands':[],'guests':[],'dependency_outputs':{}};known={};parents={};retained={};authorized=False;code=1

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()

def resolve(path,cache):
    path=pathlib.Path(os.path.normpath(str(path)));key=str(path.parent)
    if key not in cache:cache[key]=path.parent.resolve(strict=True)
    actual=cache[key]/path.name
    if actual.is_symlink():actual=actual.resolve(strict=True)
    actual.stat();return actual

def bind(path,digest=None):
    actual=resolve(path,parents);key=str(actual);digest=known.get(key) if digest is None else digest
    require((W in actual.parents or pathlib.Path('/usr') in actual.parents) and actual.is_file() and digest is not None,'Unbound or escaped input: '+key)
    if key in state['inputs']:require(state['inputs'][key]==digest,'Input digest conflict')
    else:
        require(sha(actual)==digest,'Changed input: '+key);state['inputs'][key]=digest
        if actual.suffix in ('.o','.a') and OUT not in actual.parents:metadata=actual.stat();retained[key]=(metadata.st_size,metadata.st_mtime_ns)
    state['aliases'][str(path)]=key

def receipt(path,digest):
    bind(path,digest);value=json.loads(path.read_text());require('failure' not in value,'Failed prerequisite');return value

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def run(name,argv,expected=0,timeout=180):
    argv=[str(value) for value in argv];require(pathlib.Path(argv[0]) in TOOLS and sha(argv[0])==TOOLS[pathlib.Path(argv[0])],'Exact reviewed tool only');log=OUT/(name+'.log')
    with log.open('xb') as stream:result=subprocess.run(argv,cwd=R,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode,'log_sha256':sha(log)});emit();require(result.returncode==expected and log.stat().st_size<8*1024*1024,'Failed '+name+'; preserve bounded log');print('PASS '+name,flush=True);return log

def dependencies(path):
    text=path.read_text().replace(chr(92)+chr(10),' ');require(text.count(':')==1 and chr(92) not in text and '$' not in text,'Plain complete MD syntax');names=shlex.split(text.split(':',1)[1]);require(0<len(names)<10000,'Bounded complete dependencies');return {str(R/name):str(resolve(R/name,parents)) for name in names}

def rows(lines,prefix):
    return [dict((key,int(value,16)) for key,value in (part.split('=',1) for part in line[len(prefix):].split())) for line in lines if line.startswith(prefix)]

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated no-site entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-guest-probe-v1.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Exact inherited guard')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    require(len(LINK_SHA)==len(ISA_SHA)==len(SOURCE_SHA)==64,'Actual link/ISA and new kernel review required before any guest')
    link=receipt(P/'link-02/status.json',LINK_SHA);require(all(link[key] for key in ('complete','clipboard_link_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged','outputs_unchanged','heap_new_M_qualified','exit_locked_M_MD_requalified','exit_new_M_qualified','exit_materialized_physical_sources_only')) and link['exit_legacy_source_view_consumed'] is False and set(link['outputs'])=={'positive','negative'},'Whole real format and separate compiled mode ELF qualification')
    isa=receipt(P/'isa-01/status.json',ISA_SHA);require(isa['complete'] and isa['ISA_review_pass'] and isa['bound_inputs_unchanged'] and len(isa['objects'])==5,'All five new/selected user code objects reviewed')
    require(all(link['inputs'].get(item['path'])==item['sha256'] for item in isa['objects'].values()),'Every ISA-reviewed user object is the identical qualified link input')
    merged=receipt(B/'merge-guest-02/status.json','57a3c24298b422b25a1b9ecd3b698c0f5c9db2bf9302afb9e80c88ceafbc6e75');require(all(merged[key] for key in ('complete','merged_environment_CPU_events_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged','outputs_unchanged')) and merged['reused_candidate_core_CXX_objects']==17,'Accepted current canonical kernel ABI')
    known.update(merged['inputs'])
    for path,digest in link['inputs'].items():require(path not in known or known[path]==digest,'User/kernel input conflict');known[path]=digest;bind(path,digest)
    for logical,actual in link['aliases'].items():require(str(resolve(logical,parents))==actual,'User link alias changed');state['aliases'][logical]=actual
    bind(SOURCE,SOURCE_SHA);known[str(SOURCE)]=SOURCE_SHA
    lock=receipt(R/'apps/native_environment/source-lock.json','21ac0f2b88dc8e05310b1976b2b610863abea3d1a52d8f9cf16dcc5fc8041d34');require(len(lock['CoreFiles'])==8,'Eight promoted kernel ABI files')
    for relative,digest in lock['CoreFiles'].items():bind(R/relative,digest);bind(C/relative,digest);require((R/relative).read_bytes()==(C/relative).read_bytes(),'Current canonical/cache ABI mismatch')
    for path,digest in TOOLS.items():bind(path,digest)
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp'),'QEMU_MODULE_DIR':str(LIB/'qemu')}
    state['stage']='requalify_current_core_complete_MD_and_integer_members';emit();names=['memory','native_runtime','native_events','gdt','multitasking','syscalls','interrupts','port','native_realtime','resources','native_surface','native_fp','elf32','process_address_space','paging','physical','bootstrap'];core=[]
    for name in names:
        directory=B/('root-guest-02' if name in names[:3] else 'root-guest-03');obj=directory/(name+'.o');dep=directory/(name+'.d');item=merged['objects'][str(obj)];bind(obj,item['sha256']);bind(dep,item['MD_sha256']);found=dependencies(dep);require(found==item['dependencies'],'Complete current core MD')
        for logical,actual in found.items():bind(logical,merged['inputs'][actual])
        core.append(obj);state['objects'][str(obj)]=dict(item,reused=True)
    for name in ('loader','fp-asm','stubs'):
        obj=B/'guest-05'/(name+'.o');item=merged['objects'][str(obj)];bind(obj,item['sha256']);bind(item['source'],merged['inputs'][item['source']]);core.append(obj);state['objects'][str(obj)]=dict(item,reused=True)
    pins=receipt(B/'integer-member-pins-01.json','f21ed00a66c0cd83b435538cf321cffaeb6860518e05aea9b64883c47d223326');require(pins['ObjectCount']==14 and pins['ArchiveSHA256']=='f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528','Real original integer archive provenance');integer=[]
    for item in pins['Members']:
        name=item['Name'];require(re.fullmatch(r'libscalar_integer\.[A-Za-z0-9_]+\.o',name),'Fixed typed archive member');obj=B/'merge-guest-02/integer'/name;require(merged['objects'][str(obj)]['sha256']==item['SHA256'],'Previously exact extracted member');bind(obj,item['SHA256']);data=obj.read_bytes();require(len(data)==item['Bytes'] and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Original ELF32 integer member');integer.append(obj);state['objects'][str(obj)]={'sha256':item['SHA256'],'original_archive_member':name,'reused':True}
    require(len(core)==20 and len(integer)==14,'Only twenty current core/ASM and fourteen explicit integer objects')
    listing=run('qemu-library-list',[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu'),'--list',QEMU]).read_text();require('not found' not in listing,'Bundled QEMU runtime resolves')
    for line in listing.splitlines():
        words=line.split()
        if '=>' in words:path=pathlib.Path(words[words.index('=>')+1]);require(QROOT in resolve(path,parents).parents,'No mixed host runtime');bind(path)
        elif words and words[0].startswith('/'):bind(words[0])
    for name in ('bios-256k.bin','multiboot.bin','multiboot_dma.bin'):bind(QROOT/'firmware'/name)
    bind(LIB/'qemu/accel-tcg-i386.so');script=R/'tests/native_process_smoke.ld';audit=R/'tools/audit-kernel-instructions.py';bind(script);bind(audit,'fa5fd0e5903f92e4587f42ee3dd000d75291ee76922f29ad59c5cf007caeddfa')
    baseflags=['-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-nostdinc','-nostdinc++','-std=c++11','-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize','-fno-pic','-fno-pie','-fno-stack-protector','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-write-strings','-I'+str(C/'include'),'-I'+str(R/'include'),'-O2','-fcrash-diagnostics-dir='+str(OUT/'crash'),'-fno-threadsafe-statics']
    for label,negative in (('positive',False),('negative',True)):
        meta=link['outputs'][label];elf=pathlib.Path(meta['path']);require(elf==P/'link-02'/(label+'.elf') and meta['negative_ascii_CHECK_control']==negative and meta['load_pages']<=4096 and meta['capacity']==meta['load_pages']+128+1024,'Exact bounded format ELF and mode');bind(elf,meta['sha256']);symbols=meta['symbols']
        values={'CLIPBOARD_RECORD_VA':symbols['native_clipboard_record']['value'],'CLIPBOARD_EXIT_STAGE_VA':symbols['native_v8_integration_stage']['value'],'CLIPBOARD_EXIT_RESULT_VA':symbols['native_v8_integration_result']['value'],'CLIPBOARD_CTOR_COUNT':meta['constructor_count'],'CLIPBOARD_EXPECT_NEGATIVE':int(negative)};require(all(isinstance(value,int) and 0<=value<=0xffffffff for value in values.values()),'Only bounded observation constants');flags=baseflags+['-D'+key+'='+hex(value)+'U' for key,value in values.items()];pre=OUT/(label+'.pre.d');dep=OUT/(label+'.d');obj=OUT/(label+'.o');state['stage']=label+'_observer_CXX';emit();run(label+'-M',[CC,*flags,'-M','-MT',label,'-MF',pre,SOURCE]);before=dependencies(pre)
        state['dependency_outputs'][str(pre)]=sha(pre)
        for logical in before:bind(logical)
        require(str(C/'include/process/native_runtime.h') in before.values() and str(C/'include/multitasking.h') in before.values(),'Observer consumes current cached/canonical ABI');run(label+'-CXX',[CC,*flags,'-MD','-MF',dep,'-c',SOURCE,'-o',obj]);require(dependencies(dep)==before,'Observer complete M equals MD');state['dependency_outputs'][str(dep)]=sha(dep);state['objects'][str(obj)]={'sha256':sha(obj),'MD_sha256':sha(dep),'dependencies':before,'reused':False}
        kernel=OUT/(label+'.bin');mapping=OUT/(label+'.map');run(label+'-link',[LD,'-melf_i386','--gc-sections','-T',script,'-Map',mapping,'-o',kernel,obj,*core,*integer]);require('Archive member included' not in mapping.read_text() and sum(line.startswith('LOAD ') for line in mapping.read_text().splitlines())==35,'Exactly thirty-five explicit kernel objects');run(label+'-ISA',[PYTHON,'-I','-B','-S',audit,'--objdump',OBJDUMP,'--map',mapping,'--source-root',R,kernel]);state['kernels'][label]={'path':str(kernel),'sha256':sha(kernel),'map_sha256':sha(mapping),'full_instruction_audit_pass':True};emit()
        for memory,cpus in ((('32M','1'),) if negative else (('32M','1'),('64M','4'))):
            name=label+'-'+memory+'-smp'+cpus;guest=OUT/(name+'-guest.log');state['stage']='native_'+name;emit();require(sha(elf)==meta['sha256'] and sha(kernel)==state['kernels'][label]['sha256'],'Exact actual guest images')
            run(name,[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu'),QEMU,'-L',QROOT/'firmware','-bios',QROOT/'firmware/bios-256k.bin','-machine','pc','-accel','tcg','-cpu','max','-m',memory,'-smp',cpus,'-nodefaults','-kernel',kernel,'-initrd',elf,'-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],expected=33,timeout=180)
            require(guest.stat().st_size<1024*1024,'Bounded guest log');text=guest.read_text();lines=text.splitlines();require(lines.count('GTOS CLIPBOARD RUN PASS')==1 and 'FAILED' not in text and 'GTOS CLIPBOARD RUN FAIL' not in text,'All actual kernel assertions pass');initial=rows(lines,'CLIPBOARD INITIAL ');executions=rows(lines,'CLIPBOARD EXECUTION ');semantics=rows(lines,'CLIPBOARD SEMANTICS ');reaps=rows(lines,'CLIPBOARD REAP ')
            require(len(initial)==len(executions)==len(reaps)==2 and [row['wave'] for row in executions]==[0,1] and all(row['image']==meta['load_pages'] and row['resident']==meta['load_pages']+128 and row['capacity']==meta['capacity'] for row in initial),'Two actual independent admission waves')
            require(all(row['mode']==int(negative) and row['vector']==row['fault']==0 and row['cs']==0x23 and row['ctor']==meta['constructor_count'] and row['dynamic']<=1024 for row in executions),'Actual mode CPL3 constructors and bounded heap without faults')
            require(all(row['free']==row['baseline'] and row['tasks']==1 and row['fp_init']==row['fp_invalid']==index+1 and row['fp_failures']==0 for index,row in enumerate(reaps)),'Exact frame/task/FP recovery each launch')
            if negative:
                require(not semantics and lines.count('CLIPBOARD ASCII CHECK REJECTED')==2 and lines.count('GTOS NATIVE HEAP FAIL V1')==2 and all(row['stage']==10 and row['result']==row['exitresult']==0xffffffff and row['exit']==0x48000021 and row['error']==row['checks']==row['mask']==row['exitstage']==0 for row in executions),'Genuine non-ASCII CHECK abort/panic exit, no returned value or fault accepted')
            else:
                require(len(semantics)==2 and all(row['stage']==3 and row['result']==row['error']==row['exit']==row['exitresult']==0 and row['exitstage']==4 and row['checks']==289 and row['mask']==63 for row in executions) and all(row['formats']==12 and row['serialized']==14 and row['comparisons']==100 and row['lifetimes']==32 and row['custom']==row['webcustom']==4 and row['retained']==66272 and row['rejected']==0 for row in semantics),'Complete actual class identity/serialization/order/copy/lifetime/custom semantics')
            state['guests'].append({'name':name,'negative':negative,'kernel_sha256':sha(kernel),'user_sha256':sha(elf),'log_sha256':sha(guest),'initial':initial,'executions':executions,'semantics':semantics,'reaps':reaps});emit()
    state['stage']='terminal_source_alias_cache_evidence_and_output_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()),'Final input or alias changed');require(all((pathlib.Path(path).stat().st_size,pathlib.Path(path).stat().st_mtime_ns)==info for path,info in retained.items()),'Old object/archive cache metadata changed')
    require(all(sha(path)==item['sha256'] for path,item in state['objects'].items()) and all(sha(item['path'])==item['sha256'] and sha(OUT/(name+'.map'))==item['map_sha256'] for name,item in state['kernels'].items()) and all(sha(OUT/(row['name']+'-guest.log'))==row['log_sha256'] for row in state['guests']) and all(sha(OUT/(row['name']+'.log'))==row['log_sha256'] for row in state['commands']) and len(state['guests'])==3,'All new objects/kernels/maps/guest/tool evidence unchanged')
    require(all(sha(path)==digest for path,digest in state['dependency_outputs'].items()) and len(state['dependency_outputs'])==4,'Both new observer full-M and MD evidence unchanged')
    state.update(complete=True,native_clipboard_format_pass=True,ASCII_CHECK_abort_exit_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,old_cache_metadata_unchanged=True,outputs_unchanged=True,positive_native_contexts=4,negative_native_contexts=2,stage='completed',scope='Whole original string-format ClipboardFormatType in GTOS CPL3:289 assertions, opaque serialization, unsigned ordering, copies and32 lifetime waves, custom MIME names; genuine non-ASCII CHECK invokes original abort/heap panic SYS_EXIT0x48000021. Four positive and two negative launches with constructors and exact frame/task/FP/peer recovery. All35 kernel objects audited. BSP only; no desktop clipboard data service, Content/browser/web/video/HTML5 or production image claim.');emit();print('PASS native clipboard class and genuine CHECK teardown',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
