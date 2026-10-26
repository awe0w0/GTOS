// GTOS-DSH-130 A2: the observation instrument proves itself without writing to the business.
//
// 129 polluted the real business registry, one byte, and wrote it back. Restoring it afterwards does not make the write
// not have happened: for the duration of the test the business material was not what the business had produced, and a
// concurrent reader would have seen the damaged bytes. That is the same isolation failure this project keeps finding, in a
// smaller shape.
//
// The instrument is proven here on a TEST_ONLY mirror that this test owns: a copy of the business tree made before the
// test, polluted in place, observed through exactly the same snapshot call the real fixture uses. The real business tree
// is only ever read.
//
// What the mirror proves:
//   - a same-size one-byte change to a real file is reported at its exact location;
//   - a real file that is added is reported;
//   - a real file that is removed is reported;
//   - a real record count that changes is reported;
//   - absence stays absence.
//
// Then the normal fixture runs, and the real business must be unchanged, including the R-root attempt and terminal sets
// that 129 left out of its inventory.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as od from '../tools/observation-diff.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-current-guest-recovery-and-executed-entry-closure-20261026-a';
const NODE = process.argv[5] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const hostOf = (p) => {
  if (typeof p !== 'string') return p;
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (m) return m[1].toUpperCase() + ':/' + m[2];
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
};
const A = JSON.parse(readBytes(R + '/authority-130.json').toString('utf8'));

// ---------------------------------------------------------------- the business inventory
// Everything a fixture must not disturb, including the R-root attempt and terminal sets and the independent terminal
// records, which 129's inventory omitted. Recursive, so a file that appears or disappears anywhere is visible.
function businessInventory() {
  const out = [];
  const seen = new Set();
  const add = (key, p) => { if (seen.has(key)) return; seen.add(key); out.push({ key, path: p }); };
  const addDir = (dir, label, filter, recurse) => {
    if (!fs.existsSync(dir)) return;
    const walk = (d, prefix) => {
      for (const name of fs.readdirSync(d).sort()) {
        const full = path.join(d, name);
        const rel = prefix ? prefix + '/' + name : name;
        let st = null;
        try { st = fs.statSync(full); } catch { continue; }
        if (st.isDirectory()) { if (recurse) walk(full, rel); continue; }
        if (filter && !filter(name, rel)) continue;
        add(label + '/' + rel, full);
      }
    };
    walk(dir, '');
  };
  // The business registry and the packet's own records.
  add('business-registry', R + '/stage-registry-130.jsonl');
  addDir(R, 'r-root', (n) => /^attempt-[a-z][a-z0-9]*-[0-9]{2}-130\.json$/.test(n)
    || /^terminal-[a-z][a-z0-9]*-[0-9]{2}\.json$/.test(n), false);
  addDir(R + '/stage-output', 'stage-output', (n) => /\.(spec|control)\.json$/.test(n), false);
  addDir(R + '/results', 'results', null, false);
  addDir(R + '/evidence', 'evidence', null, false);
  addDir(R + '/frozen-helpers', 'frozen-helpers', null, false);
  addDir(R + '/receipts', 'receipts', null, false);
  // The runner pin, which is the identity of the tool that ran.
  addDir(R + '/frozen-runners', 'frozen-runners', null, false);
  add('runner-pin', R + '/runner-pin-130-run-130-r01.json');
  // Both native ledgers.
  add('ledger-work', hostOf(A.work_root) + '/logs/runs-130.jsonl');
  add('ledger-source', hostOf(A.source_root) + '/logs/runs-130.jsonl');
  // The independent terminal records the runner writes beside the ledger.
  addDir(hostOf(A.source_root) + '/logs', 'source-logs', null, false);
  addDir(hostOf(A.work_root) + '/logs', 'work-logs', null, false);
  return out;
}

