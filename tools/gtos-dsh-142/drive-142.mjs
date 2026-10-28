// GTOS-DSH-142 driver.
//
// Every build, link, copy and launch this packet performs goes through one path: the runner claims a named stage and freezes a
// helper, the helper hands the frozen bridge control to the production bridge, the bridge admits its own request, and the guard
// launches it in a child of its own with the two channels it writes itself. Nothing here runs a compiler, a linker, a Python
// module or an ELF directly, and nothing here reads a result the guard did not write.
//
// Phases:
//   contract                     freeze the identity every later spec pins
//   runtimem <stage>             materialise the fixed runtime bundle into this packet's own source view
//   doctor                       report what exists, so a resume can tell finished work from unfinished
//
// The roots come from the authority, not from a literal in this file, so a driver that has been pointed at the wrong packet
// fails at its first line rather than writing into another packet's tree.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

// This file locates everything from its own path. It is committed inside the packet worktree, so a reviewer can run it from a
// clean checkout without editing a single literal: the worktree is two directories up, the report root is one above that, and
// the modules it imports are its own siblings. 141's committed copies hardcoded the report root and reached for
// `../references/...`, which did not exist in the worktree, so the committed script could not run at all.
// The paths are normalised to forward slashes because the authority records them that way and the bridge compares them as
// strings. path.resolve on Windows returns backslashes, so a self-located root would fail an equality check that the recorded
// root passes, and the failure would look like a wrong packet rather than a separator.
const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const WORKTREE = path.resolve(HERE, '..', '..').replace(/\\/g, '/');
const R = path.resolve(WORKTREE, '..').replace(/\\/g, '/');
const A = JSON.parse(fs.readFileSync(R + '/authority-142.json', 'utf8'));
const S = A.source_root;
const L = A.work_root;
const NODE = 'C:/Program Files/nodejs/node.exe';
// The runner is frozen and pinned in the report root, because that is where a frozen artefact belongs and where its pin lives.
// The copy beside this file is the one a reviewer reads; the first thing this driver does is require the two to be identical, so
// a committed copy that has drifted from the frozen one is a stop rather than a second runner nobody pinned.
const RUNNER = R + '/frozen-runners/run-142-r01.mjs';
const RUNNER_COPY = HERE + '/run-142-r01.mjs';
const BRIDGE = HERE + '/references/private-boot-checkpoints-142.mjs';
const ORDER_LOG = R + '/producer-order-142.jsonl';
const HELPER = 'native-142-01.mjs';
// The 140 producer records are read-only inputs here: they say which sources each object was built from and which flags the
// compiler was given. They are never written.
const R140_ROOT = 'F:/GTOS-Chromium/artifacts/native-guarded-frame-recovery-repeatability-and-portable-consumption-20261027-a';

if (A.task !== 'GTOS-DSH-142') throw new Error('this driver is not pointed at its own packet: ' + A.task);
{
  const frozen = fs.readFileSync(RUNNER);
  const copy = fs.readFileSync(RUNNER_COPY);
  if (!frozen.equals(copy)) throw new Error('the committed runner copy differs from the frozen runner');
}
if (S !== '/var/tmp/gtos-dsh-142-e759-current-guest-view') throw new Error('unexpected source root: ' + S);
if (L !== '/var/tmp/gtos-dsh-142-e759-current-guest') throw new Error('unexpected work root: ' + L);

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
function toLinux(p) {
  if (/^[A-Za-z]:[\\/]/.test(p)) return '/mnt/' + p[0].toLowerCase() + '/' + p.slice(3).replace(/\\/g, '/');
  // A path this process built for its own use is a Windows path, and a Windows path to a Linux file is a UNC share. Handing
  // that share to a program inside the distribution gives it a name it cannot open, which is what the first runtime attempt
  // did. The share prefix is the distribution's own root, so it is stripped rather than translated.
  const unc = /^\/\/wsl\$\/[^/]+\/(.*)$/i.exec(p);
  if (unc) return '/' + unc[1];
  return p;
}
function toHost(l) {
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(l);
  if (m) return m[1].toUpperCase() + ':/' + m[2];
  return l.startsWith('/') ? '//wsl$/Ubuntu-20.04' + l : l;
}
const exists = (p) => fs.existsSync(p);
function put(hostPath, bytes) {
  if (exists(hostPath)) throw new Error('REFUSED exists: ' + hostPath);
  fs.mkdirSync(path.dirname(hostPath), { recursive: true });
  fs.writeFileSync(hostPath, bytes, { flag: 'wx' });
  const landed = fs.readFileSync(hostPath);
  const want = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes, 'utf8');
  if (!landed.equals(want)) throw new Error('not identical: ' + hostPath);
  return { path: hostPath, bytes: landed.length, sha256: sha(landed) };
}
function putOrSame(hostPath, bytes) {
  if (exists(hostPath)) {
    const landed = fs.readFileSync(hostPath);
    const want = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes, 'utf8');
    if (!landed.equals(want)) throw new Error('REFUSED exists with different bytes: ' + hostPath);
    return { path: hostPath, bytes: landed.length, sha256: sha(landed), unchanged: true };
  }
  return { ...put(hostPath, bytes), unchanged: false };
}

// ---------------------------------------------------------------- the contract
const CONTRACT = R + '/contract-142.json';
function contractPin() {
  if (!exists(CONTRACT)) throw new Error('the contract is not frozen yet: run the contract phase first');
  return pin(CONTRACT);
}
function orderEvent(event, detail) {
  let next = 1;
  if (exists(ORDER_LOG)) {
    const lines = fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean);
    if (lines.length) next = JSON.parse(lines[lines.length - 1]).seq + 1;
  }
  const rec = { seq: next, event, at: new Date().toISOString(), ...detail };
  fs.appendFileSync(ORDER_LOG, JSON.stringify(rec) + '\n', { encoding: 'utf8' });
  return rec;
}

// The contract is frozen once, and every spec pins its bytes. An event is written the first time a stage notices it, so the ledger
// shows that the contract existed before the stage that pins it, rather than being written afterwards.
function freezeContractInLedger(note) {
  const p = contractPin();
  const lines = exists(ORDER_LOG)
    ? fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l)) : [];
  if (lines.some((l) => l.event === 'contract-frozen' && l.contract && l.contract.sha256 === p.sha256)) return;
  orderEvent('contract-frozen', { contract: p, note: note || null,
    what: 'the roots, the executor closure, the pinned tools and the runtime inventory every later spec pins' });
}

// ---------------------------------------------------------------- the bridge control and the spec
function bridgeFor(stage, kind, route, extra) {
  return {
    task: A.task, stage, outer_stage: stage, kind, route,
    report_root: R, work_root: L, source_root: S, distribution: 'Ubuntu-20.04',
    authority: pin(R + '/authority-142.json'),
    request_module: R + '/references/native-request-142.mjs',
    tools: { python_realpath: A.python.realpath },
    guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
    runner_module: RUNNER,
    binding_module: pin(BRIDGE),
    // The production compile profile. These two files are this packet's own copies of the pinned publication profile, verified
    // byte for byte when they were copied; the bridge reads BASE_CXXFLAGS out of the Makefile and the integer flag list out of
    // kernel-cxxflags and requires the caller to match the concatenation exactly.
    profile_makefile: R + '/profile-142/Makefile',
    profile_cxxflags: R + '/profile-142/kernel-cxxflags',
    report_path: R + '/evidence/' + stage + '.json',
    log_max_bytes: 524288,
    ...(extra || {}),
  };
}
function writeSpec(stage, route, mode, kind, options, limit, extra) {
  const control = {
    task: A.task, route, mode,
    bridge_module_path: BRIDGE,
    bridge: bridgeFor(stage, kind, route, extra && extra.bridge),
    stage_options: (options || {}),
    purpose: kind + ' for ' + A.task,
    report_root: R,
    report_path: R + '/results/' + stage + '.json',
  };
  const spec = { task: A.task, route, stage, root: R, runnerPath: RUNNER,
    helper: HELPER, control,
    expected_outputs: [control.report_path], limit, contract_pin: contractPin() };
  const p = R + '/stage-output/' + stage + '.spec.json';
  const text = JSON.stringify(spec, null, 2) + '\n';
  // A spec that was written and never executed is reusable only if it is byte-identical to the one this run would write and the
  // stage name is still unspent. Reusing it is not overwriting anything: the file's bytes do not change, and the runner refuses
  // a spent name regardless. A spec with different bytes is a different declaration and is refused.
  if (exists(p)) {
    if (fs.readFileSync(p, 'utf8') !== text) throw new Error('REFUSED: a different spec exists at ' + p);
    orderEvent('spec-reused', { stage, kind, spec: p, contract_sha256: spec.contract_pin.sha256 });
    return p;
  }
  fs.writeFileSync(p, text, { encoding: 'utf8', flag: 'wx' });
  orderEvent('spec-written', { stage, kind, spec: p, contract_sha256: spec.contract_pin.sha256 });
  return p;
}
function run(specPath) {
  const stage = path.basename(specPath).replace('.spec.json', '');
  orderEvent('run-started', { stage });
  const r = spawnSync(NODE, [RUNNER, specPath], { windowsHide: true, timeout: 3600000, maxBuffer: 64 * 1024 * 1024 });
  const outText = (r.stdout || '').toString();
  const line = (outText.split('\n').filter((l) => l.startsWith('stage='))[0] || '').trim();
  let detail = '';
  for (const p of [R + '/results/' + stage + '.json', R + '/evidence/' + stage + '.json']) {
    if (exists(p)) {
      try {
        const d = JSON.parse(fs.readFileSync(p, 'utf8'));
        const t = d.stderr_tail || (d.transport && d.transport.stderr_tail) || '';
        if (t) { detail = String(t).replace(/\n/g, ' | ').slice(0, 600); break; }
      } catch { }
    }
  }
  orderEvent('run-completed', { stage, status: r.status, ok: /ok=true/.test(line) });
  const errText = (r.stderr || '').toString();
  if (!detail && errText) detail = errText.replace(/\n/g, ' | ').slice(-900);
  return { stage, line, detail, status: r.status, stdout: outText.slice(-2500), stderr: errText.slice(-2500) };
}

const phase = process.argv[2];
const out = [];

// ================================================================ contract
if (phase === 'contract') {
  fs.mkdirSync(R + '/evidence', { recursive: true });
  fs.mkdirSync(R + '/results', { recursive: true });
  const materials = {
    task: A.task,
    what: 'the identity every later spec pins: the roots, the executor closure this packet actually runs, the pinned tools, '
      + 'and the runtime inventory this packet materialises from',
    roots: { report_root: R, work_root: L, source_root: S, worktree: WORKTREE },
    executor_closure: [
      'references/private-boot-checkpoints-142.mjs', 'references/scene-admission-142.mjs',
      'references/prerequisite-consumption-142.mjs', 'references/native-request-142.mjs',
      'references/host-path-142.mjs', 'frozen-runners/run-142-r01.mjs', HELPER, 'staging-142-01.py',
    ].map((rel) => pin(R + '/' + rel)),
    tools: {
      node: pin(NODE), wsl: pin(A.wsl.path), guard: pin(A.guard.path),
      python: { linux: A.python.path, realpath: A.python.realpath, bytes: A.python.bytes, sha256: A.python.sha256 },
      roles: A.tools.map((t) => ({ role: t.role, linux: t.path, bytes: t.bytes, sha256: t.sha256 })),
    },
    runtime: {
      inventory: pin(A.runtime_source_inventory.path),
      original_source: A.runtime_original_source,
      original_source_linux: toLinux(A.runtime_original_source),
      files: A.runtime_files, bytes: A.runtime_bytes,
      destination: A.runtime_destination,
      rule: 'each entry is copied from the Linux form of its pinned source; 0644 everywhere except the real emulator and the '
        + 'bundled loader, which are 0755; every copy is rehashed at the destination',
    },
    assembly_source_contract: pin(R + '/assembly-source-contract-142.json'),
    build_material_catalog: pin(R + '/build-material-catalog-142.json'),
  };
  const written = put(CONTRACT, JSON.stringify(materials, null, 2) + '\n');
  orderEvent('contract-frozen', { contract: written, what: 'roots, executor closure, tools and the runtime inventory' });
  console.log(JSON.stringify({ wrote: written }, null, 2));
  process.exit(0);
}

