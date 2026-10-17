#!/usr/bin/env python3
"""Finish successful formal objects; raw standalone .s has one explicit input."""
import ctypes,hashlib,json,os,pathlib,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';G=P/'gn-04';OLD=P/'class-formal-01';OUT=P/'class-formal-02';NM=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/llvm-nm'
state={'complete':False,'formal_CXX_ASM_pass':False,'native_rerun':False,'browser_pass':False,'inputs':{},'aliases':{},'objects':{},'commands':[]};parents={};authorized=False;code=1

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

def bind(path,digest):
    actual=resolve(path,parents);key=str(actual);require(W in actual.parents and actual.is_file(),'Fixed workspace input')
    if key in state['inputs']:require(state['inputs'][key]==digest,'Digest conflict')
    else:require(sha(actual)==digest,'Input changed: '+key);state['inputs'][key]=digest
    state['aliases'][str(path)]=key

def receipt(path,digest,complete=True):
    bind(path,digest);value=json.loads(path.read_text())
    if complete:require(value['complete'] and 'failure' not in value,'Completed prerequisite')
    return value

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-qualify-formal-classes-v1.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    graph=receipt(G/'status.json','ba9da40458d39d85479f9ec2df4bc21c3a9afcb670810b74534fb098ba2dba6f');require(graph['formal_SysInfo_candidate_member'] and graph['formal_class_diagnostic_compile_targets'] and graph['existing_members_fields_order_and_all_tool_rules_preserved'] and graph['bound_inputs_unchanged'] and graph['aliases_unchanged'],'Whole formal Content/class graph')
    partial=receipt(OLD/'status.json','05eacc7ea8090058869c6fa06251717e5de77dfb49a039871d55bd8905024121',False);missing=str(OLD/'build/obj/tools/gtos_base_class_probe/probe.start.o.d');require(not partial['complete'] and partial['failure']=="[Errno 2] No such file or directory: '"+missing+"'" and set(partial['objects'])=={'sys_info','checks','entry','length','wrong-length'} and not partial['unqualified_M_inputs'],'Exact successful5 CXX plus standalone ASM stopped only on nonexistent depfile expectation')
    prior=receipt(P/'class-compile-04/status.json','6fe51eee798fd3ab4b5462f5b763b2e1bdb49ab48535b7b71cae392295cc8cfc');native=receipt(P/'class-guest-03/status.json','903445bb3db71a55bd89d4c952a7ef2616381bcbd477d330e651861dce774961');require(native['native_class_pass'] and native['bound_inputs_unchanged'] and native['outputs_unchanged'],'Actual original six-context native proof')
    bind(P/'compile-formal-classes-v1.py','8ec8fe7ac0d2f4b4f6bb379ebb008397f151994d463a0173c2504198cfd29b20');bind(OLD/'guard.json','492f6dd10cc7583c7f18c1869f9a43c8d56da72f6b768c74d05da065eda72fc3');old_guard=json.loads((OLD/'guard.json').read_text());require(old_guard['child_sha256']==sha(P/'compile-formal-classes-v1.py') and old_guard['landlock_applied'] and old_guard['network_disabled'],'Actual compiled partial run guard')
    state['stage']='revalidate_actual_formal_CXX_inputs_aliases_objects';emit()
    for path,digest in partial['inputs'].items():bind(path,digest)
    for logical,actual in partial['aliases'].items():require(str(resolve(logical,parents))==actual,'Actual formal source alias changed');state['aliases'][logical]=actual
    for name,item in partial['objects'].items():
        obj=pathlib.Path(item['path']);require(OLD/'build' in obj.parents and item['actual_GN_owner'] in graph['owners'],'Exact formal object owner/path');owner=G/'build'/item['actual_GN_owner'];bind(owner,graph['owners'][item['actual_GN_owner']]['sha256']);require(owner.read_text().splitlines().count(item['actual_GN_row'])==1,'Actual formal target membership')
        bind(obj,item['sha256']);bind(pathlib.Path(str(obj)+'.d'),item['MD_sha256']);require(item['same_bytes_as_native_qualified_prior'] and item['sha256']==prior['objects'][name]['sha256'],'Every actual formal CXX object equals the native-qualified prior');bind(prior['objects'][name]['path'],item['sha256']);data=obj.read_bytes();require(data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Real ELF32/i386 CXX object')
        for dependency in item['dependencies']:require(dependency in partial['inputs'],'Complete CXX MD source belongs to qualified partial input closure')
        for suffix in ('M','CXX','undefined','defined'):
            commands=[row for row in partial['commands'] if row['name']==name+'-'+suffix];require(len(commands)==1 and commands[0]['returncode']==0,'Every actual CXX/MD/NM stage succeeded');bind(OLD/(name+'-'+suffix+'.log'),commands[0]['log_sha256'])
        state['objects'][name]=dict(item,reused_actual_formal_compile=True,full_M_MD_requalified=True);emit()
    name='start';owner_name='obj/tools/gtos_base_class_probe/probe.ninja';owner=G/'build'/owner_name;bind(owner,graph['owners'][owner_name]['sha256']);row=graph['owners'][owner_name]['new_ASM_members'];require(len(row)==1 and owner.read_text().splitlines().count(row[0])==1 and row[0].startswith('build obj/tools/gtos_base_class_probe/probe.start.o: asm ../source/tools/gtos_base_class_probe/start.s '),'Exact standalone formal ASM member')
    source=G/'source/tools/gtos_base_class_probe/start.s';bind(source,'aed35dc7a00ccbad0f7a350b1af1e25af05bd0532d3be64ab98d1c701b797d56');require(not any(token in source.read_text() for token in ('.include','.incbin','#include')),'Pinned standalone ASM has no external includes/incbin/preprocessor input')
    obj=OLD/'build/obj/tools/gtos_base_class_probe/probe.start.o';bind(obj,prior['objects']['start']['sha256']);require(not pathlib.Path(missing).exists(),'No fabricated raw-ASM MD file');commands=[row for row in partial['commands'] if row['name']=='start-ASM'];require(len(commands)==1 and commands[0]['returncode']==0,'Actual formal assembler succeeded');bind(OLD/'start-ASM.log',commands[0]['log_sha256']);warnings=(OLD/'start-ASM.log').read_text().splitlines();require(warnings and all(line.startswith('clang: warning: argument unused during compilation: ') and line.endswith(' [-Wunused-command-line-argument]') for line in warnings) and any("'-MD'" in line for line in warnings) and any("'-MF obj/tools/gtos_base_class_probe/probe.start.o.d'" in line for line in warnings),'Compiler explicitly documents ignored raw-ASM depfile/preprocessor flags, no other diagnostic')
    bind(NM,'629e43f674d383d8c54d43f3254f80895a380c05a4afcdec27bb03dbb72b74bc')
    for suffix,option in (('undefined','--undefined-only'),('defined','--defined-only')):
        log=OUT/('start-'+suffix+'.log');argv=[str(NM),option,'--format=posix',str(obj)]
        with log.open('xb') as stream:result=subprocess.run(argv,cwd=OUT,env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC'},shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=60)
        require(result.returncode==0 and log.stat().st_size<65536,'Bounded exact NM');state['commands'].append({'argv':argv,'returncode':0,'log_sha256':sha(log)})
    require((OUT/'start-undefined.log').read_text().splitlines()==['NativeClassEntry U 0 0'],'Only actual class-entry ASM import');state['objects']['start']={'path':str(obj),'sha256':sha(obj),'source':str(resolve(source,parents)),'source_sha256':'aed35dc7a00ccbad0f7a350b1af1e25af05bd0532d3be64ab98d1c701b797d56','actual_GN_owner':owner_name,'actual_GN_row':row[0],'same_bytes_as_native_qualified_prior':True,'raw_standalone_ASM_one_explicit_source':True,'depfile_generated':False,'MD_evidence_fabricated':False}
    state['stage']='terminal_all_actual_input_alias_output_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()) and all(sha(item['path'])==item['sha256'] for item in state['objects'].values()),'Final formal source/alias/object changed')
    state.update(complete=True,formal_CXX_ASM_pass=True,full_CXX_M_MD_source_qualification=True,standalone_ASM_explicit_source_qualification=True,all_six_objects_identical_to_native_qualified_prior=True,bound_inputs_unchanged=True,aliases_unchanged=True,outputs_unchanged=True,native_runtime_proof_inherited_by_exact_object_identity=True,stage='completed',scope='Five actual formal-GN CXX objects have full M/MD/NM and exact prior bytes. Raw lowercase .s is one pinned explicit source with no includes/incbin, actual compiler ignores -MD/-MF and produces no depfile; actual ASM object equals native-qualified prior byte for byte and only imports real entry. No synthetic MD, tool rule/source/flags change, compiler rerun or native rerun. All6 actual formal objects equal six-context native/whole-ISA inputs. No whole Content compile or browser acceptance.');emit();print('PASS formal six-object source/byte identity qualification',flush=True);code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
