// Pure planning and evidence checks. Callers pin files; this module launches nothing.
const same = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => typeof v === 'string' && v === b[i]);
const need = (ok, message) => { if (!ok) throw Error(message); };
const native = p => typeof p === 'string' && p.startsWith('/') && !p.includes('\0') && !/[\\:\t\r\n]/.test(p) && !p.split('/').slice(1).some(v => !v || v === '.' || v === '..');
const within = (p, root) => native(p) && native(root) && p.startsWith(root + '/');
export const IMAGE_NATIVE_KINDS = Object.freeze(['native_metadata', 'owner_assembly', 'static_image_link']);

export function imageExecSet(kind, tools) {
  need(IMAGE_NATIVE_KINDS.includes(kind), 'known image native category');
  const result = [tools.python_realpath, tools.loader];
  if (kind === 'owner_assembly') result.push(tools.as);
  if (kind === 'static_image_link') result.push(tools['ld.bfd']);
  need(result.every(native) && new Set(result).size === result.length, 'distinct pinned executable files');
  return result;
}

export function planImageRequest(context, stage, kind, suppliedArgv) {
  need(/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage) && IMAGE_NATIVE_KINDS.includes(kind), 'registered image stage and kind');
  const choice = context.native_choices[stage];
  if (kind === 'native_metadata') {
    need(!choice && Array.isArray(suppliedArgv) && suppliedArgv.every(v => typeof v === 'string') && suppliedArgv.length >= 4 && suppliedArgv[0] === context.tools.python_realpath && suppliedArgv[1] === '-B' && suppliedArgv[2] === '-S' && within(suppliedArgv[3], context.work_root + '/frozen-helpers'), 'pinned metadata helper');
    return { stage, kind, argv: [...suppliedArgv], outputs: [], expected: { argv: [...suppliedArgv] } };
  }
  need(choice && choice.category === kind, 'exact image choice category');
  let argv, outputs, expected;
  if (kind === 'owner_assembly') {
    const output = context.work_root + '/objects/' + stage + '/loader.asm.o';
    argv = [context.tools.as, '--32', 'src/loader.s', '-o', output];
    need(choice.source === 'src/loader.s' && choice.cwd === context.source_root + '/view' && choice.output === output && same(choice.argv, argv), 'exact loader source and output table');
    outputs = [output]; expected = { argv, output };
  } else {
    const output = context.work_root + '/image/' + stage + '/provider-static.elf', map = context.work_root + '/image/' + stage + '/provider-static.map', script = context.source_root + '/view/linker.ld';
    const prefix = [context.tools['ld.bfd'], '-melf_i386', '-T', script, '-Map', map, '-o', output];
    const inherited = choice.inherited_inputs, loader = context.selected_loader;
    need(choice.output === output && choice.map === map && choice.linker_script === script && choice.cwd === context.work_root && same(choice.prefix, prefix), 'exact final image prefix and both outputs');
    need(Array.isArray(inherited) && inherited.length === 59 && inherited.every(p => within(p, context.work_root + '/inherited') && /^[a-z][a-z0-9_]*(?:\.asm)?\.o$/.test(p.slice((context.work_root + '/inherited/').length))) && new Set(inherited).size === 59, '59 distinct inherited individuals');
    const loaderChoices = Object.values(context.native_choices).filter(v => v.category === 'owner_assembly').map(v => v.output);
    need(loaderChoices.includes(loader) && within(loader, context.work_root + '/objects') && !inherited.includes(loader), 'selected loader is a current registered individual');
    argv = [...prefix, ...inherited, loader]; outputs = [output, map]; expected = { argv, elf: output, map };
    need(argv.length === 68 && new Set(argv.slice(8)).size === 60, '60 ordered inputs and 68 tokens');
  }
  need(outputs.every(p => within(p, context.work_root)), 'all outputs in current native root');
  if (suppliedArgv !== undefined) need(same(suppliedArgv, argv), 'supplied argv equals frozen plan in order');
  return { stage, kind, argv, outputs, expected };
}