// ================================================================ runtimem
if (phase === 'runtimem') {
  const stage = process.argv[3];
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const inv = JSON.parse(fs.readFileSync(A.runtime_source_inventory.path, 'utf8'));

  // The inventory names its sources as Windows paths, and the Python module that copies them runs inside WSL, so the request
  // carries the Linux form of each source. The substitution is the only edit, and it is asserted to be the only edit: a row
  // whose Linux form is not a /mnt/ path, or whose byte count or hash moved, is a stop rather than a silent conversion.
  const rows = [];
  const problems = [];
  for (const f of inv.files) {
    const linux = f.source_linux || toLinux(f.source);
    if (!/^\/mnt\/f\/GTOS-Chromium\//.test(linux)) problems.push({ logical: f.logical, why: 'not a /mnt/f source', linux });
    if (toHost(linux) !== (f.source || f.path)) problems.push({ logical: f.logical, why: 'linux and windows forms disagree',
      linux, host: toHost(linux), windows: f.source || f.path });
    rows.push({ logical: f.logical, source: linux, bytes: f.bytes, sha256: f.sha256, mode_octal: f.mode_octal });
  }
  if (problems.length) throw new Error('the inventory cannot be converted cleanly: ' + JSON.stringify(problems.slice(0, 5)));
  const derived = { task: A.task, what: 'the fixed runtime inventory with each source in its Linux form; the only change '
    + 'from the pinned inventory is that source names the /mnt/f path rather than the F: path for the same file',
  derived_from: pin(A.runtime_source_inventory.path), files: rows, files_count: rows.length,
  bytes: rows.reduce((n, r) => n + r.bytes, 0) };
  if (derived.files_count !== A.runtime_files || derived.bytes !== A.runtime_bytes) {
    throw new Error('the derived inventory is ' + derived.files_count + '/' + derived.bytes
      + ' where the authority says ' + A.runtime_files + '/' + A.runtime_bytes);
  }
  const derivedPin = putOrSame(toHost(S + '/inputs/' + stage + '-inventory.json'),
    Buffer.from(JSON.stringify(derived, null, 2) + '\n', 'utf8'));

  // The Python module's own control. Its report is written by the module itself, under the source view it owns.
  const control = {
    task: A.task, stage, mode: 'runtime',
    source_root: S, report_root: S, report_path: S + '/evidence/' + stage + '.json',
    inventory: { path: toLinux(derivedPin.path), bytes: derivedPin.bytes, sha256: derivedPin.sha256 },
    original_source_root: toLinux(A.runtime_original_source),
    note: 'copies the fixed bundle into this packet own runtime tree and publishes it with one rename',
  };
  const pyPin = putOrSame(toHost(S + '/frozen-helpers/' + stage + '.py'), fs.readFileSync(HERE + '/staging-142-01.py'));
  const ctlPin = putOrSame(toHost(S + '/inputs/' + stage + '.json'),
    Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));

  const p = writeSpec(stage, 'runtime.stage', 'runtime-stage', 'runtimestage', { limit: 900 }, 1200);
  const r = run(p);
  out.push(r);
  const pyReportPath = toHost(S + '/evidence/' + stage + '.json');
  let pyReport = null;
  if (exists(pyReportPath)) {
    try { pyReport = JSON.parse(fs.readFileSync(pyReportPath, 'utf8')); } catch { pyReport = { unreadable: true }; }
  }
  const destHost = toHost(A.runtime_destination);
  const destState = exists(destHost) ? (() => {
    const st = fs.lstatSync(destHost);
    const kids = fs.readdirSync(destHost);
    return { path: A.runtime_destination, is_directory: st.isDirectory(), is_symlink: st.isSymbolicLink(), children: kids };
  })() : null;
  console.log(JSON.stringify({ stage, py_helper: pyPin, control: ctlPin, inventory: derivedPin,
    files: derived.files_count, bytes: derived.bytes, out, py_report: pyReport ? {
      ok: pyReport.ok, files: pyReport.files, bytes: pyReport.bytes, destination: pyReport.destination,
      published_once: pyReport.published_once, runtime_executed: pyReport.runtime_executed } : null,
    destination: destState }, null, 2));
  process.exit(out.every((x) => x.status === 0) ? 0 : 1);
}

