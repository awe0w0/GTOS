import fs from 'node:fs';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
const c = process.argv[2] ? JSON.parse(fs.readFileSync(process.argv[2], 'utf8')) : {};
const subject = c.module || { path: fileURLToPath(new URL('../tools/static-image-plan.mjs', import.meta.url)) };
const pin = p => { const b = fs.readFileSync(p); return { bytes: b.length, sha256: crypto.createHash('sha256').update(b).digest('hex') }; };
const before = pin(subject.path); if (subject.sha256 && before.sha256 !== subject.sha256) throw Error('actual subject pin');
const api = await import(pathToFileURL(subject.path).href), checks = [];
const need = (ok, name) => { checks.push({ ok: !!ok, name }); if (!ok) throw Error(name); };
const rejects = (fn, name) => { let error = null; try { fn(); } catch (e) { error = e.message; } need(typeof error === 'string', name); };
const copy = v => JSON.parse(JSON.stringify(v));
const L = '/var/tmp/gtos-image-plan-fixture', S = '/var/tmp/gtos-image-view-fixture';
const tools = { python_realpath: '/usr/bin/python3.8', loader: '/usr/lib/x86_64-linux-gnu/ld-2.31.so', as: '/usr/bin/x86_64-linux-gnu-as', 'ld.bfd': '/usr/bin/x86_64-linux-gnu-ld.bfd' };
const loader = L + '/objects/loader-01/loader.asm.o', output = L + '/image/image-01/provider-static.elf', map = L + '/image/image-01/provider-static.map', script = S + '/view/linker.ld';
const inherited = Array.from({ length: 59 }, (_, i) => L + '/inherited/part' + i + '.o');
const ctx = { work_root: L, source_root: S, tools, selected_loader: loader, native_choices: {
  'loader-01': { category: 'owner_assembly', cwd: S + '/view', source: 'src/loader.s', output: loader, argv: [tools.as, '--32', 'src/loader.s', '-o', loader] },
  'image-01': { category: 'static_image_link', cwd: L, output, map, linker_script: script, prefix: [tools['ld.bfd'], '-melf_i386', '-T', script, '-Map', map, '-o', output], inherited_inputs: inherited }
} };
const as = api.planImageRequest(ctx, 'loader-01', 'owner_assembly');
const ld = api.planImageRequest(ctx, 'image-01', 'static_image_link');
need(JSON.stringify(as.argv) === JSON.stringify([tools.as, '--32', 'src/loader.s', '-o', loader]) && as.outputs.length === 1, 'exact original loader source, five tokens and one output');
need(ld.argv.length === 68 && JSON.stringify(ld.argv.slice(8)) === JSON.stringify([...inherited, loader]) && JSON.stringify(ld.outputs) === JSON.stringify([output, map]), 'exact ordered sixty individuals and both final outputs');
need(!ld.argv.includes('-r') && !ld.argv.includes('--no-undefined'), 'final request contains neither partial link nor extra flags');
const meta = [tools.python_realpath, '-B', '-S', L + '/frozen-helpers/meta-01.py'];
need(api.planImageRequest(ctx, 'meta-01', 'native_metadata', meta).outputs.length === 0, 'metadata has no invented output obligation');
for (const kind of api.IMAGE_NATIVE_KINDS) need(JSON.stringify(api.imageExecSet(kind, tools)) === JSON.stringify([tools.python_realpath, tools.loader, ...(kind === 'native_metadata' ? [] : [kind === 'owner_assembly' ? tools.as : tools['ld.bfd']])]), 'actual exact EXEC set: ' + kind);
rejects(() => api.imageExecSet('owner_assembly', { ...tools, as: '/usr/bin/as\0hidden' }), 'NUL executable path refused');
rejects(() => api.planImageRequest(ctx, 'meta-01', 'native_metadata', [tools.python_realpath, '-B', '-S', L + '/frozen-helpers/bad\0.py']), 'NUL metadata path refused');
rejects(() => api.imageExecSet('object_compile', tools), 'compiler category receives no executable grant');
rejects(() => api.imageExecSet('owner_assembly', { ...tools, as: tools.loader }), 'duplicate executable refused');
rejects(() => api.imageExecSet('static_image_link', { ...tools, 'ld.bfd': null }), 'missing executable refused');
rejects(() => api.planImageRequest(ctx, '8loader-01', 'owner_assembly'), 'illegal stage grammar refused');
rejects(() => api.planImageRequest(ctx, 'loader-01', 'native_metadata', meta), 'product stage cannot masquerade as metadata');
rejects(() => api.planImageRequest(ctx, 'loader-01', 'object_compile'), 'ungranted category refused');
rejects(() => api.planImageRequest(ctx, 'missing-01', 'owner_assembly'), 'unregistered stage refused');
for (const [name, mutate] of [
  ['changed loader source', x => { x.native_choices['loader-01'].source = 'src/other.s'; }],
  ['changed loader flag', x => { x.native_choices['loader-01'].argv[1] = '--64'; }],
  ['stale loader output', x => { x.native_choices['loader-01'].output = L + '/objects/old.o'; }],
  ['changed final script', x => { x.native_choices['image-01'].prefix[3] = S + '/other.ld'; }],
  ['changed map output', x => { x.native_choices['image-01'].map = L + '/other.map'; }],
  ['duplicate inherited object', x => { x.native_choices['image-01'].inherited_inputs[1] = inherited[0]; }],
  ['missing inherited object', x => { x.native_choices['image-01'].inherited_inputs.pop(); }],
  ['input outside inherited root', x => { x.native_choices['image-01'].inherited_inputs[0] = '/old/part.o'; }],
  ['aggregate instead of individual', x => { x.native_choices['image-01'].inherited_inputs[0] = L + '/inherited/provider-fiftynine-rel.o'; }],
  ['unregistered current loader', x => { x.selected_loader = L + '/objects/other.o'; }]
]) { const x = copy(ctx); mutate(x); rejects(() => api.planImageRequest(x, name.includes('loader') && !name.includes('current') ? 'loader-01' : 'image-01', name.includes('loader') && !name.includes('current') ? 'owner_assembly' : 'static_image_link'), name + ' refused'); }
const reordered = [...ld.argv]; [reordered[8], reordered[9]] = [reordered[9], reordered[8]];
rejects(() => api.planImageRequest(ctx, 'image-01', 'static_image_link', reordered), 'caller reordering rejected against frozen plan');
rejects(() => api.planImageRequest(ctx, 'loader-01', 'owner_assembly', [...as.argv.slice(0, 4), L + '/objects/stale.o']), 'caller stale output rejected');
rejects(() => api.planImageRequest(ctx, 'meta-01', 'native_metadata', [tools.python_realpath, '-B', '-S', L + '/draft.py']), 'metadata draft execution rejected');

