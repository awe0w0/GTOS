#!/usr/bin/env python3
"""Two new clipboard links; extra heap/exit source proofs have unit-specific scope."""
import ctypes, hashlib, json, os, pathlib, re, struct, subprocess, sys, time, traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium')
Q=W/'artifacts/native-clipboard-format-20261018-a'
P=W/'artifacts/native-content-frontier-20261018-a'
B=W/'artifacts/native-pma-20261017-a'
OUT=Q/'link-02'
LD=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/ld.lld'
LD_SHA='99ef321b93b2f3c8214bb592b633b3736d2df70f51e5454920131631f08e3412'
SCRIPT=Q/'clipboard-v1.ld'
SCRIPT_SHA='83bdb170fe2d18c6164d9490409a3696e481397a0b29b973736f78675e72fca9'
COMPILE=Q/'compile-01/status.json'
COMPILE_SHA='9dd79f17f71913ddbda48f794aad708bca2990c299b6086ee01385e0b043141c'
PROVIDER=Q/'provider-source-05/status.json'
PROVIDER_SHA='49223f63db9194e80f1ab27d4a4f0b99bc11d9f4623a6a28f07864a44ee2f2cf'
# Selected only from actual fixture undefined symbols and the fixed qualified
# whole format object's bounded ELF table; names remain metadata, never argv.
FORMAT_ABI=None
FORMAT=P/'clipboard-format-compile-02/build/obj/ui/base/clipboard/libui_base_clipboard_types.clipboard_format_type_aura.o'
FORMAT_SHA='ae8553d59c9683267df4cf0dd9502906dd4e400cf2449649148cf9c0e9097185'
ABORT=W/'artifacts/native-v8-integration-20261011-a/wide-runtime-resume-30/build/obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.abort.o'
ABORT_SHA='6c4793c471b6248edc46e8fe917f6d85754535948ad897e317086d74aed0e78e'
HEAP=W/'artifacts/native-large-heap-20261015-a/compile-05/build/obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.heap.o'
HEAP_SHA='e07e54f37c9fd28ba8c2e485dbbd2c17b84da6a6a372506cb0a3654d008b32f4'
EXIT=W/'artifacts/native-iostream-20261014-a/compile-thread-03/build/obj/tools/gtos_isolate_diagnostic/runtime/native_exit_libc.process-exit.o'
EXIT_SHA='bdedc2dbe50b97067ccb8493d4a74618af62b5784560826bc592912d029c6693'
state={'complete':False,'clipboard_link_pass':False,'native_clipboard_pass':False,'browser_pass':False,
       'inputs':{},'aliases':{},'commands':[],'outputs':{},'link_artifacts':{}}
parents={};cache_metadata={};authorized=False;code=1;active_process=None

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    value=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
    return value.hexdigest()

def resolve(path,cache):
    path=pathlib.Path(path);key=str(path.parent)
    if key not in cache:cache[key]=path.parent.resolve(strict=True)
    actual=cache[key]/path.name
    if actual.is_symlink():actual=actual.resolve(strict=True)
    actual.stat();return actual

def cache_info(path):
    item=path.stat();return (item.st_size,item.st_mtime_ns,item.st_ctime_ns,item.st_mode,item.st_dev,item.st_ino)

def bind(path,digest):
    logical=pathlib.Path(path);actual=resolve(logical,parents);key=str(actual)
    require(W in actual.parents and actual.is_file() and re.fullmatch(r'[0-9a-f]{64}',digest),'Exact workspace input')
    if key in state['inputs']:require(state['inputs'][key]==digest,'Input digest conflict')
    else:
        require(sha(actual)==digest,'Changed input: '+key);state['inputs'][key]=digest
        if actual.suffix in ('.o','.a') and OUT not in actual.parents:cache_metadata[key]=cache_info(actual)
    state['aliases'][str(logical)]=key

def receipt(path,digest):
    bind(path,digest);record=json.loads(path.read_text());require('failure' not in record,'Failed prerequisite');return record

