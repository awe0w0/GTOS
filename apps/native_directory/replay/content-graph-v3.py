#!/usr/bin/env python3
"""Add the real directory provider to the unchanged full native Content root."""
import ctypes,hashlib,json,os,pathlib,re,shlex,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-directory-20261017-a'
E=W/'artifacts/native-getenv-20261017-a';OLD=P/'content-graph-01';SOURCE=P/'compile-06/source'
OUT=P/'content-graph-03';ROOT=OUT/'source';BUILD=OUT/'build';GN=W/'toolchains/gn/gn'
state={'diagnostic_complete':False,'gn_generation_pass':False,'complete_graph_input_qualification':False,
       'compiled_objects':False,'native_C_API_guest_pass':False,'browser_pass':False,'production_image_changed':False,
       'inputs':{},'input_symlinks':{},'source_view_aliases':{},'consumer_source_MD':{},'replaced_source_MD':{}}
authorized=False;exit_code=1;resolved={}
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
    require(W in selected.parents and selected.is_file(),'Source escaped workspace')
    require(key not in state['inputs'] or state['inputs'][key]==digest,'Input conflict')
    if key not in state['inputs']:require(sha(selected)==digest,'Changed input: '+key)
    state['inputs'][key]=digest;state['input_symlinks'][str(logical)]=key
def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def view(source,destination,overlays):
    require(source.is_dir() and overlays,'Existing source branch and explicit overlay required')
    destination.mkdir();children={child.name:child for child in source.iterdir()}
    for name in sorted(set(children)|{parts[0] for parts in overlays}):
        if name in ('.git','out','__pycache__'):continue
        selected={parts[1:]:path for parts,path in overlays.items() if parts[0]==name};target=destination/name
        if () in selected:require(len(selected)==1,'Overlapping overlay');path=actual(selected[()])
        elif selected:view(source/name,target,selected);continue
        else:path=actual(children[name])
        require(W in path.parents,'Source alias escaped workspace')
        target.symlink_to(path,target_is_directory=path.is_dir());state['source_view_aliases'][str(target)]=str(path)
def owner(path):
    lines=path.read_text().splitlines();fields={}
    for key in ('defines','include_dirs','cflags','cflags_cc'):
        values=[line[len(key)+3:] for line in lines if line.startswith(key+' = ')]
        require(len(values)==1,'Ambiguous CXX field');fields[key]=values[0]
    rows=[line for line in lines if line.startswith('build obj/') and ': cxx ../source/' in line]
    for row in rows:
        at=lines.index(row)+1
        while at<len(lines) and lines[at].startswith('  '):
            require(lines[at].startswith(('  source_file_part = ','  source_name_part = ')),'Unexpected source override');at+=1
    return fields,rows
def rule(text,name):
    lines=text.splitlines();require(lines.count('rule '+name)==1,'Ambiguous rule');start=lines.index('rule '+name);end=start+1
    while end<len(lines) and lines[end].startswith('  '):end+=1
    return lines[start:end]
