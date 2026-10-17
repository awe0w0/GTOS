#!/usr/bin/env python3
"""Compile exact new formal GN consumer/ASM rows and unchanged SysInfo base row."""
import ctypes,hashlib,json,os,pathlib,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';G=P/'gn-04';OUT=P/'class-formal-01';BUILD=OUT/'build';ROOT=OUT/'source';C=W/'artifacts/native-base-closure-20261017-c/compile-01';CC=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang';NM=CC.parent/'llvm-nm'
GN_SHA='ba9da40458d39d85479f9ec2df4bc21c3a9afcb670810b74534fb098ba2dba6f'
TOOLS={CC:'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',NM:'629e43f674d383d8c54d43f3254f80895a380c05a4afcdec27bb03dbb72b74bc'}
state={'complete':False,'formal_CXX_ASM_pass':False,'native_replay':False,'browser_pass':False,'inputs':{},'aliases':{},'commands':[],'objects':{},'unqualified_M_inputs':{}};parents={};known={};authorized=False;code=1

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
    actual=resolve(path,parents);key=str(actual);digest=known.get(key) if digest is None else digest;require(W in actual.parents and actual.is_file() and digest is not None,'Unqualified input: '+key)
    if key in state['inputs']:require(state['inputs'][key]==digest,'Source conflict')
    else:require(sha(actual)==digest,'Changed input: '+key);state['inputs'][key]=digest
    state['aliases'][str(path)]=key

def receipt(path,digest):
    bind(path,digest);value=json.loads(path.read_text());require('failure' not in value,'Failed prerequisite');return value

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def run(name,argv):
    argv=[str(value) for value in argv];require(pathlib.Path(argv[0]) in TOOLS and sha(argv[0])==TOOLS[pathlib.Path(argv[0])],'Only exact compiler/NM');log=OUT/(name+'.log')
    with log.open('xb') as stream:result=subprocess.run(argv,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode,'log_sha256':sha(log)});emit();require(result.returncode==0 and log.stat().st_size<8*1024*1024,'Failed '+name+'; inspect retained log');print('PASS '+name,flush=True)

