// GTOS-DSH-123 P0b: the business commit entry, tested through that entry and not through the module beneath it.
//
// Every negative below calls `commitBusiness`, which is the function the business path calls. The assertion is not "the
// precheck threw" but "the business entry refused and the worktree, the index (bytes AND mtime), the tip, the refs, the
// status and the canonical directory are exactly what they were".
//
// The 122 test could only show that the precheck function refuses. This one shows that the entry refuses without leaving
// anything behind, which is the property that matters to the repository.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as bc from '../tools/business-commit.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-admitted-default-link-and-tested-consumer-integration-20261026-a';
const GIT = process.argv[5] || 'C:/Users/user/AppData/Local/Programs/PortableGit-2.54.0-64-bit/cmd/git.exe';
const SCOPE = process.argv[6] || 'biz';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

function git(repo, args) {
  const r = spawnSync(GIT, ['--no-optional-locks', '-C', repo, ...args],
    { windowsHide: true, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
  return { status: r.status, out: (r.stdout || '').trim(), err: (r.stderr || '').trim() };
}

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
check('the fixture repository has a real base commit', /^[0-9a-f]{40}$/.test(baseTip), baseTip);

const srcDir = path.join(root, 'sources');
fs.mkdirSync(srcDir, { recursive: true });
const sources = [];
for (const name of ['alpha', 'beta', 'gamma']) {
  const p = path.join(srcDir, name + '.txt');
  const bytes = Buffer.from('content of ' + name + '\n', 'utf8');
  fs.writeFileSync(p, bytes);
  sources.push({ path: p, target: 'work/' + name + '.txt', bytes: bytes.length, sha256: sha(bytes) });
}
const canonicalRoot = path.join(root, 'canonical');
const tracked = ['base.txt', 'work/alpha.txt', 'work/beta.txt', 'work/gamma.txt'];
const scope = (n) => n + '-' + SCOPE;

// ---------------------------------------------------------------- the positive, through the business entry
let positive = null;
try {
  positive = bc.commitBusiness({ repo, gitExe: GIT, parent: baseTip, consumer: scope('positive'),
    canonicalRoot, paths: sources.map((s) => ({ path: s.target })), sources,
    trackedPaths: tracked, message: 'fixture: business positive' });
  check('the business entry committed', /^[0-9a-f]{40}$/.test(positive.tip) && positive.tip !== baseTip, positive.tip);
  check('the business entry staged exactly the declared paths',
    JSON.stringify(positive.staged) === JSON.stringify(['work/alpha.txt', 'work/beta.txt', 'work/gamma.txt']),
    positive.staged);
  check('the reservation was taken in phase two', fs.existsSync(positive.reservation), positive.reservation);
} catch (error) {
  check('the business entry committed', false, String(error.message));
}

// ---------------------------------------------------------------- the negatives, through the same entry
if (positive) {
  const negatives = [
    // The last source is the one with the wrong pin: the case the old validate-as-you-write shape got wrong.
    ['the last source pin is wrong', (d) => {
      d.sources = d.sources.map((s, i) => i === d.sources.length - 1 ? { ...s, sha256: 'f'.repeat(64) } : s);
    }, 'sources'],
    ['the last source is absent', (d) => {
      d.sources = d.sources.map((s, i) => i === d.sources.length - 1
        ? { ...s, path: path.join(srcDir, 'absent.txt') } : s);
    }, 'sources'],
    ['the declared parent is wrong', (d) => { d.parent = 'a'.repeat(40); }, 'parent'],
    ['the consumer is already reserved', (d) => { d.consumer = scope('positive'); }, 'consumer'],
    ['a target path escapes the repository', (d) => { d.paths = [...d.paths, { path: '../escape.txt' }]; }, 'path'],
    ['a rewrite declares the wrong current bytes', (d) => {
      d.paths = [...d.paths, { path: 'base.txt', expect_before: 'b'.repeat(64) }];
      d.sources = [...d.sources, { path: sources[0].path, target: 'base.txt',
        bytes: sources[0].bytes, sha256: sources[0].sha256 }];
    }, 'rewrite'],
  ];

  for (const [name, mutate, obligation] of negatives) {
    // Each negative starts from the base commit so that the positive's files are not in the way.
    git(repo, ['checkout', '-q', baseTip]);
    git(repo, ['clean', '-qfd']);
    const declaration = { repo, gitExe: GIT, parent: baseTip, consumer: scope('negative'),
      canonicalRoot, paths: sources.map((s) => ({ path: s.target })), sources: sources.map((s) => ({ ...s })),
      trackedPaths: tracked, message: 'should not happen' };
    mutate(declaration);

    const before = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    let refused = false;
    let reason = null;
    let sideEffects = null;
    try {
      bc.commitBusiness(declaration);
      reason = { passedUnexpectedly: true };
    } catch (error) {
      refused = true;
      reason = error.problems ? error.problems[0] : { obligation: 'throw', why: String(error.message) };
      sideEffects = error.side_effects;
    }
    const after = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    const changes = bc.businessDiff(before, after);

    check(`the business entry refused: ${name}`, refused && reason.obligation === obligation,
      { refused, obligation: reason.obligation, why: reason.why });
    // This is the assertion the 122 test could not make: nothing at all moved, including the index mtime.
    check(`nothing moved after the refusal: ${name}`, changes.length === 0, changes);
    check(`the entry reported its own side effects: ${name}`,
      Array.isArray(sideEffects) && sideEffects.length === 0, sideEffects);
  }

  // ---------------------------------------------------------------- the old defect, at the business entry
  {
    git(repo, ['checkout', '-q', baseTip]);
    git(repo, ['clean', '-qfd']);
    const declaration = { repo, gitExe: GIT, parent: baseTip, consumer: scope('lastpin'),
      canonicalRoot, paths: sources.map((s) => ({ path: s.target })),
      sources: sources.map((s, i) => i === sources.length - 1 ? { ...s, sha256: '0'.repeat(64) } : ({ ...s })),
      trackedPaths: tracked, message: 'should not happen' };
    const before = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    try { bc.commitBusiness(declaration); } catch { /* the refusal is the expected outcome */ }
    const after = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    const changes = bc.businessDiff(before, after);
    // No "or" branch here: the directory must not exist, and nothing else may have moved either.
    const workDirExists = fs.existsSync(path.join(repo, 'work'));
    check('a failure on the last source leaves no target directory', workDirExists === false, workDirExists);
    check('a failure on the last source leaves nothing else moved', changes.length === 0, changes);
    check('a failure on the last source leaves no reservation',
      !fs.existsSync(path.join(canonicalRoot, scope('lastpin'))), null);
  }

  // ---------------------------------------------------------------- the index mtime is really watched
  // A sanity check on the instrument itself: if the snapshot could not see an index write, every assertion above would be
  // vacuous. This stages a file and requires the diff to report the index.
  {
    const before = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    fs.writeFileSync(path.join(repo, 'base.txt'), 'base changed\n');
    git(repo, ['add', '--', 'base.txt']);
    const after = bc.businessSnapshot(repo, GIT, tracked, canonicalRoot);
    const changes = bc.businessDiff(before, after);
    check('the instrument sees an index write', changes.includes('index') && changes.includes('file base.txt'), changes);
    git(repo, ['reset', '-q', '--hard', baseTip]);
    git(repo, ['clean', '-qfd']);
  }
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-123', kind: 'business_commit_entry_test',
  subject: 'references/business-commit-123.mjs',
  fixture: { repo, base_tip: baseTip, sources: sources.length },
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
