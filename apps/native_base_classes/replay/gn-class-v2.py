#!/usr/bin/env python3
"""Full Content plus separate real-class compile targets and genuine SysInfo view."""
import ctypes,hashlib,json,os,pathlib,re,shlex,stat,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';G=P/'gn-02';OUT=P/'gn-04';ROOT=OUT/'source';BUILD=OUT/'build';GN=W/'toolchains/gn/gn'
ARGS='''target_os="gtos"
target_cpu="x86"
is_debug=false
is_component_build=false
use_crash_key_stubs=true
use_kerberos=false
use_official_google_api_keys=false
use_glib=false
use_dbus=false
use_gio=false
use_udev=false
use_pangocairo=false
use_gtk=false
use_cups=false
rtc_use_pipewire=false
is_clang=true
use_sysroot=false
use_ozone=false
symbol_level=0
action_pool_depth=1
concurrent_links=1
gtos_clang_bin="/mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin"
gtos_python="/usr/bin/python3"
gtos_native_sdk="//tools/gtos_native_sdk"
gtos_clang_resource_dir="/mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include"
gtos_compiler_builtins="/mnt/f/GTOS-Chromium/sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a"
'''
state={'complete':False,'gn_generation_pass':False,'formal_SysInfo_candidate_member':False,'formal_class_diagnostic_compile_targets':False,'native_class_rerun':False,'browser_pass':False,'inputs':{},'aliases':{},'source_view_aliases':{},'GN_aliases':{},'unbound_GN_inputs':{},'directory_metadata':{},'current_GN_build_data':{},'owners':{}};parents={};known={};authorized=False;code=1

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def resolve(path,cache):
    path=pathlib.Path(path)
    if path.name in ('','.','..'):return path.resolve(strict=True)
    key=str(path.parent)
    if key not in cache:cache[key]=path.parent.resolve(strict=True)
    actual=cache[key]/path.name
    if actual.is_symlink():actual=actual.resolve(strict=True)
    actual.stat();return actual

def bind(path,digest):
    actual=resolve(path,parents);key=str(actual);require(W in actual.parents and actual.is_file(),'Input escaped workspace')
    if key in state['inputs']:require(state['inputs'][key]==digest,'Conflicting source identity')
    else:require(sha(actual)==digest,'Changed input: '+key);state['inputs'][key]=digest
    state['aliases'][str(path)]=key

def receipt(path,digest):
    bind(path,digest);value=json.loads(path.read_text());require('failure' not in value,'Failed prerequisite');return value

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def view(source,dest,overlays):
    dest.mkdir();children={item.name:item for item in source.iterdir()} if source.is_dir() else {}
    for name in sorted(set(children)|{parts[0] for parts in overlays}):
        if name in ('.git','out','__pycache__'):continue
        selected={parts[1:]:path for parts,path in overlays.items() if parts[0]==name};target=dest/name
        if () in selected:require(len(selected)==1,'Overlapping overlay');actual=selected[()].resolve(strict=True)
        elif selected:view(source/name,target,selected);continue
        else:actual=children[name].resolve(strict=True)
        require(W in actual.parents,'Source view escaped');target.symlink_to(actual,target_is_directory=actual.is_dir());state['source_view_aliases'][str(target)]=str(actual)

def owner(path,assembly=False):
    lines=path.read_text().splitlines();fields={}
    for key in (('defines','include_dirs','asmflags') if assembly else ('defines','include_dirs','cflags','cflags_cc')):
        found=[line[len(key)+3:] for line in lines if line.startswith(key+' = ')];require(len(found)==1,'Unique owner field');fields[key]=found[0]
    marker=': asm ../source/' if assembly else ': cxx ../source/'
    return fields,[line for line in lines if line.startswith('build obj/') and marker in line]

