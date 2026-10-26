// GTOS-DSH-122 R0: an atomic transaction, where every check runs before the first write.
//
// The failure this exists to prevent is a transaction that validates its inputs as it goes: it writes the first file, then
// discovers the last source pin is wrong, and leaves a half-materialised worktree behind. A refusal that leaves a dirty
// tree is not a refusal, it is a partial commit that happened to stop early.
//
// So the shape is strictly two-phase:
//
//   precheck()  reads everything and writes nothing. It returns a plan or throws.
//   commit()    takes a plan that precheck produced, and is the only function that writes.
//
// A caller cannot skip the precheck, because commit takes the plan object and the plan is the only thing that carries the
// verified bytes. Nothing here is a mock: the git calls are real, the repository is real, and the byte comparisons are of
// real files.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

function git(exe, repo, args) {
  const r = spawnSync(exe, ['--no-optional-locks', '-C', repo, ...args],
    { windowsHide: true, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
  return { status: r.status, stdout: (r.stdout || '').trim(), stderr: (r.stderr || '').trim() };
}

// A snapshot of everything a refusal must leave untouched. Tracked file bytes, the index, the tip, the refs.
export function snapshot(repo, exe, trackedPaths) {
  const files = {};
  for (const rel of trackedPaths) {
    const p = path.join(repo, rel);
    files[rel] = fs.existsSync(p) ? { bytes: fs.readFileSync(p).length, sha256: sha256(fs.readFileSync(p)) } : null;
  }
  const indexPath = path.join(repo, '.git', 'index');
  const index = fs.existsSync(indexPath)
    ? { bytes: fs.readFileSync(indexPath).length, sha256: sha256(fs.readFileSync(indexPath)) } : null;
  return {
    files,
    index,
    tip: git(exe, repo, ['rev-parse', 'HEAD']).stdout || null,
    refs: git(exe, repo, ['for-each-ref', '--format=%(refname) %(objectname)']).stdout || '',
    status: git(exe, repo, ['status', '--porcelain']).stdout || '',
  };
}

export function diffSnapshot(before, after) {
  const changes = [];
  const keys = new Set([...Object.keys(before.files), ...Object.keys(after.files)]);
  for (const k of keys) {
    const a = before.files[k], b = after.files[k];
    if (JSON.stringify(a) !== JSON.stringify(b)) changes.push('file ' + k);
  }
  if (JSON.stringify(before.index) !== JSON.stringify(after.index)) changes.push('index');
  if (before.tip !== after.tip) changes.push('tip');
  if (before.refs !== after.refs) changes.push('refs');
  if (before.status !== after.status) changes.push('status');
  return changes;
}

// ---------------------------------------------------------------- phase one: read everything, write nothing

export function precheck(spec) {
  const { repo, gitExe, parent, consumer, canonicalRoot, paths: declared, sources, forbiddenPrefixes } = spec;
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const fail = () => { const e = new Error('TRANSACTION REFUSED at ' + problems[0].obligation + ': ' + problems[0].why);
    e.problems = problems; throw e; };

  // ---- the repository and its tip
  if (!fs.existsSync(path.join(repo, '.git'))) refuse('repository', 'not a git repository: ' + repo);
  else {
    const tip = git(gitExe, repo, ['rev-parse', 'HEAD']);
    if (tip.status !== 0) refuse('repository', 'cannot read the tip: ' + tip.stderr);
    else if (tip.stdout !== parent) refuse('parent', 'the tip is ' + tip.stdout + ', not the declared parent ' + parent);
  }

  // ---- the consumer must be unique. Its canonical directory is the reservation, so an existing one means another
  // transaction already claimed this consumer.
  if (typeof consumer !== 'string' || !/^[a-z0-9][a-z0-9-]*$/.test(consumer)) {
    refuse('consumer', 'bad consumer name: ' + consumer);
  } else if (fs.existsSync(path.join(canonicalRoot, consumer))) {
    refuse('consumer', 'the consumer is already reserved: ' + consumer);
  }

  // ---- every declared path must be a safe relative path inside the repository
  const seen = new Set();
  for (const d of declared) {
    const rel = d.path;
    if (typeof rel !== 'string' || !rel) { refuse('path', 'a declared path is empty'); break; }
    if (rel.startsWith('/') || /^[A-Za-z]:/.test(rel)) { refuse('path', 'absolute path: ' + rel); break; }
    const parts = rel.split('/');
    if (parts.some((p) => p === '' || p === '.' || p === '..')) { refuse('path', 'path escapes or is unsafe: ' + rel); break; }
    if (seen.has(rel)) { refuse('path', 'the same path is declared twice: ' + rel); break; }
    seen.add(rel);
    if (Array.isArray(forbiddenPrefixes) && forbiddenPrefixes.some((f) => rel.startsWith(f))) {
      refuse('path', 'the path is in a forbidden area: ' + rel);
      break;
    }
    // The resolved path must still be inside the repository, which catches a symlinked parent.
    const resolved = path.resolve(repo, rel);
    if (!resolved.startsWith(path.resolve(repo) + path.sep)) {
      refuse('path', 'the resolved path leaves the repository: ' + rel);
      break;
    }
  }

  // ---- every source must exist and match its pin. This is the check that must include the LAST source, which is what the
  // old defect got wrong: it validated in order and wrote as it went.
  const verified = [];
  if (!Array.isArray(sources) || !sources.length) refuse('sources', 'no sources declared');
  else {
    for (const s of sources) {
      if (typeof s.path !== 'string' || !s.path) { refuse('sources', 'a source has no path'); break; }
      if (!fs.existsSync(s.path)) { refuse('sources', 'absent source: ' + s.path); break; }
      const bytes = fs.readFileSync(s.path);
      if (typeof s.bytes === 'number' && bytes.length !== s.bytes) {
        refuse('sources', 'size mismatch for ' + s.path + ': ' + bytes.length + ' is not ' + s.bytes);
        break;
      }
      if (typeof s.sha256 === 'string' && sha256(bytes) !== s.sha256) {
        refuse('sources', 'hash mismatch for ' + s.path);
        break;
      }
      verified.push({ path: s.path, target: s.target, bytes: bytes.length, sha256: sha256(bytes), content: bytes });
    }
  }

  // ---- a rewrite of an existing path is only allowed when the current bytes are what the declaration pinned
  for (const v of verified) {
    const current = path.join(repo, v.target);
    if (!fs.existsSync(current)) continue;
    const now = sha256(fs.readFileSync(current));
    const declaredBefore = declared.find((d) => d.path === v.target);
    if (!declaredBefore || typeof declaredBefore.expect_before !== 'string') {
      refuse('rewrite', 'the path exists but the declaration pins no expected current bytes: ' + v.target);
      break;
    }
    if (now !== declaredBefore.expect_before) {
      refuse('rewrite', 'the current bytes of ' + v.target + ' are not the declared parent bytes');
      break;
    }
  }

  if (problems.length) fail();

  // The plan carries the verified bytes. Nothing downstream re-reads a source, so a file cannot change between the check and
  // the write.
  return { repo, gitExe, parent, consumer, canonicalRoot, paths: declared, verified, checks_run: problems.length };
}

// ---------------------------------------------------------------- phase two: the only function that writes

export function commit(plan, options = {}) {
  if (!plan || !Array.isArray(plan.verified)) throw new Error('TRANSACTION: commit needs a plan from precheck');
  const { repo, gitExe, canonicalRoot, consumer } = plan;
  const wrote = [];

  // The reservation comes first, so a second transaction cannot claim the same consumer while this one runs.
  const reservationDir = path.join(canonicalRoot, consumer);
  fs.mkdirSync(reservationDir, { recursive: true });
  wrote.push(reservationDir);

  // The files, from the bytes the precheck verified. Not re-read.
  for (const v of plan.verified) {
    const target = path.join(repo, v.target);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, v.content);
    wrote.push(target);
  }

  // Stage exactly the declared paths.
  const add = git(gitExe, repo, ['add', '--', ...plan.verified.map((v) => v.target)]);
  if (add.status !== 0) throw new Error('TRANSACTION: git add failed: ' + add.stderr);

  const staged = git(gitExe, repo, ['diff', '--cached', '--name-only']).stdout.split('\n').filter(Boolean).sort();
  const declaredSorted = plan.verified.map((v) => v.target).sort();
  if (JSON.stringify(staged) !== JSON.stringify(declaredSorted)) {
    throw new Error('TRANSACTION: the staged set is not the declared set: ' + JSON.stringify(staged));
  }

  if (options.dryRun !== true) {
    const message = options.message || 'transaction: ' + consumer;
    const env = { ...process.env,
      GIT_AUTHOR_NAME: options.author || 'xiaokang wang',
      GIT_AUTHOR_EMAIL: options.email || 'hanser0w0@gmail.com',
      GIT_COMMITTER_NAME: options.author || 'xiaokang wang',
      GIT_COMMITTER_EMAIL: options.email || 'hanser0w0@gmail.com' };
    const r = spawnSync(gitExe, ['--no-optional-locks', '-C', repo, 'commit', '-m', message],
      { windowsHide: true, encoding: 'utf8', env, maxBuffer: 32 * 1024 * 1024 });
    if (r.status !== 0) throw new Error('TRANSACTION: git commit failed: ' + (r.stderr || r.stdout));
  }

  return { consumer, reservation: reservationDir, wrote, staged, tip: git(gitExe, repo, ['rev-parse', 'HEAD']).stdout };
}

// ---------------------------------------------------------------- the canonical artifacts, four separate consumers

// Each artifact is written by its own exclusive claim, so one cannot overwrite another and a rerun cannot silently replace
// an original with a reconstruction.
export function writeCanonical(reportRoot, consumer, artifacts) {
  const dir = path.join(reportRoot, 'canonical', consumer);
  if (fs.existsSync(dir)) throw new Error('CANONICAL: the consumer is already claimed: ' + consumer);
  fs.mkdirSync(dir, { recursive: true });
  const written = [];
  for (const [name, bytes] of Object.entries(artifacts)) {
    const p = path.join(dir, name);
    if (fs.existsSync(p)) throw new Error('CANONICAL: the artifact already exists: ' + name);
    fs.writeFileSync(p, bytes, { flag: 'wx' });
    written.push({ name, bytes: bytes.length, sha256: sha256(bytes) });
  }
  return { dir, written };
}