// ================================================================ runtimeverify
// The reader stage. It reads the tree the copy stage published and re-derives every number from the filesystem, inside the
// distribution, where POSIX mode and link count are real values. The copy stage's own report is not used as evidence here.
if (phase === 'runtimeverify') {
  const stage = process.argv[3];
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const inv = JSON.parse(fs.readFileSync(A.runtime_source_inventory.path, 'utf8'));
  const rows = inv.files.map((f) => ({ logical: f.logical, source: f.source_linux || toLinux(f.source),
    bytes: f.bytes, sha256: f.sha256, mode_octal: f.mode_octal }));
  const derived = { task: A.task, what: 'the fixed runtime inventory with each source in its Linux form',
    derived_from: pin(A.runtime_source_inventory.path), files: rows, files_count: rows.length,
    bytes: rows.reduce((n, r) => n + r.bytes, 0) };
  const derivedPin = putOrSame(toHost(S + '/inputs/' + stage + '-inventory.json'),
    Buffer.from(JSON.stringify(derived, null, 2) + '\n', 'utf8'));
  // The runtime root is a parameter, not a constant. The same reader therefore serves the published runtime and any relocated copy
  // of it, which is what makes a relocation test meaningful: the reader is not allowed to know where the tree lives.
  const runtimeRoot = process.argv[4] || A.runtime_destination;
  const control = {
    task: A.task, stage, kind: 'runtime-identity-reader',
    runtime: runtimeRoot,    inventory: { path: toLinux(derivedPin.path), bytes: derivedPin.bytes, sha256: derivedPin.sha256 },
    negative_root: S + '/tmp/neg-' + stage,
    report_path: S + '/evidence/' + stage + '.json',
    note: 'reads the published runtime and re-derives its identity from the filesystem; also exercises four defects against a '
      + 'scratch tree it creates itself',
  };
  const pyPin = putOrSame(toHost(S + '/frozen-helpers/' + stage + '.py'), fs.readFileSync(HERE + '/verify-runtime-142.py'));
  const ctlPin = putOrSame(toHost(S + '/inputs/' + stage + '.json'),
    Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
  const p = writeSpec(stage, 'native.metadata', 'native-metadata', 'metadata', { limit: 900 }, 1200);
  const r = run(p);
  out.push(r);
  const repPath = toHost(S + '/evidence/' + stage + '.json');
  let rep = null;
  if (exists(repPath)) { try { rep = JSON.parse(fs.readFileSync(repPath, 'utf8')); } catch { rep = { unreadable: true }; } }
  console.log(JSON.stringify({ stage, py_helper: pyPin, control: ctlPin, inventory: derivedPin, out,
    report: rep ? { ok: rep.ok, counted: rep.counted, declared: rep.declared,
      executable_files: rep.executable_files, executable_directories: rep.executable_directories,
      problems: rep.problems ? rep.problems.slice(0, 5) : null, problems_total: rep.problems ? rep.problems.length : null,
      negatives: rep.negatives, negatives_ok: rep.negatives_ok } : null }, null, 2));
  process.exit(out.every((x) => x.status === 0) ? 0 : 1);
}

// ================================================================ pwave
// Stage the current committed tree into this packet's own source view. Every logical the catalog names is taken from the
// worktree, which is a clean checkout at the parent commit, so what gets compiled is what is committed rather than what a
// previous packet happened to have staged.
if (phase === 'pwave') {
  const stage = process.argv[3];
  const wave = process.argv[4] || 'wave-01';
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-142.json', 'utf8'));
  const byLogical = new Map();
  const absent = [];
  for (const v of C.source_view) {
    const p = WORKTREE + '/' + v.logical;
    if (!exists(p)) { absent.push(v.logical); continue; }
    const b = fs.readFileSync(p);
    byLogical.set(v.logical, { logical: v.logical, source: toLinux(p), bytes: b.length, sha256: sha(b) });
  }
  for (const s of C.source_inputs) {
    const p = WORKTREE + '/' + s.logical;
    if (!exists(p)) { absent.push(s.logical); continue; }
    const b = fs.readFileSync(p);
    byLogical.set(s.logical, { logical: s.logical, source: toLinux(p), bytes: b.length, sha256: sha(b) });
  }
  // The catalog covers what the image links. A unit also needs its own test sources and the linker script and assembly entry it
  // declares, so those are named on the command line and staged from the same worktree. Nothing here is taken from anywhere else.
  const extras = (process.argv[5] || '').split(',').map((s) => s.trim()).filter(Boolean);
  for (const logical of extras) {
    const p = WORKTREE + '/' + logical;
    if (!exists(p)) { absent.push(logical); continue; }
    const b = fs.readFileSync(p);
    byLogical.set(logical, { logical, source: toLinux(p), bytes: b.length, sha256: sha(b) });
  }
  if (absent.length) throw new Error('the worktree is missing declared sources: ' + absent.slice(0, 6).join(', '));
  freezeContractInLedger();
  const files = [...byLogical.values()].sort((a, b) => (a.logical < b.logical ? -1 : 1));
  const control = { task: A.task, source_root: S, report_root: S,
    report_path: S + '/evidence/' + stage + '.json',
    request: { wave, files },
    note: 'copies the current committed tree into a wave this stage owns' };
  putOrSame(toHost(S + '/frozen-helpers/' + stage + '.py'), fs.readFileSync(HERE + '/staging-142-01.py'));
  putOrSame(toHost(S + '/inputs/' + stage + '.json'), Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
  const p = writeSpec(stage, 'source.view', 'source-view', 'sourceview', { limit: 600 }, 900);
  out.push(run(p));
  console.log(JSON.stringify({ stage, wave, files: files.length,
    bytes: files.reduce((n, f) => n + f.bytes, 0),
    out: out.map((r) => ({ stage: r.stage, status: r.status, line: r.line, detail: r.detail })) }, null, 2));
  process.exit(out.every((r) => r.status === 0) ? 0 : 1);
}

// ================================================================ pbuild
// Build the objects the P selection says must be rebuilt, and place the ones it says may be reused, in one directory that the
// link then consumes. The two halves are deliberately different:
//
//   reuse   the object's bytes are checked against its pin AND every file its compiler read is checked against the current tree.
//           Both checks already happened when the selection was made; they are repeated here against the bytes on disk, because a
//           selection record is not a substitute for the object it describes.
//   rebuild one guarded compiler stage per object, from the current source, under the flags the 140 producer actually used.
//
// The reused objects live under the 140 view, which the guard cannot read. They are copied into this packet's own tree first, and
// the copy is verified against the pin after it lands, so the link never consumes a file whose bytes nobody checked.
if (phase === 'pbuild') {
  const rootArg = process.argv[3];
  const wave = process.argv[4] || 'wave-01';
  const tag = process.argv[5] || 'p142';
  const rootAlnum = rootArg.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*$/.test(rootAlnum)) throw new Error('bad stage root: ' + rootArg);
  let seq = 0;
  const stageName = () => rootAlnum + '-' + String(++seq).padStart(2, '0');
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-142.json', 'utf8'));
  const spec140 = JSON.parse(fs.readFileSync(C.parent_actual_final_link_spec.path, 'utf8'));
  // The flags are derived from the frozen profile, because that is what the bridge checks the caller against. The 140 producer's
  // own recorded list is then compared with the derived list: the 61 objects this packet reuses were built by that producer, so if
  // the two lists differ then a reused object and a rebuilt object would not be the same kind of object, and that is a stop.
  const contract140 = JSON.parse(fs.readFileSync(R140_ROOT + '/producer-contract-p140-05.json', 'utf8'));
  const flagSets = new Set();
  (function walk(v) {
    if (Array.isArray(v)) { for (const x of v) walk(x); return; }
    if (v && typeof v === 'object') {
      if (Array.isArray(v.cxxflags)) flagSets.add(JSON.stringify(v.cxxflags));
      for (const x of Object.values(v)) walk(x);
    }
  })(contract140);
  if (flagSets.size !== 1) throw new Error('the 140 producer used ' + flagSets.size + ' distinct flag lists, one is required');
  const fromProducer = JSON.parse([...flagSets][0]);
  const mk = fs.readFileSync(R + '/profile-142/Makefile', 'utf8');
  const baseLine = mk.split('\n').find((l) => l.startsWith('BASE_CXXFLAGS :='));
  if (!baseLine) throw new Error('the frozen Makefile declares no BASE_CXXFLAGS');
  const baseFlags = baseLine.replace('BASE_CXXFLAGS :=', '').trim()
    .replace('$(OPTIMIZATION)', '-O2').split(/\s+/).filter(Boolean);
  const intFlags = fs.readFileSync(R + '/profile-142/kernel-cxxflags', 'utf8').trim().split(/\s+/).filter(Boolean);
  const cxxflags = [...baseFlags, ...intFlags];
  if (JSON.stringify(cxxflags) !== JSON.stringify(fromProducer)) {
    throw new Error('the frozen profile and the 140 producer disagree on compile flags');
  }

  const srcRoot = S + '/' + wave;
  const objRoot = S + '/producers/' + tag;
  fs.mkdirSync(toHost(objRoot), { recursive: true });

  const reused = [];
  const rebuilt = [];
  const problems = [];
  for (const slot of sel.slots) {
    const dest = objRoot + '/' + slot.basename;
    if (slot.decision === 'reuse') {
      const srcHost = toHost(slot.linux);
      if (!exists(srcHost)) { problems.push({ slot: slot.index, basename: slot.basename, why: 'the reused object is not on disk' }); continue; }
      const b = fs.readFileSync(srcHost);
      if (sha(b) !== slot.object.pinned_sha256 || b.length !== slot.object.pinned_bytes) {
        problems.push({ slot: slot.index, basename: slot.basename, why: 'the reused object is not its pinned bytes' }); continue;
      }
      const destHost = toHost(dest);
      // A copy that already landed in an earlier attempt is kept, not redone, but only after it is re-verified against the pin: the
      // point of the check is the bytes that will be linked, not the act of copying. A destination holding different bytes is a
      // stop, because something else has put an object in this slot.
      if (exists(destHost)) {
        const already = fs.readFileSync(destHost);
        if (sha(already) !== slot.object.pinned_sha256 || already.length !== slot.object.pinned_bytes) {
          throw new Error('REFUSED: ' + dest + ' already exists with other bytes');
        }
        reused.push({ slot: slot.index, basename: slot.basename, bytes: already.length, sha256: sha(already),
          from: slot.linux, reason: slot.decision, placed: 'earlier' });
        continue;
      }
      fs.copyFileSync(srcHost, destHost);
      const landed = fs.readFileSync(destHost);
      if (sha(landed) !== slot.object.pinned_sha256) throw new Error('the copy is not the original: ' + dest);
      reused.push({ slot: slot.index, basename: slot.basename, bytes: landed.length, sha256: sha(landed),
        from: slot.linux, reason: slot.decision, placed: 'now' });
    } else {
      const sourceLogical = slot.logical;
      if (!sourceLogical) { problems.push({ slot: slot.index, basename: slot.basename, why: 'the slot names no source' }); continue; }
      const sourceLinux = srcRoot + '/' + sourceLogical;
      if (!exists(toHost(sourceLinux))) { problems.push({ slot: slot.index, basename: slot.basename, why: 'the source is not in the wave' }); continue; }
      // The kind decides the route: an assembly slot goes through `as`, a C++ slot through the compiler. The 140 record says which.
      const isAsm = /\.s$/.test(sourceLogical) || /\.asm\.o$/.test(slot.basename);
      freezeContractInLedger();
      const spec = isAsm
        ? writeSpec(stageName(), 'assembly', 'assembly', 'as',
          { sourceFile: sourceLinux, objectFile: dest, sourceRoot: srcRoot, objectRoot: objRoot }, 300)
        : writeSpec(stageName(), 'object.compile', 'object-compile', 'cxx',
          { sourceFile: sourceLinux, objectFile: dest, cxxflags, sourceRoot: srcRoot, objectRoot: objRoot }, 600);
      const r = run(spec);
      out.push(r);
      const landed = exists(toHost(dest)) ? fs.readFileSync(toHost(dest)) : null;
      rebuilt.push({ slot: slot.index, basename: slot.basename, stage: r.stage, status: r.status, kind: isAsm ? 'as' : 'cxx',
        source: sourceLogical, drift: slot.source_drift.map((d) => d.logical),
        object: landed ? { bytes: landed.length, sha256: sha(landed) } : null });
      if (r.status !== 0 || landed === null) problems.push({ slot: slot.index, basename: slot.basename, why: 'the rebuild did not produce an object',
        stage: r.stage, detail: r.detail });
    }
  }
  const total = reused.length + rebuilt.length;
  // The build writes its own record rather than only printing: a resume has to be able to tell a build that finished from one that
  // was interrupted, and stdout is not a record.
  {
    const target = R + '/p-build-142.json';
    const summary = { tag, objRoot, reused, rebuilt, total, expected: sel.slots.length,
      cxxflags, flags_from: R + '/profile-142', problems, ok: problems.length === 0 && total === sel.slots.length };
    if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
    fs.writeFileSync(target, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  }
  console.log(JSON.stringify({ tag, objRoot, reused: reused.length, rebuilt: rebuilt.length, total,
    expected: sel.slots.length, cxxflags, flags_from: R + '/profile-142', flags_equal_140_producer: true,
    problems: problems.slice(0, 8), problems_total: problems.length,
    rebuilt_rows: rebuilt, ok: problems.length === 0 && total === sel.slots.length }, null, 2));
  process.exit(problems.length === 0 && total === sel.slots.length ? 0 : 1);
}

// ================================================================ pverify
// Read back the producer directory and say, per slot, whether the object standing there is the one the selection allows. This is a
// separate phase because the build phase writes and this one only reads: a build that reports success and a directory that holds
// the right bytes are two different claims, and the link needs the second one.
if (phase === 'pverify') {
  const tag = process.argv[3] || 'p142';
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const objRoot = S + '/producers/' + tag;
  const rows = [];
  const problems = [];
  for (const slot of sel.slots) {
    const hostPath = toHost(objRoot + '/' + slot.basename);
    if (!exists(hostPath)) { problems.push({ slot: slot.index, basename: slot.basename, why: 'absent' }); continue; }
    const st = fs.lstatSync(hostPath);
    if (!st.isFile() || st.isSymbolicLink()) { problems.push({ slot: slot.index, basename: slot.basename, why: 'not an ordinary file' }); continue; }
    const b = fs.readFileSync(hostPath);
    const h = sha(b);
    // A reused slot must be its pin. A rebuilt slot must be a real object, and it must differ from the old pin when the source it
    // was built from changed: an identical hash there would mean the compiler ignored the change, which is not something to
    // assume silently in either direction.
    const equalsPin = slot.object.pinned_sha256 ? h === slot.object.pinned_sha256 && b.length === slot.object.pinned_bytes : null;
    let verdict = 'ok';
    if (slot.decision === 'reuse') { if (equalsPin !== true) { verdict = 'reused object is not its pin'; problems.push({ slot: slot.index, basename: slot.basename, why: verdict }); } }
    else if (b.length === 0) { verdict = 'rebuilt object is empty'; problems.push({ slot: slot.index, basename: slot.basename, why: verdict }); }
    else if (slot.decision === 'rebuild-source-changed' && equalsPin === true) {
      // A rebuilt object that lands on the old bytes is not a defect: the same compiler, the same flags and an unchanged
      // translation unit produce the same object, so a header that drifted without reaching codegen changes nothing. It is only
      // a defect when the translation unit itself changed and the object did not follow it, because then either the compiler
      // did not read the new source or the object standing here is not the one it produced.
      const primaryChanged = slot.source_drift.some((d) => d.logical === slot.logical);
      verdict = primaryChanged
        ? 'the translation unit changed but the rebuilt object is identical to the old one'
        : 'rebuilt, identical to the old object, translation unit unchanged (only a header drifted)';
      if (primaryChanged) problems.push({ slot: slot.index, basename: slot.basename, why: verdict });
    }
    rows.push({ slot: slot.index, basename: slot.basename, decision: slot.decision, bytes: b.length, sha256: h,
      equals_old_pin: equalsPin, verdict });
  }
  const summary = {
    tag, objRoot, slots: sel.slots.length, verified: rows.length,
    reuse: rows.filter((r) => r.decision === 'reuse').length,
    rebuilt: rows.filter((r) => r.decision !== 'reuse').length,
    rebuilt_that_differ_from_old_pin: rows.filter((r) => r.decision !== 'reuse' && r.equals_old_pin === false).length,
    rebuilt_identical_to_old_pin: rows.filter((r) => r.decision !== 'reuse' && r.equals_old_pin === true).length,
    total_bytes: rows.reduce((n, r) => n + r.bytes, 0),
    problems, ok: problems.length === 0 && rows.length === sel.slots.length,
  };
  const target = R + '/p-producers-142b.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify({ ...summary, rows }, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify(summary, null, 2));
  process.exit(summary.ok ? 0 : 1);
}

// ================================================================ plink
// Link the 61 individuals in the order the last published link used, taking each from this packet's own producer directory.
if (phase === 'plink') {
  const stage = process.argv[3];
  const tag = process.argv[4] || 'p142';
  const outTag = process.argv[5] || 'p142-default';
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-142.json', 'utf8'));
  const spec140 = JSON.parse(fs.readFileSync(C.parent_actual_final_link_spec.path, 'utf8'));
  const order = spec140.control.stage_options.individuals.map((l) => path.posix.basename(l));
  const objRoot = S + '/producers/' + tag;
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const byBase = new Map(sel.slots.map((s) => [s.basename, s]));
  const verified = [];
  const problems = [];
  for (const name of order) {
    const linux = objRoot + '/' + name;
    const hostPath = toHost(linux);
    if (!exists(hostPath)) { problems.push({ input: name, why: 'not on disk' }); continue; }
    const st = fs.lstatSync(hostPath);
    if (!st.isFile() || st.isSymbolicLink()) { problems.push({ input: name, why: 'not an ordinary file' }); continue; }
    const b = fs.readFileSync(hostPath);
    const slot = byBase.get(name);
    verified.push({ input: name, bytes: b.length, sha256: sha(b), decision: slot ? slot.decision : null,
      equals_pin: slot && slot.object ? sha(b) === slot.object.pinned_sha256 : null });
  }
  // The linker script is the current committed one, staged in the wave by pwave, not the 140 copy.
  const linkerLogical = 'linker.ld';
  const linkerLinux = S + '/' + (process.argv[6] || 'wave-01') + '/' + linkerLogical;
  if (!exists(toHost(linkerLinux))) problems.push({ input: linkerLinux, why: 'the current linker script is not staged' });
  if (problems.length) { console.log(JSON.stringify({ stage, ok: false, problems }, null, 2)); process.exit(1); }
  const imageDir = S + '/image';
  fs.mkdirSync(toHost(imageDir), { recursive: true });
  const elfFile = imageDir + '/' + outTag + '.elf';
  const mapFile = imageDir + '/' + outTag + '.map';
  freezeContractInLedger();
  const p = writeSpec(stage, 'image.link', 'image-link', 'ld',
    { individuals: order.map((n) => objRoot + '/' + n), mapFile, elfFile, linkerScript: linkerLinux, objectRoot: imageDir }, 900);
  out.push(run(p));
  const elfHost = toHost(elfFile);
  const mapHost = toHost(mapFile);
  console.log(JSON.stringify({ stage, outTag, order: order.length, verified: verified.length,
    decisions: { reuse: verified.filter((v) => v.decision === 'reuse').length,
      rebuilt: verified.filter((v) => v.decision && v.decision !== 'reuse').length },
    elf: exists(elfHost) ? pin(elfHost) : null, map: exists(mapHost) ? pin(mapHost) : null,
    out: out.map((r) => ({ stage: r.stage, status: r.status, line: r.line, detail: r.detail })) }, null, 2));
  process.exit(out.every((r) => r.status === 0) ? 0 : 1);
}

// ================================================================ pclosure
// Put the P evidence into one causal table. The question a reviewer asks is not "did it link" but "for every one of the 61 slots,
// what did this packet decide, on what evidence, and what is still unproven". Each row answers that, and the four buckets are
// exclusive: reuse (bytes and sources both proven), rebuilt-and-different (the source changed and the object followed it),
// rebuilt-and-identical (the translation unit did not change; only a header drifted, and the rebuild shows the drift is inert),
// and no-evidence-yet for everything this packet has not run.
if (phase === 'pclosure') {
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const build = JSON.parse(fs.readFileSync(R + '/p-build-142.json', 'utf8'));
  const prod = JSON.parse(fs.readFileSync(R + '/p-producers-142b.json', 'utf8'));
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-142.json', 'utf8'));
  const link = JSON.parse(fs.readFileSync(R + '/results/lk142-01.json', 'utf8'));
  const baseline = C.baseline_image;
  const elfPath = S + '/image/p142-default.elf';
  const elfHost = toHost(elfPath);
  const elfBytes = fs.readFileSync(elfHost);
  const prodByBase = new Map(prod.rows.map((r) => [r.basename, r]));
  const rows = sel.slots.map((s) => {
    const p = prodByBase.get(s.basename);
    const bucket = s.decision === 'reuse' ? 'reuse'
      : (p && p.equals_old_pin === true ? 'rebuilt-identical' : 'rebuilt-different');
    return { slot: s.index, basename: s.basename, logical: s.logical, decision: s.decision, bucket,
      object_bytes: p ? p.bytes : null, object_sha256: p ? p.sha256 : null,
      equals_old_pin: p ? p.equals_old_pin : null,
      evidence: s.decision === 'reuse'
        ? { object_matches_pin: s.object.matches_pin === true, sources_compared: s.compared,
          sources_changed: 0, why: 'the object is its pin and every file its compiler read is byte-identical to the current tree' }
        : { sources_compared: s.compared, sources_changed: s.source_drift.length,
          translation_unit_changed: s.source_drift.some((d) => d.logical === s.logical),
          changed: s.source_drift.map((d) => d.logical),
          why: p && p.equals_old_pin === true
            ? 'compiled again from the current tree; the object landed on the old bytes, so the header drift never reached codegen'
            : 'compiled again from the current tree and the object changed, as the changed source requires' } };
  });
  const buckets = {
    reuse: rows.filter((r) => r.bucket === 'reuse').length,
    rebuilt_different: rows.filter((r) => r.bucket === 'rebuilt-different').length,
    rebuilt_identical: rows.filter((r) => r.bucket === 'rebuilt-identical').length,
  };
  const outRec = {
    task: A.task,
    what: 'the causal table for the 61 link slots of this packet current image: what was reused, what was proven unaffected, '
      + 'what had to be rebuilt, and what still has no evidence',
    order_from: { spec: C.parent_actual_final_link_spec, count: sel.slots.length,
      why: 'the order of the last published link, not a list written for this packet' },
    compile_profile: { makefile: pin(R + '/profile-142/Makefile'), cxxflags: pin(R + '/profile-142/kernel-cxxflags'),
      flags: build.cxxflags, flags_equal_140_producer: true,
      note: 'the 140 producer recorded the same 23 flags; the frozen profile is the copy the bridge checks the caller against' },
    buckets,
    image: { linux: elfPath, bytes: elfBytes.length, sha256: sha(elfBytes),
      map: pin(toHost(S + '/image/p142-default.map')),
      stage: 'lk142-01', verdict: link.ok === true, guard: link.verdict || null },
    baseline_140: { path: baseline.path, bytes: baseline.bytes, sha256: baseline.sha256,
      differs_from_current: sha(elfBytes) !== baseline.sha256,
      why: '12 of the 61 objects were compiled again from the current tree; 7 of them changed, so the image is not the 140 image '
        + 'and cannot be. A byte-identical reproduction of the 140 image is not claimed.' },
    rows,
    no_evidence_yet: [
      'the current image has not been loaded or started: no guest has run against it in this packet',
      'the restricted scene matrix has not been run: no cold VM start, no per-channel raw comparison',
      'no in-process object, identifier or directory observation has been made against this image',
      'no physical relocation of the real artefacts has been performed, so the portable-consumption claim is unproven',
      'the production host consumer that must read the runtime inner originals has not been built in this packet',
    ],
    ok: link.ok === true && prod.ok === true && buckets.reuse + buckets.rebuilt_different + buckets.rebuilt_identical === 61,
  };
  const target = R + '/p-closure-142.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify(outRec, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ wrote: target, buckets, image: outRec.image, ok: outRec.ok }, null, 2));
  process.exit(outRec.ok ? 0 : 1);
}

// ================================================================ dpreserve
// Preserve the real originals this packet's current image descends from. Two rules, both of them about not losing evidence:
// nothing is ever overwritten, and every copy is read back and compared with the source after it lands. The preserved tree is a
// separate directory from every other record so that a later relocation test has a source of truth that is not the live artefact.
if (phase === 'dpreserve') {
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-142.json', 'utf8'));
  const R140 = R140_ROOT;
  const wanted = [
    { role: 'final-link-spec', path: C.parent_actual_final_link_spec.path },
    { role: 'producer-contract', path: R140 + '/producer-contract-p140-05.json' },
    { role: 'producer-closure', path: R140 + '/producer-closure-p140-05.json' },
    { role: 'link-input-read', path: R140 + '/link-input-read-p140-05.json' },
    { role: 'entry-link', path: R140 + '/entry-link-p140-10.json' },
    { role: 'default-link', path: R140 + '/default-link-p140-10.json' },
    { role: 'baseline-image', path: C.baseline_image.path },
    { role: 'baseline-map', path: C.baseline_map.path },
  ];
  const destRoot = R + '/preserved-142';
  fs.mkdirSync(destRoot, { recursive: true });
  const preserved = [];
  const problems = [];
  for (const w of wanted) {
    if (!exists(w.path)) { problems.push({ role: w.role, path: w.path, why: 'the original is not readable' }); continue; }
    const b = fs.readFileSync(w.path);
    const dest = destRoot + '/' + w.role + '-' + path.basename(w.path);
    if (exists(dest)) throw new Error('REFUSED: ' + dest + ' already exists');
    fs.writeFileSync(dest, b, { flag: 'wx' });
    const back = fs.readFileSync(dest);
    if (!back.equals(b)) throw new Error('the preserved copy is not the original: ' + dest);
    preserved.push({ role: w.role, original: w.path, preserved: dest, bytes: b.length, sha256: sha(b),
      verified_after_copy: true });
  }
  // The required-material list is independent of the selection record: it is built from what the artefacts themselves say they
  // consumed, so a relocation test can be written against it without consulting the build that produced them.
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const closure = JSON.parse(fs.readFileSync(R140 + '/producer-closure-p140-05.json', 'utf8'));
  const read = JSON.parse(fs.readFileSync(R140 + '/link-input-read-p140-05.json', 'utf8'));
  const inv = JSON.parse(fs.readFileSync(A.runtime_source_inventory.path, 'utf8'));
  const sourceNeeds = [];
  for (const r of read.inputs) {
    for (const s of (r.dep_sources_resolved || [])) {
      const m = /\/wave-[0-9]+\/(.+)$/.exec(String(s).replace(/\\/g, '/'));
      if (m && !sourceNeeds.some((x) => x.logical === m[1])) sourceNeeds.push({ logical: m[1], first_seen_in: r.slot });
    }
  }
  const required = {
    task: A.task,
    what: 'every material this packet current image and its consumers need, listed independently of the build that used them',
    producer_inputs: {
      count: sel.slots.length,
      objects: sel.slots.map((s) => ({ slot: s.index, basename: s.basename, logical: s.logical, decision: s.decision })),
      translation_units_and_headers: { count: sourceNeeds.length, rows: sourceNeeds },
      linker_script: sel.linker_script,
      compile_profile: [pin(R + '/profile-142/Makefile'), pin(R + '/profile-142/kernel-cxxflags')],
    },
    native_members: {
      count: inv.files.length,
      executables: inv.files.filter((f) => f.mode_octal === '0755').map((f) => f.logical),
      destination: A.runtime_destination,
      inventory: pin(A.runtime_source_inventory.path),
    },
    host_consumer_inputs: {
      runtime_inner_originals: [A.runtime_destination + '/root/usr/bin/qemu-system-i386',
        A.runtime_destination + '/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'],
      image: { linux: S + '/image/p142-default.elf', bytes: fs.statSync(toHost(S + '/image/p142-default.elf')).size },
      map: S + '/image/p142-default.map',
      inventory: pin(A.runtime_source_inventory.path),
    },
    rule: 'a relocation test removes the original location and calls the same production consumer on the moved artefact; a consumer '
      + 'that only works in place has not been shown to consume the artefact at all',
  };
  const outRec = { task: A.task, preserved, problems, required,
    ok: problems.length === 0 && preserved.length === wanted.length };
  const target = R + '/d-preserved-142.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify(outRec, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ wrote: target, preserved: preserved.map((p) => p.role + ' ' + p.bytes + ' ' + p.sha256.slice(0, 12)),
    producer_objects: sel.slots.length, source_needs: sourceNeeds.length, native_members: inv.files.length,
    problems, ok: outRec.ok }, null, 2));
  process.exit(outRec.ok ? 0 : 1);
}

