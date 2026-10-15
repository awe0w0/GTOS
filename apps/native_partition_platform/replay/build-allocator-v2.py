#!/usr/bin/env python3
"""Build all seventeen actual allocator_base members, with checked cache reuse."""
import ctypes
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import struct
import subprocess
import sys
import traceback

W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-partition-platform-20261016-a'
Q=W/'artifacts/native-content-sdk-20261016-a'
RUN=W/'artifacts/native-rust-gtos-20261015-a/graph-30'
OUT=P/'allocator-build-02'
BUILD=OUT/'build'
ROOT=OUT/'source'
CLANG=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
AR=CLANG.parent/'llvm-ar'
state={'diagnostic_complete':False,'browser_guest_pass':False,'partition_runtime_qualified':False,
       'complete_graph_input_qualification':False,'archive_pass':False,'target_objects_compile_pass':False,
       'inputs':{},'aliases':{},'results':{}}
authorized=False

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()

def bind(path,digest):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents,'Escaped selected input')
    # Initial hash once per resolved file; duplicate bindings must agree.
    # Every bound file is hashed again during final verification.
    if str(actual) in state['inputs']:require(state['inputs'][str(actual)]==digest,'Conflicting input')
    else:require(sha(actual)==digest,'Changed selected input: '+str(path))
    state['inputs'][str(actual)]=digest
    if logical!=actual:state['aliases'][str(logical)]=str(actual)

