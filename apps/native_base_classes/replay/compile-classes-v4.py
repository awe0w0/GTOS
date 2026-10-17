#!/usr/bin/env python3
"""Actual base CXX23 recipe: separate class diagnostics and whole UTF providers."""
import ctypes,hashlib,json,os,pathlib,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';G=P/'gn-02';OUT=P/'class-compile-04';BUILD=OUT/'build';ROOT=OUT/'source';RAW=W/'sources/chromium-full/src';C=W/'artifacts/native-base-closure-20261017-c/compile-01';CC=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang';NM=CC.parent/'llvm-nm';PRIOR=P/'class-compile-02'
TOOLS={CC:'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',NM:'629e43f674d383d8c54d43f3254f80895a380c05a4afcdec27bb03dbb72b74bc'}
state={'complete':False,'diagnostic_CXX_pass':False,'whole_UTF_provider_CXX_pass':False,'native_class_pass':False,'browser_pass':False,'inputs':{},'aliases':{},'source_view_aliases':{},'unqualified_M_inputs':{},'commands':[],'objects':{}};known={};parents={};authorized=False;code=1

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
    logical=pathlib.Path(path);actual=resolve(logical,parents);key=str(actual);digest=known.get(key) if digest is None else digest
    require((W in actual.parents or pathlib.Path('/usr') in actual.parents) and actual.is_file() and digest is not None,'Unbound/escaped input: '+key)
    if key in state['inputs']:require(state['inputs'][key]==digest,'Conflicting source origin')
    else:require(sha(actual)==digest,'Input changed: '+key);state['inputs'][key]=digest
    state['aliases'][str(logical)]=key

def receipt(path,digest):
    bind(path,digest);data=json.loads(path.read_text());require('failure' not in data,'Failed prerequisite');return data

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def view(source,destination,overlays):
    destination.mkdir();children={item.name:item for item in source.iterdir()} if source.is_dir() else {}
    for name in sorted(set(children)|{parts[0] for parts in overlays}):
        if name in ('.git','out','__pycache__'):continue
        selected={parts[1:]:path for parts,path in overlays.items() if parts[0]==name};target=destination/name
        if () in selected:require(len(selected)==1,'Overlay conflict');actual=selected[()].resolve(strict=True)
        elif selected:view(source/name,target,selected);continue
        else:actual=children[name].resolve(strict=True)
        require(W in actual.parents,'Source view escaped workspace');target.symlink_to(actual,target_is_directory=actual.is_dir());state['source_view_aliases'][str(target)]=str(actual)

def run(name,argv):
    argv=[str(value) for value in argv];require(pathlib.Path(argv[0]) in TOOLS and sha(argv[0])==TOOLS[pathlib.Path(argv[0])],'Exact compiler/NM executable');log=OUT/(name+'.log')
    with log.open('xb') as stream:result=subprocess.run(argv,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode,'log_sha256':sha(log)});emit();require(result.returncode==0 and log.stat().st_size<8*1024*1024,'Failed '+name+'; retained output');print('PASS '+name,flush=True)