const probes = Object.fromEntries(['inside_work_root_write_allowed', 'loader_readable', 'negative_cat_denied', 'negative_true_denied', 'outside_source_write_denied', 'outside_tmp_write_denied', 'positive_python_allowed', 'private_shm_write_allowed'].map(k => [k, true]));
const hash = '1'.repeat(64), product = requested_path => ({ requested_path, bytes: 12, sha256: hash });
function evidence(plan) { return { kind: plan.kind, expected: plan.expected, spec: { cmd: plan.argv }, inner: { argv: plan.argv, landlock: { ok: true }, probe_checks: probes, child: { returncode: 0, timed_out: false, group_measured: true, group_measured_final: true, descendants_left: [], killed: [], output_path: '/capture.raw', output_bytes: 0, output_file_bytes: 0, output_sha256: hash } }, raw: { requested_path: '/capture.raw', bytes: 0, sha256: hash }, products: plan.kind === 'owner_assembly' ? { object: product(loader) } : plan.kind === 'static_image_link' ? { elf: product(output), map: product(map) } : {} }; }
for (const p of [as, ld, api.planImageRequest(ctx, 'meta-01', 'native_metadata', meta)]) need(api.inspectImageToolEvidence(evidence(p)).ok, 'actual evidence obligations accept ' + p.kind);
for (const [name, mutate] of [
  ['missing map product', x => { delete x.products.map; }], ['missing explicit map', x => { delete x.expected.map; }],
  ['missing ELF product', x => { delete x.products.elf; }], ['invented depfile', x => { x.products.depfile = product('/invented.d'); }],
  ['raw identity mismatch', x => { x.raw.sha256 = '2'.repeat(64); }], ['missing probe', x => { delete x.inner.probe_checks.negative_cat_denied; }],
  ['unmeasured final process group', x => { x.inner.child.group_measured_final = false; }], ['leftover process', x => { x.inner.child.descendants_left = [3]; }],
  ['cleanup killed process', x => { x.inner.child.killed = [3]; }], ['child never started', x => { x.inner.child = null; }],
  ['tool error', x => { x.inner.child.returncode = 1; }], ['landlock not applied', x => { x.inner.landlock.ok = false; }],
  ['changed cmd0', x => { x.spec.cmd[0] = '/other/ld'; }], ['timeout', x => { x.inner.child.timed_out = true; }]
]) { const x = copy(evidence(ld)); mutate(x); need(!api.inspectImageToolEvidence(x).ok, name + ' fails real evidence function'); }

