#!/usr/bin/env python3
"""Qualify separate real native clipboard fixtures alongside the unchanged Content graph."""
import ctypes,hashlib,json,os,pathlib,re,shlex,stat,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-clipboard-format-20261018-a';G=W/'artifacts/native-content-frontier-20261018-a/gn-clipboard-01';OUT=P/'gn-probe-01';ROOT=OUT/'source';BUILD=OUT/'build';GN=W/'toolchains/gn/gn'
PINS_SHA='2cdcd9bfaa37bde63741ec2cd25994492ba1e3333ccd72082058104274d70749'
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
state={'complete':False,'gn_generation_pass':False,'formal_clipboard_fixture_targets':False,'native_clipboard_pass':False,'browser_pass':False,'inputs':{},'aliases':{},'source_view_aliases':{},'GN_aliases':{},'unbound_GN_inputs':{},'directory_metadata':{},'current_GN_build_data':{},'owners':{},'reviewed_outputs':{}}
parents={};known={};authorized=False;code=1

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()

def resolve(path,cache):
    path=pathlib.Path(path)
    if path.name in ('','.','..'):return path.resolve(strict=True)
    key=str(path.parent)
    if key not in cache:cache[key]=path.parent.resolve(strict=True)
    actual=cache[key]/path.name
    if actual.is_symlink():actual=actual.resolve(strict=True)
    actual.stat();return actual

def bind(path,digest):
    actual=resolve(path,parents);key=str(actual);require(W in actual.parents and actual.is_file(),'Workspace regular input required')
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