def dependencies(path,cwd=BUILD):
    text=path.read_text().replace(chr(92)+chr(10),' ');require(text.count(':')==1 and chr(92) not in text and '$' not in text,'Plain complete MD syntax');names=shlex.split(text.split(':',1)[1]);require(0<len(names)<10000,'Bounded dependency count');return {str(resolve(cwd/name,parents)) for name in names}

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated interpreter')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-compile-classes-v4.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and guard['output_symlink_creation_allowed'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    pins=receipt(P/'class-source-pins-01.json','367e29abf4e5f8c6a872acba03e9ab5a7ec11b04de1c41684f10aed186edcb67');require(pins['Complete'] and pins['AuthoredDiagnosticOnly'] and pins['ProgramContainsNoV8Main'] and len(pins['Files'])==5 and len(pins['WholeRawProviders'])==2 and pins['ChromiumRevision']=='04bca2584cddb39e134fc7c16bb2886e02863d0e' and pins['FixedGitLsTreeAndHashObjectNoFiltersMatched'],'Explicit class probe and original provider identities')
    graph=receipt(G/'status.json','26162a0e3447de1370771d8bd29cbaf48d6c7a298cd722479c8e46853555047a');require(all(graph[key] for key in ('complete','gn_generation_pass','native_VM_helper_GN_member','native_strnlen_GN_member','existing_members_fields_order_and_all_tool_rules_preserved','bound_inputs_unchanged','aliases_unchanged')),'Actual complete current Content GN graph')
    path=receipt(P/'path-compile-02/status.json','a14c6846556024e3de7d9c9c8c99fb36d3e11e5a236bce3ad364a636bc237474');require(all(path[key] for key in ('complete','whole_FilePath_CXX_pass','full_M_MD_source_qualification','FilePath_class_header_unchanged','bound_inputs_unchanged','aliases_unchanged')),'Whole original FilePath class and exact base recipe')
    root=receipt(P/'root-link-02/status.json','4ea7930d0c76bc6ea0fdca5f459d91eba763cda3bc7bc0b8fdf69a082f75cb47');require(all(root[key] for key in ('diagnostic_complete','actual_full_V8_link_pass','bound_inputs_unchanged','aliases_unchanged')),'Qualified real provider/header pool')
    base=receipt(C/'status.json','34e139a8ed534e4d3d8f6dce1370b3cc29a59593effd8327b1f58188224fafcf');require(base['all_CXX_pass'] and base['complete_MD_source_qualification'] and base['bound_inputs_unchanged'] and base['aliases_unchanged'],'Exact action-generated header view')
    for record in (graph,path,root,base):
        for key,digest in record['inputs'].items():require(key not in known or known[key]==digest,'Existing immutable pool conflict');known[key]=digest
    headers=receipt(P/'class-header-origins-01.json','039b855a5b7a6aeb104f39a40abc70134a76decfda18cb28a32381319d0efc1f');require(headers['complete'] and headers['fixed_git_ls_tree_and_hash_object_no_filters_matched'] and headers['revision']==pins['ChromiumRevision'] and len(headers['headers'])==2,'Independent raw header Git provenance')
    for relative,row in headers['headers'].items():source=RAW/relative;known[str(source)]=row['sha256'];bind(source,row['sha256'])
    system_headers=receipt(P/'class-system-header-origins-01.json','5bfc20770ac50019540724d1b19bb6d3f0221ea8357745bfff08bdc1fb1f716e');require(system_headers['complete'] and system_headers['revision']==pins['ChromiumRevision'] and system_headers['seven_raw_git_ls_tree_and_no_filters_hashes_matched'] and system_headers['CommandLine_whole_original_header_retained_with_one_GTOS_string_branch'] and len(system_headers['raw_headers'])==7,'Independent whole raw system-header provenance')
    for relative,row in system_headers['raw_headers'].items():source=RAW/relative;known[str(source)]=row['sha256'];bind(source,row['sha256'])
    command_header=pathlib.Path(system_headers['candidate']['path']);require(command_header==W/'artifacts/native-content-entry-20261016-a/patch-command-line-v1/base/command_line.h','Fixed existing native CommandLine header');known[str(command_header)]=system_headers['candidate']['sha256'];bind(command_header);require(command_header.read_text().replace('#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_GTOS)','#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)')==(RAW/'base/command_line.h').read_text(),'Whole original CommandLine retained with exactly the reviewed native string branch')
    prior=receipt(PRIOR/'status.json','f4542dd35275ee314d6caa26777f419154fb2de88a3fbb59efb25c008a7f0f1f');require(all(prior[key] for key in ('complete','diagnostic_CXX_pass','whole_UTF_provider_CXX_pass','full_M_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged')) and len(prior['objects'])==6 and not prior['unqualified_M_inputs'],'Exact completed six-unit prior CXX/MD qualification')
    for key,digest in prior['inputs'].items():require(key not in known or known[key]==digest,'Prior source pool conflict');known[key]=digest;bind(key,digest)
    for group in ('aliases','source_view_aliases'):
        for logical,actual in prior.get(group,{}).items():require(str(resolve(logical,parents))==actual,'Prior immutable alias changed');state['aliases'][logical]=actual
    overlays={}
    for relative,digest in pins['Files'].items():source=P/'patch-class-v1'/relative;known[str(source)]=digest;bind(source,digest);overlays[pathlib.PurePosixPath(relative).parts]=source
    for relative,row in pins['WholeRawProviders'].items():source=RAW/relative;known[str(source)]=row['SHA256'];bind(source,row['SHA256'])
    delta=receipt(P/'class-delta-origins-01.json','a2d6236a07fdb9fe73d6bb7ebd504372100cd115edadcbc9de6994bd5767c166');require(delta['complete'] and delta['whole_original_SysInfo_body_preserved'] and delta['genuine_exit_result_data_only'] and delta['accepted_startup_body_preserved_with_diagnostic_symbol_names'] and delta['raw_revision']==pins['ChromiumRevision'] and len(delta['files'])==3,'Three explicit original-body/platform/startup changes')
    for relative,digest in delta['files'].items():source=P/relative;known[str(source)]=digest;bind(source,digest)
    raw_system=RAW/'base/system/sys_info.cc';known[str(raw_system)]=delta['raw_sha256'];bind(raw_system,delta['raw_sha256']);candidate=P/'patch-system-v1/base/system/sys_info.cc';include_block='#if defined(__GTOS__)\n#include <process/vm_abi.h>\n#endif\n';method_block='#if defined(__GTOS__)\n// GTOS reserves and commits VM ranges in the same fixed ABI page unit.\nsize_t SysInfo::VMAllocationGranularity() {\n  return GTOS_VM_PAGE_BYTES;\n}\n#endif\n\n';body=candidate.read_text();require(body.count(include_block)==body.count(method_block)==1 and body.replace(include_block,'').replace(method_block,'')==raw_system.read_text(),'Entire upstream SysInfo body retained; only real GTOS page-granularity method added')
    overlays[pathlib.PurePosixPath('base/system/sys_info.cc').parts]=candidate;overlays[pathlib.PurePosixPath('tools/gtos_base_class_probe/entry.cc').parts]=P/'patch-class-v2/tools/gtos_base_class_probe/entry.cc'
    for source,digest in TOOLS.items():bind(source,digest)
    owner=G/'build/obj/base/base.ninja';bind(owner,graph['owners']['obj/base/base.ninja']['sha256']);lines=owner.read_text().splitlines();fields=[];original_fields={}
    for key in ('defines','include_dirs','cflags','cflags_cc'):
        values=[line[len(key)+3:] for line in lines if line.startswith(key+' = ')];require(len(values)==1 and values[0]==path['original_fields'][key],'Actual base owner fields unchanged');original_fields[key]=values[0];fields.extend(shlex.split(values[0]))
    require(all(value in fields for value in ('-D__GTOS__=1','-DBASE_IMPLEMENTATION','-DDCHECK_ALWAYS_ON=1','-DNDEBUG','-O2','-std=c++23')) and not any(value.startswith(('@','--sysroot','--config=','-fplugin=','-fpass-plugin=','-ivfsoverlay','-D__linux__','-D__unix__')) for value in fields) and '-load' not in fields and '-wrapper' not in fields,'Native original flags without fake platform/plugin')
    prefix=[str(CC),'-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-mmx','-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-femulated-tls','-fstack-usage','-nostdinc','-nostdinc++','-DGTOS_NATIVE_SDK=1','-isystem','../source/tools/gtos_native_sdk/sdk','-isystem','../source/third_party/libc++/src/include','-isystem','../source/tools/gtos_native_sdk/c-sdk','-isystem','../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include','-fno-exceptions','-fno-rtti']
    tool=G/'build/toolchain.ninja';bind(tool,'adfdf3ef371e22e9a3f14aa01a55045faad93e1bce59f253023ecc63acb0c76c');rules=tool.read_text().splitlines();require(rules.count('rule cxx')==1 and rules[rules.index('rule cxx')+1]=='  command = '+' '.join(prefix)+' ${defines} ${include_dirs} ${cflags} ${cflags_cc} -MD -MF ${out}.d -c ${in} -o ${out}','Exact actual native CXX rule')
    view(G/'source',ROOT,overlays);BUILD.mkdir();gen=resolve(C/'build/gen',parents);require(base['aliases'][str(C/'build/gen')]==str(gen) and W in gen.parents,'Exact action gen source');(BUILD/'gen').symlink_to(gen,target_is_directory=True);state['aliases'][str(BUILD/'gen')]=str(gen)
    for relative in ('build/config/warning_suppression.txt','build/config/raw_ptr_plugin_config.yaml','build/config/unsafe_buffers_paths.txt'):bind(ROOT/relative)
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')};crash='-fcrash-diagnostics-dir=../source/out/clang-crashreports';require(fields.count(crash)==1,'Only crash-output relocation');fields[fields.index(crash)]='-fcrash-diagnostics-dir='+str(OUT/'crash');state['original_fields']=original_fields
    require(prior['original_fields']==original_fields,'Same exact base fields for retained objects')
    for name in ('checks','length','wrong-length','utf_string_conversions','utf_string_conversion_utils'):
        item=prior['objects'][name];oldobj=pathlib.Path(item['path']);expected=P/('class-compile-01' if name in ('checks','length','wrong-length') else 'class-compile-02')/'build/obj/class-probe'/(name+'.o');require(oldobj==expected,'Exact retained original object path');bind(oldobj,item['sha256']);olddep=pathlib.Path(str(oldobj)+'.d');bind(olddep,item['MD_sha256']);deps=dependencies(olddep,oldobj.parents[2]);require(len(deps)==item['M_dependencies'],'Original complete MD dependency count')
        for actual in deps:bind(actual)
        state['objects'][name]=dict(item,reused_from_complete_CXX=True,full_MD_requalified=True);emit();print('PASS requalified existing '+name+' CXX/MD',flush=True)
    units=[('entry','tools/gtos_base_class_probe/entry.cc',False),('sys_info','base/system/sys_info.cc',True)]
    for name,relative,provider in units:
        state['stage']=name+'_complete_M';emit();source='../source/'+relative;output='obj/class-probe/'+name+'.o';obj=BUILD/output;obj.parent.mkdir(parents=True,exist_ok=True);pre=OUT/(name+'.pre.d');dep=BUILD/(output+'.d')
        if provider:
            expected='obj/base/system/libbase.'+name+'.o';members=[line for line in lines if line.startswith('build '+expected+': cxx '+source+' ')];require(len(members)==1 and resolve(ROOT/relative,parents)==candidate,'Whole SysInfo candidate and unchanged actual base GN member/recipe');state.setdefault('actual_provider_GN_rows',{})[name]=members[0]
        run(name+'-M',prefix+fields+['-M','-MT',output,'-MF',str(pre),source]);before=dependencies(pre)
        for actual in before:
            if actual in known:bind(actual,known[actual])
            else:state['unqualified_M_inputs'][actual]=sha(actual)
        emit();require(not state['unqualified_M_inputs'],'New full-M input requires independent provenance before CXX')
        if name=='checks':require(str(W/'artifacts/native-content-entry-20261016-a/patch-file-types-v1/base/files/file_path.h') in before and str(RAW/'base/metrics/persistent_memory_allocator.h') in before,'Real unchanged FilePath/PMA class headers')
        run(name+'-CXX',prefix+fields+['-MD','-MF',output+'.d','-c',source,'-o',output]);require(dependencies(dep)==before,'Exact complete M/MD');data=obj.read_bytes();require(len(data)>=52 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Actual ELF32/i386 relocatable');run(name+'-undefined',[NM,'--undefined-only','--format=posix',obj]);run(name+'-defined',[NM,'--defined-only','--format=posix',obj]);state['objects'][name]={'path':str(obj),'sha256':sha(obj),'bytes':len(data),'MD_sha256':sha(dep),'M_dependencies':len(before),'original_whole_provider':provider,'diagnostic_only':not provider,'undefined_log_sha256':sha(OUT/(name+'-undefined.log'))};emit()
    startup=P/'patch-class-v2/tools/gtos_base_class_probe/start.s';require(not any(token in startup.read_text() for token in ('.include','.incbin','#include')),'Standalone fixed alignment startup');obj=BUILD/'obj/class-probe/start.o';run('start-ASM',[CC,'--target=i686-unknown-none-elf','-x','assembler','-c',startup,'-o',obj]);data=obj.read_bytes();require(len(data)>=52 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Actual ELF32/i386 startup');run('start-undefined',[NM,'--undefined-only','--format=posix',obj]);run('start-defined',[NM,'--defined-only','--format=posix',obj]);require((OUT/'start-undefined.log').read_text().splitlines()==['NativeClassEntry U 0 0'],'Startup only calls real CXX class entry');state['objects']['start']={'path':str(obj),'sha256':sha(obj),'bytes':len(data),'source':str(startup),'diagnostic_only':True,'standalone_ASM':True}
    state['stage']='terminal_input_alias_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for group in ('aliases','source_view_aliases') for logical,actual in state[group].items()),'Terminal input/alias check')
    state.update(complete=True,diagnostic_CXX_pass=True,whole_UTF_provider_CXX_pass=True,whole_SysInfo_candidate_CXX_pass=True,standalone_aligned_startup_ASM_pass=True,full_M_MD_source_qualification=True,bound_inputs_unchanged=True,aliases_unchanged=True,stage='completed',scope='Five complete prior objects requalified unchanged; entry gains only actual exit result data; standalone accepted call-alignment startup renamed for separate class diagnostic. Whole original SysInfo body retained with real GTOS VM page-granularity method, unchanged actual base member/flags. Full CXX M/MD checked. No linked user ISA/native guard/path/UTF/PMA or browser acceptance; no production graph/source promotion.');emit();print('PASS class entry, whole SysInfo candidate and aligned startup CXX/ASM',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