// ================================================================ dreloc
// Physically move the real runtime tree to a different absolute path and call the same production reader on it. The reader has no
// knowledge of where the tree lives: it takes the root from its control and re-derives every file's identity from the filesystem.
// So a relocation is a real test of consumption, and the negative is the same reader on the same relocated tree with two real
// files genuinely gone.
if (phase === 'dreloc') {
  const tag = process.argv[3] || 'd1';
  const relocatedRoot = S + '/relocated-' + tag;
  const destination = relocatedRoot + '/runtime';
  const inv = JSON.parse(fs.readFileSync(A.runtime_source_inventory.path, 'utf8'));
  const executables = inv.files.filter((f) => f.mode_octal === '0755').map((f) => f.logical);
  const plain = inv.files.filter((f) => f.mode_octal === '0644').map((f) => f.logical);
  if (plain.length === 0) throw new Error('the inventory has no ordinary file to remove');
  const removeList = [plain[0], executables[0]];
  const copyOnlyArg = process.argv[4];
  const results = [];

  // One stage copies the tree. A second stage removes two real files from the copy. Both go through the guarded python path.
  // A copy-only relocation is what the positive needs: the reader must accept the tree where it now stands. The damaged copy is
  // what the negative needs. They are two separate destinations so that neither test can be explained by the other.
  const copyOnly = process.argv[4] === 'copyonly';
  const copyStage = copyOnly ? 'drl142-03' : 'drl142-01';
  const stages = copyOnly
    ? [{ stage: copyStage, mode: 'tree', remove: [] }]
    : [{ stage: copyStage, mode: 'tree', remove: [] },
      { stage: 'drl142-02', mode: 'remove', remove: removeList }];
  for (const s of stages) {
    const control = { task: A.task, stage: s.stage, mode: s.mode,
      source: A.runtime_destination, destination,
      remove: s.remove,
      report_path: S + '/evidence/' + s.stage + '.json' };
    putOrSame(toHost(S + '/frozen-helpers/' + s.stage + '.py'), fs.readFileSync(HERE + '/relocate-142.py'));
    putOrSame(toHost(S + '/inputs/' + s.stage + '.json'), Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
    freezeContractInLedger();
    const spec = writeSpec(s.stage, 'native.metadata', 'native-metadata', 'metadata', { limit: 1200 }, 1800);
    const r = run(spec);
    results.push(r);
    const repPath = toHost(S + '/evidence/' + s.stage + '.json');
    let rep = null;
    if (exists(repPath)) { try { rep = JSON.parse(fs.readFileSync(repPath, 'utf8')); } catch { rep = { unreadable: true }; } }
    s.report = rep ? { ok: rep.ok, counted: rep.counted || null, source_counted: rep.source_counted || null,
      removed: rep.removed || null, problems: (rep.problems || []).slice(0, 4) } : null;
    if (r.status !== 0) { console.log(JSON.stringify({ phase: 'dreloc', failed_stage: s.stage, report: s.report,
      detail: r.detail, out: results }, null, 2)); process.exit(1); }
  }
  console.log(JSON.stringify({ phase: 'dreloc', relocatedRoot, destination, removeList,
    copy: stages[0].report, remove: stages[1].report,
    out: results.map((r) => ({ stage: r.stage, status: r.status, line: r.line })) }, null, 2));
  process.exit(0);
}

// ================================================================ hunit
// Build the real runtime host unit from the current committed source and link it. This is the unit that carries the eleven named
// cases; it is not a test written for this packet, it is the packet's own existing unit, rebuilt from the tree as it stands.
//
// Ten production translation units have to be compiled with the declared host seam, because they are the ones that read a
// bootstrap processor identity, a CR3 or an interrupt mask that a host process does not have. The seam is a frozen, pinned macro
// list; the bridge refuses any flag set that is not exactly the production profile plus that list, so the exception cannot widen.
if (phase === 'hunit') {
  const rootArg = process.argv[3];
  const wave = process.argv[4] || 'wave-01';
  const tag = process.argv[5] || 'h142';
  const rootAlnum = rootArg.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*$/.test(rootAlnum)) throw new Error('bad stage root: ' + rootArg);
  let seq = 0;
  const stageName = () => rootAlnum + '-' + String(++seq).padStart(2, '0');
  const mk = fs.readFileSync(R + '/profile-142/Makefile', 'utf8');
  const baseLine = mk.split('\n').find((l) => l.startsWith('BASE_CXXFLAGS :='));
  if (!baseLine) throw new Error('the frozen Makefile declares no BASE_CXXFLAGS');
  const baseFlags = baseLine.replace('BASE_CXXFLAGS :=', '').trim()
    .replace('$(OPTIMIZATION)', '-O2').split(/\s+/).filter(Boolean);
  const intFlags = fs.readFileSync(R + '/profile-142/kernel-cxxflags', 'utf8').trim().split(/\s+/).filter(Boolean);
  const CXXFLAGS = [...baseFlags, ...intFlags];
  const DELTA_PATH = R + '/profile-142/host-test-delta-runtime.txt';
  const DELTA = fs.readFileSync(DELTA_PATH, 'utf8').split('\n').map((l) => l.trim())
    .filter((l) => l && !l.startsWith('#'));
  const SEAM = [...CXXFLAGS, ...DELTA.map((m) => '-D' + m)];
  const deltaPin = pin(DELTA_PATH);
  const srcRoot = S + '/' + wave;
  const objRoot = S + '/test-build/' + tag;
  fs.mkdirSync(toHost(objRoot), { recursive: true });

  // The production units the unit links against. They are compiled from the wave, which is the current committed tree, with the
  // seam; everything else in them is the production code.
  const productionUnits = [
    'src/process/native_runtime.cpp', 'src/gdt.cpp', 'src/process/native_fp.cpp', 'src/multitasking.cpp',
    'src/process/native_realtime.cpp', 'src/memory/physical.cpp', 'src/memory/process_address_space.cpp',
    'src/memory/paging.cpp', 'src/process/native_surface.cpp', 'src/process/native_windows.cpp',
  ];
  // The unit's own translation units, plus the host seam implementation and the assembly entry that hands control to it.
  const testUnits = ['tests/runtime_test.cpp', 'tests/runtime_boundary.cpp', 'tests/gdt_host_seam.cpp'];
  const startSource = 'tests/runtime_start.s';
  const linkerScript = 'tests/runtime_host.ld';

  const objs = [];
  const stages = [];
  const problems = [];
  for (const logical of [...productionUnits, ...testUnits]) {
    const src = srcRoot + '/' + logical;
    if (!exists(toHost(src))) { problems.push({ logical, why: 'not in the wave' }); continue; }
    const obj = objRoot + '/' + path.basename(logical, '.cpp') + '.o';
    freezeContractInLedger();
    const spec = writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: src, objectFile: obj, cxxflags: SEAM, host_test_delta_path: deltaPin.path,
        sourceRoot: srcRoot, objectRoot: objRoot }, 700);
    const r = run(spec);
    stages.push(r);
    objs.push(obj);
    if (r.status !== 0) problems.push({ logical, why: 'the compile stage failed', stage: r.stage, detail: r.detail });
  }
  if (problems.length) {
    console.log(JSON.stringify({ phase: 'hunit', ok: false, problems: problems.slice(0, 6),
      stages: stages.map((s) => ({ stage: s.stage, status: s.status, detail: s.detail.slice(0, 200) })) }, null, 2));
    process.exit(1);
  }
  // The unit also needs the objects whose sources the seam build does not recompile. They come from this packet's own producer
  // directory, which pverify already read back against the selection, and each one is hashed here before the link sees it.
  const extraNames = (process.argv[6] || '').split(',').map((s) => s.trim()).filter(Boolean);
  const producerRoot = S + '/producers/' + (process.argv[7] || 'p142b');
  const prod = JSON.parse(fs.readFileSync(R + '/p-producers-142b.json', 'utf8'));
  const prodByBase = new Map(prod.rows.map((r) => [r.basename, r]));
  for (const name of extraNames) {
    const hostPath = toHost(producerRoot + '/' + name);
    if (!exists(hostPath)) { problems.push({ logical: name, why: 'the extra object is not on disk' }); continue; }
    const b = fs.readFileSync(hostPath);
    const row = prodByBase.get(name);
    if (row && row.sha256 !== sha(b)) { problems.push({ logical: name, why: 'the extra object is not its verified bytes' }); continue; }
    objs.push(producerRoot + '/' + name);
  }
  if (problems.length) {
    console.log(JSON.stringify({ phase: 'hunit', ok: false, stage: 'extra-objects', problems }, null, 2));
    process.exit(1);
  }
  const startObj = objRoot + '/runtime_start.o';
  freezeContractInLedger();
  const asSpec = writeSpec(stageName(), 'assembly', 'assembly', 'as',
    { sourceFile: srcRoot + '/' + startSource, objectFile: startObj, sourceRoot: srcRoot, objectRoot: objRoot }, 240);
  const asRun = run(asSpec);
  stages.push(asRun);
  if (asRun.status !== 0) {
    console.log(JSON.stringify({ phase: 'hunit', ok: false, stage: 'assembly', detail: asRun.detail }, null, 2));
    process.exit(1);
  }
  const elfFile = objRoot + '/runtime_test.elf';
  const mapFile = objRoot + '/runtime_test.map';
  freezeContractInLedger();
  const ldSpec = writeSpec(stageName(), 'image.link', 'image-link', 'ld',
    { individuals: [startObj, ...objs], mapFile, elfFile, linkerScript: srcRoot + '/' + linkerScript,
      objectRoot: objRoot }, 300);
  const ldRun = run(ldSpec);
  stages.push(ldRun);
  const elfHost = toHost(elfFile);
  const elf = exists(elfHost) ? pin(elfHost) : null;
  // Every file this unit was compiled from, hashed here at the moment the unit exists. The consumer re-reads each one and
  // compares, so "built from the current tree" is a statement about bytes rather than a sentence in a description field.
  const inputs = [];
  const addInput = (logical, hostPath, role) => {
    const b = fs.readFileSync(hostPath);
    inputs.push({ logical, role, host_path: hostPath, bytes: b.length, sha256: sha(b) });
  };
  for (const logical of [...productionUnits, ...testUnits, startSource, linkerScript]) {
    addInput(logical, toHost(srcRoot + '/' + logical), logical.startsWith('tests/') ? 'test' : 'production');
  }
  for (const name of extraNames) addInput('producer/' + name, toHost(producerRoot + '/' + name), 'producer-object');
  addInput('profile/Makefile', R + '/profile-142/Makefile', 'profile');
  addInput('profile/kernel-cxxflags', R + '/profile-142/kernel-cxxflags', 'profile');
  addInput('profile/host-test-delta-runtime.txt', DELTA_PATH, 'seam');
  const summary = {
    phase: 'hunit', tag, elfFile, elf,
    inputs,
    units: { production: productionUnits.length, test: testUnits.length, objects: objs.length },
    seam: { path: deltaPin.path, bytes: deltaPin.bytes, sha256: deltaPin.sha256, macros: DELTA,
      flags_count: SEAM.length },
    production_flags_count: CXXFLAGS.length,
    stages: stages.map((s) => ({ stage: s.stage, status: s.status })),
    ok: ldRun.status === 0 && elf !== null,
  };
  const target = R + '/h-unit-build-' + tag + '-142.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify(summary, null, 2));
  process.exit(summary.ok ? 0 : 1);
}

