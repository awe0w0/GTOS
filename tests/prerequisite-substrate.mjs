// GTOS-DSH-139 T: build the fixture substrate the prerequisite consumer test reads.
//
// The point of this script is that the substrate is real. Every file a fixture record points at is written here from bytes
// that exist, and every pin is computed from those bytes after they are written. A test that hand-wrote a hash and a size
// would be testing its own arithmetic; a test that reads a real file and recomputes the hash is testing the consumer.
//
// The substrate is deliberately shaped like this packet's real chain — the same kinds, the same roles, the same six read
// roles on the link record — because the consumer's job is to read that chain, and a fixture chain of a different shape
// would exercise different code.
//
// Everything is written once, under flag 'wx', into a fresh directory. The directory is created here and its absence is
// required, so a second run cannot quietly reuse the first run's bytes.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

export const TASK = 'GTOS-DSH-139';

// A small deterministic byte stream, so a fixture file has content that is stable across runs and still not a run of
// identical bytes (which would make a truncation test ambiguous).
function blob(seed, n) {
  const out = Buffer.alloc(n);
  let h = crypto.createHash('sha256').update(seed).digest();
  for (let i = 0; i < n; i += 32) {
    h.copy(out, i, 0, Math.min(32, n - i));
    h = crypto.createHash('sha256').update(h).digest();
  }
  return out;
}

// An ELF with a real multiboot header at offset 0, so the static-image consumer's own scan finds what the record claims.
function multibootElf(seed, size) {
  const b = blob(seed, size);
  // e_ident: 0x7F 'ELF', 32-bit, little endian, version 1.
  b.write('\x7fELF', 0, 'binary');
  b[4] = 1; b[5] = 1; b[6] = 1;
  // e_type = ET_EXEC (2), e_machine = EM_386 (3).
  b.writeUInt16LE(2, 16);
  b.writeUInt16LE(3, 18);
  // The multiboot header: magic, flags 7, and a checksum that makes the three words sum to zero.
  const off = 64;
  const magic = 0x1BADB002;
  const flags = 7;
  b.writeUInt32LE(magic, off);
  b.writeUInt32LE(flags, off + 4);
  b.writeUInt32LE((-(magic + flags)) >>> 0, off + 8);
  return { bytes: b, offset: off };
}

// A depfile with a real first rule and `-MP` stubs after it, which is what the compiler actually emits.
function depfile(target, sources) {
  const line = target + ': ' + sources.join(' ');
  const stubs = sources.map((s) => s + ':').join('\n');
  return line + '\n\n' + stubs + '\n';
}