let realEntries = [];
if (c.object && c.reader) {
  need(pin(c.object.path).sha256 === c.object.sha256 && pin(c.reader.path).sha256 === c.reader.sha256, 'sealed object and actual ET_REL reader pins');
  const { readElf32 } = await import(pathToFileURL(c.reader.path).href), bytes = fs.readFileSync(c.object.path), parsed = readElf32(bytes);
  realEntries = api.resolveInitializerEntries(bytes, parsed);
  need(realEntries.length === 3 && JSON.stringify(realEntries.map(x => x.constructor)) === JSON.stringify(c.expected_initializers), 'three real SECTION plus addend targets have independently specified ordered owners');
  need(JSON.stringify(realEntries.map(x => x.addend)) === '[0,176,192]' && realEntries.every(x => x.relocation_symbol === '.text.startup' && x.relocation_symbol_type === 3), 'real addends read from object bytes, not absent property names');
  for (const [name, mutate] of [
    ['missing relocation symbol index', x => { delete x.relocations.find(r => r.targetSection === x.sections.find(s => s.name === '.init_array').index).symbolIndex; }],
    ['wrong relocation kind', x => { x.relocations.find(r => r.targetSection === x.sections.find(s => s.name === '.init_array').index).type = 2; }],
    ['duplicate slot relocation', x => { x.relocations.push(copy(x.relocations.find(r => r.targetSection === x.sections.find(s => s.name === '.init_array').index))); }],
    ['missing constructor definition', x => { x.symbols = x.symbols.filter(s => !s.raw.startsWith('_GLOBAL__sub_I_')); }],
    ['unknown priority semantics', x => { x.sections.push({ name: '.init_array.100', size: 4 }); }],
    ['wrong parsed byte identity', x => { x.bytes--; }]
  ]) { const x = copy(parsed); mutate(x); rejects(() => api.resolveInitializerEntries(bytes, x), name + ' refused'); }
  rejects(() => api.resolveInitializerEntries(bytes, { sections: parsed.sections, ordered_initializers: [{ offset: 0, symbol: null, addend: null }] }), '103 offset-only null ownership cannot pass resolver');
  const corrupt = Buffer.from(bytes), array = parsed.sections.find(s => s.name === '.init_array'); corrupt.writeUInt32LE(0xffffffff, array.offset);
  rejects(() => api.resolveInitializerEntries(corrupt, parsed), 'out of section addend refused');
}
let stable = true; for (let i = 0; i < 512; i++) stable = stable && JSON.stringify(api.planImageRequest(ctx, 'image-01', 'static_image_link')) === JSON.stringify(ld);
need(stable && pin(subject.path).sha256 === before.sha256, '512 requests preserve exact plan and module bytes');
const result = { task: c.task || 'static-image-plan', ok: true, assertions: checks.length, failed: 0, stability_cycles: 512, module: { path: subject.path, ...before }, real_project_object_read: realEntries.length === 3, real_initializer_entries: realEntries, native_tools_started: 0, target_execution: false, checks };
if (c.result_path) fs.writeFileSync(c.result_path, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ ok: true, assertions: checks.length, real_project_object_read: result.real_project_object_read, stability_cycles: 512 }));