try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-graph-v3.py')
        and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
        and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    bind(GN,'c0dec98e11c4008b165a44bf407e06704710cf76e2af4aea22dd3b7102f0778e')
    bind(P/'source-pins-01.json','545782856822eedbb6e8c16b2314fa6b166d5c67e1879b0f4370831e759e632c')
    pins=json.loads((P/'source-pins-01.json').read_text())
    require(pins['previous_goal_turn']=='progress' and len(pins['overlays'])==6,'Exact directory packet required')
    for path,digest in pins['anchors'].items():bind(W/path,digest)
    bind(OLD/'status.json','12c1f279e8766bbbecb83bd80dc19139fbf69acc2c33fa06b044cd8f0762cdd1');old=json.loads((OLD/'status.json').read_text())
    require(all(old[k] for k in ('diagnostic_complete','gn_generation_pass','bound_inputs_unchanged','input_symlinks_unchanged',
        'source_view_aliases_unchanged','native_CXX_fields_unchanged','native_CXX_rule_unchanged','actual_content_directory_dependency'))
        and old['GN_targets']==21493 and old['GN_files']==2515 and 'failure' not in old,'Actual full native Content baseline required')
    for path,digest in old['inputs'].items():bind(path,digest)
    bind(P/'compile-06/status.json','2b26e0319fe8b88dd8fea6ee25cdfa329307ba5548ab9f3c72e43efafcfd794d')
    compiled=json.loads((P/'compile-06/status.json').read_text())
    require(all(compiled[k] for k in ('diagnostic_complete','objects_qualified','full_muxer_M_pass','full_muxer_CXX_pass','bound_inputs_unchanged','aliases_unchanged','MD_aliases_unchanged'))
        and not compiled['failed_rows'] and not compiled['unbound_MD_inputs'] and 'failure' not in compiled,'Final actual source/MD/CXX view required')
    for path,digest in compiled['inputs'].items():bind(path,digest)
    bind(P/'content-helper-v3.py','fac90a9eb5b716c387ab1f0ca880554a47a360c5924a3d8af2e5b99d574e4eb3')
    bind(P/'content-dotfile-v3.gn','d9f174cc6e6c00549e34b95d81f592467cb31269f1d33f867b095f1e096308dc')
    before=(P/'content-helper-v1.py').read_text().splitlines();after=(P/'content-helper-v3.py').read_text().splitlines()
    at=before.index("OUT=W/'artifacts/native-directory-20261017-a/content-graph-01'")
    require(after==before[:at]+["OUT=W/'artifacts/native-directory-20261017-a/content-graph-03'"]+before[at+1:],'Only helper output relocation permitted')
    before=(P/'content-dotfile-v1.gn').read_text().splitlines();after=(P/'content-dotfile-v3.gn').read_text().splitlines()
    at=before.index('script_executable = "'+str(P/'content-helper-v1.py')+'"')
    require(after==before[:at]+['script_executable = "'+str(P/'content-helper-v3.py')+'"']+before[at+1:]
        and after.count('exec_script_allowlist = [ "//build/timestamp.gni", "//build/util/branding.gni", "//chrome/version.gni" ]')==1,'Exact three helper callers required')
    for item in pins['overlays'].values():bind(item['path'],item['sha256'])
    overlays={('content-diagnostic.gn',):P/'content-dotfile-v3.gn'}
    state['stage']='prepare_final_full_source_view';emit();view(SOURCE,ROOT,overlays);BUILD.mkdir()
    for relative,digest in old['consumer_source_MD'].items():
        path=ROOT/relative;normalized=os.path.normpath(relative)
        require(not os.path.isabs(normalized) and normalized!='..' and not normalized.startswith('../')
            and actual(path)==actual(ROOT/normalized),'MD escaped root')
        if normalized=='tools/gtos_native_sdk/sdk/sys/types.h':
            require(digest=='4cbd01b1f8e5518014fa5d122971d19ac68aab914c0a095d0782ea74035335d4','Only accepted native UID projection replaces prior source MD')
            replacement='84cc6a1e25769fbf8cc1d08a5f7f260070d2d1548e596311197243f5234ff35a';state['replaced_source_MD'][relative]={'before':digest,'after':replacement};digest=replacement
        bind(path,digest);state['consumer_source_MD'][relative]=digest
    reproduced_MD={};generated_MD={};resource_MD={}
    for logical,physical in compiled['MD_aliases'].items():
        relative=os.path.relpath(logical,P/'compile-06/build')
        if relative.startswith('../source/'):
            source_relative=relative[len('../source/'):];selected=actual(ROOT/source_relative)
            require(str(selected)==physical,'Final Content source view changed qualified MD bytes');bind(ROOT/source_relative,compiled['inputs'][physical]);reproduced_MD[logical]={'relative':source_relative,'physical':physical}
        elif relative.startswith('gen/'):
            require(W/'artifacts' in pathlib.Path(physical).parents,'Generated reference escaped reviewed artifacts')
            bind(logical,compiled['inputs'][physical]);generated_MD[logical]={'relative':relative,'physical':physical,'sha256':compiled['inputs'][physical],'regenerated_in_this_graph':False}
        else:
            require(W/'toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include' in pathlib.Path(physical).parents,'Unexpected external MD root')
            bind(BUILD/relative,compiled['inputs'][physical]);resource_MD[logical]={'relative':relative,'physical':physical}
    require(len(reproduced_MD)+len(generated_MD)+len(resource_MD)==len(compiled['MD_aliases']),'Every selected compiled MD alias must be classified')
    before=old['argv'];require(len(before)==9 and before[:7]==[str(GN),'gen',str(OLD/'build'),'--root='+str(OLD/'source'),
        '--dotfile='+str(OLD/'source/content-diagnostic.gn'),'--root-target=//content/shell:content_shell','--root-pattern=//content/shell:content_shell']
        and before[7].startswith('--args=') and before[8]=='--fail-on-unused-args','Exact complete Content root required')
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    argv=[str(GN),'gen',str(BUILD),'--root='+str(ROOT),'--dotfile='+str(ROOT/'content-diagnostic.gn'),before[5],before[6],before[7],before[8]]
    state.update(stage='actual_full_Content_GN',argv=argv);emit()
    with (OUT/'gn.log').open('xb') as stream:
        result=subprocess.run(argv,cwd=ROOT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=240)
    require((OUT/'gn.log').stat().st_size<8*1024*1024,'Unbounded GN log')
    state.update(gn_returncode=result.returncode,gn_log_sha256=sha(OUT/'gn.log'));emit();require(result.returncode==0,'Full GN failed; retain diagnostic')
    counts=re.search(r'Made (\d+) targets from (\d+) files',(OUT/'gn.log').read_text())
    require(counts and tuple(map(int,counts.groups()))==(21493,2515),'Final source integration must preserve actual full target/file counts')
    calls={}
    for name in ('timestamp','branding','chrome-version'):
        path=OUT/(name+'-calls.jsonl');rows=[json.loads(line) for line in path.read_text().splitlines()]
        require(len(rows)==1 and rows[0]['bound_inputs_unchanged'] and rows[0]['returncode']==0,'Exact helper call required');calls[name]={'records':rows,'sha256':sha(path)}
    targets={}
    names=list(old['targets'])+[old['new_directory_target']['name']]
    require(len(names)==7 and len(names)==len(set(names)),'Seven original owner groups required')
    for name in names:
        expected=old['targets'][name]['sha256'] if name in old['targets'] else old['new_directory_target']['sha256']
        bind(OLD/'build'/name,expected);old_fields,old_rows=owner(OLD/'build'/name);new_fields,new_rows=owner(BUILD/name)
        require((old_fields,old_rows)==(new_fields,new_rows),'Original actual owner changed')
        targets[name]={'sha256':sha(BUILD/name),'fields':new_fields,'rows':new_rows,'CXX_rows':len(new_rows),'CXX_fields_unchanged':True,'original_CXX_rows_unchanged':True}
    toolchain=(BUILD/'toolchain.ninja').read_text();require(rule(toolchain,'cxx')==rule((OLD/'build/toolchain.ninja').read_text(),'cxx'),'Original CXX recipe changed')
    name='obj/tools/gtos_native_sdk/runtime/directory_native.ninja';fields,rows=owner(BUILD/name)
    require(len(rows)==1 and rows[0].split()[3]=='../source/tools/gtos_native_sdk/runtime/directory.cc','Actual new provider row required')
    flags=shlex.split(' '.join(fields.values()))
    require(all(flag in flags for flag in ('-D__GTOS__=1','-DDCHECK_ALWAYS_ON=1','-O2','-DNDEBUG','-std=c++23','-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE')),'Actual original native feature/hardening required')
    shell=(BUILD/'obj/content/shell/content_shell.ninja').read_text().split();output=rows[0].split()[1][:-1]
    require(output in shell or 'phony/tools/gtos_native_sdk/runtime/directory_native.linkdeps' in shell,'Real Content link must consume provider')
    cfg_name='__build_rust_gni_impl_rustc_print_cfg___build_toolchain_gtos_clang_x86__rule'
    before_rule=rule((OLD/'build/toolchain.ninja').read_text(),cfg_name);after_rule=rule(toolchain,cfg_name)
    require([line.replace(str(P/'content-helper-v1.py'),str(P/'content-helper-v3.py')) for line in before_rule]==after_rule,'Rust cfg changed beyond helper relocation')
    json_input='../source/build/config/gtos/rust/i686-unknown-gtos.json';phony='phony/build/rust/gni_impl/rustc_print_cfg.inputdeps'
    lines=toolchain.splitlines();deps=[line for line in lines if line.startswith('build ') and ': '+cfg_name+' ' in line]
    require(len(deps)==1,'One cfg action required')
    if phony in deps[0].split():deps.extend(line for line in lines if line.startswith('build '+phony+': phony '))
    require(any(json_input in line.split() for line in deps),'Rust cfg JSON input lost')
    state.update(stage='final_input_alias_checks',targets=targets,new_directory_target={'name':name,'sha256':sha(BUILD/name),'fields':fields,'rows':rows},
        actual_helper_calls=calls,GN_targets=21493,GN_files=2515,native_toolchain_sha256=sha(BUILD/'toolchain.ninja'),actual_content_directory_dependency=True,
        actual_final_compiled_source_view=True,reproduced_compiled_source_MD=reproduced_MD,qualified_generated_MD_references=generated_MD,qualified_resource_MD=resource_MD);emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Selected input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==target for group in (state['input_symlinks'],state['source_view_aliases']) for path,target in group.items()),'Input/source alias changed')
    state.update(stage='completed',diagnostic_complete=True,gn_generation_pass=True,bound_inputs_unchanged=True,input_symlinks_unchanged=True,source_view_aliases_unchanged=True,
        native_CXX_fields_unchanged=True,native_CXX_rule_unchanged=True,native_cfg_JSON_input_tracked=True,
        scope='Actual full Content21493-target/2515-file root includes final directory/ThreadChecker/UID source and consumes directory_native. Seven owners/rows/features/CXX rule preserved. All1028 selected compiled MD aliases classified: source aliases reproduced, Clang resources bound, qualified generated references retained without generation or copying into this fresh build. Earlier1318 source MD retained except explicit UID projection. Original three helper callers/Rust cfg JSON kept. Complete graph input qualification, generated actions and C API/ThreadChecker/Content runtime remain pending; previous native V8 launch remains historical.')
    emit();print(json.dumps({'gn_generation_pass':True,'GN_targets':21493,'previous_source_MD':len(state['consumer_source_MD'])}),flush=True);exit_code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(exit_code)