// `elf` must come from the caller's pinned bounded ET_REL reader for these bytes.
export function resolveInitializerEntries(input, elf) {
  need(input instanceof Uint8Array && input.byteLength >= 52, 'real ET_REL bytes required');
  const bytes = Buffer.from(input);
  need(bytes.subarray(0, 7).equals(Buffer.from([0x7f, 69, 76, 70, 1, 1, 1])) && bytes.readUInt16LE(16) === 1 && bytes.readUInt16LE(18) === 3 && elf?.bytes === bytes.length && Array.isArray(elf.sections) && Array.isArray(elf.symbols) && Array.isArray(elf.relocations), 'matching bounded ET_REL parse');
  need(!elf.sections.some(s => s.size && s.name?.startsWith('.init_array.')), 'priority initializer section needs an explicit policy');
  const arrays = elf.sections.filter(s => s.name === '.init_array' && s.size > 0), result = [];
  for (const section of arrays) {
    need(section.type === 14 && Number.isInteger(section.offset) && section.offset >= 0 && Number.isInteger(section.size) && section.size % 4 === 0 && section.offset <= bytes.length && section.size <= bytes.length - section.offset, 'bounded initializer section');
    const relocs = elf.relocations.filter(r => r.targetSection === section.index);
    need(relocs.length === section.size / 4, 'one relocation per initializer slot');
    for (let offset = 0; offset < section.size; offset += 4) {
      const matches = relocs.filter(r => r.offset === offset);
      need(matches.length === 1 && matches[0].type === 1, 'one R_386_32 relocation at each slot');
      const relocation = matches[0], symbols = elf.symbols.filter(s => s.table === relocation.symbolTable && s.index === relocation.symbolIndex);
      need(symbols.length === 1, 'real relocation symbol table and index');
      const symbol = symbols[0], targetSection = elf.sections[symbol.section], addend = bytes.readUInt32LE(section.offset + offset), target = symbol.value + addend;
      need(targetSection && symbol.section > 0 && Number.isInteger(symbol.value) && target <= 0xffffffff && target >= 0 && target < targetSection.size, 'initializer target inside a real object section');
      const functions = elf.symbols.filter(s => s.type === 2 && s.section === symbol.section && s.value === target && typeof s.raw === 'string' && s.raw.startsWith('_GLOBAL__sub_I_'));
      need(functions.length === 1, 'one real constructor function for symbol plus addend');
      result.push({ section: section.name, offset, addend, relocation_symbol: symbol.raw || targetSection.name, relocation_symbol_type: symbol.type, target_section: symbol.section, target_offset: target, constructor: functions[0].raw });
    }
  }
  return result;
}

export function inspectImageToolEvidence(evidence) {
  const e = evidence || {}, expected = e.expected || {}, inner = e.inner || {}, child = inner.child || {}, products = e.products || {}, checks = [];
  const check = (name, ok) => checks.push({ name, ok: ok === true });
  const pin = p => p && Number.isSafeInteger(p.bytes) && p.bytes >= 0 && typeof p.sha256 === 'string' && /^[0-9a-f]{64}$/.test(p.sha256);
  check('known native category', IMAGE_NATIVE_KINDS.includes(e.kind));
  check('nonempty exact expected argv', Array.isArray(expected.argv) && expected.argv.length > 0 && expected.argv.every(v => typeof v === 'string'));
  check('spec and inner use expected argv', same(e.spec?.cmd, expected.argv) && same(inner.argv, expected.argv));
  check('real child exited zero without timeout', inner.child !== null && typeof inner.child === 'object' && child.returncode === 0 && child.timed_out === false);
  check('landlock applied', inner.landlock?.ok === true);
  const probes = ['inside_work_root_write_allowed', 'loader_readable', 'negative_cat_denied', 'negative_true_denied', 'outside_source_write_denied', 'outside_tmp_write_denied', 'positive_python_allowed', 'private_shm_write_allowed'];
  check('all eight required probes observed true', probes.every(k => inner.probe_checks?.[k] === true) && Object.values(inner.probe_checks || {}).every(v => v === true));
  check('both process group measurements and no leftovers', child.group_measured === true && child.group_measured_final === true && Array.isArray(child.descendants_left) && child.descendants_left.length === 0 && Array.isArray(child.killed) && child.killed.length === 0);
  check('actual raw bytes and hash match child', pin(e.raw) && e.raw.requested_path === child.output_path && e.raw.bytes === child.output_bytes && e.raw.bytes === child.output_file_bytes && e.raw.sha256 === child.output_sha256);
  const want = e.kind === 'owner_assembly' ? ['object'] : e.kind === 'static_image_link' ? ['elf', 'map'] : [];
  check('no undeclared product or depfile', Object.keys(products).every(k => want.includes(k)) && expected.depfile === undefined);
  for (const key of want) {
    const requested = key === 'object' ? expected.output : expected[key], option = key === 'map' ? '-Map' : '-o', argv = expected.argv || [], positions = argv.flatMap((v, i) => v === option ? [i] : []);
    check(key + ' obligation is explicit and matches argv', native(requested) && requested.endsWith(key === 'object' ? '.o' : key === 'elf' ? '.elf' : '.map') && positions.length === 1 && argv[positions[0] + 1] === requested);
    check(key + ' actual file was read and pinned', pin(products[key]) && products[key].bytes > 0 && products[key].requested_path === requested);
  }
  if (want.length === 0) check('metadata declares no product paths', expected.output === undefined && expected.elf === undefined && expected.map === undefined);
  return { ok: checks.every(x => x.ok), kind: e.kind || null, checks, failed: checks.filter(x => !x.ok).map(x => x.name), target_execution_proved: false };
}