// ================================================================ unit
// The two units the required host set also names, built the same way the runtime unit is: the production body, the unit's own
// test, its own entry, and the repository's own link script, under the same frozen seam. They do not touch paging, so they keep
// the conventional layout.
if (phase === 'unit') {
  const stageRoot = process.argv[3];
  const wave = process.argv[4] || 'wave-03';
  const tag = process.argv[5] || 'u142';
  const helperLogical = process.argv[6] || '';
  const testLogical = process.argv[7];
  const startLogical = process.argv[8];
  const ldLogical = process.argv[9] || 'tests/safe_tss.ld';
  const extras = (process.argv[10] || '').split(',').map((s) => s.trim()).filter(Boolean);
  const rootAlnum = stageRoot.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*$/.test(rootAlnum)) throw new Error('bad stage root: ' + stageRoot);
  let seq = 0;
  const stageName = () => rootAlnum + '-' + String(++seq).padStart(2, '0');
  const mk = fs.readFileSync(R + '/profile-142/Makefile', 'utf8');
  const baseLine = mk.split('\n').find((l) => l.startsWith('BASE_CXXFLAGS :='));
  const baseFlags = baseLine.replace('BASE_CXXFLAGS :=', '').trim()
    .replace('$(OPTIMIZATION)', '-O2').split(/\s+/).filter(Boolean);
  const intFlags = fs.readFileSync(R + '/profile-142/kernel-cxxflags', 'utf8').trim().split(/\s+/).filter(Boolean);
  const CXXFLAGS = [...baseFlags, ...intFlags];
  const DELTA_PATH = R + '/profile-142/host-test-delta-runtime.txt';
  const DELTA = fs.readFileSync(DELTA_PATH, 'utf8').split('\n').map((l) => l.trim())
    .filter((l) => l && !l.startsWith('#'));
  const SEAM = [...CXXFLAGS, ...DELTA.map((m) => '-D' + m)];
  const deltaPin = pin(DELTA_PATH);
  const srcRoot = S + '/' + wave;
  const objRoot = S + '/test-build/' + tag;
  fs.mkdirSync(toHost(objRoot), { recursive: true });
  const base = path.basename(testLogical, '.cpp');
  const helperList = helperLogical ? helperLogical.split(',') : [];
  const helperObjs = helperList.map((h) => objRoot + '/' + path.basename(h, '.cpp') + '.o');
  const testObj = objRoot + '/' + base + '.o';
  const startObj = objRoot + '/start.o';
  const elfFile = objRoot + '/' + base + '.elf';
  const mapFile = objRoot + '/' + base + '.map';
  const stages = [];
  const problems = [];
  const compiled = [];
  for (let i = 0; i < helperList.length; i++) {
    if (!exists(toHost(srcRoot + '/' + helperList[i]))) { problems.push({ logical: helperList[i], why: 'not in the wave' }); continue; }
    freezeContractInLedger();
    const r = run(writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: srcRoot + '/' + helperList[i], objectFile: helperObjs[i], cxxflags: SEAM,
        host_test_delta_path: deltaPin.path, sourceRoot: srcRoot, objectRoot: objRoot }, 480));
    stages.push(r); compiled.push(helperList[i]);
    if (r.status !== 0) problems.push({ logical: helperList[i], why: 'compile failed', detail: r.detail });
  }
  if (exists(toHost(srcRoot + '/' + testLogical))) {
    freezeContractInLedger();
    const r = run(writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: srcRoot + '/' + testLogical, objectFile: testObj, cxxflags: SEAM,
        host_test_delta_path: deltaPin.path, sourceRoot: srcRoot, objectRoot: objRoot }, 480));
    stages.push(r); compiled.push(testLogical);
    if (r.status !== 0) problems.push({ logical: testLogical, why: 'compile failed', detail: r.detail });
  } else problems.push({ logical: testLogical, why: 'not in the wave' });
  if (exists(toHost(srcRoot + '/' + startLogical))) {
    freezeContractInLedger();
    const r = run(writeSpec(stageName(), 'assembly', 'assembly', 'as',
      { sourceFile: srcRoot + '/' + startLogical, objectFile: startObj, sourceRoot: srcRoot, objectRoot: objRoot }, 240));
    stages.push(r); compiled.push(startLogical);
    if (r.status !== 0) problems.push({ logical: startLogical, why: 'assembly failed', detail: r.detail });
  } else problems.push({ logical: startLogical, why: 'not in the wave' });
  const prod = JSON.parse(fs.readFileSync(R + '/p-producers-142b.json', 'utf8'));
  const prodByBase = new Map(prod.rows.map((r) => [r.basename, r]));
  const producerRoot = S + '/producers/p142b';
  const extraObjs = [];
  for (const name of extras) {
    const hostPath = toHost(producerRoot + '/' + name);
    if (!exists(hostPath)) { problems.push({ logical: name, why: 'the extra object is not on disk' }); continue; }
    const row = prodByBase.get(name);
    if (!row || row.sha256 !== sha(fs.readFileSync(hostPath))) { problems.push({ logical: name, why: 'not its verified bytes' }); continue; }
    extraObjs.push(producerRoot + '/' + name);
  }
  if (problems.length) {
    console.log(JSON.stringify({ phase: 'unit', ok: false, problems: problems.slice(0, 6),
      stages: stages.map((s) => ({ stage: s.stage, status: s.status, detail: s.detail.slice(0, 200) })) }, null, 2));
    process.exit(1);
  }
  freezeContractInLedger();
  const ldRun = run(writeSpec(stageName(), 'image.link', 'image-link', 'ld',
    { individuals: [startObj, ...helperObjs, testObj, ...extraObjs], mapFile, elfFile,
      linkerScript: srcRoot + '/' + ldLogical, objectRoot: objRoot }, 300));
  stages.push(ldRun);
  const elfHost = toHost(elfFile);
  const elf = exists(elfHost) ? pin(elfHost) : null;
  const inputs = [];
  const addInput = (logical, hostPath, role) => {
    const b = fs.readFileSync(hostPath);
    inputs.push({ logical, role, host_path: hostPath, bytes: b.length, sha256: sha(b) });
  };
  for (const logical of compiled) addInput(logical, toHost(srcRoot + '/' + logical), logical.startsWith('tests/') ? 'test' : 'production');
  for (const name of extras) addInput('producer/' + name, toHost(producerRoot + '/' + name), 'producer-object');
  addInput('profile/Makefile', R + '/profile-142/Makefile', 'profile');
  addInput('profile/kernel-cxxflags', R + '/profile-142/kernel-cxxflags', 'profile');
  addInput('profile/host-test-delta-runtime.txt', DELTA_PATH, 'seam');
  const summary = { phase: 'unit', tag, unit: base, elfFile, elf, inputs,
    seam: { path: deltaPin.path, bytes: deltaPin.bytes, sha256: deltaPin.sha256, macros: DELTA, flags_count: SEAM.length },
    stages: stages.map((s) => ({ stage: s.stage, status: s.status })),
    ok: ldRun.status === 0 && elf !== null };
  const target = R + '/h-unit-build-' + base + '-' + tag + '-142.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ phase: 'unit', unit: base, elf, ok: summary.ok,
    stages: stages.map((s) => ({ stage: s.stage, status: s.status })) }, null, 2));
  process.exit(summary.ok ? 0 : 1);
}

