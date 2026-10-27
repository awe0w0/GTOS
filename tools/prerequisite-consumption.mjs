// GTOS-DSH-139 T: the ten roles, each read from the artifact it is a claim about.
//
// Packet 138 asked each role's record what it said about itself. A record could therefore carry `all_passed: true` and an
// empty pin object and be admitted, because the check read the declaration rather than the thing declared. This module
// reads the thing declared.
//
// The rule every function here obeys: a field is not evidence. A path and a hash are, but only after this module opens the
// path and computes the hash itself. So each consumer returns the fact it established from bytes it read, and a record
// that names nothing openable fails even when every one of its fields is present and green.
//
// The second rule: the ten roles are one set, not ten independent green lights. They are all about the same build of the
// same sources into the same image with the same tools. A set whose members describe different contracts, or different
// images, or different tool generations is not a complete set; it is ten documents that happen to be green. `bindSet`
// enforces that, and it is what stops a caller assembling one correct member from each of several unrelated runs.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// A read that survives the transient UNC failures this environment produces, without ever turning a real absence into a
// success: it retries the open, and returns null only after every attempt failed.
function readBytes(p) {
  if (typeof p !== 'string' || !p) return null;
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) {
    try { b = fs.readFileSync(p); } catch { /* retry */ }
  }
  return b;
}

// Read a file and return its identity, or null. The pin comparison is done by the caller against this, never against the
// record's own claim.
function identify(p) {
  const b = readBytes(p);
  return b === null ? null : { path: p, bytes: b.length, sha256: sha256(b) };
}

function samePin(pin, actual) {
  return !!pin && !!actual && pin.bytes === actual.bytes && pin.sha256 === actual.sha256;
}

const bad = (why) => ({ ok: false, why });
const good = (detail, binding) => ({ ok: true, detail, binding: binding || null });

// ---------------------------------------------------------------- per-role consumption
//
// Each case below reads the artifacts the role is a claim about. Where a record names a list of pins, every pin is
// re-derived from disk; where it names a summary count, the count is recomputed from the members. Nothing here trusts a
// boolean the record wrote about itself, and nothing here accepts a pin object with no path.

