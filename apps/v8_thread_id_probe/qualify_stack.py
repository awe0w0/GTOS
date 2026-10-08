#!/usr/bin/env python3
"""Strict whole-ELF stack proof for the actual pinned V8 ThreadId/compiler-emulated TLS and checked coarse clock."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import traceback
sys.dont_write_bytecode=True
repo=pathlib.Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('build',type=pathlib.Path)
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--analysis-directory',type=pathlib.Path,default=repo.parent.parent/'artifacts/png-stack-gn-release-20261007T103153Z')
args=parser.parse_args()
build=args.build.resolve();out=args.output.resolve();engine=args.analysis_directory.resolve()
assert not out.exists(),'Choose a fresh qualification evidence directory'
out.mkdir(parents=True)
state=dict(native_tls_backend=False,shared_pas_threads=False,platform_key_tls=False,tls_dynamic_initialization=False,tls_destructor_registration=False,browser_guest_pass=False,media_guest_pass=False,video_guest_pass=False,html5_guest_pass=False,full_v8_backend=False,jit_guest_pass=False,scope='Exact linked i386 whole-byte pinned V8 ThreadId/compiler-emulated TLS complete stack proof',
    timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),qualification_pass=False,
    full_stack_callchain_qualified=False,guest_pass=False,unknowns=[],usable_stack_bytes=8176)
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module);return module
def owned_section_proof(analysis, objects, map_path, symbol_elf):
    inputs = {path.name: analysis_module.Elf32(path) for path in objects}
    assert len(inputs) == len(objects), 'Object basenames must be unique'
    placements = []
    pattern = re.compile(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(.+):\(([^)]+)\)$')
    for line in map_path.read_text().splitlines():
        match = pattern.match(line)
        if not match:
            continue
        address, lma, size, alignment, owner, section_name = match.groups()
        if owner == '<internal>':
            assert section_name in ('.symtab', '.strtab', '.shstrtab'), ('Unowned linker pool', section_name)
            continue
        assert address == lma
        name = pathlib.Path(owner).name
        assert name in inputs, ('Unknown map object', owner)
        obj = inputs[name]
        sections = [section for section in obj.sections if section['name'] == section_name]
        assert len(sections) == 1
        section = sections[0]
        assert int(size, 16) == section['size'] and int(alignment) == section['align']
        if not section['size'] or not section['flags'] & 2:
            continue
        placements.append(dict(address=int(address, 16), bytes=section['size'], alignment=section['align'],
            object=name, input_section=section_name, section_index=section['index'], section_type=section['type'], flags=section['flags']))
    byname = {}
    for symbol in symbol_elf.symbols:
        if symbol['name'] and symbol['section']:
            byname.setdefault(symbol['name'], []).append(symbol)
    def resolve(obj, target):
        if target['section']:
            section = obj.sections[target['section']]
            owned = [row for row in placements if row['object'] == obj.path.name and row['input_section'] == section['name']]
            if owned:
                assert len(owned)==1
                return owned[0]['address']+target['value']
            # LLD may choose another object's byte-identical weak COMDAT body.
            # Local/section references cannot acquire an unrelated external owner.
            assert target['bind']==2 and target['type']==2 and target['name'] in analysis.functions, ('Unplaced relocation section',obj.path,target)
            chosen=analysis.owners[target['name']]
            assert chosen[1]['bind']==2 and chosen[1]['size']==target['size']
            assert helper.payload((obj,target))==helper.payload(chosen), ('Weak COMDAT payload/relocations differ',obj.path,target['name'])
            return analysis.functions[target['name']]['value']
        matches = byname.get(target['name'], [])
        assert len(matches) == 1, ('Unknown/ambiguous external relocation', target)
        return matches[0]['value']
    rows = []
    for placement in placements:
        obj = inputs[placement['object']]
        section = obj.sections[placement['section_index']]
        relocation_rows = []
        if section['type'] == 8:
            assert not [rr for rr in obj.relocations if rr['section'] == section['index']]
            rows.append(dict(**placement, identity=dict(kind='Exact input NOBITS zero initialization')))
            continue
        expected = bytearray(obj.bytes(section))
        for relocation in obj.relocations:
            if relocation['section'] != section['index']:
                continue
            offset = relocation['offset']
            assert relocation['type'] in (1, 2) and 0 <= offset <= len(expected) - 4
            target = resolve(obj, relocation['symbol'])
            addend = struct.unpack_from('<I', expected, offset)[0]
            value = (target + addend - (placement['address'] + offset if relocation['type'] == 2 else 0)) & 0xffffffff
            struct.pack_into('<I', expected, offset, value)
            relocation_rows.append(dict(offset=offset, type=relocation['type'], target=relocation['symbol']['name'], resolved_target=hex(target), linked_value=hex(value)))
        actual = analysis.elf.virtual(placement['address'], placement['bytes'])
        assert actual == expected, ('Whole input section bytes/relocations differ', placement)
        rows.append(dict(**placement, identity=dict(kind='Complete exact owner bytes with all relocations resolved',
            linked_bytes_sha256=hashlib.sha256(actual).hexdigest(), relocations=relocation_rows)))
    # Every allocated section byte has a real owner, or exact LLD input alignment.
    coverage = []
    for section in analysis.elf.sections:
        if not section['flags'] & 2 or not section['size']:
            continue
        members = sorted((row for row in rows if section['addr'] <= row['address'] < section['addr'] + section['size']), key=lambda row: row['address'])
        assert members, ('Unowned allocated output section', section['name'])
        cursor = section['addr']; padding = []
        for row in members:
            assert row['address'] >= cursor
            if row['address'] != cursor:
                assert row['address'] == (cursor + row['alignment'] - 1) & ~(row['alignment'] - 1)
                assert row['alignment'] > 1 and not row['alignment'] & (row['alignment'] - 1)
                if section['type'] != 8:
                    actual = analysis.elf.virtual(cursor, row['address'] - cursor)
                    expected = b'\xcc' if section['flags'] & 4 else b'\0'
                    assert actual == expected * len(actual), ('Noncanonical exact section alignment', section['name'], hex(cursor))
                padding.append(dict(address=hex(cursor), bytes=row['address']-cursor, alignment=row['alignment']))
            cursor = row['address'] + row['bytes']
        assert cursor == section['addr'] + section['size'], ('Allocated section tail not owned', section['name'])
        coverage.append(dict(section=section['name'], bytes=section['size'], owned_bytes=sum(row['bytes'] for row in members), exact_alignment_padding=padding))
    return rows, coverage

def executable_coverage(analysis, readonly, objects, map_path):
    # Executable alignment bytes are accepted only between actual LLD input
    # sections. A function's instruction interval is never shortened/skipped.
    inputs = {path.name:analysis_module.Elf32(path) for path in objects}
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
                    assert len(predecessor) == 1, ('ambiguous padding predecessor', hex(expected))
                    terminal_op, terminal_arg, terminal_line = analysis.instructions[predecessor[0]]
                    terminal = terminal_op in ('ret', 'retl', 'ud2', 'hlt', 'jmp')
                    if terminal_op in ('call', 'calll'):
                        destination = re.match(r'^(?:0x)?([0-9a-f]+)\b', terminal_arg)
                        assert destination and int(destination[1], 16) in analysis.starts
                        callee = analysis.starts[int(destination[1], 16)]
                        assert analysis.results[callee]['callee_return_cleanup'] == [] and not analysis.cleanup[callee], ('padding follows returning call', callee)
                        terminal = True
                    assert terminal, ('padding has reachable fallthrough', hex(expected))
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

try:
    engine_status=json.loads((engine/'status.json').read_text())
    assert engine_status['full_stack_callchain_qualified'] and not engine_status['unknowns']
    sys.path.insert(0,str(engine))
    analysis_module=load('v8_thread_id_stack_engine',repo/'apps/v8_thread_id_probe/stack_engine.py')
    helper=load('v8_stack_coverage',repo/'tools/qualify-native-image-stack.py')
    helper.module=analysis_module
    manifest=build/'manifest.json';m=json.loads(manifest.read_text())
    assert m['native_build_pass'] and m['actual_elf32_validation_pass'] and m['boot_file_admission_pass'] and m['source_provenance_pass']
    revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    assert revision==m['source_base']
    bindings={str(repo/name):value for name,value in m['source_sha256'].items()}
    for path,value in bindings.items():assert sha(path)==value,('Changed build-bound source before proof',path)
    bindings.update(m['compiled_input_sha256'])
    bindings.update({str(build/name):value for name,value in m['consumer_object_sha256'].items()})
    bindings.update({str(manifest):sha(manifest),str(build/'native-link.map'):sha(build/'native-link.map'),
        str(repo/'apps/v8_thread_id_probe/stack_engine.py'):sha(repo/'apps/v8_thread_id_probe/stack_engine.py'),str(pathlib.Path(__file__).resolve()):sha(__file__),str(repo/'tools/qualify-native-image-stack.py'):sha(repo/'tools/qualify-native-image-stack.py')})
    for name in ('stack-callchain.py','elf32.py','status.json'):bindings[str(engine/name)]=sha(engine/name)
    for path,value in bindings.items():assert sha(path)==value,('Changed exact stack input',path)
    runtime=build/'v8-thread-id-probe.stripped.elf';symbols=build/'v8-thread-id-probe.elf'
    assert sha(runtime)==m['stripped_sha256'] and sha(symbols)==m['elf_sha256']
    objects=[build/name for name in m['consumer_object_sha256']]
    reports=sorted(build.glob('*.su'))
    dis=out/'disassembly.log'
    dis.write_bytes(subprocess.check_output(['objdump','-dwz',str(runtime)]))
    a=analysis_module.Analysis(runtime,objects,reports,dis,[],symbols)
    aliases=[]
    for name,fn in list(a.functions.items()):
        canonical=a.starts[fn['value']]
        if canonical not in a.su:
            choices=[n for n,f in a.functions.items() if f['value']==fn['value'] and n in a.su]
            if len(choices)==1:
                canonical=choices[0];a.starts[fn['value']]=canonical
        if name==canonical:continue
        target=a.functions[canonical]
        assert fn['size']==target['size'] and helper.payload(a.owners[name])==helper.payload(a.owners[canonical]),('Unequal function alias',name,canonical)
        aliases.append(dict(alias=name,canonical=canonical,address=hex(fn['value']),bytes=fn['size']))
        del a.functions[name]
    start_object=analysis_module.Elf32(build/'start.o')
    start_symbol=next(s for s in start_object.symbols if s['name']=='_start')
    start_size=a.functions['_start']['size']
    assert a.elf.header[4]==a.functions['_start']['value']
    assert start_symbol['value']+start_size==start_object.sections[start_symbol['section']]['size']
    a.owners['_start']=(start_object,dict(start_symbol,size=start_size))
    # There is no virtual object, function-pointer table or callback in this
    # leaf. Any actual indirect call fails closed rather than acquiring a target
    # from a source-level claim about an allocator type.
    for name,fn in a.functions.items():
        for at,(op,arg,line) in a.instructions.items():
            if fn['value']<=at<fn['value']+fn['size'] and op.startswith('call'):
                assert not arg.startswith('*'),('Unknown native TLS indirect call',name,hex(at),arg)
    a.annotations=[]
    symbol_elf=analysis_module.Elf32(symbols)
    correspondence=[] # Complete section bytes and every relocation are checked below; no masked anonymous data references.
    for name in a.functions:a.analyse_function(name)
    for name in a.functions:a.bound(name)
    # Explicit genuine four-byte class-return signatures; any other cleanup fails.
    expected_sret = {
        '_ZN2v88internal8ThreadId13TryGetCurrentEv': ('ThreadId ThreadId::TryGetCurrent()', 'thread-id.o'),
        '_ZN2v88internal8ThreadId7CurrentEv': ('ThreadId ThreadId::Current()', 'thread_id_calls.o'),
        '_ZN2v88internal8ThreadId7InvalidEv': ('ThreadId ThreadId::Invalid()', None),
        '_ZN2v88internal8ThreadId11FromIntegerEi': ('ThreadId ThreadId::FromInteger(int)', 'thread_id_calls.o'),
        '_Z13actual_v8_tryv': ('ThreadId actual_v8_try()', 'thread_id_calls.o'),
        '_Z17actual_v8_currentv': ('ThreadId actual_v8_current()', 'thread_id_calls.o'),
        '_Z17actual_v8_invalidv': ('ThreadId actual_v8_invalid()', 'thread_id_calls.o'),
        '_Z22actual_v8_from_integeri': ('ThreadId actual_v8_from_integer(int)', 'thread_id_calls.o')}
    required_sret = {'_ZN2v88internal8ThreadId13TryGetCurrentEv', '_Z13actual_v8_tryv', '_Z17actual_v8_currentv', '_Z17actual_v8_invalidv', '_Z22actual_v8_from_integeri'}
    assert required_sret <= set(a.functions)
    sret_proof = []
    for name, result in a.results.items():
        if name in expected_sret:
            signature, owner = expected_sret[name]
            assert result['callee_return_cleanup'] == [4], ('Actual ThreadId hidden-return cleanup differs', name)
            assert owner is None or a.owners[name][0].path.name == owner
            assert a.owners[name][0].path.name in ('thread-id.o', 'thread_id_calls.o')
            sret_proof.append(dict(function=name, actual_signature=signature, object_bytes=4, hidden_return_pointer_bytes=4,
                callee_pop_bytes=4, actual_compiled_method_owner=a.owners[name][0].path.name))
        else:
            assert result['callee_return_cleanup'] in ([], [0]), ('Unknown callee return cleanup', name, result['callee_return_cleanup'])
    optimized = m['optimization'] == 'Oz'
    expected = {
        '_Z13actual_v8_tryv': ['_ZN2v88internal8ThreadId13TryGetCurrentEv'],
        '_Z17actual_v8_currentv': ['_ZN2v88internal8ThreadId18GetCurrentThreadIdEv' if optimized else '_ZN2v88internal8ThreadId7CurrentEv'],
        '_Z17actual_v8_invalidv': [] if optimized else ['_ZN2v88internal8ThreadId7InvalidEv'],
        '_Z22actual_v8_from_integeri': [] if optimized else ['_ZN2v88internal8ThreadId11FromIntegerEi'],
        '_Z15actual_v8_validRKN2v88internal8ThreadIdE': [] if optimized else ['_ZNK2v88internal8ThreadId7IsValidEv'],
        '_Z15actual_v8_equalRKN2v88internal8ThreadIdES3_': [] if optimized else ['_ZNK2v88internal8ThreadIdeqERKS1_'],
        '_Z17actual_v8_unequalRKN2v88internal8ThreadIdES3_': [] if optimized else ['_ZNK2v88internal8ThreadIdneERKS1_'],
        '_Z17actual_v8_integerRKN2v88internal8ThreadIdE': [] if optimized else ['_ZNK2v88internal8ThreadId9ToIntegerEv']}
    guest_targets = {row['target'] for row in a.results['tls_guest_main']['calls']}
    assert set(expected) <= guest_targets, 'Genuine ThreadId operation is missing from guest main'
    consumer_calls = []; inline_operations = []
    for caller, targets in expected.items():
        assert a.owners[caller][0].path.name == 'thread_id_calls.o'
        calls = a.results[caller]['calls']
        assert sorted(row['target'] for row in calls) == sorted(targets), ('Actual ThreadId consumer target differs', caller, calls)
        for call in calls:
            assert a.owners[call['target']][0].path.name in ('thread-id.o', 'thread_id_calls.o')
            consumer_calls.append(dict(consumer=caller, consumer_owner='thread_id_calls.o', target_owner=a.owners[call['target']][0].path.name, **call))
        if not targets:
            inline_operations.append(dict(consumer=caller, consumer_owner='thread_id_calls.o', actual_upstream_header='src/execution/thread-id.h', actual_compiled_inline_body=True))
    # Bind actual TLS code to its compiler descriptor, not a substitute resolver call.
    upstream_nested = []; compiler_calls = []
    wrappers = {'actual_tls_zero': '_ZTW14tls_probe_zero', 'actual_tls_nonzero': '_ZTW17tls_probe_nonzero',
        'actual_tls_aligned': '_ZTW17tls_probe_aligned'}
    if m['tls_mode'] in (4,5,6): wrappers['actual_tls_oom'] = '_ZTW13tls_probe_oom'
    assert set(wrappers) <= guest_targets
    for caller, wrapper in wrappers.items():
        assert a.owners[caller][0].path.name == 'tls_calls.o'
        calls = a.results[caller]['calls']
        assert len(calls) == 1 and calls[0]['target'] in ('__emutls_get_address',wrapper), ('Actual compiler TLS wrapper differs', caller, calls)
        target=calls[0]['target']
        compiler_calls.append(dict(compiler_consumer=caller, compiler_generated_storage_wrapper_inlined=target=='__emutls_get_address', **calls[0]))
        if target == wrapper:
            assert a.owners[wrapper][0].path.name == 'tls_calls.o'
            nested = a.results[wrapper]['calls']
            assert len(nested) == 1 and nested[0]['target'] == '__emutls_get_address'
            compiler_calls.append(dict(compiler_wrapper=wrapper, **nested[0]))
    for caller in ('_ZN2v88internal8ThreadId13TryGetCurrentEv', '_ZN2v88internal8ThreadId18GetCurrentThreadIdEv'):
        assert a.owners[caller][0].path.name == 'thread-id.o'
        calls = a.results[caller]['calls']
        helper_calls = [call for call in calls if call['target'] == '__emutls_get_address']
        assert helper_calls, ('Original ThreadId has no actual compiler TLS consumption', caller)
        upstream_nested.extend(dict(upstream=caller, **call) for call in helper_calls)
    getter = '_ZN2v88internal8ThreadId18GetCurrentThreadIdEv'
    owned_atomic_functions = [name for name in a.functions if a.owners[name][0].path.name == 'thread-id.o']
    atomic_sites = [dict(function=name, site=hex(at), instruction=line) for name in owned_atomic_functions
        for at, (op, arg, line) in a.instructions.items() if a.functions[name]['value'] <= at < a.functions[name]['value'] + a.functions[name]['size'] and 'lock xadd' in line]
    assert len(atomic_sites) == (1 if optimized else 5), ('Actual pinned ThreadId atomic fetch-add is missing/duplicated', atomic_sites)
    # Actual upstream CHECK terminates via int3;ud2. Both opcodes are covered.
    traps = []
    check_lambda='_ZZN2v88internal8ThreadId18GetCurrentThreadIdEvENK3$_0clEv'
    check_owner=getter if optimized else check_lambda
    assert check_owner in a.functions and a.owners[check_owner][0].path.name=='thread-id.o'
    if not optimized:
        assert check_lambda in {call['target'] for call in a.results[getter]['calls']}
    fn=a.functions[check_owner]
    for at,(op,arg,line) in a.instructions.items():
        if fn['value']<=at<fn['value']+fn['size'] and op=='int3':
            assert a.instruction_bytes[at]==b'\xcc' and a.instructions.get(at+1,())[:2]==('ud2','') and a.instruction_bytes[at+1]==b'\x0f\x0b'
            traps.append(dict(function=check_owner,address=hex(at),exact_trap_bytes='cc0f0b',no_success_stub=True))
    assert len(traps)==1,'Original CHECK trap path must remain'
    assert a.owners['__emutls_get_address'][0].path.name == 'emutls.o'
    runtime_targets = {row['target'] for row in a.results['__emutls_get_address']['calls']}
    assert 'posix_memalign' in runtime_targets, 'TLS storage must consume the real native heap'
    bound,critical=a.bound('_start')
    section_rows, allocated_coverage = owned_section_proof(a,objects,build/'native-link.map',symbol_elf)
    descriptor_arguments=[]
    descriptor_names={row['logical_role']:row['symbol'] for row in m['control_inventory']}
    expected_descriptor_consumers={
        '_ZN2v88internal8ThreadId13TryGetCurrentEv':0,
        '_ZN2v88internal8ThreadId18GetCurrentThreadIdEv':0,
        'actual_tls_zero':1,'actual_tls_nonzero':2,'actual_tls_aligned':3}
    if m['tls_mode'] in (4,5,6):expected_descriptor_consumers['actual_tls_oom']=4
    for caller,role in expected_descriptor_consumers.items():
        actual_caller=caller
        if caller.startswith('actual_tls') and a.results[caller]['calls'][0]['target']!='__emutls_get_address':
            actual_caller=a.results[caller]['calls'][0]['target']
        obj,symbol=a.owners[actual_caller]
        references=[]
        for rel in obj.relocations:
            if rel['section']!=symbol['section'] or not symbol['value']<=rel['offset']<symbol['value']+symbol['size']:continue
            target=rel['symbol'];name=target['name']
            if target['section']:
                target_section=obj.sections[target['section']]['name']
                if target_section.startswith('.data.__emutls_v.'):
                    section=obj.sections[target['section']]
                    assert section['size']==16 and section['align']==4 and section['flags']==3
                    assert target['type'] in (1,3)
                    name=target_section[len('.data.'):]
            if not name.startswith('__emutls_v.'):continue
            assert rel['type']==1 and name==descriptor_names[role], ('Wrong actual compiler TLS descriptor argument',caller,name)
            addend=struct.unpack_from('<I',obj.bytes(obj.sections[symbol['section']]),rel['offset'])[0]
            assert target['value']+addend==0, ('Compiler TLS argument is not exact descriptor base',caller,rel)
            references.append(rel)
        assert references, ('Missing actual compiler TLS descriptor argument',caller)
        descriptor_arguments.append(dict(consumer=caller,actual_argument_function=actual_caller,logical_role=role,
            expected_private_or_global_descriptor=descriptor_names[role],actual_argument_relocations=[dict(offset=rel['offset']-symbol['value'],type=rel['type']) for rel in references]))

    readonly=[]
    merged_pointers=[]
    coverage,padding=executable_coverage(a,readonly,objects,build/'native-link.map')
    header=repo/'include/process/native_runtime.h';runtime_source=repo/'src/process/native_runtime.cpp'
    assert 'UserStackBottom = 0xBFFFD000U' in header.read_text() and 'UserStackTop = 0xBFFFF000U' in header.read_text()
    assert 'cpu.esp = UserStackTop - 16;' in runtime_source.read_text()
    for path,value in {**bindings,**a.input_hashes}.items():assert sha(path)==value,('Stack input changed during proof',path)
    assert bound<8176 and not a.elf.symbols
    state.update(qualification_pass=True,full_stack_callchain_qualified=True,source_base=revision,
        elf=str(runtime),elf_sha256=sha(runtime),symbol_elf=str(symbols),symbol_elf_sha256=sha(symbols),
        full_chain_bound_bytes=bound,headroom_bytes=8176-bound,critical_path=critical,
        function_count=len(a.functions),functions=a.results,function_aliases=aliases,actual_hidden_sret_signature_proof=sret_proof,
        indirect_calls=a.indirect,jump_tables=a.tables,actual_compiler_tls_calls=compiler_calls,actual_atomic_fetch_add=atomic_sites,actual_check_traps=traps,
         actual_compiler_descriptor_argument_proof=descriptor_arguments,allocated_section_origin_proof=section_rows,allocated_section_byte_coverage=allocated_coverage,
        actual_consumer_call_sites=consumer_calls,actual_upstream_nested_calls=upstream_nested,actual_upstream_inline_operations=inline_operations,
        actual_indirect_calls_forbidden=True,linked_object_machine_code_correspondence=correspondence,
        merged_string_pointer_relocations=merged_pointers,executable_section_coverage=coverage,executable_padding=padding,readonly_data_in_executable_section=readonly,
        input_hashes={**bindings,**a.input_hashes},entire_executable_section_raw_bytes_covered=True,
        allocated_symbol_sections_byte_identical=True,interrupt_user_stack_bytes=0,
        notes=['All retained linked functions and every executable-section byte are covered.',
            'Transient arguments, four-byte nested returns, tail calls, hidden-sret cleanup and alignment are included.',
            'CPL3 syscalls use the TSS kernel stack; static proof does not establish guest behavior.'])
except Exception as error:
    state['unknowns'].append(str(error));(out/'failure.log').write_text(traceback.format_exc())
for path in [pathlib.Path(__file__),repo/'apps/v8_thread_id_probe/stack_engine.py',repo/'tools/qualify-native-image-stack.py',engine/'stack-callchain.py',engine/'elf32.py']:
    if path.is_file():shutil.copyfile(path,out/path.name)
(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps({k:state[k] for k in ('qualification_pass','unknowns')},indent=2))
raise SystemExit(0 if state['qualification_pass'] else 1)
