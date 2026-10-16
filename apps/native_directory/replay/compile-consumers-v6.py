#!/usr/bin/env python3
"""Exact native GN provider CXX and full original TracingMuxer preflight."""
import ctypes,hashlib,json,os,pathlib,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-directory-20261017-a'
E=W/'artifacts/native-getenv-20261017-a';B=W/'artifacts/native-perfetto-time-20261016-a'
G=P/'content-graph-01';OUT=P/'compile-06';BUILD=OUT/'build';ROOT=OUT/'source'
CC=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
GRAPH_SHA='12c1f279e8766bbbecb83bd80dc19139fbf69acc2c33fa06b044cd8f0762cdd1'
state={'diagnostic_complete':False,'objects_qualified':False,'full_muxer_M_pass':False,'full_muxer_CXX_pass':False,'native_C_API_guest_pass':False,'browser_pass':False,
       'inputs':{},'aliases':{},'MD_aliases':{},'objects':{},'failed_rows':{},'unbound_MD_inputs':{},'commands':[]}
allowed={};resolved={};authorized=False;exit_code=1
def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def actual(path):
    key=str(path)
    if key not in resolved:resolved[key]=pathlib.Path(path).resolve(strict=True)
    return resolved[key]
def bind(path,digest):
    logical=pathlib.Path(path);selected=actual(logical);key=str(selected)
    require(W in selected.parents and selected.is_file(),'Input escaped workspace')
    require(key not in state['inputs'] or state['inputs'][key]==digest,'Input conflict')
    if key not in state['inputs']:require(sha(selected)==digest,'Changed input: '+key)
    state['inputs'][key]=digest;state['aliases'][str(logical)]=key
def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def view(source,destination,overlays):
    require(source.is_dir() and overlays,'Existing branch and explicit overlay required')
    destination.mkdir();children={child.name:child for child in source.iterdir()}
    for name in sorted(set(children)|{parts[0] for parts in overlays}):
        if name in ('.git','out','__pycache__'):continue
        selected={parts[1:]:path for parts,path in overlays.items() if parts[0]==name};target=destination/name
        if () in selected:require(len(selected)==1,'Overlapping overlay');path=actual(selected[()])
        elif selected:view(source/name,target,selected);continue
        else:path=actual(children[name])
        require(W in path.parents,'View escaped workspace')
        target.symlink_to(path,target_is_directory=path.is_dir());state['aliases'][str(target)]=str(path)
def receipt(path,digest,checks):
    bind(path,digest);value=json.loads(path.read_text())
    require(all(value[key] for key in checks) and 'failure' not in value,'Prior qualification incomplete')
    for key,expected in value['inputs'].items():
        require(key not in allowed or allowed[key]==expected,'Source receipt conflict');allowed[key]=expected
    return value
def run(argv,name):
    argv=[str(v) for v in argv];path=OUT/(name+'.log')
    with path.open('xb') as stream:
        result=subprocess.run(argv,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
            stdout=stream,stderr=subprocess.STDOUT,timeout=180)
    require(path.stat().st_size<8*1024*1024,'Unbounded compiler log')
    state['commands'].append({'argv':argv,'returncode':result.returncode,'log':str(path),'sha256':sha(path)});emit();return result.returncode
def dependencies(path,obj):
    text=path.read_text().replace(chr(92)+chr(10),' ')
    require(text.startswith(str(obj)+': ') and chr(92) not in text and '$' not in text,'Unexpected MD format')
    names=text.split(':',1)[1].split();require(0<len(names)<10000 and len(names)==len(set(names)),'Unexpected MD count')
    aliases={};unbound=[]
    for name in names:
        logical=BUILD/name;selected=actual(logical);key=str(selected)
        require(W in selected.parents and selected.is_file(),'MD escaped workspace')
        if key in allowed:bind(logical,allowed[key])
        else:
            if key not in state['unbound_MD_inputs']:state['unbound_MD_inputs'][key]={'logical':str(logical),'sha256':sha(selected),'bytes':selected.stat().st_size}
            bind(logical,state['unbound_MD_inputs'][key]['sha256']);unbound.append(key)
        aliases[str(logical)]=key
    return aliases,unbound
