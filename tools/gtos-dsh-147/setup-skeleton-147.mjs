// Create the directory skeleton both native roots need.
//
// The guard writes its own spec and run log under `<work root>/logs`, and it runs with HOME, TMPDIR and XDG_CACHE_HOME pointed
// inside the same root. A root that exists but has none of those directories is not usable: the guard fails while opening its
// own spec, before it has run anything, and the failure looks like a bad stage rather than a missing directory.
//
// So the skeleton is created once, before the first guarded stage, and recorded. It is created by Node file work against the
// UNC form of each root -- never by a WSL command -- and each directory is created only if absent, so a second run reports the
// skeleton as already present rather than touching anything.

import fs from 'node:fs';
import crypto from 'node:crypto';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const HERE = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const L = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-147-e759-scene-bound';
const S = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-147-e759-scene-bound-view';

// The two roots have different jobs, so they get different skeletons. L is where guarded work runs: its logs carry the guard's
// own spec and run rows, and its home/tmp/cache are the sandbox's writable environment. S is the source view the compiler reads:
// it holds the staged waves, the materialised runtime, the image, and the helper inputs each stage is handed.
const PLAN = [
  { root: L, linux: '/var/tmp/gtos-dsh-147-e759-scene-bound', dirs: ['logs', 'home', 'tmp', 'cache', 'shm', 'evidence', 'inputs', 'frozen-helpers', 'test-only'] },
  { root: S, linux: '/var/tmp/gtos-dsh-147-e759-scene-bound-view', dirs: ['logs', 'home', 'tmp', 'cache', 'shm', 'evidence', 'inputs', 'frozen-helpers', 'image', 'producers', 'runtime', 'staging', 'test-build', 'test-only', 'unit-run'] },
];

const rows = [];
for (const p of PLAN) {
  if (!fs.existsSync(p.root)) throw new Error('the native root is absent, so this is not the packet that owns it: ' + p.root);
  for (const d of p.dirs) {
    const full = p.root + '/' + d;
    const existed = fs.existsSync(full);
    if (!existed) fs.mkdirSync(full, { recursive: false });
    const st = fs.statSync(full);
    if (!st.isDirectory()) throw new Error('not a directory: ' + full);
    rows.push({ root: p.linux, dir: d, existed, mode: '0' + (st.mode & 0o777).toString(8) });
  }
}

const record = {
  task: 'GTOS-DSH-147',
  what: 'the directory skeleton both native roots need before the first guarded stage',
  why: 'the guard writes <work root>/logs/spec-<stage>.json and runs with HOME, TMPDIR and XDG_CACHE_HOME inside its work root; '
    + 'a root without those directories fails before the guarded command starts, which would look like a bad stage',
  created_by: 'node fs.mkdirSync against the UNC form of each root, never by a WSL command',
  roots: PLAN.map((p) => ({ linux: p.linux, host: p.root, dirs: p.dirs.length })),
  rows,
  created: rows.filter((r) => !r.existed).length,
  already_present: rows.filter((r) => r.existed).length,
  ok: true,
};
const text = JSON.stringify(record, null, 2) + '\n';
const out = HERE + '/setup-147-02.json';
if (fs.existsSync(out)) {
  if (fs.readFileSync(out, 'utf8') !== text) throw new Error('setup-147-02.json already exists with other content');
} else {
  fs.writeFileSync(out, text, { flag: 'wx' });
}
console.log(JSON.stringify({ wrote: out, created: record.created, already_present: record.already_present,
  bytes: Buffer.byteLength(text), sha256: sha256(Buffer.from(text)) }, null, 2));
