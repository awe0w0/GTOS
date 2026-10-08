#!/usr/bin/env python3
"""Build and boot actual GTOS self-query probes without reusing any artifact."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import traceback

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[1]
out = args.output.resolve()
if out.exists():
    raise SystemExit('Refusing to reuse artifact directory: ' + str(out))
out.mkdir(parents=True)
state = dict(all_required_checks_pass=False, scope='Actual production self-query ABI in CPL3 with whole-buffer sentinels and deferred Reap',
             native_process_info_guest_pass=False, native_isolate_pass=False, full_v8_backend=False,
             browser_guest_pass=False, video_guest_pass=False, html5_guest_pass=False,
             capacity_changed=False, linux_reference_workload_rerun=False, commands=[], guests=[])
def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def emit(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')
def run(argv, log, timeout=120, qemu=False):
    argv = [str(value) for value in argv]
    start = time.monotonic()
    invocation = dict(argv=argv, cwd=str(repo), executable_sha256=sha(argv[0]), log=str(log))
    with log.open('wb') as stream:
        try:
            code = subprocess.run(argv, cwd=repo, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = 124
    invocation.update(exit_code=code, elapsed_seconds=time.monotonic() - start, log_sha256=sha(log))
    state['commands'].append(invocation)
    emit(out / 'commands.json', state['commands'])
    if code != (33 if qemu else 0):
        detail = log.read_text(errors='replace')[-6000:]
        if qemu and (log.parent / 'guest.log').is_file():
            detail += '\n' + (log.parent / 'guest.log').read_text(errors='replace')[-6000:]
        raise RuntimeError('Command failed (' + str(code) + '): ' + str(log) + '\n' + detail)
    return code
def tool(name, environment=None):
    value = os.environ.get(environment, '') if environment else ''
    path = shutil.which(value or name)
    if not path:
        raise RuntimeError('Required tool unavailable: ' + name)
    return pathlib.Path(path).absolute()
def numbers(line):
    return {name: int(value, 16) for name, value in re.findall(r'(\w+)=([0-9A-F]+)', line)}
def metadata(words, identity):
    assert words == [1, identity, identity, 0xbfffd000, 0xbffff000,
                     0x40000000, 0xc0000000, 4096, 256, 32, 1, 1]
def observations(log):
    calls = {}
    records = []
    cases = []
    reaps = []
    survivors = []
    final = None
    baseline = None
    for line in log.splitlines():
        if line.startswith('INFO CALL '):
            values = numbers(line)
            assert values['free_before'] == values['free_after'] and values['preserved'] == 1
            assert values['status'] in (0, (-22) & 0xffffffff, (-14) & 0xffffffff, (-38) & 0xffffffff)
            calls.setdefault(values['id'], []).append(values)
        elif line.startswith('INFO BASELINE '):
            baseline = numbers(line)
            assert baseline['cpu_peer'] and baseline['info_peer'] != baseline['cpu_peer']
            assert baseline['kernel_cr3'] and baseline['initial'] > baseline['survivor']
        elif line.startswith('INFO RECORD RAW words='):
            record = [int(value, 16) for value in line.split('words=', 1)[1].split()]
            assert len(record) == 40
            records.append(record)
        elif line.startswith('INFO CASE '):
            values = numbers(line)
            assert records and baseline
            record = records[-1]
            identity = values['id']
            assert identity not in (baseline['cpu_peer'], baseline['info_peer'])
            assert record[0] == 1 and record[1] == values['mode'] and not record[4]
            assert record[5] == values['errors'] == 39 and record[6] == 14
            assert record[7] == 50 and record[35] == 50 * 8192 and record[37] == values['calls'] == 53
            assert record[38:] == [0, 0] and record[36] == 0
            metadata(record[8:20], identity)
            metadata(record[20:32], identity)
            assert record[32] == 0x80000000 and record[33] and record[34] == 2
            assert values['cs'] == 0x23 and values['cr3'] != baseline['kernel_cr3']
            assert values['static'] > 0 and values['dynamic'] == 3
            query = calls[identity]
            assert len(query) == 53 and sum(value['status'] != 0 for value in query) == 39
            if values['mode'] == 1:
                assert values['exit'] == 0x8000000e and values['pf'] == 6 and values['cr2'] == 0xbfffcffc and record[2] == 1
            elif values['mode'] == 2:
                assert values['exit'] == 73 and not values['pf'] and not values['cr2'] and record[2] == 1
            else:
                assert values['mode'] == 0 and not values['exit'] and not values['pf'] and not values['cr2'] and record[2] == 2
            values['record_words'] = record
            cases.append(values)
        elif line.startswith('INFO REAP '):
            values = numbers(line)
            assert baseline and values['free'] == values['expected'] == baseline['survivor']
            reaps.append(values)
        elif line.startswith('INFO SURVIVORS '):
            values = numbers(line)
            assert values['cpu_syscalls'] == values['cpu_yields'] == 0 and values['progress'] == 1
            survivors.append(values)
        elif line.startswith('INFO FINAL '):
            final = numbers(line)
    assert '\nNATIVE PROCESS INFO SMOKE PASS\n' in log and 'FAILED' not in log
    assert [(case['round'], case['mode']) for case in cases] == [(round, mode) for round in range(2) for mode in range(3)]
    assert len(records) == len(reaps) == len(survivors) == 6 and len({case['id'] for case in cases}) == 6
    assert len({case['record_words'][33] for case in cases}) == 6
    assert final and baseline and final['free'] == final['expected'] == baseline['initial']
    assert final['admitted'] == 8 and final['cases'] == 6 and final['preserved'] == sum(map(len, calls.values()))
    assert set(calls) == {baseline['info_peer'], *(case['id'] for case in cases)}
    assert all(case['record_words'][8:20] == case['record_words'][20:32] for case in cases)
    assert [(value['round'], value['mode']) for value in survivors] == [(case['round'], case['mode']) for case in cases]
    return dict(baseline=baseline, cases=cases, query_count=sum(map(len, calls.values())),
                complete_reap_checks=7, independent_caller_checks=True,
                non_result_registers_preserved=True, full_output_sentinels=True,
                actual_query_has_no_resource_or_clock_side_effect=True, final=final)
def stack_proof(directory, disassembly):
    frames = []
    for path in sorted(directory.glob('*.su')):
        for line in path.read_text().splitlines():
            function, size, kind = line.rsplit('\t', 2)
            assert kind in ('static', 'dynamic,bounded')
            frames.append(dict(function=function, bytes=int(size), kind=kind))
    graph = {}
    current = None
    for line in disassembly.splitlines():
        header = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if header:
            current = header.group(1)
            graph.setdefault(current, set())
        instruction = re.match(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+([a-z][a-z0-9]*)\s*(.*)', line)
        if not instruction or instruction.group(1) not in ('call', 'calll', 'jmp', 'jmpl'):
            continue
        operand = instruction.group(2)
        assert '*' not in operand, 'Unqualified indirect transfer: ' + line
        target = re.search(r'<([^>]+)>', operand)
        assert target and current, 'Unknown direct transfer: ' + line
        target = target.group(1).split('+0x', 1)[0]
        if target != current or instruction.group(1).startswith('call'):
            graph[current].add(target)
    active = set()
    done = set()
    def visit(node):
        assert node in graph, 'Unknown direct target: ' + node
        assert node not in active, 'Recursive call/tail graph: ' + node
        if node in done:
            return
        active.add(node)
        for target in graph[node]:
            visit(target)
        active.remove(node)
        done.add(node)
    for node in graph:
        visit(node)
    bound = sum(frame['bytes'] + 32 for frame in frames) + 64
    assert frames and bound <= 8176
    return dict(method='Conservative sum of all bounded compiler frames +32/frame +64 entry; actual direct-call/tail graph acyclic',
                bound=bound, limit=8176, frames=frames,
                direct_graph={node: sorted(edges) for node, edges in graph.items()},
                no_indirect_transfer=True, no_recursive_call_chain=True)

try:
    assert sys.version_info >= (3, 9)
    state['python'] = dict(executable=sys.executable, version=sys.version)
    state['environment'] = {name: os.environ[name] for name in sorted(os.environ)
                            if name in ('PATH', 'CXX', 'LD_LIBRARY_PATH', 'TMPDIR', 'GTOS_QEMU_SYSTEM_I386',
                                        'GTOS_QEMU_DATA_DIR', 'GTOS_GRUB_MKRESCUE', 'GTOS_GRUB_MODULES_DIR')}
    cxx = tool('g++', 'CXX')
    qemu = tool('qemu-system-i386', 'GTOS_QEMU_SYSTEM_I386')
    grub = tool('grub-mkrescue', 'GTOS_GRUB_MKRESCUE')
    tools = {name: tool(name) for name in ('as', 'ld', 'nm', 'objcopy', 'readelf', 'objdump')}
    state['tools'] = {name: dict(path=str(path), sha256=sha(path))
                      for name, path in dict(tools, cxx=cxx, qemu=qemu, grub=grub).items()}
    for name, path in dict(cxx=cxx, qemu=qemu, grub=grub).items():
        run([path, '--version'], out / (name + '-version.log'))
    run([cxx, '-dumpfullversion'], out / 'cxx-full-version.log')
    compiler_version = (out / 'cxx-full-version.log').read_text().strip()
    assert re.fullmatch(r'\d+(?:\.\d+){1,2}', compiler_version) and int(compiler_version.split('.')[0]) >= 13
    state['actual_compiler_version'] = compiler_version
    gitref = repo / '.git'
    if gitref.is_file():
        git = gitref.read_text().strip()
        assert git.startswith('gitdir: ')
        gitdir = (repo / git[8:]).resolve()
    else:
        gitdir = gitref
    head = (gitdir / 'HEAD').read_text().strip()
    if head.startswith('ref: '):
        common = (gitdir / (gitdir / 'commondir').read_text().strip()).resolve() if (gitdir / 'commondir').is_file() else gitdir
        ref = head[5:]
        if (common / ref).is_file():
            head = (common / ref).read_text().strip()
        else:
            packed = (common / 'packed-refs').read_text().splitlines()
            matches = [line.split()[0] for line in packed if line.endswith(' ' + ref)]
            assert len(matches) == 1
            head = matches[0]
    state['source_base'] = head
    assert re.fullmatch('[0-9a-f]{40}', head)
    sources = ['src/gdt.cpp', 'src/multitasking.cpp', 'src/syscalls.cpp',
               'src/hardwarecommunication/interrupts.cpp', 'src/hardwarecommunication/port.cpp',
               'src/process/native_runtime.cpp', 'src/process/resources.cpp', 'src/process/native_surface.cpp',
               'src/process/native_fp.cpp', 'src/process/elf32.cpp', 'src/memory/process_address_space.cpp',
               'src/memory/paging.cpp', 'src/memory/physical.cpp', 'src/memory/bootstrap.cpp',
               'tests/native_process_info_smoke.cpp']
    assembly = ['tests/native_process_loader.s', 'src/process/native_fp.s',
                'src/hardwarecommunication/interruptstubs.s']
    inputs = {path for path in (repo / 'include').rglob('*') if path.is_file()}
    inputs.update(repo / path for path in sources + assembly + [
        'src/process/resources_png.inc', 'tests/native_process_smoke.cpp', 'tests/native_process_probe_expectations.h',
        'tests/native_process_smoke.ld', 'tests/native_process_info_smoke.sh', 'tools/verify-native-process-info.py',
        'tools/kernel-cxxflags', 'tools/audit-kernel-instructions.py'])
    inputs.update(path for path in (repo / 'apps/native_process_info_probe').rglob('*') if path.is_file())
    state['source_inputs'] = {str(path): sha(path) for path in sorted(inputs)}
    emit(out / 'source-inputs.json', state['source_inputs'])
    snapshot = out / 'source-snapshot'
    for path in sorted(inputs):
        destination = snapshot / path.relative_to(repo)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
    common = ['-m32', '-std=c++11', '-ffreestanding', '-nostdlib', '-nostdinc', '-fno-builtin',
              '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-pie', '-fno-threadsafe-statics',
              '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections',
              '-fstack-usage', '-Iinclude', '-D__GTOS__=1', '-U__linux__', '-U__unix__', '-Ulinux', '-Uunix',
              '-Wall', '-Wextra', '-Werror'] + (repo / 'tools/kernel-cxxflags').read_text().split()
    for optimization in (0, 2):
        level = out / ('O' + str(optimization))
        level.mkdir()
        elfs = []
        for mode in range(4):
            directory = level / ('mode' + str(mode))
            directory.mkdir()
            for source in ('main.cpp', 'memory.cc'):
                obj = directory / (source.split('.')[0] + '.o')
                run([cxx, *common, '-O' + str(optimization), '-DGTOS_PROCESS_INFO_PROBE_MODE=' + str(mode),
                     '-MD', '-MF', obj.with_suffix('.d'), '-c', 'apps/native_process_info_probe/' + source, '-o', obj],
                    directory / (source + '.log'))
            run([tools['as'], '--32', 'apps/native_process_info_probe/start.s', '-o', directory / 'start.o'], directory / 'assembly.log')
            run([tools['ld'], '-melf_i386', '--gc-sections', '-T', 'apps/native_process_info_probe/linker.ld',
                 '-Map', directory / 'probe.map', '-o', directory / 'probe.elf',
                 directory / 'start.o', directory / 'main.o', directory / 'memory.o'], directory / 'link.log')
            run([tools['nm'], '-u', directory / 'probe.elf'], directory / 'undefined.txt')
            assert not (directory / 'undefined.txt').read_text().strip()
            run([tools['nm'], '-n', directory / 'probe.elf'], directory / 'symbols.txt')
            assert re.search(r'^40020000 [Dd] native_process_info_record$', (directory / 'symbols.txt').read_text(), re.M)
            run([tools['readelf'], '-h', '-l', directory / 'probe.elf'], directory / 'readelf.txt')
            run([tools['objdump'], '-d', directory / 'probe.elf'], directory / 'disassembly.txt')
            emit(directory / 'stack-bound.json', stack_proof(directory, (directory / 'disassembly.txt').read_text()))
            run([pathlib.Path(sys.executable), 'tools/audit-kernel-instructions.py', '--map', directory / 'probe.map',
                 directory / 'probe.elf'], directory / 'scalar-audit.txt')
            elf = directory / 'probe.stripped.elf'
            run([tools['objcopy'], '--strip-all', directory / 'probe.elf', elf], directory / 'strip.log')
            assert elf.stat().st_size <= 65536
            elfs.append(elf)
        kernel = level / 'kernel'
        kernel.mkdir()
        for source in sources:
            obj = kernel / (pathlib.Path(source).stem + '.o')
            run([cxx, *common, '-O' + str(optimization), '-Wno-write-strings', '-MD', '-MF', obj.with_suffix('.d'),
                 '-c', source, '-o', obj], kernel / (pathlib.Path(source).stem + '.log'))
        run([cxx, *common, '-O' + str(optimization), '-c', 'apps/native_process_info_probe/memory.cc',
             '-o', kernel / 'byte_helpers.o'], kernel / 'byte-helpers.log')
        for source, name in zip(assembly, ('loader.o', 'native_fp.asm.o', 'stubs.o')):
            run([tools['as'], '--32', source, '-o', kernel / name], kernel / (name + '.log'))
        run([tools['ld'], '-melf_i386', '--gc-sections', '-T', 'tests/native_process_smoke.ld',
             '-Map', level / 'kernel.map', '-o', level / 'kernel.bin', *sorted(kernel.glob('*.o'))], level / 'link.log')
        run([tools['nm'], '-u', level / 'kernel.bin'], level / 'undefined.txt')
        assert not (level / 'undefined.txt').read_text().strip()
        run([tools['nm'], '-n', level / 'kernel.bin'], level / 'symbols.txt')
        assert 'NativeProcessInfoUnusedBaseline' not in (level / 'symbols.txt').read_text()
        run([pathlib.Path(sys.executable), 'tools/audit-kernel-instructions.py', '--map', level / 'kernel.map',
             '--source-root', repo, level / 'kernel.bin'], level / 'kernel-integer-audit.txt')
        iso = level / 'iso'
        (iso / 'boot/grub').mkdir(parents=True)
        shutil.copyfile(level / 'kernel.bin', iso / 'boot/native.bin')
        copies = []
        for mode, elf in enumerate(elfs):
            destination = iso / 'boot' / ('info-' + str(mode) + '.elf')
            shutil.copyfile(elf, destination)
            assert sha(elf) == sha(destination)
            copies.append(dict(mode=mode, source=str(elf), copy=str(destination), sha256=sha(elf)))
        emit(level / 'input-copies.json', copies)
        (iso / 'boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native self query" {\n multiboot /boot/native.bin\n' +
            ''.join(' module /boot/info-' + str(mode) + '.elf\n' for mode in range(4)) + ' boot\n}\n')
        run([grub, '--output=' + str(level / 'info.iso'), iso], level / 'grub.log')
        for memory, cpus in (('32M', 1), ('64M', 4)):
            case = level / (memory + '-smp' + str(cpus))
            case.mkdir()
            argv = [qemu]
            if os.environ.get('GTOS_QEMU_DATA_DIR'):
                argv += ['-L', os.environ['GTOS_QEMU_DATA_DIR']]
            argv += ['-machine', 'pc', '-accel', 'tcg', '-m', memory, '-smp', str(cpus),
                     '-cdrom', level / 'info.iso', '-boot', 'd', '-nic', 'none', '-display', 'none',
                     '-monitor', 'none', '-serial', 'none', '-debugcon', 'file:' + str(case / 'guest.log'),
                     '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
            emit(case / 'qemu-invocation.json', dict(argv=[str(value) for value in argv], cwd=str(repo),
                 environment=state['environment'], qemu_sha256=sha(qemu), iso_sha256=sha(level / 'info.iso'),
                 kernel_sha256=sha(level / 'kernel.bin'), input_copies=copies))
            run(argv, case / 'qemu.log', timeout=180, qemu=True)
            actual = observations((case / 'guest.log').read_text(errors='strict'))
            actual.update(optimization=optimization, memory=memory, cpus=cpus, guest_log_sha256=sha(case / 'guest.log'))
            emit(case / 'observations.json', actual)
            # The acceptance parser must reject a missing baseline or altered real payload.
            log = (case / 'guest.log').read_text()
            controls = [log.replace('NATIVE PROCESS INFO SMOKE PASS', 'NATIVE PROCESS INFO SMOKE FAIL', 1),
                        '\n'.join(line for line in log.splitlines() if not line.startswith('INFO FINAL ')),
                        log.replace('dynamic=00000003', 'dynamic=00000004', 1)]
            first_record = next(line for line in log.splitlines() if line.startswith('INFO RECORD RAW words='))
            words = first_record.split('words=', 1)[1].split()
            for index in (4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 29, 32, 33, 34, 35, 36, 37, 38, 39):
                changed = list(words)
                changed[index] = '%08X' % (int(changed[index], 16) ^ 1)
                controls.append(log.replace(first_record, 'INFO RECORD RAW words= ' + ' '.join(changed), 1))
            for control in controls:
                try:
                    observations(control)
                except (AssertionError, KeyError, StopIteration):
                    pass
                else:
                    raise AssertionError('Acceptance parser accepted a corrupted real log')
            actual['rejected_parser_controls'] = len(controls)
            emit(case / 'observations.json', actual)
            state['guests'].append(dict(optimization=optimization, memory=memory, cpus=cpus,
                                       log=str(case / 'guest.log'), observations=str(case / 'observations.json'),
                                       observation_sha256=sha(case / 'observations.json')))
            print('PASS process info O' + str(optimization) + ' ' + memory + '/smp' + str(cpus) + ' cases=6', flush=True)
    assert all(sha(path) == before for path, before in state['source_inputs'].items()), 'Source changed during evidence'
    state.update(all_required_checks_pass=True, native_process_info_guest_pass=True,
                 source_before_after_identical=True, actual_guest_cases=24, exact_reap_checks=28,
                 kernel_and_probe_compiler='GCC' + compiler_version, optimization_scope='actual kernel/probes O0 and O2',
                 no_native_v8_execution_claim=True, production_image_changed=False)
except BaseException as error:
    state['failure'] = str(error)
    (out / 'exception.log').write_text(traceback.format_exc())
finally:
    state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    emit(out / 'status.json', state)
    print(json.dumps({key: value for key, value in state.items() if key not in ('commands', 'source_inputs', 'environment')}, indent=2))
sys.exit(0 if state['all_required_checks_pass'] else 1)
