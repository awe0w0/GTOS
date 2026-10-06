#!/usr/bin/env python3
"""Build the real pinned Skia alpha leaf and an existing GTOS ABI1 app."""
import argparse, datetime, hashlib, json, os, pathlib, re, shutil, subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=pathlib.Path, help='Fresh output directory')
p.add_argument('--clang', default=os.environ.get('CLANG', 'clang'))
p.add_argument('--host-cxx', default=os.environ.get('CXX', 'g++'))
a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
app=repo/'apps/skia_alpha_probe'
out=a.output.resolve()
assert not out.exists(), 'Choose a fresh output directory'
out.parent.mkdir(parents=True, exist_ok=True)
assert shutil.disk_usage(out.parent).free>128*1024*1024
out.mkdir()
upstream=json.loads((app/'source-manifest.json').read_text())
for item in upstream['files']:
    assert hashlib.sha256((app/item['file']).read_bytes()).hexdigest()==item['sha256']
fragment=(app/'skia_alpha_upstream.inc').read_text()
assert hashlib.sha256(fragment[fragment.index('/**'):].encode()).hexdigest()==upstream['fragment_sha256']
original=(app/'SkMath.original.h').read_text()
assert fragment[fragment.index('/**'):] in original
assert not upstream['function_bodies_modified']
source_hashes={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in app.iterdir() if x.is_file()}
clang=pathlib.Path(shutil.which(a.clang) or a.clang).resolve()
assert clang.is_file()
def tool(name):
    local=clang.parent/name
    result=str(local) if local.is_file() else shutil.which(name)
    assert result, name
    return result
checks=[]
def run(name, command):
    command=[str(x) for x in command]
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    checks.append(dict(name=name, command=command, exit_code=result.returncode))
    if result.returncode:
        raise RuntimeError(name+' failed; see '+str(out/(name+'.log')))
    return (out/(name+'.log')).read_text()

state=dict(scope='Skia integer alpha and bounded BGRA-to-premultiplied-RGBA component; not full Skia or presentation',
    build_pass=False, host_asan_ubsan_pass=False, guest_pass=False, browser_guest_pass=False,
    checks=checks, upstream_manifest=upstream, source_sha256=source_hashes,
    native_compiler_target='i686-unknown-none-elf', full_cxx_runtime_used=False,
    linux_target_sdk_used=False, kernel_source_changed=False, alpha_pairs=65536, pixel_cases=2000)
try:
    for opt in ('0','2'):
        binary=out/('host-O'+opt)
        run('host-build-O'+opt,[a.host_cxx,'-std=c++11','-O'+opt,'-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',app/'skia_pixel.cc',app/'skia_alpha_host.cpp','-o',binary])
        assert 'HOST SKIA ALPHA PASS 65536 PAIRS 2000 PIXEL CASES BOUNDS' in run('host-run-O'+opt,[binary])
    state['host_asan_ubsan_pass']=True
    flags=['--target=i686-unknown-none-elf','-std=c++11','-Oz','-Wall','-Wextra','-Werror','-ffreestanding',
        '-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-fno-exceptions','-fno-rtti',
        '-fno-threadsafe-statics','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-mno-mmx','-mno-sse','-mno-sse2','-ffunction-sections','-fdata-sections','-I'+str(app)]
    run('native-pixel',[clang,*flags,'-c',app/'skia_pixel.cc','-o',out/'pixel.o'])
    run('native-probe',[clang,*flags,'-c',app/'skia_alpha_guest.cpp','-o',out/'probe.o'])
    run('native-entry',[clang,'--target=i686-unknown-none-elf','-c',app/'start.s','-o',out/'start.o'])
    library=out/'libgtos_skia_alpha.a'
    run('archive-component',[tool('llvm-ar'),'rcs',library,out/'pixel.o'])
    elf=out/'skia-alpha-probe.elf'
    run('native-link',[tool('ld.lld'),'-m','elf_i386','--gc-sections','--build-id=none','-T',app/'linker.ld',
        '-o',elf,out/'start.o',out/'probe.o',library])
    assert not run('native-undefined',[tool('llvm-nm'),'--undefined-only',elf]).strip()
    assert elf.stat().st_size<=65536
    assembly=run('native-disassembly',['objdump','-d',elf])
    assert not re.search(r'%(?:[xyz]mm\d+|mm\d+|st)\b',assembly)
    assert not re.search(r'\t(?:fadd|fsub|fmul|fdiv|fld|fst|fild|fist|fsin|fcos|fsqrt)\w*\s',assembly)
    run('native-readelf',['readelf','-h','-l','-S',elf])
    inventory=json.loads(subprocess.check_output(['python3',str(repo/'tools/browser_artifact.py'),str(elf)],text=True))
    assert inventory['elf_bits']==32 and inventory['machine']=='i386' and inventory['elf_type']=='EXEC'
    assert not inventory['interpreters']
    assert set(inventory['requirements']) <= {'zero-filled memory beyond file-backed bytes'}
    assert all(s['permissions']!='RWX' for s in inventory['segments'])
    (out/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
    assert source_hashes=={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in app.iterdir() if x.is_file()}
    state.update(build_pass=True, elf=str(elf), elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
        elf_bytes=elf.stat().st_size, component=str(library), component_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
        compiler_version=subprocess.check_output([str(clang),'--version'],text=True), unresolved_symbols=0,
        fp_simd_registers=0, system_install=False)
except Exception as error:
    state['error']=repr(error)
    raise
finally:
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps(dict(build_pass=state['build_pass'], host_asan_ubsan_pass=state['host_asan_ubsan_pass'],
    guest_pass=False, elf=str(elf), elf_sha256=state['elf_sha256']),indent=2))
