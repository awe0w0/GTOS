#!/usr/bin/env python3
"""Separate original-class CPL3 diagnostic with current canonical kernel ABI."""
import ctypes,hashlib,json,os,pathlib,re,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';R=W/'sources/GTOS';C=P/'kernel-candidate-v1';OUT=P/'class-guest-03';T=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin';CC=T/'clang';LD=pathlib.Path('/usr/bin/x86_64-linux-gnu-ld.bfd');OBJDUMP=pathlib.Path('/usr/bin/x86_64-linux-gnu-objdump');PYTHON=pathlib.Path('/usr/bin/python3');PRIOR=P/'class-guest-02'
QROOT=W/'toolchains/gtos-qemu8-native-files-a';LIB=QROOT/'root/usr/lib/x86_64-linux-gnu';LOADER=LIB/'ld-linux-x86-64.so.2';QEMU=QROOT/'root/usr/bin/qemu-system-i386';SOURCE=P/'class-kernel-v2.cpp';LINK_SHA='f5218569cbfe1f22ab490508ec04f5ff53d07bc90a5f4b976b10801741564846';ISA_SHA='0aee0b36be62b4e3c0e5338a359b38cb94396c395d7dd344204d6ccea7d9db68'
TOOLS={CC:'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',LD:'476b24d5cc1fef54f80412d06e7430b2dcf4e2b6f1525ee17e6eaaf16b28a00f',OBJDUMP:'afd9c7433c96cb0f37df99cab98fc860bbbe2bed17f37df66254a8778adfc61d',PYTHON:'298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06',QEMU:'28fa14f1c45fca7422e3ec5737768b33b705de2f31014a76e658d4263464a6a5',LOADER:'c20a2dc8917c755f02b94049356320fe1f62ac7d9f8994731f807d9df39302da'}
state={'complete':False,'native_class_pass':False,'strnlen_guard_pass':False,'FilePath_UTF_lifetime_pass':False,'LocalPMA_class_pass':False,'file_flush_pass':False,'browser_pass':False,'production_image_changed':False,'inputs':{},'aliases':{},'objects':{},'kernels':{},'commands':[],'guests':[]};known={};parents={};retained={};authorized=False;code=1

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
    if actual.is_symlink():actual=actual.resolve(strict=True)
    actual.stat();return actual

def bind(path,digest=None):
    actual=resolve(path,parents);key=str(actual);digest=known.get(key) if digest is None else digest;require((W in actual.parents or pathlib.Path('/usr') in actual.parents) and actual.is_file() and digest is not None,'Unbound/escaped input: '+key)
    if key in state['inputs']:require(state['inputs'][key]==digest,'Digest conflict')
    else:
        require(sha(actual)==digest,'Input changed: '+key);state['inputs'][key]=digest
        if actual.suffix in ('.o','.a') and OUT not in actual.parents:item=actual.stat();retained[key]=(item.st_size,item.st_mtime_ns)
    state['aliases'][str(path)]=key

def receipt(path,digest):
    bind(path,digest);value=json.loads(path.read_text());require('failure' not in value,'Failed prerequisite');return value

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def run(name,argv,expected=0,timeout=180):
    argv=[str(value) for value in argv];require(pathlib.Path(argv[0]) in TOOLS and sha(argv[0])==TOOLS[pathlib.Path(argv[0])],'Exact reviewed tool');log=OUT/(name+'.log')
    with log.open('xb') as stream:result=subprocess.run(argv,cwd=R,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode,'log_sha256':sha(log)});emit();require(result.returncode==expected and log.stat().st_size<8*1024*1024,'Failed '+name+'; inspect retained log');print('PASS '+name,flush=True);return log

def dependencies(path,cwd=R):
    text=path.read_text().replace(chr(92)+chr(10),' ');require(text.count(':')==1 and chr(92) not in text and '$' not in text,'Plain complete MD syntax');names=shlex.split(text.split(':',1)[1]);require(0<len(names)<10000,'Bounded MD count');return {str(cwd/name):str(resolve(cwd/name,parents)) for name in names}

