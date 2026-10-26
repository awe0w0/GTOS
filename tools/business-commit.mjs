// GTOS-DSH-125 P0b: the business commit entry, wired to the atomic transaction.
//
// The 122 commit driver still called the old delivery.prepare/commit pair, so the new transaction module existed beside the
// business path instead of protecting it. This module is the entry the business path calls, and it does not have a second
// implementation: it delegates to the same two-phase transaction that has its own tests.
//
// What a refusal must leave behind, and what this file measures:
//
//   the worktree    no file created, changed or removed
//   the index       unchanged bytes and unchanged mtime
//   the tip         unchanged
//   the refs        unchanged
//   the canonical   nothing written
//   the reservation nothing left behind, because the reservation is only taken in phase two
//
// A refusal that leaves a reservation directory or a staged index behind is a partial commit that stopped early.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as tx from './atomic-transaction.mjs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

function git(exe, repo, args) {
  const r = spawnSync(exe, ['--no-optional-locks', '-C', repo, ...args],
    { windowsHide: true, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
  return { status: r.status, stdout: (r.stdout || '').trim(), stderr: (r.stderr || '').trim() };
}

// Everything the business path must not touch on a refusal. The index mtime is included because a stage-then-fail leaves
// the index rewritten even when its content happens to hash the same.
export function businessSnapshot(repo, gitExe, trackedPaths, canonicalRoot) {
  const files = {};
  for (const rel of trackedPaths) {
    const p = path.join(repo, rel);
    if (!fs.existsSync(p)) { files[rel] = null; continue; }
    const st = fs.statSync(p);
    files[rel] = { bytes: st.size, sha256: sha256(fs.readFileSync(p)), mtime_ms: st.mtimeMs };
  }
  const indexPath = path.join(repo, '.git', 'index');
  const index = fs.existsSync(indexPath)
    ? { bytes: fs.statSync(indexPath).size, sha256: sha256(fs.readFileSync(indexPath)),
      mtime_ms: fs.statSync(indexPath).mtimeMs }
    : null;
  const canonical = {};
  if (canonicalRoot && fs.existsSync(canonicalRoot)) {
    for (const name of fs.readdirSync(canonicalRoot).sort()) {
      const p = path.join(canonicalRoot, name);
      if (fs.statSync(p).isDirectory()) { canonical[name] = 'dir'; continue; }
      canonical[name] = sha256(fs.readFileSync(p));
    }
  }
  return { files, index, canonical,
    tip: git(gitExe, repo, ['rev-parse', 'HEAD']).stdout || null,
    refs: git(gitExe, repo, ['for-each-ref', '--format=%(refname) %(objectname)']).stdout || '',
    status: git(gitExe, repo, ['status', '--porcelain']).stdout || '' };
}

export function businessDiff(before, after) {
  const changes = [];
  for (const k of new Set([...Object.keys(before.files), ...Object.keys(after.files)])) {
    if (JSON.stringify(before.files[k]) !== JSON.stringify(after.files[k])) changes.push('file ' + k);
  }
  if (JSON.stringify(before.index) !== JSON.stringify(after.index)) changes.push('index');
  if (JSON.stringify(before.canonical) !== JSON.stringify(after.canonical)) changes.push('canonical');
  if (before.tip !== after.tip) changes.push('tip');
  if (before.refs !== after.refs) changes.push('refs');
  if (before.status !== after.status) changes.push('status');
  return changes;
}

// The business entry. It runs the real precheck, and only if that succeeds does it take the reservation, write the files
// and commit. On refusal it throws before any of those, so the caller has nothing to undo.
export function commitBusiness(declaration) {
  const { repo, gitExe, parent, consumer, canonicalRoot, paths, sources, trackedPaths, message, author, email } = declaration;
  const before = businessSnapshot(repo, gitExe, trackedPaths, canonicalRoot);

  let plan;
  try {
    plan = tx.precheck({ repo, gitExe, parent, consumer, canonicalRoot, paths, sources });
  } catch (error) {
    const after = businessSnapshot(repo, gitExe, trackedPaths, canonicalRoot);
    error.side_effects = businessDiff(before, after);
    error.side_effect_free = error.side_effects.length === 0;
    error.business_entry = true;
    throw error;
  }

  const result = tx.commit(plan, { message, author, email });
  const after = businessSnapshot(repo, gitExe, trackedPaths, canonicalRoot);
  return { ...result, before, after, changed: businessDiff(before, after) };
}