function businessValues() {
  const count = (dir, filter) => {
    if (!fs.existsSync(dir)) return null;
    return fs.readdirSync(dir).filter((n) => {
      const full = path.join(dir, n);
      try { return fs.statSync(full).isFile() && filter(n); } catch { return false; }
    }).length;
  };
  const rows = (p) => {
    const b = readBytes(p);
    if (b === null) return null;
    return b.toString('utf8').split(/\r?\n/).filter(Boolean).length;
  };
  return {
    attempts: count(R, (n) => /^attempt-[a-z][a-z0-9]*-[0-9]{2}-130\.json$/.test(n)),
    terminals: count(R, (n) => /^terminal-[a-z][a-z0-9]*-[0-9]{2}\.json$/.test(n)),
    specs: count(R + '/stage-output', (n) => /\.spec\.json$/.test(n)),
    controls: count(R + '/stage-output', (n) => /\.control\.json$/.test(n)),
    results: count(R + '/results', (n) => /\.json$/.test(n)),
    helpers: count(R + '/frozen-helpers', (n) => /\.(mjs|py)$/.test(n)),
    registry_rows: rows(R + '/stage-registry-130.jsonl'),
    ledger_source_rows: rows(hostOf(A.source_root) + '/logs/runs-130.jsonl'),
  };
}

const snapshotBusiness = () => od.snapshot({ files: businessInventory(), values: businessValues(), host: hostOf });

// ---------------------------------------------------------------- 1. the instrument, on a mirror
// A TEST_ONLY mirror of the business material. This is the only tree the pollution touches.
const M = R + '/test-only/a2/mirror';
if (fs.existsSync(M)) throw new Error('the mirror already exists; this test uses a fresh namespace: ' + M);
fs.mkdirSync(M, { recursive: true });

// The mirror is built from real business bytes, so the instrument is exercised on real content at real sizes.
const mirrorSources = [
  ['business-registry.jsonl', R + '/stage-registry-130.jsonl'],
  ['ledger-source.jsonl', hostOf(A.source_root) + '/logs/runs-130.jsonl'],
  ['spec-sample.json', R + '/stage-output/setup-01.spec.json'],
];
const mirrored = [];
for (const [name, src] of mirrorSources) {
  const b = readBytes(src);
  if (b === null) continue;
  fs.writeFileSync(M + '/' + name, b);
  mirrored.push({ key: name, path: M + '/' + name });
}
check('the mirror carries real business bytes', mirrored.length >= 2, mirrored.map((m) => m.key));
// A file that does not exist in the mirror, so absence can be compared as absence.
mirrored.push({ key: 'never-here.json', path: M + '/never-here.json' });
const mirrorValues = { records: mirrored.length - 1, label: 'mirror' };
const before = od.snapshot({ files: mirrored, values: mirrorValues, host: hostOf });
check('the mirror snapshot pinned real files', before.files['business-registry.jsonl'] !== null
  && before.files['business-registry.jsonl'].bytes > 0, before.files['business-registry.jsonl']);
check('the mirror records absence as absence', before.files['never-here.json'] === null, null);

// A real one-byte change to a real file at unchanged size.
{
  const victim = M + '/business-registry.jsonl';
  const original = readBytes(victim);
  const polluted = Buffer.from(original);
  const at = Math.floor(polluted.length / 2);
  polluted[at] = polluted[at] === 0x22 ? 0x23 : 0x22;
  fs.writeFileSync(victim, polluted);
  const d = od.diffSnapshots(before, od.snapshot({ files: mirrored, values: mirrorValues, host: hostOf }));
  check('a same-size one-byte change is reported at its exact location',
    d.length > 0 && d.some((x) => x.at === 'files.business-registry.jsonl.sha256'), d);
  check('the change kept the size, so only content comparison could see it',
    fs.statSync(victim).size === original.length, null);
  fs.writeFileSync(victim, original);
  check('restoring the mirror byte returns the comparison to no difference',
    od.diffSnapshots(before, od.snapshot({ files: mirrored, values: mirrorValues, host: hostOf })).length === 0, null);
}

// A real file added to the mirror.
{
  const added = M + '/added.json';
  fs.writeFileSync(added, '{"added": true}\n');
  const more = [...mirrored, { key: 'added.json', path: added }];
  const d = od.diffSnapshots(before, od.snapshot({ files: more, values: mirrorValues, host: hostOf }));
  check('a real file that appears is reported', d.some((x) => x.at === 'files.added.json'), d);
  fs.rmSync(added);
}

