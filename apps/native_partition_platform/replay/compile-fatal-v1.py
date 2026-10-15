#!/usr/bin/env python3
"""Compile the real platform consumer using the qualified allocator GN profile."""
import ctypes
import hashlib
import json
import os
import pathlib
import shlex
import struct
import subprocess
import sys
import traceback

W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-partition-consumer-20261016-a'
R=W/'artifacts/native-partition-platform-20261016-a'
GN=W/'artifacts/native-rust-gtos-20261015-a/graph-30'
OUT=P/'compile-fatal-01';BUILD=OUT/'build'
CLANG=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
SOURCE=P/'patch/fatal-checks.cc'
state={'all_compile_pass':False,'guest_qualified':False,'inputs':{},'aliases':{},'objects':{}}
authorized=False

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    result=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):result.update(block)
    return result.hexdigest()

def bind(path,digest):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents,'Escaped input')
    if str(actual) in state['inputs']:require(state['inputs'][str(actual)]==digest,'Conflicting input')
    else:require(sha(actual)==digest,'Changed selected input: '+str(path))
    state['inputs'][str(actual)]=digest
    if logical!=actual:state['aliases'][str(logical)]=str(actual)

def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry required')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-compile-fatal-v1.py')
            and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
            and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required')
    authorized=True
    bind(CLANG,'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7')
    bind(SOURCE,'3a41f8fc389e2bbcf28d78c523d586c73e87e68ba2dc074dcd5eb1144c58473a')
    prior_path=R/'allocator-build-02/status.json'
    bind(prior_path,'6ee5d53ebf9d21c7b47de654bf7a6bfbee045726a641b5df56a7ce5878fb37c9')
    prior=json.loads(prior_path.read_text())
    require(prior['diagnostic_complete'] and prior['archive_pass'] and prior['selected_inputs_unchanged']
            and prior['selected_aliases_unchanged'] and 'failure' not in prior,'Qualified real library required')
    for path,digest in prior['inputs'].items():bind(path,digest)
    profile_path=R/'allocator-base-inputs-02.json'
    bind(profile_path,'aa39ff932b92f16c134dd12dc8e4e72c378e14216b779f94b3c6930d91746189')
    profile=json.loads(profile_path.read_text())
    target=GN/'build'/profile['target'];bind(target,profile['target_sha256'])
    bind(GN/'build/toolchain.ninja',profile['toolchain_sha256'])
    (OUT/'source').symlink_to(GN/'source',target_is_directory=True);BUILD.mkdir()
    (BUILD/'gen').symlink_to(R/'allocator-build-02/build/gen',target_is_directory=True)
    generated=BUILD/'gen'/profile['buildflags']['output_relative']
    bind(generated,'4d651c767438276d21f5b63e2a227617437a7d5dfd3a4524f5c58e52095d1a6f')
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    prefix=[str(CLANG),'-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector',
            '-fno-pic','-fno-pie','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-mmx',
            '-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-femulated-tls',
            '-fstack-usage','-nostdinc','-nostdinc++','-DGTOS_NATIVE_SDK=1',
            '-isystem','../source/tools/gtos_native_sdk/sdk','-isystem','../source/third_party/libc++/src/include',
            '-isystem','../source/tools/gtos_native_sdk/c-sdk','-isystem',
            '../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include','-fno-exceptions','-fno-rtti']
    require(profile['command_template']==' '.join(prefix)+' ${defines} ${include_dirs} ${cflags} ${cflags_cc} -MD -MF ${out}.d -c ${in} -o ${out}',
            'Static prefix differs from actual native GN rule')
    fields=[]
    for key in ('defines','include_dirs','cflags','cflags_cc'):
        value=profile['fields'][key]
        require('$' not in value and target.read_text().splitlines().count(key+' = '+value)==1,'Actual compiler variable changed')
        fields.extend(shlex.split(value))
    old='-fcrash-diagnostics-dir=../source/out/clang-crashreports'
    require(fields.count(old)==1 and '-load' not in fields and not any(value.startswith(('@','-fplugin=')) for value in fields),'Unreviewed compiler extension')
    fields[fields.index(old)]='-fcrash-diagnostics-dir='+str(OUT/'crash')
    obj=OUT/'partition-checks.o';dep=OUT/'partition-checks.o.d'
    argv=prefix+fields+['-MD','-MF',str(dep),'-c',str(SOURCE),'-o',str(obj)]
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    with (OUT/'compile.log').open('xb') as stream:
        result=subprocess.run(argv,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
            stdout=stream,stderr=subprocess.STDOUT,timeout=180)
    state.update(argv=argv,returncode=result.returncode,cwd=str(BUILD),actual_gn_profile=True)
    require(result.returncode==0 and obj.is_file(),'Actual platform consumer compile failed')
    data=obj.read_bytes()
    require(data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(1,3),'Expected ELF32/i386 object')
    offset=struct.unpack_from('<I',data,32)[0];size,count=struct.unpack_from('<HH',data,46)
    require(size==40 and offset+count*size<=len(data),'Invalid ELF sections')
    require(not any(struct.unpack_from('<I',data,offset+index*size+8)[0]&0x400 for index in range(count)),
            'Hardware TLS section in native consumer')
    tokens=dep.read_text().split(':',1)[1].replace('\\\n',' ').split()
    actual={str((BUILD/path).resolve(strict=True)) for path in tokens}
    state['unbound_headers']=sorted(actual-set(state['inputs']))
    require(not state['unbound_headers'],'Consumer headers outside pre-bound native closure')
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Bound alias changed')
    state.update(all_compile_pass=True,bound_inputs_unchanged=True,input_aliases_unchanged=True,
                 full_MD=True,dependencies=len(actual),objects={'checks':{'path':str(obj),'sha256':sha(obj),'bytes':len(data)}},
                 limits='Actual PA GN profile and complete selected MD closure; standalone callable consumer object only, no host execution/link/guest claim.')
    emit();print(json.dumps({key:value for key,value in state.items() if key not in ('inputs','aliases','argv')},indent=2),flush=True)
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc();sys.exit(1)
