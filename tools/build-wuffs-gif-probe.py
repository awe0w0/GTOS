#!/usr/bin/env python3
"""Build a genuine scalar GIF dependency and existing-i386-ABI qualification app."""
import argparse, base64, datetime, hashlib, json, os, pathlib, re, shutil, subprocess, urllib.request

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',help='New output directory; all downloads/builds stay here or in --dependency-cache')
parser.add_argument('--dependency-cache',type=pathlib.Path)
parser.add_argument('--clang',default=os.environ.get('CLANG','clang'))
parser.add_argument('--host-cc',default=os.environ.get('HOST_CC','gcc'))
args=parser.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
app=repo/'apps/wuffs_gif_probe'
out=pathlib.Path(args.output).resolve()
assert not out.exists(),'Choose a fresh output directory'
out.parent.mkdir(parents=True,exist_ok=True)
assert shutil.disk_usage(out.parent).free>128*1024*1024,'At least 128 MiB free is needed'
out.mkdir()
cache=(args.dependency_cache or out/'dependencies').resolve();cache.mkdir(parents=True,exist_ok=True)
manifest=json.loads((app/'dependencies.json').read_text())
downloaded=[]
for item in manifest['files']:
    path=cache/item['file'];path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():data=path.read_bytes()
    else:
        request=urllib.request.Request(item['url'],headers={'User-Agent':'GTOS-native-codec-qualification'})
        limit=(item['bytes']+2)//3*4+16 if item.get('encoding')=='base64' else item['bytes']+1
        with urllib.request.urlopen(request,timeout=45) as response:data=response.read(limit)
        if item.get('encoding')=='base64':data=base64.b64decode(data,validate=True)
        assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['file']
        with path.open('xb') as destination:destination.write(data)
    assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['file']
    downloaded.append(dict(file=str(path),sha256=item['sha256'],bytes=len(data),url=item['url']))

clang=pathlib.Path(shutil.which(args.clang) or args.clang).resolve()
assert clang.is_file(),clang
def tool(name,fallback=None):
    local=clang.parent/name
    value=str(local) if local.is_file() else shutil.which(fallback or name)
    assert value,'Required tool: '+name
    return value
ld=tool('ld.lld');ar=tool('llvm-ar','ar');nm=tool('llvm-nm','nm')
objdump=shutil.which('objdump');readelf=shutil.which('readelf')
assert objdump and readelf,'GNU objdump/readelf are required'
checks=[]
def run(name,command):
    command=[str(x) for x in command]
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=240)
    checks.append(dict(name=name,command=command,exit_code=result.returncode))
    if result.returncode:
        raise RuntimeError(name+' failed; see '+str(out/(name+'.log')))
    return (out/(name+'.log')).read_text()

common=['-std=c11','-Oz','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin',
        '-fno-pic','-fno-pie','-fno-stack-protector','-fno-unwind-tables',
        '-fno-asynchronous-unwind-tables','-mno-mmx','-mno-sse','-mno-sse2',
        '-ffunction-sections','-fdata-sections','-D__GTOS__=1','-DGTOS_NATIVE_COMPONENT=1',
        '-I'+str(app),'-I'+str(app/'freestanding'),'-I'+str(cache)]
objects={}
for name in ('gif_codec','memory','codec_test','main'):
    obj=out/(name+'.o');objects[name]=obj
    run('compile-'+name,[clang,'--target=i686-unknown-none-elf',*common,'-c',app/(name+'.c'),'-o',obj])
runtime=out/'ashldi3.o'
run('compile-scalar-runtime',[clang,'--target=i686-unknown-none-elf',*common,
                              '-c',cache/'compiler-rt/ashldi3.c','-o',runtime])
start=out/'start.o'
run('assemble-entry',[clang,'--target=i686-unknown-none-elf','-c',app/'start.s','-o',start])
library=out/'libgtos_wuffs_gif.a'
run('archive-component',[ar,'rcs',library,objects['gif_codec'],objects['memory'],runtime])
component=out/'gtos-wuffs-gif.o'
run('link-reachable-component',[ld,'-r','--gc-sections','--undefined=gtos_gif_context_size',
                               '--undefined=gtos_gif_first_frame','-o',component,library])
assert not run('component-undefined',[nm,'--undefined-only',component]).strip()
elf=out/'wuffs-gif-probe.elf'
run('link-native-probe',[ld,'-m','elf_i386','--gc-sections','--build-id=none','-T',app/'linker.ld',
                        '-o',elf,start,objects['main'],objects['codec_test'],library])
assert elf.stat().st_size<=65536,'The unchanged StartNativeDemo boot fixture limits each external ELF to 64 KiB'
assert not run('probe-undefined',[nm,'--undefined-only',elf]).strip()
assembly=run('native-disassembly',[objdump,'-d',elf])
assert not re.search(r'%(?:[xyz]mm\d+|mm\d+|st)\b',assembly),'Unexpected native FP/SIMD'
run('native-readelf',[readelf,'-h','-l','-S','-r',elf])
run('native-size',[shutil.which('size'),'--format=berkeley',elf])
inventory=json.loads(subprocess.check_output(['python3',str(repo/'tools/browser_artifact.py'),str(elf)],text=True))
assert inventory['elf_bits']==32 and inventory['machine']=='i386' and inventory['elf_type']=='EXEC'
assert not inventory['interpreters']
assert set(inventory['requirements']) <= {'zero-filled memory beyond file-backed bytes'}
assert all(segment['permissions']!='RWX' for segment in inventory['segments'])
(out/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')

# Rename the actual byte primitives so ASan's own startup uses its normal libc.
# The instrumented decoder still calls our real byte-operation implementations.
renames=['-D'+name+'=gtos_codec_'+name for name in ('memcpy','memmove','memset','memcmp')]
host=out/'host-gif-asan-ubsan'
run('host-sanitizer-build',[args.host_cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                          '-fno-builtin','-ffunction-sections','-fdata-sections',
                          '-fsanitize=address,undefined','-fno-pie','-no-pie','-DGTOS_CODEC_HOST_TEST',
                          *renames,'-I'+str(app),'-I'+str(cache),app/'gif_codec.c',app/'memory.c',
                          app/'codec_test.c','-Wl,--gc-sections','-o',host])
assert 'WUFFS GIF GTOS COMPONENT PASS' in run('host-sanitizer-runtime',[host])
state=dict(timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
           scope='Real scalar Wuffs GIF native component and i386 ABI1 test app, not a browser',
           build_pass=True,host_asan_ubsan_pass=True,guest_pass=False,browser_guest_pass=False,
           elf=str(elf),elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
           component=str(component),component_sha256=hashlib.sha256(component.read_bytes()).hexdigest(),
           compiler_version=subprocess.check_output([str(clang),'--version'],text=True),
           dependency_manifest=manifest,downloaded=downloaded,checks=checks,
           native_unresolved_symbols=0,native_simd_registers=0,system_install=False,
           boot_demo_file_limit_bytes=65536,generic_trusted_image_limit_bytes=2*1024*1024)
(out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps(dict(build_pass=True,host_asan_ubsan_pass=True,guest_pass=False,elf=str(elf),elf_sha256=state['elf_sha256']),indent=2))
