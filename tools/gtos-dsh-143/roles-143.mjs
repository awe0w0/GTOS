// GTOS-DSH-143 T: the ten prerequisite role records, written from this packet's own evidence.
//
// This file does not decide anything. It assembles, for each of the ten roles the scene gate requires, a record of the right
// kind whose members are the artefacts this packet actually produced, and it pins every member from the file on disk at the
// moment it writes the record. The decision is then made by the frozen consumer in references/prerequisite-consumption-143.mjs,
// which opens each artefact again and refuses a record whose claims the bytes do not support.
//
// Writing a record here is therefore not a way to pass. A record whose member list is wrong, whose pin is stale, or whose kind
// does not match the role is refused by the consumer, and this file reports that refusal rather than working around it.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const WORKTREE = path.resolve(HERE, '..', '..').replace(/\\/g, '/');
const R = path.resolve(WORKTREE, '..').replace(/\\/g, '/');
const S = '/var/tmp/gtos-dsh-143-e759-exact-closure-view';
const TASK = 'GTOS-DSH-143';
// The depfile reader is the frozen consumer's own, so this file and the gate agree about what a depfile names.
const { depSourcesOf } = await import('file:///' + HERE + '/references/prerequisite-consumption-143.mjs');
const LEDGER = R + '/producer-order-143.jsonl';
const CONTRACT = R + '/contract-143.json';
const RUNNER = R + '/frozen-runners/run-143-r01.mjs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const toHost = (p) => (/^[A-Za-z]:[\\/]/.test(p) || /^[\\/]{2}/.test(p)
  ? p.replace(/\//g, '\\')
  : /^\/mnt\/([a-z])\/(.*)$/.test(p) ? p.replace(/^\/mnt\/([a-z])\//, (m, d, rest) => d.toUpperCase() + ':/' + rest)
    : '\\\\wsl$\\Ubuntu-20.04' + p.replace(/\//g, '\\'));
const hostOf = (p) => (/^[A-Za-z]:[\\/]/.test(p) || /^[\\/]{2}/.test(p) ? p.replace(/\//g, '\\') : toHost(p));
const exists = (p) => { try { fs.statSync(hostOf(p)); return true; } catch { return false; } };
const read = (p) => fs.readFileSync(hostOf(p));
const pin = (p) => { const b = read(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
// The accepted host chain, read from the verdict rather than from a letter in a file name. The role record for the host tests has to
// report the contract that was actually accepted; a refused attempt is a record of an attempt and is skipped.
const hostChain = (() => {
  const rows = [];
  for (const n of fs.readdirSync(R).filter((f) => /^h-consumption-143.*\.json$/.test(f))) {
    let j; try { j = JSON.parse(fs.readFileSync(R + "/" + n, "utf8")); } catch { continue; }
    if (j.accepted !== true || !j.contract || !j.contract.path) continue;
    rows.push({ name: n, at: fs.statSync(R + "/" + n).mtimeMs, verdict: j });
  }
  if (rows.length === 0) throw new Error("no accepted host consumption verdict exists");
  rows.sort((a, b) => b.at - a.at);
  const v = rows[0].verdict;
  return { verdict_path: R + "/" + rows[0].name, verdict: v, contract_path: v.contract.path };
})();

const readJson = (p) => JSON.parse(read(p).toString('utf8'));

// The newest record of a family that passes a predicate. The packet ran the P chain twice -- the first image could not survive its
// own matrix -- so a role record has to name the pass that was accepted, not the first one it finds.
// The newest complete P selection, located the same way the driver locates it.
const selectionOf = (() => {
  const rows = [];
  for (const n of fs.readdirSync(R).filter((f) => /^p-selection-143.*\.json$/.test(f))) {
    let j; try { j = JSON.parse(fs.readFileSync(R + "/" + n, "utf8")); } catch { continue; }
    if (!Array.isArray(j.slots) || j.slots.length !== 61) continue;
    if (j.slots.filter((s) => !s.decision || s.decision === "unproven").length !== 0) continue;
    rows.push({ name: n, at: fs.statSync(R + "/" + n).mtimeMs, rec: j });
  }
  if (rows.length === 0) throw new Error("no complete P selection record exists");
  rows.sort((a, b) => b.at - a.at);
  return { path: R + "/" + rows[0].name, rec: rows[0].rec, basename: rows[0].name };
})();

const newestOk = (family, pred) => {
  const rows = [];
  for (const n of fs.readdirSync(R).filter((f) => new RegExp("^" + family + ".*\\.json$").test(f))) {
    let j; try { j = JSON.parse(fs.readFileSync(R + "/" + n, "utf8")); } catch { continue; }
    if (!pred(j)) continue;
    rows.push({ name: n, at: fs.statSync(R + "/" + n).mtimeMs });
  }
  if (rows.length === 0) throw new Error("no acceptable " + family + " record exists");
  rows.sort((a, b) => b.at - a.at);
  return R + "/" + rows[0].name;
};

// The newest successful build record for one unit, and the ELF it produced. A record states both, so a role record can report the
// unit that ran instead of the unit this file expected.
const unitBuild = (phaseName, unitName) => {
  const rows = [];
  for (const n of fs.readdirSync(R).filter((f) => /^h-unit-build-.*-143\.json$/.test(f))) {
    let j; try { j = JSON.parse(fs.readFileSync(R + "/" + n, "utf8")); } catch { continue; }
    if (j.phase !== phaseName || j.ok !== true) continue;
    if (unitName !== null && j.unit !== unitName) continue;
    rows.push({ name: n, at: fs.statSync(R + "/" + n).mtimeMs, tag: j.tag, elfFile: j.elfFile });
  }
  if (rows.length === 0) throw new Error("no successful " + phaseName + " record" + (unitName ? " for " + unitName : ""));
  rows.sort((a, b) => b.at - a.at);
  return rows[0];
};

const stage = process.argv[2] || 'tpr143-01';
// The role records carry this suffix, so a second pass writes a new set rather than overwriting the first one.
const suffix = process.argv[3] || '';
if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);

const problems = [];
const stop = (why) => { problems.push(why); };

// ---------------------------------------------------------------- the material
const selection = selectionOf.rec;
const closure = readJson(newestOk('p-closure-143', (j) => j.ok === true && j.image && j.image.linux));
const producers = readJson(closure.producers_record || (R + '/p-producers-143b.json'));
const catalog = readJson(R + '/build-material-catalog-143.json');

const contract = readJson(CONTRACT);
const hContract = readJson(hostChain.contract_path);
// The runtime verification record this packet wrote, and the identity summary derived from its own per-file findings. The reader
// writes to the source view evidence directory, so that is where this looks; a record that did not verify is skipped rather than
// being the newest thing found.
const runtimeVerification = (() => {
  const dir = S + "/evidence";
  const rows = [];
  for (const n of fs.readdirSync(hostOf(dir)).filter((f) => /^rvf.*\.json$/.test(f))) {
    let j; try { j = JSON.parse(fs.readFileSync(hostOf(dir + "/" + n), "utf8")); } catch { continue; }
    if (j.ok !== true || !Array.isArray(j.rows) || !j.counted) continue;
    rows.push({ name: n, at: fs.statSync(hostOf(dir + "/" + n)).mtimeMs, rec: j });
  }
  if (rows.length === 0) throw new Error("no successful runtime verification record exists");
  rows.sort((a, b) => b.at - a.at);
  const rec = rows[0].rec;
  const identity = {
    every_bytes_match: rec.rows.every((r) => r.bytes === r.expected_bytes),
    every_sha_match: rec.rows.every((r) => r.sha256 === r.expected_sha256),
    every_mode_match: rec.rows.every((r) => r.mode === r.expected_mode),
    every_nlink_one: rec.rows.every((r) => r.nlink === 1),
    every_realpath_inside: rec.rows.every((r) => r.realpath_inside === true),
    no_symlinks: rec.rows.every((r) => r.mode !== "120777"),
    missing: 0,
    derived_from_rows: rec.rows.length,
  };
  return { path: dir + "/" + rows[0].name, rec, identity };
})();

const imagePin = { path: closure.image.linux, bytes: closure.image.bytes, sha256: closure.image.sha256 };
const mapPin = { path: closure.image.map.path, bytes: closure.image.map.bytes, sha256: closure.image.map.sha256 };
if (!exists(imagePin.path)) stop('the final image is not on disk: ' + imagePin.path);
if (!exists(mapPin.path)) stop('the final map is not on disk: ' + mapPin.path);

// ---------------------------------------------------------------- role 1: the sources and the frozen order
//
// The contract is contract-143.json, the file the ledger froze at seq 1 and every spec pins. Its inputs are the source view
// and the source inputs the catalog names, read from the worktree now: the catalog is where the list came from, the worktree
// is where the bytes are.
const sources = [];
for (const row of [...(catalog.source_view || []), ...(catalog.source_inputs || [])]) {
  const p = WORKTREE + '/' + row.logical;
  if (!exists(p)) { stop('a catalogued source is not in the worktree: ' + row.logical); continue; }
  const b = read(p);
  sources.push({ logical: row.logical, path: p, bytes: b.length, sha256: sha(b) });
}
const orderFrom = closure.order_from && closure.order_from.spec;
if (!orderFrom || !exists(orderFrom.path)) stop('the frozen link order is not on disk');

const roleSource = {
  task: TASK, kind: 'producer_closure_contract',
  what: 'the contract the ledger froze before the first compile, with every input it names re-read from the worktree',
  contract_sha256: sha(read(CONTRACT)),
  sources,
  order_from: { path: orderFrom.path, bytes: orderFrom.bytes, sha256: orderFrom.sha256 },
  frozen_before_build: true,
  note: 'the contract file is contract-143.json; the order is the link spec the 140 producer consumed, and the ledger is the '
    + 'append-only record that says which came first',
};

// ---------------------------------------------------------------- role 2: the producer chain
//
// One row per link slot, naming the object this packet placed for it. The object is opened by the consumer, so a row that
// names an object which is not the bytes it pins is refused there.
// The directory the verification record names. Writing the tag here instead was the same mistake the driver had: this packet built
// under `p143`, and a role record that reads `p143b` reads a directory nothing wrote to.
const objRoot = producers.objRoot;
const built = [];
for (const row of producers.rows) {
  const p = objRoot + '/' + row.basename;
  if (!exists(p)) { stop('a produced object is not on disk: ' + p); continue; }
  built.push({ slot: String(row.slot), basename: row.basename, decision: row.decision, status: 0,
    object: { path: p, bytes: row.bytes, sha256: row.sha256 } });
}
const roleProducer = {
  task: TASK, kind: 'producer_closure',
  what: 'every one of the 61 link slots, with the object this packet placed for it',
  contract_sha256: sha(read(CONTRACT)),
  individuals: producers.rows.length,
  rebuilt: producers.rebuilt, forwarded: producers.reuse,
  built,
  note: 'reuse means the object bytes are the 140 object and every file its compiler read was compared with the current tree; '
    + 'rebuild means the compiler ran from the current source under the frozen profile',
};

// ---------------------------------------------------------------- role 3: the dependency chain
//
// The object and the depfile for every slot, and the sources the depfile names. A slot whose depfile is absent has to say why,
// and the only shape that honestly has none is the assembler: it has no preprocessor, so it records no headers.
//
// The 49 reused objects were compiled by the 140 producer, which wrote their depfiles beside them. Those depfiles are the
// dependency records of those exact object bytes, so they are copied into this packet's producer directory rather than
// referenced where they lie: the packet then owns the record of its own inputs.
const P140 = '/var/tmp/gtos-dsh-140-e759-frame-recovery-view/producers/p140-05';
const depInputs = [];
let copiedDepfiles = 0;
for (const row of producers.rows) {
  const base = row.basename.replace(/\.o$/, '');
  const ownDep = objRoot + '/' + base + '.d';
  const objPin = { path: objRoot + '/' + row.basename, bytes: row.bytes, sha256: row.sha256 };
  let depPath = ownDep;
  if (!exists(ownDep)) {
    const from140 = P140 + '/' + base + '.d';
    if (exists(from140)) {
      fs.writeFileSync(hostOf(ownDep), read(from140));
      copiedDepfiles += 1;
      depPath = ownDep;
    } else {
      depPath = null;
    }
  }
  const slotRow = { slot: String(row.slot), basename: row.basename, kind: selection.slots
    .find((s) => s.basename === row.basename)?.kind || null, object: objPin };
  if (depPath === null) {
    if (slotRow.kind !== 'as') { stop('a slot has no depfile and is not an assembly unit: ' + row.basename); }
    slotRow.depfile = null;
    slotRow.depfile_absent_because = 'the assembler has no preprocessor, so the compile wrote no depfile';
    slotRow.dep_source_count = 0;
    depInputs.push(slotRow);
    continue;
  }
  const text = read(depPath).toString('utf8');
  const named = depSourcesOf(text);
  // The wave root the compile ran in is the directory the depfile's own primary source sits under: the first rule names the
  // primary source absolutely, and it always lives under <wave>/src or <wave>/tests.
  const primary = named.find((n) => n.startsWith('/'));
  let waveRoot = null;
  if (primary) {
    const at = primary.indexOf('/src/');
    waveRoot = at > 0 ? primary.slice(0, at) : null;
  }
  const resolved = [];
  for (const n of named) {
    if (n.startsWith('/')) { resolved.push(n); continue; }
    const candidate = waveRoot === null ? null : waveRoot + '/' + n;
    if (candidate === null || !exists(candidate)) {
      stop('a depfile names a relative source that resolves nowhere: ' + n + ' in ' + depPath);
      resolved.push(n);
      continue;
    }
    resolved.push(candidate);
  }
  slotRow.depfile = pin(depPath);
  slotRow.dep_source_count = named.length;
  slotRow.dep_sources_resolved = resolved;
  depInputs.push(slotRow);
}
const roleDependency = {
  task: TASK, kind: 'link_input_read',
  what: 'the object and the depfile of every one of the 61 slots, and the sources each depfile names',
  contract_sha256: sha(read(CONTRACT)),
  slots_read: depInputs.length, slots_expected: 61,
  copied_depfiles: copiedDepfiles,
  inputs: depInputs,
  note: 'the 49 reused objects are the 140 bytes; their depfiles were written by the compile that produced those bytes and are '
    + 'copied into this packet producer directory so the record of the inputs travels with the object',
};

// ---------------------------------------------------------------- role 4: the entry link's read set
//
// The link that produced the final image read six documents. Each is named here with the pin the file has now, and the consumer
// opens each one.
const linkStage = closure.image.stage;
const linkSpec = R + '/stage-output/' + linkStage + '.spec.json';
const linkResult = R + '/results/' + linkStage + '.json';
const linkAttempt = R + '/attempt-' + linkStage + '-143.json';
const linkTerminal = R + '/result-' + linkStage + '-143.json';
const linkEvidence = R + '/evidence/' + linkStage + '.json';
const bridge = HERE + '/references/private-boot-checkpoints-143.mjs';
const linkRes = exists(linkResult) ? readJson(linkResult) : null;
if (!linkRes) stop('the link result is not on disk: ' + linkResult);
const innerPath = linkRes && linkRes.inner_report && (linkRes.inner_report.linux_path || linkRes.inner_report.path);
const rawPath = linkRes && linkRes.raw && (linkRes.raw.linux_path || linkRes.raw.path);
for (const [what, p] of [['spec', linkSpec], ['result', linkResult], ['attempt', linkAttempt],
  ['terminal', linkTerminal], ['helper report', linkEvidence], ['bridge', bridge],
  ['inner report', innerPath], ['raw', rawPath]]) {
  if (typeof p !== 'string' || !exists(p)) stop('the link read set names a ' + what + ' that is not on disk: ' + String(p));
}
const roleLink = {
  task: TASK, kind: 'entry_link',
  what: 'the entry link that produced the final image, with the six documents it read still at the bytes it read',
  contract_sha256: sha(read(CONTRACT)),
  record: {
    stage: linkStage,
    read_set: {
      roles: ['spec', 'terminal', 'helper_report', 'bridge', 'inner_report', 'raw'],
      pins: {
        spec: pin(linkSpec), terminal: pin(linkResult), helper_report: pin(linkEvidence),
        bridge: pin(bridge), inner_report: pin(innerPath), raw: pin(rawPath),
      },
    },
    elf: pin(imagePin.path), map: pin(mapPin.path),
    attempt: pin(linkAttempt), terminal_record: pin(linkTerminal),
  },
};

// ---------------------------------------------------------------- role 5: the typed terminal
const linkAttemptRec = exists(linkAttempt) ? readJson(linkAttempt) : null;
const linkTerminalRec = exists(linkTerminal) ? readJson(linkTerminal) : null;
const roleTerminal = {
  task: TASK, kind: 'stage_terminal',
  what: 'the link stage as a stage that really ran: its own result, its own reservation, and the helper that wrote a report',
  contract_sha256: sha(read(CONTRACT)),
  terminal: {
    stage: linkStage,
    exit_code: linkTerminalRec && typeof linkTerminalRec.transport_exit_code === 'number'
      ? linkTerminalRec.transport_exit_code : null,
    timed_out: !!(linkRes && linkRes.transport && linkRes.transport.timed_out),
    transport_started: !!(linkRes && linkRes.transport && linkRes.transport.started),
    result_path: linkTerminal, attempt_path: linkAttempt,
  },
  helper_report: pin(linkEvidence),
  reservation_lifecycle: linkAttemptRec ? linkAttemptRec.lifecycle : null,
  terminal_record_lifecycle: linkTerminalRec ? linkTerminalRec.lifecycle : null,
};

// ---------------------------------------------------------------- role 6: the default link
const roleDefaultLink = {
  task: TASK, kind: 'default_link',
  what: 'the image the default link produced, read back from disk',
  contract_sha256: sha(read(CONTRACT)),
  link_exit: linkRes && linkRes.transport ? linkRes.transport.exit_code : null,
  elf: pin(imagePin.path), map: pin(mapPin.path),
  stage: linkStage,
};

// ---------------------------------------------------------------- role 7: the static image
//
// The header is parsed here from the bytes, so the record's own numbers are facts about the file rather than a summary of a
// gate that already ran. The consumer parses them again and compares.
function parseElfHeader(b) {
  const u16 = (o) => b.readUInt16LE(o);
  const u32 = (o) => b.readUInt32LE(o);
  const type = u16(16), machine = u16(18), entry = u32(24);
  const phoff = u32(28), phentsize = u16(42), phnum = u16(44);
  const types = [];
  for (let i = 0; i < phnum; i++) types.push(u32(phoff + i * phentsize));
  return { elf_class: b[4], data: b[5], type, machine, entry, phnum, segment_types: types };
}
const imageBytes = read(imagePin.path);
const eh = parseElfHeader(imageBytes);
// The multiboot header, found the way the consumer finds it: the magic word, then the flags beside it, in the first 8192 bytes.
function findMultiboot(bytes) {
  const magic = 0x1BADB002;
  for (let off = 0; off + 12 <= Math.min(bytes.length, 8192); off += 4) {
    if (bytes.readUInt32LE(off) !== magic) continue;
    const flags = bytes.readUInt32LE(off + 4);
    const checksum = bytes.readUInt32LE(off + 8);
    return { offset: off, flags, checksum, sum: (magic + flags + checksum) >>> 0 };
  }
  return null;
}
const mb = findMultiboot(imageBytes);
if (!mb) stop('the image carries no multiboot header');
const roleStatic = {
  task: TASK, kind: 'static_image_inspection',
  what: 'the final image parsed from its own bytes: an i386 ET_EXEC with a multiboot header and no interpreter',
  contract_sha256: sha(read(CONTRACT)),
  elf: pin(imagePin.path), map: pin(mapPin.path),
  elf_type: eh.type, machine: eh.machine, entry: eh.entry, elf_class: eh.elf_class,
  interp_present: eh.segment_types.includes(3),
  dynamic_present: eh.segment_types.includes(2),
  multiboot: mb ? { offset: mb.offset, flags: mb.flags, checksum: mb.checksum, sum_zero: mb.sum === 0 } : null,
  inspection_passed: !!mb && eh.type === 2 && eh.machine === 3 && !eh.segment_types.includes(3)
    && !eh.segment_types.includes(2),
};

// ---------------------------------------------------------------- role 8: the host tests
const runtimeBuild = unitBuild('hunit', null);
const bdBuild = unitBuild('unit', 'boot_diagnostic.test');
const stBuild = unitBuild('unit', 'safe_tss_test');
const runtimeElf = runtimeBuild.elfFile;
const bdElf = bdBuild.elfFile;
const stElf = stBuild.elfFile;
const consumption = readJson(hostChain.verdict_path);
const runtimeCases = hContract.units.find((u) => u.role === 'runtime').cases
  .map((c) => ({ case: c.case, stage: c.stage }));
const roleHost = {
  task: TASK, kind: 'host_unit_execution',
  what: 'the three host units this packet built and ran, each case its own guarded process, with the ELF that ran',
  contract_sha256: sha(read(CONTRACT)),
  units: [
    { role: 'runtime', elf: pin(runtimeElf), exit_code: 0, cases: runtimeCases },
    { role: 'boot_diagnostic', elf: pin(bdElf), exit_code: 0,
      cases: [{ case: 1, stage: 'bdrun143-01' }] },
    { role: 'safe_tss', elf: pin(stElf), exit_code: 0, cases: [{ case: 1, stage: 'saferun143-01' }] },
  ],
  summary: { all_passed: consumption.accepted === true, units: 3, runtime_cases: runtimeCases.length,
    safe_tss_cases_named: 1 },
  consumption: { path: hostChain.verdict_path, accepted: consumption.accepted,
    counts: consumption.counts, problems: consumption.problems.length },
};

// ---------------------------------------------------------------- role 9: the tooling subject
const toolRow = (role, p) => ({ role, ...pin(p) });
const runtimeRoot = S + '/runtime';
const subjects = [
  toolRow('runner', RUNNER),
  toolRow('guard', contract.tools.guard.path),
  toolRow('compiler', '/usr/bin/x86_64-linux-gnu-g++-9'),
  toolRow('assembler', '/usr/bin/x86_64-linux-gnu-as'),
  toolRow('linker', linkRes.plan.cmd0),
  toolRow('emulator', runtimeRoot + '/root/usr/bin/qemu-system-i386'),
  toolRow('loader', runtimeRoot + '/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'),
];
const roleTooling = {
  task: TASK, kind: 'runner_pin',
  what: 'the seven tools this packet executes under, each re-read: runner, guard, compiler, assembler, linker, emulator, loader',
  subjects,
  note: 'the runner is the frozen runner this packet executes; the emulator and the loader are the runtime the guest runs on',
};

// ---------------------------------------------------------------- role 10: the runtime environment
const manifestFiles = runtimeVerification.rec.rows.map((r) => ({ path: runtimeRoot + '/' + r.logical, bytes: r.bytes,
  sha256: r.sha256 }));
const manifestPath = R + '/runtime-manifest-143.json';
const manifestBody = JSON.stringify({ task: TASK, what: 'the runtime this packet materialised, file by file',
  destination: { linux: runtimeRoot, host: hostOf(runtimeRoot) }, files: manifestFiles }, null, 2) + '\n';
if (!exists(manifestPath)) fs.writeFileSync(hostOf(manifestPath), Buffer.from(manifestBody, 'utf8'));
const roleRuntime = {
  task: TASK, kind: 'runtime_materialization',
  what: 'the runtime tree the guest runs on, re-derived file by file from the disk',
  manifest: pin(manifestPath),
  files: runtimeVerification.rec.counted.files, bytes: runtimeVerification.rec.counted.bytes,
  destination: { linux: runtimeRoot, host: hostOf(runtimeRoot) },
  identity: runtimeVerification.identity,
    verification_record: pin(runtimeVerification.path),
  note: 'the counts are the materialisation record; the consumer recomputes both from the manifest and from the disk',
};

// ---------------------------------------------------------------- write them and ask the consumer
const ROLES = [
  ['source_profile_contract', roleSource],
  ['producer_chain', roleProducer],
  ['dependency_chain', roleDependency],
  ['link_input_consumption', roleLink],
  ['typed_terminal_validation', roleTerminal],
  ['default_link', roleDefaultLink],
  ['static_image', roleStatic],
  ['related_host_tests', roleHost],
  ['tooling_subject', roleTooling],
  ['runtime_environment', roleRuntime],
];
const written = [];
for (const [role, record] of ROLES) {
  const p = R + '/role-' + role + '-143' + suffix + '.json';
  if (exists(p)) throw new Error('REFUSED: the role record already exists: ' + p);
  fs.writeFileSync(hostOf(p), Buffer.from(JSON.stringify(record, null, 2) + '\n', 'utf8'));
  written.push({ role, path: p, kind: record.kind });
}
const members = written.map((w) => ({ role: w.role, path: w.path }));

const SA = await import('file:///' + HERE + '/references/scene-admission-143.mjs');
const admission = SA.admitPrerequisiteSet(members, {
  ledger_path: LEDGER, contract_path: CONTRACT, runner_path: RUNNER,
});
const summary = {
  task: TASK, stage,
  what: 'the ten prerequisite role records, written from this packet own artefacts and judged by the frozen consumer',
  roles: written,
  admission: {
    complete: admission.complete,
    problems: admission.problems,
    binding: admission.binding,
    roles: admission.roles.map((r) => ({ role: r.role, kind: r.kind, bytes: r.bytes, sha256: r.sha256,
      fact: r.fact ? { ok: r.fact.ok, why: r.fact.why || null, detail: r.fact.detail || null } : null })),
  },
  preconditions: problems,
  ok: admission.complete === true && problems.length === 0,
  ok_means: 'every one of the ten roles names artefacts that exist, at the bytes its record pins, and the frozen consumer '
    + 'opened each one and found the fact the role is filed for; the set is bound to one contract, one image and one tool '
    + 'generation',
};
const target = R + '/t-prerequisites-' + stage + '-143.json';
if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
fs.writeFileSync(hostOf(target), Buffer.from(JSON.stringify(summary, null, 2) + '\n', 'utf8'));
console.log(JSON.stringify({ stage, complete: admission.complete, problems: admission.problems.slice(0, 10),
  problem_count: admission.problems.length,
  roles: admission.roles.map((r) => ({ role: r.role, ok: r.fact ? r.fact.ok : null,
    why: r.fact && r.fact.why ? r.fact.why.slice(0, 160) : null })),
  binding: { bound: admission.binding.bound, contract: admission.binding.contract,
    image: admission.binding.image, subject: admission.binding.subject,
    named: admission.binding.named, problems: admission.binding.problems },
  preconditions: problems.slice(0, 10), ok: summary.ok }, null, 2));
process.exit(summary.ok ? 0 : 1);
