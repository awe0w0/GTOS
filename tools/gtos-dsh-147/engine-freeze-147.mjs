// Freeze the publication engine.
//
// The driver refuses to run any phase until the engine it would publish with is frozen and the frozen copy is byte-identical to
// the engine on disk. That ordering is the point: a record written after the run that published would be a claim rather than a
// freeze, so the first freeze has to exist before anything runs, and a later engine change adds a revision instead of editing
// this one.
//
// The record names `publish-147.mjs`, which is the engine 144 published with, copied with the packet identity substituted. Its
// bytes are what the record pins, and the pinned copy lives under frozen-engine/ where nothing in the packet writes.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const WORKTREE = path.resolve(HERE, '..', '..').replace(/\\/g, '/');
const R = path.resolve(WORKTREE, '..').replace(/\\/g, '/');
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

const revision = process.argv[2] || 'r01';
if (!/^r[0-9]{2}$/.test(revision)) throw new Error('bad revision: ' + revision);

const enginePath = HERE + '/publish-147.mjs';
const bytes = fs.readFileSync(enginePath);
const copyPath = R + '/frozen-engine/publish-147-' + revision + '.mjs';
fs.mkdirSync(path.dirname(copyPath), { recursive: true });
if (fs.existsSync(copyPath)) {
  const existing = fs.readFileSync(copyPath);
  if (!existing.equals(bytes)) throw new Error('a frozen engine copy already exists with other bytes: ' + copyPath);
} else {
  fs.writeFileSync(copyPath, bytes, { flag: 'wx' });
}

const previous = fs.readdirSync(R).filter((f) => /^engine-freeze-147-r[0-9]+\.json$/.test(f)).sort();
const recordPath = R + '/engine-freeze-147-' + revision + '.json';
const record = {
  task: 'GTOS-DSH-147',
  revision,
  what: 'the publication engine, frozen before the run that publishes',
  frozen: { path: enginePath, bytes: bytes.length, sha256: sha256(bytes) },
  frozen_copy: { path: copyPath, bytes: bytes.length, sha256: sha256(bytes), identical: true },
  supersedes: previous.length ? previous[previous.length - 1] : null,
  why: previous.length
    ? 'the engine changed after ' + previous[previous.length - 1] + ', so this revision records the new bytes rather than editing that record'
    : 'the first freeze of the 147 engine; there is nothing before it to supersede',
  derived_from: {
    task: 'GTOS-DSH-144',
    path: 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/worktree-144/tools/gtos-dsh-144/publish-144.mjs',
    rule: 'the packet identity only, as with the runner',
  },
  rule: 'a production publication runs under the newest freeze record; a later engine change adds another revision rather than '
    + 'editing this one, and the revisions that ran before keep the rehearsals that ran under them',
  ok: true,
};
const text = JSON.stringify(record, null, 2) + '\n';
if (fs.existsSync(recordPath)) {
  if (fs.readFileSync(recordPath, 'utf8') !== text) throw new Error('the freeze record already exists with other content');
} else {
  fs.writeFileSync(recordPath, text, { flag: 'wx' });
}
console.log(JSON.stringify({ wrote: recordPath, frozen: record.frozen, copy: copyPath }, null, 2));
