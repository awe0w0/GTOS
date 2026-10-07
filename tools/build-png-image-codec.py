#!/usr/bin/env python3
"""Build real Wuffs PNG -> existing Skia pixels; report guest admission separately."""
import argparse,base64,datetime,hashlib,json,os,pathlib,re,shutil,subprocess,urllib.request

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=pathlib.Path)
p.add_argument('--dependency-cache',required=True,type=pathlib.Path)
p.add_argument('--clang',default=os.environ.get('CLANG','clang'))
p.add_argument('--host-cc',default=os.environ.get('HOST_CC','gcc'))
p.add_argument('--host-cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--wuffs-profile',choices=('upstream','gtos-release'),default='gtos-release',
               help='Explicit concrete-API adaptation; upstream control remains selectable')
a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
app=repo/'apps/png_image_codec';skia=repo/'apps/skia_alpha_probe'
gif=repo/'apps/wuffs_gif_probe';cache=a.dependency_cache.resolve()
out=a.output.resolve();assert not out.exists(),'Choose a fresh output directory'
out.parent.mkdir(parents=True,exist_ok=True)
assert shutil.disk_usage(out.parent).free>128*1024*1024
out.mkdir();cache.mkdir(parents=True,exist_ok=True)
clang=pathlib.Path(shutil.which(a.clang) or a.clang).resolve();assert clang.is_file()
def tool(name):
    local=clang.parent/name
    value=str(local) if local.is_file() else shutil.which(name)
    assert value,name
    return value
checks=[]
state=dict(scope='Real static PNG decoder and existing Skia premultiplication component; no browser or presentation',
    source_base=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
    qualification_checkpoint='c237c70af9de32d097f5b0812c5d34a023f69d29',native_compiler_target='i686-unknown-none-elf',
    stage='building',wuffs_profile=a.wuffs_profile,native_build_pass=False,host_asan_ubsan_pass=False,guest_pass=False,
    browser_guest_pass=False,qualification_complete=False,kernel_source_changed=False,
    abi_changed=False,capacity_changed=False,linux_target_sdk_used=False,system_install=False,
    boot_demo_file_limit_bytes=65536,user_page_budget=256,user_stack_bytes=8192,checks=checks)
def run(name,args,timeout=240):
    args=[str(x) for x in args]
    with (out/(name+'.log')).open('w') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    checks.append(dict(name=name,command=args,exit_code=r.returncode))
    if r.returncode:raise RuntimeError(name+' failed; see '+str(out/(name+'.log')))
    return (out/(name+'.log')).read_text()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