export function buildSubstrate(root) {
  if (fs.existsSync(root)) throw new Error('the fixture root already exists: ' + root);
  fs.mkdirSync(root, { recursive: true });
  const put = (rel, bytes) => {
    const p = path.join(root, rel);
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, bytes, { flag: 'wx' });
    return { path: p, bytes: bytes.length, sha256: sha256(bytes) };
  };
  const putJson = (rel, obj) => put(rel, Buffer.from(JSON.stringify(obj, null, 2) + '\n', 'utf8'));

  const F = {};

  // ---------------------------------------------------------------- sources and headers
  F.sources = [];
  for (const name of ['gdt.cpp', 'runtime.cpp', 'objects.cpp']) {
    F.sources.push({ name: 'src/' + name, ...put('src/' + name, blob('src-' + name, 900)) });
  }
  F.headers = [];
  for (const name of ['gdt.h', 'runtime.h']) {
    F.headers.push({ name: 'include/' + name, ...put('include/' + name, blob('hdr-' + name, 300)) });
  }

  // ---------------------------------------------------------------- the order, frozen before the build
  const orderPath = 'evidence/producer-order.jsonl';
  const orderRows = [
    { seq: 1, event: 'spec-written', stage: 't-01', at: '2026-10-27T01:00:00.000Z' },
    { seq: 2, event: 'run-started', stage: 't-01', at: '2026-10-27T01:00:05.000Z' },
  ];
  F.order = put(orderPath, Buffer.from(orderRows.map((r) => JSON.stringify(r)).join('\n') + '\n', 'utf8'));
  F.orderRows = orderRows;

  // The build began after the order was frozen. These two instants are the relation the consumer checks; they are not a
  // boolean the contract writes about itself.
  F.frozenAt = '2026-10-27T00:59:00.000Z';
  F.buildStartedAt = '2026-10-27T01:00:05.000Z';

  // ---------------------------------------------------------------- the objects and their depfiles
  F.slots = [];
  for (let i = 1; i <= 61; i += 1) {
    const slot = 'slot' + String(i).padStart(2, '0');
    const srcRel = 'src/slot' + String(i).padStart(2, '0') + '.cpp';
    const src = put(srcRel, blob('slot-src-' + i, 200));
    const obj = put('obj/' + slot + '.o', blob('slot-obj-' + i, 512));
    const dep = put('obj/' + slot + '.d', Buffer.from(
      depfile('obj/' + slot + '.o', [path.join(root, srcRel), path.join(root, 'include/gdt.h')]), 'utf8'));
    F.slots.push({ slot, source: src, object: obj, depfile: dep });
  }

  // ---------------------------------------------------------------- the producer closure
  F.individuals = 61;
  const provenance = F.slots.map((s) => ({ slot: s.slot, object: s.object }));

  // ---------------------------------------------------------------- the image
  const elfMb = multibootElf('final-image', 745748);
  F.elf = put('image/default.elf', elfMb.bytes);
  F.multibootOffset = elfMb.offset;
  F.map = put('image/default.map', blob('final-map', 4096));

  // ---------------------------------------------------------------- the link's six documents
  F.spec = putJson('chain/spec.json', { task: TASK, stage: 'lk01-01', route: 'ld', control: {} });
  F.terminal = putJson('chain/result-lk01-01-139.json',
    { task: TASK, stage: 'lk01-01', exit_code: 0, timed_out: false, ok: true });
  F.attempt = putJson('chain/attempt-lk01-01-139.json',
    { task: TASK, stage: 'lk01-01', route: 'ld', reserved: true });
  F.helperReport = putJson('chain/helper-lk01-01.json', { ok: true, stage: 'lk01-01', produced: 'image/default.elf' });
  F.bridge = putJson('chain/bridge-lk01-01.json', { ok: true, kind: 'ld', argv_tokens: 12 });
  F.innerReport = putJson('chain/inner-lk01-01.json', { ok: true, exit_code: 0 });
  F.raw = put('chain/raw-lk01-01.log', blob('raw-log', 1200));

  // ---------------------------------------------------------------- the host units
  F.hostElf = put('host/safe-tss.elf', blob('safe-tss-elf', 17240));
  F.hostElf2 = put('host/runtime.elf', blob('runtime-elf', 5365276));

  // ---------------------------------------------------------------- the tools
  F.tools = [];
  for (const [role, name] of [['runner', 'run-139-r01.mjs'], ['guard', 'guard.py'], ['compiler', 'g++'],
    ['assembler', 'as'], ['linker', 'ld.bfd'], ['emulator', 'qemu-system-i386'],
    ['loader', 'ld-linux-x86-64.so.2']]) {
    F.tools.push({ role, ...put('tools/' + name, blob('tool-' + name, 4000)) });
  }

  // ---------------------------------------------------------------- the runtime tree
  F.runtimeFiles = [];
  let runtimeBytes = 0;
  for (let i = 0; i < 12; i += 1) {
    const rel = 'runtime/root/usr/lib/file' + i;
    const pin = put(rel, blob('runtime-' + i, 1000 + i));
    F.runtimeFiles.push(pin);
    runtimeBytes += pin.bytes;
  }
  // The two files the guest cannot run without, at their real names.
  const emu = put('runtime/root/usr/bin/qemu-system-i386', blob('qemu', 3000));
  const ldr = put('runtime/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', blob('ld', 2000));
  F.runtimeFiles.push(emu, ldr);
  runtimeBytes += emu.bytes + ldr.bytes;
  F.runtimeManifest = putJson('runtime/manifest.json', { files: F.runtimeFiles });
  F.runtimeBytes = runtimeBytes;

  // ---------------------------------------------------------------- the ten role records
  //
  // Each is built from the pins above, which were computed from the bytes written above. Nothing here is a literal hash.
  F.records = {};

  F.records.source_profile_contract = {
    task: TASK, kind: 'producer_closure_contract',
    sources: F.sources.map((s) => ({ path: s.path, bytes: s.bytes, sha256: s.sha256 })),
    headers: F.headers.map((h) => ({ path: h.path, bytes: h.bytes, sha256: h.sha256 })),
    order: F.orderRows,
    order_from: { path: F.order.path, bytes: F.order.bytes, sha256: F.order.sha256, frozen_at: F.frozenAt },
    frozen_before_build: true,
    contract_sha256: sha256(Buffer.from('contract-139', 'utf8')),
  };

  F.records.producer_chain = {
    task: TASK, kind: 'producer_closure',
    individuals: F.individuals, rebuilt: 61, forwarded: 0,
    provenance, contract_sha256: sha256(Buffer.from('contract-139', 'utf8')),
  };

  F.records.dependency_chain = {
    task: TASK, kind: 'link_input_read',
    slots_read: 61, slots_expected: 61, all_present: true, all_relocatable_i386: true,
    inputs: F.slots.map((s) => ({ slot: s.slot, object: s.object, depfile: s.depfile })),
    summary: { depfiles_present: 61, depfiles_with_missing_sources: [] },
    contract_sha256: sha256(Buffer.from('contract-139', 'utf8')),
  };

  const contractSha = sha256(Buffer.from('contract-139', 'utf8'));
  F.records.link_input_consumption = {
    task: TASK, kind: 'entry_link',
    contract_sha256: contractSha,
    record: {
      stage: 'lk01-01', contract: F.spec.path, refusal: null,
      read_set: {
        roles: ['spec', 'terminal', 'helper_report', 'bridge', 'inner_report', 'raw'],
        pins: {
          spec: F.spec, terminal: F.terminal, helper_report: F.helperReport,
          bridge: F.bridge, inner_report: F.innerReport, raw: F.raw,
        },
      },
      elf: F.elf, map: F.map,
    },
  };

  F.records.typed_terminal_validation = {
    task: TASK, kind: 'default_link', contract_sha256: contractSha, image_sha256: F.elf.sha256,
    terminal: { stage: 'lk01-01', exit_code: 0, timed_out: false,
      result_path: F.terminal.path, attempt_path: F.attempt.path },
    helper_report: F.helperReport,
  };

  F.records.default_link = {
    task: TASK, kind: 'default_link', contract_sha256: contractSha, image_sha256: F.elf.sha256,
    elf: F.elf, link_exit: 0, image_exists: true,
  };

  F.records.static_image = {
    task: TASK, kind: 'static_image_inspection', contract_sha256: contractSha, image_sha256: F.elf.sha256,
    elf_type: 2, machine: 3, interp_present: false, dynamic_present: false,
    multiboot: { offset: F.multibootOffset, flags: 7 },
    entry: '0x00100000', script_symbols: ['_start'], inspection_passed: true, elf: F.elf,
  };

  F.records.related_host_tests = {
    task: TASK, kind: 'host_unit_execution', contract_sha256: contractSha, image_sha256: F.elf.sha256,
    units: [
      { role: 'safe_tss', elf: F.hostElf, exit_code: 0,
        cases: Array.from({ length: 16 }, (_, i) => 'safe case ' + (i + 1)) },
      { role: 'runtime', elf: F.hostElf2, exit_code: 0,
        cases: Array.from({ length: 10 }, (_, i) => 'runtime case ' + (i + 1)) },
    ],
    summary: { all_passed: true, safe_tss_cases_named: 16, runtime_cases_passed: '10/10' },
  };

  F.records.tooling_subject = {
    task: TASK, kind: 'tool_pin',
    subjects: F.tools.map((t) => ({ role: t.role, path: t.path, bytes: t.bytes, sha256: t.sha256 })),
  };

  F.records.runtime_environment = {
    task: TASK, kind: 'runtime_materialization',
    files: F.runtimeFiles.length, bytes: F.runtimeBytes,
    manifest: F.runtimeManifest, all_match: true,
  };

  // ---------------------------------------------------------------- write them out
  F.roleFiles = {};
  for (const [role, rec] of Object.entries(F.records)) {
    F.roleFiles[role] = putJson('records/' + role + '.json', rec);
  }
  F.root = root;
  return F;
}

// The member list a caller hands to the gate: role, the record's path, and the record's hash as the caller read it.
export function membersFrom(F) {
  return Object.entries(F.roleFiles).map(([role, pin]) => ({ role, path: pin.path, sha256: pin.sha256 }));
}

if (process.argv[2] === '--build') {
  const F = buildSubstrate(process.argv[3]);
  console.log(JSON.stringify({ root: F.root, roles: Object.keys(F.roleFiles).length,
    slots: F.slots.length, runtime_files: F.runtimeFiles.length, runtime_bytes: F.runtimeBytes,
    elf: F.elf.sha256, contract: F.records.producer_chain.contract_sha256 }, null, 2));
}
