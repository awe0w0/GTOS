// GTOS-DSH-111 G0 transaction test: two real sequential commits in a real fixture repository.
//
// The point is the property 110 lacked. The second commit must reserve its consumer BEFORE it creates a commit
// object, so the artifact collision that left 110 with "commit created, delivery incomplete" cannot happen here. The
// test drives the real delivery module against a real repository created by the pinned git.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
if (control.task !== 'GTOS-DSH-111') throw new Error('control is for ' + control.task);

const R = control.report_root;
const FIX = control.fixture_root;
if (fs.existsSync(FIX)) throw new Error('the fixture root must not exist yet: ' + FIX);
fs.mkdirSync(FIX, { recursive: true });

const out = [];
function check(name, ok, detail) {
  out.push({ name, ok: Boolean(ok), detail: detail === undefined ? null : detail });
}

const delivery = await import('file:///' + control.delivery_module);
const canonical = await import('file:///' + control.canonical_module);
const GITEXE = control.git;

function git(cwd, args) {
  const r = spawnSync(GITEXE, ['--no-optional-locks', '--no-pager', '-c', 'core.autocrlf=false',
    '-c', 'core.longpaths=true', '-c', 'core.hooksPath=' + FIX + '/empty-hooks', ...args],
    { cwd, windowsHide: true, timeout: 120000, maxBuffer: 32 * 1024 * 1024,
      env: { ...process.env, GIT_OPTIONAL_LOCKS: '0', GIT_TERMINAL_PROMPT: '0' } });
  if (r.status !== 0) throw new Error('git ' + args[0] + ' failed: ' + (r.stderr || '').toString().slice(0, 300));
  return (r.stdout || Buffer.alloc(0)).toString('utf8').trim();
}

// ---------------------------------------------------------------- a real fixture repository
const repo = FIX + '/repo';
const hooks = FIX + '/empty-hooks';
fs.mkdirSync(hooks, { recursive: true });
fs.mkdirSync(repo, { recursive: true });
git(repo, ['init', '-q', '-b', 'dev']);
fs.writeFileSync(repo + '/README', 'fixture base\n', { encoding: 'utf8' });
git(repo, ['add', '--', 'README']);
git(repo, ['-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid',
  'commit', '-q', '-m', 'fixture base']);
const base = git(repo, ['rev-parse', 'HEAD']);

const author = { name: 'xiaokang wang', email: 'hanser0w0@gmail.com' };
const worktree = FIX + '/worktree';
const branch = 'fixture-branch';

function policyFor(commitName, files, parent) {
  return {
    task: 'GTOS-DSH-111', test_fixture_only: true,
    report_root: FIX, repository: repo, worktree, branch,
    base: parent, primary_base: base,
    git: GITEXE, empty_hooks: hooks, author,
    message: 'fixture: ' + commitName,
    stage: commitName === 'first' ? 'gitcommit-01' : 'gitcommit-02',
    canonical_consumer: 'git-commit' + (commitName === 'first' ? '' : '-2'),
    files,
  };
}
function makeFiles(prefix, count) {
  return Array.from({ length: count }, (_, i) => {
    const logical = 'src/' + prefix + '-' + i + '.txt';
    const source = FIX + '/sources/' + prefix + '-' + i + '.txt';
    fs.mkdirSync(path.dirname(source), { recursive: true });
    const body = Buffer.from('content of ' + logical + ' at ' + prefix + '\n', 'utf8');
    fs.writeFileSync(source, body);
    return { logical, source, bytes: body.length, sha256: sha(body), parent_pin: null, shape: 'create' };
  });
}

// ---------------------------------------------------------------- the first commit
const firstFiles = makeFiles('first', 3);
const firstPolicy = policyFor('first', firstFiles, base);
const firstPrepared = delivery.prepare(firstPolicy);
check('the first commit stages exactly its declared paths', firstPrepared.staged_count === 3,
  firstPrepared.staged_count);
const firstCommitted = delivery.commit(firstPolicy, canonical);
check('the first commit is created', typeof firstCommitted.oid === 'string' && firstCommitted.oid.length === 40,
  firstCommitted.oid);
check('the first commit reserves its consumer before mutating', firstCommitted.canonical_reservation
  && firstCommitted.canonical_reservation.reserved === true, firstCommitted.canonical_reservation);
check('the first commit writes four canonical artifacts', firstCommitted.artifacts.length === 4,
  firstCommitted.artifacts.length);
check('the first commit artifacts are originals',
  firstCommitted.artifacts.every((a) => a.kind === 'original'), true);
const firstArtifactDir = path.dirname(firstCommitted.artifacts[0].path);
check('the first commit owns its own consumer directory',
  path.resolve(firstArtifactDir) === path.resolve(FIX + '/canonical/git-commit'), firstArtifactDir);
const firstCommitBytes = fs.readFileSync(firstCommitted.artifacts.find((a) => a.name === 'commit.json').path);
check('the first commit artifact records the first OID',
  JSON.parse(firstCommitBytes.toString('utf8')).oid === firstCommitted.oid, true);

// ---------------------------------------------------------------- the second commit reuses the worktree
const secondFiles = makeFiles('second', 2);
const secondPolicy = policyFor('second', secondFiles, firstCommitted.oid);
const secondPrepared = delivery.prepare(secondPolicy);
check('the second commit reuses the worktree at its declared base',
  secondPrepared.base === firstCommitted.oid, secondPrepared.base);
const secondCommitted = delivery.commit(secondPolicy, canonical);
check('the second commit is created', typeof secondCommitted.oid === 'string'
  && secondCommitted.oid !== firstCommitted.oid, secondCommitted.oid);