try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-consumers-v6.py')
        and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
        and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    bind(CC,'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7')
    graph=receipt(G/'status.json',GRAPH_SHA,('diagnostic_complete','gn_generation_pass','bound_inputs_unchanged','input_symlinks_unchanged','source_view_aliases_unchanged','native_CXX_rule_unchanged','actual_content_directory_dependency'))
    require(graph['GN_targets']==21493 and graph['GN_files']==2515,'Actual full Content directory graph required')
    receipt(E/'source-qualified-01/status.json','cf6ae107c025966c18606d133bb65420f120a533b58ea677d8b88eb926c0b055',('diagnostic_complete','source_view_qualification_pass','bound_inputs_unchanged','aliases_unchanged'))
    receipt(E/'providers-02/status.json','4d36b0f775ae5a9d2dafcd7413b8480183d01fb09d64b97ccf8ffa9de301c3f3',('diagnostic_complete','objects_qualified','complete_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged','MD_aliases_unchanged'))
    bind(P/'compiler-config-pins-01.json','696d6e6fe21c0cf47cde6ac00e3e58784d344b20a665d08af82acd8d30ac6f2a')
    config_pins=json.loads((P/'compiler-config-pins-01.json').read_text())
    require(config_pins['revision']=='04bca2584cddb39e134fc7c16bb2886e02863d0e' and set(config_pins['files'])=={
        'build/config/unsafe_buffers_paths.txt','build/config/raw_ptr_plugin_config.yaml','build/config/warning_suppression.txt'},'Exact original compiler policy files required')
    for relative,value in config_pins['files'].items():
        require(value['path']==str(W/'sources/chromium-full/src'/relative),'Unexpected policy origin');bind(value['path'],value['sha256']);allowed[value['path']]=value['sha256']
    bind(B/'next-muxer-profile-01.json','519272187b540ef19a9b19c89caae861be4f40cb5bbeeda26412c43983fb366e')
    profile=json.loads((B/'next-muxer-profile-01.json').read_text());require(profile['source_owner_inventory_complete'] and len(profile['rows'])==1,'Original raw consumer inventory required')
    item=profile['rows'][0];require(item['name']=='tracing_muxer_impl' and item['revision']=='fd9c7de6e0c57bee61ee1a4113e60af975881cee'
        and item['sha256']=='6097332acf94d76dcb6400c51f6bf9fb07bbc65c1b66f41b55b6dcc97b0b2a7d','Exact original complete Muxer required')
    bind(item['path'],item['sha256']);allowed[item['path']]=item['sha256']
    bind(P/'thread-checker-origins-01.json','60a9896c359a16bb694b46c2df68dadd6dca0ca90ead1d2988a5f73122eb3c47')
    origins=json.loads((P/'thread-checker-origins-01.json').read_text());require(origins['revision']=='fd9c7de6e0c57bee61ee1a4113e60af975881cee','Original ThreadChecker revision required')
    patches={'third_party/perfetto/include/perfetto/ext/base/thread_checker.h':'c953e9f9d7697a0f3c95d7af7f90177374428261227952a5ab74ed21915d07af',
             'third_party/perfetto/src/base/thread_checker.cc':'ba87b4b9c64a5dc7a03c5c1a7ecd849b3225277b15cc0680f1837bdd57a1f320'}
    require(set(origins['files'])==set(patches),'Exact native checker pair required')
    overlays={}
    for relative,digest in patches.items():
        original=origins['files'][relative];bind(original['path'],original['sha256']);path=P/'patch'/relative;bind(path,digest)
        require(str(path) not in allowed,'Unexpected checker snapshot conflict');allowed[str(path)]=digest;overlays[pathlib.PurePosixPath(relative).parts]=path
    service=origins['native_services'];bind(service['path'],service['sha256']);allowed[service['path']]=service['sha256']
    require(actual(G/'source/tools/gtos_native_sdk/native-services.h')==pathlib.Path(service['path']),'Real native identity SDK view required')
    bind(P/'additional-MD-01.json','4e4ff22561ede2d74d072645f2de3a1ab98991e315257d8f1020900269ce9fb9')
    additional=json.loads((P/'additional-MD-01.json').read_text())
    require(len(additional['files'])==58 and additional['counts']=={'raw_Git':41,'prior_full_MD':8,'regenerated_bytes':9},'Exact independently qualified MD slice required')
    bind(P/'compile-03/status.json',additional['source_observation_sha256'])
    for relative,digest in additional['receipt_pins'].items():bind(W/relative,digest)
    for path,value in additional['files'].items():
        require(value['kind'] in ('raw_Git','prior_full_MD','regenerated_bytes'),'Unknown source provenance')
        bind(path,value['sha256']);require(path not in allowed or allowed[path]==value['sha256'],'Additional source conflict');allowed[path]=value['sha256']
        if value['kind']=='regenerated_bytes':
            proof=json.loads((W/value['receipt']).read_text());output=proof['outputs'][value['output']]
            require(output['path']==value['actual_regenerated_path'] and output['sha256']==value['sha256'] and output['reference_bytes_identical'],'Actual regenerated bytes proof mismatch');bind(output['path'],value['sha256'])
    bind(P/'uid-header-origins-01.json','8e6b31b5f672915ffe68743550e0f218ace33249f6924032796d96a1bba7b927')
    uid=json.loads((P/'uid-header-origins-01.json').read_text());original=uid['original_native_header'];bind(original['path'],original['sha256'])
    scalar=uid['uid_type'];require(scalar['revision']=='ebe33e01982dbbf879661e3b6b78450f3020a53f' and scalar['relative']=='include/llvm-libc-types/uid_t.h','Original LLVM UID scalar required')
    facade=actual(G/'source/tools/gtos_native_sdk/sdk/llvm-libc-types/uid_t.h')
    for path in (scalar['path'],scalar['raw_path'],str(facade)):
        bind(path,scalar['sha256']);require(path not in allowed or allowed[path]==scalar['sha256'],'UID type source conflict');allowed[path]=scalar['sha256']
    bind(G/'source/tools/gtos_native_sdk/sdk/llvm-libc-types/uid_t.h',scalar['sha256'])
    path=P/'patch/tools/gtos_native_sdk/sdk/sys/types.h';digest='84cc6a1e25769fbf8cc1d08a5f7f260070d2d1548e596311197243f5234ff35a'
    bind(path,digest);allowed[str(path)]=digest;overlays[('tools','gtos_native_sdk','sdk','sys','types.h')]=path
    state['stage']='prepare_native_identity_source_view';emit();BUILD.mkdir();view(G/'source',ROOT,overlays)
    gen=actual(B/'buffers-02/build/gen');require(W in gen.parents,'Generated view escaped workspace');(BUILD/'gen').symlink_to(gen,target_is_directory=True);state['aliases'][str(BUILD/'gen')]=str(gen)
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    prefix=[str(CC),'-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-mmx','-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3',
        '-mno-sse4','-mno-avx','-femulated-tls','-fstack-usage','-nostdinc','-nostdinc++','-DGTOS_NATIVE_SDK=1',
        '-isystem','../source/tools/gtos_native_sdk/sdk','-isystem','../source/third_party/libc++/src/include',
        '-isystem','../source/tools/gtos_native_sdk/c-sdk','-isystem','../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include','-fno-exceptions','-fno-rtti']
    bind(G/'build/toolchain.ninja',graph['native_toolchain_sha256']);lines=(G/'build/toolchain.ninja').read_text().splitlines();at=lines.index('rule cxx')
    require(lines.count('rule cxx')==1 and lines[at+1]=='  command = '+' '.join(prefix)+' ${defines} ${include_dirs} ${cflags} ${cflags_cc} -MD -MF ${out}.d -c ${in} -o ${out}','Exact native original CXX recipe required')
    directory=graph['new_directory_target'];muxer=graph['targets'][item['target']];checker=origins['owner']
    require(checker['relative']=='obj/third_party/perfetto/src/base/base.ninja' and len(checker['row'])==1
        and checker['row'][0].split()[3]=='../source/third_party/perfetto/src/base/thread_checker.cc','Exact actual checker member required')
    require(item['actual_row'] in muxer['rows'] and muxer['fields']==profile['targets'][item['target']]['fields'],'Original full consumer owner changed')
    for name,target,row,compile_object in (('directory',directory,directory['rows'][0],True),('thread_checker',checker,checker['row'][0],True),('tracing_muxer_impl',muxer,item['actual_row'],True)):
        target_name=directory['name'] if name=='directory' else checker['relative'] if name=='thread_checker' else item['target'];bind(G/'build'/target_name,target['sha256'])
        content=(G/'build'/target_name).read_text().splitlines();require(content.count(row)==1,'Exact source/member row required');fields=[]
        for key in ('defines','include_dirs','cflags','cflags_cc'):
            value=target['fields'][key];require('$' not in value and content.count(key+' = '+value)==1,'Own field mismatch');fields.extend(shlex.split(value))
        require(all(flag in fields for flag in ('-D__GTOS__=1','-DDCHECK_ALWAYS_ON=1','-O2','-DNDEBUG','-std=c++23','-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE'))
            and not any(flag.startswith(('@','--sysroot','--config=','-fplugin=','-fpass-plugin=','-ivfsoverlay','-D__linux__','-D__unix__')) for flag in fields)
            and '-load' not in fields and '-wrapper' not in fields,'Original native features and no tool/plugin overrides required')
        for index,flag in enumerate(fields):
            if flag=='-add-plugin':require(fields[index-1:index+3] in [['-Xclang','-add-plugin','-Xclang',value] for value in ('find-bad-constructs','raw-ptr-plugin','unsafe-buffers')],'Only original static plugins')
        crash='-fcrash-diagnostics-dir=../source/out/clang-crashreports';require(fields.count(crash)==1,'Only exact crash output relocation allowed');fields[fields.index(crash)]='-fcrash-diagnostics-dir='+str(OUT/'crash')
        words=row.split();source=words[3];obj=BUILD/words[1][:-1]
        policy_dependencies=words[5:]
        if name in ('tracing_muxer_impl','thread_checker'):
            phony='phony/third_party/perfetto/src/tracing/client_api_without_backends.inputs' if name=='tracing_muxer_impl' else 'phony/third_party/perfetto/src/base/base.inputs'
            harddeps='phony/third_party/perfetto/src/tracing/client_api_without_backends.harddeps' if name=='tracing_muxer_impl' else 'phony/third_party/perfetto/gn/gen_buildflags'
            require(words[4:]==['|',phony,'||',harddeps],'Original complete consumer dependency edges required')
            policy_rows=[line for line in content if line.startswith('build '+phony+': phony ')]
            require(len(policy_rows)==1,'Exact compiler input phony required')
            policy_dependencies=policy_rows[0].split()[3:]
        require(words[4]=='|' and len(policy_dependencies)==5 and set(policy_dependencies)=={'../source/'+path for path in config_pins['files']},'Original explicit compiler policy dependencies required')
        for relative,value in config_pins['files'].items():
            logical=ROOT/relative;require(actual(logical)==pathlib.Path(value['path']),'Compiler policy source view changed');bind(logical,value['sha256'])
        require(words[1].startswith('obj/') and '..' not in pathlib.PurePosixPath(words[1]).parts and words[1].endswith('.o:'),'Output escaped owner')
        obj.parent.mkdir(parents=True,exist_ok=True);pre=OUT/(name+'-pre.d');dep=pathlib.Path(str(obj)+'.d');key=str(actual(BUILD/source));require(key in allowed,'Source not qualified');bind(BUILD/source,allowed[key])
        state['stage']='full_M_'+name;emit();result=run(prefix+fields+['-M','-MF',pre,'-MT',obj,source],name+'-M')
        if result:state['failed_rows'][name]={'stage':'preprocessor','returncode':result};emit();continue
        expected,unbound=dependencies(pre,obj);state['MD_aliases'].update(expected)
        if unbound:state['failed_rows'][name]={'stage':'source_qualification','unbound_MD':unbound};emit();continue
        if name=='tracing_muxer_impl':state['full_muxer_M_pass']=True;state['muxer_MD_unique']=len(set(expected.values()));emit()
        require(compile_object,'Every selected row must compile after source qualification')
        state['stage']='CXX_'+name;emit();result=run(prefix+fields+['-MD','-MF',dep,'-c',source,'-o',obj],name+'-CXX')
        if result:state['failed_rows'][name]={'stage':'compiler','returncode':result};emit();continue
        observed,unbound=dependencies(dep,obj);require(observed==expected and not unbound,'Full MD changed during CXX')
        data=obj.read_bytes();require(data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Wrong native object')
        offset=struct.unpack_from('<I',data,32)[0];size,count=struct.unpack_from('<HH',data,46)
        require(size==40 and 0<count<4096 and offset+count*40<=len(data),'Bad ELF section table')
        require(all(not(struct.unpack_from('<I',data,offset+i*40+8)[0]&0x400) for i in range(count)),'Unexpected hardware TLS')
        state['objects'][name]={'path':str(obj),'sha256':sha(obj),'MD':str(dep),'MD_sha256':sha(dep),'MD_unique':len(set(expected.values())),'owner':target_name};emit()
    state['stage']='final_input_alias_checks';emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==target for group in (state['aliases'],state['MD_aliases']) for path,target in group.items()),'Bound alias changed')
    state.update(stage='completed',diagnostic_complete=True,objects_qualified=set(state['objects'])=={'directory','thread_checker','tracing_muxer_impl'},full_muxer_CXX_pass='tracing_muxer_impl' in state['objects'],bound_inputs_unchanged=True,aliases_unchanged=True,MD_aliases_unchanged=True,original_compiler_policy_inputs_bound=True,
        scope='Actual original GN owners for directory provider/native ThreadChecker/whole raw original TracingMuxer M/MD/CXX when qualified. New view overlays native ThreadChecker pair and exposes existing raw LLVM UID scalar through native sys/types; no user-ID query or fabricated identity.21493-target Content graph fields/rows/full Muxer source unchanged. Additional58 sources independently qualified, including9 regenerated outputs. Real native process-info TID and atomic/detach/copy preserved; no pthread shim. No native C API/ThreadChecker runtime, Muxer link/Content/browser acceptance.')
    emit();exit_code=0 if state['objects_qualified'] and state['full_muxer_M_pass'] and not state['failed_rows'] else 1
    print(json.dumps({'objects_qualified':state['objects_qualified'],'full_muxer_M_pass':state['full_muxer_M_pass'],'failed_rows':{k:v['stage'] for k,v in state['failed_rows'].items()}}),flush=True)
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(exit_code)
