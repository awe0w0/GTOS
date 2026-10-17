#!/usr/bin/env python3
"""Link a separate original-class diagnostic, never an original V8 root claim."""
import ctypes,hashlib,json,os,pathlib,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';OUT=P/'class-link-03';LD=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/ld.lld';SCRIPT=P/'class-v2.ld';PRIOR=P/'class-link-02'
CLASS_SHA='6fe51eee798fd3ab4b5462f5b763b2e1bdb49ab48535b7b71cae392295cc8cfc';SCRIPT_SHA='4f95888623b0d0b5e401305fb7c2137aa612cea1d07d2b55b6bfc65498025ffe'
state={'complete':False,'separate_class_link_pass':False,'native_class_pass':False,'original_V8_root_replayed':False,'browser_pass':False,'inputs':{},'aliases':{},'commands':[],'outputs':{}};parents={};metadata={};authorized=False;code=1

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def resolve(path,cache):
    path=pathlib.Path(path);key=str(path.parent)
    if key not in cache:cache[key]=path.parent.resolve(strict=True)
    actual=cache[key]/path.name
    if actual.is_symlink():actual=actual.resolve(strict=True)
    actual.stat();return actual

def bind(path,digest):
    actual=resolve(path,parents);key=str(actual);require(W in actual.parents and actual.is_file() and len(digest)==64,'Fixed workspace input')
    if key in state['inputs']:require(state['inputs'][key]==digest,'Input digest conflict')
    else:
        require(sha(actual)==digest,'Changed input: '+key);state['inputs'][key]=digest
        if actual.suffix in ('.o','.a'):item=actual.stat();metadata[key]=(item.st_size,item.st_mtime_ns)
    state['aliases'][str(path)]=key

def receipt(path,digest):
    bind(path,digest);record=json.loads(path.read_text());require('failure' not in record,'Failed prerequisite');return record