// ================================================================ rununit
// Run a built unit once, as its own guarded process, with the same host-unit route the runtime cases use.
if (phase === 'rununit') {
  const stage = process.argv[3];
  const tag = process.argv[4] || 'u142';
  const elfBase = process.argv[5];
  const selectorArg = process.argv[6];
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const elfLinux = S + '/test-build/' + tag + '/' + elfBase;
  if (!exists(toHost(elfLinux))) throw new Error('no unit ELF at ' + elfLinux);
  const workRoot = S + '/unit-run/' + stage;
  fs.mkdirSync(toHost(workRoot + '/logs'), { recursive: true });
  const options = { testElfLinux: elfLinux, workRoot, limit: 240 };
  if (selectorArg !== undefined && selectorArg !== '') options.caseSelector = Number(selectorArg);
  freezeContractInLedger();
  const spec = writeSpec(stage, 'test.cpp.run', 'host-unit-run', 'hostunit', options, 300);
  out.push(run(spec));
  const evidence = R + '/evidence/' + stage + '.json';
  let d = null;
  if (exists(evidence)) { try { d = JSON.parse(fs.readFileSync(evidence, 'utf8')); } catch { d = null; } }
  console.log(JSON.stringify({ stage, elf: pin(toHost(elfLinux)),
    verdict: d ? d.verdict : null,
    inner_report: d ? d.inner_report : null, raw: d ? d.raw : null,
    ok: d ? d.ok : null,
    out: out.map((r) => ({ stage: r.stage, status: r.status, detail: r.detail.slice(0, 200) })) }, null, 2));
  process.exit(d && d.ok ? 0 : 1);
}

// ================================================================ hrun
// Run one case of the runtime unit as its own process. Each case is a separate launch because a successful Activate publishes a
// process-wide owner that has no destroy path: two activations cannot share a process, so a second case in the same process would
// be measuring the first one's leftovers.
if (phase === 'hrun') {
  const caseNumber = Number(process.argv[3]);
  const tag = process.argv[4] || 'h142';
  if (!Number.isInteger(caseNumber) || caseNumber < 1 || caseNumber > 11) throw new Error('case must be 1..11');
  const suffix = process.argv[5] || '';
  const stage = 'hcase142' + suffix + '-' + String(caseNumber).padStart(2, '0');
  const elfLinux = S + '/test-build/' + tag + '/runtime_test.elf';
  if (!exists(toHost(elfLinux))) throw new Error('no unit ELF at ' + elfLinux);
  const workRoot = S + '/unit-run/' + stage;
  fs.mkdirSync(toHost(workRoot + '/logs'), { recursive: true });
  freezeContractInLedger();
  const spec = writeSpec(stage, 'test.cpp.run', 'host-unit-run', 'hostunit',
    { testElfLinux: elfLinux, workRoot, caseSelector: caseNumber, limit: 240 }, 300);
  const r = run(spec);
  out.push(r);
  // The raw channel is the unit's own output. It is read from the guard's copy, not from a stream this process captured.
  let rawText = null;
  const rawPath = toHost(workRoot + '/logs/child-output-' + stage + '.raw');
  for (let i = 0; i < 40 && rawText === null; i++) {
    try { rawText = fs.readFileSync(rawPath, 'utf8'); } catch {
      const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, 250);
    }
  }
  const repPath = R + '/results/' + stage + '.json';
  let rep = null;
  if (exists(repPath)) { try { rep = JSON.parse(fs.readFileSync(repPath, 'utf8')); } catch { rep = null; } }
  const summary = {
    phase: 'hrun', case: caseNumber, stage, status: r.status,
    verdict: rep ? rep.verdict : null,
    raw_bytes: rawText === null ? 0 : rawText.length,
    raw_sha256: rawText === null ? null : sha(Buffer.from(rawText, 'utf8')),
    raw_text: rawText,
    // The runner record carries ok rather than status for this route, so a stage that exited zero is recognised either way.
    ok: (r.status === 0 || r.ok === true) && rep !== null && rep.ok === true,
  };
  const target = R + '/h-case' + suffix + '-' + String(caseNumber).padStart(2, '0') + '-142.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ case: caseNumber, stage, ok: summary.ok, status: r.status,
    raw_bytes: summary.raw_bytes, detail: r.detail.slice(0, 300) }, null, 2));
  process.exit(summary.ok ? 0 : 1);
}

// ================================================================ hconsume
// Call the production host consumer on the real chain, then call the same entry again on seven deliberately damaged copies of it.
// The point of the negatives is not that they fail; it is that they fail through the same entry, with the specific defect named,
// and without launching anything.

