#!/usr/bin/env python3
import datetime
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import struct
import sys
import time
import traceback

import argparse

parser = argparse.ArgumentParser(description='Qualify the pinned native runtime_error termination in an offline network namespace')
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--integration-build', type=pathlib.Path, required=True)
parser.add_argument('--integer-builtins', type=pathlib.Path, required=True)
parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
parser.add_argument('--qemu', type=pathlib.Path, required=True)
parser.add_argument('--firmware', type=pathlib.Path, required=True)
parser.add_argument('--workspace', type=pathlib.Path, required=True)
parser.add_argument('--runtime-root', type=pathlib.Path, required=True)
parser.add_argument('--tool-state', type=pathlib.Path, required=True)
parser.add_argument('--link-evidence', type=pathlib.Path, required=True)
args = parser.parse_args()
workspace = args.workspace.resolve()
interfaces = [x.split(':',1)[0].strip() for x in pathlib.Path('/proc/self/net/dev').read_text().splitlines()[2:] if ':' in x]
assert interfaces == ['lo'] and not pathlib.Path('/proc/self/net/route').read_text().splitlines()[1:]
assert set(os.environ) <= {'PATH','LANG','TZ','LC_CTYPE'}
repo = pathlib.Path(__file__).resolve().parents[1]
source = repo/'apps/native_runtime_error/probe'
out = args.output.resolve()
assert not out.exists(), 'Fresh output required; preserve prior failures and caches'
assert workspace in out.parents
runtime = args.runtime_root.resolve()
assert workspace in runtime.parents
assert args.qemu.resolve() == runtime/'root/usr/bin/qemu-system-i386'
assert args.firmware.resolve() == runtime/'root/usr/share/qemu'
out.mkdir(parents=True)
(out/'home').mkdir(); (out/'tmp').mkdir()
environment = {'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC',
               'HOME':str(out/'home'),'TMPDIR':str(out/'tmp'),
               'LD_LIBRARY_PATH':str(runtime/'root/lib/x86_64-linux-gnu')+':'+str(runtime/'root/usr/lib/x86_64-linux-gnu'),
               'QEMU_MODULE_DIR':str(runtime/'root/usr/lib/x86_64-linux-gnu/qemu')}
full = args.integration_build.resolve()
state = {'scope': 'Actual pinned no-exception runtime_error helper with unchanged native diagnostic callback; long/empty/actual locale messages, invalid pointer fault and exact reap',
         'all_required_checks_pass': False, 'native_runtime_error_guest_pass': False,
         'native_multithread_waits_pass': False, 'native_isolate_pass': False,
         'javascript_guest_pass': False, 'browser_guest_pass': False,
         'production_image_changed': False, 'capacity_changed': False,
         'commands': [], 'inputs': {}, 'guests': []}
state['network_disabled'] = True
state['inherited_environment_discarded'] = True
state['execution_guards'] = 'Fixed reviewed tools; vector arguments; no inline Python, log-derived actions or shell command strings'


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def emit(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def bind(path):
    path = pathlib.Path(path).resolve()
    actual = sha(path)
    if str(path) in state['inputs']:
        assert state['inputs'][str(path)] == actual, path
    else:
        state['inputs'][str(path)] = actual
        try:
            relative = path.relative_to(repo)
        except ValueError:
            relative = pathlib.Path('external') / hashlib.sha256(str(path).encode()).hexdigest() / path.name
        destination = out / 'source-snapshot' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)


