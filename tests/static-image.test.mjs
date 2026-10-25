import fs from 'node:fs';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
const c = process.argv[2] ? JSON.parse(fs.readFileSync(process.argv[2], 'utf8')) : {};
const subject = c.module || { path: fileURLToPath(new URL('../tools/static-image.mjs', import.meta.url)) };
const hash = () => crypto.createHash('sha256').update(fs.readFileSync(subject.path)).digest('hex');
const before = hash(); if (subject.sha256 && subject.sha256 !== before) throw Error('actual module pin');
const { inspectStaticImage } = await import(pathToFileURL(subject.path).href);
const checks = [];
const need = (ok, name) => { checks.push({ ok: !!ok, name }); if (!ok) throw Error(name); };
const names = ['_GLOBAL__sub_I_fixture_a', '_GLOBAL__sub_I_fixture_b'];
// TEST_ONLY ELF bytes, never launched. These encode no model of kernel behaviour.
function fixture() {
  const b = Buffer.alloc(0x1600), ph = 52, sh = 0x1400;
  b.set([0x7f, 69, 76, 70, 1, 1, 1]); b.writeUInt16LE(2, 16); b.writeUInt16LE(3, 18); b.writeUInt32LE(1, 20);
  b.writeUInt32LE(0x100110, 24); b.writeUInt32LE(ph, 28); b.writeUInt32LE(sh, 32); b.writeUInt16LE(52, 40); b.writeUInt16LE(32, 42); b.writeUInt16LE(1, 44); b.writeUInt16LE(40, 46); b.writeUInt16LE(7, 48); b.writeUInt16LE(6, 50);
  [1, 0, 0x100000, 0x100000, 0x1008, 0x2000, 7, 4096].forEach((v, i) => b.writeUInt32LE(v, ph + 4 * i));
  b.writeUInt32LE(0x1badb002, 0x100); b.writeUInt32LE(7, 0x104); b.writeUInt32LE((-(0x1badb002 + 7)) >>> 0, 0x108);
  b.writeUInt32LE(0x100114, 0x1000); b.writeUInt32LE(0x100118, 0x1004);
  const strings = ['loader', 'kernel_start', 'kernel_readonly_start', 'kernel_readonly_end', 'kernel_end', 'start_ctors', 'end_ctors', ...names, 'missing'];
  const offsets = {}; let st = '\0'; for (const s of strings) { offsets[s] = Buffer.byteLength(st); st += s + '\0'; } b.write(st, 0x1100);
  const sectionNames = ['', '.text', '.data', '.bss', '.strtab', '.symtab', '.shstrtab']; let sn = ''; const no = {}; for (const s of sectionNames) { no[s] = Buffer.byteLength(sn); sn += s + '\0'; } b.write(sn, 0x1300);
  const section = (i, name, type, flags, address, offset, size, link = 0, entry = 0) => { const at = sh + 40 * i; [no[name], type, flags, address, offset, size, link, 0, 4, entry].forEach((v, n) => b.writeUInt32LE(v, at + 4 * n)); };
  section(0, '', 0, 0, 0, 0, 0); section(1, '.text', 1, 6, 0x100100, 0x100, 0x20); section(2, '.data', 1, 3, 0x101000, 0x1000, 8); section(3, '.bss', 8, 3, 0x101008, 0x1008, 0x200); section(4, '.strtab', 3, 0, 0, 0x1100, Buffer.byteLength(st)); section(5, '.symtab', 2, 0, 0, 0x1200, 11 * 16, 4, 16); section(6, '.shstrtab', 3, 0, 0, 0x1300, Buffer.byteLength(sn));
  const symbols = [['loader', 0x100110, 1, 2], ['kernel_start', 0x100000, 0xfff1, 0], ['kernel_readonly_start', 0x100000, 0xfff1, 0], ['kernel_readonly_end', 0x101000, 0xfff1, 0], ['kernel_end', 0x102000, 0xfff1, 0], ['start_ctors', 0x101000, 2, 0], ['end_ctors', 0x101008, 2, 0], [names[0], 0x100114, 1, 2], [names[1], 0x100118, 1, 2], ['missing', 0x100120, 1, 2]];
  symbols.forEach(([name, value, section, type], i) => { const at = 0x1200 + 16 * (i + 1); b.writeUInt32LE(offsets[name], at); b.writeUInt32LE(value, at + 4); b[at + 12] = ((name.startsWith('_GLOBAL') || name === 'missing' ? 0 : 1) << 4) | type; b.writeUInt16LE(section, at + 14); });
  return b;
}
const expected = { expectedConstructors: names };
const original = fixture(), originalHash = crypto.createHash('sha256').update(original).digest('hex');
const good = inspectStaticImage(original, expected);
need(good.entry === 0x100110 && good.script_symbols.kernel_end === 0x102000, 'entry and script addresses read from actual bytes');
need(good.constructors.length === 2 && good.constructors[1].target === 0x100118, 'constructor pointers use mapped final bytes');
need(good.final_initializer_addresses_validated && good.constructors_executed === false && good.target_execution === false && good.init_ready === 'unknown', 'address evidence never becomes execution');
need(good.executable_stack_requested === null && good.writable_executable_loads === 1, 'missing stack policy and writable executable load are reported');
need(originalHash === crypto.createHash('sha256').update(original).digest('hex'), 'input buffer never mutated');
function rejects(name, mutate, options = expected) { const b = fixture(); mutate(b); let error = null; try { inspectStaticImage(b, options); } catch (e) { error = e.message; } need(typeof error === 'string', name); }
rejects('ET_REL rejected', b => b.writeUInt16LE(1, 16));
rejects('wrong machine rejected', b => b.writeUInt16LE(62, 18));
rejects('wrong endianness rejected', b => { b[5] = 2; });
rejects('program table overflow rejected', b => b.writeUInt32LE(0xfffffff0, 28));
rejects('section table overflow rejected', b => b.writeUInt32LE(0xfffffff0, 32));
rejects('unsupported header stride rejected', b => b.writeUInt16LE(31, 42));
rejects('INTERP rejected', b => b.writeUInt32LE(3, 52));
rejects('dynamic segment rejected', b => b.writeUInt32LE(2, 52));
rejects('file size larger than memory rejected', b => b.writeUInt32LE(1, 52 + 20));
rejects('load virtual address overflow rejected', b => b.writeUInt32LE(0xfffff000, 52 + 8));
rejects('load physical address overflow rejected', b => b.writeUInt32LE(0xfffff000, 52 + 12));
rejects('non-power-of-two load alignment rejected', b => b.writeUInt32LE(3, 52 + 28));
rejects('allocated section wrong file mapping rejected', b => b.writeUInt32LE(0x104, 0x1400 + 40 + 16));
rejects('NOBITS beyond load memory rejected', b => b.writeUInt32LE(0x4000, 0x1400 + 3 * 40 + 20));
rejects('invalid symbol section rejected', b => b.writeUInt16LE(8, 0x1210 + 14));
rejects('symbol table invalid linked section rejected', b => b.writeUInt32LE(1, 0x1400 + 5 * 40 + 24));
rejects('symbol table invalid entry size rejected', b => b.writeUInt32LE(15, 0x1400 + 5 * 40 + 36));
const undefine = (b, binding) => { b[0x1200 + 10 * 16 + 12] = binding; b.writeUInt16LE(0, 0x1200 + 10 * 16 + 14); };
  rejects('strong undefined rejected', b => undefine(b, 16));
  rejects('local undefined rejected', b => undefine(b, 0));
  rejects('nameless undefined rejected', b => { undefine(b, 16); b.writeUInt32LE(0, 0x1200 + 10 * 16); });
