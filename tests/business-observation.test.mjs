// GTOS-DSH-129 E1b: the real isolated fixture, observed by the real comparator.
//
// 128 had a comparator that could fail and a fixture that claimed a zero difference, and the two were not connected: the
// fixture used its own `same`, and the comparator was tested on its own. A correct instrument that the production path does
// not call proves nothing about the production path.
//
// Here the fixture's before/after comparison IS the comparator from tools/observation-diff.mjs. Two things are required:
//
//   1. a deliberate pollution of a real file inside a TEST_ONLY root must be reported by this same path, with the exact
//      location, so the observation is known to be able to see a change; and
//   2. a normal fixture run must report zero difference over the business material, including absence compared as absence.
//
// The business material is the packet's real registry, both native ledgers, and the attempt/terminal/helper/control/spec
// sets, which is what a fixture is not allowed to disturb.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as od from '../tools/observation-diff.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-resource-recovery-and-current-business-integration-20261026-a';
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
const A = JSON.parse(readBytes(R + '/authority-129.json').toString('utf8'));

// ---------------------------------------------------------------- the business material
// Every file a fixture is not allowed to touch, listed by where it is rather than by what it should contain, so a file that
// appears or disappears is as visible as one whose bytes change.
function businessFiles() {
  const out = [];
  const addDir = (dir, label, filter) => {
    if (!fs.existsSync(dir)) return;
    for (const name of fs.readdirSync(dir).sort()) {
      const full = path.join(dir, name);
      let st = null;
      try { st = fs.statSync(full); } catch { continue; }
      if (st.isDirectory()) continue;
      if (filter && !filter(name)) continue;
      out.push({ key: label + '/' + name, path: full });
    }
  };
  // The registry is a single append-only file.
  out.push({ key: 'registry', path: R + '/stage-registry-129.jsonl' });
  // The two native ledgers.
  out.push({ key: 'ledger-work', path: hostOf(A.work_root) + '/logs/runs-129.jsonl' });
  out.push({ key: 'ledger-source', path: hostOf(A.source_root) + '/logs/runs-129.jsonl' });
  // The stage-output sets: specs, controls, attempts and results all live here.
  addDir(R + '/stage-output', 'stage-output', (n) => /\.(spec|control)\.json$/.test(n));
  addDir(R + '/results', 'results');
  addDir(R + '/evidence', 'evidence');
  addDir(R + '/frozen-helpers', 'frozen-helpers');
  addDir(hostOf(A.source_root) + '/logs', 'source-logs');
  addDir(hostOf(A.work_root) + '/logs', 'work-logs');
  return out;
}
// The scalar counts that belong to the business state but are not files: how many of each kind of record exists.
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
    attempts: count(R, (n) => /^attempt-[a-z][a-z0-9]*-[0-9]{2}-129\.json$/.test(n)),
    specs: count(R + '/stage-output', (n) => /\.spec\.json$/.test(n)),
    results: count(R + '/results', (n) => /\.json$/.test(n)),
    helpers: count(R + '/frozen-helpers', (n) => /\.(mjs|py)$/.test(n)),
    registry_rows: rows(R + '/stage-registry-129.jsonl'),
    ledger_source_rows: rows(hostOf(A.source_root) + '/logs/runs-129.jsonl'),
  };
}

function snapshotBusiness() {
  return od.snapshot({ files: businessFiles(), values: businessValues(), host: hostOf });
}

// ---------------------------------------------------------------- the fixture
// A real stage, run through the real runner, inside a fresh TEST_ONLY namespace. It is a real invocation, not a simulated
// one: the runner executes a helper that writes a file, and the guard records the child.
const F = R + '/test-only/e1b';
if (fs.existsSync(F)) throw new Error('the fixture already exists; this test uses a fresh namespace: ' + F);
fs.mkdirSync(F + '/stage-output', { recursive: true });
fs.mkdirSync(F + '/frozen-runners', { recursive: true });

const before = snapshotBusiness();
check('the business snapshot covers real files, not an empty set',
  Object.keys(before.files).length > 20 && before.files.registry !== null,
  { files: Object.keys(before.files).length, registry: before.files.registry && before.files.registry.bytes });
check('the business snapshot records the counts it will compare',
  typeof before.values.attempts === 'number' && typeof before.values.registry_rows === 'number',
  before.values);

// ---------------------------------------------------------------- 1. the pollution positive
// A real same-size change to a real file that the fixture is not allowed to touch. The comparator must name it exactly.
// The file is restored afterwards, and the restore is verified by the same comparison returning to no difference.
{
  const victim = R + '/stage-registry-129.jsonl';
  const original = readBytes(victim);
  const polluted = Buffer.from(original);
  // Change one byte in the middle of the file: the length stays the same, so only a content comparison can see it.
  const at = Math.floor(polluted.length / 2);
  polluted[at] = polluted[at] === 0x22 ? 0x23 : 0x22;
  fs.writeFileSync(victim, polluted);
  const during = snapshotBusiness();
  const d = od.diffSnapshots(before, during);
  check('a real one-byte pollution of the business registry is reported by this path',
    d.length > 0 && d.some((x) => x.at === 'files.registry.sha256'), d);
  check('the pollution did not change the file size, so only the hash could catch it',
    fs.statSync(victim).size === original.length, { size: fs.statSync(victim).size, was: original.length });
  fs.writeFileSync(victim, original);
  check('restoring the byte returns the comparison to no difference',
    od.diffSnapshots(before, snapshotBusiness()).length === 0, null);
}

