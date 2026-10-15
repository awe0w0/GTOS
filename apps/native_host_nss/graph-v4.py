#!/usr/bin/env python3
"""Check actual NSS config values in both Linux secondary toolchains."""
import ctypes
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-host-nss-20261016-a'
OUT=P/'graph-04';ROOT=OUT/'source';BUILD=OUT/'build'
UPSTREAM=W/'sources/chromium-full/src';GN=W/'toolchains/gn/gn'
MANIFEST=P/'inputs-03.json'
INPUT_SHA='03e94164aeba885014d57c8f6a7d2b99bb92b279ec3eade7cff80aa1327e0f83'
state={'diagnostic_complete':False,'gn_generation_pass':False,'compiled_objects':False,
       'nss_gn_metadata_qualified':False,'guest_executed':False,'inputs':{},'aliases':{},
       'exec_script_allowed':False,'nss_configurations':{},'content_graph_qualified':False,
       'tls_runtime_qualified':False,'production_image_changed':False}

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
    require(inputs['revision']=='04bca2584cddb39e134fc7c16bb2886e02863d0e'
            and len(inputs['source_inputs'])==111 and len(inputs['binary_inputs'])==1
            and len(inputs['metadata_inputs'])==24 and not inputs['gtos_main_use_sysroot']
            and inputs['linux_secondary_use_sysroot'] and not inputs['exec_script_allowed'],'Input scope changed')
    for name,digest in inputs['proofs'].items(): bind(name,digest)
    for name,digest in inputs['binary_inputs'].items(): bind(name,digest)
    for name,digest in inputs['metadata_inputs'].items():bind(name,digest)
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
use_glib=false
symbol_level=0
action_pool_depth=1
concurrent_links=1
gtos_clang_bin="/mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin"
gtos_python="/usr/bin/python3"
'''
    (OUT/'requested-args.gn').write_text(args)
    scope=['--root='+str(ROOT),'--dotfile='+str(ROOT/'content-diagnostic.gn'),
           '--root-target=//tools/gtos_host_nss_qualification:qualification',
           '--root-pattern=//tools/gtos_host_nss_qualification:qualification']
    require((ROOT/'content-diagnostic.gn').read_text().endswith('exec_script_allowlist = []\n'),
            'All GN helpers must stay denied for generation and description')
    argv=[str(GN),'gen',str(BUILD)]+scope+['--args='+args,'--fail-on-unused-args']
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
        for arch,sysroot_arch in (('x86','i386'),('x64','amd64')):
            label='//tools/gtos_host_nss_qualification:nss(//build/toolchain/linux:clang_'+arch+')'
            description=OUT/('nss-'+arch+'.json')
            error=OUT/('nss-'+arch+'.stderr.log')
            command=[str(GN),'desc',str(BUILD),label,'--format=json']+scope
            with description.open('xb') as output,error.open('xb') as errors:
                query=subprocess.run(command,cwd=ROOT,env=environment,shell=False,close_fds=True,
                    stdin=subprocess.DEVNULL,stdout=output,stderr=errors,timeout=30)
            require(query.returncode==0 and error.stat().st_size==0,'Actual GN description failed: '+arch)
            data=json.loads(description.read_text())
            require(isinstance(data,dict) and len(data)==1 and label in data,'Unexpected GN JSON target')
            values=data[label]
            prefix='-isystem../source/build/linux/debian_bullseye_'+sysroot_arch+'-sysroot/usr/include/'
            expected=[prefix+'nspr',prefix+'nss']
            require(values['type']=='source_set' and values['cflags_c']==expected
                    and values['cflags_cc']==expected
                    and values['libs']==['nspr4','nss3','nssutil3','plc4','plds4','smime3']
                    and values.get('lib_dirs',[])==[],'Actual NSS config mismatch: '+arch)
            require(any(value.startswith('//build/config/linux/nss:nss') for value in values['configs']),
                    'Actual upstream NSS config missing: '+arch)
            state['nss_configurations'][arch]={'label':label,'argv':command,'returncode':query.returncode,
                'description_sha256':sha(description),'stderr_sha256':sha(error),
                'cflags_c':values['cflags_c'],'cflags_cc':values['cflags_cc'],'libs':values['libs'],
                'lib_dirs':values.get('lib_dirs',[]),'configs':values['configs']}
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Bound alias changed')
    state.update(diagnostic_complete=True,gn_generation_pass=result.returncode==0,bound_inputs_unchanged=True,
                 input_aliases_unchanged=True,nss_gn_metadata_qualified=result.returncode==0 and len(state['nss_configurations'])==2,
                 gtos_freestanding_assert_retained=True,gtos_main_use_sysroot=False,
                 certificate_configuration_changed=False,pkg_config_executed_by_gn=False,
                 scoped_gn_commands_only=True,gn_scope_switches=scope)
    emit();print(json.dumps({key:value for key,value in state.items() if key not in ('inputs','aliases','argv','gn_dependencies')},indent=2),flush=True)
except BaseException as error:
    state['failure']=type(error).__name__+': '+str(error);emit();raise
