#!/usr/bin/env python3
"""Replay statically reviewed GN Rust/C link arguments without Ninja or shell."""
import ctypes
import hashlib
import json
import os
import pathlib
import re
import shlex
import struct
import subprocess
import sys

W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-rust-bin-20261015-a'
OUT=P/'qualify-03';BUILD=OUT/'build';ROOT=OUT/'source';GRAPH=P/'graph-04'
PYTHON=pathlib.Path('/usr/bin/python3')
CLANG=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
LLD=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/ld.lld'
LABEL='tools/gtos_rust_bin_qualification';OBJ='obj/'+LABEL
COMMON=['--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-mmx','-mno-sse','-mno-sse2']
FLAGS='--edition=2024 -Zunstable-options --target=/mnt/f/GTOS-Chromium/artifacts/native-rust-gtos-20261015-a/target/i686-unknown-gtos.json -Cpanic=abort -Copt-level=2 -Cdefault-linker-libraries=no --extern=core=/mnt/f/GTOS-Chromium/artifacts/native-rust-gn-core-20261015-a/build-03/build/local_rustc_sysroot/lib/rustlib/i686-unknown-gtos/lib/libcore_core.rlib --extern=compiler_builtins=/mnt/f/GTOS-Chromium/artifacts/native-rust-bin-20261015-a/builtins-build-01/libcompiler_builtins.rlib'
SOURCE='../source/'+LABEL+'/entry.rs'
SOURCES=SOURCE+' ../../../native-rust-gtos-20261015-a/target/i686-unknown-gtos.json ../../../native-rust-gn-core-20261015-a/build-03/build/local_rustc_sysroot/lib/rustlib/i686-unknown-gtos/lib/libcore_core.rlib ../../builtins-build-01/libcompiler_builtins.rlib'
RUSTDEPS='-Clink-arg=-Bdynamic -Clink-arg='+OBJ+'/native_values.native-values.o'
ENV={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
state={'rust_bin_qualified':False,'full_std_qualified':False,'guest_executed':False,'commands':[],
       'inputs':{},'aliases':{},'production_image_changed':False}

def require(ok,why):
    if not ok: raise RuntimeError(why)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()

def bind(path,digest):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents and sha(actual)==digest,'Bound input changed: '+str(path))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)]==digest,'Conflicting input')
    state['inputs'][str(actual)]=digest
    if logical!=actual:state['aliases'][str(logical)]=str(actual)

