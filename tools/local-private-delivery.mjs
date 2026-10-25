// GTOS-DSH-109 scoped local Git transaction.
//
// prepare: verify the parent, the six existing paths against their parent pins, create the worktree and stage the nine
//          files. commit: re-verify, commit, and write the four canonical commit artifacts through the canonical writer,
//          so a later step cannot overwrite them.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const need = (ok, text, detail) => { if (!ok) throw new Error('DELIVERY: ' + text + (detail === undefined ? '' : ' :: ' + JSON.stringify(detail))); };

export function gitRun(policy, cwd, args) {
  const argv = ['--no-optional-locks', '--no-pager', '-c', 'core.autocrlf=false',
    '-c', 'core.longpaths=true', '-c', 'core.hooksPath=' + policy.empty_hooks, ...args];
  const r = spawnSync(policy.git, argv, { cwd, windowsHide: true, timeout: 120000,
    maxBuffer: 64 * 1024 * 1024,
    env: { ...process.env, GIT_OPTIONAL_LOCKS: '0', GIT_TERMINAL_PROMPT: '0' } });
  return { status: r.status, stdout: r.stdout, stderr: r.stderr };
}
function gitText(policy, cwd, args) {
  const r = gitRun(policy, cwd, args);
  need(r.status === 0, 'git ' + args[0] + ' failed', r.stderr.toString().slice(0, 400));
  return r.stdout.toString('utf8').trim();
}

export function prepare(policy) {
  const { repository, worktree, branch, base } = policy;
  const head = gitText(policy, repository, ['rev-parse', 'HEAD']);
  need(head === policy.primary_base, 'primary HEAD is the packet base', { head, want: policy.primary_base });
  need(gitText(policy, repository, ['rev-parse', '--abbrev-ref', 'HEAD']) === 'dev', 'primary branch is dev');
  need(!fs.existsSync(worktree), 'the worktree does not exist yet', worktree);

  // The six existing paths must equal their parent pin BEFORE the rewrite is permitted.
  for (const file of policy.files) {
    if (!file.parent_pin) continue;
    const r = gitRun(policy, repository, ['show', base + ':' + file.logical]);
    need(r.status === 0, 'parent blob readable: ' + file.logical);
    const b = r.stdout;
    need(b.length === file.parent_pin.bytes && sha(b) === file.parent_pin.sha256,
      'existing path equals its approved parent pin', { logical: file.logical,
        now: { bytes: b.length, sha256: sha(b) }, pinned: file.parent_pin });
  }
  // The three new paths must NOT exist in the parent.
  for (const file of policy.files) {
    if (file.parent_pin) continue;
    const r = gitRun(policy, repository, ['cat-file', '-e', base + ':' + file.logical]);
    need(r.status !== 0, 'new path is absent from the parent', file.logical);
  }

  gitText(policy, repository, ['worktree', 'add', '-b', branch, worktree, base]);
  need(fs.existsSync(worktree), 'the worktree exists after add');
  need(gitText(policy, worktree, ['rev-parse', 'HEAD']) === base, 'the worktree starts at the parent');

  const staged = [];
  for (const file of policy.files) {
    const target = path.join(worktree, ...file.logical.split('/'));
    fs.mkdirSync(path.dirname(target), { recursive: true });
    const bytes = fs.readFileSync(file.source);
    need(bytes.length === file.bytes && sha(bytes) === file.sha256, 'source equals the tested pin', file.logical);
    fs.writeFileSync(target, bytes);
    staged.push({ logical: file.logical, bytes: bytes.length, sha256: sha(bytes) });
  }
  gitText(policy, worktree, ['add', '--', ...policy.files.map((f) => f.logical)]);
  const stagedNames = gitText(policy, worktree, ['diff', '--cached', '--name-only']).split('\n').filter(Boolean).sort();
  need(JSON.stringify(stagedNames) === JSON.stringify(policy.files.map((f) => f.logical).sort()),
    'exactly the declared paths are staged', stagedNames);

  return { mode: 'prepare', ok: true, repository, worktree, branch, base,
    primary_head_before: head, staged, staged_count: staged.length,
    parent_pins_verified: policy.files.filter((f) => f.parent_pin).length,
    new_paths_verified_absent: policy.files.filter((f) => !f.parent_pin).length,
    commit: null };
}

