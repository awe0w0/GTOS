"""Independent exact-ELF native PNG surface consumer stack qualification.

Reuse the proven decoder target sets only after object-function identity checks.
Run fresh instruction/CFG/call-graph analysis for every linked machine function.
"""
import argparse, datetime, hashlib, importlib.util, json, os, pathlib, re, shutil, struct, subprocess, sys, traceback

sys.dont_write_bytecode = True

TREE = pathlib.Path(__file__).resolve().parents[1]
BASE = TREE.parent.parent
REFERENCE = BASE / 'artifacts/png-release-final-20261007T102400Z'
PROOF = BASE / 'artifacts/png-stack-gn-release-20261007T103153Z'
module = None


def local_path(value):
    """Preserve exact pinned inputs while resolving existing Windows/WSL paths."""
    name = str(value).replace('\\', '/')
    if os.name != 'nt' and re.match(r'^[A-Za-z]:/', name):
        name = '/mnt/' + name[0].lower() + name[2:]
    elif os.name == 'nt' and re.match(r'^/mnt/[a-z]/', name):
        name = name[5].upper() + ':' + name[6:]
    return pathlib.Path(name)


def input_record(path):
    path = pathlib.Path(path)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=module.sha(path))

def object_functions(objects):
    functions = {}
    for path in objects:
        obj = module.Elf32(path)
        for symbol in obj.symbols:
            if symbol['type'] == 2 and symbol['size']:
                functions[symbol['name']] = (obj, symbol)
    return functions

def payload(owner):
    obj, symbol = owner
    data = bytearray(obj.bytes(obj.sections[symbol['section']])[symbol['value']:symbol['value'] + symbol['size']])
    relocs = []
    for reloc in obj.relocations:
        if reloc['section'] != symbol['section'] or not symbol['value'] <= reloc['offset'] < symbol['value'] + symbol['size']:
            continue
        offset = reloc['offset'] - symbol['value']
        target = reloc['symbol']
        label = target['name'] or obj.sections[target['section']]['name']
        relocs.append((offset, reloc['type'], label, target['value'], bytes(data[offset:offset + 4]).hex()))
        data[offset:offset + 4] = b'\0' * 4
    return bytes(data), relocs

def reports(paths):
    result = {}
    for path in paths:
        for line in path.read_text().splitlines():
            fields = line.split('\t')
            if len(fields) == 3:
                result[fields[0].rsplit(':', 1)[-1]] = dict(bytes=int(fields[1]), kind=fields[2], source=fields[0], report=str(path))
    return result

def linked_object_correspondence(analysis, symbol_elf):
    symbols = {symbol['name']:symbol for symbol in symbol_elf.symbols if symbol['name'] and symbol['section']}
    rows = []
    for name, fn in analysis.functions.items():
        assert name in analysis.owners, ('missing native object owner', name)
        obj, symbol = analysis.owners[name]
        assert fn['size'] == symbol['size'], ('linked/object function size mismatch', name)
        actual = bytearray(analysis.elf.virtual(fn['value'], fn['size']))
        reference = bytearray(obj.bytes(obj.sections[symbol['section']])[symbol['value']:symbol['value'] + symbol['size']])
        checked = []
        section_relocs = []
        for reloc in obj.relocations:
            if reloc['section'] != symbol['section'] or not symbol['value'] <= reloc['offset'] < symbol['value'] + symbol['size']:
                continue
            offset = reloc['offset'] - symbol['value']
            assert reloc['type'] in (1, 2), ('unsupported native function relocation', name, reloc['type'])
            target = reloc['symbol']
            target_va = None
            if target['name'] in symbols:
                target_va = symbols[target['name']]['value']
            elif target['section'] and target['section'] < len(obj.sections):
                target_section = obj.sections[target['section']]['name']
                if target_section.startswith('.text.') and target_section[6:] in analysis.functions:
                    target_name = target_section[6:]
                    target_obj, target_sym = analysis.owners[target_name]
                    assert target_obj.path == obj.path
                    target_va = analysis.functions[target_name]['value'] - target_sym['value'] + target['value']
            if target_va is not None:
                addend = struct.unpack_from('<I', reference, offset)[0]
                expected = target_va + addend
                if reloc['type'] == 2:
                    expected -= fn['value'] + offset
                expected &= 0xffffffff
                linked = struct.unpack_from('<I', actual, offset)[0]
                assert linked == expected, ('linked function relocation mismatch', name, offset, target['name'], hex(linked), hex(expected))
                checked.append(dict(offset=offset, type=reloc['type'], target=target['name'], linked_value=hex(linked)))
            else:
                assert target['type'] != 2, ('unresolved function relocation', name, target)
                section_relocs.append(dict(offset=offset, type=reloc['type'], target_section=obj.sections[target['section']]['name']))
            actual[offset:offset + 4] = b'\0' * 4
            reference[offset:offset + 4] = b'\0' * 4
        assert actual == reference, ('actual linked machine bytes differ from owner object', name)
        rows.append(dict(function=name, linked_function_bytes_sha256=hashlib.sha256(analysis.elf.virtual(fn['value'], fn['size'])).hexdigest(),
            normalized_function_bytes_sha256=hashlib.sha256(actual).hexdigest(), named_or_function_relocations_verified=checked,
            anonymous_data_section_relocations=section_relocs))
    return rows

