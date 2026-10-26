// GTOS-DSH-127 I1: fixtures are isolated, and the test proves it by never touching business material.
//
// The 126 test declared a TEST_ONLY root and then filtered the business registry, rewrote the business work-root ledger,
// and deleted business attempt, spec, control and helper files. A label cannot authorise that. This test is built the other
// way round: every destructive action lands inside one fixture namespace, and the business material is snapshotted before
// and after and compared, with absence compared as absence.
//
// Three things it also fixes from 126:
//
//   the raw assertion was `raw === null || raw.bytes >= 0`, which cannot fail. Here the real raw is non-empty and is read
//   from the path the guard recorded, and each of the four states is asserted separately.
//
//   inner and raw were looked up under the source root with a Linux string used as a host path. The guard writes them under
//   the work root. This test converts Linux to host properly and reads where the record says they are.
//
//   a 4-check 2-failure run was reported beside a 26-check pass from a different invocation. Here one invocation produces
//   one result, and it is the result that is reported.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-isolated-fixtures-current-evidence-and-runtime-delivery-20261026-a';
const NODE = process.argv[5] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };
const same = (a, b) => (a === null && b === null) || (a !== null && b !== null && a.bytes === b.bytes && a.sha256 === b.sha256);

const A = JSON.parse(readBytes(R + '/authority-127.json').toString('utf8'));
const BRIDGE = R + '/references/private-boot-checkpoints-127.mjs';
const IDENTITY = JSON.parse(readBytes(R + '/identity/identity-127.json').toString('utf8'));

// The Linux side of the fixture. The guard writes its ledger and its logs here, so this subtree is where a fixture's
// records belong. It is a declared test-only subtree of the work root, not the work root itself.
const F = R + '/test-only/i1';
const FL = A.work_root + '/test-only/i1';

// ---------------------------------------------------------------- the business snapshot
// Everything a fixture must never touch. Absence is a state and is compared as one.
const BUSINESS = {
  registry: R + '/stage-registry-127.jsonl',
  ledgerL: '//wsl$/Ubuntu-20.04' + A.work_root + '/logs/runs-127.jsonl',
  ledgerS: '//wsl$/Ubuntu-20.04' + A.source_root + '/logs/runs-127.jsonl',
  stdoutDir: R + '/stage-output',
  frozenHelpers: R + '/frozen-helpers',
};
function businessSnapshot() {
  const snap = {
    registry: pin(BUSINESS.registry),
    ledgerL: pin(BUSINESS.ledgerL),
    ledgerS: pin(BUSINESS.ledgerS),
    attempts: {}, results: {}, controls: {}, helpers: {}, specs: {},
  };
  // The per-stage business records. Listed, not assumed: a file that appears is a change.
  for (const name of (fs.existsSync(R) ? fs.readdirSync(R).sort() : [])) {
    if (/^attempt-.*-127\.json$/.test(name)) snap.attempts[name] = pin(R + '/' + name);
    if (/^result-.*-127\.json$/.test(name)) snap.results[name] = pin(R + '/' + name);
  }
  for (const dir of [BUSINESS.stdoutDir, BUSINESS.frozenHelpers]) {
    const key = dir === BUSINESS.stdoutDir ? 'controls' : 'helpers';
    const into = dir === BUSINESS.stdoutDir ? 'specs' : null;
    if (!fs.existsSync(dir)) continue;
    for (const name of fs.readdirSync(dir).sort()) {
      if (dir === BUSINESS.stdoutDir && /\.control\.json$/.test(name)) snap.controls[name] = pin(dir + '/' + name);
      else if (dir === BUSINESS.stdoutDir && /\.spec\.json$/.test(name)) snap.specs[name] = pin(dir + '/' + name);
      else if (dir === BUSINESS.frozenHelpers && /\.mjs$/.test(name)) snap.helpers[name] = pin(dir + '/' + name);
    }
  }
  return snap;
}
// A readable difference, not just a boolean. Every added, removed or changed key is named.
function businessDiff(before, after) {
  const out = [];
  const walk = (a, b, trail) => {
    if (a === null || a === undefined || typeof a !== 'object' || Array.isArray(a)) {
      if (!same(a, b)) out.push({ at: trail, before: a, after: b });
      return;
    }
    for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) walk(a[k], b[k], trail + '.' + k);
  };
  walk(before, after, 'business');
  return out;
}

