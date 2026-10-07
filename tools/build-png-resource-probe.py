#!/usr/bin/env python3
"""Build a genuine GTOS i386 resource consumer from proven PNG archive objects."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--qualified-png',required=True,type=pathlib.Path)
parser.add_argument('--dependency-cache',required=True,type=pathlib.Path)
parser.add_argument('--clang',required=True,type=pathlib.Path)
parser.add_argument('--host-cc',required=True,type=pathlib.Path)
parser.add_argument('--host-cxx',required=True,type=pathlib.Path)
parser.add_argument('--surface',action='store_true',help='Build the native PNG desktop surface consumer')
args=parser.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
resource_app=repo/'apps/png_resource_probe'
app=repo/('apps/png_surface_probe' if args.surface else 'apps/png_resource_probe');png=repo/'apps/png_image_codec'
reference=args.qualified_png.resolve();cache=args.dependency_cache.resolve()
out=args.output.resolve();assert not out.exists(),'Choose a fresh output directory'
out.mkdir(parents=True)
checks=[]
state=dict(scope='Versioned immutable public PNG read -> unchanged real Wuffs/Skia decode; no presentation',
    surface_consumer=args.surface,
    source_base=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
    native_build_pass=False,boot_file_admission_pass=False,guest_pass=False,
    stack_call_chain_qualified=False,qualification_complete=False,
    native_compiler_target='i686-unknown-none-elf',linux_target_sdk_used=False,
    capacity_changed=False,boot_demo_file_limit_bytes=65536,user_page_budget=256,
    user_stack_bytes=8192,user_stack_usable_bytes=8176,checks=checks)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(name,command,timeout=240):
    command=[str(x) for x in command]
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    checks.append(dict(name=name,command=command,exit_code=result.returncode))
    assert not result.returncode,name+' failed; see '+str(out/(name+'.log'))
    return (out/(name+'.log')).read_text()

def record():
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')

def scalar_audit(elf,map_path,owners):
    # The existing kernel audit accepts GNU maps only. This component uses the
    # qualified lld linker, so bind its actual input-section placements directly
    # to the same checked ELF parser and prohibited-instruction classifier.
    spec=importlib.util.spec_from_file_location('kernel_scalar_audit',repo/'tools/audit-kernel-instructions.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    binary=module.Elf(elf)
    inputs={path.name:module.Elf(path) for path in owners}
    ranges=[];seen=set()
    pattern=re.compile(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\d+\s+(.+):\(([^)]+)\)$')
    text=map_path.read_text()
    assert text.startswith('     VMA      LMA     Size Align Out     In      Symbol')
    for line in text.splitlines():
        match=pattern.match(line)
        if not match:continue
        address,lma,size,owner,section_name=match.groups()
        assert address==lma,'Unexpected component load address'
        if owner=='<internal>':
            assert section_name in ('.symtab','.strtab','.shstrtab','.rodata.cst8'),\
                'Unexpected linker-generated input: '+section_name
            if section_name in ('.symtab','.strtab','.shstrtab'):
                metadata=next(s for s in binary.sections if s['name']==section_name)
                assert not metadata['flags']&6,'Allocated/executable synthetic metadata'
                assert metadata['type']==(2 if section_name=='.symtab' else 3)
                assert metadata['addr']==int(address,16) and metadata['size']==int(size,16)
            else:
                # lld deduplicates mergeable 8-byte constants into one internal
                # placement. Prove every output atom belongs to non-code input.
                atoms=set()
                for obj in inputs.values():
                    for section in obj.sections:
                        if section['name']!=section_name:continue
                        assert not section['flags']&4 and section['size']%8==0
                        data=obj.section_data(section)
                        atoms.update(data[i:i+8] for i in range(0,len(data),8))
                start=int(address,16);length=int(size,16)
                mapped=binary.containing(start,length)
                data=binary.data[mapped['offset']+start-mapped['addr']:mapped['offset']+start-mapped['addr']+length]
                assert length%8==0 and all(data[i:i+8] in atoms for i in range(0,length,8))
            continue
        member=re.search(r'\.a\(([^)]+)\)$',owner)
        name=member.group(1) if member else pathlib.Path(owner).name
        assert name in inputs,'Unproven linked input: '+owner
        section=next(s for s in inputs[name].sections if s['name']==section_name)
        assert int(size,16)==section['size'],'Input section size changed'
        if not section['flags']&4 or not section['size']:continue
        key=(name,section_name);assert key not in seen,'Duplicate executable placement'
        seen.add(key);start=int(address,16);end=start+section['size']
        binary.containing(start,end-start);ranges.append((start,end))
    assert ranges,'No linked executable input sections'
    merged=[]
    for start,end in sorted(ranges):
        assert not merged or start>=merged[-1][1],'Overlapping linked code ranges'
        if merged and start==merged[-1][1]:merged[-1]=(merged[-1][0],end)
        else:merged.append((start,end))
    count=0
    for start,end in merged:
        section=binary.containing(start,end-start)
        command=['objdump','-D','-z','--no-show-raw-insn','-m','i386',
                 '--section='+section['name'],'--start-address='+str(start),
                 '--stop-address='+str(end),str(elf)]
        decoded=subprocess.check_output(command,text=True)
        found=0
        for line in decoded.splitlines():
            match=re.match(r'^\s*([0-9a-f]+):\s+(.+)$',line)
            if not match:continue
            address=int(match.group(1),16);instruction=match.group(2).strip()
            assert start<=address<end
            assert '(bad)' not in instruction and not instruction.startswith('.byte'),'Undecodable code'
            assert not module.touches_fp(instruction),'Prohibited FP/SIMD instruction: '+instruction
            found+=1;count+=1
        assert found,'Executable range not decoded'
    result=dict(pass_=True,scope='All lld-placed SHF_EXECINSTR input sections; no exemptions',
                executable_sections=len(ranges),merged_ranges=len(merged),instructions=count,
                input_objects=len(inputs),classifier_sha256=sha(repo/'tools/audit-kernel-instructions.py'))
    (out/'native-scalar-audit.log').write_text(json.dumps(result,indent=2)+'\n')
    state['scalar_audit']=result

try:
    run('resource-generated-stable',['python3',repo/'tools/generate-native-png-resource.py','--check'])
    resource=json.loads((resource_app/'resource_manifest.json').read_text())
    assert resource['png_bytes']==1108 and sha(resource_app/'resource.png')==resource['png_sha256']
    state['resource']=resource
    state.update(resource_version=resource['resource_version'],resource_id=resource['resource_id'],
                 png_sha256=resource['png_sha256'],png_bytes=resource['png_bytes'],
                 bgra_sha256=resource['bgra_sha256'],premul_rgba_sha256=resource['premul_rgba_sha256'])
    proven=json.loads((reference/'manifest.json').read_text())
    assert proven['native_build_pass'] and proven['host_asan_ubsan_pass']
    assert proven['wuffs_profile']=='gtos-release' and proven['component_undefined']==''
    assert sha(reference/'libgtos_png_pixels.a')==proven['archive_sha256']
    assert sha(reference/'png-pixels-probe.stripped.elf')==proven['stripped_sha256']
    # Bind the reused object graph to all exact original input bytes, including
    # preserved fixture/native qualifier sources, without rerunning their matrix.
    documentation_changes={}
    for name,digest in proven['source_sha256'].items():
        current=sha(repo/name)
        if current!=digest and name=='apps/png_image_codec/CORE_REVIEW.md':
            # The already-published baseline updated this documentation after
            # qualification. Prove that exact known baseline, never waive code.
            baseline=subprocess.check_output(['git','-C',str(repo),'show','f7e6a47:'+name])
            assert baseline==(repo/name).read_bytes(),'Unexpected qualification-document change'
            assert sha(reference/'source-snapshot'/name)==digest
            documentation_changes[name]=dict(old_sha256=digest,new_sha256=current,
                baseline='f7e6a47',scope='Known qualification-documentation-only publication change')
        else:
            assert current==digest,'Qualified PNG input changed: '+name
    state['qualification_documentation_changes']=documentation_changes
    for item in proven['verified_dependencies']:
        path=cache/item['file']
        assert path.stat().st_size==item['bytes'] and sha(path)==item['sha256'],item['file']
    adaptation=proven['wuffs_adaptation']
    for key in ('derived','diff'):
        part=adaptation[key]
        path=reference/'derived'/pathlib.Path(part['file']).name
        assert sha(path)==part['sha256'],key
    clang=args.clang.resolve();bindir=clang.parent
    ar=bindir/'llvm-ar';linker=bindir/'ld.lld';nm=bindir/'llvm-nm';objcopy=bindir/'llvm-objcopy'
    objects=('decode','memory','ashldi3','lshrdi3','udivdi3','udivmoddi4','pixel','skia')
    library=out/'libgtos_png_pixels.a';shutil.copyfile(reference/library.name,library)
    imported={}
    for name in objects:
        obj=reference/(name+'.o');su=reference/(name+'.su')
        member=subprocess.check_output([str(ar),'p',str(library),obj.name])
        assert member==obj.read_bytes(),'Archive/object ownership mismatch: '+name
        shutil.copyfile(obj,out/obj.name);shutil.copyfile(su,out/su.name)
        imported[obj.name]=sha(obj)
    state.update(qualified_archive_sha256=sha(library),qualified_object_sha256=imported,
                 qualified_build_manifest_sha256=sha(reference/'manifest.json'),
                 qualified_png_source_sha256=proven['source_sha256'],
                 host_asan_ubsan_pass=True,host_matrix_reused_for_identical_png_objects=True,
                 consumer_host_execution=False)
    paths=set(proven['source_sha256'])
    paths.update(str(p.relative_to(repo)) for p in app.iterdir() if p.is_file())
    paths.update(str(p.relative_to(repo)) for p in resource_app.iterdir() if p.is_file())
    if args.surface:
        paths.update(('include/process/surface_abi.h','include/process/native_surface.h',
            'src/process/native_surface.cpp','include/gui/native_image.h'))
    paths.update(('tools/build-png-resource-probe.py','tools/generate-native-png-resource.py',
                  'include/process/abi.h','include/process/resource_abi.h','include/process/resources.h',
                  'src/process/resources.cpp','src/process/resources_png.inc',
                  'src/process/native_runtime.cpp','src/kernel.cpp','src/process/elf32.cpp',
                  'apps/wuffs_gif_probe/validation_host.c','apps/wuffs_gif_probe/validation_host.cpp',
                  'tools/audit-kernel-instructions.py','tools/browser_artifact.py'))
    state['source_sha256']={name:sha(repo/name) for name in sorted(paths)}
    for name in paths:
        snapshot=out/'source-snapshot'/name;snapshot.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(repo/name,snapshot)
    common=['--target=i686-unknown-none-elf','-Oz','-Wall','-Wextra','-Werror','-ffreestanding',
        '-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-fno-unwind-tables',
        '-fno-asynchronous-unwind-tables','-mno-mmx','-mno-sse','-mno-sse2',
        '-ffunction-sections','-fdata-sections','-fstack-usage','-DGTOS_NATIVE_COMPONENT=1',
        '-I'+str(repo/'include'),'-I'+str(app),'-I'+str(resource_app),'-I'+str(png),
        '-I'+str(repo/'apps/wuffs_gif_probe/freestanding')]
    run('native-main',[clang,'-std=c++11','-fno-exceptions','-fno-rtti',
        '-fno-threadsafe-statics',*common,'-c',app/'native_main.cc','-o',out/'main.o'])
    run('native-start',[clang,'--target=i686-unknown-none-elf','-c',png/'start.s','-o',out/'start.o'])
    run('native-register-probe',[clang,'--target=i686-unknown-none-elf','-c',
        resource_app/'register_probe.s','-o',out/'register-probe.o'])
    # Include the unchanged qualified linker geometry. The extra absolute symbol
    # is solely a diagnostic description of our own final mapping boundary.
    script=out/'resource-linker.ld'
    script.write_text('INCLUDE "'+str(png/'linker.ld')+'"\n'+
                      'resource_probe_rw_limit = ALIGN(ADDR(.bss) + SIZEOF(.bss), 4096);\n')
    elf=out/'resource-probe.elf'
    run('native-link',[linker,'-m','elf_i386','--gc-sections','--build-id=none',
        '-Map='+str(out/'native-link.map'),'-T',script,'-o',elf,
        out/'start.o',out/'main.o',out/'register-probe.o',library])
    assert not run('native-undefined',[nm,'--undefined-only',elf]).strip()
    assembly=run('native-disassembly',['objdump','-d',elf])
    assert not re.search(r'%(?:[xyz]mm\d+|mm\d+|st)\b',assembly)
    assert not re.search(r'\t(?:fadd|fsub|fmul|fdiv|fld|fst|fild|fist|fsin|fcos|fsqrt)\w*\s',assembly)
    scalar_audit(elf,out/'native-link.map',
                 [out/(name+'.o') for name in objects]+[out/'main.o',out/'start.o',out/'register-probe.o'])
    state['readelf']=run('native-readelf',['readelf','-h','-lW','-r',elf])
    stripped=out/'resource-probe.stripped.elf'
    run('native-strip',[objcopy,'--strip-all',elf,stripped])
    inventory=json.loads(subprocess.check_output(['python3',str(repo/'tools/browser_artifact.py'),str(elf)],text=True))
    assert inventory['elf_bits']==32 and inventory['machine']=='i386' and inventory['elf_type']=='EXEC'
    assert not inventory['interpreters'] and all(s['permissions']!='RWX' for s in inventory['segments'])
    assert set(inventory['requirements']) <= {'zero-filled memory beyond file-backed bytes'}
    (out/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
    load_pages=0
    for segment in inventory['segments']:
        if segment['type']=='LOAD':
            start=segment['address'];size=segment['memory_bytes']
            load_pages+=((start+size+4095)//4096)-(start//4096)
    assert load_pages+2<=256,'Existing per-process page budget exceeded'
    # Invoke the actual repository ELF32 validator, with host ASan/UBSan.
    validator=out/'actual-elf32-validator';host_io=out/'host-io.o'
    validation=repo/'apps/wuffs_gif_probe'
    run('host-io-build',[args.host_cc,'-std=c11','-O1','-g','-fsanitize=address,undefined',
        '-fno-pie','-c',validation/'validation_host.c','-o',host_io])
    run('host-validator-build',[args.host_cxx,'-std=c++11','-O1','-g',
        '-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(repo/'include'),
        repo/'src/process/elf32.cpp',validation/'validation_host.cpp',host_io,'-o',validator])
    validation_result=run('actual-elf32-validation',[validator,stripped])
    assert 'REAL GTOS ELF32 VALIDATOR PASS' in validation_result
    stack=[]
    for file in out.glob('*.su'):
        for line in file.read_text().splitlines():
            fields=line.split('\t')
            if len(fields)>=3:stack.append(dict(function=fields[0],bytes=int(fields[1]),kind=fields[2]))
    assert state['source_sha256']=={name:sha(repo/name) for name in sorted(paths)},'Source changed during build'
    state.update(stage='native-built-stack-and-guest-pending',native_build_pass=True,
        elf=str(elf),elf_bytes=elf.stat().st_size,elf_sha256=sha(elf),
        stripped_elf=str(stripped),stripped_file_bytes=stripped.stat().st_size,
        stripped_sha256=sha(stripped),boot_file_admission_pass=stripped.stat().st_size<=65536,
        load_pages=load_pages,total_user_pages=load_pages+2,actual_elf32_validation_pass=True,
        native_unresolved_symbols=0,native_fp_simd_registers=0,stack_usage=stack,
        maximum_function_stack_bytes=max(x['bytes'] for x in stack),
        consumer_object_sha256={name:sha(out/name) for name in ('main.o','start.o','register-probe.o')})
    assert state['boot_file_admission_pass'],'Existing external ELF file cap exceeded'
    if args.surface:
        state['scope']='Native immutable PNG read -> real Wuffs/Skia decode -> bounded desktop surface v1'
except Exception as error:
    state.update(stage='failed',error=repr(error));raise
finally:
    record()
print(json.dumps({key:state[key] for key in ('stage','native_build_pass','boot_file_admission_pass',
    'stripped_elf','stripped_file_bytes','stripped_sha256','total_user_pages','stack_call_chain_qualified')},indent=2))