def readonly_data_spans(analysis, objects, map_path):
    inputs = {path.name:module.Elf32(path) for path in objects}
    rows = []
    pattern = re.compile(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(.+):\((\.rodata[^)]*)\)$')
    for line in map_path.read_text().splitlines():
        match = pattern.match(line)
        if not match:
            continue
        address, load, size, alignment, source, section_name = match.groups()
        address, size, alignment = int(address, 16), int(size, 16), int(alignment)
        actual = bytearray(analysis.elf.virtual(address, size))
        checked = []
        if source == '<internal>':
            candidates = [(obj, section) for obj in inputs.values() for section in obj.sections if section['name'] == section_name and section['flags'] & 16]
            assert candidates, ('unowned merged readonly section', section_name)
            entry_sizes = {section['entsize'] for obj, section in candidates}
            assert len(entry_sizes) == 1 and 0 not in entry_sizes
            entry_size = entry_sizes.pop()
            assert size % entry_size == 0
            original_entries = {obj.bytes(section)[offset:offset + entry_size] for obj, section in candidates for offset in range(0, section['size'], entry_size)}
            linked_entries = {bytes(actual[offset:offset + entry_size]) for offset in range(0, size, entry_size)}
            assert original_entries == linked_entries, ('merged readonly constant bytes differ', section_name)
            assert all(not section['flags'] & 4 for obj, section in candidates)
            identity = dict(kind='linker merged readonly constants', entry_bytes=entry_size,
                contributing_objects=[str(obj.path) for obj, section in candidates])
        else:
            member = re.search(r'\(([^()]+\.o)\)$', source)
            object_name = member[1] if member else pathlib.PurePosixPath(source).name
            assert object_name in inputs, ('unowned readonly input', source)
            obj = inputs[object_name]
            section = next(section for section in obj.sections if section['name'] == section_name)
            assert not section['flags'] & 4 and section['size'] == size
            expected = bytearray(obj.bytes(section))
            for reloc in obj.relocations:
                if reloc['section'] != section['index']:
                    continue
                assert reloc['type'] == 1, ('unsupported readonly relocation', section_name)
                target = reloc['symbol']
                if target['name'] in analysis.functions:
                    target_va = analysis.functions[target['name']]['value']
                else:
                    target_section = obj.sections[target['section']]['name']
                    assert target_section.startswith('.text.') and target_section[6:] in analysis.functions
                    target_name = target_section[6:]
                    target_obj, target_sym = analysis.owners[target_name]
                    assert target_obj.path == obj.path
                    target_va = analysis.functions[target_name]['value'] - target_sym['value'] + target['value']
                offset = reloc['offset']
                addend = struct.unpack_from('<I', expected, offset)[0]
                value = (target_va + addend) & 0xffffffff
                assert struct.unpack_from('<I', actual, offset)[0] == value
                checked.append(dict(offset=offset, target=target['name'] or target_section, linked_value=hex(value)))
                actual[offset:offset + 4] = b'\0' * 4
                expected[offset:offset + 4] = b'\0' * 4
            assert actual == expected, ('readonly linked/object bytes differ', section_name)
            identity = dict(kind='original native readonly object section', object=str(obj.path))
        rows.append(dict(address=hex(address), bytes=size, alignment=alignment, input_section=section_name,
            identity=identity, relocated_code_targets_verified=checked,
            linked_bytes_sha256=hashlib.sha256(analysis.elf.virtual(address, size)).hexdigest()))
    assert rows, 'No readonly data map coverage'
    return rows