def emit(): (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def variables(path):
    result={}
    for line in path.read_text().splitlines():
        match=re.fullmatch(r'\s*([a-z_]+) =(?: (.*))?',line)
        if match:
            key,value=match.group(1),match.group(2) or ''
            require(key not in result,'Repeated Ninja variable');result[key]=value
    return result

def run(name,argv,success=True):
    require(pathlib.Path(argv[0]) in (PYTHON,CLANG),'Unexpected direct tool')
    with (OUT/(name+'.stdout')).open('xb') as stdout,(OUT/(name+'.stderr')).open('xb') as stderr:
        child=subprocess.run([str(a) for a in argv],cwd=BUILD,env=ENV,shell=False,close_fds=True,
                             stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr,timeout=180)
    state['commands'].append({'name':name,'argv':[str(a) for a in argv],'returncode':child.returncode,'shell':False,
        'stdout_sha256':sha(OUT/(name+'.stdout')),'stderr_sha256':sha(OUT/(name+'.stderr'))})
    emit();require((child.returncode==0)==success,'Unexpected result for '+name)

def inspect(path):
    data=path.read_bytes()
    def section(offset,size):
        require(0<=offset<=len(data) and 0<=size<=len(data)-offset,'ELF range outside file')
        return data[offset:offset+size]
    def unpack(fmt,offset):return struct.unpack(fmt,section(offset,struct.calcsize(fmt)))
    def string(table,offset):
        require(0<=offset<len(table),'ELF string offset');end=table.find(b'\0',offset)
        require(end>=offset,'Unterminated ELF string');return table[offset:end].decode('ascii')
    require(data[:7]==b'\x7fELF\x01\x01\x01','ELF32 little endian required')
    kind,machine,version,entry,phoff,shoff,flags,ehsize,phsize,phnum,shsize,shnum,shstr=unpack('<HHIIIIIHHHHHH',16)
    require((kind,machine,version,ehsize,phsize,shsize)==(2,3,1,52,32,40) and 0<phnum<32 and 0<shnum<256 and shstr<shnum,'ELF32 static header mismatch')
    programs=[unpack('<IIIIIIII',phoff+i*32) for i in range(phnum)]
    require(not any(p[0] in (2,3,7) for p in programs),'Dynamic/interpreter/TLS segment forbidden')
    for p in programs:
        section(p[1],p[4]);require(p[4]<=p[5] and p[2]+p[5]<=0x100000000,'ELF segment overflow')
        require(not (p[6]&1 and p[6]&2),'Writable executable segment')
    require(any(p[0]==0x6474e551 and not p[6]&1 for p in programs),'Nonexecuting stack required')
    require(any(p[0]==1 and p[6]&1 and p[2]<=entry<p[2]+p[4] for p in programs),'Entry outside file-backed code')
    sections=[unpack('<IIIIIIIIII',shoff+i*40) for i in range(shnum)]
    require(sections[shstr][1]==3,'Section string table required')
    for s in sections:
        if s[1]!=8:section(s[4],s[5])
        require(not (s[2]&1 and s[2]&4) and not s[2]&0x400,'Writable code or target TLS section')
        require(s[1] not in (4,9),'Residual relocation section')
    symbols={};imports=[]
    for s in sections:
        if s[1]!=2:continue
        require(s[9]==16 and s[5]%16==0 and s[6]<shnum and sections[s[6]][1]==3,'Symbol table bounds')
        names=sections[s[6]];table=section(names[4],names[5])
        for offset in range(s[4],s[4]+s[5],16):
            name,value,size,info,other,index=unpack('<IIIBBH',offset);name=string(table,name)
            require(index<shnum or index in (0xfff1,0xfff2),'Symbol section index')
            if name:
                if index==0 and info>>4 in (1,2):imports.append(name)
                elif index!=0 and info&15 in (1,2):
                    require(name not in symbols,'Ambiguous linked symbol')
                    symbols[name]={'value':value,'size':size,'type':info&15,'binding':info>>4,'visibility':other&3}
    require(not imports,'Unresolved linked imports')
    require(all(name in symbols for name in ('GtosRustEntry','GtosNativeAdd','gtos_native_value','gtos_native_divisor','__udivdi3')),'Missing Rust/C/intrinsic symbols')
    require(symbols['GtosRustEntry']['value']==entry and symbols['GtosRustEntry']['type']==2,'Wrong Rust entry')
    require(all(symbols[name]['binding']==1 and symbols[name]['type']==2 for name in ('GtosRustEntry','GtosNativeAdd')),'Invalid exported C ABI function')
    require(symbols['__udivdi3']['binding']==0 and symbols['__udivdi3']['visibility']==2 and symbols['__udivdi3']['type']==2,'Intrinsic must be a defined local hidden function')
    start=symbols['GtosRustEntry']['value'];size=symbols['GtosRustEntry']['size']
    load=next(p for p in programs if p[0]==1 and p[2]<=start and start+size<=p[2]+p[4])
    code=section(load[1]+start-load[2],size)
    branches=[start+i+5+struct.unpack_from('<i',code,i+1)[0] for i in range(max(0,len(code)-4)) if code[i] in (0xe8,0xe9)]
    require(symbols['GtosNativeAdd']['value'] in branches,'Rust to native C branch not resolved')
    require(symbols['__udivdi3']['value'] in branches,'Rust builtin division branch not resolved')
    require(struct.pack('<I',symbols['gtos_native_value']['value']) in code,'Rust native data address not resolved')
    return {'sha256':sha(path),'bytes':len(data),'entry':entry,'symbols':symbols,'imports':imports,
            'rust_to_c_branch_resolved':True,'rust_builtin_division_resolved':True,'native_data_address_resolved':True,'no_host_libraries':True,
            'no_wx':True,'nonexecuting_stack':True,'residual_relocations':False}

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed Python-I-B-S required')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['landlock_applied'] and guard['network_disabled']
            and guard['source_write_denied'] and guard['writable_output']==str(OUT)
            and guard['artifact_execution_denied'] and not guard['directory_execute_permissions'],'Guard mismatch')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs missing')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==info['st_ino'] for name,info in guard['namespace_metadata'].items()),'Namespace changed')
    proof=GRAPH/'status.json';bind(proof,'17253cb7452147c792f735fa45f08e18a183253b14ef33ea0338e7b1d5548f8d')
    graph=json.loads(proof.read_text())
    require(graph['gn_generation_pass'] and graph['gn_dependency_closure_qualified'] and graph['bound_inputs_unchanged'] and 'failure' not in graph,'Complete GN fixture qualification required')
    for name,digest in graph['inputs'].items():bind(name,digest)
    for name in ('build','home','tmp'):(OUT/name).mkdir()
    ROOT.symlink_to(GRAPH/'source',target_is_directory=True)
    (BUILD/OBJ).mkdir(parents=True)
    rules={};current=None;toolchain=GRAPH/'build/toolchain.ninja';bind(toolchain,sha(toolchain))
    for line in toolchain.read_text().splitlines():
        if line.startswith('rule '):current=line[5:];rules[current]={}
        elif line.startswith('  ') and ' = ' in line and current:
            key,value=line.strip().split(' = ',1);rules[current][key]=value
        elif not line.startswith('  '):current=None
    expected='"/usr/bin/python3" -I -B -S "../source/build/rust/gni_impl/rustc_wrapper.py" --dump-rustc-env-and-flags=${target_out_dir}/${label_name}.rustflags.json --rsp=${target_out_dir}/${label_name}.rsp --rustc=../source/third_party/rust-toolchain/bin/rustc --depfile=${output_dir}/${target_output_name}${output_extension}.d --emit=dep-info=${output_dir}/${target_output_name}${output_extension}.d,link -o ${output_dir}/${target_output_name}${output_extension} -- -Clinker="'+str(LLD)+'" ${in} LDFLAGS ${ldflags} -m elf_i386 --no-undefined -z noexecstack --build-id=none RUSTENV ${rustenv}'
    require(rules['rust_bin']['command']==expected and rules['rust_bin']['deps']=='gcc'
            and rules['rust_bin']['rspfile_content']=='--crate-name ${crate_name} --crate-type ${crate_type} ${rustflags} ${rustdeps} ${externs} SOURCES ${sources}', 'GN Rust rule differs from static review')
    prior=W/'artifacts/native-shared-link-20261015-a/qualify-02/build/toolchain.ninja'
    bind(prior,'9deba3f8cb209babd45b94521a36920c87cde26f0d0edd4020d1450f34e9a936')
    previous_rules={};current=None
    for line in prior.read_text().splitlines():
        if line.startswith('rule '):current=line[5:];previous_rules[current]={}
        elif line.startswith('  ') and ' = ' in line and current:
            key,value=line.strip().split(' = ',1);previous_rules[current][key]=value
        elif not line.startswith('  '):current=None
    for name in ('cc','cxx','asm','alink','link','solink','solink_module','rust_rlib'):
        require(name in rules and rules[name]==previous_rules[name],'Qualified prior tool changed: '+name)
    state['prior_native_and_rlib_rules_unchanged']=True
    native=GRAPH/'build'/OBJ/'native_values.ninja';rust=GRAPH/'build'/OBJ/'rust_entry.ninja'
    bind(native,sha(native));bind(rust,sha(rust));nv=variables(native);rv=variables(rust)
    require(all(nv.get(key)=='' for key in ('defines','include_dirs','cflags','cflags_c')),'Native compile flags changed')
    require(rv['crate_name']=='gtos_rust_entry' and rv['crate_type']=='bin' and rv['rustflags']==FLAGS
            and rv['rustdeps']==RUSTDEPS and rv['externs']=='' and rv['rustenv']=='' and rv['sources']==SOURCES
            and rv['ldflags']=='--entry=GtosRustEntry' and rv['output_extension']=='.elf' and rv['output_dir']=='.','GN Rust fixture arguments changed')
    state['generated_rule_matches_static_review']=True;emit()
    obj=OBJ+'/native_values.native-values.o'
    run('native-values',[CLANG]+COMMON+['-MD','-MF',obj+'.d','-c','../source/'+LABEL+'/native-values.c','-o',obj])
    dep=(BUILD/(obj+'.d')).read_text().replace('\\\n',' ')
    names=shlex.split(dep.split(':',1)[1]);require(names==['../source/'+LABEL+'/native-values.c'],'Unexpected native compile inputs')
    bind(BUILD/names[0],sha(BUILD/names[0]))
    wrapper='../source/build/rust/gni_impl/rustc_wrapper.py'
    for name,output,deps,success in (('rust-entry','./rust_entry.elf',RUSTDEPS,True),
                                    ('missing-provider','./missing_provider.elf','-Clink-arg=-Bdynamic',False)):
        rsp=OBJ+'/'+name+'.rsp';dump=OBJ+'/'+name+'.rustflags.json'
        (BUILD/rsp).write_text('--crate-name gtos_rust_entry --crate-type bin '+FLAGS+' '+deps+'  SOURCES '+SOURCES)
        run(name,[PYTHON,'-I','-B','-S',wrapper,'--dump-rustc-env-and-flags='+dump,'--rsp='+rsp,
            '--rustc=../source/third_party/rust-toolchain/bin/rustc','--depfile='+output+'.d',
            '--emit=dep-info='+output+'.d,link','-o',output,'--','-Clinker='+str(LLD),SOURCE,
            'LDFLAGS','--entry=GtosRustEntry','-m','elf_i386','--no-undefined','-z','noexecstack','--build-id=none','RUSTENV'],success)
    require(not (BUILD/'missing_provider.elf').exists(),'Unresolved link emitted target')
    errors=(OUT/'missing-provider.stderr').read_text()
    require('GtosNativeAdd' in errors and 'gtos_native_value' in errors and 'undefined symbol' in errors,'Wrong unresolved-provider failure')
    state['elf']=inspect(BUILD/'rust_entry.elf')
    require(state['elf']['sha256']=='85518a55a7c514677c929748463496ebb671cd1aa0fea0c9a49e88bf88bec5ca','Executable differs from revalidated earlier compile')
    dependencies=[]
    for line in (BUILD/'rust_entry.elf.d').read_text().splitlines():
        if line and not line.startswith('#') and ':' in line:
            for name in line.split(':',1)[1].split():
                path=(BUILD/name).resolve(strict=True);require(str(path) in state['inputs'],'Unbound Rust source dependency')
                dependencies.append(str(path))
    require(dependencies,'Missing Rust dep-info')
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Final input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Final alias changed')
    require(all(sha(path)==digest for path,digest in guard['executable_files'].items()),'Final tool changed')
    state.update(rust_bin_qualified=True,undefined_provider_rejected=True,rust_dependencies=sorted(set(dependencies)),replayed_elf_bytes_identical=True,
                 bound_inputs_unchanged=True,input_aliases_unchanged=True,guard_sha256=sha(OUT/'guard.json'))
    emit();print(json.dumps({'rust_bin_qualified':True,'elf_bytes':state['elf']['bytes'],'undefined_provider_rejected':True,'guest_executed':False}),flush=True)
except BaseException as error:
    state['failure']=type(error).__name__+': '+str(error);emit();raise