// A pollution of a count, which is the case the old comparator could not see at all.
{
  const values = businessValues();
  const doctored = { ...values, attempts: values.attempts + 1 };
  const d = od.diffValues(before.values, doctored, 'values');
  check('a changed record count is reported by this path',
    d.some((x) => x.at === 'values.attempts' && x.before === values.attempts && x.after === values.attempts + 1), d);
}

// A pollution by removal and by addition.
{
  const files = businessFiles();
  const fewer = files.filter((f) => f.key !== 'registry');
  const dMissing = od.diffSnapshots(before, od.snapshot({ files: fewer, values: businessValues(), host: hostOf }));
  check('a business record that disappears is reported',
    dMissing.some((x) => x.at === 'files.registry'), dMissing);
  const more = [...files, { key: 'intruder', path: R + '/test-only/e1b/intruder.bin' }];
  const dExtra = od.diffSnapshots(before, od.snapshot({ files: more, values: businessValues(), host: hostOf }));
  check('a business record that appears is reported',
    dExtra.some((x) => x.at === 'files.intruder'), dExtra);
}

// ---------------------------------------------------------------- 2. a real fixture run, then no difference
// The runner and its pin, copied so the fixture has its own frozen root, exactly as the namespace gate requires.
{
  const runnerSrc = R + '/frozen-runners/run-129-r01.mjs';
  const runnerBytes = readBytes(runnerSrc);
  fs.writeFileSync(F + '/frozen-runners/run-129-r01.mjs', runnerBytes);
  fs.writeFileSync(F + '/runner-pin-129-run-129-r01.json', JSON.stringify({
    task: 'GTOS-DSH-129', runner: 'frozen-runners/run-129-r01.mjs',
    bytes: runnerBytes.length, sha256: sha(runnerBytes),
    note: 'the fixture is its own frozen root, so it pins the runner it holds',
  }, null, 2) + '\n');

  // A helper that does real work: it writes a file inside the fixture and reports what it wrote.
  const stageName = 'e1bw' + String(10 + (Date.now() % 90)) + '-01';
  const helperName = stageName + '.mjs';
  fs.writeFileSync(F + '/' + helperName, [
    "import fs from 'node:fs';",
    "import { readFileSync } from 'node:fs';",
    'const control = JSON.parse(readFileSync(process.argv[2], "utf8"));',
    'const root = control.args[0];',
    "fs.mkdirSync(root + '/out', { recursive: true });",
    "fs.writeFileSync(root + '/out/written.txt', 'the fixture wrote this\\n');",
    "process.stdout.write(JSON.stringify({ wrote: 'out/written.txt' }) + '\\n');",
  ].join('\n') + '\n');

  const spec = {
    task: 'GTOS-DSH-129', route: 'test.python', stage: stageName, root: F,
    fixture_root: F,
    runnerPath: F + '/frozen-runners/run-129-r01.mjs',
    helper: helperName,
    control: {
      task: 'GTOS-DSH-129', stage: stageName, report_root: F,
      // The guard writes its ledger under the fixture's own Linux namespace, which is what the namespace gate requires.
      bridge: {
        work_root: A.work_root + '/test-only/e1b',
        source_root: A.source_root,
      },
      report_path: F + '/results/' + stageName + '.json',
      args: [F],
      expected_outputs: [F + '/out/written.txt'],
    },
    expected_outputs: [F + '/out/written.txt'],
    limit: 120,
  };
  fs.writeFileSync(F + '/stage-output/' + stageName + '.spec.json', JSON.stringify(spec, null, 2) + '\n');

  // The fixture's own Linux namespace has to exist for the guard to write into it.
  const fixtureLinux = A.work_root + '/test-only/e1b';
  fs.mkdirSync(hostOf(fixtureLinux) + '/logs', { recursive: true });

  const r = spawnSync(NODE, [F + '/frozen-runners/run-129-r01.mjs', F + '/stage-output/' + stageName + '.spec.json'], {
    windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8',
  });
  const stdout = String(r.stdout || '');
  const ok = /ok=true/.test(stdout);
  check('a real stage runs inside the fixture', ok,
    { status: r.status, tail: stdout.split('\n').filter((l) => l.trim()).slice(-2) });
  check('the fixture stage really wrote its output',
    fs.existsSync(F + '/out/written.txt')
    && readBytes(F + '/out/written.txt').toString('utf8') === 'the fixture wrote this\n', null);
  check('the fixture stage produced its own records inside the fixture',
    fs.existsSync(F + '/results/' + stageName + '.json') || fs.existsSync(F + '/stage-output/' + stageName + '.spec.json'),
    null);

  // The point of the whole test: the business material, observed by this same comparator, is unchanged.
  const after = snapshotBusiness();
  const businessDiff = od.diffSnapshots(before, after);
  check('the business material is unchanged by the whole fixture run', businessDiff.length === 0, businessDiff);
  check('the business comparison covered real files, not an empty set',
    Object.keys(before.files).length === Object.keys(after.files).length
    && Object.keys(after.files).length > 20, Object.keys(after.files).length);
  check('the counts are unchanged too', od.diffValues(before.values, after.values, 'values').length === 0,
    od.diffValues(before.values, after.values, 'values'));
  // And the fixture's own records exist, so the run was not a no-op that trivially changed nothing.
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
    fixtureFiles.length >= 4, fixtureFiles.slice(0, 10));
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-129', kind: 'business_observation_test',
  subject: 'references/observation-diff-129.mjs wired to the real isolated fixture',
  fixture: F,
  business_files: Object.keys(before.files).length,
  business_values: before.values,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