// ================================================================ hconsume
// The host consumer, called on the chain this packet actually produced. The contract names which stage ran each case and which
// ELF each unit is; how many cases the runtime unit has, and that all three units are required, are the consumer's own constants.
// A contract that lists fewer cases than the consumer requires is refused, so shrinking the contract changes nothing.
if (phase === 'hconsume') {
  const stage = process.argv[3];
  const tag = process.argv[4] || 'h142f';
  const suffix = process.argv[5] || 'c';
  const bdTag = process.argv[6] || 'u142';
  const stTag = process.argv[7] || 'u142b';
  const outSuffix = process.argv[8] || '';
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const buildRecord = R + '/h-unit-build-' + tag + '-142.json';
  const bdRecord = R + '/h-unit-build-boot_diagnostic.test-' + bdTag + '-142.json';
  const stRecord = R + '/h-unit-build-safe_tss_test-' + stTag + '-142.json';
  for (const p of [buildRecord, bdRecord, stRecord]) if (!exists(p)) throw new Error('absent build record: ' + p);
  const bd = JSON.parse(fs.readFileSync(bdRecord, 'utf8'));
  const st = JSON.parse(fs.readFileSync(stRecord, 'utf8'));
  const runtimeElfLinux = S + '/test-build/' + tag + '/runtime_test.elf';
  const runtimeElfHost = toHost(runtimeElfLinux);
  const cases = [];
  for (let n = 1; n <= 11; n++) {
    cases.push({ case: n, stage: 'hcase142' + suffix + '-' + String(n).padStart(2, '0') });
  }
  const contract = {
    task: A.task,
    what: 'the host chain this packet produced: three units, the runtime unit case by case, each case its own guarded process',
    units: [
      { role: 'runtime', elf: pin(runtimeElfHost), elf_linux: runtimeElfLinux, cases },
      { role: 'boot_diagnostic', elf: pin(toHost(bd.elfFile)), elf_linux: bd.elfFile,
        cases: [{ case: 1, stage: 'bdrun142-01' }] },
      { role: 'safe_tss', elf: pin(toHost(st.elfFile)), elf_linux: st.elfFile,
        cases: [{ case: 1, stage: 'saferun142-01' }] },
    ],
    note: 'the case counts and the membership of the set are the consumer own constants; this contract names stages and ELFs only',
  };
  const contractPath = R + '/h-contract-142' + (outSuffix || 'e') + '.json';
  if (exists(contractPath)) throw new Error('REFUSED: ' + contractPath + ' already exists');
  fs.writeFileSync(contractPath, JSON.stringify(contract, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  const control = {
    task: A.task,
    build_record_path: buildRecord,
    contract_path: contractPath,
    unit_elf_host_path: runtimeElfHost,
    unit_elf_linux: runtimeElfLinux,
    results_dir: R + '/results',
  };
  const controlPath = R + '/stage-output/hconsume-' + stage + '.json';
  fs.writeFileSync(controlPath, JSON.stringify(control, null, 2) + '\n', { encoding: 'utf8' });
  const verdictPath = R + '/h-consumption-142' + (outSuffix || 'e') + '.json';
  if (exists(verdictPath)) throw new Error('REFUSED: ' + verdictPath + ' already exists');
  // The order log is read before and after, so "this consumer started nothing" is measured against the guard's own ledger of
  // every launch this packet made, not only against the consumer's own counter.
  const ledgerBefore = exists(ORDER_LOG) ? fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean).length : 0;
  const consumer = (await import('file:///' + HERE + '/host-consume-142.mjs')).consumeHostChain;
  const verdict = consumer(control);
  const ledgerAfter = exists(ORDER_LOG) ? fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean).length : 0;
  verdict.no_child_measurement = {
    consumer_spawn_attempts: verdict.spawn_attempts,
    order_log_lines_before: ledgerBefore,
    order_log_lines_after: ledgerAfter,
    order_log_unchanged: ledgerBefore === ledgerAfter,
    why: 'the order log records every stage this packet starts; a consumer that launched something to find out would have moved it',
  };
  fs.writeFileSync(verdictPath, JSON.stringify(verdict, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ stage, contract: pin(contractPath), verdict: pin(verdictPath),
    accepted: verdict.accepted, refused: verdict.refused,
    counts: verdict.counts, problems: verdict.problems.slice(0, 6), problem_count: verdict.problems.length,
    no_child: verdict.no_child_measurement,
    units: verdict.observed.units.map((u) => ({ role: u.role, listed: u.listed_cases, elf: u.elf && u.elf.matches })) }, null, 2));
  process.exit(verdict.accepted ? 0 : 1);
}

// ================================================================ hneg
// The same consumer, on the same chain, with exactly one real dependency broken in each case. Every negative is a fresh copy of
// the whole chain into a tree this phase owns: the result records, the inner reports, the raw channels, the contract and the build
// record. The mutation is applied to the copy, and where the mutation changes a file that another record hashes, the copy is
// resealed so the records stay consistent with each other and the only broken thing is the one thing the negative names.
//
// That resealing is the point. A negative that leaves a hash stale is refused because the hash does not match, which proves the
// hash is checked and nothing else. Resealing first means the refusal has to come from the consumer actually reading the fact the
// negative destroyed.
if (phase === 'hneg') {
  const stage = process.argv[3];
  const outSuffix = process.argv[4] || 'a';
  const tag = 'h142f';
  const suffix = 'c';
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const bdTag = 'u142';
  const stTag = 'u142b';
  const treeRoot = S + '/tmp/neg-' + stage;
  const buildRecordReal = R + '/h-unit-build-' + tag + '-142.json';
  const contractReal = R + '/h-contract-142h.json';
  for (const p of [buildRecordReal, contractReal]) if (!exists(p)) throw new Error('absent: ' + p);
  const stageNames = [];
  for (let n = 1; n <= 11; n++) stageNames.push('hcase142' + suffix + '-' + String(n).padStart(2, '0'));
  stageNames.push('bdrun142-01', 'saferun142-01');
  const consume = (await import('file:///' + HERE + '/host-consume-142.mjs')).consumeHostChain;
  const runtimeElfLinux = S + '/test-build/' + tag + '/runtime_test.elf';

  const writeJson = (p, v) => fs.writeFileSync(toHost(p), JSON.stringify(v, null, 2) + '\n', { encoding: 'utf8' });
  // A record may name either domain. The records written by the bridge carry host paths and the inner reports carry Linux paths,
  // so a translation is applied only to the Linux form. Applying it to a host path a second time produces a path with the share
  // name in it twice, which is exactly what a first attempt here did.
  const hostOf = (p) => (/^[A-Za-z]:[\\/]/.test(p) || /^[\\/]{2}/.test(p) ? p.replace(/\//g, '\\') : toHost(p));
  // Reseal the raw channel into the inner report, and the inner report into the result record. After this the three agree with
  // each other, so a negative that changed the raw channel is refused for what it did rather than for a hash it left behind.
  const resealInner = (dest, s) => {
    const recPath = dest + '/results/' + s + '.json';
    const rec = JSON.parse(fs.readFileSync(toHost(recPath), 'utf8'));
    const innerPath = rec.inner_report.path;
    const inner = JSON.parse(fs.readFileSync(hostOf(innerPath), 'utf8'));
    const raw = fs.readFileSync(hostOf(inner.child.output_path));
    inner.child.output_bytes = raw.length;
    inner.child.output_sha256 = sha(raw);
    writeJson(innerPath, inner);
    const b = fs.readFileSync(toHost(innerPath));
    rec.inner_report.bytes = b.length;
    rec.inner_report.sha256 = sha(b);
    writeJson(recPath, rec);
  };
  const copyChain = (dest) => {
    fs.mkdirSync(toHost(dest + '/results'), { recursive: true });
    fs.mkdirSync(toHost(dest + '/logs'), { recursive: true });
    for (const s of stageNames) {
      const rec = JSON.parse(fs.readFileSync(R + '/results/' + s + '.json', 'utf8'));
      const innerDst = dest + '/logs/inner-report-' + s + '.json';
      const rawDst = dest + '/logs/child-output-' + s + '.raw';
      fs.writeFileSync(toHost(innerDst), fs.readFileSync(hostOf(rec.inner_report.path)));
      fs.writeFileSync(toHost(rawDst), fs.readFileSync(hostOf(rec.raw.path)));
      const inner = JSON.parse(fs.readFileSync(toHost(innerDst), 'utf8'));
      inner.child.output_path = rawDst;
      writeJson(innerDst, inner);
      rec.inner_report.path = innerDst;
      rec.inner_report.linux_path = innerDst;
      rec.raw.path = rawDst;
      rec.raw.linux_path = rawDst;
      const ib = fs.readFileSync(toHost(innerDst));
      rec.inner_report.bytes = ib.length;
      rec.inner_report.sha256 = sha(ib);
      writeJson(dest + '/results/' + s + '.json', rec);
    }
    fs.copyFileSync(toHost(buildRecordReal), toHost(dest + '/build.json'));
    fs.copyFileSync(toHost(contractReal), toHost(dest + '/contract.json'));
    return dest;
  };
  const controlFor = (dest) => ({
    task: A.task,
    build_record_path: dest + '/build.json',
    contract_path: dest + '/contract.json',
    unit_elf_host_path: toHost(runtimeElfLinux),
    unit_elf_linux: runtimeElfLinux,
    results_dir: dest + '/results',
  });
  const readModify = (p, f) => { const v = JSON.parse(fs.readFileSync(toHost(p), 'utf8')); f(v); writeJson(p, v); return v; };
  const rawOf = (dest, s) => dest + '/logs/child-output-' + s + '.raw';
  const innerOf = (dest, s) => dest + '/logs/inner-report-' + s + '.json';
  const recOf = (dest, s) => dest + '/results/' + s + '.json';

  const negatives = [
    { name: 'child-return-code-nonzero', why: 'the child failed and the record was resealed to say so',
      expect: ['the child did not exit zero'],
      mutate: (dest) => {
        readModify(innerOf(dest, 'hcase142c-05'), (v) => { v.child.returncode = 1; });
        resealInner(dest, 'hcase142c-05');
      } },
    { name: 'child-argv-rewritten', why: 'the case ran a different binary than the one the unit names',
      expect: ['launched with a different argv'],
      mutate: (dest) => {
        readModify(innerOf(dest, 'hcase142c-07'), (v) => { v.argv[0] = S + '/test-build/u142b/safe_tss_test.elf'; });
        resealInner(dest, 'hcase142c-07');
      } },
    { name: 'inner-report-deleted', why: 'the run left no account of itself, so the record is the only voice',
      expect: ['absent inner report'],
      mutate: (dest) => { fs.unlinkSync(toHost(innerOf(dest, 'hcase142c-02'))); } },
    { name: 'exit-layers-disagree', why: 'the guard failed a launch the child reports as clean',
      expect: ['the three exit layers disagree'],
      mutate: (dest) => {
        readModify(recOf(dest, 'hcase142c-04'), (v) => { v.guard_row.exit_code = 1; });
      } },
    { name: 'transport-timed-out', why: 'the transport ran out of time and the record still carries the case',
      expect: ['the transport timed out'],
      mutate: (dest) => {
        readModify(recOf(dest, 'hcase142c-06'), (v) => { v.transport.timed_out = true; });
      } },
    { name: 'raw-channel-truncated', why: 'the channel is not the bytes the run produced, and nothing was resealed',
      expect: ['raw channel for case 3 is a different size'],
      mutate: (dest) => {
        const p = toHost(rawOf(dest, 'hcase142c-03'));
        const b = fs.readFileSync(p);
        fs.writeFileSync(p, b.subarray(0, b.length - 4));
      } },
    { name: 'pass-marker-removed', why: 'the unit no longer says it passed, and the hashes were resealed to hide it',
      expect: ['carries no pass marker'],
      mutate: (dest) => {
        const p = toHost(rawOf(dest, 'hcase142c-08'));
        fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('RUNTIME UNIT PASS', 'RUNTIME UNIT DONE'));
        resealInner(dest, 'hcase142c-08');
      } },
    { name: 'case-marker-removed', why: 'the channel is from another case, and the hashes were resealed to hide it',
      expect: ['is not from this case'],
      mutate: (dest) => {
        const p = toHost(rawOf(dest, 'hcase142c-09'));
        fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('CASE 9', 'CASE 1'));
        resealInner(dest, 'hcase142c-09');
      } },
    { name: 'unit-dropped-from-set', why: 'the set is one unit short and the contract no longer mentions it',
      expect: ['required unit is absent'],
      mutate: (dest) => {
        readModify(dest + '/contract.json', (v) => { v.units = v.units.filter((u) => u.role !== 'safe_tss'); });
      } },
    { name: 'unit-case-dropped', why: 'a case is quietly removed instead of repaired',
      expect: ['does not carry every case'],
      mutate: (dest) => {
        readModify(dest + '/contract.json', (v) => {
          const u = v.units.find((x) => x.role === 'runtime');
          u.cases = u.cases.filter((c) => c.case !== 10);
        });
      } },
    { name: 'unit-elf-rewritten', why: 'the unit points at a different binary than the one that ran',
      expect: ['ELF for safe_tss has different bytes at the same size'],
      mutate: (dest) => {
        readModify(dest + '/contract.json', (v) => {
          const u = v.units.find((x) => x.role === 'safe_tss');
          u.elf.sha256 = 'f'.repeat(64);
        });
      } },
    { name: 'build-input-rewritten', why: 'the unit claims a source it was not built from',
      expect: ['build input', 'has different bytes at the same size'],
      mutate: (dest) => {
        readModify(dest + '/build.json', (v) => {
          const i = v.inputs.find((x) => x.role === 'production');
          i.sha256 = 'e'.repeat(64);
        });
      } },
  ];

  const results = [];
  let acceptedByMistake = 0;
  let wrongReason = 0;
  for (const neg of negatives) {
    const dest = treeRoot + '/' + neg.name;
    if (exists(toHost(dest))) throw new Error('REFUSED: the negative tree already exists: ' + dest);
    copyChain(dest);
    const before = exists(ORDER_LOG) ? fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean).length : 0;
    neg.mutate(dest);
    const v = consume(controlFor(dest));
    const after = exists(ORDER_LOG) ? fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean).length : 0;
    const texts = v.problems.map((p) => [p.why, p.role, p.case, p.member, p.path].filter(Boolean).join(' '));
    const hit = neg.expect.every((e) => texts.some((t) => t.includes(e)));
    if (!v.refused) acceptedByMistake += 1;
    if (v.refused && !hit) wrongReason += 1;
    results.push({ name: neg.name, why: neg.why, refused: v.refused, expected: neg.expect, hit_target: hit,
      problems: texts.slice(0, 4), problem_count: v.problems.length,
      spawn_attempts: v.spawn_attempts, order_log_moved: before !== after,
      tree: dest });
  }
  const summary = {
    phase: 'hneg', stage, tree_root: treeRoot,
    negatives: results.length,
    refused: results.filter((r) => r.refused).length,
    hit_target: results.filter((r) => r.hit_target).length,
    accepted_by_mistake: acceptedByMistake,
    wrong_reason: wrongReason,
    any_spawn: results.some((r) => r.spawn_attempts > 0),
    any_order_log_movement: results.some((r) => r.order_log_moved),
    results,
    ok: results.length === negatives.length && acceptedByMistake === 0 && wrongReason === 0,
    ok_means: 'each negative is a fresh copy of the whole chain with exactly one real dependency broken; the same consumer had to '
      + 'refuse it for the reason the negative names, and had to start nothing while doing it',
  };
  const target = R + '/h-negatives-142' + outSuffix + '.json';
  if (exists(target)) throw new Error('REFUSED: ' + target + ' already exists');
  fs.writeFileSync(target, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ stage, negatives: summary.negatives, refused: summary.refused,
    hit_target: summary.hit_target, accepted_by_mistake: summary.accepted_by_mistake,
    wrong_reason: summary.wrong_reason, any_spawn: summary.any_spawn,
    failures: results.filter((r) => !r.refused || !r.hit_target).map((r) => ({ name: r.name, refused: r.refused,
      expected: r.expected, problems: r.problems })), ok: summary.ok }, null, 2));
  process.exit(summary.ok ? 0 : 1);
}

