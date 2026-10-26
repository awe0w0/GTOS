// GTOS-DSH-122 R0: the atomic transaction, tested against a real isolated git repository.
//
// The fixture is a real repository with real commits. The positives really commit. The negatives really run the same
// production precheck and really fail, and after each one the tracked bytes, the index, the tip, the refs and the status
// are compared with the snapshot taken before. A refusal that leaves a dirty tree fails this test.
//
// The specific defect this closes: a transaction that validates its inputs in order and writes as it goes. The last
// negative below puts the wrong pin on the LAST source, which is exactly the case the old shape got wrong.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as tx from '../tools/atomic-transaction.mjs';

const here = new URL('.', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const R = process.argv[4] ? path.win32.resolve(process.argv[4]) : path.win32.dirname(here);
const GIT = process.argv[5] || 'C:/Users/user/AppData/Local/Programs/PortableGit-2.54.0-64-bit/cmd/git.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

function git(repo, args) {
  const r = spawnSync(GIT, ['--no-optional-locks', '-C', repo, ...args],
    { windowsHide: true, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
  return { status: r.status, out: (r.stdout || '').trim(), err: (r.stderr || '').trim() };
}

// ---------------------------------------------------------------- the fixture repository
const fixture = path.join(R, 'fixture', process.argv[6] || 'repo');
if (fs.existsSync(fixture)) throw new Error('the fixture already exists; this test builds it once: ' + fixture);
fs.mkdirSync(fixture, { recursive: true });
git(fixture, ['init', '-q', '-b', 'main']);
git(fixture, ['config', 'user.name', 'xiaokang wang']);
git(fixture, ['config', 'user.email', 'hanser0w0@gmail.com']);
fs.writeFileSync(path.join(fixture, 'base.txt'), 'base\n');
git(fixture, ['add', '--', 'base.txt']);
git(fixture, ['commit', '-q', '-m', 'fixture: base']);
const baseTip = git(fixture, ['rev-parse', 'HEAD']).out;
check('the fixture repository has a real base commit', /^[0-9a-f]{40}$/.test(baseTip), baseTip);

// Sources the transaction will materialise, kept outside the repository.
const srcDir = path.join(R, 'fixture', 'sources');
fs.mkdirSync(srcDir, { recursive: true });
const sources = [];
for (const name of ['alpha', 'beta', 'gamma']) {
  const p = path.join(srcDir, name + '.txt');
  const bytes = Buffer.from('content of ' + name + '\n', 'utf8');
  fs.writeFileSync(p, bytes);
  sources.push({ path: p, target: 'work/' + name + '.txt', bytes: bytes.length, sha256: sha(bytes) });
}
const canonicalRoot = path.join(R, 'fixture', process.argv[6] ? process.argv[6] + '-canonical' : 'canonical');
const scope = (name) => process.argv[6] ? name + '-' + process.argv[6] : name;

// ---------------------------------------------------------------- the positive
const tracked = ['base.txt', 'work/alpha.txt', 'work/beta.txt', 'work/gamma.txt'];
let positive = null;
try {
  const plan = tx.precheck({ repo: fixture, gitExe: GIT, parent: baseTip, consumer: scope('fixture-positive'),
    canonicalRoot, paths: sources.map((s) => ({ path: s.target })), sources });
  check('the positive precheck wrote nothing', !fs.existsSync(path.join(fixture, 'work')), null);
  positive = tx.commit(plan, { message: 'fixture: positive commit' });
  check('the positive commit created a new tip', positive.tip !== baseTip, positive.tip);
  check('the positive commit staged exactly the declared paths',
    JSON.stringify(positive.staged) === JSON.stringify(['work/alpha.txt', 'work/beta.txt', 'work/gamma.txt']),
    positive.staged);
  const committed = git(fixture, ['show', '--stat', '--format=', 'HEAD']).out;
  check('the commit really contains the three files', /alpha\.txt/.test(committed) && /gamma\.txt/.test(committed), committed);
} catch (error) {
  check('the positive precheck wrote nothing', false, String(error.message));
}

// ---------------------------------------------------------------- the negatives
if (positive) {
  const headAfterPositive = git(fixture, ['rev-parse', 'HEAD']).out;

  // Each negative starts from the state the positive left behind, and must return to exactly that state.
  const negatives = [
    // The one the old shape got wrong: the LAST source is the one with the wrong pin.
    ['the last source pin is wrong', (spec) => {
      spec.sources = spec.sources.map((s, i) => i === spec.sources.length - 1
        ? { ...s, sha256: 'f'.repeat(64) } : s);
    }, 'sources'],
    ['the last source is absent', (spec) => {
      spec.sources = spec.sources.map((s, i) => i === spec.sources.length - 1
        ? { ...s, path: path.join(srcDir, 'does-not-exist.txt') } : s);
    }, 'sources'],
    ['the last source has the wrong size', (spec) => {
      spec.sources = spec.sources.map((s, i) => i === spec.sources.length - 1
        ? { ...s, bytes: s.bytes + 1 } : s);
    }, 'sources'],
    ['the declared parent is wrong', (spec) => { spec.parent = 'a'.repeat(40); }, 'parent'],
    ['the consumer is already reserved', (spec) => { spec.consumer = scope('fixture-positive'); }, 'consumer'],
    ['a target path escapes the repository', (spec) => {
      spec.paths = [...spec.paths, { path: '../escape.txt' }];
    }, 'path'],
    ['a target path is absolute', (spec) => {
      spec.paths = [...spec.paths, { path: '/tmp/absolute.txt' }];
    }, 'path'],
    ['the same path is declared twice', (spec) => {
      spec.paths = [...spec.paths, { path: 'work/alpha.txt' }];
    }, 'path'],
    ['a rewrite carries no expected current bytes', (spec) => {
      // base.txt exists in the repository, so writing it needs the parent bytes declared.
      spec.paths = [...spec.paths, { path: 'base.txt' }];
      spec.sources = [...spec.sources, { path: sources[0].path, target: 'base.txt',
        bytes: sources[0].bytes, sha256: sources[0].sha256 }];
    }, 'rewrite'],
    ['a rewrite declares the wrong current bytes', (spec) => {
      spec.paths = [...spec.paths, { path: 'base.txt', expect_before: 'b'.repeat(64) }];
      spec.sources = [...spec.sources, { path: sources[0].path, target: 'base.txt',
        bytes: sources[0].bytes, sha256: sources[0].sha256 }];
    }, 'rewrite'],
  ];

  for (const [name, mutate, obligation] of negatives) {
    // Each negative starts from a clean repository, because the positive left the three target files committed and a later
    // negative would otherwise be refused by the rewrite rule before reaching the obligation it is meant to test. The
    // reset is a checkout of the base commit in the fixture only.
    git(fixture, ['checkout', '-q', baseTip]);
    git(fixture, ['clean', '-qfd']);
    const spec = { repo: fixture, gitExe: GIT, parent: baseTip, consumer: scope('fixture-negative'),
      canonicalRoot, paths: sources.map((s) => ({ path: s.target })), sources: sources.map((s) => ({ ...s })) };
    mutate(spec);

    const before = tx.snapshot(fixture, GIT, tracked);
    let refused = false;
    let reason = null;
    try {
      const plan = tx.precheck(spec);
      // If the precheck somehow passed, the test must not commit: the point is that it should have refused.
      reason = { passedUnexpectedly: true, plan: { verified: plan.verified.length } };
    } catch (error) {
      refused = true;
      reason = error.problems ? error.problems[0] : { obligation: 'throw', why: String(error.message) };
    }
    const after = tx.snapshot(fixture, GIT, tracked);
    const changes = tx.diffSnapshot(before, after);

    check(`refused: ${name}`, refused && reason.obligation === obligation,
      { refused, obligation: reason.obligation, why: reason.why });
    check(`no tracked byte, index, tip, ref or status changed: ${name}`, changes.length === 0, changes);
  }

  // ---------------------------------------------------------------- the old defect, stated as its own check
  // Validate-then-write must not degrade into validate-as-you-write. With the last source broken, not one target file may
  // exist on disk afterwards.
  {
    git(fixture, ['checkout', '-q', baseTip]);
    git(fixture, ['clean', '-qfd']);
    const spec = { repo: fixture, gitExe: GIT, parent: baseTip, consumer: scope('fixture-lastpin'),
      canonicalRoot, paths: sources.map((s) => ({ path: s.target })),
      sources: sources.map((s, i) => i === sources.length - 1 ? { ...s, sha256: '0'.repeat(64) } : ({ ...s })) };
    const before = tx.snapshot(fixture, GIT, tracked);
    try { tx.precheck(spec); } catch { /* the refusal is the expected outcome */ }
    const after = tx.snapshot(fixture, GIT, tracked);
    const changes = tx.diffSnapshot(before, after);
    check('a failure on the last source leaves nothing written', changes.length === 0, changes);
    check('no target file appeared even though the first sources were valid',
      !fs.existsSync(path.join(fixture, 'work', 'alpha.txt')) || changes.length === 0,
      fs.existsSync(path.join(fixture, 'work', 'alpha.txt')));
  }

  // ---------------------------------------------------------------- canonical claims are exclusive
  {
    const first = tx.writeCanonical(R, process.argv[6] ? 'fixture-canonical-' + process.argv[6] : 'fixture-canonical', {
      'show.txt': Buffer.from('show\n'), 'show.patch': Buffer.from('patch\n'),
      'format-patch.patch': Buffer.from('format\n'), 'commit-object.txt': Buffer.from('object\n'),
    });
    check('four canonical artifacts were written', first.written.length === 4, first.written.length);
    let secondThrew = false;
    try {
      tx.writeCanonical(R, process.argv[6] ? 'fixture-canonical-' + process.argv[6] : 'fixture-canonical', { 'show.txt': Buffer.from('other\n') });
    } catch { secondThrew = true; }
    check('a second claim on the same consumer is refused', secondThrew, null);
  }
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-122', kind: 'atomic_transaction_fixture',
  subject: 'references/atomic-transaction-122.mjs',
  fixture: { repo: fixture, base_tip: baseTip, sources: sources.length },
  positive: positive ? { tip: positive.tip, staged: positive.staged } : null,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