export function commit(policy, canonical) {
  const { worktree, branch, base, message, author } = policy;
  need(fs.existsSync(worktree), 'the worktree exists');
  need(gitText(policy, worktree, ['rev-parse', 'HEAD']) === base, 'the worktree is still at the parent');
  need(gitText(policy, worktree, ['symbolic-ref', '--short', 'HEAD']) === branch, 'the branch is unchanged');

  // The staged blobs must equal the tested files before the commit is created.
  for (const file of policy.files) {
    const r = gitRun(policy, worktree, ['show', ':' + file.logical]);
    need(r.status === 0 && r.stdout.length === file.bytes && sha(r.stdout) === file.sha256,
      'staged blob equals the tested file', file.logical);
  }
  const unstaged = gitText(policy, worktree, ['diff', '--name-only']).split('\n').filter(Boolean);
  need(unstaged.length === 0, 'no unstaged modification in the isolated worktree', unstaged);
  const untracked = gitText(policy, worktree, ['status', '--porcelain=v1', '--untracked-files=all'])
    .split('\n').filter((line) => line.startsWith('??'));
  need(untracked.length === 0, 'no untracked file in the isolated worktree', untracked);

  const tree = gitText(policy, worktree, ['write-tree']);
  const commitOut = gitRun(policy, worktree, ['-c', 'user.name=' + author.name,
    '-c', 'user.email=' + author.email, 'commit', '--no-verify', '-m', message]);
  need(commitOut.status === 0, 'the commit was created', commitOut.stderr.toString().slice(0, 400));
  const oid = gitText(policy, worktree, ['rev-parse', 'HEAD']);
  const parent = gitText(policy, worktree, ['rev-parse', 'HEAD^']);

  const changed = gitText(policy, worktree, ['diff-tree', '--no-commit-id', '--name-only', '-r', oid])
    .split('\n').filter(Boolean).sort();
  need(JSON.stringify(changed) === JSON.stringify(policy.files.map((f) => f.logical).sort()),
    'the commit changed exactly the declared paths', changed);

  const blobs = [];
  for (const file of policy.files) {
    const b = gitRun(policy, worktree, ['show', oid + ':' + file.logical]).stdout;
    need(b.length === file.bytes && sha(b) === file.sha256, 'committed blob equals the tested file', file.logical);
    blobs.push({ logical: file.logical, bytes: b.length, sha256: sha(b) });
  }

  // The four commit artifacts go through the canonical writer, so they cannot be silently replaced later.
  const artifactDir = path.join(worktree, '..', 'commit-artifacts-109');
  fs.mkdirSync(artifactDir, { recursive: true });
  const showPatch = gitRun(policy, worktree, ['show', '--format=fuller', '--binary', '--no-ext-diff',
    '--no-renames', oid, '--', ...policy.files.map((f) => f.logical)]).stdout;
  const formatPatch = gitRun(policy, worktree, ['format-patch', '-1', '--stdout', '--no-signature',
    '--no-stat', oid]).stdout;

  const written = [];
  written.push(canonical.createCanonical(policy.report_root, 'git-commit',
    'commit.json', Buffer.from(JSON.stringify({ task: policy.task, oid, parent, tree, branch,
      author: author.name + ' <' + author.email + '>', message, blobs }, null, 2) + '\n')));
  written.push(canonical.createCanonical(policy.report_root, 'git-commit',
    'show.patch', showPatch));
  written.push(canonical.createCanonical(policy.report_root, 'git-commit',
    'format-patch.patch', formatPatch));
  written.push(canonical.createCanonical(policy.report_root, 'git-commit',
    'verification.json', Buffer.from(JSON.stringify({ task: policy.task, ok: true,
      commit: { oid, parent, tree, blobs }, blobs_checked: blobs.length,
      changed_paths: changed.length, five_way: true }, null, 2) + '\n')));

  return { mode: 'commit', ok: true, oid, parent, tree, branch, author,
    message, blobs, changed_paths: changed.length, artifacts: written,
    five_way_identity: true };
}

// The runner freezes this module and executes it with a control path. The entry below is the only way it is driven:
// it reads the control, calls the exported transaction, and writes a receipt exclusively.
const isMain = process.argv[1]
  && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
if (isMain) {
  (async () => {
    const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
    const policy = JSON.parse(fs.readFileSync(control.policy.path, 'utf8'));
    if (policy.task !== control.task) throw new Error('DELIVERY: policy task mismatch');
    const drifted = policy.files.filter((f) => {
      if (!fs.existsSync(f.source)) return true;
      const bytes = fs.readFileSync(f.source);
      return bytes.length !== f.bytes || sha(bytes) !== f.sha256;
    });
    if (drifted.length > 0) {
      throw new Error('DELIVERY: a policy source drifted before the transaction: '
        + drifted.map((f) => f.logical).join(', '));
    }
    let receipt;
    if (control.mode === 'prepare') {
      receipt = prepare(policy);
    } else if (control.mode === 'commit') {
      const canonical = await import(new URL('file:///'
        + control.canonical_module.replace(/\\/g, '/')).href);
      receipt = commit(policy, canonical);
    } else {
      throw new Error('DELIVERY: unknown mode ' + control.mode);
    }
    receipt.task = control.task;
    receipt.policy_sha256 = control.policy.sha256;
    fs.mkdirSync(path.dirname(control.receipt_path), { recursive: true });
    if (fs.existsSync(control.receipt_path)) throw new Error('DELIVERY: receipt exists');
    fs.writeFileSync(control.receipt_path, JSON.stringify(receipt, null, 2) + '\n',
      { encoding: 'utf8', flag: 'wx' });
    console.log(JSON.stringify({ ok: true, mode: control.mode,
      receipt: control.receipt_path, commit: receipt.oid || null }));
  })().catch((error) => {
    const text = String(error && error.stack ? error.stack : error);
    try {
      if (!fs.existsSync(process.argv[2])) throw new Error('no control');
      const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
      if (control.receipt_path && !fs.existsSync(control.receipt_path)) {
        fs.mkdirSync(path.dirname(control.receipt_path), { recursive: true });
        fs.writeFileSync(control.receipt_path,
          JSON.stringify({ ok: false, error: text }, null, 2) + '\n',
          { encoding: 'utf8', flag: 'wx' });
      }
    } catch { /* the receipt is best effort; the non-zero exit carries the failure */ }
    console.error(text);
    process.exitCode = 1;
  });
}