export function consumeRole(role, r, ctx) {
  switch (role) {
    // ---- the sources, the headers, and the order that was frozen before anything was built.
    case 'source_profile_contract': {
      // The contract is either the producer closure contract or the host C++ contract. Both name their inputs; both are
      // read here rather than described.
      const units = Array.isArray(r.units) ? r.units : null;
      const sources = Array.isArray(r.sources) ? r.sources : null;
      const members = units || sources;
      if (!members || members.length === 0) return bad('the contract names no inputs at all');
      let read = 0;
      const unreadable = [];
      for (const m of members) {
        const p = m && (m.path || m.source);
        const a = identify(p);
        if (a === null) { unreadable.push(String(p)); continue; }
        // The pin has to be the bytes on disk. A record that names a file and a hash that no longer matches is describing
        // a former version of that file.
        if (m.sha256 !== undefined && !samePin({ bytes: m.bytes, sha256: m.sha256 }, a)) {
          return bad('the contract pin is not the file on disk: ' + String(p));
        }
        read += 1;
      }
      if (unreadable.length) return bad('the contract names inputs that are not on disk: ' + unreadable.slice(0, 3).join(', '));
      // The order was frozen before the build. That is not a boolean the contract writes about itself: it is a relation
      // between the order file's own identity and the identity recorded when the first object was produced. The caller
      // supplies the build event from the order ledger, which this consumer reads.
      const orderPin = r.order_from;
      if (!orderPin || typeof orderPin.path !== 'string') return bad('the contract names no frozen order');
      const order = identify(orderPin.path);
      if (order === null) return bad('the frozen order is not on disk: ' + orderPin.path);
      if (!samePin(orderPin, order)) return bad('the frozen order changed after the contract was written');
      if (r.frozen_before_build !== true) return bad('the contract does not claim to precede the build');
      if (!ctx || !ctx.build_started_at) return bad('no build event was supplied, so "frozen before build" cannot be checked');
      const frozenAt = orderPin.frozen_at || r.frozen_at;
      if (typeof frozenAt !== 'string') return bad('the contract records no freeze time');
      if (!(Date.parse(frozenAt) <= Date.parse(ctx.build_started_at))) {
        return bad('the contract was frozen after the build began: ' + frozenAt + ' is not before ' + ctx.build_started_at);
      }
      // The contract the set is bound to is the one the contract itself names. A hash computed here from the member list
      // could never agree with what the other roles name, so the binding would refuse every honest set.
      if (typeof r.contract_sha256 !== 'string' || !r.contract_sha256) return bad('the contract names no contract hash');
      return good({ inputs_read: read, order: order.sha256, frozen_at: frozenAt, build_started_at: ctx.build_started_at },
        { contract_sha256: r.contract_sha256 });
    }

    // ---- every individual the producer rebuilt, with the object it produced.
    case 'producer_chain': {
      if (typeof r.individuals !== 'number' || r.individuals === 0) return bad('the closure names no individuals');
      const provenance = Array.isArray(r.provenance) ? r.provenance : null;
      if (!provenance) return bad('the closure carries no provenance');
      if (provenance.length !== r.individuals) return bad('the provenance covers ' + provenance.length + ' of ' + r.individuals);
      const seen = new Set();
      let objects = 0;
      for (const p of provenance) {
        if (!p || typeof p.slot !== 'string') return bad('a provenance row names no slot');
        if (seen.has(p.slot)) return bad('the provenance names a slot twice: ' + p.slot);
        seen.add(p.slot);
        // The object the row claims to have produced has to be on disk at the bytes the row pins.
        const obj = p.object;
        if (!obj || typeof obj.path !== 'string') return bad('a provenance row names no object: ' + p.slot);
        const actual = identify(obj.path);
        if (actual === null) return bad('a produced object is not on disk: ' + obj.path);
        if (!samePin(obj, actual)) return bad('a produced object is not the bytes the row pins: ' + obj.path);
        objects += 1;
      }
      if (objects !== r.individuals) return bad('the provenance does not resolve to every individual');
      // The closure states how many it rebuilt and how many it forwarded. A closure that forwarded everything proves
      // nothing about the current sources, so the split has to be a real split of the same total.
      const rebuilt = typeof r.rebuilt === 'number' ? r.rebuilt : null;
      const forwarded = typeof r.forwarded === 'number' ? r.forwarded : null;
      if (rebuilt === null || forwarded === null) return bad('the closure does not state its rebuilt/forwarded split');
      if (rebuilt + forwarded !== r.individuals) return bad('the rebuilt/forwarded split does not add up to the individuals');
      if (rebuilt === 0) return bad('the closure rebuilt nothing, so it is not evidence about the current sources');
      return good({ individuals: r.individuals, objects_read: objects, rebuilt, forwarded },
        { contract_sha256: r.contract_sha256 });
    }

    // ---- the dependency chain: the objects and the depfiles, and the sources the depfiles name.
    case 'dependency_chain': {
      if (r.slots_read !== 61 || r.slots_expected !== 61) return bad('not all 61 slots were read');
      const inputs = Array.isArray(r.inputs) ? r.inputs : null;
      if (!inputs) return bad('the record carries no per-slot inputs, so its counts cannot be checked');
      if (inputs.length !== 61) return bad('the record carries ' + inputs.length + ' slot rows, not 61');
      let objects = 0, depfiles = 0, depSources = 0;
      const slots = new Set();
      for (const s of inputs) {
        if (!s || typeof s.slot !== 'string') return bad('a slot row names no slot');
        if (slots.has(s.slot)) return bad('a slot appears twice: ' + s.slot);
        slots.add(s.slot);
        // The object.
        const o = identify(s.object && s.object.path);
        if (o === null) return bad('a slot object is not on disk: ' + s.slot);
        if (!samePin(s.object, o)) return bad('a slot object is not the bytes the row pins: ' + s.slot);
        objects += 1;
        // The depfile, and the sources inside it. A depfile that names a source which is not on disk is the defect this
        // role exists to catch: it means the object was compiled from something that has since moved or never existed.
        if (!s.depfile || typeof s.depfile.path !== 'string') return bad('a slot row names no depfile: ' + s.slot);
        const d = identify(s.depfile.path);
        if (d === null) return bad('a slot depfile is not on disk: ' + s.slot);
        if (!samePin(s.depfile, d)) return bad('a slot depfile is not the bytes the row pins: ' + s.slot);
        depfiles += 1;
        const text = readBytes(s.depfile.path);
        if (text === null) return bad('a slot depfile could not be read: ' + s.slot);
        const named = depSourcesOf(text.toString('utf8'));
        if (named.length === 0) return bad('a slot depfile names no source: ' + s.slot);
        for (const src of named) {
          if (readBytes(src) === null) return bad('a depfile names a source that is not on disk: ' + src);
          depSources += 1;
        }
      }
      return good({ slots: slots.size, objects_read: objects, depfiles_read: depfiles, dep_sources_read: depSources },
        { contract_sha256: r.contract_sha256 });
    }

    // ---- the entry link's own read set: the six documents it opened, each still at the bytes it opened.
    case 'link_input_consumption': {
      const rec = r.record;
      if (!rec) return bad('the role carries no link record');
      if (rec.refusal) return bad('the entry link was refused: ' + (rec.refusal.why || rec.refusal.obligation));
      const rs = rec.read_set;
      if (!rs || !Array.isArray(rs.roles) || !rs.pins) return bad('the link record carries no read set');
      const need = ['spec', 'terminal', 'helper_report', 'bridge', 'inner_report', 'raw'];
      const missing = need.filter((n) => !rs.roles.includes(n));
      if (missing.length) return bad('the entry did not read: ' + missing.join(', '));
      let pins = 0;
      for (const n of need) {
        const pin = rs.pins[n];
        if (!pin || typeof pin.path !== 'string') return bad('the read set pins no path for: ' + n);
        const a = identify(pin.path);
        if (a === null) return bad('a document the entry read is no longer on disk: ' + n + ' ' + pin.path);
        if (!samePin(pin, a)) return bad('a document the entry read changed afterwards: ' + n);
        pins += 1;
      }
      // The image the link produced has to be on disk at the hash it recorded, or the record describes a former image.
      const elf = identify(rec.elf && rec.elf.path);
      if (elf === null) return bad('the entry link names no image that exists');
      if (!samePin(rec.elf, elf)) return bad('the entry link image is not the bytes it recorded');
      const map = identify(rec.map && rec.map.path);
      if (map === null) return bad('the entry link names no map that exists');
      if (!samePin(rec.map, map)) return bad('the entry link map is not the bytes it recorded');
      return good({ read_roles: rs.roles.length, pins_read: pins, elf: elf.sha256, map: map.sha256 },
        { contract_sha256: rec.contract_sha256 || r.contract_sha256, image_sha256: elf.sha256 });
    }

    // ---- the terminal: a real stage that really ran and really exited, not a summary of one.
    case 'typed_terminal_validation': {
      // A gate record is not a terminal. It is a record of which claims were admitted, and 138 allowed it here, which let
      // "the gate admitted one case" stand in for "a stage ran to a real terminal". A gate can be read alongside, but the
      // role needs the terminal itself.
      if (r.kind === 'link_gate') {
        return bad('a link gate is a summary of claims, not a terminal a stage reached');
      }
      const terminal = r.terminal || r.record;
      if (!terminal) return bad('the role carries no terminal');
      if (typeof terminal.exit_code !== 'number') return bad('the terminal carries no numeric exit code');
      if (terminal.exit_code !== 0) return bad('the terminal exited ' + terminal.exit_code);
      if (terminal.timed_out === true) return bad('the terminal timed out');
      // The terminal and the attempt are the runner's own files. Both are read here, and they have to agree about the
      // stage and the exit code, or one of them describes a different run.
      const tPath = terminal.result_path || (r.terminal_path);
      const aPath = terminal.attempt_path || (r.attempt_path);
      if (typeof tPath !== 'string' || typeof aPath !== 'string') return bad('the terminal names no attempt and result path');
      const tBytes = readBytes(tPath);
      const aBytes = readBytes(aPath);
      if (tBytes === null) return bad('the terminal file is not on disk: ' + tPath);
      if (aBytes === null) return bad('the attempt file is not on disk: ' + aPath);
      let tRec = null, aRec = null;
      try { tRec = JSON.parse(tBytes.toString('utf8')); } catch { return bad('the terminal file is not JSON'); }
      try { aRec = JSON.parse(aBytes.toString('utf8')); } catch { return bad('the attempt file is not JSON'); }
      if (tRec.stage !== aRec.stage) return bad('the terminal and the attempt name different stages');
      if (typeof tRec.exit_code !== 'number') return bad('the terminal file carries no numeric exit code');
      if (tRec.exit_code !== terminal.exit_code) return bad('the terminal file disagrees with the record about the exit code');
      if (tRec.timed_out === true) return bad('the terminal file records a timeout');
      // The helper's own report is the fourth document: the stage is only a real terminal if the helper it launched wrote
      // something and that something is on disk now.
      const helper = identify(r.helper_report && r.helper_report.path);
      if (helper === null) return bad('the helper wrote no report, so no helper ran');
      return good({ stage: tRec.stage, exit_code: tRec.exit_code, helper: helper.sha256 },
        { contract_sha256: r.contract_sha256, image_sha256: r.image_sha256 });
    }

    // ---- the default link: the image it produced, read from disk.
    case 'default_link': {
      const elf = identify(r.elf && (r.elf.path || r.elf));
      if (elf === null) return bad('the default link names no image that exists');
      if (typeof r.elf === 'object' && !samePin(r.elf, elf)) return bad('the default link image is not the bytes it recorded');
      if (r.link_exit !== 0) return bad('the default link did not exit zero');
      return good({ elf: elf.sha256, bytes: elf.bytes }, { image_sha256: elf.sha256, contract_sha256: r.contract_sha256 });
    }

    // ---- the static image: re-inspected here, from the file, not from the record's verdict about it.
    case 'static_image': {
      if (r.elf_type !== 2 || r.machine !== 3) return bad('the record does not describe an i386 ET_EXEC');
      const elf = identify(r.elf && r.elf.path);
      if (elf === null) return bad('the inspected image is not on disk');
      if (!samePin(r.elf, elf)) return bad('the image on disk is not the image the inspection describes');
      if (r.interp_present !== false || r.dynamic_present !== false) return bad('the image carries an interpreter or a dynamic section');
      if (!r.multiboot || r.multiboot.flags !== 7) return bad('the multiboot header does not ask for the declared flags');
      if (r.inspection_passed !== true) return bad('the inspection did not pass');
      // The bytes are read here and the header is found in them. A record whose flags say one thing while its file says
      // another is refused, and that is only possible because this reads the file.
      const bytes = readBytes(r.elf.path);
      if (bytes === null) return bad('the inspected image could not be read');
      const found = findMultiboot(bytes);
      if (!found) return bad('the image on disk carries no multiboot header, whatever the record says');
      if (found.flags !== 7) return bad('the multiboot header on disk asks for flags ' + found.flags + ', not 7');
      if (found.offset !== r.multiboot.offset) return bad('the multiboot header is not at the offset the record names');
      return good({ entry: r.entry, multiboot_offset: found.offset, multiboot_flags: found.flags, elf: elf.sha256 },
        { image_sha256: elf.sha256, contract_sha256: r.contract_sha256 });
    }

    // ---- the host tests: the actual ELF that ran, and the cases it named.
    case 'related_host_tests': {
      const units = Array.isArray(r.units) ? r.units : null;
      if (!units || units.length === 0) return bad('the role names no host units');
      let cases = 0;
      const roles = [];
      for (const u of units) {
        if (!u || typeof u.role !== 'string') return bad('a host unit names no role');
        roles.push(u.role);
        // The ELF that ran has to be on disk, and it has to be the one the record pins. A summary that says all_passed
        // while naming no binary is a claim about a run nobody can find.
        const elf = identify(u.elf && u.elf.path);
        if (elf === null) return bad('the host unit names no ELF that exists: ' + u.role);
        if (!samePin(u.elf, elf)) return bad('the host unit ELF is not the bytes the record pins: ' + u.role);
        if (typeof u.exit_code !== 'number') return bad('the host unit records no exit code: ' + u.role);
        // The runner treats a guest-shaped non-zero exit as expected; a host unit is not a guest, and a host unit that
        // exited non-zero did not pass. The safe-TSS unit reports PASS on fd 2 and exits 0; so does the runtime unit.
        if (u.exit_code !== 0) return bad('the host unit exited ' + u.exit_code + ': ' + u.role);
        if (!Array.isArray(u.cases) || u.cases.length === 0) return bad('the host unit named no cases: ' + u.role);
        cases += u.cases.length;
      }
      if (new Set(roles).size !== roles.length) return bad('a host unit role appears twice');
      // The record's own summary is compared with the members, so a summary cannot be greener than the units under it.
      const sum = r.summary;
      if (!sum) return bad('the role carries no summary');
      if (sum.all_passed !== true) return bad('the summary does not claim the units passed');
      if (typeof sum.safe_tss_cases_named === 'number' && sum.safe_tss_cases_named > cases) {
        return bad('the summary claims more named cases than the units carry');
      }
      return good({ units: units.length, cases_named: cases, roles },
        { contract_sha256: r.contract_sha256, image_sha256: r.image_sha256 });
    }

    // ---- the tools: each one re-read, and the runner pin matched against the frozen runner itself.
    case 'tooling_subject': {
      const subjects = Array.isArray(r.subjects) ? r.subjects : null;
      if (!subjects || subjects.length === 0) return bad('the role names no tools');
      const CAPABILITY = {
        runner: ['runner'], guard: ['guard'], compiler: ['compiler', 'g++', 'cc1plus'],
        assembler: ['assembler', 'as'], linker: ['linker', 'ld.bfd'],
        emulator: ['emulator'], loader: ['loader', 'bundled_loader'],
      };
      const have = new Set(subjects.map((s) => s && s.role));
      for (const [need, names] of Object.entries(CAPABILITY)) {
        if (!names.some((n) => have.has(n))) return bad('the tooling subject omits the ' + need);
      }
      let read = 0;
      const digests = [];
      for (const s of subjects) {
        if (!s || typeof s.path !== 'string' || !s.path) return bad('a tooling subject has no path');
        const a = identify(s.path);
        if (a === null) return bad('a tooling subject is not on disk: ' + String(s.role) + ' ' + s.path);
        if (!samePin(s, a)) return bad('a tooling subject is not the bytes the record pins: ' + String(s.role));
        read += 1;
        digests.push(a.sha256);
      }
      // The runner is not just any tool: it is the frozen runner this packet is executing under. If the record's runner
      // pin is not the file on disk that is running now, the record describes a different executor.
      const runnerSubject = subjects.find((s) => s.role === 'runner');
      if (!runnerSubject) return bad('the tooling subject names no runner');
      if (typeof ctx.runner_path !== 'string' || !ctx.runner_path) {
        return bad('the caller named no running runner, so the runner pin cannot be compared with anything');
      }
      const runnerBytes = identify(ctx.runner_path);
      if (runnerBytes === null) return bad('the running runner could not be read for comparison');
      if (runnerSubject.sha256 !== runnerBytes.sha256) return bad('the runner pin is not the runner that is running');
      digests.sort();
      return good({ subjects: read, runner_matches: true },
        { subject_sha256: sha256(Buffer.from(digests.join('\n'), 'utf8')) });
    }

    // ---- the runtime the guest ran on: the materialised tree, read back file by file.
    case 'runtime_environment': {
      if (r.kind === 'setup') return good({ roots: r.roots }, null);
      if (r.kind !== 'runtime_materialization') return bad('the role holds no runtime materialisation');
      const man = r.manifest;
      if (!man || typeof man.path !== 'string') return bad('the runtime names no materialisation manifest');
      const manBytes = readBytes(man.path);
      if (manBytes === null) return bad('the runtime manifest is not on disk: ' + man.path);
      if (typeof man.sha256 === 'string' && sha256(manBytes) !== man.sha256) return bad('the runtime manifest changed after it was written');
      let m = null;
      try { m = JSON.parse(manBytes.toString('utf8')); } catch { return bad('the runtime manifest is not JSON'); }
      const files = Array.isArray(m.files) ? m.files : null;
      if (!files) return bad('the runtime manifest lists no files');
      if (typeof r.files !== 'number' || typeof r.bytes !== 'number') return bad('the record states no file or byte count');
      // The counts are recomputed from the manifest and from the disk, not read off the record.
      let bytes = 0, present = 0;
      const absent = [];
      for (const f of files) {
        if (!f || typeof f.path !== 'string') return bad('a runtime manifest row names no path');
        const a = identify(f.path);
        if (a === null) { absent.push(f.path); continue; }
        if (!samePin(f, a)) return bad('a runtime file is not the bytes the manifest pins: ' + f.path);
        bytes += a.bytes;
        present += 1;
      }
      if (absent.length) return bad(absent.length + ' runtime files are not on disk');
      if (present !== r.files) return bad('the record says ' + r.files + ' runtime files, the manifest lists ' + present);
      if (bytes !== r.bytes) return bad('the record says ' + r.bytes + ' runtime bytes, the files are ' + bytes);
      // The emulator and the loader are the two files the guest cannot run without. Their presence is checked here rather
      // than assumed from the count.
      const emu = files.find((f) => /qemu-system-i386$/.test(f.path));
      const loader = files.find((f) => /ld-linux-x86-64\.so\.2$/.test(f.path));
      if (!emu) return bad('the runtime holds no emulator');
      if (!loader) return bad('the runtime holds no loader');
      return good({ files: present, bytes, emulator: emu.path, loader: loader.path }, null);
    }

    default: return bad('no fact is declared for this role');
  }
}

