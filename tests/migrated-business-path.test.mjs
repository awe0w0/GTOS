// GTOS-DSH-125 F0b: the business entry, exercised on the migrated path.
//
// The 123 fixture proved the transaction module refuses. This test proves the path the business now uses does too, and that
// the specific defect of the old driver cannot happen: a wrong pin on the LAST declared file must leave not one earlier
// file written, because the entry prechecks the whole declaration before it writes anything.
//
// Everything here runs through `commitBusiness`, which is what the commit driver calls.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as bc from '../tools/business-commit.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-producer-business-wiring-runtime-recovery-20261026-a';
const GIT = process.argv[5] || 'C:/Users/user/AppData/Local/Programs/PortableGit-2.54.0-64-bit/cmd/git.exe';
const SCOPE = process.argv[6] || 'f0b';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const git = (repo, args) => {
  const r = spawnSync(GIT, ['--no-optional-locks', '-C', repo, ...args],
    { windowsHide: true, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
  return { status: r.status, out: (r.stdout || '').trim(), err: (r.stderr || '').trim() };
};

// ---------------------------------------------------------------- the fixture
const root = path.join(R, 'fixture', SCOPE);
const repo = path.join(root, 'repo');
if (fs.existsSync(root)) throw new Error('the fixture already exists; this test builds it once: ' + root);
fs.mkdirSync(repo, { recursive: true });
git(repo, ['init', '-q', '-b', 'main']);
git(repo, ['config', 'user.name', 'xiaokang wang']);
git(repo, ['config', 'user.email', 'hanser0w0@gmail.com']);
fs.writeFileSync(path.join(repo, 'base.txt'), 'base\n');
git(repo, ['add', '--', 'base.txt']);
git(repo, ['commit', '-q', '-m', 'fixture: base']);
const baseTip = git(repo, ['rev-parse', 'HEAD']).out;
check('the fixture has a real base commit', /^[0-9a-f]{40}$/.test(baseTip), baseTip);

const srcDir = path.join(root, 'sources');
fs.mkdirSync(srcDir, { recursive: true });
const sources = [];
for (const name of ['alpha', 'beta', 'gamma', 'delta']) {
  const p = path.join(srcDir, name + '.txt');
  const bytes = Buffer.from('content of ' + name + '\n', 'utf8');
  fs.writeFileSync(p, bytes);
  sources.push({ path: p, target: 'work/' + name + '.txt', bytes: bytes.length, sha256: sha(bytes) });
}
const canonicalRoot = path.join(root, 'canonical');
const tracked = ['base.txt', 'work/alpha.txt', 'work/beta.txt', 'work/gamma.txt', 'work/delta.txt'];
const scope = (n) => n + '-' + SCOPE;
const declaration = (consumer) => ({
  repo, gitExe: GIT, parent: baseTip, consumer: scope(consumer), canonicalRoot,
  paths: sources.map((s) => ({ path: s.target })), sources: sources.map((s) => ({ ...s })),
  trackedPaths: tracked, message: 'fixture: business commit',
  author: 'xiaokang wang', email: 'hanser0w0@gmail.com',
});

// ---------------------------------------------------------------- the positive
let positive = null;
try {
  positive = bc.commitBusiness(declaration('positive'));
  check('the business entry committed', /^[0-9a-f]{40}$/.test(String(positive.tip)), positive.tip);
  check('it staged exactly the four declared paths',
    JSON.stringify(positive.staged) === JSON.stringify(['work/alpha.txt', 'work/beta.txt', 'work/delta.txt',
      'work/gamma.txt']), positive.staged);
} catch (error) {
  check('the business entry committed', false, String(error.message));
}

// ---------------------------------------------------------------- the negatives through the same entry
if (positive) {
  const negatives = [
    // The old driver's exact defect: the last source is the one with the wrong pin.
    ['the last source pin is wrong', (d) => {
      d.sources = d.sources.map((s, i) => i === d.sources.length - 1 ? { ...s, sha256: 'f'.repeat(64) } : s);
    }, 'sources'],
    ['the last source is absent', (d) => {
      d.sources = d.sources.map((s, i) => i === d.sources.length - 1
        ? { ...s, path: path.join(srcDir, 'absent.txt') } : s);
    }, 'sources'],
    ['the parent is wrong', (d) => { d.parent = 'a'.repeat(40); }, 'parent'],
    ['the consumer is already reserved', (d) => { d.consumer = scope('positive'); }, 'consumer'],
    ['a target escapes the repository', (d) => { d.paths = [...d.paths, { path: '../escape.txt' }]; }, 'path'],
    ['a target is absolute', (d) => { d.paths = [...d.paths, { path: '/tmp/abs.txt' }]; }, 'path'],
    ['a rewrite declares the wrong current bytes', (d) => {
      d.paths = [...d.paths, { path: 'base.txt', expect_before: 'b'.repeat(64) }];
      d.sources = [...d.sources, { path: sources[0].path, target: 'base.txt',
        bytes: sources[0].bytes, sha256: sources[0].sha256 }];
    }, 'rewrite'],
  ];
  for (const [name, mutate, obligation] of negatives) {
    git(repo, ['checkout', '-q', baseTip]);
    git(repo, ['clean', '-qfd']);
    const d = declaration('negative');
    mutate(d);
    const before = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    let refused = false;
    let reason = null;
    let sideEffects = null;
    try { bc.commitBusiness(d); reason = { passedUnexpectedly: true }; }
    catch (error) {
      refused = true;
      reason = error.problems ? error.problems[0] : { obligation: 'throw', why: String(error.message) };
      sideEffects = error.side_effects;
    }
    const after = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    const changes = bc.businessDiff(before, after);
    check(`refused at ${obligation}: ${name}`, refused && reason.obligation === obligation, reason);
    check(`nothing moved: ${name}`, changes.length === 0, changes);
    check(`the entry reported no side effect: ${name}`,
      Array.isArray(sideEffects) && sideEffects.length === 0, sideEffects);
  }

  // ---------------------------------------------------------------- the old defect stated as its own check
  {
    git(repo, ['checkout', '-q', baseTip]);
    git(repo, ['clean', '-qfd']);
    const d = declaration('lastpin');
    d.sources = d.sources.map((s, i) => i === d.sources.length - 1 ? { ...s, sha256: '0'.repeat(64) } : ({ ...s }));
    const before = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    try { bc.commitBusiness(d); } catch { /* the refusal is the expected outcome */ }
    const after = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    const changes = bc.businessDiff(before, after);
    // No escape branch: the directory must not exist, and nothing else may have moved either.
    check('a wrong last pin leaves no target directory at all',
      fs.existsSync(path.join(repo, 'work')) === false, fs.existsSync(path.join(repo, 'work')));
    check('a wrong last pin leaves the tree, index, tip and refs untouched', changes.length === 0, changes);
    check('a wrong last pin leaves no reservation',
      !fs.existsSync(path.join(canonicalRoot, scope('lastpin'))), null);
  }

  // ---------------------------------------------------------------- a refusal does not poison the next commit
  {
    git(repo, ['checkout', '-q', baseTip]);
    git(repo, ['clean', '-qfd']);
    const bad = declaration('recover');
    bad.sources = bad.sources.map((s, i) => i === 0 ? { ...s, sha256: 'c'.repeat(64) } : s);
    let refused = false;
    try { bc.commitBusiness(bad); } catch { refused = true; }
    check('the bad declaration was refused', refused, null);
    // The same consumer, now with a correct declaration, must commit.
    let recovered = null;
    try { recovered = bc.commitBusiness(declaration('recover')); } catch (error) {
      check('a valid declaration after a refusal commits', false, String(error.message));
    }
    check('a valid declaration after a refusal commits',
      recovered !== null && /^[0-9a-f]{40}$/.test(String(recovered.tip)), recovered ? recovered.tip : null);
    check('the recovered commit carries the declared paths',
      recovered !== null && JSON.stringify(recovered.staged) === JSON.stringify(['work/alpha.txt', 'work/beta.txt',
        'work/delta.txt', 'work/gamma.txt']), recovered ? recovered.staged : null);
  }

  // ---------------------------------------------------------------- a forged plan is not a plan
  {
    git(repo, ['checkout', '-q', baseTip]);
    git(repo, ['clean', '-qfd']);
    // A plan that was never produced by precheck has no verified bytes, so commit must refuse it.
    let refused = false;
    try { bc.commitBusiness({ ...declaration('forged'), sources: [] }); } catch { refused = true; }
    check('a declaration with no sources is refused', refused, null);
    // And commit itself refuses a plan it did not make.
    let planRefused = false;
    try {
      const tx = await import('file:///' + R + '/references/atomic-transaction.mjs');
      tx.commit({ repo, gitExe: GIT, consumer: scope('planforged'), canonicalRoot, verified: undefined });
    } catch { planRefused = true; }
    check('commit refuses a plan that precheck did not produce', planRefused, null);
  }

  // ---------------------------------------------------------------- the instrument sees an index write
  {
    const before = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    fs.writeFileSync(path.join(repo, 'base.txt'), 'changed\n');
    git(repo, ['add', '--', 'base.txt']);
    const after = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    const changes = bc.businessDiff(before, after);
    check('the snapshot instrument sees an index write', changes.includes('index'), changes);
    git(repo, ['reset', '-q', '--hard', baseTip]);
    git(repo, ['clean', '-qfd']);
  }
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-125', kind: 'migrated_business_path_test',
  subject: 'references/business-commit-125.mjs',
  fixture: { repo, base_tip: baseTip, sources: sources.length },
  positive: positive ? { tip: positive.tip, staged: positive.staged } : null,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const outPath = process.argv[3];
if (outPath) {
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