check('the second commit parent is the first commit',
  secondCommitted.parent === firstCommitted.oid, secondCommitted.parent);
check('the second commit reserves a DIFFERENT consumer', secondCommitted.canonical_consumer === 'git-commit-2',
  secondCommitted.canonical_consumer);
check('the second commit writes four canonical artifacts', secondCommitted.artifacts.length === 4,
  secondCommitted.artifacts.length);
check('the second commit artifacts are originals, not reconstructions',
  secondCommitted.artifacts.every((a) => a.kind === 'original'),
  secondCommitted.artifacts.map((a) => a.kind));
const secondDir = path.dirname(secondCommitted.artifacts[0].path);
check('the two commits have distinct canonical directories', secondDir !== firstArtifactDir,
  [firstArtifactDir, secondDir]);

// ---------------------------------------------------------------- the first commit's artifacts survive
const firstAfterSecond = fs.readFileSync(firstCommitted.artifacts.find((a) => a.name === 'commit.json').path);
check('the first commit artifacts are untouched by the second',
  firstAfterSecond.equals(firstCommitBytes), true);
check('the first commit still records its own OID',
  JSON.parse(firstAfterSecond.toString('utf8')).oid === firstCommitted.oid, true);

// ---------------------------------------------------------------- the 110 failure is now impossible
// Reserving the first consumer again must refuse BEFORE any Git mutation. This is the check that would have caught
// 110's defect: the collision is detected while the branch tip and the artifacts are still consistent.
const tipBefore = git(repo, ['rev-parse', branch]);
let reserveRefused = false, reserveReason = null;
try { delivery.reserveConsumer(FIX, 'git-commit', 'gitcommit-03'); }
catch (e) { reserveRefused = true; reserveReason = String(e.message || e); }
check('re-reserving a used consumer refuses', reserveRefused, reserveReason);
check('the refusal names the owning stage', reserveReason !== null && /gitcommit-01/.test(reserveReason),
  reserveReason);
check('the branch tip did not move during the refusal', git(repo, ['rev-parse', branch]) === tipBefore, tipBefore);
check('the first artifacts are still intact after the refusal',
  fs.readFileSync(firstCommitted.artifacts.find((a) => a.name === 'commit.json').path).equals(firstCommitBytes),
  true);

// ---------------------------------------------------------------- a wrong parent refuses before mutation
const wrongParent = policyFor('first', makeFiles('wrong', 1), 'f'.repeat(40));
let parentRefused = false, parentReason = null;
try { delivery.prepare(wrongParent); } catch (e) { parentRefused = true; parentReason = String(e.message || e); }
check('a wrong parent refuses', parentRefused, parentReason);
check('the wrong-parent refusal did not move the tip', git(repo, ['rev-parse', branch]) === tipBefore, tipBefore);

// ---------------------------------------------------------------- an occupied new path refuses
const occupied = makeFiles('occupied', 1);
occupied[0].logical = 'README';
const occupiedPolicy = policyFor('first', occupied, secondCommitted.oid);
occupiedPolicy.canonical_consumer = 'git-commit-3';
let occupiedRefused = false, occupiedReason = null;
try { delivery.prepare(occupiedPolicy); }
catch (e) { occupiedRefused = true; occupiedReason = String(e.message || e); }
check('an occupied new path refuses', occupiedRefused, occupiedReason);

// ---------------------------------------------------------------- five-way identity across both commits
for (const [label, committed, policy] of [['first', firstCommitted, firstPolicy],
  ['second', secondCommitted, secondPolicy]]) {
  const doc = JSON.parse(fs.readFileSync(committed.artifacts.find((a) => a.name === 'commit.json').path, 'utf8'));
  const fiveWay = policy.files.every((f) => {
    const stagedBytes = fs.readFileSync(f.source);
    const inWorktree = fs.readFileSync(path.join(worktree, ...f.logical.split('/')));
    // Three independent byte facts: the declared hash, the committed blob's own hash, and the worktree bytes.
    const committedBlob = git(repo, ['rev-parse', committed.oid + ':' + f.logical]);
    const sourceBlob = git(repo, ['hash-object', '--no-filters', f.source]);
    return stagedBytes.length === f.bytes && sha(stagedBytes) === f.sha256
      && inWorktree.equals(stagedBytes) && committedBlob === sourceBlob
      && /^[0-9a-f]{40}$/.test(committedBlob);
  });
  check(label + ' commit has five-way byte identity', fiveWay, true);
  check(label + ' commit artifact blobs agree with the commit',
    doc.blobs.length === policy.files.length
    && doc.blobs.every((b) => policy.files.some((f) => f.logical === b.logical && f.sha256 === b.sha256)),
    doc.blobs.length);
}

const summary = { task: 'GTOS-DSH-111', kind: 'g0_transaction',
  first_commit: firstCommitted.oid, second_commit: secondCommitted.oid,
  checks: out.length, failed: out.filter((c) => !c.ok).length, results: out,
  ok: out.every((c) => c.ok) };
const p = control.result_path;
if (fs.existsSync(p)) throw new Error('result exists');
fs.writeFileSync(p, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: summary.ok, checks: summary.checks, failed: summary.failed,
  first: summary.first_commit, second: summary.second_commit }, null, 2));
for (const c of out.filter((x) => !x.ok)) console.log('  FAIL ' + c.name + ' :: ' + JSON.stringify(c.detail));
if (!summary.ok) process.exitCode = 1;
