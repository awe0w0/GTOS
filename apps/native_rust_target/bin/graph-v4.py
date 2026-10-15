#!/usr/bin/env python3
"""Generate real GN rules for the reviewed native Rust/C binary fixture."""
import ctypes
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-rust-bin-20261015-a'
OUT=P/'graph-04';ROOT=OUT/'source';BUILD=OUT/'build'
UPSTREAM=W/'sources/chromium-full/src';GN=W/'toolchains/gn/gn'
MANIFEST=P/'inputs-03.json'
INPUT_SHA='2f5a17eaeefb28f71b79dc512dbbbb44dbbfdeaaf689c1315fd5909bbdc677aa'
state={'diagnostic_complete':False,'gn_generation_pass':False,'compiled_objects':False,
       'rust_bin_qualified':False,'guest_executed':False,'inputs':{},'aliases':{}}

def require(ok,why):
    if not ok: raise RuntimeError(why)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()

def bind(path,digest):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents and sha(actual)==digest,'Changed/scoped input: '+str(path))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)]==digest,'Conflicting input')
    state['inputs'][str(actual)]=digest
    if logical!=actual: state['aliases'][str(logical)]=str(actual)

def emit(): (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')

def target(relative):
    parts=pathlib.PurePosixPath(relative).parts
    require(parts and not pathlib.PurePosixPath(relative).is_absolute() and '..' not in parts,'Escaped overlay')
    current=ROOT
    for part in parts[:-1]:
        current=current/part
        if current.is_symlink():
            original=current.resolve(strict=True)
            require(UPSTREAM in original.parents and original.is_dir(),'Escaped source view')
            current.unlink();current.mkdir()
            for child in original.iterdir():
                if child.name not in ('.git','out','__pycache__'):
                    (current/child.name).symlink_to(child,target_is_directory=child.is_dir())
        else: current.mkdir(exist_ok=True)
        require(ROOT in current.resolve(strict=True).parents,'Escaped output')
    result=ROOT/relative
    if result.is_symlink(): result.unlink()
    require(not result.exists(),'Owned target collision')
    return result

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Python-I-B-S required')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['network_disabled'] and guard['landlock_applied']
            and guard['source_write_denied'] and not guard['directory_execute_permissions']
            and guard['writable_output']==str(OUT),'Guard mismatch')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs missing')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==info['st_ino'] for name,info in guard['namespace_metadata'].items()),'Namespace changed')
    bind(MANIFEST,INPUT_SHA);inputs=json.loads(MANIFEST.read_text())
    require(len(inputs['source_inputs'])==102 and len(inputs['binary_inputs'])==4,'Input scope changed')
    for name,digest in inputs['proofs'].items(): bind(name,digest)
    for name,digest in inputs['binary_inputs'].items(): bind(name,digest)
    for name in ('source','home','tmp'): (OUT/name).mkdir()
    for child in UPSTREAM.iterdir():
        if child.name not in ('.git','out','__pycache__'):
            (ROOT/child.name).symlink_to(child,target_is_directory=child.is_dir())
    for relative,item in inputs['source_inputs'].items():
        bind(item['path'],item['sha256']);dst=target(relative)
        shutil.copyfile(item['path'],dst);bind(dst,item['sha256'])
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    args='''target_os="gtos"
target_cpu="x86"
is_debug=false
is_component_build=false
is_clang=true
use_sysroot=false
symbol_level=0
action_pool_depth=1
concurrent_links=1
gtos_clang_bin="/mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin"
gtos_python="/usr/bin/python3"
'''
    (OUT/'requested-args.gn').write_text(args)
    argv=[str(GN),'gen',str(BUILD),'--root='+str(ROOT),'--dotfile='+str(ROOT/'content-diagnostic.gn'),
          '--root-target=//tools/gtos_rust_bin_qualification:rust_entry',
          '--root-pattern=//tools/gtos_rust_bin_qualification:rust_entry','--args='+args,'--fail-on-unused-args']
    with (OUT/'gn.log').open('xb') as stream:
        result=subprocess.run(argv,cwd=ROOT,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,
                              stdout=stream,stderr=subprocess.STDOUT,timeout=180)
    state.update(argv=argv,gn_returncode=result.returncode,gn_log_sha256=sha(OUT/'gn.log'))
    if result.returncode==0:
        text=(BUILD/'build.ninja.d').read_text()
        require(text.startswith('build.ninja.stamp: '),'Unknown GN dependencies')
        names=sorted(set(text.split(':',1)[1].split()))
        for name in names:
            if name.startswith('../source/'):
                relative=name[len('../source/'):]
                require(relative in inputs['source_inputs'],'Unbound actual GN input: '+relative)
                bind(BUILD/name,inputs['source_inputs'][relative]['sha256'])
            else:
                require(name in ('./args.gn','gen/module_platform.txt','./mobileprovisions.filelist'),'Unknown generated GN input: '+name)
                bind(BUILD/name,sha(BUILD/name))
        state.update(gn_dependencies=names,gn_dependency_closure_qualified=True)
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Bound alias changed')
    state.update(diagnostic_complete=True,gn_generation_pass=result.returncode==0,bound_inputs_unchanged=True,input_aliases_unchanged=True)
    emit();print(json.dumps({key:value for key,value in state.items() if key not in ('inputs','aliases','argv','gn_dependencies')},indent=2),flush=True)
except BaseException as error:
    state['failure']=type(error).__name__+': '+str(error);emit();raise