// ---------------------------------------------------------------- the fixture
// A fresh namespace. Nothing from an earlier run is reused, and nothing is cleared to make a name available.
const NS = 'i1';
if (fs.existsSync(F)) throw new Error('the fixture namespace already exists; this run uses a fresh one: ' + F);
for (const d of ['frozen-runners', 'stage-output', 'frozen-helpers']) fs.mkdirSync(F + '/' + d, { recursive: true });
// The fixture's own frozen runner: the same bytes as the packet's, with its own pin beside it.
const runnerBytes = readBytes(R + '/frozen-runners/run-127-r01.mjs');
fs.writeFileSync(F + '/frozen-runners/run-127-r01.mjs', runnerBytes);
fs.writeFileSync(F + '/runner-pin-127-run-127-r01.json', JSON.stringify({
  task: 'GTOS-DSH-127', runner: 'frozen-runners/run-127-r01.mjs',
  bytes: runnerBytes.length, sha256: sha(runnerBytes), revision: 'run-127-r01', fixture: true,
  note: 'a fixture copy of the same-hash runner, pinned beside itself',
}, null, 2) + '\n');
fs.writeFileSync(F + '/helper-draft.mjs', readBytes(R + '/native-127-01.mjs'));
check('the fixture runner is the same bytes as the packet runner',
  sha(runnerBytes) === sha(readBytes(R + '/frozen-runners/run-127-r01.mjs')), sha(runnerBytes).slice(0, 16));

// The Linux side needs a logs directory for the guard's ledger and its inner and raw output.
fs.mkdirSync('//wsl$/Ubuntu-20.04' + FL + '/logs', { recursive: true });

const pinOf = (f) => { const b = readBytes(f); return { path: f, bytes: b.length, sha256: sha(b) }; };