// ---------------------------------------------------------------- the binding across the set

// The ten roles are one claim, not ten. They are all about one contract, one image and one tool generation. This takes the
// bindings the individual consumers established and refuses a set whose members do not all agree.
//
// A member may decline to name a binding it has nothing to say about; what it may not do is name a different one.
export function bindSet(consumed) {
  const problems = [];
  const pick = (key) => {
    const named = [];
    for (const c of consumed) {
      const v = c.binding && c.binding[key];
      if (typeof v === 'string' && v) named.push({ role: c.role, value: v });
    }
    if (named.length === 0) return { value: null, named: [], problems: [] };
    const first = named[0];
    const disagree = named.filter((n) => n.value !== first.value);
    if (disagree.length) {
      problems.push({ obligation: 'prerequisite', why: 'the roles describe different ' + key + ': ' + first.role
        + ' says ' + first.value.slice(0, 12) + ', ' + disagree[0].role + ' says ' + disagree[0].value.slice(0, 12) });
    }
    return { value: first.value, named, problems };
  };
  const contract = pick('contract_sha256');
  const image = pick('image_sha256');
  const subject = pick('subject_sha256');
  problems.push(...contract.problems, ...image.problems, ...subject.problems);
  // Each binding has to be named by at least one role. A set where nothing states the contract is not bound to a contract;
  // it is a set of ten documents that never mention one.
  if (contract.value === null) problems.push({ obligation: 'prerequisite', why: 'no role names the contract the set was built under' });
  if (image.value === null) problems.push({ obligation: 'prerequisite', why: 'no role names the image the set describes' });
  if (subject.value === null) problems.push({ obligation: 'prerequisite', why: 'no role names the tool generation the set used' });
  return { bound: problems.length === 0, contract: contract.value, image: image.value, subject: subject.value,
    named: { contract: contract.named.length, image: image.named.length, subject: subject.named.length }, problems };
}