def run(argv, log, cwd=repo, expected=0, timeout=120):
    argv = [str(item) for item in argv]
    program = pathlib.Path(argv[0])
    assert program.is_absolute() and program in allowed_programs, program
    assert workspace in log.resolve().parents
    assert cwd in (repo,full/'build')
    assert not any(x in ('-c','--command') for x in argv[1:]) if program == pathlib.Path(sys.executable) else True
    assert all('$(' not in x and '`' not in x and x not in ('&&',';','|') for x in argv)
    if program == pathlib.Path(sys.executable): argv[1:1] = ['-I','-B']
    start = time.monotonic()
    with log.open('wb') as stream:
        try:
            code = subprocess.run(argv, cwd=cwd, env=environment, shell=False, close_fds=True,
                                  stdout=stream, stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = 124
    state['commands'].append({'argv': argv, 'cwd': str(cwd), 'exit_code': code,
                              'seconds': time.monotonic() - start, 'log_sha256': sha(log)})
    emit(out / 'commands.json', state['commands'])
    assert code == expected, str(log) + '\n' + log.read_text(errors='replace')[-3500:]
    return log.read_text(errors='replace')


def bind_dependencies(path, cwd):
    words = path.read_text().replace('\\\n', ' ').split(':', 1)[1].split()
    for word in words:
        bind(cwd / word)


def numbers(line):
    return {key: int(value, 16) for key, value in re.findall(r'(\w+)=([0-9A-F]+)', line)}


def observe(text):
    clean = text.replace('UNHANDLED INTERRUPT 0x00000027', '')
    assert 'UNHANDLED INTERRUPT' not in clean and 'PANIC' not in clean and 'FAILED' not in clean
    assert '\nNATIVE DIAG SMOKE PASS\n' in clean
    payload=''.join(chr(65+i%26) for i in range(900))
    prefix='runtime_error was thrown in -fno-exceptions mode with message "'
    for message in (payload,'','locale not supported'):
        assert clean.count(prefix+message+'"GTOS NATIVE HEAP FAIL V1\n')==1
    assert clean.count('GTOS NATIVE HEAP FAIL V1\n')==3
    normal='DIAG MESSAGE mode=0 value=-7 hex=00001234 ['+payload+'] END\n'
    assert clean.count(normal)==2
    cases=[x for x in clean.splitlines() if x.startswith('DIAG CASE ')]
    reaps=[numbers(x) for x in clean.splitlines() if x.startswith('DIAG REAP ')]
    final=[numbers(x) for x in clean.splitlines() if x.startswith('DIAG FINAL ')]
    assert len(cases)==len(reaps)==6 and len(final)==1
    records=[]
    for mode,line in zip((0,1,2,3,4,0),cases):
        head=numbers(line);words=[int(x,16) for x in line.split(' words= ',1)[1].split()]
        assert len(words)==16 and words[:2]==[1,mode] and words[4]==0 and words[12:]==[0,0,0,0]
        assert head['mode']==mode and head['cs']==0x23 and head['cr3'] and head['cost']==ExpectedFrameCost
        assert words[8]==0 and words[9] and words[10:12]==[0x80000000,4096]
        if mode==0:
            assert words[2:4]==[2,8] and words[5:8]==[len(normal),900,len(normal)]
            assert head['exit']==head['vector']==head['fault']==0 and head['written']==len(normal)
        elif mode==4:
            assert words[2:8]==[1,5,0,0,0,0] and head['exit']==0x8000000e and head['vector']==14
            assert head['fault']==0xbfffcff0 and head['written']==0
        else:
            size=900 if mode==1 else 20 if mode==3 else 0
            assert words[2:8]==[1,5,0,0,size,len(prefix)+1+size]
            assert head['exit']==ExpectedAbortExit and head['vector']==head['fault']==0
            assert head['written']==len(prefix)+1+size+25
        records.append({'header':head,'words':words})
    assert len({x['header']['id'] for x in records})==6
    assert all(x['free']==x['expected']==reaps[0]['expected'] for x in reaps)
    assert final[0]['free']==final[0]['expected']>reaps[0]['free']
    return {'cases':records,'reaps':reaps,'final':final[0],'exact_reap_checks':7,
            'pinned_runtime_error_diagnostic_termination_pass':True,'user_fault_and_exact_recovery_pass':True}


try:
    bind(__file__)
    for path in source.iterdir():
        bind(path)
    bind(full/'status.json')
    bind(full/'actual-compile-database.log')
    integration = json.loads((full/'status.json').read_text())
    assert integration['gn_pass'] and integration['bound_inputs_unchanged'] and not integration.get('live_command')
    assert all(c['exit_code']==0 for c in integration['commands']), 'Require actual successful GN preparation'
    database = json.loads((full/'actual-compile-database.log').read_text())
    final = shlex.split(integration['link_command'])
    published = repo/'apps/native_v8_integration/overlay/tools/gtos_isolate_diagnostic/BUILD.gn'
    bind(published)
    assert sha(full/'source/tools/gtos_isolate_diagnostic/BUILD.gn') == sha(published)
    link_path = args.link_evidence.resolve()
    assert sha(link_path) == '2329a3ad9688d481404aca124f35f78c2b8829137640d6486cdce1f186d5ed61'
    link = json.loads(link_path.read_text())
    bind(link_path)
    assert link['network_disabled'] and link['runtime_error_resolved_in_actual_full_link']
    assert link['original_gn_link_command'] == integration['link_command']
    module = repo/'apps/native_v8_integration'
    assert sha(module/'source-lock.json') == 'd0640c8efe46bc9693d66c745d5ab5a5e1647fd878ef6acf6ede5abfdd2866eb'
    assert sha(module/'payload-lock.json') == 'dadbba06c599b919532fbc95ec095dd43d6ba2d5627d431b67d43f44bce55c97'
    bind(module/'source-lock.json'); bind(module/'payload-lock.json')
    selected = [r for r in database if r['output'].endswith('.o') and ('/native_runtime_error_libcxx.' in r['output'] or '/native_diagnostic_libcxx.' in r['output'])]
    assert len(selected)==3
    diagnostic_objects = [full/'build'/r['output'] for r in selected]
    assert all(r['output'] in final for r in selected)
    names = [name for name in final if name.endswith('.o') and '/native_runtime.' in name]
    names += ['obj/third_party/llvm-libc/src/src/errno/native_c_clock.libc_errno.o',
              'obj/third_party/llvm-libc/src/src/string/native_string_libc.memcpy.o',
              'obj/third_party/llvm-libc/src/src/string/native_string_libc.memset.o',
              'obj/third_party/llvm-libc/src/src/__support/printf_core/native_printf_libc.float_impl.o',
              'obj/third_party/llvm-libc/src/src/__support/StringUtil/native_printf_libc.error_to_string.o']
    assert all(name in final for name in names)
    diagnostic_objects += [full/'build'/name for name in names]
    for path in diagnostic_objects:
        relative = path.relative_to(full/'build')
        candidates = [str(path.resolve())]
        candidates += [p for p in link['object_inputs_sha256'] if p.endswith('/build/'+str(relative))]
        assert any(link['object_inputs_sha256'].get(p)==sha(path) for p in candidates)
    scalar = args.integer_builtins.resolve()
    assert str(scalar) in final, 'Use the actual complete-target integer builtins'
    bind(scalar)
    for path in diagnostic_objects:
        bind(path)
        if path.with_suffix('.su').exists(): bind(path.with_suffix('.su'))
    state['executed_conversion_objects_sha256'] = {str(path):sha(path) for path in diagnostic_objects}
    branch_ref = repo/'.git/refs/heads/dev'
    if branch_ref.is_file(): state['base_commit'] = branch_ref.read_text().strip()
    cxx = pathlib.Path(shutil.which(args.cxx)).resolve()
    tools = {name:pathlib.Path('/usr/bin')/name for name in ('as','ld','nm','objcopy','readelf','objdump')}
    tools['grub-mkrescue'] = runtime/'bin/grub-mkrescue'
    tools['qemu-system-i386'] = args.qemu.resolve()
    seed, = [row for row in database if row['output'].endswith('native_runtime_error_libcxx.runtime-error-no-exceptions.o')]
    compiler = pathlib.Path(shlex.split(seed['command'])[0]).resolve()
    allowed_programs = {cxx,*tools.values(),pathlib.Path(sys.executable),compiler}
    prior_path = args.tool_state.resolve()
    assert sha(prior_path) == '3ed18587f56f5f8622dd1e6214af29f4b2301a946fcf9bbb05cb3a4c80ac7162'
    prior = json.loads(prior_path.read_text())
    assert prior['all_required_checks_pass']
    for path in allowed_programs:
        if path == pathlib.Path(sys.executable):
            assert path.resolve() == pathlib.Path('/usr/bin/python3.8')
            assert sha(path) == '298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
        else:
            assert prior['inputs'][str(path.resolve())] == sha(path), 'Changed reviewed tool: '+str(path)
    for path in (cxx, *tools.values()):
        bind(path)
    firmware = args.firmware.resolve()
    for path in firmware.rglob('*'):
        if path.is_file(): bind(path)
    qemu = tools['qemu-system-i386']
    # Bind the already-qualified library files; do not execute ldd on a program.
    for name,digest in prior['inputs'].items():
        path = pathlib.Path(name)
        if '.so' in path.name:
            assert sha(path) == digest
            bind(path)

    run([cxx,'-dumpfullversion'],out/'gcc-version.log')
    assert (out/'gcc-version.log').read_text().strip() == '13.3.0'
    app = out / 'app'; app.mkdir()
    seed, = [row for row in database if row['output'].endswith('native_runtime_error_libcxx.runtime-error-no-exceptions.o')]
    command = shlex.split(seed['command'])
    command[command.index('-c') + 1] = str(source/'main.cc')
    command[command.index('-o') + 1] = str(app/'main.o')
    command[command.index('-MF') + 1] = str(app/'main.d')
    command = ['-MD' if item == '-MMD' else item for item in command]
    command += ['-D_LIBCPP_BUILDING_LIBRARY','-fno-builtin','-std=c++20',
                '-I'+str(source),'-I'+str(full/'source/third_party/llvm-libc/src'),
                '-I'+str(full/'source/tools/gtos_isolate_diagnostic/runtime')]
    bind(command[0])
    run(command, app/'compile.log', cwd=full/'build')
    bind_dependencies(app/'main.d', full/'build')
    common = ['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin',
              '-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-pie','-fno-threadsafe-statics',
              '-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections',
              '-fstack-usage','-Iinclude','-I'+str(repo),'-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix',
              '-Wall','-Wextra','-Werror','-O2'] + (repo/'tools/kernel-cxxflags').read_text().split()
    run([cxx,*common,'-c','apps/native_process_info_probe/memory.cc','-o',app/'memory.o'],app/'memory.log')
    run([tools['as'],'--32','apps/native_process_info_probe/start.s','-o',app/'start.o'],app/'start.log')
    run([tools['ld'],'-melf_i386','--gc-sections','-T',source/'linker.ld','-o',app/'probe.elf',
         app/'start.o',app/'main.o',*diagnostic_objects,scalar],app/'link.log')
    assert not run([tools['nm'],'-u',app/'probe.elf'],app/'undefined.log').strip()
    symbols = run([tools['nm'],'-n',app/'probe.elf'],app/'symbols.log')
    assert re.search(r'^40100000 [Dd] native_diag_record$',symbols,re.M)
    header = run([tools['readelf'],'-h','-l',app/'probe.elf'],app/'readelf.log')
    assert 'Intel 80386' in header and 'EXEC' in header
    run([tools['objdump'],'-d','-C',app/'probe.elf'],app/'disassembly.log')
    run([tools['objcopy'],'--strip-all',app/'probe.elf',app/'probe.stripped.elf'],app/'strip.log')
    assert (app/'probe.stripped.elf').stat().st_size <= 1048576
    elf = (app/'probe.stripped.elf').read_bytes()
    assert elf[:7] == b'\x7fELF\x01\x01\x01'
    phoff = struct.unpack_from('<I', elf, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', elf, 42)
    pages, tables = set(), set()
    for index in range(phcount):
        kind, _, address, _, _, size, _, _ = struct.unpack_from('<8I', elf, phoff + index * phsize)
        if kind == 1 and size:
            pages.update(range(address // 4096, (address + size - 1) // 4096 + 1))
            tables.update(range(address >> 22, ((address + size - 1) >> 22) + 1))
    runtime_header = (repo/'include/process/native_runtime.h').read_text()
    stack, = re.findall(r'UserStackBottom\s*=\s*(0x[0-9A-Fa-f]+)', runtime_header)
    tables.add(int(stack, 16) >> 22)
    ExpectedFrameCost = len(pages) + 2 + len(tables) + 1 + 2
    heap_header=full/'source/tools/gtos_isolate_diagnostic/runtime/heap.h'
    abort_source=full/'source/tools/gtos_isolate_diagnostic/runtime/abort.cc'
    for path in (heap_header,abort_source): bind(path)
    prefix, = re.findall(r'^#define GTOS_NATIVE_HEAP_EXIT_BASE (0x[0-9A-Fa-f]+)U$',heap_header.read_text(),re.M)
    reason, = re.findall(r'gtos_native_heap_panic\((0x[0-9A-Fa-f]+)U\)',abort_source.read_text())
    ExpectedAbortExit=int(prefix,16)|int(reason,16)
    state['expected_abort_exit']=ExpectedAbortExit
    kernel = out / 'kernel'; kernel.mkdir()
    sources = ['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp',
               'src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
               'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp',
               'src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/elf32.cpp',
               'src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp']
    for path in [repo/name for name in sources] + [source/'kernel.cpp',repo/'apps/native_process_info_probe/memory.cc']:
        bind(path)
        obj = kernel / (path.stem + '.o')
        run([cxx,*common,'-Wno-write-strings','-MD','-MF',obj.with_suffix('.d'),'-c',path,'-o',obj],kernel/(path.stem+'.log'))
        bind_dependencies(obj.with_suffix('.d'), repo)
    for name, output in [('tests/native_process_loader.s','loader.o'),('src/process/native_fp.s','native_fp_asm.o'),
                         ('src/hardwarecommunication/interruptstubs.s','stubs.o')]:
        bind(repo/name)
        run([tools['as'],'--32',name,'-o',kernel/output],kernel/(output+'.log'))
    for name in ('tools/kernel-cxxflags','tests/native_process_smoke.ld','tools/audit-kernel-instructions.py',
                 'apps/native_process_info_probe/start.s','apps/native_process_info_probe/memory.cc'):
        bind(repo/name)
    run([tools['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',out/'kernel.map',
         '-o',out/'kernel.bin',*sorted(kernel.glob('*.o'))],out/'kernel-link.log')
    assert not run([tools['nm'],'-u',out/'kernel.bin'],out/'kernel-undefined.log').strip()
    run([sys.executable,'tools/audit-kernel-instructions.py','--map',out/'kernel.map','--source-root',repo,
         out/'kernel.bin'],out/'kernel-integer-audit.log')
    iso = out/'iso'; (iso/'boot/grub').mkdir(parents=True)
    shutil.copyfile(out/'kernel.bin',iso/'boot/native.bin')
    shutil.copyfile(app/'probe.stripped.elf',iso/'boot/diag.elf')
    (iso/'boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native diagnostic" {\n multiboot /boot/native.bin\n module /boot/diag.elf\n boot\n}\n')
    run([tools['grub-mkrescue'],'--output='+str(out/'diag.iso'),iso],out/'grub.log')
    for memory, cpus in (('32M',1),('64M',4)):
        guest = out/(memory+'-smp'+str(cpus)); guest.mkdir()
        argv = [tools['qemu-system-i386'],'-L',str(firmware),'-machine','pc','-accel','tcg',
                '-cpu','max','-m',memory,'-smp',str(cpus),'-cdrom',out/'diag.iso','-boot','d','-nic','none',
                '-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest/'guest.log'),
                '-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot']
        emit(guest/'invocation.json',{'argv':[str(item) for item in argv],'qemu_sha256':sha(tools['qemu-system-i386']),
             'iso_sha256':sha(out/'diag.iso'),'kernel_sha256':sha(out/'kernel.bin'),
             'native_elf_sha256':sha(app/'probe.stripped.elf'),'executed_conversion_objects_sha256':state['executed_conversion_objects_sha256']})
        run(argv,guest/'qemu.log',expected=33,timeout=300)
        result = observe((guest/'guest.log').read_text())
        result.update(memory=memory,cpus=cpus,guest_log_sha256=sha(guest/'guest.log'))
        emit(guest/'observations.json',result)
        state['guests'].append(result)
        print('PASS actual pinned runtime_error CPL3 '+memory+'/smp'+str(cpus),flush=True)
    assert all(sha(path) == expected for path,expected in state['inputs'].items())
    state.update(all_required_checks_pass=True,native_runtime_error_guest_pass=True,
                 source_before_after_identical=True,exact_reap_checks=14,actual_victim_cases=12,pinned_runtime_error_guest_pass=True)
except BaseException as error:
    state['failure'] = str(error)
    (out/'exception.log').write_text(traceback.format_exc())
finally:
    state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    emit(out/'status.json',state)
    print(json.dumps({key:value for key,value in state.items() if key not in ('inputs','commands','guests','executed_conversion_objects_sha256')},indent=2),flush=True)
sys.exit(0 if state['all_required_checks_pass'] else 1)