rejects('weak undefined rejected', b => undefine(b, 32));
rejects('missing GLOBAL script name rejected', b => { b[0x1200 + 2 * 16 + 12] = 0; });
rejects('entry differs from loader rejected', b => b.writeUInt32LE(0x100114, 24));
rejects('entry outside executable load rejected', b => b.writeUInt32LE(6, 52 + 24));
rejects('misaligned readonly boundary rejected', b => b.writeUInt32LE(0x101001, 0x1200 + 4 * 16 + 4));
rejects('reversed ctor range rejected', b => b.writeUInt32LE(0x101010, 0x1200 + 6 * 16 + 4));
rejects('unaligned ctor range rejected', b => b.writeUInt32LE(0x101007, 0x1200 + 7 * 16 + 4));
rejects('ctor range points into NOBITS rejected', b => { b.writeUInt32LE(0x101008, 0x1200 + 6 * 16 + 4); b.writeUInt32LE(0x101010, 0x1200 + 7 * 16 + 4); });
rejects('ctor pointer outside executable load rejected', b => b.writeUInt32LE(0x500000, 0x1000));
rejects('ctor pointer without function definition rejected', b => b.writeUInt32LE(0x100119, 0x1000));
rejects('wrong ordered expected owners rejected', () => {}, { expectedConstructors: [...names].reverse() });
rejects('duplicate expected owners rejected', () => {}, { expectedConstructors: [names[0], names[0]] });
rejects('wrong expected kernel base rejected', () => {}, { expectedKernelStart: 0x200000 });
rejects('bad multiboot checksum rejected', b => b.writeUInt32LE(1, 0x108));
rejects('changed multiboot flags rejected even with good checksum', b => { b.writeUInt32LE(3, 0x104); b.writeUInt32LE((-(0x1badb002 + 3)) >>> 0, 0x108); });

rejects('overlapping load virtual ranges rejected', b => { b.writeUInt16LE(2, 44); [1, 0x100, 0x100100, 0x100100, 0x20, 0x20, 5, 1].forEach((v, i) => b.writeUInt32LE(v, 84 + i * 4)); });
rejects('missing load rejected', b => b.writeUInt32LE(4, 52));
rejects('remaining relocation section rejected', b => b.writeUInt32LE(9, 0x1400 + 40 + 4));
rejects('bad symbol string bounds rejected', b => b.writeUInt32LE(1, 0x1400 + 4 * 40 + 20));
rejects('unallocated COMMON symbol rejected', b => b.writeUInt16LE(0xfff2, 0x1200 + 10 * 16 + 14));
for (const [flags, requested] of [[7, true], [6, false]]) {
  const b = fixture(); b.writeUInt16LE(2, 44); [0x6474e551, 0, 0, 0, 0, 0, flags, 4].forEach((v, i) => b.writeUInt32LE(v, 84 + i * 4));
  need(inspectStaticImage(b, expected).executable_stack_requested === requested, 'stack policy reported exactly: ' + requested);
}
for (const input of [Buffer.alloc(0), Buffer.alloc(51), null, {}]) { let refused = false; try { inspectStaticImage(input); } catch { refused = true; } need(refused, 'invalid or truncated input rejected'); }
let stable = true; for (let cycle = 0; cycle < 256; cycle++) stable = stable && JSON.stringify(inspectStaticImage(original, expected)) === JSON.stringify(good);
need(stable && hash() === before, '256 stable reads preserve module and output');
const result = { task: c.task || 'static-image', ok: true, assertions: checks.length, failed: 0, stability_cycles: 256, module: { path: subject.path, bytes: fs.statSync(subject.path).size, sha256: before }, fixtures_are_elf_bytes_only: true, project_image_inspected: false, target_execution: false, checks };
if (c.result_path) fs.writeFileSync(c.result_path, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ ok: true, assertions: checks.length, stability_cycles: 256 }));