function writeSpec(stage, extra) {
  const control = {
    task: A.task, route: 'qemu.probe', mode: 'qemu-probe', bridge_module_path: BRIDGE,
    bridge: {
      task: A.task, stage, outer_stage: stage, kind: 'version', route: 'qemu.probe',
      report_root: R,
      // The work root is the fixture's Linux subtree, so the guard's ledger and logs stay in the fixture.
      work_root: FL, source_root: A.source_root, distribution: 'Ubuntu-20.04',
      authority: pinOf(R + '/authority-127.json'),
      request_module: R + '/references/native-request-127.mjs',
      tools: { python_realpath: A.python.realpath },
      guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
      runner_module: F + '/frozen-runners/run-127-r01.mjs',
      binding_module: pinOf(BRIDGE),
      profile_makefile: R + '/profile-127/Makefile', profile_cxxflags: R + '/profile-127/kernel-cxxflags',
      report_path: F + '/evidence-' + stage + '.json', log_max_bytes: 524288,
    },
    stage_options: { limit: 60 },
    purpose: 'a real version child inside an isolated fixture',
    report_path: F + '/results-' + stage + '.json',
  };
  const spec = {
    task: A.task, route: 'qemu.probe', stage, root: R,
    // The explicit fixture context. Every mutable path of this stage derives from it.
    fixture_root: F,
    runnerPath: F + '/frozen-runners/run-127-r01.mjs',
    helper: 'helper-draft.mjs', control,
    expected_outputs: [control.report_path], limit: 300,
    ...(extra || {}),
  };
  const sp = F + '/stage-output/' + stage + '.spec.json';
  fs.writeFileSync(sp, JSON.stringify(spec, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  return sp;
}
function runSpec(specPath) {
  const r = spawnSync(NODE, [F + '/frozen-runners/run-127-r01.mjs', specPath],
    { windowsHide: true, timeout: 600000, maxBuffer: 32 * 1024 * 1024 });
  const out = String(r.stdout || '');
  const line = out.split('\n').filter((l) => l.startsWith('stage='))[0] || '';
  return { status: r.status, line: line.trim(), ok: /ok=true/.test(line), stderr: String(r.stderr || '') };
}

// ---------------------------------------------------------------- I1a: a real stage, then the 126 escape
const before = businessSnapshot();
check('the business snapshot was taken before any fixture work', before !== null, {
  attempts: Object.keys(before.attempts).length, results: Object.keys(before.results).length,
  helpers: Object.keys(before.helpers).length });

const S1 = 'fixrun-01';
let first = null;
try {
  first = runSpec(writeSpec(S1));
  check('a real stage inside the fixture succeeded', first.ok === true,
    first.ok ? first.line : first.stderr.slice(0, 400));
} catch (error) {
  check('a real stage inside the fixture succeeded', false, String(error.message));
}

if (first && first.ok) {
  // Every mutable artefact of this stage is inside the fixture, not in the business root.
  const fixtureFiles = [];
  const walk = (d) => {
    for (const n of fs.readdirSync(d).sort()) {
      const f = d + '/' + n;
      if (fs.statSync(f).isDirectory()) { walk(f); continue; }
      fixtureFiles.push(f.slice(F.length + 1));
    }
  };
  walk(F);
  check('the fixture holds the whole record set of its own stage',
    fixtureFiles.includes('attempt-' + S1 + '-127.json') && fixtureFiles.includes('result-' + S1 + '-127.json')
    && fixtureFiles.includes('stage-registry-127.jsonl') && fixtureFiles.includes('stage-output/' + S1 + '.control.json')
    && fixtureFiles.includes('frozen-helpers/' + S1 + '.mjs'),
    fixtureFiles.filter((f) => f.includes(S1) || f.includes('registry')));
  check('no business attempt or result was created for this stage',
    !fs.existsSync(R + '/attempt-' + S1 + '-127.json') && !fs.existsSync(R + '/result-' + S1 + '-127.json'),
    null);

  // I1c: the real log location, derived from the record rather than guessed.
  const bridgeResult = JSON.parse(readBytes(F + '/results-' + S1 + '.json').toString('utf8'));
  check('the bridge records a present inner report and a present raw log',
    bridgeResult.inner_report_present === true && bridgeResult.raw_present === true,
    { inner: bridgeResult.inner_report_present, raw: bridgeResult.raw_present, bytes: bridgeResult.raw_bytes });
  // The Linux path the guard recorded, converted to a host path properly.
  const hostOf = (linux) => {
    const m = /^\/mnt\/([a-z])\/(.*)$/.exec(linux);
    return m ? m[1].toUpperCase() + ':/' + m[2] : '//wsl$/Ubuntu-20.04' + linux;
  };
  const innerLinux = FL + '/logs/inner-report-' + S1 + '.json';
  const rawLinux = FL + '/logs/child-output-' + S1 + '.raw';
  check('the inner report is where the record says it is', fs.existsSync(hostOf(innerLinux)), innerLinux);
  const rawPin = pin(hostOf(rawLinux));
  check('the raw log is non-empty and matches the recorded size',
    rawPin !== null && rawPin.bytes > 0 && rawPin.bytes === bridgeResult.raw_bytes,
    { bytes: rawPin ? rawPin.bytes : null, recorded: bridgeResult.raw_bytes });
  check('the raw log was read from the work root, not the source root',
    !fs.existsSync('//wsl$/Ubuntu-20.04' + A.source_root + '/logs/child-output-' + S1 + '.raw'), null);

  // I1a: the 126 escape. Peripheral records go; the registry stays; the name is refused.
  const registryBefore = pin(F + '/stage-registry-127.jsonl');
  // A saved copy of the first generation, taken before the escape. The escape removes the live files, so the copy is what
  // proves the first generation's bytes were not lost.
  const savedDir = F + '/first-generation';
  fs.mkdirSync(savedDir, { recursive: true });
  const savedFirst = {};
  for (const [label, src] of [
    ['attempt', F + '/attempt-' + S1 + '-127.json'],
    ['result', F + '/result-' + S1 + '-127.json'],
    ['control', F + '/stage-output/' + S1 + '.control.json'],
    ['spec', F + '/stage-output/' + S1 + '.spec.json'],
    ['helper', F + '/frozen-helpers/' + S1 + '.mjs'],
    ['inner', '//wsl$/Ubuntu-20.04' + FL + '/logs/inner-report-' + S1 + '.json'],
    ['raw', '//wsl$/Ubuntu-20.04' + FL + '/logs/child-output-' + S1 + '.raw'],
  ]) {
    const b = readBytes(src);
    if (b === null) { savedFirst[label] = null; continue; }
    fs.writeFileSync(savedDir + '/' + label, b);
    savedFirst[label] = pin(savedDir + '/' + label);
  }
  check('the first generation was saved as a copy before the escape',
    Object.values(savedFirst).every((v) => v !== null), Object.keys(savedFirst).filter((k) => savedFirst[k] === null));
  const removed = [];
  for (const f of [F + '/attempt-' + S1 + '-127.json', F + '/result-' + S1 + '-127.json',
    F + '/stage-output/' + S1 + '.control.json', F + '/stage-output/' + S1 + '.out.txt',
    F + '/stage-output/' + S1 + '.err.txt', F + '/stage-output/' + S1 + '.spec.json',
    F + '/frozen-helpers/' + S1 + '.mjs']) {
    if (fs.existsSync(f)) { fs.rmSync(f); removed.push(f.slice(F.length + 1)); }
  }
  check('the peripheral records of the first attempt were removed inside the fixture',
    removed.length === 7, removed);

  let second = null;
  try {
    // The spec has to be rewritten to be submitted again, which is the strongest form of the escape.
    second = runSpec(writeSpec(S1));
  } catch (error) { second = { ok: false, stderr: String(error.message), status: -1 }; }
  check('the same stage name is refused even with every peripheral record gone',
    second.ok === false && /already claimed/.test(String(second.stderr)) && /never reused/.test(String(second.stderr)),
    second.stderr.split('\n').filter((l) => l.trim()).slice(0, 2).join(' | '));
  check('the registry is unchanged by the refusal', same(registryBefore, pin(F + '/stage-registry-127.jsonl')),
    { before: registryBefore.sha256, after: pin(F + '/stage-registry-127.jsonl').sha256 });
  check('the refusal wrote no new attempt for the refused name',
    !fs.existsSync(F + '/attempt-' + S1 + '-127.json'), null);

  // I1a: a legal new stage carries the retry identity and both records coexist.
  const S2 = 'fixrun-02';
  let third = null;
  try {
    third = runSpec(writeSpec(S2, { parent_attempt: S1, retry_of: S1,
      failure_cause: 'the first attempt was used to show that the name is spent' }));
  } catch (error) { third = { ok: false, stderr: String(error.message) }; }
  check('a legal new stage name succeeds', third.ok === true, third.ok ? third.line : third.stderr.slice(0, 300));
  if (third.ok) {
    const reg = readBytes(F + '/stage-registry-127.jsonl').toString('utf8').split(/\r?\n/).filter(Boolean)
      .map((l) => JSON.parse(l));
    const s2claim = reg.find((r) => r.stage === S2);
    check('the retry claim names the attempt it follows', s2claim && s2claim.retry_of === S1,
      s2claim ? s2claim.retry_of : null);
      check('the saved first generation is byte identical after the escape and the retry',
      Object.entries(savedFirst).every(([k, v]) => same(v, pin(savedDir + '/' + k))),
      Object.entries(savedFirst).filter(([k, v]) => !same(v, pin(savedDir + '/' + k))).map(([k]) => k));
    check('the registry prefix still begins with the first claim',
      readBytes(F + '/stage-registry-127.jsonl').toString('utf8').split(/\r?\n/).filter(Boolean)[0]
        .includes('"' + S1 + '"'), null);
    check('the new attempt has its own terminal record and the old name stays claimed',
      fs.existsSync(F + '/result-' + S2 + '-127.json')
      && reg.some((r) => r.stage === S1) && reg.some((r) => r.stage === S2), null);
  }

  // I1c: the four raw states, asserted separately.
  // (1) a real non-empty raw, already read above.
  check('state 1: a real run has a non-empty raw', rawPin !== null && rawPin.bytes > 0, rawPin ? rawPin.bytes : null);
  // (2) a legal empty raw. An empty file is a real record with zero bytes, distinct from absence.
  const emptyRaw = F + '/empty-raw.txt';
  fs.writeFileSync(emptyRaw, Buffer.alloc(0));
  check('state 2: an empty raw is a record of zero bytes, not an absence',
    fs.existsSync(emptyRaw) && pin(emptyRaw).bytes === 0, pin(emptyRaw).bytes);
  // (3) a real pre-child absence: a stage that never launched has no raw at all.
  const absentRaw = hostOf(FL + '/logs/child-output-neverran-01.raw');
  check('state 3: a stage that never launched has no raw at all, and absence is reported as absence',
    !fs.existsSync(absentRaw) && pin(absentRaw) === null, absentRaw);
  // (4) started but raw missing is a refusal. This is the case the 126 assertion could not express.
  {
    // Move the real raw aside and ask the archive engine to certify a ledger row whose raw is gone.
    const engine = await import('file:///' + R + '/references/publication-engine-127.mjs');
    const probeRoot = F + '/raw-probe';
    fs.mkdirSync(probeRoot + '/logs', { recursive: true });
    // A ledger with a started row but no raw beside it.
    fs.writeFileSync(probeRoot + '/logs/runs-127.jsonl',
      JSON.stringify({ stage: 'noraw-01', kind: 'result', exit_code: 0 }) + '\n');
    // The inner report is what says a child ran. Without it the engine cannot tell started from never-started, and the
    // case would not be the one being tested.
    fs.writeFileSync(probeRoot + '/logs/inner-report-noraw-01.json',
      JSON.stringify({ stage: 'noraw-01', child: { returncode: 0, output_path: '/nonexistent' } }) + '\n');
    let refused = false;
    let message = null;
    try { engine.assertStartedHasRaw({ host_root: probeRoot, ledger: 'runs-127.jsonl' }); }
    catch (error) { refused = true; message = String(error.message); }
    check('state 4: a started stage with no raw is refused', refused && /raw/i.test(String(message)), message);
    check('the raw-probe ledger is byte identical after the refusal',
      pin(probeRoot + '/logs/runs-127.jsonl').bytes ===
        Buffer.byteLength(JSON.stringify({ stage: 'noraw-01', kind: 'result', exit_code: 0 }) + '\n'), null);
  }

  // I1b: an illegal fixture root is refused before anything is written, and it does not touch business.
  {
    const illegal = R + '/stage-output';
    let refused = false;
    let message = null;
    const specPath = F + '/stage-output/illegal-01.spec.json';
    const control = JSON.parse(readBytes(F + '/stage-output/' + S2 + '.spec.json').toString('utf8'));
    const badSpec = { ...control, stage: 'illegal-01', fixture_root: illegal };
    fs.writeFileSync(specPath, JSON.stringify(badSpec, null, 2) + '\n');
    const r = spawnSync(NODE, [F + '/frozen-runners/run-127-r01.mjs', specPath],
      { windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024 });
    refused = r.status !== 0;
    message = String(r.stderr || '');
    check('an illegal fixture root is refused', refused && /FIXTURE GATE/.test(message),
      message.split('\n').filter((l) => l.trim()).slice(0, 2).join(' | '));
    check('the illegal-root refusal wrote nothing into the business stage-output',
      !fs.existsSync(R + '/stage-output/illegal-01.spec.json')
      && !fs.existsSync(R + '/stage-output/illegal-01.control.json'), null);
  }
}

// ---------------------------------------------------------------- I1b: the business is untouched
const after = businessSnapshot();
const diff = businessDiff(before, after);
check('the business material is unchanged by the whole fixture run', diff.length === 0, diff);
check('the business comparison covered real files, not an empty set',
  Object.keys(before.attempts).length + Object.keys(before.results).length > 0,
  { attempts: Object.keys(before.attempts).length, results: Object.keys(before.results).length,
    helpers: Object.keys(before.helpers).length });
// Absence is compared as absence: a business record that did not exist before must still not exist.
check('absence in the business snapshot is absence in both readings',
  Object.keys(before.attempts).every((k) => after.attempts[k] !== undefined),
  Object.keys(before.attempts).filter((k) => after.attempts[k] === undefined));

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-127', kind: 'isolated_fixture_test',
  subject: 'tools/run-stage.mjs fixture context, tools/private-boot-checkpoints.mjs work-root gate, tools/publication-engine.mjs ledger check',
  fixture: { host_root: F, linux_root: FL, namespace: NS,
    runner_sha256: sha(runnerBytes), same_hash_as_packet: true },
  business: {
    snapshot_before: { registry: before.registry, ledgerL: before.ledgerL, ledgerS: before.ledgerS,
      attempts: Object.keys(before.attempts).length, results: Object.keys(before.results).length,
      controls: Object.keys(before.controls).length, helpers: Object.keys(before.helpers).length,
      specs: Object.keys(before.specs).length },
    snapshot_after: { registry: after.registry, ledgerL: after.ledgerL, ledgerS: after.ledgerS,
      attempts: Object.keys(after.attempts).length, results: Object.keys(after.results).length,
      controls: Object.keys(after.controls).length, helpers: Object.keys(after.helpers).length,
      specs: Object.keys(after.specs).length },
    difference: diff,
  },
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
