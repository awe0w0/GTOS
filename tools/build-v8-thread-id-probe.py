#!/usr/bin/env python3
"""Build the real pinned V8 ThreadId/compiler-emulated TLS as an integer-only GTOS CPL3 leaf."""
import argparse
import datetime
import difflib
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import subprocess
import sys
import yaml
sys.dont_write_bytecode=True

V8_REV='be042d4462bee463c9b785701b8c6ee4576ee0a3'
LIBCXX_REV='97b436da4c33663581d394f4ee0a5977fc38c2f4'
GTEST_REV='4fe3307fb2d9f86d19777c7eb0e4809e9694dde7'
LIBC_REV='ebe33e01982dbbf879661e3b6b78450f3020a53f'
FOUNDATION_SHA='f8679584282798a4fb5d40701895548457e94675f026ca111a36c728ef819f48'
repo=pathlib.Path(__file__).resolve().parents[1]
workspace=repo.parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--v8-source',type=pathlib.Path,default=workspace/'sources/v8-reference/v8')
parser.add_argument('--libcxx-source',type=pathlib.Path,default=workspace/'sources/chromium-full/src/third_party/libc++/src')
parser.add_argument('--llvm-libc-source',type=pathlib.Path,default=workspace/'sources/chromium-full/src/third_party/llvm-libc/src')
parser.add_argument('--platform-patch',type=pathlib.Path,default=workspace/'patches/gtos-platform-foundation/v8-0001-gtos-platform-foundation.patch')
parser.add_argument('--qualified-png',required=True,type=pathlib.Path)
parser.add_argument('--clang',required=True,type=pathlib.Path)
parser.add_argument('--host-cc',required=True,type=pathlib.Path)
parser.add_argument('--host-cxx',required=True,type=pathlib.Path)
parser.add_argument('--mode',type=int,choices=range(7),default=0)
parser.add_argument('--stack-analysis-directory',type=pathlib.Path,default=workspace/'artifacts/png-stack-gn-release-20261007T103153Z')
parser.add_argument('--optimization',choices=('O0','Oz'),default='Oz')
parser.add_argument('--link-only',action='store_true',help='Diagnostic complete link; does not establish static qualification')
parser.add_argument('--prepare-only',action='store_true',help='Compile the actual upstream object and qualify its SDK inputs without linking the consumer')
args=parser.parse_args()
out=args.output.resolve();assert not out.exists(),'Choose a fresh evidence directory'
out.mkdir(parents=True)
app=repo/'apps/v8_thread_id_probe'
v8=args.v8_source.resolve();libcxx=args.libcxx_source.resolve();libc=args.llvm_libc_source.resolve()
clang=args.clang.resolve();bindir=clang.parent
gtest=v8/'third_party/googletest/src'
checks=[]
state=dict(scope='Real whole-byte pinned V8 thread-id.cc/thread-id.h plus Clang24 compiler-emulated TLS bounded single-Task storage',
    native_build_pass=False,guest_pass=False,stack_call_chain_qualified=False,
    qualification_complete=False,source_provenance_pass=False,linux_target_sdk_used=False,capacity_changed=False,
    jit_guest_pass=False,browser_guest_pass=False,media_guest_pass=False,video_guest_pass=False,html5_guest_pass=False,full_v8_backend=False,
    native_tls_backend=False,shared_pas_threads=False,platform_key_tls=False,tls_dynamic_initialization=False,tls_destructor_registration=False,
    source_base=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
    tls_mode=args.mode,optimization=args.optimization,role_nonce=0xA9,probe_record_address=0x40030000,probe_record_bytes=512,
    peer_copy_annotation=dict(offset=28,bytes=4,original=0xA9,peer=0x37,original_formal_elf_is_immutable=True),
    boot_demo_file_limit_bytes=65536,user_page_budget=256,user_stack_bytes=8192,
    user_stack_usable_bytes=8176,native_compiler_target='i686-unknown-none-elf',checks=checks)

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(name,command,timeout=120):
    command=[str(x) for x in command]
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    checks.append(dict(name=name,command=command,exit_code=result.returncode))
    assert result.returncode==0,name+' failed; see '+str(out/(name+'.log'))
    return (out/(name+'.log')).read_text()
def save_state():
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')
def copied(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,destination)
    assert source.read_bytes()==destination.read_bytes()