def executable_coverage(analysis, readonly, objects, map_path):
    # Executable alignment bytes are accepted only between actual LLD input
    # sections. A function's instruction interval is never shortened/skipped.
    inputs = {path.name:module.Elf32(path) for path in objects}
    placements = []
    pattern = re.compile(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(.+):\(([^)]+)\)$')
    for line in map_path.read_text().splitlines():
        match = pattern.match(line)
        if not match:
            continue
        address, load, size, alignment, source, section_name = match.groups()
        if source == '<internal>':
            continue
        member = re.search(r'\(([^()]+\.o)\)$', source)
        object_name = member[1] if member else pathlib.PurePosixPath(source).name
        assert object_name in inputs, ('unowned map input', source)
        obj = inputs[object_name]
        section = next(section for section in obj.sections if section['name'] == section_name)
        if not section['flags'] & 4 or not section['size']:
            continue
        address, size, alignment = int(address, 16), int(size, 16), int(alignment)
        assert int(load, 16) == address and size == section['size']
        assert alignment == section['align'] and alignment > 0 and not alignment & (alignment - 1)
        assert address % alignment == 0
        placements.append(dict(address=address, bytes=size, alignment=alignment,
                               object=object_name, input_section=section_name))
    assert placements, 'No executable input section placement proof'
    sections, padding = [], []
    for section in analysis.elf.sections:
        if not section['flags'] & 4 or not section['size']:
            continue
        expected = section['addr']
        intervals = [(fn['value'], fn['size'], 1, 'code', name) for name, fn in analysis.functions.items()
                     if section['addr'] <= fn['value'] < section['addr'] + section['size']]
        intervals += [(int(row['address'], 16), row['bytes'], row['alignment'], 'readonly data',
                       row['input_section']) for row in readonly
                      if section['addr'] <= int(row['address'], 16) < section['addr'] + section['size']]
        previous = None
        for address, size, alignment, kind, name in sorted(intervals):
            assert address >= expected, ('overlapping executable layout regions', name)
            if address > expected:
                gap = analysis.elf.virtual(expected, address - expected)
                preceding = [row for row in placements if row['address'] + row['bytes'] == expected]
                preceding += [row for row in readonly if int(row['address'], 16) + row['bytes'] == expected]
                if kind == 'code':
                    obj, symbol = analysis.owners[name]
                    owner_section = obj.sections[symbol['section']]
                    following = [row for row in placements if row['address'] == address and
                                 row['object'] == obj.path.name and
                                 row['input_section'] == owner_section['name']]
                    assert len(following) == 1 and symbol['value'] == 0, ('gap is inside code input section', name)
                    placement = following[0]
                    alignment = placement['alignment']
                    assert gap == b'\xcc' * len(gap), ('noncanonical executable trap alignment', hex(expected), gap.hex())
                    for byte_address in range(expected, address):
                        assert analysis.instruction_bytes.get(byte_address) == b'\xcc' and \
                               analysis.instructions[byte_address][:2] == ('int3', ''), ('trap padding disassembly differs', hex(byte_address))
                    padding_kind = 'LLD executable input section trap alignment'
                else:
                    following = [row for row in readonly if int(row['address'], 16) == address and
                                 row['input_section'] == name]
                    assert len(following) == 1, ('unowned readonly alignment boundary', name)
                    placement = following[0]
                    assert gap == b'\0' * len(gap) or gap == b'\xcc' * len(gap), ('noncanonical readonly alignment', hex(expected), gap.hex())
                    padding_kind = 'LLD readonly input section alignment'
                assert len(preceding) == 1, ('gap does not follow an executable input section boundary', hex(expected))
                assert alignment > 1 and not alignment & (alignment - 1)
                assert address == (expected + alignment - 1) & ~(alignment - 1), ('gap differs from exact input alignment', name)
                assert previous, ('unproven padding predecessor', name)
                predecessor = []
                if previous[3] == 'code':
                    predecessor = [at for at in analysis.instruction_bytes
                                   if at + len(analysis.instruction_bytes[at]) == expected]
                    assert len(predecessor) == 1 and analysis.instructions[predecessor[0]][0] in (
                        'ret', 'retl', 'ud2', 'hlt', 'jmp'), ('padding has reachable fallthrough', hex(expected))
                else:
                    assert kind == 'readonly data', ('unproven code padding predecessor', name)
                for at, (op, arg, line) in analysis.instructions.items():
                    if not (op.startswith('j') or op.startswith('call')) or not any(
                            fn['value'] <= at < fn['value'] + fn['size'] for fn in analysis.functions.values()):
                        continue
                    target = re.match(r'^(?:0x)?([0-9a-f]+)\b', arg)
                    assert not target or not expected <= int(target[1], 16) < address, ('control transfer targets padding', hex(at), line)
                padding.append(dict(address=hex(expected), bytes=len(gap), kind=padding_kind,
                    fill_byte=gap[0], exact_alignment=alignment,
                    preceding_input_section=preceding[0], following_input_section=placement,
                    terminal_predecessor_address=hex(predecessor[0]) if predecessor else None,
                    no_direct_control_transfer_targets_padding=True))
            expected = address + size
            previous = (address, size, alignment, kind, name)
        assert expected == section['addr'] + section['size'], ('incomplete executable section', section['name'], hex(expected))
        section_padding = sum(row['bytes'] for row in padding
                              if section['addr'] <= int(row['address'], 16) < expected)
        code_bytes = sum(size for address, size, alignment, kind, name in intervals if kind == 'code')
        readonly_bytes = sum(size for address, size, alignment, kind, name in intervals if kind == 'readonly data')
        assert code_bytes + readonly_bytes + section_padding == section['size'], 'Executable byte coverage differs'
        sections.append(dict(name=section['name'], address=hex(section['addr']), bytes=section['size'],
            code_bytes=code_bytes, readonly_data_bytes=readonly_bytes, alignment_padding_bytes=section_padding,
            code_instruction_count=sum(analysis.results[name]['instruction_count']
                                       for address, size, alignment, kind, name in intervals if kind == 'code')))
    return sections, padding