def qualify_toolchain(old_path,new_path,old_helper,helper):
    # New targets add metadata only. Every existing line, rule and action must
    # remain byte-identical after the already-reviewed fixed helper relocation.
    target='tools/gtos_clipboard_format_probe/'
    subninjas={'subninja obj/'+target+stem+'.ninja' for stem in ('negative_entry','probe')}
    root='build phony/'+target+'qualification_root: phony '
    hard='build phony/'+target+'qualification_root.harddeps: phony '
    deps={'phony/'+target+stem for stem in ('negative_entry','probe')}|{'phony/tools/gtos_base_class_probe/qualification_root'}
    own_hard={'phony/'+target+stem+'.harddeps' for stem in ('negative_entry','probe')}
    allowed_hard=own_hard|{'phony/tools/gtos_base_class_probe/qualification_root.harddeps'}
    retained=[];additions=[];seen=set()
    for line in new_path.read_text().splitlines(keepends=True):
        value=line.rstrip('\n')
        if value in subninjas:
            require(value not in seen,'Duplicate new subninja');seen.add(value);additions.append(value)
        elif value.startswith(root):
            words=value[len(root):].split();require(root not in seen and len(words)==3 and set(words)==deps,'Only exact new qualification group dependencies');seen.add(root);additions.append(value)
        elif value.startswith(hard):
            words=value[len(hard):].split();require(hard not in seen and len(words)==3 and set(words)==allowed_hard,'Only exact group hard-dependency metadata');seen.add(hard);additions.append(value)
        else:retained.append(line)
    require(seen==subninjas|{root,hard},'Exactly two source sets and one complete qualification group')
    require(''.join(retained)==old_path.read_text().replace(str(old_helper),str(helper)),'Existing whole native rules/actions/metadata changed')
    state['new_toolchain_metadata']=additions
    state['reviewed_outputs'][str(new_path)]=sha(new_path)

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated no-site entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-gn-probe-v1.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and guard['output_symlink_creation_allowed'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Exact guard required')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    bind(GN,'c0dec98e11c4008b165a44bf407e06704710cf76e2af4aea22dd3b7102f0778e')
    old=receipt(G/'status.json','c3a702764baf8fcc8779b1fc49a3f80c7eaea146326c020269a886a791e3642f');require(all(old[key] for key in ('complete','gn_generation_pass','typed_GN_dependency_qualification','bound_inputs_unchanged','aliases_unchanged','directory_identity_unchanged','clipboard_format_selection_pass','whole_original_string_format_backend')) and (old['GN_targets'],old['GN_files'])==(21498,2517) and old['argv'][7]=='--args='+ARGS,'Exact qualified full Content graph and unchanged arguments')
    compiled=receipt(G.parent/'clipboard-format-compile-02/status.json','0b7e704222a57503715e3d09d979e714d80665db43ebc22ef40ffaab33e58d11');require(all(compiled[key] for key in ('complete','clipboard_format_compile_pass','complete_M_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged','outputs_unchanged')) and compiled['M_dependency_count']==933,'Complete actual original format implementation before diagnostic targets')
    require(len(PINS_SHA)==64,'Review fixture source pins before execution');pins=receipt(P/'probe-source-pins-01.json',PINS_SHA)
    prefix='tools/gtos_clipboard_format_probe/'
    require(set(pins['files'])=={prefix+name for name in ('BUILD.gn','api.h','checks.cc','entry.cc','start.s')} and pins['authored_fixture_only'] and not pins['native_runtime_qualified'],'Only five reviewed diagnostic files')
    known.update(old['inputs']);overlays={('content-diagnostic.gn',):P/'content-dotfile-v1.gn'}
    for relative,item in pins['files'].items():
        bind(item['path'],item['sha256']);known[item['path']]=item['sha256'];overlays[pathlib.PurePosixPath(relative).parts]=pathlib.Path(item['path'])
    helper=P/'content-helper-v1.py';dot=P/'content-dotfile-v1.gn'
    bind(helper,'71cde6317ac58a201c3a467e3dd1d8bfbb9eee95973bc465180235058f296e9f');bind(dot,'b876d7255675f80d2c47c43db3abfd0be5413be9927deb2c2257c9f266e25bef');known[str(helper)]=sha(helper);known[str(dot)]=sha(dot)
    old_helper=G.parent/'content-helper-clipboard-v1.py';old_dot=G.parent/'content-dotfile-clipboard-v1.gn';bind(old_helper,'1894b0df5bc71afca4196b3a14a5eaf3c11979babe8fbf10360ac1bcea5a8eb0');bind(old_dot,'6d41681ba288ca20371d1a1b241071a4f367dfe05b96f3be3cef07565a613aa2')
    before="OUT=W/'artifacts/native-content-frontier-20261018-a/gn-clipboard-01'";after="OUT=W/'artifacts/native-clipboard-format-20261018-a/gn-probe-01'"
    require(old_helper.read_text().count(before)==1 and old_helper.read_text().replace(before,after)==helper.read_text(),'Only fixed helper output changed; no generated executable code')
    require(old_dot.read_text().replace(str(old_helper),str(helper))==dot.read_text(),'Same dotfile and exact three-call helper allowlist')
    bind(G/'build/args.gn',old['current_GN_build_data'][str(G/'build/args.gn')]);args_sha=sha(G/'build/args.gn');state['stage']='fresh_native_clipboard_fixture_source_view';emit();view(G/'source',ROOT,overlays);BUILD.mkdir()
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')};label='//tools/gtos_clipboard_format_probe:qualification_root'
    argv=[str(GN),'gen',str(BUILD),'--root='+str(ROOT),'--dotfile='+str(ROOT/'content-diagnostic.gn'),'--root-target='+label,'--root-pattern='+label,'--args='+ARGS,'--fail-on-unused-args'];state.update(stage='actual_full_Content_and_clipboard_fixture_GN',argv=argv);emit()
    with (OUT/'GN.log').open('xb') as stream:result=subprocess.run(argv,cwd=ROOT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    state['GN_returncode']=result.returncode;emit();require(result.returncode==0,'Full current Content plus fixture GN failed');counts=re.search(r'Made (\d+) targets from (\d+) files',(OUT/'GN.log').read_text());require(counts and tuple(map(int,counts.groups()))==(21501,2518),'Exactly three new diagnostic targets and one BUILD file')
    for name in ('timestamp','branding','chrome-version'):
        path=OUT/(name+'-calls.jsonl');records=[json.loads(line) for line in path.read_text().splitlines()];require(len(records)==1 and records[0]['bound_inputs_unchanged'] and records[0]['returncode']==0,'Exactly three qualified fixed helpers');state.setdefault('helper_receipts',{})[name]={'sha256':sha(path),'records':records}
    for name,item in old['owners'].items():
        bind(G/'build'/name,item['sha256']);require((G/'build'/name).read_bytes()==(BUILD/name).read_bytes(),'Existing complete owner changed: '+name);state['owners'][name]=dict(item);state['owners'][name]['all_members_unchanged']=True;state['reviewed_outputs'][str(BUILD/name)]=item['sha256']
    toolchain=G/'build/toolchain.ninja';bind(toolchain,old['native_toolchain_sha256']);qualify_toolchain(toolchain,BUILD/'toolchain.ninja',old_helper,helper)
    fixture_fields={}
    for stem,sources in (('probe',{'checks.cc','entry.cc'}),('negative_entry',{'entry.cc'})):
        target='obj/tools/gtos_clipboard_format_probe/'+stem+'.ninja';fields,members=owner(BUILD/target)
        require({line.split(': cxx ',1)[1].split()[0] for line in members}=={'../source/'+prefix+name for name in sources} and len(members)==len(sources),'Exact independent fixture CXX members')
        flags=[token for value in fields.values() for token in shlex.split(value)];require(all(token in flags for token in ('-D__GTOS__=1','-DNDEBUG','-DDCHECK_ALWAYS_ON=1','-O2','-std=c++23')) and '-DUSE_AURA' not in flags and not any(token.startswith(('@','--sysroot','--config=','-fplugin=','-fpass-plugin=','-ivfsoverlay','-D__linux__','-D__unix__')) for token in flags) and '-load' not in flags,'Real hardened native fixture configuration')
        require(('-DGTOS_CLIPBOARD_MODE=1' in flags)==(stem=='negative_entry'),'Only explicit negative-entry mode is selected')
        fixture_fields[stem]=fields;state['owners'][target]={'sha256':sha(BUILD/target),'fields':fields,'new_CXX_members':members};state['reviewed_outputs'][str(BUILD/target)]=sha(BUILD/target)
    for name in ('include_dirs','cflags','cflags_cc'):require(fixture_fields['probe'][name]==fixture_fields['negative_entry'][name],'Positive/negative entry configuration differs')
    require([token for token in shlex.split(fixture_fields['negative_entry']['defines']) if token!='-DGTOS_CLIPBOARD_MODE=1']==shlex.split(fixture_fields['probe']['defines']),'Only negative mode define may differ')
    target='obj/tools/gtos_clipboard_format_probe/probe.ninja';asm_fields,asm_members=owner(BUILD/target,True);require(len(asm_members)==1 and asm_members[0].split(': asm ',1)[1].split()[0]=='../source/'+prefix+'start.s','Exact aligned native startup assembly member');state['owners'][target].update(asm_fields=asm_fields,new_ASM_members=asm_members)
    state['stage']='qualify_every_actual_GN_dependency';emit();bind(BUILD/'args.gn',args_sha);dep=(BUILD/'build.ninja.d').read_text().replace(chr(92)+chr(10),' ');require(chr(92) not in dep and '$' not in dep,'Plain GN dependency syntax');names=dep.split(':',1)[1].split();require(0<len(names)<10000,'Bounded full GN dependency list')
    for name in names:
        logical=BUILD/name;actual=resolve(logical,parents);key=str(actual);require(W in actual.parents,'GN input escaped')
        if actual.is_dir():
            metadata=actual.stat();state['directory_metadata'][str(logical)]={'actual':key,'st_dev':metadata.st_dev,'st_ino':metadata.st_ino,'file_type':stat.S_IFMT(metadata.st_mode)};continue
        require(actual.is_file(),'Regular GN input required')
        if BUILD in actual.parents:
            ancestor=str(G/'build'/actual.relative_to(BUILD));require(ancestor in old['current_GN_build_data'],'New generated GN input filename needs independent review');digest=sha(actual);bind(logical,digest);state['current_GN_build_data'][key]=digest
        elif key in state['inputs']:bind(logical,state['inputs'][key])
        elif key in known:bind(logical,known[key])
        else:state['unbound_GN_inputs'][key]=sha(actual);continue
        state['GN_aliases'][str(logical)]=key
    state['GN_dependency_count']=len(names);emit();require(not state['unbound_GN_inputs'],'New actual GN input requires independent origin')
    state['stage']='terminal_GN_source_alias_directory_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for group in ('aliases','source_view_aliases','GN_aliases') for logical,actual in state[group].items()),'GN input/source view changed')
    for logical,item in state['directory_metadata'].items():
        actual=resolve(logical,fresh);metadata=actual.stat();require(str(actual)==item['actual'] and (metadata.st_dev,metadata.st_ino,stat.S_IFMT(metadata.st_mode))==(item['st_dev'],item['st_ino'],item['file_type']),'GN directory identity/type changed')
    require(all(sha(path)==digest for path,digest in state['reviewed_outputs'].items()),'Reviewed owner/toolchain/group metadata changed before terminal receipt');state['reviewed_outputs_unchanged']=True
    state.update(complete=True,gn_generation_pass=True,GN_input_qualification=True,typed_GN_dependency_qualification=True,formal_clipboard_fixture_targets=True,prior_owners_byte_identical=True,whole_toolchain_and_generation_edges_preserved=True,bound_inputs_unchanged=True,aliases_unchanged=True,directory_identity_unchanged=True,native_toolchain_sha256=sha(BUILD/'toolchain.ninja'),GN_targets=21501,GN_files=2518,stage='completed',scope='Full current Content graph unchanged with separate clipboard positive/negative entry fixture source sets and qualification group. Prior owners byte-identical, original format owner/object retained, exact native CXX/ASM rows. Only GTOS_CLIPBOARD_MODE=1 differs between entry configurations. No kernel WriteMemory API, fixture CXX/native acceptance or Content/browser claim.');emit();print('PASS actual full Content plus native clipboard fixture GN',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