def import_inputs(record):
    for path,digest in record['inputs'].items():bind(path,digest)
    for group in ('aliases','source_view_aliases','input_symlinks'):
        for logical,actual in record.get(group,{}).items():
            require(str(resolve(logical,parents))==actual,'Source alias changed');state['aliases'][logical]=actual

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def elf_object(path):
    with path.open('rb') as stream:head=stream.read(52)
    require(len(head)==52 and head[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',head,16)==(1,3),'Actual ELF32/i386 input object')

def format_members(path):
    data=path.read_bytes();require(52<=len(data)<32*1024*1024 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Qualified whole format ELF32 object')
    offset=struct.unpack_from('<I',data,32)[0];size,count,strings=struct.unpack_from('<3H',data,46)
    require(size==40 and 0<count<65535 and strings<count and offset+40*count<=len(data),'Bounded original format section table')
    sections=[struct.unpack_from('<10I',data,offset+40*i) for i in range(count)];members={}
    for row in sections:
        require(row[1]==8 or row[4]+row[5]<=len(data),'Bounded original format section')
        if row[1]!=2:continue
        require(row[9]==16 and row[5]%16==0 and row[6]<count,'Bounded original format symbol table')
        names_row=sections[row[6]];require(names_row[1]==3 and names_row[4]+names_row[5]<=len(data),'Bounded original format names');names=data[names_row[4]:names_row[4]+names_row[5]]
        for at in range(row[4],row[4]+row[5],16):
            name,value,length,info,other,index=struct.unpack_from('<IIIBBH',data,at)
            require(name<len(names),'Bounded original format symbol name');end=names.find(b'\0',name);require(end>=name,'Terminated original format name');label=names[name:end].decode('ascii')
            if not index or info>>4 not in (1,2) or info&15!=2 or not re.fullmatch(r'_ZNK?2ui19ClipboardFormatType[A-Za-z0-9_]+',label):continue
            require(index<count and sections[index][2]&4 and not sections[index][2]&1 and value+length<=sections[index][5],'Original format member is defined in bounded executable non-W section')
            item={'value':value,'size':length,'info':info,'section':index}
            require(label not in members or members[label]==item,'Unambiguous original format ABI member');members[label]=item
    require({'_ZNK2ui19ClipboardFormatType9SerializeEv','_ZNK2ui19ClipboardFormatType7GetNameEv'}<=set(members),'Actual original public format ABI')
    return members

def inspect(path,negative):
    # B/class-link-v3.py bounded inspector, adapted to the actual clipboard ABI.
    data=path.read_bytes();require(52<=len(data)<32*1024*1024 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,3),'Actual static ELF32/i386 clipboard diagnostic')
    entry,phoff,shoff=struct.unpack_from('<III',data,24);eh,phsize,phcount,shsize,shcount,strings=struct.unpack_from('<6H',data,40)
    require(eh==52 and phsize==32 and 0<phcount<=16 and shsize==40 and 0<shcount<65535 and strings<shcount and phoff+32*phcount<=len(data) and shoff+40*shcount<=len(data),'Bounded actual ELF tables')
    loads=[];pages=set()
    for i in range(phcount):
        kind,offset,va,pa,filesz,memsz,flags,alignment=struct.unpack_from('<8I',data,phoff+32*i);require(kind not in (2,3,7),'No dynamic loader/interpreter/hardware TLS')
        if kind!=1:continue
        require(0<memsz and filesz<=memsz and offset+filesz<=len(data) and 0x40000000<=va<va+memsz<=0xc0000000 and va==pa and alignment==4096 and offset%4096==va%4096 and flags in (5,6),'Bounded non-WX actual LOAD')
        owned=set(range(va//4096,(va+memsz+4095)//4096));require(not pages&owned,'Distinct LOAD pages');pages|=owned;loads.append({'offset':offset,'va':va,'file':filesz,'memory':memsz,'flags':flags,'pages':len(owned)})
    def executable(address):return any(row['flags']==5 and row['va']<=address<row['va']+row['file'] for row in loads)
    def writable(address,length):return any(row['flags']==6 and row['va']<=address<=row['va']+row['memory']-length for row in loads)
    def payload(address,length):
        for row in loads:
            if row['va']<=address<=row['va']+row['file'] and length<=row['va']+row['file']-address:return data[row['offset']+address-row['va']:row['offset']+address-row['va']+length]
        raise RuntimeError('Required record/array/data not file backed')
    require(loads and executable(entry) and len(pages)<=4096 and 256<=len(pages)+128+1024<=4354,'Existing128 stack/1024 dynamic/4096 image profile and existing4354 total admission bound')
    sections=[struct.unpack_from('<10I',data,shoff+40*i) for i in range(shcount)];symbols={};strong_undefined=[]
    need={'NativeClipboardStart','NativeClipboardEntry','NativeClipboardChecks','native_clipboard_record','native_v8_integration_stage','native_v8_integration_result','__init_array_start','__init_array_end','__fini_array_start','__fini_array_end','exit','abort','gtos_native_heap_panic'}|set(FORMAT_ABI)
    forbidden={'NativeV8IsolateMain','NativeEntry','_start','main','NativeClassStart','NativeClassEntry','NativeBaseClassChecks','native_class_record','NativeClassLength','__wrap_NativeClassLength'}
    observed=need|forbidden
    for row in sections:
        require(not row[2]&0x400 and not(row[2]&1 and row[2]&4) and (row[1]==8 or row[4]+row[5]<=len(data)),'No hardware TLS/WX and bounded section')
        if row[2]&2 and row[5]:
            owner=[load for load in loads if load['va']<=row[3] and row[3]+row[5]<=load['va']+load['memory']]
            require(len(owner)==1 and (not row[2]&1 or owner[0]['flags']==6) and (not row[2]&4 or owner[0]['flags']==5),'Allocated section remains in a compatible W^X LOAD')
        if row[1]!=2:continue
        require(row[9]==16 and row[5]%16==0 and row[6]<shcount,'Bounded symbol table');namesrow=sections[row[6]];require(namesrow[1]==3 and namesrow[4]+namesrow[5]<=len(data),'Bounded symbol strings');names=data[namesrow[4]:namesrow[4]+namesrow[5]]
        for off in range(row[4],row[4]+row[5],16):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,off);require(name<len(names),'Bounded symbol name');end=names.find(b'\0',name);require(end>=name,'Terminated symbol name');label=names[name:end].decode('ascii')
            if index==0 and info>>4==1:strong_undefined.append(label)
            if index and label in observed:
                item={'value':value,'size':size,'info':info,'section':index};require(label not in symbols or symbols[label]==item,'Unambiguous selected ABI symbol');symbols[label]=item
    require(need<=set(symbols) and not strong_undefined and not forbidden&set(symbols),'Only clipboard entry, genuine providers, actual format ABI and zero strong undefined')
    functions={'NativeClipboardStart','NativeClipboardEntry','NativeClipboardChecks','exit','abort','gtos_native_heap_panic'}|set(FORMAT_ABI)
    require(entry==symbols['NativeClipboardStart']['value'] and entry!=symbols['NativeClipboardEntry']['value'] and all(executable(symbols[name]['value']) for name in functions),'Aligned startup and every selected genuine function is RX')
    record=symbols['native_clipboard_record'];expected=(1,int(negative),0,0,0,0,0xffffffff,0)+(0,)*56
    require(record['size']==256 and record['value']%4==0 and writable(record['value'],256) and struct.unpack('<64I',payload(record['value'],256))==expected,'Exact256-byte initialized record and distinct fixed mode')
    for name,initial in (('native_v8_integration_stage',0),('native_v8_integration_result',0xffffffff)):
        item=symbols[name];require(item['size']==4 and item['value']%4==0 and writable(item['value'],4) and struct.unpack('<I',payload(item['value'],4))[0]==initial,'Genuine exit provider diagnostic-data ABI')
    arrays={}
    for kind in ('init','fini'):
        begin=symbols['__'+kind+'_array_start']['value'];end=symbols['__'+kind+'_array_end']['value'];require(begin<=end and begin%4==end%4==0 and end-begin<=16384,'Bounded constructor/finalizer array');values=list(struct.unpack('<'+'I'*((end-begin)//4),payload(begin,end-begin))) if end>begin else [];require(all(executable(value) for value in values),'Every retained constructor/finalizer is nonnull RX');arrays[kind]=values
    return {'path':str(path),'sha256':sha(path),'bytes':len(data),'entry':entry,'loads':loads,'load_pages':len(pages),'stack_pages':128,'dynamic_pages':1024,'capacity':len(pages)+128+1024,'constructor_targets':arrays['init'],'constructor_count':len(arrays['init']),'finalizer_targets':arrays['fini'],'finalizer_count':len(arrays['fini']),'symbols':{name:symbols[name] for name in sorted(need)},'format_ABI_symbols':{name:symbols[name] for name in FORMAT_ABI},'negative_ascii_CHECK_control':negative,'expected_CHECK_exit_code':0x48000021 if negative else None,'strong_undefined':strong_undefined}

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    require(set(os.environ)<={'PATH','LANG','TZ','LC_CTYPE'} and all(os.environ.get(key)==value for key,value in {'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC'}.items()),'Minimum inherited environment required')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(Q/'launch-link-probe-v2.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    require(re.fullmatch(r'[0-9a-f]{64}',COMPILE_SHA),'PENDING complete actual compile receipt SHA; do not run until root binds it')
    require(re.fullmatch(r'[0-9a-f]{64}',PROVIDER_SHA),'PENDING complete provider source qualification receipt; do not run until root binds it')
    bind(__file__,guard['child_sha256']);bind(Q/'launch-link-probe-v2.py',guard['launcher_sha256']);bind(LD,LD_SHA);bind(SCRIPT,SCRIPT_SHA)
    bind(B/'class-link-v3.py','2603db733cd990204fcc2039d8db77cb12b851a4c7798c3d0e67e87747666176')
    root=receipt(B/'root-link-02/status.json','4ea7930d0c76bc6ea0fdca5f459d91eba763cda3bc7bc0b8fdf69a082f75cb47')
    require(all(root[key] for key in ('diagnostic_complete','actual_full_V8_link_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged')) and root['returncode']==0 and not root['errors'],'Accepted original provider pool')
    compiled=receipt(COMPILE,COMPILE_SHA)
    require(all(compiled[key] for key in ('complete','diagnostic_complete','formal_CXX_ASM_pass','full_CXX_M_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged','outputs_unchanged','original_format_object_reused_without_recompile')) and not compiled['unqualified_M_inputs'] and set(compiled['objects'])=={'checks','start','positive-entry','negative-entry'},'Four complete actual GN source-qualified fixture objects')
    original=receipt(P/'clipboard-format-compile-02/status.json','0b7e704222a57503715e3d09d979e714d80665db43ebc22ef40ffaab33e58d11')
    require(all(original[key] for key in ('complete','clipboard_format_compile_pass','complete_M_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged','outputs_unchanged')) and original['M_dependency_count']==933 and original['outputs'].get(str(FORMAT))==FORMAT_SHA,'Whole original actual format object and full933-dependency qualification')
    reuse=compiled['reused_format_object'];require(reuse['path']==str(FORMAT) and reuse['sha256']==FORMAT_SHA and not reuse['recompiled'] and reuse['M_dependencies']==933,'No format recompilation or replacement')
    state['stage']='bind_original_and_new_source_inputs';emit()
    for record in (root,compiled,original):import_inputs(record)
    bind(FORMAT,FORMAT_SHA);elf_object(FORMAT)
    state['qualified_format_object_ABI']=format_members(FORMAT)
    demanded={name for name in compiled['objects']['checks']['undefined_symbols'] if re.fullmatch(r'_ZNK?2ui19ClipboardFormatType[A-Za-z0-9_]+',name)}
    require(demanded and demanded<=set(state['qualified_format_object_ABI']),'Every actual fixture format dependency is defined by the qualified original whole object')
    FORMAT_ABI=tuple(sorted(demanded & set(state['qualified_format_object_ABI'])))
    critical=('^_ZNK2ui19ClipboardFormatType9SerializeEv$','^_ZNK2ui19ClipboardFormatType7GetNameEv$',
              '^_ZN2ui19ClipboardFormatType11DeserializeE','^_ZN2ui19ClipboardFormatType18CustomPlatformTypeE',
              '^_ZNK2ui19ClipboardFormatTypeeqE','^_ZNK2ui19ClipboardFormatTypeltE')
    require(all(any(re.search(pattern,name) for name in FORMAT_ABI) for pattern in critical),'Actual consumed serialization/custom/comparison ABI remains required; do not force unused private members')
    # This old argv is trusted receipt DATA only. Independently authored LLD argv follows below.
    old_prefix=[str(LD),'-m','elf_i386','-T',str(W/'artifacts/native-iostream-20261014-a/gn-06/source/tools/gtos_isolate_diagnostic/native.ld'),'--gc-sections','-Map=native_embedder.map','-o','native_embedder.elf','--wrap=GtosPartitionPlatformChecks','--error-limit=0']
    require(root['executed_argv'][:11]==old_prefix,'Fixed accepted object-inventory offset')
    original_objects=root['executed_argv'][11:];require(len(original_objects)==len(set(original_objects))==1256,'Whole1256 accepted object inventory as data')
    excluded=[W/'artifacts/native-getenv-20261017-a/entry-02/build/obj/tools/gtos_isolate_diagnostic/native_embedder.native-entry.o',W/'artifacts/native-v8-integration-20261011-a/wide-runtime-resume-30/build/obj/tools/gtos_isolate_diagnostic/native_embedder.native-start.o',W/'artifacts/native-partition-consumer-20261016-a/compile-main-01/build/obj/tools/gtos_isolate_diagnostic/consumer.main.o']
    require(all(original_objects.count(str(path))==1 for path in excluded),'Exact three old entry/start/main objects')
    retained=[path for path in original_objects if path not in {str(path) for path in excluded}];require(len(retained)==1253,'No additional legacy class providers added')
    for path in retained:
        logical=pathlib.Path(path);actual=resolve(logical,parents);require(logical.suffix in ('.o','.a') and W in actual.parents and root['inputs'].get(str(actual))==state['inputs'].get(str(actual)) and root['aliases'].get(str(logical),str(logical))==str(actual),'Every retained object has accepted exact provenance')
    for path,digest in ((ABORT,ABORT_SHA),(HEAP,HEAP_SHA),(EXIT,EXIT_SHA)):
        require(str(path) in retained and root['inputs'].get(str(resolve(path,parents)))==digest,'Exact genuine abort/heap/exit provider is retained');bind(path,digest)
    abort_source=W/'artifacts/native-v8-integration-20261011-a/wide-runtime-resume-30/module-input-snapshot/overlay/tools/gtos_isolate_diagnostic/runtime/abort.cc'
    bind(abort_source,'696699e47eb964cae22e77a6241a10971b13c1b13c9a09d19f2b23bfc8cc96bd');require('gtos_native_heap_panic(0x21U);' in abort_source.read_text(),'Real abort forwards exact CHECK panic code')
    provider=receipt(PROVIDER,PROVIDER_SHA)
    require(all(provider[key] for key in ('complete','provider_source_qualification_pass','bound_inputs_unchanged','aliases_unchanged','outputs_unchanged','old_cache_metadata_unchanged','heap_new_M_qualified','exit_locked_M_MD_requalified','exit_readonly_preflight_before_Clang','exit_new_M_qualified','exit_materialized_physical_sources_only')) and provider['exit_legacy_source_view_consumed'] is False and not provider['recompiled_provider_objects'] and provider['new_Clang_M_commands']==2 and provider['new_full_M_heap_count']==75 and provider['exit_locked_M_MD_count']==113 and provider['exit_unique_dependency_count']==provider['new_full_M_exit_count']==111 and provider['new_full_M_exit_raw_count']==113 and not provider['heap_old_depfile_hashes_historical'],'Two independently typed new full-M actions, unchanged provider objects, full exit raw113/physical111 closure from locked physical post-copy sources')
    require(provider['provider_objects']=={'heap':{'path':str(HEAP),'sha256':HEAP_SHA},'exit':{'path':str(EXIT),'sha256':EXIT_SHA}},'Same accepted exact provider object bytes')
    import_inputs(provider)
    for path,digest in provider['outputs'].items():bind(path,digest)
    state['provider_symlink_nodes']=provider['symlink_nodes']
    require(all(pathlib.Path(path).is_symlink() and os.readlink(path)==target for path,target in state['provider_symlink_nodes'].items()),'Actual consumed provider symlink nodes unchanged')
    state.update(provider_source_receipt_sha256=PROVIDER_SHA,heap_new_M_qualified=True,exit_locked_M_MD_requalified=True,exit_new_M_qualified=True,exit_materialized_physical_sources_only=True,exit_legacy_source_view_consumed=False,provider_source_scope='Unit-qualified consumed source dependencies only. Two new independently typed full-M actions retain the old exact objects; exit uses its originally compiled, historically locked physical post-copy source view, raw113/physical111. Old pre-M text remains historical data; its unused deep source aliases are not traversed or claimed unchanged. Old aggregate receipts are locked data, without unchanged claims for unconsumed public manifests.')
    heap_source=W/'artifacts/native-large-heap-20261015-a/patch-v2/runtime/heap.cc';heap_header=heap_source.with_name('heap.h')
    bind(heap_source,'b10c3497cd8f2e3659044cca847f73fe21042b31f0951e14d5e1dbf297400308');bind(heap_header,'2ccee048e304672a0a978fefd95e5b3e625a3639d8c70b31ab86bf75999ccea2')
    require('Call(GTOS_SYS_EXIT, GTOS_NATIVE_HEAP_EXIT_BASE | code, 0);' in heap_source.read_text() and '#define GTOS_NATIVE_HEAP_EXIT_BASE 0x48000000U' in heap_header.read_text(),'Expected real CHECK exit0x48000021; guest must still verify nonfault exit/Reap')
    exit_source=W/'artifacts/native-iostream-20261014-a/compile-thread-03/source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc'
    require(provider['inputs'].get(str(exit_source))=='7848755069a7d45ab00fd9896d883a9de78fc671c1f1e54cf946ef29a79c4791','Actual exit full-M consumes the original compiled physical process-exit source')
    bind(exit_source,'7848755069a7d45ab00fd9896d883a9de78fc671c1f1e54cf946ef29a79c4791')
    new={}
    for name,item in compiled['objects'].items():
        bind(item['path'],item['sha256']);elf_object(pathlib.Path(item['path']));require(compiled['outputs'].get(item['path'])==item['sha256'],'Exact compiled output identity')
        if name=='start':
            require(item['standalone_ASM'] and not item['depfile_generated'] and not item['MD_evidence_fabricated'] and item['MD_sha256'] is None and item['raw_standalone_ASM_one_explicit_source'] and item['exact_ignored_MD_MF_diagnostics'] and len(item['dependencies'])==1 and set(item['dependencies'].values())=={item['source']},'Actual standalone lowercase.s has one pinned source and no fabricated MD')
            bind(item['source'],item['source_sha256'])
        else:
            require(not item['standalone_ASM'] and item['depfile_generated'] and not item['MD_evidence_fabricated'],'Actual CXX complete M=MD evidence')
            bind(item['path']+'.d',item['MD_sha256'])
        new[name]=item['path']
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    state.update(excluded_diagnostic_objects=[str(path) for path in excluded],retained_provider_objects=retained,original_provider_objects=1256,retained_provider_count=1253,expected_native_CHECK_exit_code=0x48000021,format_ABI_reviewed_symbols=list(FORMAT_ABI))
    for name,negative in (('positive',False),('negative',True)):
        ordered=retained+[str(FORMAT),new['start'],new['negative-entry' if negative else 'positive-entry'],new['checks']]
        require(len(ordered)==len(set(ordered))==1257,'Only1253 original providers and four exact new/reused objects')
        output=OUT/(name+'.elf');map_path=OUT/(name+'.map');log=OUT/(name+'-link.log')
        argv=[str(LD),'-m','elf_i386','-T',str(SCRIPT),'--gc-sections','-Map='+str(map_path),'-o',str(output),'--error-limit=0']+ordered
        require(not any(argument.startswith('--wrap') for argument in argv),'No wrapped API or old argv replay')
        state['stage']=name+'_actual_LLD_link';entry={'name':name,'argv':argv,'shell':False,'reused_ELF':False,'state':'starting'};state['commands'].append(entry);emit();began=time.monotonic()
        with log.open('xb') as stream:
            active_process=subprocess.Popen(argv,cwd=OUT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT)
            entry.update(state='running',pid=active_process.pid);emit()
            try:result=active_process.wait(timeout=180)
            except subprocess.TimeoutExpired:active_process.kill();active_process.wait();result=124
            entry.update(state='completed',returncode=result,seconds=time.monotonic()-began);active_process=None
        entry['log_sha256']=sha(log);emit();require(result==0 and log.stat().st_size<8*1024*1024,'Actual LLD failed; preserve bounded log')
        require(map_path.is_file() and not map_path.is_symlink() and 0<map_path.stat().st_size<32*1024*1024,'Bounded new actual LLD map')
        state['outputs'][name]=inspect(output,negative)
        state['link_artifacts'][str(log)]=sha(log);state['link_artifacts'][str(map_path)]=sha(map_path);emit()
        print('PASS actual new '+name+' LLD/ELF clipboard metadata',flush=True)
    state['stage']='terminal_source_alias_cache_output_checks';emit();fresh={}
    require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()),'Terminal source/alias change')
    require(all(cache_info(pathlib.Path(path))==metadata for path,metadata in cache_metadata.items()),'Original cache metadata changed')
    require(all(pathlib.Path(path).is_symlink() and os.readlink(path)==target for path,target in state['provider_symlink_nodes'].items()),'Provider symlink node changed during links')
    require(all(sha(path)==digest for path,digest in state['link_artifacts'].items()) and all(sha(item['path'])==item['sha256'] for item in state['outputs'].values()),'Linked ELF/map/log changed')
    require(all(not path.is_symlink() for path in OUT.rglob('*')),'Only new regular link artifacts')
    state.update(complete=True,clipboard_link_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,old_cache_metadata_unchanged=True,outputs_unchanged=True,all_link_children_terminal=True,stage='completed',scope='Two actual new LLD ELF32/i386 clipboard diagnostics, exact1253-provider pool plus whole original format/checks/aligned startup/fixed mode entry, no wrap/patch/reused ELF. Selected ABI unique, zero strong undefined, exact record modes, genuine exit/abort/heap providers, bounded constructors/finalizers and W^X LOADs. CHECK expected exit0x48000021 is source evidence only; native semantics, nonfault exit and Reap remain guest gates. No desktop clipboard/Content/browser/web/video/HTML5 claim.');code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc())
    traceback.print_exc()
finally:
    if active_process is not None:
        if active_process.poll() is None:active_process.kill()
        result=active_process.wait();entry.update(state='completed',returncode=result,aborted_after_driver_failure=True);state['aborted_link_child']={'pid':active_process.pid,'returncode':result,'terminal':True}
    if authorized:state['returncode']=code;emit()
sys.exit(code)
