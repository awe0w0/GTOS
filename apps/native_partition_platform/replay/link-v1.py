#!/usr/bin/env python3
"""Link the real V8 consumer with the qualified GTOS platform checks/archive."""
import ctypes
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-partition-consumer-20261016-a'
R=W/'artifacts/native-partition-platform-20261016-a'
OUT=P/'link-01'
OLD=W/'artifacts/native-v8-workloads-20261015-a/full-link-62'
LD=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/ld.lld'
PINS={OLD/'status.json':'014b18d577856bbd1d3fc69321d30e920fe1071ed5aa33b05cdd5ecaf88853ae',
 OLD/'reviewed-final-link-argv.json':'ad98b3933806e2f7b4b7a581a647632d762adb9eaa97ef74df4e5c212c8d4dd7',
 P/'compile-main-01/status.json':'07e4eca658b12a050cf45fc1e260db0915c37573486c1a5f7b5ef643c8a321e4',
 P/'compile-checks-01/status.json':'f95d0fec11b5687cb19df73a09a397386ab5a6b4cc73582e792cb86f5c4c3946',
 R/'allocator-build-02/status.json':'6ee5d53ebf9d21c7b47de654bf7a6bfbee045726a641b5df56a7ce5878fb37c9',
 LD:'99ef321b93b2f3c8214bb592b633b3736d2df70f51e5454920131631f08e3412'}
state={'actual_full_link_pass':False,'native_isolate_pass':False,'browser_guest_pass':False,
 'diagnostic_only':True,'inputs':{},'input_symlinks':{},'production_image_changed':False}
authorized=False
def require(ok,why):
    if not ok: raise RuntimeError(why)
def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): h.update(block)
    return h.hexdigest()
def bind(path,digest):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents,'Input escaped workspace')
    require(sha(actual)==digest,'Input changed: '+str(actual))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)]==digest,'Hash conflict')
    state['inputs'][str(actual)]=digest
    if logical!=actual: state['input_symlinks'][str(logical)]=str(actual)