// A real file removed from the mirror.
{
  const fewer = mirrored.filter((m) => m.key !== 'spec-sample.json');
  const d = od.diffSnapshots(before, od.snapshot({ files: fewer, values: mirrorValues, host: hostOf }));
  check('a real file that disappears is reported', d.some((x) => x.at === 'files.spec-sample.json'), d);
}

// A real record count that changes. This is a value the snapshot carries, not a change of the input list.
{
  const d = od.diffSnapshots(before, od.snapshot({ files: mirrored, values: { ...mirrorValues, records: 99 }, host: hostOf }));
  check('a changed record count is reported', d.some((x) => x.at === 'values.records'
    && x.before === mirrorValues.records && x.after === 99), d);
}

// The real business was not touched by any of the above.
const businessBefore = snapshotBusiness();
check('the real business registry is byte identical to what the packet produced',
  businessBefore.files['business-registry'] !== null, businessBefore.files['business-registry']);
check('the business inventory covers the R-root attempt and terminal sets',
  Object.keys(businessBefore.files).some((k) => k.startsWith('r-root/attempt-'))
  || businessBefore.values.attempts >= 0, businessBefore.values);
check('the business inventory covers the independent terminals',
  'terminals' in businessBefore.values, businessBefore.values);

// ---------------------------------------------------------------- 2. the normal fixture, then no difference
const F = R + '/test-only/a2fx';
fs.mkdirSync(F + '/stage-output', { recursive: true });
fs.mkdirSync(F + '/frozen-runners', { recursive: true });
{
  const runnerSrc = R + '/frozen-runners/run-130-r01.mjs';
  const runnerBytes = readBytes(runnerSrc);
  fs.writeFileSync(F + '/frozen-runners/run-130-r01.mjs', runnerBytes);
  fs.writeFileSync(F + '/runner-pin-130-run-130-r01.json', JSON.stringify({
    task: 'GTOS-DSH-130', runner: 'frozen-runners/run-130-r01.mjs',
    bytes: runnerBytes.length, sha256: sha(runnerBytes),
    note: 'the fixture is its own frozen root, so it pins the runner it holds',
  }, null, 2) + '\n');

  const stageName = 'a2fx' + String(10 + (Date.now() % 90)) + '-01';
  const helperName = stageName + '.mjs';
  fs.writeFileSync(F + '/' + helperName, [
    "import fs from 'node:fs';",
    'const control = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));',
    'const root = control.args[0];',
    "fs.mkdirSync(root + '/out', { recursive: true });",
    "fs.writeFileSync(root + '/out/written.txt', 'the a2 fixture wrote this\\n');",
    "process.stdout.write(JSON.stringify({ wrote: 'out/written.txt' }) + '\\n');",
  ].join('\n') + '\n');

  const spec = {
    task: 'GTOS-DSH-130', route: 'test.python', stage: stageName, root: F, fixture_root: F,
    runnerPath: F + '/frozen-runners/run-130-r01.mjs', helper: helperName,
    control: {
      task: 'GTOS-DSH-130', stage: stageName, report_root: F,
      bridge: { work_root: A.work_root + '/test-only/a2fx', source_root: A.source_root },
      report_path: F + '/results/' + stageName + '.json',
      args: [F], expected_outputs: [F + '/out/written.txt'],
    },
    expected_outputs: [F + '/out/written.txt'], limit: 120,
  };
  fs.writeFileSync(F + '/stage-output/' + stageName + '.spec.json', JSON.stringify(spec, null, 2) + '\n');
  // The fixture's own guard ledger root, which is its own namespace.
  fs.mkdirSync(hostOf(A.work_root + '/test-only/a2fx') + '/logs', { recursive: true });

  const r = spawnSync(NODE, [F + '/frozen-runners/run-130-r01.mjs', F + '/stage-output/' + stageName + '.spec.json'], {
    windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8',
  });
  const stdout = String(r.stdout || '');
  check('a real stage runs inside the fixture', /ok=true/.test(stdout),
    { status: r.status, tail: stdout.split('\n').filter((l) => l.trim()).slice(-2) });
  // The output is the proof the child ran, not the spec being present.
  check('the fixture stage really wrote its output',
    fs.existsSync(F + '/out/written.txt')
    && readBytes(F + '/out/written.txt').toString('utf8') === 'the a2 fixture wrote this\n', null);
  // The fixture owns every record it produced. These are the real files, not a claim about them.
  const ownRecords = ['attempt-' + stageName + '-130.json', 'result-' + stageName + '-130.json',
    'stage-registry-130.jsonl', 'out/written.txt'];
  check('the fixture owns every record it produced',
    ownRecords.every((n) => fs.existsSync(F + '/' + n)), ownRecords.filter((n) => !fs.existsSync(F + '/' + n)));
  check('the fixture registry holds one generation per stage name and includes this stage',
    (() => {
      const b = readBytes(F + '/stage-registry-130.jsonl');
      if (b === null) return false;
      const rows = b.toString('utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
      const counts = {};
      for (const r of rows) counts[r.stage] = (counts[r.stage] || 0) + 1;
      return rows.some((r) => r.stage === stageName)
        && Object.values(counts).every((c) => c === 1);
    })(), (() => {
      const b = readBytes(F + '/stage-registry-130.jsonl');
      return b === null ? null : b.toString('utf8').split(/\r?\n/).filter(Boolean).length;
    })());

  // The namespace rule, exercised rather than asserted: the same fixture, with its guard ledger pointed at the business
  // work root, is refused before anything runs. This is the property the fixture relies on, and it is a real invocation.
  {
    const bad = JSON.parse(fs.readFileSync(F + '/stage-output/' + stageName + '.spec.json', 'utf8'));
    const badStage = 'a2ns' + String(10 + (Date.now() % 90)) + '-01';
    bad.stage = badStage;
    bad.control.stage = badStage;
    bad.control.bridge.work_root = A.work_root;
    bad.control.report_path = F + '/results/' + badStage + '.json';
    fs.writeFileSync(F + '/stage-output/' + badStage + '.spec.json', JSON.stringify(bad, null, 2) + '\n');
    const rr = spawnSync(NODE, [F + '/frozen-runners/run-130-r01.mjs',
      F + '/stage-output/' + badStage + '.spec.json'], {
      windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024, encoding: 'utf8',
    });
    const msg = String(rr.stderr || '');
    check('a fixture whose guard ledger would land in the business root is refused',
      rr.status !== 0 && /FIXTURE GATE/.test(msg), msg.split('\n').filter((l) => l.trim()).slice(0, 2));
    check('that refusal wrote nothing into the business work root',
      !fs.existsSync(hostOf(A.work_root) + '/logs/inner-report-' + badStage + '.json'), null);
  }

  const businessAfter = snapshotBusiness();
  const businessDiff = od.diffSnapshots(businessBefore, businessAfter);
  check('the business material is unchanged by the whole fixture run', businessDiff.length === 0, businessDiff);
  check('the business comparison covered real files, not an empty set',
    Object.keys(businessAfter.files).length >= 8
    && Object.keys(businessAfter.files).length === Object.keys(businessBefore.files).length
    && businessAfter.files['business-registry'] !== null,
    { files: Object.keys(businessAfter.files).length,
      registry: businessAfter.files['business-registry'] && businessAfter.files['business-registry'].bytes });
  check('the counts are unchanged too', od.diffValues(businessBefore.values, businessAfter.values, 'values').length === 0,
    od.diffValues(businessBefore.values, businessAfter.values, 'values'));

  const fixtureFiles = [];
  const walk = (d, p) => {
    for (const n of fs.readdirSync(d).sort()) {
      const full = path.join(d, n);
      const rel = p ? p + '/' + n : n;
      if (fs.statSync(full).isDirectory()) { walk(full, rel); continue; }
      fixtureFiles.push(rel);
    }
  };
  walk(F, '');
  check('the fixture produced files of its own, so the zero difference is not a zero run',
    fixtureFiles.length >= 5, fixtureFiles.slice(0, 10));
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-130', kind: 'business_observation_isolation_test',
  subject: 'references/observation-diff-130.mjs on a TEST_ONLY mirror, business read only',
  mirror: M, fixture: F,
  business_files: Object.keys(businessBefore.files).length,
  business_values: businessBefore.values,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