def main():
    global module, REFERENCE, PROOF
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=pathlib.Path, required=True)
    parser.add_argument('--stripped-name', default='resource-probe.stripped.elf')
    parser.add_argument('--symbols-name', default='resource-probe.elf')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--qualified-png', type=pathlib.Path, default=REFERENCE)
    parser.add_argument('--stack-proof', type=pathlib.Path, default=PROOF)
    parser.add_argument('--objdump', default='objdump')
    parser.add_argument('--source-sha256', type=pathlib.Path,
                        help='Optional additional expected precommit source hashes')
    args = parser.parse_args()
    REFERENCE, PROOF = local_path(args.qualified_png).resolve(), local_path(args.stack_proof).resolve()
    args.build_dir, args.output = local_path(args.build_dir).resolve(), local_path(args.output).resolve()
    assert not args.output.exists(), 'Choose a fresh stack evidence directory'
    args.output.mkdir(parents=True)
    state = dict(scope='Fresh complete exact machine-code i386 PNG surface consumer user-stack call chain',
        timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), stack_bytes=8192,
        initial_top_reservation_bytes=16, usable_stack_bytes=8176, interrupt_user_stack_bytes=0,
        qualification_pass=False, full_stack_callchain_qualified=False, guest_run=False, guest_pass=False,
        independent_review=True, unknowns=[], artifact_directory=str(args.output))
    try:
        sys.path.insert(0, str(PROOF))
        spec = importlib.util.spec_from_file_location('stack_analysis', PROOF / 'stack-callchain.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        document = json.loads((PROOF / 'indirect-stack-targets.json').read_text())
        assert not document['remaining_unknown_targets']
        for item in document['inputs']:
            path = local_path(item['path'])
            assert path.stat().st_size == item['bytes'] and module.sha(path) == item['sha256'], ('changed proof input', item['path'])
        assert module.sha(REFERENCE / 'png-pixels-probe.stripped.elf') == 'bbdadb066c66d12db9bb1d8f253aecebd7f57e65746936ea853714c38d3786e7'
        reference_objects = [REFERENCE / (name + '.o') for name in ('decode', 'memory', 'pixel', 'skia', 'ashldi3', 'lshrdi3', 'udivdi3', 'udivmoddi4')]
        reference_owners = object_functions(reference_objects)
        reference_reports = reports(list(REFERENCE.glob('*.su')))
        reference_symbols = {symbol['name']:symbol for symbol in module.Elf32(REFERENCE / 'png-pixels-probe.elf').symbols if symbol['type'] == 2 and symbol['size']}
        runtime = args.build_dir / args.stripped_name
        symbols = args.build_dir / args.symbols_name
        build_manifest_path = args.build_dir / 'manifest.json'
        build_manifest = json.loads(build_manifest_path.read_text())
        assert build_manifest['native_build_pass'] and build_manifest['boot_file_admission_pass'] and build_manifest['actual_elf32_validation_pass']
        revision = subprocess.check_output(['git', '-C', str(TREE), 'rev-parse', 'HEAD'], text=True).strip()
        assert revision == build_manifest['source_base'], 'Stack build source_base differs from HEAD'
        expected_sources = dict(build_manifest['source_sha256'])
        if args.source_sha256:
            extra = json.loads(local_path(args.source_sha256).read_text())
            for name, value in extra.items():
                assert name not in expected_sources or expected_sources[name] == value, ('conflicting source hash', name)
                expected_sources[name] = value
        assert expected_sources, 'Build source SHA-256 proof is missing'
        for name, value in expected_sources.items():
            relative = pathlib.PurePosixPath(name)
            assert not relative.is_absolute() and '..' not in relative.parts and '\\' not in name
            assert re.fullmatch(r'[0-9a-f]{64}', value) and module.sha(TREE / name) == value, ('changed native source', name)
        assert 'apps/png_surface_probe/native_main.cc' in expected_sources, 'Actual surface consumer source proof missing'
        assert module.sha(symbols) == build_manifest['elf_sha256'], 'Symbol ELF differs from native build'
        state.update(source_base=revision, source_sha256=expected_sources,
            analysis_tool_inputs=[input_record(PROOF / name) for name in ('stack-callchain.py', 'elf32.py')])
        native_names = ('decode', 'memory', 'ashldi3', 'lshrdi3', 'udivdi3', 'udivmoddi4', 'pixel', 'skia', 'main', 'start', 'register-probe')
        objects = [args.build_dir / (name + '.o') for name in native_names]
        sus = [args.build_dir / (name + '.su') for name in native_names if (args.build_dir / (name + '.su')).exists()]
        assert all(path.is_file() for path in objects) and sus, 'missing real native link objects or .su files'
        before_hash = module.sha(runtime)
        assert build_manifest['stripped_sha256'] == before_hash
        assert build_manifest['qualified_archive_sha256'] == module.sha(REFERENCE / 'libgtos_png_pixels.a') == module.sha(args.build_dir / 'libgtos_png_pixels.a')
        input_objects = dict(build_manifest['qualified_object_sha256'])
        input_objects.update(build_manifest['consumer_object_sha256'])
        for filename, digest in input_objects.items():
            assert module.sha(args.build_dir / filename) == digest, ('changed final native input', filename)
        command = [args.objdump, '-d', '-w', '-z', str(runtime)]
        if os.name == 'nt':
            name = runtime.as_posix()
            assert re.match(r'^[A-Za-z]:/', name), 'Windows ELF must be on an existing drive'
            elf_linux = '/mnt/' + name[0].lower() + name[2:]
            command = ['wsl.exe', '-d', 'Ubuntu-20.04', '--', args.objdump,
                       '-d', '-w', '-z', elf_linux]
        disassembly = args.output / 'surface-native-disassembly.log'
        disassembly.write_bytes(subprocess.check_output(command, timeout=120))
        assert module.sha(runtime) == before_hash
        analysis = module.Analysis(runtime, objects, sus, disassembly, [], symbols)
        analysis.input_hashes[str(build_manifest_path)] = module.sha(build_manifest_path)
        analysis.input_hashes[str(args.build_dir / 'native-link.map')] = module.sha(args.build_dir / 'native-link.map')
        assert analysis.elf.header[4] == analysis.functions['_start']['value'], 'ELF entry differs from _start'
        start_object = module.Elf32(args.build_dir / 'start.o')
        start_symbol = next(symbol for symbol in start_object.symbols if symbol['name'] == '_start')
        start_size = analysis.functions['_start']['size']
        assert start_symbol['value'] + start_size == start_object.sections[start_symbol['section']]['size']
        analysis.owners['_start'] = (start_object, dict(start_symbol, size=start_size))
        linked_correspondence = linked_object_correspondence(analysis, module.Elf32(symbols))
        correspondence = []
        for name in analysis.functions:
            if name not in reference_owners:
                continue
            assert name in analysis.owners, ('missing actual object', name)
            actual = payload(analysis.owners[name])
            reference = payload(reference_owners[name])
            assert actual == reference, ('qualified helper machine code or relocation differs', name)
            correspondence.append(dict(function=name, actual_object=str(analysis.owners[name][0].path),
                reference_object=str(reference_owners[name][0].path), normalized_function_bytes_sha256=hashlib.sha256(actual[0]).hexdigest(), relocation_count=len(actual[1])))
            if name not in analysis.su:
                assert name in reference_reports, ('missing stack report', name)
                analysis.su[name] = dict(reference_reports[name], inherited_from_byte_identical_function=True)
        names = {row['function'] for row in correspondence}
        mapped = []
        for annotation in document['call_targets']:
            name = annotation['callsite_function']
            assert name in names and all(target in names for target in annotation['targets']), ('unproven indirect caller or target', name)
            copied = dict(annotation)
            copied['surface_address'] = hex(int(annotation['release_address'], 16) - reference_symbols[name]['value'] + analysis.functions[name]['value'])
            copied['reuse_proof'] = 'Original caller and every target match exact object-function bytes, addends and relocation signatures.'
            mapped.append(copied)
        analysis.annotations = mapped
        assembly_proofs = []
        for name in ('resource_probe_registers', 'surface_probe_registers'):
            if name not in analysis.functions:
                continue
            assert name in analysis.owners, 'missing real register-probe object'
            assembly_sources = [TREE / app / 'register_probe.s' for app in
                                ('apps/png_surface_probe', 'apps/png_resource_probe')]
            assembly_source = next(path for path in assembly_sources if path.is_file() and
                                   name in path.read_text())
            relative = assembly_source.relative_to(TREE).as_posix()
            assert relative in expected_sources and module.sha(assembly_source) == expected_sources[relative], 'Register assembly source is unbound'
            analysis.su[name] = dict(bytes=36, kind='static', source=str(assembly_source),
                report='Handwritten assembly, no compiler .su; four register pushes plus a 20-byte scratch reservation.',
                machine_derived_manual_assembly_report=True)
            assembly_proofs.append(dict(function=name, expected_local_high_water_bytes=36,
                source=input_record(assembly_source), actual_object=input_record(analysis.owners[name][0].path)))
        for name in analysis.functions:
            analysis.analyse_function(name)
        for proof in assembly_proofs:
            result = analysis.results[proof['function']]
            assert result['local_high_water_bytes'] == 36 and not result['calls'] and result['callee_return_cleanup'] == [0] and not result['alignment'], ('assembly stack proof mismatch', result)
            proof['all_reachable_returns_balanced'] = True
        for name in analysis.functions:
            analysis.bound(name)
        bound, path = analysis.bound('_start')
        readonly = readonly_data_spans(analysis, objects, args.build_dir / 'native-link.map')
        covered_sections, padding = executable_coverage(analysis, readonly, objects, args.build_dir / 'native-link.map')
        runtime_header = TREE / 'include/process/native_runtime.h'
        runtime_source = TREE / 'src/process/native_runtime.cpp'
        assert 'UserStackBottom = 0xBFFFD000U' in runtime_header.read_text()
        assert 'UserStackTop = 0xBFFFF000U' in runtime_header.read_text()
        assert 'cpu.esp = UserStackTop - 16;' in runtime_source.read_text()
        assert 'UserStackBottom - 4096, reservedEnd = UserStackTop + 4096' in runtime_source.read_text()
        for name, value in analysis.input_hashes.items():
            assert module.sha(name) == value, ('stack input changed during qualification', name)
        for name, value in expected_sources.items():
            assert module.sha(TREE / name) == value, ('native source changed during qualification', name)
        assert subprocess.check_output(['git', '-C', str(TREE), 'rev-parse', 'HEAD'], text=True).strip() == revision
        new_functions = [name for name in analysis.functions if name not in names]
        state.update(qualification_pass=bound < 8176, full_stack_callchain_qualified=bound < 8176,
            elf=str(runtime), elf_sha256=before_hash, symbol_elf=str(symbols), symbol_elf_sha256=module.sha(symbols),
            final_build_manifest=input_record(build_manifest_path),
            full_chain_bound_bytes=bound, headroom_bytes=8176 - bound, critical_path=path,
            functions=analysis.results, function_count=len(analysis.functions), new_functions=new_functions,
            machine_function_correspondence=correspondence, jump_tables=analysis.tables, indirect_calls=analysis.indirect,
            linked_object_machine_code_correspondence=linked_correspondence,
            manual_assembly_proofs=assembly_proofs,
            input_hashes=analysis.input_hashes, proof_inputs_verified=document['inputs'],
            reference_inputs=[input_record(p) for p in reference_objects + list(REFERENCE.glob('*.su'))],
            abi_stack_source_evidence=[input_record(p) for p in (runtime_header, runtime_source, TREE / 'src/gdt.cpp', TREE / 'src/multitasking.cpp', TREE / 'include/multitasking.h', TREE / 'src/hardwarecommunication/interruptstubs.s')],
            executable_section_coverage=covered_sections, executable_padding=padding,
            readonly_data_in_executable_section=readonly,
            disassembly_raw_bytes_and_complete_function_coverage_verified=True,
            entire_executable_section_raw_bytes_covered=True, allocated_symbol_sections_byte_identical=True,
            runtime_elf_is_stripped=not analysis.elf.symbols, command=command,
            alignment_model='Native _start ESP is known 16-aligned; each ordinary realignment conservatively adds 0..alignment-1 independent bytes.',
            notes=['All live transient argument pushes and each nested four-byte return address are included.',
                'Hidden structure-return ret$4 cleanup is read from exact linked instructions.',
                'The indexed PNG palette is included in the fresh decode_bgra frame analysis.',
                'All compiler switch-table entries are checked against original object relocations and exact linked ELF table values.',
                'CPL3 interrupts/syscalls switch to the per-task TSS kernel stack; user ESP remains in the privilege frame.',
                'Static stack qualification does not establish guest behavior. No frozen workload was built, inspected or run.'])
        (args.output / 'indirect-stack-targets.json').write_text(json.dumps(dict(document, call_targets=mapped), indent=2) + '\n')
    except Exception as error:
        state['unknowns'].append(str(error))
        (args.output / 'failure.log').write_text(traceback.format_exc())
    for name in ('stack-callchain.py', 'elf32.py'):
        if (PROOF / name).is_file():
            shutil.copyfile(PROOF / name, args.output / name)
        else:
            state['unknowns'].append('Missing analysis dependency: ' + name)
    shutil.copyfile(pathlib.Path(__file__), args.output / 'qualify-native-image-stack.py')
    (args.output / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
    summary = {key:state[key] for key in ('qualification_pass', 'unknowns', 'artifact_directory')}
    summary.update({key:state[key] for key in ('elf_sha256', 'full_chain_bound_bytes', 'headroom_bytes', 'critical_path', 'function_count', 'new_functions') if key in state})
    print(json.dumps(summary, indent=2))
    return 0 if state['qualification_pass'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