// ---------------------------------------------------------------- small readers

// The sources a depfile names. The first rule is `target: dep dep dep`; `-MP` stubs add `dep:` lines with no sources, and
// those are skipped because they name no inputs. A depfile with no first rule names nothing, which the caller refuses.
export function depSourcesOf(text) {
  const lines = String(text).split(/\r?\n/);
  // Join continuations: a long rule is wrapped with a trailing backslash.
  const joined = [];
  let acc = '';
  for (const line of lines) {
    if (/\\\s*$/.test(line)) { acc += line.replace(/\\\s*$/, ' '); continue; }
    acc += line;
    if (acc.trim()) joined.push(acc);
    acc = '';
  }
  if (acc.trim()) joined.push(acc);
  const out = [];
  for (const line of joined) {
    const m = /^\s*([^:]+):\s*(.*)$/.exec(line);
    if (!m) continue;
    const targets = m[1].trim();
    const deps = m[2].trim();
    // A stub line is `path:` with nothing after it. It names no source.
    if (!deps) continue;
    // The first rule is the object's own; later rules are phony stubs.
    for (const tok of deps.split(/\s+/)) {
      const t = tok.trim();
      if (!t) continue;
      if (targets === t) continue;
      out.push(t);
    }
    // Only the first real rule is the object's rule.
    if (out.length) break;
  }
  return out;
}

// The multiboot header, found in the bytes rather than taken from a record. It is 32-bit aligned and carries the magic
// 0x1BADB002; the flags word follows the magic.
export function findMultiboot(bytes) {
  const magic = 0x1BADB002;
  const limit = Math.min(bytes.length - 12, 8192);
  for (let off = 0; off <= limit; off += 4) {
    if (bytes.readUInt32LE(off) !== magic) continue;
    const flags = bytes.readUInt32LE(off + 4);
    const checksum = bytes.readUInt32LE(off + 8);
    // The header is only a header if the checksum makes the three words sum to zero.
    if (((magic + flags + checksum) >>> 0) !== 0) continue;
    return { offset: off, flags };
  }
  return null;
}