// ================================================================ pstatic
// The static byte gate over the current image. It is a separate stage because it runs inside the distribution, where the file's
// bytes and its permissions are both observable, and because a gate that ran in the same process as the link could pass on the
// link's own word.
if (phase === 'pstatic') {
  const stage = process.argv[3];
  const imageTag = process.argv[4] || 'p142-default';
  const suffix = process.argv[5] || '';
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-142.json', 'utf8'));
  // The image directory is a parameter, so the same gate can be pointed at a relocated copy of the same bytes. A gate that could
  // only read the original location would be testing a path rather than an image.
  const imageDir = process.argv[6] || (S + '/image');
  const imageLinux = imageDir + '/' + imageTag + '.elf';
  const mapLinux = imageDir + '/' + imageTag + '.map';
  const control = {
    task: A.task, stage,
    image: imageLinux, map: mapLinux,
    negatives_root: S + '/tmp/gate-' + stage + suffix,
    report_path: S + '/evidence/' + stage + '.json',
    expected: {
      load_address: 0x0100000,
      entry_symbol: 'loader',
      object_order: sel.slots.map((s) => s.basename),
      initializers: C.baseline_expected_initializers,
    },
    note: 'reads the image bytes and the map text; runs no tool, disassembles nothing, and builds four single-defect copies of the '
      + 'image in a scratch directory it owns',
  };
  putOrSame(toHost(S + '/frozen-helpers/' + stage + '.py'), fs.readFileSync(HERE + '/static-gate-142.py'));
  putOrSame(toHost(S + '/inputs/' + stage + '.json'), Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
  freezeContractInLedger();
  const p = writeSpec(stage, 'native.metadata', 'native-metadata', 'metadata', { limit: 900 }, 1200);
  const r = run(p);
  out.push(r);
  const repPath = toHost(S + '/evidence/' + stage + '.json');
  let rep = null;
  if (exists(repPath)) { try { rep = JSON.parse(fs.readFileSync(repPath, 'utf8')); } catch { rep = { unreadable: true }; } }
  console.log(JSON.stringify({ stage, image: pin(toHost(imageLinux)), map: pin(toHost(mapLinux)),
    accepted: rep ? rep.accepted : null, ok: rep ? rep.ok : null,
    problems: rep && rep.problems ? rep.problems.slice(0, 4) : null,
    problem_count: rep && rep.problems ? rep.problems.length : null,
    negatives: rep && rep.negatives ? rep.negatives.map((n) => n.name + ':' + (n.refused ? 'refused' : 'ACCEPTED')
      + ':' + (n.matched_expectation ? 'expected' : 'wrong-reason')) : null,
    header: rep ? { type: rep.header && rep.header.e_type, machine: rep.header && rep.header.e_machine,
      entry: rep.entry, entry_symbols: rep.entry_symbols } : null,
    sections: rep ? rep.sections : null, program_headers: rep ? rep.program_headers : null,
    loadable_segments: rep ? rep.loadable_segments : null, lowest_load_vaddr: rep ? rep.lowest_load_vaddr : null,
    undefined_symbols: rep ? (rep.undefined_symbols || []).length : null,
    map_objects: rep ? rep.map_objects : null,
    initializers_expected: rep ? rep.initializers_expected : null,
    initializers_missing: rep ? rep.initializers_missing : null,
    out: out.map((x) => ({ stage: x.stage, status: x.status, detail: x.detail.slice(0, 200) })) }, null, 2));
  process.exit(rep && rep.ok ? 0 : 1);
}

// ================================================================ pjudge
// The static byte judge, with the obligations the first gate left out, and with its negatives going through the same decision.
// The judge itself lives in static-judge-142.py as a pure function; this stage only loads bytes and calls it. The normal image,
// the relocated image and every one-defect mutant are all judged by that one function, so the four mutants are not a second,
// weaker copy of the judgement.
if (phase === 'pjudge') {
  const stage = process.argv[3];
  const imageTag = process.argv[4] || 'p142-default';
  const imageDir = process.argv[5] || (S + '/image');
  const relocatedDir = process.argv[6] || null;
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage: ' + stage);
  const sel = JSON.parse(fs.readFileSync(R + '/p-selection-142c.json', 'utf8'));
  const objects = sel.slots.map((s) => S + '/producers/p142b/' + s.basename);
  for (const o of objects) if (!exists(toHost(o))) throw new Error('absent producer object: ' + o);
  const control = {
    task: A.task, stage,
    image: imageDir + '/' + imageTag + '.elf',
    map: imageDir + '/' + imageTag + '.map',
    objects,
    relocated_image: relocatedDir ? relocatedDir + '/' + imageTag + '.elf' : null,
    relocated_map: relocatedDir ? relocatedDir + '/' + imageTag + '.map' : null,
    scratch: S + '/tmp/judge-' + stage,
    report_path: S + '/evidence/' + stage + '.json',
    expected: { entry_symbol: 'loader' },
    note: 'the judge is a pure function of bytes and text; the normal image, the relocated copy and every mutant go through it',
  };
  // The judge module and its runner both live in the helper root, because the runner imports its neighbour by name.
  putOrSame(toHost(S + '/frozen-helpers/static-judge-142.py'), fs.readFileSync(HERE + '/static-judge-142.py'));
  putOrSame(toHost(S + '/frozen-helpers/' + stage + '.py'), fs.readFileSync(HERE + '/static-gate-142b.py'));
  putOrSame(toHost(S + '/inputs/' + stage + '.json'), Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
  freezeContractInLedger();
  const p = writeSpec(stage, 'native.metadata', 'native-metadata', 'metadata', { limit: 900 }, 1200);
  const r = run(p);
  out.push(r);
  const repPath = toHost(S + '/evidence/' + stage + '.json');
  let rep = null;
  if (exists(repPath)) { try { rep = JSON.parse(fs.readFileSync(repPath, 'utf8')); } catch { rep = { unreadable: true }; } }
  console.log(JSON.stringify({ stage, image: pin(toHost(control.image)), map: pin(toHost(control.map)),
    objects: objects.length,
    ok: rep ? rep.ok : null,
    real: rep && rep.real ? { accepted: rep.real.accepted, obligations_failed: rep.real.obligations_failed,
      problem_count: rep.real.problems.length,
      multiboot: rep.real.multiboot, multiboot_checksum_sum: rep.real.multiboot_checksum_sum,
      entry: rep.real.entry, entry_symbols: rep.real.entry_symbols, entry_section: rep.real.entry_section,
      entry_segment: rep.real.entry_segment, loadable_segments: rep.real.loadable_segments,
      lowest_load_vaddr: rep.real.lowest_load_vaddr, sections: rep.real.sections,
      common_symbols: (rep.real.common_symbols || []).length, undefined_symbols: (rep.real.undefined_symbols || []).length,
      map_objects: rep.real.map_objects, map_order_matches_argv: rep.real.map_order_matches_argv,
      script_symbols: rep.real.script_symbols,
      constructors_expected: rep.real.constructors_expected, constructors_in_image: rep.real.constructors_in_image,
      ctors_section: rep.real.ctors_section, last_section_end: rep.real.last_section_end } : null,
    relocated: rep ? rep.relocated : null,
    counts: rep ? rep.counts : null,
    mutants: rep && rep.mutants ? rep.mutants.map((m) => m.name + ':' + (m.refused ? 'refused' : 'ACCEPTED') + ':'
      + (m.hit_target ? 'expected' : 'wrong-reason') + ':' + m.obligations_failed.join('+')) : null,
    out: out.map((x) => ({ stage: x.stage, status: x.status, detail: x.detail.slice(0, 300) })) }, null, 2));
  process.exit(rep && rep.ok ? 0 : 1);
}

// ================================================================ doctor
if (phase === 'doctor') {
  const rel = (d) => (exists(d) ? fs.readdirSync(d).sort() : null);
  const report = {
    task: A.task,
    roots: { L, S, L_exists: exists(toHost(L)), S_exists: exists(toHost(S)) },
    R_files: rel(R), R_evidence: rel(R + '/evidence'), R_results: rel(R + '/results'),
    S_top: rel(toHost(S)), S_inputs: rel(toHost(S + '/inputs')),
    S_frozen_helpers: rel(toHost(S + '/frozen-helpers')), S_evidence: rel(toHost(S + '/evidence')),
    runtime_present: exists(toHost(A.runtime_destination)),
    contract: exists(CONTRACT) ? pin(CONTRACT) : null,
    stage_registry: exists(R + '/stage-registry-142.jsonl')
      ? fs.readFileSync(R + '/stage-registry-142.jsonl', 'utf8').split('\n').filter(Boolean).length : 0,
  };
  console.log(JSON.stringify(report, null, 2));
  process.exit(0);
}

console.error('unknown phase: ' + phase);
process.exit(2);