def rows(lines,prefix):
    return [dict((key,int(value,16)) for key,value in (part.split('=',1) for part in line[len(prefix):].split())) for line in lines if line.startswith(prefix)]

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-class-guest-v3.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    require(len(LINK_SHA)==len(ISA_SHA)==64,'Final class link and all new user code ISA review required')
    link=receipt(P/'class-link-03/status.json',LINK_SHA);require(all(link[key] for key in ('complete','separate_class_link_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged')) and set(link['outputs'])=={'classes','wrong-length'} and not link['original_V8_root_replayed'],'Separate class ELF provenance only')
    isa=receipt(P/'class-isa-review-02.json',ISA_SHA);require(isa['complete'] and isa['ISA_review_pass'] and isa['bound_inputs_unchanged'] and len(isa['objects'])==9,'All9 current class/startup/UTF/SysInfo/PMA indexed code units reviewed')
    merged=receipt(P/'merge-guest-02/status.json','57a3c24298b422b25a1b9ecd3b698c0f5c9db2bf9302afb9e80c88ceafbc6e75');require(all(merged[key] for key in ('complete','merged_environment_CPU_events_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged','outputs_unchanged')) and merged['reused_candidate_core_CXX_objects']==17,'Accepted current canonical Task/Runtime ABI')
    original=receipt(P/'root-guest-03/status.json','05df29b1b27b72401e959b8a680cd6418fe9fcc647763c60bf9193dcdcca3549');recipes=[row for row in original['commands'] if row['returncode']==0 and row['argv'][:1]==[str(CC)] and '-c' in row['argv'] and str(P/'root-kernel-v1.cpp') in row['argv']];require(original['complete'] and len(recipes)==1 and '-fno-threadsafe-statics' in recipes[0]['argv'],'Accepted once-only boot observer uses freestanding static initialization, keeping its actual constructor; no guard-function stubs')
    known.update(merged['inputs'])
    for path,digest in link['inputs'].items():require(path not in known or known[path]==digest,'User/kernel source identity conflict');known[path]=digest;bind(path,digest)
    for logical,actual in link['aliases'].items():require(str(resolve(logical,parents))==actual,'Class link alias changed');state['aliases'][logical]=actual
    known[str(SOURCE)]='2888acd7e719c11968bd1afe498d859fd2d0bdbb0d16f16768a1e205a9c5278e';bind(SOURCE)
    source_lock=receipt(R/'apps/native_environment/source-lock.json','21ac0f2b88dc8e05310b1976b2b610863abea3d1a52d8f9cf16dcc5fc8041d34');require(len(source_lock['CoreFiles'])==8,'Current promoted eight-file source ABI')
    for relative,digest in source_lock['CoreFiles'].items():bind(R/relative,digest);bind(C/relative,digest);require((R/relative).read_bytes()==(C/relative).read_bytes(),'Canonical/cached current ABI differs')
    for path,digest in TOOLS.items():bind(path,digest)
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp'),'QEMU_MODULE_DIR':str(LIB/'qemu')}
    state['stage']='requalify_current_core_full_MD_and_integer_members';emit();names=['memory','native_runtime','native_events','gdt','multitasking','syscalls','interrupts','port','native_realtime','resources','native_surface','native_fp','elf32','process_address_space','paging','physical','bootstrap'];core=[]
    for name in names:
        directory=P/('root-guest-02' if name in names[:3] else 'root-guest-03');obj=directory/(name+'.o');dep=directory/(name+'.d');item=merged['objects'][str(obj)];bind(obj,item['sha256']);bind(dep,item['MD_sha256']);found=dependencies(dep);require(found==item['dependencies'],'Complete current core MD')
        for logical,actual in found.items():bind(logical,merged['inputs'][actual])
        core.append(obj);state['objects'][str(obj)]=dict(item,reused=True)
    for name in ('loader','fp-asm','stubs'):
        obj=P/'guest-05'/(name+'.o');item=merged['objects'][str(obj)];bind(obj,item['sha256']);bind(item['source'],merged['inputs'][item['source']]);core.append(obj);state['objects'][str(obj)]=dict(item,reused=True)
    pins=receipt(P/'integer-member-pins-01.json','f21ed00a66c0cd83b435538cf321cffaeb6860518e05aea9b64883c47d223326');require(pins['ObjectCount']==14 and pins['ArchiveSHA256']=='f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528','Exact original integer archive provenance');integer=[]
    for item in pins['Members']:
        name=item['Name'];require(re.fullmatch(r'libscalar_integer\.[A-Za-z0-9_]+\.o',name),'Fixed typed integer member name');obj=P/'merge-guest-02/integer'/name;require(merged['objects'][str(obj)]['sha256']==item['SHA256'],'Previously strictly extracted original archive member');bind(obj,item['SHA256']);data=obj.read_bytes();require(len(data)==item['Bytes'] and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Real original ELF32 member');integer.append(obj);state['objects'][str(obj)]={'sha256':item['SHA256'],'original_archive_member':name,'reused':True}
    require(len(core)==20 and len(integer)==14,'Only20 current core/ASM plus14 explicit integer members')
    listing=run('qemu-library-list',[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu'),'--list',QEMU]).read_text();require('not found' not in listing,'Bundled QEMU runtime resolves')
    for line in listing.splitlines():
        words=line.split()
        if '=>' in words:path=pathlib.Path(words[words.index('=>')+1]);require(QROOT in resolve(path,parents).parents,'No mixed host runtime');bind(path)
        elif words and words[0].startswith('/'):bind(words[0])
    for name in ('bios-256k.bin','multiboot.bin','multiboot_dma.bin'):bind(QROOT/'firmware'/name)
    bind(LIB/'qemu/accel-tcg-i386.so');script=R/'tests/native_process_smoke.ld';audit=R/'tools/audit-kernel-instructions.py';bind(script);bind(audit,'fa5fd0e5903f92e4587f42ee3dd000d75291ee76922f29ad59c5cf007caeddfa')
    checks=P/'patch-class-v1/tools/gtos_base_class_probe/checks.cc';bind(checks);source_lines=checks.read_text().splitlines();negative_lines=[index for index,line in enumerate(source_lines,1) if line.strip().startswith('REQUIRE_CLASS(NativeClassLength(reinterpret_cast')];require(len(negative_lines)==1 and source_lines[negative_lines[0]].strip()=='== 0);','Exact original two-line max0 macro invocation');expected_error=negative_lines[0]+1;state['wrong_length_expected_source_line']=expected_error;state['macro_line_origin']='Closing line of the pinned complete macro invocation, independently visible as0x39 in original object and native rejection; no assertion/value comparison weakened.'
    baseflags=['-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-nostdinc','-nostdinc++','-std=c++11','-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize','-fno-pic','-fno-pie','-fno-stack-protector','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-write-strings','-I'+str(C/'include'),'-I'+str(R/'include'),'-O2','-fcrash-diagnostics-dir='+str(OUT/'crash')]
    # NativeProcessSmoke constructs this observer before enabling interrupts.
    # The accepted kernel observer uses the same once-only freestanding flag.
    # User CXX objects and their real thread-safe initialization stay unchanged.
    baseflags.append('-fno-threadsafe-statics')
    bind(PRIOR/'status.json','5bf76ec4583b25ac9c0ae0f50635ce27e75040e28c4b7b69036b5ae660f69bcf');prior=json.loads((PRIOR/'status.json').read_text());require(not prior['complete'] and prior['failure']=='Failed wrong-length-32M-smp1; inspect retained log' and prior['wrong_length_expected_source_line']+1==expected_error and len(prior['guests'])==2 and set(prior['kernels'])=={'classes','wrong-length'},'Exact preserved four-positive-context run with only max0 macro start/end expectation error')
    positive_meta=link['outputs']['classes'];positive_elf=P/'class-link-03/classes.elf';bind(positive_elf,positive_meta['sha256']);positive_kernel=PRIOR/'classes.bin';require(prior['kernels']['classes']['path']==str(positive_kernel) and prior['kernels']['classes']['full_instruction_audit_pass'],'Exact accepted positive kernel');bind(positive_kernel,prior['kernels']['classes']['sha256']);bind(PRIOR/'classes.map',prior['kernels']['classes']['map_sha256']);positive_obj=PRIOR/'classes.o';positive_item=prior['objects'][str(positive_obj)];bind(positive_obj,positive_item['sha256']);bind(PRIOR/'classes.d',positive_item['MD_sha256']);found=dependencies(PRIOR/'classes.d');require(found==positive_item['dependencies'],'Positive observer complete MD remains unchanged')
    for logical,actual in found.items():bind(logical,prior['inputs'][actual])
    for suffix in ('M','CXX','link','ISA'):
        commands=[row for row in prior['commands'] if row['name']=='classes-'+suffix];require(len(commands)==1 and commands[0]['returncode']==0,'All original positive build/audit stages succeeded');bind(PRIOR/('classes-'+suffix+'.log'),commands[0]['log_sha256'])
    for item,expected_name in zip(prior['guests'],('classes-32M-smp1','classes-64M-smp4')):
        require(item['name']==expected_name and not item['negative'] and item['kernel_sha256']==prior['kernels']['classes']['sha256'] and item['user_sha256']==positive_meta['sha256'],'Exact positive images and configurations');log=PRIOR/(expected_name+'-guest.log');bind(log,item['log_sha256']);text=log.read_text();lines=text.splitlines();require(lines.count('GTOS CLASS RUN PASS')==1 and 'FAILED' not in text and rows(lines,'CLASS INITIAL ')==item['initial'] and rows(lines,'CLASS EXECUTION ')==item['executions'] and rows(lines,'CLASS SEMANTICS ')==item['semantics'] and rows(lines,'CLASS REAP ')==item['reaps'],'Actual original complete positive log records')
        require(len(item['executions'])==len(item['semantics'])==len(item['reaps'])==2 and all(row['stage']==3 and row['result']==row['error']==row['exit']==row['vector']==row['fault']==0 and row['mask']==7 and row['cs']==0x23 and row['ctor']==positive_meta['constructor_count'] and row['exitstage']==4 for row in item['executions']),'Four true CPL3/constructor/class/exit positive contexts')
        require(all(row['strings']==14 and row['paths']==21 and row['lifetimes']==32 and 2<row['allocations']<1026 and row['pages']==257 and row['released']==1 for row in item['semantics']) and all(row['free']==row['baseline'] and row['tasks']==1 and row['fp_init']==row['fp_invalid']==index+1 and row['fp_failures']==0 for index,row in enumerate(item['reaps'])),'All actual positive semantics and exact physical/task/FP recovery retained');state['guests'].append(dict(item,reused_successful_native_run=True))
    state['objects'][str(positive_obj)]=dict(positive_item,reused=True);state['kernels']['classes']=dict(prior['kernels']['classes'],reused=True);state['four_positive_contexts_requalified_without_rerun']=True
    for label,negative in (('wrong-length',True),):
        meta=link['outputs'][label];elf=P/'class-link-03'/(label+'.elf');bind(elf,meta['sha256']);require(meta['negative_diagnostic_API_control']==negative and meta['load_pages']<=4096 and meta['capacity']==meta['load_pages']+128+1024,'Exact bounded actual ELF metadata');symbols=meta['symbols'];values={'CLASS_RECORD_VA':symbols['native_class_record']['value'],'CLASS_EXIT_STAGE_VA':symbols['native_v8_integration_stage']['value'],'CLASS_EXIT_RESULT_VA':symbols['native_v8_integration_result']['value'],'CLASS_CTOR_COUNT':meta['constructor_count'],'CLASS_EXPECT_NEGATIVE':int(negative),'CLASS_EXPECT_ERROR':expected_error};require(all(isinstance(value,int) and 0<=value<=0xffffffff for value in values.values()),'Only bounded integer observation constants');flags=baseflags+['-D'+key+'='+hex(value)+'U' for key,value in values.items()];pre=OUT/(label+'.pre.d');dep=OUT/(label+'.d');obj=OUT/(label+'.o');state['stage']=label+'_observer_CXX';emit();run(label+'-M',[CC,*flags,'-M','-MT',label,'-MF',pre,SOURCE]);before=dependencies(pre)
        for logical in before:bind(logical)
        require(str(C/'include/process/native_runtime.h') in before.values() and str(C/'include/multitasking.h') in before.values(),'Observer consumes actual current cached/canonical ABI');run(label+'-CXX',[CC,*flags,'-MD','-MF',dep,'-c',SOURCE,'-o',obj]);require(dependencies(dep)==before,'Observer complete M=MD');state['objects'][str(obj)]={'sha256':sha(obj),'MD_sha256':sha(dep),'dependencies':before,'reused':False}
        kernel=OUT/(label+'.bin');mapping=OUT/(label+'.map');run(label+'-link',[LD,'-melf_i386','--gc-sections','-T',script,'-Map',mapping,'-o',kernel,obj,*core,*integer]);require('Archive member included' not in mapping.read_text() and len([line for line in mapping.read_text().splitlines() if line.startswith('LOAD ')])==35,'Exactly35 explicit kernel objects, no archive exemption');run(label+'-ISA',[PYTHON,'-I','-B','-S',audit,'--objdump',OBJDUMP,'--map',mapping,'--source-root',R,kernel]);state['kernels'][label]={'path':str(kernel),'sha256':sha(kernel),'map_sha256':sha(mapping),'full_instruction_audit_pass':True};emit()
        for memory,cpus in ((('32M','1'),) if negative else (('32M','1'),('64M','4'))):
            name=label+'-'+memory+'-smp'+cpus;guest=OUT/(name+'-guest.log');state['stage']='native_'+name;emit();require(sha(elf)==meta['sha256'] and sha(kernel)==state['kernels'][label]['sha256'],'Guest images unchanged');run(name,[LOADER,'--library-path',str(LIB)+':'+str(QROOT/'root/lib/x86_64-linux-gnu'),QEMU,'-L',QROOT/'firmware','-bios',QROOT/'firmware/bios-256k.bin','-machine','pc','-accel','tcg','-cpu','max','-m',memory,'-smp',cpus,'-nodefaults','-kernel',kernel,'-initrd',elf,'-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],expected=33,timeout=180)
            require(guest.stat().st_size<1024*1024,'Bounded guest output');text=guest.read_text();lines=text.splitlines();require(lines.count('GTOS CLASS RUN PASS')==1 and 'FAILED' not in text and 'GTOS CLASS RUN FAIL' not in text,'Every actual kernel assertion passes');initial=rows(lines,'CLASS INITIAL ');executions=rows(lines,'CLASS EXECUTION ');semantics=rows(lines,'CLASS SEMANTICS ');reaps=rows(lines,'CLASS REAP ');require(len(initial)==len(executions)==len(reaps)==2 and [row['wave'] for row in executions]==[0,1] and all(row['image']==meta['load_pages'] and row['resident']==meta['load_pages']+128 and row['capacity']==meta['capacity'] for row in initial),'Two independent actual admission/launch waves')
            require(all(row['stage']==3 and row['vector']==row['fault']==0 and row['cs']==0x23 and row['ctor']==meta['constructor_count'] and row['exitstage']==4 for row in executions),'Real CPL3, constructors, class and exit markers');require(all(row['free']==row['baseline'] and row['tasks']==1 and row['fp_init']==row['fp_invalid']==index+1 and row['fp_failures']==0 for index,row in enumerate(reaps)),'Exact physical/task/FP recovery every launch')
            if negative:require(not semantics and lines.count('CLASS WRONG LENGTH REJECTED')==2 and all(row['result']==row['error']==row['exit']==expected_error and row['mask']==0 and row['checks']==4 for row in executions),'Deliberate wrong-length rejected by actual max0 assertion')
            else:require(len(semantics)==2 and all(row['result']==row['error']==row['exit']==0 and row['mask']==7 for row in executions) and all(row['strings']==14 and row['paths']==21 and row['lifetimes']==32 and 2<row['allocations']<1026 and row['pages']==257 and row['released']==1 for row in semantics),'Actual guard/lexical/UTF/lifetime/whole LocalPMA and release semantics')
            state['guests'].append({'name':name,'negative':negative,'kernel_sha256':sha(kernel),'user_sha256':sha(elf),'log_sha256':sha(guest),'initial':initial,'executions':executions,'semantics':semantics,'reaps':reaps});emit()
    state['stage']='terminal_all_source_alias_cache_output_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()),'Terminal source/alias changed');require(all((pathlib.Path(path).stat().st_size,pathlib.Path(path).stat().st_mtime_ns)==info for path,info in retained.items()),'Prior cache metadata changed');require(all(sha(path)==item['sha256'] for path,item in state['objects'].items()) and all(sha(item['path'])==item['sha256'] for item in state['kernels'].values()),'New/reused output changed');require(len(state['guests'])==3,'All3 actual guests required')
    state.update(complete=True,native_class_pass=True,strnlen_guard_pass=True,FilePath_UTF_lifetime_pass=True,LocalPMA_class_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,old_cache_metadata_unchanged=True,outputs_unchanged=True,positive_native_contexts=4,wrong_length_rejected_contexts=2,stage='completed',scope='Separate CPL3 class diagnostic, not V8 main: whole LLVM strnlen guard pages; actual unchanged FilePath class lexical/UTF replacement and32 lifetime waves; actual whole LocalPMA allocate/type/iterate/zero/full/257-page VM/dtor-handle invalidation; real aligned startup/constructors/exit and exact current Task/Runtime frame/task/FP/peer recovery. Two32M1/64M4 positive guests and one32M1 explicit diagnostic-API wrong-length control, two launches each. All35 kernel object instructions audited including14 explicit original integer members. BSP only. FilePMA flush, Content/browser/web/video/HTML5 and production image remain unimplemented/unaccepted.');emit();print('PASS separate native class semantics and wrong-length control',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