def emit(): (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-link-v1.py')
        and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
        and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    for path,digest in PINS.items(): bind(path,digest)
    old=json.loads((OLD/'status.json').read_text());provider=json.loads((P/'compile-main-01/status.json').read_text())
    require(old['actual_full_link_pass'] and old['bound_inputs_unchanged'] and old['old_cache_metadata_unchanged']
        and 'failure' not in old,'Real full62 proof required')
    require(provider['all_compile_pass'] and provider['bound_inputs_unchanged'] and provider['input_symlinks_unchanged']
        and provider['full_M_MD'] and set(provider['objects'])=={'main'}
        and 'failure' not in provider,'Qualified actual main object required')
    checks=json.loads((P/'compile-checks-01/status.json').read_text())
    library=json.loads((R/'allocator-build-02/status.json').read_text())
    require(checks['all_compile_pass'] and checks['bound_inputs_unchanged'] and checks['input_aliases_unchanged']
            and checks['full_MD'] and set(checks['objects'])=={'checks'} and 'failure' not in checks,
            'Qualified native platform consumer required')
    require(library['archive_pass'] and library['diagnostic_complete'] and library['selected_inputs_unchanged']
            and library['selected_aliases_unchanged'] and len(library['archive']['members'])==17
            and 'failure' not in library,'Qualified actual allocator archive required')
    helper=checks['objects']['checks'];bind(helper['path'],helper['sha256'])
    archive=R/'allocator-build-02/build/obj/base/allocator/partition_allocator/src/partition_alloc/liballocator_base.a'
    bind(archive,'d5222bb3ed9073a46e8d6f57940d9fa8dd2d3a1c0c3046df8ff7c301c4bd3c6e')
    require(library['archive']['sha256']==sha(archive),'Archive differs from qualification')
    # The pinned successful compile/full-link proofs qualify retained objects.
    # This stage reads those object bytes, not their 12073 historical source
    # inputs. Rehash every actual object/archive/script/tool before and after
    # linking, and bind the exact current main being published separately.
    bind(P/'patch/main.cc',provider['inputs'][str(P/'patch/main.cc')])
    argv=json.loads((OLD/'reviewed-final-link-argv.json').read_text())
    script=W/'artifacts/native-iostream-20261014-a/gn-06/source/tools/gtos_isolate_diagnostic/native.ld'
    bind(script,old['inputs'][str(script.resolve(strict=True))])
    require(argv==old['executed_argv'] and argv[:9]==[str(LD),'-m','elf_i386','-T',str(script),
        '--gc-sections','-Map=native_embedder.map','-o','native_embedder.elf'] and len(argv[9:])==1110
        and len(set(argv[9:]))==1110,'Reviewed fixed link shape changed')
    require(set(argv[9:])==set(old['object_inputs_sha256']),'Original object closure differs')
    retained={};ordered=[];changes=[]
    suffixes={'main':'/consumer.main.o'}
    for path in argv[9:]:
        require(path.startswith(str(W)+'/') and pathlib.Path(path).suffix in ('.o','.a'),'Only absolute qualified objects/archives allowed')
        bind(path,old['object_inputs_sha256'][path]);info=pathlib.Path(path).stat();retained[path]=(info.st_size,info.st_mtime_ns)
        names=[name for name,suffix in suffixes.items() if path.endswith(suffix)]
        require(len(names)<=1,'Ambiguous replacement')
        if names:
            name=names[0];item=provider['objects'][name];bind(item['path'],item['sha256']);ordered.append(item['path'])
            changes.append({'name':name,'old':path,'path':item['path'],'sha256':item['sha256']})
        else: ordered.append(path)
    require(len(changes)==1 and changes[0]['name']=='main'
        and len(set(ordered))==1110,'Expected exactly1 object substitution')
    require([path for path in ordered if path not in {item['path'] for item in changes}]
        ==[path for path in argv[9:] if path not in {item['old'] for item in changes}],'Retained object order changed')
    integer=W/'sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a'
    require(ordered.count(str(integer))==1 and old['object_inputs_sha256'][str(integer)]
            =='f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528','Integer runtime position changed')
    index=ordered.index(str(integer))
    ordered[index:index]=[helper['path'],str(archive)]
    require(len(ordered)==1112 and len(set(ordered))==1112,'Expected two new qualified link inputs')
    argv=argv[:9]+ordered
    for name in ('home','tmp'): (OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    (OUT/'reviewed-final-link-argv.json').write_text(json.dumps(argv,indent=2)+'\n')
    with (OUT/'link.log').open('xb') as stream:
        result=subprocess.run(argv,cwd=OUT,env=environment,shell=False,close_fds=True,
            stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    state.update(returncode=result.returncode,executed_argv=argv,substitutions=changes);emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Input changed at final validation')
    require(all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['input_symlinks'].items()),'Input alias changed')
    require(all((pathlib.Path(path).stat().st_size,pathlib.Path(path).stat().st_mtime_ns)==info for path,info in retained.items()),'Old cache metadata changed')
    state.update(bound_inputs_unchanged=True,input_symlinks_unchanged=True,old_cache_metadata_unchanged=True)
    require(result.returncode==0 and (OUT/'native_embedder.elf').is_file(),'Actual full link failed; preserve log')
    state.update(actual_full_link_pass=True,full_native_elf_exists=True,qualified_gn_link_profile_retained=True,
        new_consumer_and_archive_inputs_added=True,bound_inputs_unchanged=True,
        input_symlinks_unchanged=True,old_cache_metadata_unchanged=True,retained_inputs=1109,
        all_retained_archives_unchanged=True,new_platform_archive_added=True,prior_source_closure_rehashed=False,
        input_scope='pinned qualified proofs plus all actual object/archive/script/tool bytes and exact current main',
        elf_sha256=sha(OUT/'native_embedder.elf'),elf_bytes=(OUT/'native_embedder.elf').stat().st_size,
        object_inputs_sha256={path:state['inputs'][str(pathlib.Path(path).resolve(strict=True))] for path in ordered});emit()
    print(json.dumps({'actual_full_link_pass':True,'elf_sha256':state['elf_sha256'],'elf_bytes':state['elf_bytes']},indent=2),flush=True)
except BaseException as error:
    if authorized: state['failure']=str(error);state['traceback']=traceback.format_exc();emit()
    traceback.print_exc();sys.exit(1)