def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def object_symbols(path):
    data=path.read_bytes()
    require(data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Expected ELF32/i386 object')
    offset=struct.unpack_from('<I',data,32)[0];size,count=struct.unpack_from('<HH',data,46)
    require(size==40 and offset+size*count<=len(data),'Invalid section table')
    sections=[struct.unpack_from('<10I',data,offset+size*index) for index in range(count)]
    require(not any(section[2]&0x400 for section in sections),'Unexpected hardware TLS')
    undefined=set()
    for section in sections:
        if section[1]!=2:continue
        require(section[9]==16 and section[5]%16==0 and section[6]<count,'Invalid symbol table')
        strings=sections[section[6]]
        require(section[4]+section[5]<=len(data) and strings[4]+strings[5]<=len(data),'Escaped symbol data')
        table=data[strings[4]:strings[4]+strings[5]]
        for location in range(section[4],section[4]+section[5],16):
            name,_,_,_,_,index=struct.unpack_from('<IIIBBH',data,location)
            require(name<len(table),'Invalid symbol name')
            if name and index==0:
                end=table.find(b'\0',name);require(end>=name,'Unterminated symbol')
                undefined.add(table[name:end].decode('ascii'))
    return sorted(undefined)

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry required')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-allocator-v2.py')
            and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
            and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required')
    authorized=True
    bind(CLANG,'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7')
    row_path=P/'compile-rows-01.json';bind(row_path,'c6f81c6cf780f8c344ea9ce63df5c3458537d1f78425561e24aafe9c32559ca0')
    rows=json.loads(row_path.read_text())
    bind(RUN/'status.json','7a05215fee189228ecc148135d935dc6c30704c32314ad39d06c77f8f119a572')
    graph=json.loads((RUN/'status.json').read_text())
    require(graph['diagnostic_complete'] and graph['gn_returncode']==0 and 'failure' not in graph,'Successful actual GN graph required')
    batch_path=P/'allocator-base-inputs-02.json'
    bind(batch_path,'aa39ff932b92f16c134dd12dc8e4e72c378e14216b779f94b3c6930d91746189')
    batch=json.loads(batch_path.read_text())
    require(batch['total_cxx_rows']==17 and batch['new_compiles']==9 and batch['reuse_qualified']==8
            and len(batch['rows'])==17 and batch['command_template']==rows['command_template']
            and batch['fields']==rows['rows']['native_log']['fields']
            and batch['graph_status_sha256']==sha(RUN/'status.json'),
            'Reviewed complete allocator target scope changed')
    bind(RUN/'build/toolchain.ninja',batch['toolchain_sha256'])
    bind(RUN/'build'/batch['target'],batch['target_sha256'])
    part_path=P/'candidate-inputs-03.json'
    bind(part_path,'0241d867428a8328d9ec61c7fb8320324c5730e6ef78827b99f0d98b29630f31')
    part=json.loads(part_path.read_text())
    sdk_path=Q/'candidate-inputs-01.json';bind(sdk_path,'84724e78d4422b325e7a6e6c47a0ab8f966f2c1f3c4aac5020f8b3e77917bcb4')
    for relative,item in json.loads(sdk_path.read_text())['files'].items():
        bind(item['path'],item['sha256'])
        if relative not in part['files']:bind(RUN/'source'/relative,item['sha256'])
    for relative,item in part['files'].items():
        bind(item['path'],item['sha256']);bind(RUN/'source'/relative,item['sha256'])
    dawn_path=W/'artifacts/native-dawn-gtos-20261016-a/candidate-inputs-01.json'
    bind(dawn_path,'e2a98ddf8b3112d281909a54cc2243a52c869bbaa796fa5f3fd23c35c86103a0')
    dawn=json.loads(dawn_path.read_text())
    require(set(dawn['files'])=={'third_party/dawn/src/dawn/common/BUILD.gn','third_party/dawn/src/utils/platform.h'},
            'Dawn overlay scope changed')
    for relative,item in dawn['files'].items():
        bind(item['path'],item['sha256']);bind(RUN/'source'/relative,item['sha256'])
    for relative,item in batch['upstream_sources'].items():
        bind(W/'sources/chromium-full/src'/relative,item['sha256'])
        if relative not in part['files']:bind(RUN/'source'/relative,item['sha256'])
    native_path=W/'sources/GTOS/apps/native_v8_integration/source-lock.json'
    bind(native_path,'df90671239c1e647c564a9b825e97f840177eaad8b74980a6ce345ed3b7c2946')
    native=json.loads(native_path.read_text())
    require(native['tool_sha256']['llvm-ar']=='1929714d5474b3aeaf0e4eed5eb6d9c9af73c569b9565d6463953d4af6e34572',
            'Official native archiver source lock changed')
    bind(AR,native['tool_sha256']['llvm-ar'])
    for relative,digest in native['prepared_source_files'].items():
        if relative.startswith(('third_party/libc++/src/include/','third_party/libc++abi/src/include/')):
            bind(RUN/'source'/relative,digest)
    for relative,digest in native['clang_resource_files'].items():
        bind(W/'toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include'/relative,digest)
    headers_path=P/'consumer-headers-02.json'
    bind(headers_path,'ec41dee96ad51bd5eea3e3d7d489ef51f11e711c59fe485c964d031e8bdcc29f')
    for relative,item in json.loads(headers_path.read_text())['files'].items():
        bind(W/'sources/chromium-full/src'/relative,item['sha256'])
        if relative not in part['files'] and relative not in dawn['files']:
            bind(RUN/'source'/relative,item['sha256'])
    extra_headers=P/'native-log-headers-01.json'
    bind(extra_headers,'008c6815e5432a087b6edb32aef3f055d076a3c631d383c9b7038fc78dff10c6')
    for relative,item in json.loads(extra_headers.read_text())['files'].items():
        bind(W/'sources/chromium-full/src'/relative,item['sha256'])
        if relative not in part['files'] and relative not in dawn['files']:
            bind(RUN/'source'/relative,item['sha256'])
    full_headers=P/'allocator-headers-01.json'
    bind(full_headers,'0756f31563f4a9322e72f398ee0265724eaae6fda0b29c80890a131da114f2f5')
    for relative,item in json.loads(full_headers.read_text())['files'].items():
        bind(W/'sources/chromium-full/src'/relative,item['sha256'])
        if relative not in part['files'] and relative not in dawn['files']:
            bind(RUN/'source'/relative,item['sha256'])
    previous_path=P/'compile-02/status.json'
    bind(previous_path,'37065edc0a1557afe12cb63ae622a3ef2f4925275dd5267c04f3fa04344d8419')
    previous=json.loads(previous_path.read_text())
    require(previous['diagnostic_complete'] and previous['mixed_identity_rejected']
            and previous['native_thread_no_heap_or_tls_imports'] and 'failure' not in previous,
            'Previous complete consumer/thread qualification required')
    for name in ('platform-abi','AlignedAlloc','native_thread'):
        item=previous['results'][name]
        require(item['returncode']==0 and item['header_closure_qualified'],'Previous object was not qualified')
        bind(P/'compile-02'/(name+'.o'),item['object_sha256'])
        dep=P/'compile-02'/(name+'.o.d')
        bind(dep,sha(dep))
        require('/partition_alloc_base/strings/safe_sprintf.h' not in dep.read_text(),
                'Header fix affects an earlier positive object; replay that object too')
    state['retained_positive_objects']={name:previous['results'][name]['object_sha256']
                                      for name in ('platform-abi','AlignedAlloc','native_thread')}
    state['header_delta_not_in_retained_object_dependencies']=True
    log_status=P/'compile-03/status.json'
    bind(log_status,'01cc92dd20cb68acf679fbad865bff721346a4a74c2b3fb5a0747954aefae6aa')
    log_qualification=json.loads(log_status.read_text())
    require(log_qualification['diagnostic_complete'] and 'failure' not in log_qualification
            and log_qualification['results']['native_log']['returncode']==0
            and log_qualification['results']['native_log']['header_closure_qualified'],
            'Native log object not qualified')
    bind(P/'compile-03/native_log.o','016a12fc7c4c11c72a4383a90534ac578dbcf005f7d89a00f924ef76374169a2')
    state['target_reused_objects']={
        'log_message':{'path':str(P/'compile-03/native_log.o'),'sha256':'016a12fc7c4c11c72a4383a90534ac578dbcf005f7d89a00f924ef76374169a2'},
        'platform_thread_gtos':{'path':str(P/'compile-02/native_thread.o'),'sha256':previous['results']['native_thread']['object_sha256']}}
    bind(P/'platform-abi.cc','7a1489395b1779ceb4ee4a17349d8ff0d636eea0d98eae01906bca385ba270d3')
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    ROOT.symlink_to(RUN/'source',target_is_directory=True);BUILD.mkdir()
    relative='base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/strings/safe_sprintf.h'
    bind(P/'header-base'/relative,'3b0bf924cfc2fcf2b30c928af0369a5f58f8919b8a33cced87276bdbd4c06c8e')
    patch=P/'header-patch'/relative
    bind(patch,'003b6641f595923721987db57b1bbf718e9f6505cebc189fb6eb32b5ac3887d8')
    bind(ROOT/relative,'003b6641f595923721987db57b1bbf718e9f6505cebc189fb6eb32b5ac3887d8')
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    prefix=[str(CLANG),'-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector',
            '-fno-pic','-fno-pie','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-mmx',
            '-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-femulated-tls',
            '-fstack-usage','-nostdinc','-nostdinc++','-DGTOS_NATIVE_SDK=1',
            '-isystem','../source/tools/gtos_native_sdk/sdk','-isystem','../source/third_party/libc++/src/include',
            '-isystem','../source/tools/gtos_native_sdk/c-sdk','-isystem',
            '../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include','-fno-exceptions','-fno-rtti']
    require(rows['command_template']==' '.join(prefix)+' ${defines} ${include_dirs} ${cflags} ${cflags_cc} -MD -MF ${out}.d -c ${in} -o ${out}',
            'Actual CXX prefix differs from static review')
    action=rows['buildflags'];require(action['flag_count']==61 and rows['buildflags_response_unchanged'],'Allocator flags changed')
    writer=RUN/'source'/action['source_relative'];bind(writer,'7c21cf63a64a89c514cb8f32055feb9733fc3b569dce1f352c7b65814c251118')
    response=OUT/'buildflags.rsp';response.write_text(action['response_content']+'\n')
    require(sha(response)=='a705acdec58de4a0b15f69b0544d3bbbc2d7a861fd42ba221603f8fd150aa83f','Actual definitions changed')
    header=BUILD/'gen'/action['output_relative'];header.parent.mkdir(parents=True)
    command=['/usr/bin/python3','-I','-B','-S',str(writer),'--output',action['output_relative'],
             '--rulename',action['rulename'],'--gen-dir','gen','--definitions',str(response)]
    with (OUT/'buildflags.log').open('xb') as stream:
        generated=subprocess.run(command,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
            stdout=stream,stderr=subprocess.STDOUT,timeout=30)
    require(generated.returncode==0 and sha(header)=='4d651c767438276d21f5b63e2a227617437a7d5dfd3a4524f5c58e52095d1a6f','Generated header changed')
    state['buildflags_action']={'argv':command,'returncode':0,'header_sha256':sha(header),'flags_unchanged':True}
    cache_status={
        'allocator-build-01':'c46a51e947d4987747a0ff2758943c3d9adfee205712eb2e0e7a407cb636da07',
        'compile-03':'01cc92dd20cb68acf679fbad865bff721346a4a74c2b3fb5a0747954aefae6aa',
        'compile-02':'37065edc0a1557afe12cb63ae622a3ef2f4925275dd5267c04f3fa04344d8419'}
    old_states={};dependency_memo=set();objects={};cache_results={}
    for selected in batch['rows']:
        obj=BUILD/selected['original_output'];obj.parent.mkdir(parents=True,exist_ok=True)
        objects[selected['name']]=obj
        if not selected['reuse']:continue
        cache_name,item_name=selected['reuse'].split('/')
        require(cache_name in cache_status,'Unreviewed cache')
        status_path=P/cache_name/'status.json';bind(status_path,cache_status[cache_name])
        old=old_states.setdefault(cache_name,json.loads(status_path.read_text()))
        require(old['selected_inputs_unchanged'] and old['selected_aliases_unchanged'],'Old cache input verification failed')
        if cache_name=='allocator-build-01':
            require(old.get('failure')=='Positive object has unbound headers','Old batch failure changed')
        else:require(old['diagnostic_complete'] and 'failure' not in old,'Old successful qualification changed')
        item=old['results'][item_name]
        require(item['returncode']==0 and item['header_closure_qualified'],'Cached object lacks individual closure')
        cached=P/cache_name/(item_name+'.o');bind(cached,item['object_sha256'])
        dep=P/cache_name/(item_name+'.o.d');bind(dep,sha(dep))
        tokens=dep.read_text().split(':',1)[1].replace('\\\n',' ').split()
        old_build=P/cache_name/'build';actual=set()
        for token in tokens:
            key=(cache_name,token)
            old_actual=(old_build/token).resolve(strict=True);actual.add(str(old_actual))
            if key in dependency_memo:continue
            digest=old['inputs'].get(str(old_actual))
            if digest is None:
                require(old_actual==old_build/'gen'/batch['buildflags']['output_relative'],'Unbound cached dependency')
                digest='4d651c767438276d21f5b63e2a227617437a7d5dfd3a4524f5c58e52095d1a6f'
            bind(old_actual,digest);bind(BUILD/token,digest)
            dependency_memo.add(key)
        require(len(actual)==item['dependencies'],'Cached dependency inventory changed')
        shutil.copyfile(cached,obj);bind(obj,item['object_sha256'])
        cache_results[selected['name']]={'sha256':sha(obj),'undefined_symbols':object_symbols(obj),
            'header_closure_qualified':True,'every_dependency_old_and_current_equal':True,'source':selected['reuse']}
    require(len(cache_results)==8,'Unexpected cache reuse count')
    state['cache_revalidation']=cache_results
    target_lines=(RUN/'build'/batch['target']).read_text().splitlines()
    calls=[]
    for selected in batch['rows']:
        require(target_lines.count(selected['build_row'])==1,'Actual selected build row changed')
        if selected['reuse']:
            continue
        source=selected['source']
        require((BUILD/source).resolve(strict=True)==(RUN/'build'/source).resolve(strict=True),
                'Actual selected CXX source changed with the header view')
        calls.append((selected['name'],rows['rows']['native_log'],source,[]))
    require(len(calls)==9,'Unexpected compile count')
    for name,row,source,extra in calls:
        fields=[];target=RUN/'build'/batch['target']
        for key in ('defines','include_dirs','cflags','cflags_cc'):
            value=row['fields'][key]
            require('$' not in value and target.read_text().splitlines().count(key+' = '+value)==1,'Actual field changed')
            fields.extend(shlex.split(value))
        require(not any(value.startswith(('@','-fplugin=','-specs=')) for value in fields)
                and '-load' not in fields and '-cc1' not in fields,'External compiler extension rejected')
        old='-fcrash-diagnostics-dir=../source/out/clang-crashreports'
        require(fields.count(old)==1,'Unexpected crash output setting')
        fields[fields.index(old)]='-fcrash-diagnostics-dir='+str(OUT/'crash')
        obj=objects[name];dep=pathlib.Path(str(obj)+'.d');log=OUT/(name+'.log')
        command=prefix+fields+extra+['-MD','-MF',str(dep),'-c',source,'-o',str(obj)]
        with log.open('xb') as stream:
            result=subprocess.run(command,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
                stdout=stream,stderr=subprocess.STDOUT,timeout=120)
        item={'argv':command,'returncode':result.returncode,'log_sha256':sha(log),'object_created':obj.exists()}
        state['results'][name]=item
        print(json.dumps({'stage':'compiled','name':name,'returncode':result.returncode,'object_created':obj.exists()}),flush=True)
        if name=='mixed-linux':
            require(result.returncode!=0 and not obj.exists() and
                    'GTOS must use its own PartitionAlloc platform identity.' in log.read_text(),'Mixed identity was not rejected')
            continue
        if result.returncode==0:
            item.update(object_sha256=sha(obj),object_bytes=obj.stat().st_size,undefined_symbols=object_symbols(obj))
            dependencies=dep.read_text().split(':',1)[1].replace('\\\n',' ').split()
            actual={str((BUILD/path).resolve(strict=True)) for path in dependencies}
            item['dependencies']=len(actual)
            allowed=set(state['inputs'])|{str(header.resolve(strict=True)),str((P/'platform-abi.cc').resolve(strict=True))}
            item['unbound_headers']=sorted(actual-allowed)
            item['header_closure_qualified']=not item['unbound_headers']
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Bound alias changed')
    require(ROOT.resolve(strict=True)==(RUN/'source').resolve(strict=True) and sha(ROOT/relative)=='003b6641f595923721987db57b1bbf718e9f6505cebc189fb6eb32b5ac3887d8' and
            sha(header)=='4d651c767438276d21f5b63e2a227617437a7d5dfd3a4524f5c58e52095d1a6f','Generated view changed')
    state.update(selected_inputs_unchanged=True,selected_aliases_unchanged=True,
                 retained_mixed_identity_rejection=True,combined_gn_source_view=True)
    require(all(not item.get('unbound_headers') for item in state['results'].values()),'Positive object has unbound headers')
    require(all(item['returncode']==0 for item in state['results'].values()),'Actual allocator source compilation failed')
    state['target_objects_compile_pass']=True
    ordered=[objects[item['name']] for item in batch['rows']]
    archive=BUILD/'obj/base/allocator/partition_allocator/src/partition_alloc/liballocator_base.a'
    expected='build '+str(archive.relative_to(BUILD))+': alink '+' '.join(item['original_output'] for item in batch['rows'])+' | '
    require(batch['archive_build_row'].startswith(expected),'Actual archive input order changed')
    rule='  command = '+str(AR)+' rcs ${out} ${in}'
    require((RUN/'build/toolchain.ninja').read_text().splitlines().count(rule)==1,'Actual archive rule changed')
    for path in ordered:bind(path,sha(path))
    argv=[str(AR),'rcs',str(archive)]+[str(path) for path in ordered]
    with (OUT/'archive.log').open('xb') as stream:
        result=subprocess.run(argv,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
                              stdout=stream,stderr=subprocess.STDOUT,timeout=120)
    require(result.returncode==0 and archive.is_file(),'Actual archive action failed')
    data=archive.read_bytes();require(data[:8]==b'!<arch>\n','Expected regular GNU archive')
    cursor=8;long_names=b'';members=[]
    while cursor<len(data):
        raw=data[cursor:cursor+60];require(len(raw)==60 and raw[58:]==b'`\n','Invalid archive header')
        size=int(raw[48:58]);name=raw[:16].decode('ascii').strip();cursor+=60
        payload=data[cursor:cursor+size];require(len(payload)==size,'Truncated archive member')
        cursor+=size+(size&1)
        if name=='//':long_names=payload;continue
        if name in ('/','/SYM64/'):continue
        if name.startswith('/') and name[1:].isdigit():
            begin=int(name[1:]);end=long_names.find(b'/\n',begin)
            require(end>=begin,'Invalid archive long name');name=long_names[begin:end].decode('ascii')
        else:
            require(name.endswith('/'),'Unreviewed archive format');name=name[:-1]
        index=len(members);require(index<len(ordered) and name==ordered[index].name,'Archive member order/name differs')
        require(hashlib.sha256(payload).hexdigest()==sha(ordered[index]),'Archive member differs from qualified object')
        members.append({'name':name,'bytes':size,'sha256':hashlib.sha256(payload).hexdigest()})
    require(len(members)==17 and cursor==len(data),'Incomplete archive members')
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Input changed during archive')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Alias changed during archive')
    state['archive']={'argv':argv,'returncode':0,'sha256':sha(archive),'bytes':archive.stat().st_size,'members':members}
    state.update(diagnostic_complete=True,actual_rows_replayed=9,target_source_rows=17,reused_qualified_objects=8,
                 target_objects_compile_pass=True,archive_pass=True,
                 generated_header_unchanged=True,
                 positive_objects=[name for name,item in state['results'].items() if item['returncode']==0],
                 limits='All seventeen actual allocator_base members compiled or independently cache-revalidated, then archived in the actual GN order with byte-equal member payloads. Provider linking, guest runtime, complete PartitionAlloc/Dawn and browser remain pending.')
    emit();print(json.dumps({key:value for key,value in state.items() if key not in ('inputs','aliases','results')},indent=2),flush=True)
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc();sys.exit(1)