def dependencies(path):
    text=path.read_text().replace(chr(92)+chr(10),' ');require(text.count(':')==1 and chr(92) not in text and '$' not in text,'Plain complete MD syntax');names=shlex.split(text.split(':',1)[1]);require(0<len(names)<10000,'Bounded dependency count');return {str(resolve(BUILD/name,parents)) for name in names}

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-compile-formal-classes-v1.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and guard['output_symlink_creation_allowed'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    require(len(GN_SHA)==64,'Terminal formal graph review required');graph=receipt(G/'status.json',GN_SHA);require(all(graph[key] for key in ('complete','gn_generation_pass','formal_SysInfo_candidate_member','formal_class_diagnostic_compile_targets','existing_members_fields_order_and_all_tool_rules_preserved','typed_GN_dependency_qualification','bound_inputs_unchanged','aliases_unchanged','directory_identity_unchanged')),'Whole Content plus actual native diagnostic targets')
    prior=receipt(P/'class-compile-04/status.json','6fe51eee798fd3ab4b5462f5b763b2e1bdb49ab48535b7b71cae392295cc8cfc');require(prior['complete'] and prior['full_M_MD_source_qualification'] and prior['bound_inputs_unchanged'] and prior['aliases_unchanged'],'Prior real source and object identities')
    root=receipt(P/'class-link-03/status.json','f5218569cbfe1f22ab490508ec04f5ff53d07bc90a5f4b976b10801741564846');require(root['complete'] and root['bound_inputs_unchanged'] and root['aliases_unchanged'],'Qualified real dependency pool')
    base=receipt(C/'status.json','34e139a8ed534e4d3d8f6dce1370b3cc29a59593effd8327b1f58188224fafcf');require(base['all_CXX_pass'] and base['complete_MD_source_qualification'] and base['bound_inputs_unchanged'] and base['aliases_unchanged'],'Original action-generated header source')
    for record in (graph,prior,root,base):
        for key,digest in record['inputs'].items():require(key not in known or known[key]==digest,'Immutable origin conflict');known[key]=digest
    for path,digest in TOOLS.items():bind(path,digest)
    BUILD.mkdir();(ROOT).symlink_to(G/'source',target_is_directory=True);state['aliases'][str(ROOT)]=str((G/'source').resolve(strict=True));gen=resolve(C/'build/gen',parents);require(base['aliases'][str(C/'build/gen')]==str(gen),'Exact action-generated gen view');(BUILD/'gen').symlink_to(gen,target_is_directory=True);state['aliases'][str(BUILD/'gen')]=str(gen)
    for relative in ('build/config/warning_suppression.txt','build/config/raw_ptr_plugin_config.yaml','build/config/unsafe_buffers_paths.txt'):bind(ROOT/relative)
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    cxx=[str(CC),'-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-mmx','-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-femulated-tls','-fstack-usage','-nostdinc','-nostdinc++','-DGTOS_NATIVE_SDK=1','-isystem','../source/tools/gtos_native_sdk/sdk','-isystem','../source/third_party/libc++/src/include','-isystem','../source/tools/gtos_native_sdk/c-sdk','-isystem','../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include','-fno-exceptions','-fno-rtti'];assembly=[str(CC),'--target=i686-unknown-none-elf','-nostdinc'];tool=G/'build/toolchain.ninja';bind(tool,graph['native_toolchain_sha256']);rules=tool.read_text().splitlines();require(rules.count('rule cxx')==rules.count('rule asm')==1 and rules[rules.index('rule cxx')+1]=='  command = '+' '.join(cxx)+' ${defines} ${include_dirs} ${cflags} ${cflags_cc} -MD -MF ${out}.d -c ${in} -o ${out}' and rules[rules.index('rule asm')+1]=='  command = '+' '.join(assembly)+' ${defines} ${include_dirs} ${asmflags} -MD -MF ${out}.d -c ${in} -o ${out}','Exact formal CXX/ASM tools, no command replay')
    units=[('sys_info','obj/base/base.ninja','base/system/sys_info.cc','obj/base/system/libbase.sys_info.o',False)]+[(name,'obj/tools/gtos_base_class_probe/probe.ninja','tools/gtos_base_class_probe/'+name+'.cc','obj/tools/gtos_base_class_probe/probe.'+name+'.o',False) for name in ('checks','entry','length')]+[('wrong-length','obj/tools/gtos_base_class_probe/wrong_length.ninja','tools/gtos_base_class_probe/wrong-length.cc','obj/tools/gtos_base_class_probe/wrong_length.wrong-length.o',False),('start','obj/tools/gtos_base_class_probe/probe.ninja','tools/gtos_base_class_probe/start.s','obj/tools/gtos_base_class_probe/probe.start.o',True)]
    for name,owner,relative,output,is_asm in units:
        state['stage']=name+'_formal_source_qualification';emit();owner_path=G/'build'/owner;bind(owner_path,graph['owners'][owner]['sha256']);lines=owner_path.read_text().splitlines();fields=graph['owners'][owner]['asm_fields' if is_asm else 'fields'];flags=[]
        for key in (('defines','include_dirs','asmflags') if is_asm else ('defines','include_dirs','cflags','cflags_cc')):
            values=[line[len(key)+3:] for line in lines if line.startswith(key+' = ')];require(len(values)==1 and values[0]==fields[key],'Actual formal owner field');flags.extend(shlex.split(values[0]))
        source='../source/'+relative;marker=': asm ' if is_asm else ': cxx ';members=[line for line in lines if line.startswith('build '+output+marker+source+' ')];require(len(members)==1,'Exact unique formal member row');require(not any(value.startswith(('@','--sysroot','--config=','-fplugin=','-fpass-plugin=','-ivfsoverlay','-D__linux__','-D__unix__')) for value in flags) and '-load' not in flags,'No host/plugin/path substitution')
        if not is_asm:
            require(all(value in flags for value in ('-D__GTOS__=1','-DNDEBUG','-DDCHECK_ALWAYS_ON=1','-O2','-std=c++23')),'Unchanged native CXX23/hardening configuration');crash='-fcrash-diagnostics-dir=../source/out/clang-crashreports';require(flags.count(crash)==1,'Only crash-output relocation');flags[flags.index(crash)]='-fcrash-diagnostics-dir='+str(OUT/'crash')
        obj=BUILD/output;obj.parent.mkdir(parents=True,exist_ok=True);dep=pathlib.Path(str(obj)+'.d');bind(ROOT/relative);before=None
        if is_asm:require(not any(token in (ROOT/relative).read_text() for token in ('.include','.incbin','#include')),'Standalone original alignment ASM only')
        else:
            pre=OUT/(name+'.pre.d');run(name+'-M',cxx+flags+['-M','-MT',output,'-MF',str(pre),source]);before=dependencies(pre)
            for actual in before:
                if actual in known:bind(actual,known[actual])
                else:state['unqualified_M_inputs'][actual]=sha(actual)
            emit();require(not state['unqualified_M_inputs'],'New full-M dependency needs independent origin before CXX')
        run(name+('-ASM' if is_asm else '-CXX'),(assembly if is_asm else cxx)+flags+['-MD','-MF',output+'.d','-c',source,'-o',output]);after=dependencies(dep);require(after==({str(resolve(ROOT/relative,parents))} if is_asm else before),'Actual complete M/MD/standalone-ASM qualification');data=obj.read_bytes();require(len(data)>=52 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Formal ELF32/i386 object');run(name+'-undefined',[NM,'--undefined-only','--format=posix',obj]);run(name+'-defined',[NM,'--defined-only','--format=posix',obj]);old=prior['objects'][name];bind(old['path'],old['sha256']);digest=sha(obj);state['objects'][name]={'path':str(obj),'sha256':digest,'bytes':len(data),'source':str(resolve(ROOT/relative,parents)),'actual_GN_owner':owner,'actual_GN_row':members[0],'MD_sha256':sha(dep),'dependencies':sorted(after),'same_bytes_as_native_qualified_prior':digest==old['sha256'],'prior_path':old['path'],'prior_sha256':old['sha256']};emit()
    state['stage']='terminal_effective_input_alias_object_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()) and all(sha(item['path'])==item['sha256'] for item in state['objects'].values()),'Input/alias/output changed')
    state.update(complete=True,formal_CXX_ASM_pass=True,full_M_MD_source_qualification=True,bound_inputs_unchanged=True,aliases_unchanged=True,outputs_unchanged=True,all_objects_identical_to_prior=all(item['same_bytes_as_native_qualified_prior'] for item in state['objects'].values()),stage='completed',scope='Exact formal GN owner/tool rules compile three actual native probe CXX, one wrong control CXX, one original SysInfo base CXX and one aligned ASM. Only crash diagnostics relocated. Full M/MD and standalone ASM dependencies/inputs bound. Prior byte identity recorded per object; differences require review/ISA/native verification before inheriting prior runtime. No whole Content compile/browser or public promotion claim.');emit();print('PASS formal class/SysInfo CXX and ASM',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