def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def inspect(path,negative):
    data=path.read_bytes();require(52<=len(data)<32*1024*1024 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,3),'Actual static ELF32/i386 class diagnostic')
    entry,phoff,shoff=struct.unpack_from('<III',data,24);eh,phsize,phcount,shsize,shcount,strings=struct.unpack_from('<6H',data,40)
    require(eh==52 and phsize==32 and 0<phcount<=16 and shsize==40 and 0<shcount<65535 and strings<shcount and phoff+32*phcount<=len(data) and shoff+40*shcount<=len(data),'Bounded actual ELF tables')
    loads=[];pages=set()
    for i in range(phcount):
        kind,offset,va,pa,filesz,memsz,flags,alignment=struct.unpack_from('<8I',data,phoff+32*i);require(kind not in (2,3,7),'No interpreter/dynamic loader/hardware TLS')
        if kind!=1:continue
        require(0<memsz and filesz<=memsz and offset+filesz<=len(data) and 0x40000000<=va<va+memsz<=0xc0000000 and va==pa and alignment==4096 and offset%4096==va%4096 and flags in (5,6),'Bounded non-WX actual LOAD')
        owned=set(range(va//4096,(va+memsz+4095)//4096));require(not pages&owned,'Distinct LOAD pages');pages|=owned;loads.append({'offset':offset,'va':va,'file':filesz,'memory':memsz,'flags':flags,'pages':len(owned)})
    def executable(value):return any(row['flags']==5 and row['va']<=value<row['va']+row['file'] for row in loads)
    def payload(address,length):
        for row in loads:
            if row['va']<=address<=row['va']+row['file'] and length<=row['va']+row['file']-address:return data[row['offset']+address-row['va']:row['offset']+address-row['va']+length]
        raise RuntimeError('Required array or record not file backed')
    require(loads and executable(entry) and len(pages)<=4096 and 256<=len(pages)+128+1024<=4354,'Existing large trusted admission profile only')
    sections=[struct.unpack_from('<10I',data,shoff+40*i) for i in range(shcount)];symbols={};strong_undefined=[]
    need={'NativeClassStart','NativeClassEntry','NativeBaseClassChecks','native_class_record','native_v8_integration_stage','native_v8_integration_result','__init_array_start','__init_array_end','__fini_array_start','__fini_array_end','strnlen','_ZN4base7SysInfo23VMAllocationGranularityEv'};observed=need|{'NativeClassLength','__wrap_NativeClassLength','NativeV8IsolateMain','NativeEntry','_start'}
    for row in sections:
        require(not row[2]&0x400 and (row[1]==8 or row[4]+row[5]<=len(data)),'No hardware TLS; bounded section')
        if row[1]!=2:continue
        require(row[9]==16 and row[5]%16==0 and row[6]<shcount,'Bounded symbol table');namesrow=sections[row[6]];require(namesrow[1]==3 and namesrow[4]+namesrow[5]<=len(data),'Bounded symbol strings');names=data[namesrow[4]:namesrow[4]+namesrow[5]]
        for off in range(row[4],row[4]+row[5],16):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,off);require(name<len(names),'Bounded symbol name');end=names.find(b'\0',name);require(end>=name,'Terminated symbol name');label=names[name:end].decode('ascii')
            if index==0 and info>>4==1:strong_undefined.append(label)
            if index and label in observed:
                item={'value':value,'size':size,'info':info,'section':index};require(label not in symbols or symbols[label]==item,'Unambiguous selected entry/record/provider ABI symbol');symbols[label]=item
    require(need<=set(symbols) and not strong_undefined and not {'NativeV8IsolateMain','NativeEntry','_start'}&set(symbols),'Separate class entry, genuine string provider, no original V8 entry/main')
    require(entry==symbols['NativeClassStart']['value'] and entry!=symbols['NativeClassEntry']['value'] and executable(symbols['NativeClassEntry']['value']) and executable(symbols['strnlen']['value']) and executable(symbols['_ZN4base7SysInfo23VMAllocationGranularityEv']['value']),'Aligned startup, real CXX entry, native SysInfo and genuine strnlen retained')
    require(('__wrap_NativeClassLength' in symbols)==negative and (negative or 'NativeClassLength' in symbols),'Only explicit diagnostic API wrong control')
    record=symbols['native_class_record'];require(record['size']==256 and record['value']%4==0 and any(row['flags']==6 and row['va']<=record['value']<=row['va']+row['memory']-256 for row in loads) and struct.unpack('<64I',payload(record['value'],256))==(1,)+(0,)*63,'Exact initialized bounded class record')
    for name,initial in (('native_v8_integration_stage',0),('native_v8_integration_result',0xffffffff)):
        item=symbols[name];require(item['size']==4 and item['value']%4==0 and any(row['flags']==6 and row['va']<=item['value']<=row['va']+row['memory']-4 for row in loads) and struct.unpack('<I',payload(item['value'],4))[0]==initial,'Actual real exit provider data ABI and initialized value')
    arrays={}
    for kind in ('init','fini'):
        begin=symbols['__'+kind+'_array_start']['value'];end=symbols['__'+kind+'_array_end']['value'];require(begin<=end and begin%4==end%4==0 and end-begin<=16384,'Bounded actual constructor/finalizer array');values=list(struct.unpack('<'+'I'*((end-begin)//4),payload(begin,end-begin))) if end>begin else [];require(all(executable(value) for value in values),'Every retained constructor/finalizer target is nonnull RX');arrays[kind]=values
    return {'sha256':sha(path),'bytes':len(data),'entry':entry,'loads':loads,'load_pages':len(pages),'capacity':len(pages)+128+1024,'constructor_targets':arrays['init'],'constructor_count':len(arrays['init']),'finalizer_targets':arrays['fini'],'finalizer_count':len(arrays['fini']),'symbols':{name:symbols[name] for name in sorted(need)},'negative_diagnostic_API_control':negative,'strong_undefined':strong_undefined}

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-class-link-v3.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch');authorized=True
    bind(LD,'99ef321b93b2f3c8214bb592b633b3736d2df70f51e5454920131631f08e3412');require(len(CLASS_SHA)==len(SCRIPT_SHA)==64,'Terminal CXX and static linker script review required');bind(SCRIPT,SCRIPT_SHA)
    root=receipt(P/'root-link-02/status.json','4ea7930d0c76bc6ea0fdca5f459d91eba763cda3bc7bc0b8fdf69a082f75cb47');require(all(root[key] for key in ('diagnostic_complete','actual_full_V8_link_pass','bound_inputs_unchanged','aliases_unchanged','old_cache_metadata_unchanged')) and root['returncode']==0 and not root['errors'],'Previously accepted genuine provider pool')
    classes=receipt(P/'class-compile-04/status.json',CLASS_SHA);require(all(classes[key] for key in ('complete','diagnostic_CXX_pass','whole_UTF_provider_CXX_pass','whole_SysInfo_candidate_CXX_pass','standalone_aligned_startup_ASM_pass','full_M_MD_source_qualification','bound_inputs_unchanged','aliases_unchanged')) and not classes['unqualified_M_inputs'] and len(classes['objects'])==8,'Complete source-qualified diagnostic, aligned startup, whole original UTF and real SysInfo candidate')
    audit=receipt(P/'class-symbol-audit-01.json','99682d8d86161b6c7be4062afe0b080056222e1406d6a7e62fbcb92ca7a6e319');require(audit['complete'] and audit['read_only'] and not audit['target_executed'] and audit['current_duplicate_names_preexisting_in_accepted_original'] and audit['required_ABI_symbols_have_no_collisions'] and len(audit['classes']['duplicates'])==3,'Read-only original-root comparison proves only preexisting unselected libc++ name duplicates')
    bind(P/'root-link-02/native_embedder.elf',audit['original']['sha256']);bind(PRIOR/'classes.elf',audit['classes']['sha256']);bind(PRIOR/'status.json','8d0fe1972f9f4fe7be4d4956b0cbeb0a40fcfef932e7246025f8c45977d4040d');prior=json.loads((PRIOR/'status.json').read_text());require(not prior['complete'] and prior['failure']=='Unambiguous global symbol' and not prior['outputs'] and len(prior['commands'])==1 and prior['commands'][0]['name']=='classes' and prior['commands'][0]['returncode']==0,'Exact preserved successful LLD followed only by overbroad metadata-check failure')
    state['stage']='bind_complete_original_and_new_source_inputs';emit()
    for item in (root,classes):
        for path,digest in item['inputs'].items():bind(path,digest)
        for group in ('aliases','source_view_aliases'):
            for logical,actual in item.get(group,{}).items():require(str(resolve(logical,parents))==actual,'Source alias changed');state['aliases'][logical]=actual
    original=root['executed_argv'][11:];require(len(original)==len(set(original))==1256 and root['executed_argv'][:3]==[str(LD),'-m','elf_i386'],'Validated receipt inventory is object data only, no command replay')
    excluded=[W/'artifacts/native-getenv-20261017-a/entry-02/build/obj/tools/gtos_isolate_diagnostic/native_embedder.native-entry.o',W/'artifacts/native-v8-integration-20261011-a/wide-runtime-resume-30/build/obj/tools/gtos_isolate_diagnostic/native_embedder.native-start.o',W/'artifacts/native-partition-consumer-20261016-a/compile-main-01/build/obj/tools/gtos_isolate_diagnostic/consumer.main.o'];require(all(original.count(str(path))==1 for path in excluded),'Exact three old diagnostic entry/main objects excluded')
    retained=[path for path in original if path not in {str(path) for path in excluded}]
    for path in retained:
        logical=pathlib.Path(path);require(path.startswith(str(W)+'/') and logical.suffix in ('.o','.a'),'Only workspace object/archive inventory');actual=resolve(logical,parents);require(root['inputs'].get(str(actual))==state['inputs'].get(str(actual)) and root['aliases'].get(str(logical),str(logical))==str(actual),'Every retained object has exact accepted provenance')
    new={}
    for name,item in classes['objects'].items():require(name in ('entry','checks','length','wrong-length','utf_string_conversions','utf_string_conversion_utils','sys_info','start'),'Exact eight qualified class objects');bind(item['path'],item['sha256']);new[name]=item['path']
    for name in ('home','tmp'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')};state['excluded_diagnostic_objects']=[str(path) for path in excluded];state['retained_provider_objects']=retained
    for name,negative in (('classes',False),('wrong-length',True)):
        added=[new[key] for key in ('start','entry','checks','length','utf_string_conversions','utf_string_conversion_utils','sys_info')]+([new['wrong-length']] if negative else []);ordered=retained+added;require(len(ordered)==len(set(ordered)),'Unique exact provider inventory');output=OUT/(name+'.elf');log=OUT/(name+'-link.log');argv=[str(LD),'-m','elf_i386','-T',str(SCRIPT),'--gc-sections','--undefined=NativeClassLength','-Map='+str(OUT/(name+'.map')),'-o',str(output),'--error-limit=0']+(['--wrap=NativeClassLength'] if negative else [])+ordered
        state['stage']=name+'_link';emit()
        if not negative:
            expected=[str(LD),'-m','elf_i386','-T',str(SCRIPT),'--gc-sections','--undefined=NativeClassLength','-Map='+str(PRIOR/'classes.map'),'-o',str(PRIOR/'classes.elf'),'--error-limit=0']+ordered;require(prior['commands'][0]['argv']==expected and all(state['inputs'].get(path)==digest for path,digest in prior['inputs'].items()),'Exact typed successful original link recipe and all source/object inputs, no argv replay');bind(PRIOR/'classes-link.log',prior['commands'][0]['log_sha256']);require((PRIOR/'classes-link.log').stat().st_size==0,'Clean original successful LLD output')
            with output.open('xb') as stream:stream.write((PRIOR/'classes.elf').read_bytes())
            require(sha(output)==audit['classes']['sha256'],'Reused successful ELF copied byte for byte as data');state['commands'].append({'name':name,'reused_successful_LLD_output':True,'prior_receipt_sha256':state['inputs'][str(PRIOR/'status.json')],'prior_actual_argv':expected,'prior_returncode':0,'source_ELF_sha256':sha(PRIOR/'classes.elf'),'copied_ELF_sha256':sha(output)})
        else:
            with log.open('xb') as stream:result=subprocess.run(argv,cwd=OUT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
            state['commands'].append({'name':name,'argv':argv,'returncode':result.returncode,'log_sha256':sha(log)});emit();require(result.returncode==0 and log.stat().st_size<8*1024*1024,'Link failed; inspect retained bounded log')
        state['outputs'][name]=inspect(output,negative);emit();print('PASS separate '+name+' link/ELF metadata',flush=True)
    state['stage']='terminal_source_alias_cache_checks';emit();fresh={};require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(resolve(logical,fresh))==actual for logical,actual in state['aliases'].items()),'Terminal source/alias changed');require(all((pathlib.Path(path).stat().st_size,pathlib.Path(path).stat().st_mtime_ns)==info for path,info in metadata.items()),'Prior cache metadata changed')
    state.update(complete=True,separate_class_link_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,old_cache_metadata_unchanged=True,reused_successful_positive_ELF=True,selected_ABI_symbols_unambiguous=True,original_root_unselected_libcxx_duplicates_verified=True,stage='completed',scope='Separate original FilePath/LocalPMA/strnlen class diagnostics plus explicit wrong-length API control. Aligned diagnostic startup and real exit data, genuine UTF converters and whole SysInfo with real native page unit. Original V8 main/start/NativeEntry excluded. Successful positive ELF reused byte for byte after exact typed original recipe and full input requalification; only negative link newly executed. All selected ABI symbols unique, zero strong undefined; three unselected libc++ name duplicates proven present in accepted original root. Constructors/finalizers and LOADs bounded. No native class or browser/image acceptance; FilePMA flush and other SysInfo native backends remain unsupported.');emit();code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(code)
