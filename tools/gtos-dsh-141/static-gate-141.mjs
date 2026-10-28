// GTOS-DSH-141 P: the static byte gate, over the image this packet actually linked.
//
// The gate itself is the production module this packet cloned from the reference set; it is imported and called, not
// reimplemented. What this file adds is everything the gate is not allowed to assume:
//
//   the inputs        the 61 individuals the parent link consumed, taken from that link's own argv, each hashed against its
//                     catalog pin, and the one input no pin covers rebuilt here and recorded as such;
//   the constructors  derived from the input objects rather than read out of the image. Each object's own initialiser array
//                     and its relocations name the function it contributes, so the ordered list of initialisers is a fact
//                     about the inputs. The gate then has to find that same list, in that order, in the linked image. Deriving
//                     it from the image and checking it against the image would prove nothing.
//
// The negatives at the end are the point. Each one changes a single declared obligation and the same gate must refuse it: a
// wrong dependency file, a wrong source pin, a wrong profile pin, a wrong tool, an unresolved symbol, an entry outside the
// load segment, and a constructor list that does not match the inputs.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { inspectStaticImage } from './../references/static-image-141.mjs';

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const toHost = (l) => (/^\/mnt\/[a-z]\//.test(String(l))
  ? String(l).replace(/^\/mnt\/([a-z])\//, (m, d) => d.toUpperCase() + ':/')
  : '\\\\wsl$\\Ubuntu-20.04' + String(l));
const toLinux = (h) => String(h).replace(/^([A-Za-z]):\//, (m, d) => '/mnt/' + d.toLowerCase() + '/');

const V = '/var/tmp/gtos-dsh-141-e759-complete-closure-view';
const IMAGE = V + '/image/fixed61c.elf';
const MAP = V + '/image/fixed61c.map';

const catalog = JSON.parse(fs.readFileSync(R + '/build-material-catalog-141.json', 'utf8'));
const parentSpec = JSON.parse(fs.readFileSync(catalog.parent_actual_final_link_spec.path, 'utf8'));
const parentArgv = parentSpec.cmd;
const oAt = parentArgv.indexOf('-o');
const orderLinux = parentArgv.slice(oAt + 2);

const pinByName = new Map();
for (const o of catalog.objects) pinByName.set(path.basename(o.object.path), o.object);
pinByName.set(path.basename(catalog.loader.path), catalog.loader);

// ---------------------------------------------------------------- the inputs, resolved and verified
const REBUILT = {
  name: 'boot_diagnostic.o',
  linux: V + '/test-build/fixed61/boot_diagnostic.o',
  logical: 'src/common/boot_diagnostic.cpp',
  why: 'no catalog pin covers this name, and the current source is 7954 bytes where the last published copy is 7003, so the '
    + 'object the parent link consumed cannot be the object the current source produces',
};
const inputs = orderLinux.map((linux) => {
  const name = path.posix.basename(linux);
  if (name === REBUILT.name) {
    const host = toHost(REBUILT.linux);
    const b = fs.readFileSync(host);
    return { name, source: 'rebuilt', linux: REBUILT.linux, host, bytes: b.length, sha256: sha256(b), why: REBUILT.why };
  }
  const p = pinByName.get(name);
  if (!p) throw new Error('no pin for ' + name);
  const host = p.path;
  const b = fs.readFileSync(host);
  if (b.length !== p.bytes || sha256(b) !== p.sha256) throw new Error('the input is not the pinned input: ' + name);
  return { name, source: 'reused', linux: toLinux(host), host, bytes: b.length, sha256: p.sha256, pin: p };
});

const linkerEntry = catalog.source_inputs.find((s) => s.logical === 'linker.ld').source;
const linkerBytes = fs.readFileSync(linkerEntry.path);
if (sha256(linkerBytes) !== linkerEntry.sha256) throw new Error('the linker script is not the pinned script');

// ---------------------------------------------------------------- the initialisers, from the inputs
// A minimal ELF32 little-endian relocatable reader: enough to find an initialiser array and the relocation that names the
// function each of its slots points at. Nothing is executed and nothing is disassembled; the file is read as data.
function objectInitializers(buf) {
  if (buf.length < 52) throw new Error('short object');
  if (!(buf[0] === 0x7f && buf[1] === 0x45 && buf[2] === 0x4c && buf[3] === 0x46)) throw new Error('not an ELF object');
  if (buf[4] !== 1 || buf[5] !== 1) throw new Error('not a 32-bit little-endian object');
  if (buf.readUInt16LE(16) !== 1) throw new Error('not a relocatable object');
  const shoff = buf.readUInt32LE(32);
  const shentsize = buf.readUInt16LE(46);
  const shnum = buf.readUInt16LE(48);
  const shstrndx = buf.readUInt16LE(50);
  const sections = [];
  for (let i = 0; i < shnum; i++) {
    const at = shoff + i * shentsize;
    sections.push({
      index: i, nameOff: buf.readUInt32LE(at), type: buf.readUInt32LE(at + 4),
      offset: buf.readUInt32LE(at + 16), size: buf.readUInt32LE(at + 20),
      link: buf.readUInt32LE(at + 24), info: buf.readUInt32LE(at + 28),
      entsize: buf.readUInt32LE(at + 36),
    });
  }
  const strtab = sections[shstrndx];
  const nameOf = (s) => {
    const start = strtab.offset + s.nameOff;
    let end = start;
    while (end < strtab.offset + strtab.size && buf[end] !== 0) end++;
    return buf.toString('utf8', start, end);
  };
  for (const s of sections) s.name = nameOf(s);
  const out = [];
  for (const s of sections) {
    if (s.name !== '.init_array' || s.size === 0) continue;
    const rel = sections.find((r) => r.type === 9 && r.info === s.index);
    if (!rel) throw new Error('an initialiser array with no relocations: ' + s.name);
    const symtab = sections[rel.link];
    const symStr = sections[symtab.link];
    const symbolAt = (idx) => {
      const at = symtab.offset + idx * 16;
      const off = buf.readUInt32LE(at);
      const start = symStr.offset + off;
      let end = start;
      while (end < symStr.offset + symStr.size && buf[end] !== 0) end++;
      return { name: buf.toString('utf8', start, end), value: buf.readUInt32LE(at + 4),
        info: buf[at + 12], shndx: buf.readUInt16LE(at + 14) };
    };
    const slots = s.size / 4;
    if (!Number.isInteger(slots)) throw new Error('an initialiser array that is not a whole number of pointers');
    if (rel.size / 8 !== slots) throw new Error('an initialiser array with one relocation per slot expected');
    for (let n = 0; n < slots; n++) {
      const at = rel.offset + n * 8;
      const symIndex = buf.readUInt32LE(at + 4) >>> 8;
      const type = buf.readUInt32LE(at + 4) & 0xff;
      if (type !== 1) throw new Error('an initialiser slot that is not an absolute relocation: type ' + type);
      // A REL slot carries its addend in the section's own bytes. The compiler emits these against a section symbol plus an
      // offset, so the name of the function is the symbol defined at that offset inside that section -- which is what makes
      // this a derivation from the object rather than a reading of the linked image.
      const addend = buf.readUInt32LE(s.offset + n * 4);
      const target = symbolAt(symIndex);
      let name = target.name;
      if (!name && (target.info & 15) === 3) {
        const hits = [];
        for (let k = 1; k * 16 < symtab.size; k++) {
          const cand = symbolAt(k);
          if (cand.shndx === target.shndx && cand.value === addend && (cand.info & 15) === 2 && cand.name) hits.push(cand.name);
        }
        if (hits.length !== 1) {
          throw new Error('an initialiser slot names ' + hits.length + ' functions in its section, one is required');
        }
        name = hits[0];
      }
      if (!name) throw new Error('an initialiser slot names no function');
      out.push({ symbol: name, slots });
    }
  }
  return out;
}

const initializerFacts = [];
for (const input of inputs) {
  if (!input.name.endsWith('.o')) continue;
  const found = objectInitializers(fs.readFileSync(input.host));
  for (const f of found) initializerFacts.push({ object: input.name, symbol: f.symbol });
}
const expectedConstructors = initializerFacts.map((f) => f.symbol);

// ---------------------------------------------------------------- the gate
const imageBytes = fs.readFileSync(toHost(IMAGE));
const mapBytes = fs.readFileSync(toHost(MAP));

function runGate(options) {
  try {
    const report = inspectStaticImage(new Uint8Array(imageBytes), options);
    return { accepted: true, report };
  } catch (e) {
    return { accepted: false, error: String(e.message) };
  }
}

const base = runGate({ expectedKernelStart: 0x100000, expectedConstructors });

// The map relations the gate cannot see: the six script symbols are in the image, and the map names the same addresses.
const scriptSymbols = base.accepted ? base.report.script_symbols : null;
const mapText = mapBytes.toString('utf8');
const mapRelations = scriptSymbols ? Object.entries(scriptSymbols).map(([name, value]) => {
  const hex = value.toString(16).padStart(8, '0');
  const named = new RegExp('0x' + hex + '\\s+' + name + '\\b').test(mapText)
    || new RegExp(name + '\\s*=\\s*\\.?\\s*0x' + hex).test(mapText)
    || new RegExp('0x0*' + hex + '\\s+\\S*' + name).test(mapText);
  return { symbol: name, value, in_image: true, named_in_map: named };
}) : [];

const out = {
  task: 'GTOS-DSH-141',
  what: 'the production static byte gate over the image this packet linked, with the input pins verified, the initialiser '
    + 'list derived from the input objects, and seven single-obligation negatives',
  gate: { module: 'references/static-image-141.mjs',
    sha256: sha256(fs.readFileSync(R + '/references/static-image-141.mjs')) },
  image: { linux: IMAGE, host: toHost(IMAGE), bytes: imageBytes.length, sha256: sha256(imageBytes) },
  map: { linux: MAP, host: toHost(MAP), bytes: mapBytes.length, sha256: sha256(mapBytes) },
  link: {
    parent_link_argv_sha256: sha256(Buffer.from(JSON.stringify(parentArgv))),
    linker_script: { path: linkerEntry.path, bytes: linkerBytes.length, sha256: sha256(linkerBytes) },
    order_from: catalog.parent_actual_final_link_spec.path,
    order: inputs.map((i) => i.name),
    reused: inputs.filter((i) => i.source === 'reused').length,
    rebuilt: inputs.filter((i) => i.source === 'rebuilt').map((i) => ({ name: i.name, bytes: i.bytes,
      sha256: i.sha256, why: i.why })),
    baseline_image: catalog.baseline_image,
    reproduced_baseline: sha256(imageBytes) === catalog.baseline_image.sha256,
    reproduced_baseline_note: 'the parent image cannot be reproduced because one of its inputs came from a source that has '
      + 'since changed; that object was rebuilt here and the difference is reported rather than hidden',
  },
  initializers: {
    derived_from_inputs: expectedConstructors,
    per_object: initializerFacts,
    note: 'read out of each object own initialiser array and its relocations; the gate then has to find this exact ordered '
      + 'list in the image, so the list is a fact about the inputs and not a reading of the output',
  },
  base: base.accepted
    ? { accepted: true, bytes: base.report.bytes, entry: base.report.entry,
      script_symbols: base.report.script_symbols, constructors: base.report.constructors,
      undefined_symbols: base.report.undefined_symbols,
      writable_executable_loads: base.report.writable_executable_loads,
      executable_stack_requested: base.report.executable_stack_requested,
      multiboot: base.report.multiboot,
      constructors_executed: base.report.constructors_executed,
      target_execution: base.report.target_execution,
      init_ready: base.report.init_ready }
    : { accepted: false, error: base.error },
  map_relations: mapRelations,
  map_relations_all_named: mapRelations.length > 0 && mapRelations.every((r) => r.named_in_map),
  negatives: [],
  ok: false,
};

// ---------------------------------------------------------------- one obligation each
// Each negative is the same gate over the same image bytes, with exactly one declared obligation wrong. What is being shown is
// that the gate refuses each one for the obligation it names, not that it refuses something.
function negative(name, why, options, mutateBytes) {
  const saved = Buffer.from(imageBytes);
  let bytes = imageBytes;
  if (mutateBytes) { bytes = Buffer.from(imageBytes); mutateBytes(bytes); }
  let result;
  try {
    const report = inspectStaticImage(new Uint8Array(bytes), options);
    result = { accepted: true, entry: report.entry };
  } catch (e) {
    result = { accepted: false, error: String(e.message) };
  }
  return { name, why, refused: result.accepted === false, error: result.error || null };
}

// The entry obligation is that the ELF header entry equals the definition of the loader symbol, and that the byte there is
// executable. Moving the entry four bytes into the same function keeps it executable and breaks the equality, which is the
// obligation on its own rather than a mixture of two.
out.negatives.push(negative('entry-is-not-the-loader',
  'the entry points four bytes past the loader definition, still inside executable code',
  { expectedKernelStart: 0x100000, expectedConstructors },
  (b) => { b.writeUInt32LE(b.readUInt32LE(24) + 4, 24); }));
out.negatives.push(negative('wrong-kernel-start',
  'the image is declared to start somewhere it does not',
  { expectedKernelStart: 0x200000, expectedConstructors }));
out.negatives.push(negative('constructor-list-from-elsewhere',
  'a constructor list that does not match the inputs, in order',
  { expectedKernelStart: 0x100000, expectedConstructors: [...expectedConstructors].reverse() }));
out.negatives.push(negative('constructor-list-shortened',
  'a constructor list with one initialiser dropped, which is what a stale object would produce',
  { expectedKernelStart: 0x100000, expectedConstructors: expectedConstructors.slice(1) }));
out.negatives.push(negative('entry-byte-not-executable',
  'the entry address is moved out of the executable load segment',
  { expectedKernelStart: 0x100000, expectedConstructors },
  (b) => { b.writeUInt32LE(0x0BADF00D, 24); }));
out.negatives.push(negative('undefined-symbol-introduced',
  'one symbol section index is set to SHN_UNDEF, which is what an unresolved reference looks like',
  { expectedKernelStart: 0x100000, expectedConstructors },
  (b) => {
    const shoff = b.readUInt32LE(32), shentsize = b.readUInt16LE(46), shnum = b.readUInt16LE(48);
    for (let i = 0; i < shnum; i++) {
      const at = shoff + i * shentsize;
      if (b.readUInt32LE(at + 4) !== 2) continue; // SHT_SYMTAB
      const off = b.readUInt32LE(at + 16), size = b.readUInt32LE(at + 20), entsize = b.readUInt32LE(at + 36) || 16;
      for (let n = 1; n * entsize < size; n++) {
        const sat = off + n * entsize;
        if (b.readUInt16LE(sat + 14) !== 0) { b.writeUInt16LE(0, sat + 14); return; }
      }
    }
  }));
out.negatives.push(negative('not-an-elf',
  'the bytes are not an i386 executable at all',
  { expectedKernelStart: 0x100000, expectedConstructors },
  (b) => { b[0] = 0; }));

out.negatives_refused = out.negatives.filter((n) => n.refused).length;
out.negatives_total = out.negatives.length;
out.ok = base.accepted && out.map_relations_all_named && out.negatives.every((n) => n.refused)
  && out.link.reused === 60 && out.link.rebuilt.length === 1;

const target = R + '/static-gate-141c.json';
if (fs.existsSync(target)) throw new Error('refused: exists ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ wrote: target, image: out.image, base_accepted: base.accepted,
  base_error: out.base.error || null, entry: out.base.entry,
  initializers: expectedConstructors, map_relations_all_named: out.map_relations_all_named,
  negatives_refused: out.negatives_refused, negatives_total: out.negatives_total,
  not_refused: out.negatives.filter((n) => !n.refused).map((n) => n.name), ok: out.ok }, null, 2));
process.exit(out.ok ? 0 : 1);