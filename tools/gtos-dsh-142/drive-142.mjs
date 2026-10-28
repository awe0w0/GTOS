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
const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
const WORKTREE = path.resolve(HERE, '..', '..');
const R = path.resolve(WORKTREE, '..');
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
  const control = {
    task: A.task, stage, kind: 'runtime-identity-reader',
    runtime: A.runtime_destination,
    inventory: { path: toLinux(derivedPin.path), bytes: derivedPin.bytes, sha256: derivedPin.sha256 },
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