try:
    manifest=json.loads((app/'dependencies.json').read_text());verified=[]
    for item in manifest['files']:
        path=cache/item['file'];path.parent.mkdir(parents=True,exist_ok=True)
        if not path.exists():
            request=urllib.request.Request(item['url'],headers={'User-Agent':'GTOS-native-PNG-component'})
            limit=(item['bytes']+2)//3*4+16 if item.get('encoding')=='base64' else item['bytes']+1
            with urllib.request.urlopen(request,timeout=45) as response:data=response.read(limit)
            if item.get('encoding')=='base64':data=base64.b64decode(data,validate=True)
            assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['file']
            with path.open('xb') as f:f.write(data)
        assert path.stat().st_size==item['bytes'] and sha(path)==item['sha256'],item['file']
        verified.append(item)
    state['dependency_manifest']=manifest;state['verified_dependencies']=verified
    release=[]
    if a.wuffs_profile=='gtos-release':
        derived=out/'derived'
        run('derive-wuffs',['python3',repo/'tools/adapt-wuffs-png.py',cache/'wuffs-v0.3.c',derived])
        adaptation=json.loads((derived/'wuffs-png-gtos.manifest.json').read_text())
        assert adaptation['upstream']['sha256']==sha(cache/'wuffs-v0.3.c')
        assert adaptation['derived']['sha256']==sha(derived/'wuffs-png-gtos.c')
        assert adaptation['diff']['sha256']==sha(derived/'wuffs-png-gtos.diff')
        assert adaptation['function_bodies_modified'] and not adaptation['generic_upcasting_supported']
        assert adaptation['source_dispatch_cases_preserved']==13 and adaptation['pixel_specialization']
        state['wuffs_adaptation']=adaptation
        release=['-DGTOS_PNG_USE_DERIVED_RELEASE=1','-I'+str(derived)]
    upstream=json.loads((skia/'source-manifest.json').read_text())
    for item in upstream['files']:assert sha(skia/item['file'])==item['sha256']
    fragment=(skia/'skia_alpha_upstream.inc').read_text();body=fragment[fragment.index('/**'):]
    assert hashlib.sha256(body.encode()).hexdigest()==upstream['fragment_sha256']
    assert body in (skia/'SkMath.original.h').read_text() and not upstream['function_bodies_modified']
    state['skia_manifest']=upstream
    source_paths=[x for x in app.iterdir() if x.is_file()]+[skia/'skia_pixel.cc',skia/'skia_pixel.h',gif/'memory.c',
                  repo/'tools/adapt-wuffs-png.py',pathlib.Path(__file__).resolve()]
    state['source_sha256']={str(x.relative_to(repo)):sha(x) for x in source_paths}
    for source in source_paths:
        snapshot=out/'source-snapshot'/source.relative_to(repo)
        snapshot.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,snapshot)
    run('fixtures-stable',['python3',app/'generate_png_fixtures.py','--check'])
    run('fixtures-independent',['python3',app/'verify_png_fixtures.py'])
    renames=['-D'+name+'=gtos_png_'+name for name in ('memcpy','memmove','memset','memcmp')]
    for opt in ('0','2'):
        common=['-O'+opt,'-g','-Wall','-Wextra','-Werror','-fno-builtin',
                '-fsanitize=address,undefined','-fno-pie','-ffunction-sections','-fdata-sections',
                '-I'+str(app),'-I'+str(skia),'-I'+str(cache),*release]
        cobjects=[]
        for name,source in [('decode',app/'png_decode.c'),('memory',gif/'memory.c')]:
            obj=out/('host-'+name+'-O'+opt+'.o');cobjects.append(obj)
            run('host-'+name+'-O'+opt,[a.host_cc,'-std=c11',*common,*renames,'-c',source,'-o',obj])
        binary=out/('host-O'+opt)
        run('host-link-O'+opt,[a.host_cxx,'-std=c++11',*common,'-no-pie',app/'png_pixel.cc',skia/'skia_pixel.cc',
                               app/'host_tests.cc',app/'host_assert.cc',*cobjects,'-Wl,--gc-sections','-o',binary])
        result=run('host-run-O'+opt,[binary])
        assert 'GTOS PNG HOST PASS' in result
        state['host_O'+opt+'_result']=result.strip()
    state['host_asan_ubsan_pass']=True
    common=['--target=i686-unknown-none-elf','-Oz','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin',
            '-fno-pic','-fno-pie','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
            '-mno-mmx','-mno-sse','-mno-sse2','-ffunction-sections','-fdata-sections','-fstack-usage',
            '-DGTOS_NATIVE_COMPONENT=1','-I'+str(app),'-I'+str(skia),'-I'+str(gif/'freestanding'),'-I'+str(cache),*release]
    objects=[]
    for name,source in [('decode',app/'png_decode.c'),('memory',gif/'memory.c'),
                        ('ashldi3',cache/'compiler-rt/ashldi3.c'),('lshrdi3',cache/'compiler-rt/lshrdi3.c'),
                        ('udivdi3',cache/'compiler-rt/udivdi3.c'),
                        ('udivmoddi4',cache/'compiler-rt/udivmoddi4.c')]:
        obj=out/(name+'.o');objects.append(obj)
        run('native-'+name,[clang,'-std=c11',*common,'-c',source,'-o',obj])
    cxx=['-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics']
    for name,source in [('pixel',app/'png_pixel.cc'),('skia',skia/'skia_pixel.cc')]:
        obj=out/(name+'.o');objects.append(obj)
        run('native-'+name,[clang,*cxx,*common,'-c',source,'-o',obj])
    library=out/'libgtos_png_pixels.a'
    run('native-archive',[tool('llvm-ar'),'rcs',library,*objects])
    component=out/'gtos-png-pixels.o'
    run('native-reachable',[tool('ld.lld'),'-m','elf_i386','-r','--gc-sections',
        '--undefined=gtos_png_context_bytes','--undefined=gtos_png_inspect',
        '--undefined=gtos_png_decode_bgra','--undefined=gtos_png_decode_rgba','-o',component,library])
    state['component_undefined']=run('component-undefined',[tool('llvm-nm'),'--undefined-only',component]).strip()
    assert not state['component_undefined'],'Reusable component must close its real dependency graph'
    state['component_size']=run('component-size',['size',component]).strip()
    run('native-main',[clang,*cxx,*common,'-c',app/'native_main.cc','-o',out/'main.o'])
    run('native-start',[clang,'--target=i686-unknown-none-elf','-c',app/'start.s','-o',out/'start.o'])
    elf=out/'png-pixels-probe.elf'
    run('native-link',[tool('ld.lld'),'-m','elf_i386','--gc-sections','--build-id=none',
        '-Map='+str(out/'native-link.map'),'-T',app/'linker.ld',
        '-o',elf,out/'start.o',out/'main.o',library])
    assert not run('native-undefined',[tool('llvm-nm'),'--undefined-only',elf]).strip()
    assembly=run('native-disassembly',['objdump','-d',elf])
    assert not re.search(r'%(?:[xyz]mm\d+|mm\d+|st)\b',assembly)
    assert not re.search(r'\t(?:fadd|fsub|fmul|fdiv|fld|fst|fild|fist|fsin|fcos|fsqrt)\w*\s',assembly)
    state['readelf']=run('native-readelf',['readelf','-h','-lW','-r',elf])
    stripped=out/'png-pixels-probe.stripped.elf'
    run('native-strip',[tool('llvm-objcopy'),'--strip-all',elf,stripped])
    inventory=json.loads(subprocess.check_output(['python3',str(repo/'tools/browser_artifact.py'),str(elf)],text=True))
    assert inventory['elf_bits']==32 and inventory['machine']=='i386' and inventory['elf_type']=='EXEC'
    assert not inventory['interpreters'] and all(s['permissions']!='RWX' for s in inventory['segments'])
    assert set(inventory['requirements']) <= {'zero-filled memory beyond file-backed bytes'}
    (out/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
    stack=[]
    for file in out.glob('*.su'):
        for line in file.read_text().splitlines():
            fields=line.split('\t')
            if len(fields)>=3:stack.append(dict(function=fields[0],bytes=int(fields[1]),kind=fields[2]))
    state['stack_usage']=stack
    state['maximum_function_stack_bytes']=max(x['bytes'] for x in stack)
    state['stack_call_chain_qualified']=False
    state.update(native_build_pass=True,elf=str(elf),elf_bytes=elf.stat().st_size,elf_sha256=sha(elf),
        stripped_elf=str(stripped),stripped_file_bytes=stripped.stat().st_size,stripped_sha256=sha(stripped),
        component=str(component),component_sha256=sha(component),archive=str(library),archive_sha256=sha(library),
        boot_file_admission_pass=stripped.stat().st_size<=65536,native_unresolved_symbols=0,native_fp_simd_registers=0,
        compiler_version=subprocess.check_output([str(clang),'--version'],text=True))
    state['stage']='host-tested-native-built-guest-blocked' if not state['boot_file_admission_pass'] else 'host-tested-native-built-guest-pending'
    state['guest_not_run_reason']='Existing 64KiB external boot ELF admission exceeded; no limit change or alternate kernel-load substitution.' if not state['boot_file_admission_pass'] else 'Guest acceptance still required.'
    assert state['source_sha256']=={str(x.relative_to(repo)):sha(x) for x in source_paths}
except Exception as error:
    state['error']=repr(error);state['stage']='failed';raise
finally:
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps({k:v for k,v in state.items() if k in ('stage','host_asan_ubsan_pass','native_build_pass','elf','elf_bytes','elf_sha256','boot_file_admission_pass','guest_pass','browser_guest_pass','guest_not_run_reason')},indent=2))