def pinned_inputs(paths,root,revision):
    paths=sorted(p for p in paths if p.is_file() and p.is_relative_to(root))
    if not paths:return {}
    specs=[revision+':'+p.relative_to(root).as_posix() for p in paths]
    data=subprocess.check_output(['git','-C',str(root),'cat-file','--batch'],input=('\n'.join(specs)+'\n').encode())
    offset=0;result={}
    for path,spec in zip(paths,specs):
        end=data.index(b'\n',offset);header=data[offset:end].decode().split()
        assert len(header)==3 and header[1]=='blob',('Missing pinned SDK input',spec)
        size=int(header[2]);offset=end+1;original=data[offset:offset+size];offset+=size+1
        assert path.read_bytes()==original,('SDK input differs from pin',spec)
        result[str(path)]=hashlib.sha256(original).hexdigest()
    assert offset==len(data)
    return result

def sdk_config(template):
    values={'_LIBCPP_ABI_VERSION':'1','_LIBCPP_ABI_NAMESPACE':'__gtos_leaf',
        '_LIBCPP_ABI_FORCE_ITANIUM':'1','_LIBCPP_HAS_LOCALIZATION':'1',
        '_LIBCPP_LIBC_LLVM_LIBC':'1','_LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION':'1',
        '_LIBCPP_HARDENING_MODE_DEFAULT':'(1 << 2)',
        '_LIBCPP_ASSERTION_SEMANTIC_DEFAULT':'(1 << 1)'}
    text=template.read_text()
    text=re.sub(r'^#cmakedefine01 (\w+)$',lambda m:'#define '+m[1]+' '+values.get(m[1],'0'),text,flags=re.M)
    text=re.sub(r'^#cmakedefine (\w+)(?: .*?)?$',lambda m:'#define '+m[1]+' '+values[m[1]] if m[1] in values else '/* #undef '+m[1]+' */',text,flags=re.M)
    text=text.replace('@_LIBCPP_ABI_DEFINES@','').replace('@_LIBCPP_EXTRA_SITE_DEFINES@','#define _LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE\n#define _LIBCPP_PSTL_BACKEND_SERIAL')
    assert '#cmakedefine' not in text and not re.search(r'@\w+@',text)
    return text