def rule(path,name):
    lines=path.read_text().splitlines();require(lines.count('rule '+name)==1,'Unique native tool rule');at=lines.index('rule '+name);end=at+1
    while end<len(lines) and lines[end].startswith('  '):end+=1
    return lines[at:end]

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-gn-class-v2.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and guard['output_symlink_creation_allowed'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    bind(GN,'c0dec98e11c4008b165a44bf407e06704710cf76e2af4aea22dd3b7102f0778e')
    old=receipt(G/'status.json','26162a0e3447de1370771d8bd29cbaf48d6c7a298cd722479c8e46853555047a');require(all(old[key] for key in ('complete','gn_generation_pass','GN_input_qualification','typed_GN_dependency_qualification','bound_inputs_unchanged','aliases_unchanged','directory_identity_unchanged','native_VM_helper_GN_member','native_strnlen_GN_member','existing_members_fields_order_and_all_tool_rules_preserved')) and (old['GN_targets'],old['GN_files'])==(21495,2516) and old['argv'][7]=='--args='+ARGS,'Exact complete prior Content graph and unchanged arguments')
    compiled=receipt(P/'class-compile-04/status.json','6fe51eee798fd3ab4b5462f5b763b2e1bdb49ab48535b7b71cae392295cc8cfc');require(all(compiled[key] for key in ('complete','whole_SysInfo_candidate_CXX_pass','standalone_aligned_startup_ASM_pass','full_M_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged')),'Genuine whole SysInfo candidate and actual class sources')
    native=receipt(P/'class-guest-03/status.json','903445bb3db71a55bd89d4c952a7ef2616381bcbd477d330e651861dce774961');require(all(native[key] for key in ('complete','native_class_pass','strnlen_guard_pass','FilePath_UTF_lifetime_pass','LocalPMA_class_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged','outputs_unchanged')) and native['positive_native_contexts']==4 and native['wrong_length_rejected_contexts']==2,'Actual6-context class acceptance remains scoped independently')
    pins=receipt(P/'gn-source-pins-04.json','738283256bbcca58d1b1a92a0e9ef8135914556d8efc5a519ecd7bb5ab86f2d0');require(pins['complete'] and len(pins['files'])==8 and not pins['file_flush_provider_implemented'],'Exactly one source replacement and seven diagnostic files')
    known.update(old['inputs'])
    for path,digest in compiled['inputs'].items():require(path not in known or known[path]==digest,'Existing immutable origin conflict');known[path]=digest
    overlays={('content-diagnostic.gn',):P/'content-dotfile-v4.gn'}
    for relative,item in pins['files'].items():bind(item['path'],item['sha256']);known[item['path']]=item['sha256'];overlays[pathlib.PurePosixPath(relative).parts]=pathlib.Path(item['path'])
    for name,key in (('content-helper-v4.py','helper_sha256'),('content-dotfile-v4.gn','dotfile_sha256')):bind(P/name,pins[key]);known[str(P/name)]=pins[key]
    old_helper=P/'content-helper-v2.py';old_dot=P/'content-dotfile-v2.gn';bind(old_helper,'e80ee2d017d3599a09cdc0e68b564e9c6debc8830576c6dc50d026f2878fdbc8');bind(old_dot,'f8764b795a581acdafe9824d3e9bbf83a6790157387eb0bcbd51174f8bd28d3f')
    new_helper=(P/'content-helper-v4.py').read_text();new_dot=(P/'content-dotfile-v4.gn').read_text();old_out="OUT=W/'artifacts/native-pma-20261017-a/gn-02'";new_out="OUT=W/'artifacts/native-pma-20261017-a/gn-04'";require(new_helper.count(new_out)==1 and new_helper.replace(new_out,old_out)==old_helper.read_text(),'Only fixed helper output location changed, no generated executable code');require(new_dot.replace('content-helper-v4.py','content-helper-v2.py')==old_dot.read_text(),'Only fixed helper path changed, same three-call exec_script allowlist')
    base_pin=receipt(P/'gn-source-pins-02.json','b918d30c8c42aa49fc17f29515b36b8bd53809c96d466204be1eded0bb8b2528')
    for item in base_pin['files'].values():bind(item['path'],item['sha256'])
    bind(G/'build/args.gn',old['current_GN_build_data'][str(G/'build/args.gn')]);args_sha=sha(G/'build/args.gn');state['stage']='create_fresh_formal_class_source_view';emit();view(G/'source',ROOT,overlays);BUILD.mkdir()
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')};root_label='//tools/gtos_base_class_probe:qualification_root';argv=[str(GN),'gen',str(BUILD),'--root='+str(ROOT),'--dotfile='+str(ROOT/'content-diagnostic.gn'),'--root-target='+root_label,'--root-pattern='+root_label,'--args='+ARGS,'--fail-on-unused-args'];state.update(stage='actual_full_Content_plus_class_GN',argv=argv);emit()
    with (OUT/'GN.log').open('xb') as stream:result=subprocess.run(argv,cwd=ROOT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    state['GN_returncode']=result.returncode;emit();require(result.returncode==0,'Full Content plus class GN failed');counts=re.search(r'Made (\d+) targets from (\d+) files',(OUT/'GN.log').read_text());require(counts and tuple(map(int,counts.groups()))==(pins['expected_GN_targets'],pins['expected_GN_files']),'Exactly three additional targets and one BUILD file')
    for name in ('timestamp','branding','chrome-version'):
        path=OUT/(name+'-calls.jsonl');records=[json.loads(line) for line in path.read_text().splitlines()];require(len(records)==1 and records[0]['bound_inputs_unchanged'] and records[0]['returncode']==0,'Exactly three qualified fixed helpers');state.setdefault('helper_receipts',{})[name]={'sha256':sha(path),'records':records}
    for name,item in old['owners'].items():
        bind(G/'build'/name,item['sha256']);assembly=name=='obj/third_party/boringssl/boringssl_asm.ninja';require(owner(G/'build'/name,assembly)==owner(BUILD/name,assembly),'Existing owner fields/members/order changed: '+name);state['owners'][name]={'sha256':sha(BUILD/name),'fields':owner(BUILD/name,assembly)[0],'all_members_unchanged':True}
    base='obj/base/base.ninja';old_archive=[line for line in (G/'build'/base).read_text().splitlines() if line.startswith('build obj/base/libbase.a: ')];new_archive=[line for line in (BUILD/base).read_text().splitlines() if line.startswith('build obj/base/libbase.a: ')];require(len(old_archive)==1 and new_archive==old_archive,'Complete existing base archive members/dependencies/order unchanged')
    for name in ('cc','cxx','asm','alink','link','stamp','copy'):require(rule(G/'build/toolchain.ninja',name)==rule(BUILD/'toolchain.ninja',name),'Existing native tool rule changed: '+name)
    sysinfo=ROOT/'base/system/sys_info.cc';require(resolve(sysinfo,parents)==pathlib.Path(pins['files']['base/system/sys_info.cc']['path']),'Actual formal SysInfo source resolves to exact native candidate');bind(sysinfo,pins['files']['base/system/sys_info.cc']['sha256']);require(len([line for line in (BUILD/base).read_text().splitlines() if line.startswith('build obj/base/system/libbase.sys_info.o: cxx ../source/base/system/sys_info.cc ')])==1,'Existing genuine base SysInfo member retained')
    for target,expected in (('probe',{'checks.cc','entry.cc','length.cc'}),('wrong_length',{'wrong-length.cc'})):
        name='obj/tools/gtos_base_class_probe/'+target+'.ninja';fields,members=owner(BUILD/name);sources={line.split(': cxx ',1)[1].split()[0] for line in members};require(sources=={'../source/tools/gtos_base_class_probe/'+source for source in expected} and len(members)==len(expected),'Exact new diagnostic CXX membership');flags=[token for value in fields.values() for token in shlex.split(value)];require(all(token in flags for token in ('-D__GTOS__=1','-DNDEBUG','-DDCHECK_ALWAYS_ON=1','-O2','-std=c++23')) and not any(token.startswith(('@','--sysroot','--config=','-fplugin=','-fpass-plugin=','-ivfsoverlay','-D__linux__','-D__unix__')) for token in flags) and '-load' not in flags,'Native consumer flags, no host/plugin substitutions');state['owners'][name]={'sha256':sha(BUILD/name),'fields':fields,'new_CXX_members':members}
    probe='obj/tools/gtos_base_class_probe/probe.ninja';asmfields,asmmembers=owner(BUILD/probe,True);require(len(asmmembers)==1 and asmmembers[0].split(': asm ',1)[1].split()[0]=='../source/tools/gtos_base_class_probe/start.s','Actual unchanged assembler rule compiles exactly the aligned startup');state['owners'][probe].update(asm_fields=asmfields,new_ASM_members=asmmembers)
    for relative,item in pins['files'].items():require(resolve(ROOT/relative,parents)==pathlib.Path(item['path']),'Formal source view selected another class body');bind(ROOT/relative,item['sha256'])
    state['stage']='qualify_every_actual_GN_dependency';emit();bind(BUILD/'args.gn',args_sha);dep=(BUILD/'build.ninja.d').read_text().replace(chr(92)+chr(10),' ');require(chr(92) not in dep and '$' not in dep,'Plain GN dependency syntax');names=dep.split(':',1)[1].split();require(0<len(names)<10000,'Bounded complete GN dependency list')
    for name in names:
        logical=BUILD/name;actual=resolve(logical,parents);key=str(actual);require(W in actual.parents,'GN input escaped')
        if actual.is_dir():
            metadata=actual.stat();state['directory_metadata'][str(logical)]={'actual':key,'st_dev':metadata.st_dev,'st_ino':metadata.st_ino,'file_type':stat.S_IFMT(metadata.st_mode)};continue
        require(actual.is_file(),'Regular GN input required')
        if BUILD in actual.parents:
            ancestor=str(G/'build'/actual.relative_to(BUILD));require(ancestor in old['current_GN_build_data'],'New generated GN input filename requires independent review');digest=sha(actual);bind(logical,digest);state['current_GN_build_data'][key]=digest
        elif key in state['inputs']:bind(logical,state['inputs'][key])
        elif key in known:bind(logical,known[key])
        else:state['unbound_GN_inputs'][key]=sha(actual);continue
        state['GN_aliases'][str(logical)]=key
    state['GN_dependency_count']=len(names);emit();require(not state['unbound_GN_inputs'],'New GN inputs require independent source qualification')
    state['stage']='terminal_actual_GN_inputs_aliases_directories';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for group in ('aliases','source_view_aliases','GN_aliases') for logical,actual in state[group].items()),'GN input/alias changed')
    for logical,item in state['directory_metadata'].items():
        actual=resolve(logical,fresh);metadata=actual.stat();require(str(actual)==item['actual'] and (metadata.st_dev,metadata.st_ino,stat.S_IFMT(metadata.st_mode))==(item['st_dev'],item['st_ino'],item['file_type']),'GN directory identity/type changed')
    state.update(complete=True,gn_generation_pass=True,GN_input_qualification=True,typed_GN_dependency_qualification=True,formal_SysInfo_candidate_member=True,formal_class_diagnostic_compile_targets=True,existing_members_fields_order_and_all_tool_rules_preserved=True,bound_inputs_unchanged=True,aliases_unchanged=True,directory_identity_unchanged=True,native_toolchain_sha256=sha(BUILD/'toolchain.ninja'),GN_targets=pins['expected_GN_targets'],GN_files=pins['expected_GN_files'],stage='completed',scope='Full Content graph retained under a new qualification group; only two separate source_set class compile targets and their root group added. Whole SysInfo candidate uses its existing original base member; no base member/config/archive/rule changes. Fixed three qualified GN helper calls. New actual consumer CXX/ASM rows and full typed GN dependency/alias/directory checks. Compile targets only: no GN executable link, new CXX/MD or native replay/browser acceptance inferred.');emit();print('PASS full Content plus formal class/SysInfo GN graph',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
