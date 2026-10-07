#!/usr/bin/env python3
"""Strict whole-ELF stack proof for the actual pinned V8 memory.h and native C/C++ heap."""
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
state=dict(browser_guest_pass=False,media_guest_pass=False,video_guest_pass=False,html5_guest_pass=False,full_v8_backend=False,jit_guest_pass=False,scope='Exact linked i386 actual V8 memory.h/native-heap complete stack proof',
    timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),qualification_pass=False,
    full_stack_callchain_qualified=False,guest_pass=False,unknowns=[],usable_stack_bytes=8176)
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module);return module
def readonly_data_spans(analysis, objects, map_path):
    inputs = {path.name:analysis_module.Elf32(path) for path in objects}
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
            if section_name=='.rodata.str1.1':
                assert len(candidates)==1, 'Merged strings require one exact owner'
                obj,section=candidates[0]
                assert section['flags']&48==48 and not section['flags']&5 and section['entsize']==1
                original=obj.bytes(section)
                assert original==bytes(actual) and size==section['size'] and original[-1:]==b'\0', 'Merged string exact identity differs'
                entries=[];offset=0
                while offset<len(original):
                    end=original.index(b'\0',offset)+1
                    entries.append(dict(offset=offset,bytes=end-offset,sha256=hashlib.sha256(original[offset:end]).hexdigest()))
                    offset=end
                identity=dict(kind='Exact single native merge-string input section',object=str(obj.path),
                    input_section=section_name,complete_null_terminated_strings=entries,
                    original_and_linked_bytes_sha256=hashlib.sha256(original).hexdigest(),all_pool_bytes_have_exact_input_origin=True)
            else:
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
    analysis_module=load('v8_stack_engine',engine/'stack-callchain.py')
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
        str(pathlib.Path(__file__).resolve()):sha(__file__),str(repo/'tools/qualify-native-image-stack.py'):sha(repo/'tools/qualify-native-image-stack.py')})
    for name in ('stack-callchain.py','elf32.py','status.json'):bindings[str(engine/name)]=sha(engine/name)
    for path,value in bindings.items():assert sha(path)==value,('Changed exact stack input',path)
    runtime=build/'v8-heap-probe.stripped.elf';symbols=build/'v8-heap-probe.elf'
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
                assert not arg.startswith('*'),('Unknown heap indirect call',name,hex(at),arg)
    a.annotations=[]
    symbol_elf=analysis_module.Elf32(symbols)
    correspondence=helper.linked_object_correspondence(a,symbol_elf)
    for name in a.functions:a.analyse_function(name)
    for name in a.functions:a.bound(name)
    # Bind each retained genuine consumer call to its actual linked service.
    # Seeing a declaration, exported symbol or source wrapper is insufficient.
    expected={
        'actual_v8_malloc':('malloc','memory_calls.o'),
        'actual_v8_calloc':('calloc','memory_calls.o'),
        'actual_v8_realloc':('_ZN2v84base7ReallocEPvj','memory_calls.o'),
        'actual_v8_free':('free','memory_calls.o'),
        'actual_v8_aligned_alloc':('_ZN2v84base12AlignedAllocEjj','memory_calls.o'),
        'actual_v8_aligned_free':('free','memory_calls.o'),
        'actual_v8_allocate_at_least':('malloc','memory_calls.o'),
        'actual_cpp_new_scalar':('_Znwj','cpp_calls.o'),
        'actual_cpp_new_array':('_Znaj','cpp_calls.o'),
        'actual_cpp_new_nothrow_scalar':('_ZnwjRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_new_nothrow_array':('_ZnajRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_new_aligned_scalar':('_ZnwjSt11align_val_t','cpp_calls.o'),
        'actual_cpp_new_aligned_array':('_ZnajSt11align_val_t','cpp_calls.o'),
        'actual_cpp_new_aligned_nothrow_scalar':('_ZnwjSt11align_val_tRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_new_aligned_nothrow_array':('_ZnajSt11align_val_tRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_delete_scalar':('_ZdlPv','cpp_calls.o'),
        'actual_cpp_delete_array':('_ZdaPv','cpp_calls.o'),
        'actual_cpp_delete_nothrow_scalar':('_ZdlPvRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_delete_nothrow_array':('_ZdaPvRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_delete_sized_scalar':('_ZdlPvj','cpp_calls.o'),
        'actual_cpp_delete_sized_array':('_ZdaPvj','cpp_calls.o'),
        'actual_cpp_delete_aligned_scalar':('_ZdlPvSt11align_val_t','cpp_calls.o'),
        'actual_cpp_delete_aligned_array':('_ZdaPvSt11align_val_t','cpp_calls.o'),
        'actual_cpp_delete_aligned_nothrow_scalar':('_ZdlPvSt11align_val_tRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_delete_aligned_nothrow_array':('_ZdaPvSt11align_val_tRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_delete_sized_aligned_scalar':('_ZdlPvjSt11align_val_t','cpp_calls.o'),
        'actual_cpp_delete_sized_aligned_array':('_ZdaPvjSt11align_val_t','cpp_calls.o'),
        'actual_cpp_construct':('_ZnwjSt11align_val_t','cpp_calls.o'),
        'actual_cpp_construct_nothrow_oversized':('_ZnwjSt11align_val_tRKSt9nothrow_t','cpp_calls.o'),
        'actual_cpp_destroy':('_ZdlPvjSt11align_val_t','cpp_calls.o')}
    guest_targets={row['target'] for row in a.results['heap_guest_main']['calls']}
    assert set(expected)<=guest_targets,'A consumer operation is not called by actual guest main'
    consumer_calls=[]
    for caller,(target,owner) in expected.items():
        assert a.owners[caller][0].path.name==owner,('Unexpected consumer code owner',caller)
        calls=a.results[caller]['calls']
        assert len(calls)==1 and calls[0]['target']==target,('Actual consumer target differs',caller,calls)
        target_owner='operators.o' if caller.startswith('actual_cpp_') else ('heap.o' if target in ('malloc','calloc','free') else 'memory_calls.o')
        assert a.owners[target][0].path.name==target_owner,('Unexpected allocation service code owner',target)
        consumer_calls.append(dict(consumer=caller,consumer_owner=owner,target_owner=target_owner,**calls[0]))
    upstream_nested=[]
    for caller,target in (('_ZN2v84base7ReallocEPvj','realloc'),('_ZN2v84base12AlignedAllocEjj','posix_memalign')):
        calls=a.results[caller]['calls']
        assert len(calls)==1 and calls[0]['target']==target,('Upstream inline allocation service differs',caller,calls)
        assert a.owners[caller][0].path.name=='memory_calls.o' and a.owners[target][0].path.name=='heap.o'
        upstream_nested.append(dict(upstream=caller,**calls[0]))
    bound,critical=a.bound('_start')
    readonly=readonly_data_spans(a,objects,build/'native-link.map')
    merged_pointers=[]
    for row in readonly:
        if row['identity']['kind']!='Exact single native merge-string input section':continue
        owner=pathlib.Path(row['identity']['object']);obj=analysis_module.Elf32(owner)
        section=next(section for section in obj.sections if section['name']==row['input_section'])
        for relocation in obj.relocations:
            symbol=relocation['symbol']
            if symbol['section']!=section['index']:continue
            assert relocation['type']==1,'Unsupported merged string relocation'
            callers=[(name,source_symbol) for name,(source_object,source_symbol) in a.owners.items()
                if name in a.functions and source_object.path==owner and source_symbol['section']==relocation['section']
                and source_symbol['value']<=relocation['offset']<source_symbol['value']+source_symbol['size']]
            assert len(callers)==1,('Unowned merged-string address relocation',owner,relocation)
            name,source_symbol=callers[0]
            raw=obj.bytes(obj.sections[relocation['section']])
            offset=(symbol['value']+struct.unpack_from('<I',raw,relocation['offset'])[0])&0xffffffff
            assert offset<section['size']
            at=a.functions[name]['value']+relocation['offset']-source_symbol['value']
            linked=struct.unpack('<I',a.elf.virtual(at,4))[0]
            assert linked==int(row['address'],16)+offset,('Merged-string relocation differs',name,hex(at))
            merged_pointers.append(dict(function=name,relocation_site=hex(at),input_offset=offset,actual_pool_pointer=hex(linked)))
    assert not any(row['identity']['kind']=='Exact single native merge-string input section' for row in readonly) or merged_pointers
    coverage,padding=executable_coverage(a,readonly,objects,build/'native-link.map')
    header=repo/'include/process/native_runtime.h';runtime_source=repo/'src/process/native_runtime.cpp'
    assert 'UserStackBottom = 0xBFFFD000U' in header.read_text() and 'UserStackTop = 0xBFFFF000U' in header.read_text()
    assert 'cpu.esp = UserStackTop - 16;' in runtime_source.read_text()
    for path,value in {**bindings,**a.input_hashes}.items():assert sha(path)==value,('Stack input changed during proof',path)
    assert bound<8176 and not a.elf.symbols
    state.update(qualification_pass=True,full_stack_callchain_qualified=True,source_base=revision,
        elf=str(runtime),elf_sha256=sha(runtime),symbol_elf=str(symbols),symbol_elf_sha256=sha(symbols),
        full_chain_bound_bytes=bound,headroom_bytes=8176-bound,critical_path=critical,
        function_count=len(a.functions),functions=a.results,function_aliases=aliases,
        indirect_calls=a.indirect,jump_tables=a.tables,
        actual_consumer_call_sites=consumer_calls,actual_upstream_nested_calls=upstream_nested,
        actual_indirect_calls_forbidden=True,linked_object_machine_code_correspondence=correspondence,
        merged_string_pointer_relocations=merged_pointers,executable_section_coverage=coverage,executable_padding=padding,readonly_data_in_executable_section=readonly,
        input_hashes={**bindings,**a.input_hashes},entire_executable_section_raw_bytes_covered=True,
        allocated_symbol_sections_byte_identical=True,interrupt_user_stack_bytes=0,
        notes=['All retained linked functions and every executable-section byte are covered.',
            'Transient arguments, four-byte nested returns, tail calls, hidden-sret cleanup and alignment are included.',
            'CPL3 syscalls use the TSS kernel stack; static proof does not establish guest behavior.'])
except Exception as error:
    state['unknowns'].append(str(error));(out/'failure.log').write_text(traceback.format_exc())
for path in [pathlib.Path(__file__),repo/'tools/qualify-native-image-stack.py',engine/'stack-callchain.py',engine/'elf32.py']:
    if path.is_file():shutil.copyfile(path,out/path.name)
(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps({k:state[k] for k in ('qualification_pass','unknowns')},indent=2))
raise SystemExit(0 if state['qualification_pass'] else 1)