def scalar_audit(elf,map_path,owners):
    spec=importlib.util.spec_from_file_location('scalar_classifier',repo/'tools/audit-kernel-instructions.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    binary=module.Elf(elf);inputs={p.name:module.Elf(p) for p in owners}
    ranges=[];merged_strings=[]
    for line in map_path.read_text().splitlines():
        match=re.match(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\d+\s+(.+):\(([^)]+)\)$',line)
        if not match:continue
        address,lma,size,owner,name=match.groups();assert address==lma
        if owner=='<internal>':
            if name=='.rodata.str1.1':
                candidates=[(obj,section) for obj in inputs.values() for section in obj.sections
                    if section['name']==name and section['flags']&48==48 and not section['flags']&5]
                assert len(candidates)==1,'Merged strings require one exact owner'
                obj,section=candidates[0];length=int(size,16);base=int(address,16)
                source=obj.section_data(section);target=binary.containing(base,length)
                actual=binary.section_data(target)[base-target['addr']:base-target['addr']+length]
                assert section['entsize']==1 and section['size']==length and source==actual and source[-1:]==b'\0'
                merged_strings.append(dict(address=base,bytes=length,object=str(obj.path),section=name,
                    original_and_linked_bytes_sha256=hashlib.sha256(source).hexdigest(),exact_single_input_identity=True))
            else:assert name in ('.symtab','.strtab','.shstrtab'),'Unexpected linker input: '+name
            continue
        obj=inputs[pathlib.Path(owner).name]
        section=next(s for s in obj.sections if s['name']==name)
        assert section['size']==int(size,16)
        if section['flags']&4 and section['size']:
            ranges.append((int(address,16),int(address,16)+section['size']))
    assert ranges
    count=0
    for start,end in sorted(ranges):
        section=binary.containing(start,end-start)
        decoded=subprocess.check_output(['objdump','-D','-z','--no-show-raw-insn','-m','i386',
            '--section='+section['name'],'--start-address='+str(start),'--stop-address='+str(end),str(elf)],text=True)
        for line in decoded.splitlines():
            match=re.match(r'^\s*([0-9a-f]+):\s+(.+)$',line)
            if not match:continue
            instruction=match[2].strip()
            assert '(bad)' not in instruction and not instruction.startswith('.byte')
            assert not module.touches_fp(instruction),'FP/SIMD instruction: '+instruction
            count+=1
    assert count
    state['scalar_readonly_merged_strings']=merged_strings
    state['scalar_audit']=dict(pass_=True,instructions=count,input_code_sections=len(ranges),
        classifier_sha256=sha(repo/'tools/audit-kernel-instructions.py'))

source_paths=set(p for tree in (app,) for p in tree.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
source_paths.update(p for p in (repo/'apps/native_heap').rglob('*') if p.is_file())
source_paths.add(repo/'apps/native_thread_id_probe/record.h')
source_paths.update((repo/'tools/build-v8-thread-id-probe.py',repo/'include/process/abi.h',repo/'include/multitasking.h',repo/'src/multitasking.cpp',repo/'include/process/vm_abi.h',repo/'include/process/native_runtime.h',repo/'include/memory/process_address_space.h',repo/'src/memory/process_address_space.cpp',repo/'src/process/native_runtime.cpp',repo/'src/process/native_realtime.cpp',repo/'include/process/native_realtime.h',repo/'include/process/realtime_abi.h',repo/'src/process/elf32.cpp',repo/'tools/audit-kernel-instructions.py',repo/'tools/browser_artifact.py'))
source_paths={p for p in source_paths if p.exists()}
initial_source_sha256={p:sha(p) for p in source_paths}
try:
    assert sys.version_info>=(3,9),'Official pinned LLVM libc hdrgen requires Python >=3.9'
    state['sdk_python']=dict(executable=sys.executable,version=sys.version)
    state['compiler']=dict(path=str(clang),sha256=sha(clang),version=run('compiler-version',[clang,'--version']),
        resource_dir=run('compiler-resource-directory',[clang,'--print-resource-dir']).strip())
    assert subprocess.check_output(['git','-C',str(v8),'rev-parse','HEAD'],text=True).strip()==V8_REV
    assert subprocess.check_output(['git','-C',str(libcxx),'rev-parse','HEAD'],text=True).strip()==LIBCXX_REV
    assert subprocess.check_output(['git','-C',str(libc),'rev-parse','HEAD'],text=True).strip()==LIBC_REV
    assert subprocess.check_output(['git','-C',str(gtest),'rev-parse','HEAD'],text=True).strip()==GTEST_REV
    pinned_deps=subprocess.check_output(['git','-C',str(v8),'show',V8_REV+':DEPS'])
    assert (v8/'DEPS').read_bytes()==pinned_deps and GTEST_REV.encode() in pinned_deps
    foundation=args.platform_patch.resolve();assert sha(foundation)==FOUNDATION_SHA
    derived=out/'derived-v8'
    for name in ('BUILD.gn','include/v8config.h','include/v8-platform.h','include/v8-source-location.h','src/base/platform/semaphore.h'):
        source=v8/name
        assert source.read_bytes()==subprocess.check_output(['git','-C',str(v8),'show',V8_REV+':'+name]),name+' differs from pin'
        copied(source,derived/name)
    run('platform-foundation-patch',['patch','--batch','--forward','-p1','-d',derived,'-i',foundation])
    assert 'GTOS V8 runtime backend is not implemented' in (derived/'BUILD.gn').read_text()
    semaphore=derived/'src/base/platform/semaphore.h'
    original_semaphore=semaphore.read_text()
    marker='  using NativeHandle = starboard::Semaphore;\n#endif'
    assert original_semaphore.count(marker)==1
    semaphore.write_text(original_semaphore.replace(marker,'  using NativeHandle = starboard::Semaphore;\n#elif V8_OS_GTOS\n  // Declaration only: no GTOS semaphore or thread operation is provided.\n  using NativeHandle = uint32_t;\n#endif'))
    declaration_patch=''.join(difflib.unified_diff(original_semaphore.splitlines(True),semaphore.read_text().splitlines(True),fromfile='a/src/base/platform/semaphore.h',tofile='b/src/base/platform/semaphore.h'))
    (out/'semaphore-declaration.patch').write_text(declaration_patch)
    state['semaphore_declaration_patch_sha256']=sha(out/'semaphore-declaration.patch')
    sdk=out/'sdk';sdk.mkdir()
    (sdk/'__config_site').write_text(sdk_config(libcxx/'include/__config_site.in'))
    copied(libcxx/'vendor/llvm/default_assertion_handler.in',sdk/'__assertion_handler')
    sdk_input_paths={libcxx/'include/__config_site.in',libcxx/'vendor/llvm/default_assertion_handler.in',
        libc/'include/__llvm-libc-common.h'}
    sdk_input_paths.update(p for directory in ('llvm-libc-macros','llvm-libc-types','utils/hdrgen')
        for p in (libc/('include/'+directory if directory!='utils/hdrgen' else directory)).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    for directory in ('llvm-libc-macros','llvm-libc-types'):
        shutil.copytree(libc/'include'/directory,sdk/directory)
    copied(libc/'include/__llvm-libc-common.h',sdk/'__llvm-libc-common.h')
    headers=('malloc','stdlib','string','strings','math','ctype','locale','stdio','errno','time','wchar','wctype','inttypes','fenv','assert','limits','features')
    math_profile=[]
    for name in headers:
        selection=[]
        if name=='math':
            # Use hdrgen's actual public entry-point selection. The pin's
            # unguarded cbrtf16 declaration cannot parse on scalar i386.
            functions=yaml.safe_load((libc/'include/math.yaml').read_text())['functions']
            math_profile=[f['name'] for f in functions if '_Float16' not in str(f)]
            for entry in math_profile:selection.extend(('-e',entry))
        run('llvm-libc-header-'+name,[sys.executable,'-B',libc/'utils/hdrgen/main.py',libc/'include'/(name+'.yaml'),
            '-o',sdk/(name+'.h'),'--depfile',out/(name+'-header.d'),*selection])
    (out/'math-header-entry-points.json').write_text(json.dumps(math_profile,indent=2)+'\n')
    state['sdk']=dict(v8_revision=V8_REV,googletest_declaration_revision=GTEST_REV,libcxx_revision=LIBCXX_REV,llvm_libc_revision=LIBC_REV,
        c_headers='Official pinned LLVM libc hdrgen, declarations only; retained services checked by link',
        cxx_headers='Unmodified pinned libc++ headers; official generated __config_site',
        libcxx_header_only=True,threads=False,exception_unwinding=False,
        locale_runtime=False,stdio_runtime=False,foundation_patch_sha256=sha(foundation),
        semaphore_declaration_patch_sha256=sha(out/'semaphore-declaration.patch'))
    generated_inputs={str(p.resolve()):sha(p) for tree in (sdk,derived) for p in tree.rglob('*') if p.is_file()}
    resource_include=pathlib.Path(state['compiler']['resource_dir']).resolve()/'include'
    assert resource_include==(bindir.parent/'lib/clang/24/include').resolve()
    common=['--target=i686-unknown-none-elf','-'+args.optimization,'-femulated-tls','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-ffreestanding',
        '-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-fno-unwind-tables',
        '-fno-asynchronous-unwind-tables','-mno-mmx','-mno-sse','-mno-sse2',
        '-ffunction-sections','-fdata-sections','-fstack-usage','-nostdinc','-nostdinc++',
        '-isystem',libcxx/'include','-isystem',sdk,'-isystem',bindir.parent/'lib/clang/24/include',
        '-I',derived,'-I',v8,'-I',repo/'include','-I',app,
        '-D__GTOS__=1','-DV8_HAVE_TARGET_OS=1','-DV8_TARGET_OS_GTOS=1',
        '-DV8_LOGGING_LEVEL=0','-DV8_NO_FAST_TLS=1','-DNDEBUG',
        '-DGTOS_TLS_PROBE_MODE='+str(args.mode)]
    cxx=['-std=c++20','-fsized-deallocation','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics']
    predefined=run('actual-gtos-target-predefines',[clang,*cxx,*common,'-dM','-E','-x','c++','-include','include/v8config.h','/dev/null'])
    macros=dict(re.findall(r'^#define (\w+) (.*)$',predefined,flags=re.M))
    assert macros.get('__GTOS__')=='1' and macros.get('V8_OS_GTOS')=='1'
    assert not any(name in macros for name in ('__linux__','__linux','linux','__unix__','__unix','_WIN32'))
    assert macros.get('V8_OS_POSIX','0')=='0' and macros.get('V8_OS_LINUX','0')=='0'
    state['actual_target_identity']=dict(gtos=True,linux=False,unix=False,posix=False,macro_log_sha256=sha(out/'actual-gtos-target-predefines.log'))
    actual_upstream=('src/execution/thread-id.cc','src/execution/thread-id.h')
    state['upstream_thread_id_source_sha256']={}
    for name in actual_upstream:
        source=v8/name
        assert source.read_bytes()==subprocess.check_output(['git','-C',str(v8),'show',V8_REV+':'+name])
        state['upstream_thread_id_source_sha256'][name]=sha(source)
    run('actual-upstream-thread-id',[clang,*cxx,*common,'-MD','-MF',out/'thread-id.d','-c',v8/'src/execution/thread-id.cc','-o',out/'thread-id.o'])
    run('actual-upstream-thread-id-calls',[clang,*cxx,*common,'-MD','-MF',out/'thread_id_calls.d','-c',app/'thread_id_calls.cc','-o',out/'thread_id_calls.o'])
    if args.prepare_only:
        state.update(stage='actual-original-thread-id-prepared',sdk_parse_pass=True,
            upstream_consumer_object_sha256=sha(out/'thread_id_calls.o'),upstream_thread_id_object_sha256=sha(out/'thread-id.o'))
    else:
        owners=[out/'thread-id.o',out/'thread_id_calls.o']
        for name,source in (('tls_calls',app/'tls_calls.cc'),('native_main',app/'native_main.cc'),('emutls',app/'emutls.cc'),('heap',repo/'apps/native_heap/heap.cc'),('runtime',app/'runtime.cc')):
            obj=out/(name+'.o');owners.append(obj)
            run(name,[clang,*cxx,*common,'-MD','-MF',out/(name+'.d'),'-c',source,'-o',obj])
        sys.path.insert(0,str(args.stack_analysis_directory.resolve()))
        spec=importlib.util.spec_from_file_location('thread_elf32',args.stack_analysis_directory.resolve()/'elf32.py')
        elfmod=importlib.util.module_from_spec(spec);spec.loader.exec_module(elfmod)
        controls=[];templates={}
        for object_path in (out/'thread-id.o',out/'tls_calls.o'):
            obj=elfmod.Elf32(object_path)
            assert not any(sec['flags']&0x400 or sec['name'].startswith(('.init_array','.fini_array','.ctors','.dtors','.tdata','.tbss')) for sec in obj.sections)
            assert not any(sym['name'].startswith(('_ZTH','_GLOBAL__sub_I')) or sym['name'].startswith('__cxa') for sym in obj.symbols)
            for section in obj.sections:
                if section['name'].startswith('.rodata.__emutls_t.'):
                    symbols=[sy for sy in obj.symbols if sy['section']==section['index'] and sy['type']==1]
                    assert len(symbols)==1 and symbols[0]['value']==0 and symbols[0]['size']==section['size'] and section['flags']==2
                    templates[symbols[0]['name']]=dict(bytes=section['size'],alignment=section['align'],sha256=hashlib.sha256(obj.bytes(section)).hexdigest(),object=str(object_path),section=section['name'])
                if not section['name'].startswith('.data.__emutls_v.'):continue
                symbols=[sy for sy in obj.symbols if sy['section']==section['index'] and sy['type']==1]
                assert len(symbols)==1 and symbols[0]['value']==0 and symbols[0]['size']==16 and section['size']==16 and section['align']==4 and section['flags']==3
                import struct
                size,alignment,cache,initial=struct.unpack('<IIII',obj.bytes(section))
                assert cache==0 and initial==0
                reloc=[rr for rr in obj.relocations if rr['section']==section['index']]
                assert not reloc or len(reloc)==1 and reloc[0]['offset']==12 and reloc[0]['type']==1
                template=reloc[0]['symbol']['name'] if reloc else None
                controls.append(dict(symbol=symbols[0]['name'],binding=symbols[0]['bind'],size=size,alignment=alignment,template_symbol=template,input_section=section['name'],object=str(object_path)))
        controls.sort(key=lambda row:row['input_section'])
        expected=[(4,4,None),(64,4096,'__emutls_t.tls_probe_aligned'),(64,16,'__emutls_t.tls_probe_nonzero'),(65504,16,None),(257,64,None)]
        assert [(row['size'],row['alignment'],row['template_symbol']) for row in controls]==expected
        assert controls[0]['binding']==0 and len(templates)==2
        source=['#include "emutls.h"','extern "C" {']
        for i,row in enumerate(controls):
            row['actual_slot']=i;row['control_offset']=i*16
            row['logical_role']=[0,3,2,4,1][i]
            if row['template_symbol']:
                assert row['binding']==1 and templates[row['template_symbol']]['bytes']==row['size']
                source.append('extern const unsigned char template_%d[%d] asm("%s");'%(i,row['size'],row['template_symbol']))
        source.append('extern const GtosEmutlsSpec gtos_emutls_specs[] = {')
        for i,row in enumerate(controls):source.append('    {%d, %d, %d, %s},'%(i*16,row['size'],row['alignment'],'template_'+str(i) if row['template_symbol'] else 'nullptr'))
        source.extend(('};','extern const unsigned gtos_emutls_spec_count = 5;','}'))
        (out/'emutls_specs.cc').write_text('\n'.join(source)+'\n')
        generated_inputs[str((out/'emutls_specs.cc').resolve())]=sha(out/'emutls_specs.cc')
        specs=out/'emutls_specs.o';owners.append(specs)
        run('actual-immutable-specs',[clang,*cxx,*common,'-MD','-MF',out/'emutls_specs.d','-c',out/'emutls_specs.cc','-o',specs])
        specobj=elfmod.Elf32(specs)
        assert not any(sec['flags']&4 and sec['size'] or sec['name'].startswith(('.init_array','.fini_array','.ctors','.dtors')) for sec in specobj.sections)
        state['compiler_descriptor_object_inventory']=controls
        state['compiler_template_object_inventory']=templates
        start=out/'start.o';owners.append(start)
        run('native-start',[clang,'--target=i686-unknown-none-elf','-c',app/'start.s','-o',start])
        reference=args.qualified_png.resolve()
        proven=json.loads((reference/'manifest.json').read_text())
        assert proven['native_build_pass'] and proven['host_asan_ubsan_pass']
        assert sha(reference/'libgtos_png_pixels.a')==proven['archive_sha256']
        assert subprocess.check_output([str(bindir/'llvm-ar'),'p',str(reference/'libgtos_png_pixels.a'),'memory.o'])==(reference/'memory.o').read_bytes()
        copied(reference/'memory.o',out/'memory.o');copied(reference/'memory.su',out/'memory.su');owners.append(out/'memory.o')
        state['qualified_memory_object_sha256']=sha(out/'memory.o')
        state['qualified_memory_manifest_sha256']=sha(reference/'manifest.json')
        elf=out/'v8-thread-id-probe.elf'
        run('native-link',[bindir/'ld.lld','-m','elf_i386','--gc-sections','--build-id=none',
            '-Map='+str(out/'native-link.map'),'-T',app/'linker.ld','-o',elf,*owners])
        symbols=run('native-symbols',[bindir/'llvm-nm','-n',elf])
        record=re.findall(r'^([0-9a-f]+) [A-Za-z] native_tls_record$',symbols,flags=re.M)
        assert len(record)==1 and int(record[0],16)==state['probe_record_address']
        state['probe_record_symbol']='native_tls_record'
        linked=elfmod.Elf32(elf)
        import struct
        byname={sym['name']:sym for sym in linked.symbols if sym['section']!=0}
        controls_start=byname['__gtos_emutls_controls_start']['value'];controls_end=byname['__gtos_emutls_controls_end']['value']
        templates_start=byname['__gtos_emutls_templates_start']['value'];templates_end=byname['__gtos_emutls_templates_end']['value']
        assert controls_start==0x40040000 and controls_end-controls_start==80 and templates_end-templates_start==128
        inventory=[]
        for row in controls:
            actual=dict(row);actual['control_va']=controls_start+row['control_offset']
            words=struct.unpack('<IIII',linked.virtual(actual['control_va'],16))
            initial=byname[row['template_symbol']]['value'] if row['template_symbol'] else 0
            assert words==(row['size'],row['alignment'],0,initial)
            actual['template_va']=initial
            if initial:
                assert templates_start<=initial and initial+row['size']<=templates_end
                assert initial%row['alignment']==0
                assert hashlib.sha256(linked.virtual(initial,row['size'])).hexdigest()==templates[row['template_symbol']]['sha256']
            spec_address=byname['gtos_emutls_specs']['value']+16*row['actual_slot']
            assert struct.unpack('<IIII',linked.virtual(spec_address,16))==(row['control_offset'],row['size'],row['alignment'],initial)
            inventory.append(actual)
        assert struct.unpack('<I',linked.virtual(byname['gtos_emutls_spec_count']['value'],4))[0]==5
        state['control_inventory']=inventory
        state['control_range']=dict(start=controls_start,end=controls_end,bytes=80,exact_slot_bytes=16)
        state['template_range']=dict(start=templates_start,end=templates_end,bytes=128)
        for obj in [elfmod.Elf32(p) for p in owners]+[linked]:
            assert not any(sec['flags']&0x400 or sec['name'].startswith(('.init_array','.fini_array','.ctors','.dtors','.tdata','.tbss')) for sec in obj.sections)
            assert not any(sym['name'].startswith(('_ZTH','_GLOBAL__sub_I','__cxa')) for sym in obj.symbols)
        state['actual_compiler_emulated_tls_admission_pass']=True
        state['original_thread_id_cc_unmodified']=True
        state['original_thread_id_h_unmodified']=True

        assert not run('native-undefined',[bindir/'llvm-nm','--undefined-only',elf]).strip()
        scalar_audit(elf,out/'native-link.map',owners)
        state['readelf']=run('native-readelf',['readelf','-h','-lW','-r',elf])
        stripped=out/'v8-thread-id-probe.stripped.elf'
        run('native-strip',[bindir/'llvm-objcopy','--strip-all',elf,stripped])
        inventory=json.loads(subprocess.check_output([sys.executable,str(repo/'tools/browser_artifact.py'),str(elf)],text=True))
        assert inventory['elf_bits']==32 and inventory['machine']=='i386' and inventory['elf_type']=='EXEC'
        assert not inventory['interpreters'] and all(s['permissions']!='RWX' for s in inventory['segments'])
        assert set(inventory['requirements'])<={'zero-filled memory beyond file-backed bytes'}
        (out/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
        pages=sum(((s['address']+s['memory_bytes']+4095)//4096)-(s['address']//4096) for s in inventory['segments'] if s['type']=='LOAD')
        assert pages+2<=256 and stripped.stat().st_size<=65536
        validation=repo/'apps/wuffs_gif_probe';host_io=out/'host-io.o';validator=out/'actual-elf32-validator'
        run('host-io',[args.host_cc,'-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-c',validation/'validation_host.c','-o',host_io])
        run('host-validator',[args.host_cxx,'-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(repo/'include'),repo/'src/process/elf32.cpp',validation/'validation_host.cpp',host_io,'-o',validator])
        assert 'REAL GTOS ELF32 VALIDATOR PASS' in run('actual-elf32-validation',[validator,stripped])
        stack=[]
        for path in out.glob('*.su'):
            for line in path.read_text().splitlines():
                fields=line.split('\t')
                if len(fields)>=3:stack.append(dict(function=fields[0],bytes=int(fields[1]),kind=fields[2]))
        state.update(stage='native-built-full-stack-and-guest-pending',native_build_pass=True,
            boot_file_admission_pass=True,actual_elf32_validation_pass=True,native_unresolved_symbols=0,
            elf=str(elf),elf_bytes=elf.stat().st_size,elf_sha256=sha(elf),stripped_elf=str(stripped),
            stripped_file_bytes=stripped.stat().st_size,stripped_sha256=sha(stripped),load_pages=pages,
            total_user_pages=pages+2,stack_usage=stack,maximum_function_stack_bytes=max(s['bytes'] for s in stack),
            consumer_object_sha256={p.name:sha(p) for p in owners})
    dependencies=set()
    for path in out.glob('*.d'):
        content=path.read_text().replace('\\\n',' ')
        dependencies.update(pathlib.Path(p) for p in content.split(':',1)[1].split())
    if (out/'emutls_specs.cc').exists(): dependencies.add(out/'emutls_specs.cc')
    dependencies.update(sdk_input_paths);dependencies.add(v8/'DEPS')
    state['pinned_sdk_input_sha256']={}
    pending=set(dependencies)
    for source_root,revision in ((gtest,GTEST_REV),(v8,V8_REV),(libcxx,LIBCXX_REV),(libc,LIBC_REV)):
        verified=pinned_inputs(pending,source_root,revision)
        state['pinned_sdk_input_sha256'].update(verified)
        pending.difference_update(pathlib.Path(p) for p in verified)
    closure={}
    for dependency in sorted(pending):
        path=dependency.resolve()
        if str(path) in generated_inputs:
            assert sha(path)==generated_inputs[str(path)],('Generated SDK input changed during compilation',str(path))
            origin='official generated SDK or explicitly derived V8 header'
        elif path.is_relative_to(repo):
            assert '.git' not in path.relative_to(repo).parts
            origin='GTOS repository source'
        elif path.is_relative_to(resource_include):
            origin='actual hash-bound Clang 24 builtin resource header'
        else:raise AssertionError(('Unowned compiler/header-generator input; unexpected sysroot',str(path)))
        closure[str(path)]=dict(origin=origin,sha256=sha(path))
    state['remaining_input_origins']=closure
    state['generated_sdk_and_derived_sha256']=generated_inputs
    for p,value in initial_source_sha256.items():assert sha(p)==value,('GTOS source changed during actual compilation',str(p))
    state['source_sha256']={str(p.relative_to(repo)):initial_source_sha256[p] for p in sorted(source_paths)}
    state['build_bound_source_before_after_identical']=True
    state['compiled_input_sha256']={str(p):sha(p) for p in sorted(dependencies)}
    for p in source_paths:copied(p,out/'source-snapshot'/p.relative_to(repo))
    state['derived_v8_sha256']={str(p.relative_to(derived)):sha(p) for p in derived.rglob('*') if p.is_file()}
    for name,value in state['upstream_thread_id_source_sha256'].items():assert sha(v8/name)==value
    state['source_provenance_pass']=True
except Exception as error:
    state.update(stage='failed',error=repr(error));raise
finally:save_state()
if state['native_build_pass'] and not args.link_only:
    # The admission proof first binds the unqualified manifest. Preserve that
    # exact input, then qualify a new immutable static-only manifest again.
    # No guest success is inferred from either machine-code proof.
    copied(out/'manifest.json',out/'manifest.unqualified.json')
    command=[sys.executable,str(app/'qualify_stack.py'),str(out),str(out/'stack-admission'),
        '--analysis-directory',str(args.stack_analysis_directory.resolve())]
    try:
        run('whole-stack-admission',command)
        proof=json.loads((out/'stack-admission/status.json').read_text())
        assert proof['qualification_pass'] and proof['full_stack_callchain_qualified'] and not proof['unknowns']
        assert proof['input_hashes'][str(out/'manifest.json')]==sha(out/'manifest.unqualified.json')
        state.update(stage='native-static-qualified-real-guest-pending',stack_call_chain_qualified=True,
            qualification_complete=True,qualification_scope='Static source closure, ELF32, integer-only machine code and complete linked call-stack admission; real guest acceptance remains pending',
            whole_stack_status=str(out/'whole-stack/status.json'),
            full_stack_bound_bytes=proof['full_chain_bound_bytes'],stack_headroom_bytes=proof['headroom_bytes'],
            qualified_retained_functions=proof['function_count'],
            actual_consumer_call_sites=proof['actual_consumer_call_sites'],actual_upstream_nested_calls=proof['actual_upstream_nested_calls'],
            actual_upstream_inline_operations=proof['actual_upstream_inline_operations'],
            actual_hidden_sret_signature_proof=proof['actual_hidden_sret_signature_proof'])
        command[3]=str(out/'whole-stack')
        state['whole_stack_command']=command
        save_state()
        with (out/'whole-stack.log').open('w') as log:
            result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=120)
        assert result.returncode==0,'Final immutable-manifest whole-stack proof failed'
        final=json.loads((out/'whole-stack/status.json').read_text())
        assert final['qualification_pass'] and final['full_stack_callchain_qualified'] and not final['unknowns']
        assert final['input_hashes'][str(out/'manifest.json')]==sha(out/'manifest.json')
        assert final['elf_sha256']==state['stripped_sha256'] and final['full_chain_bound_bytes']==state['full_stack_bound_bytes']
    except Exception as error:
        state.update(stage='failed',stack_call_chain_qualified=False,qualification_complete=False,error=repr(error))
        save_state()
        raise
print(json.dumps({k:state[k] for k in ('stage','native_build_pass','guest_pass','stack_call_chain_qualified')},indent=2))
