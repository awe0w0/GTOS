// Derive the 147 driver and its reference modules from the 144 originals.
//
// 147 has to run the same guarded chain 144 ran -- the pinned runner, the frozen helper, the production bridge, the guard --
// and it must not re-implement any of it. The honest way to do that is to copy the originals and change only the packet
// identity: the task name, the report root, the two native roots, and the record-name suffix. The chain code itself is
// copied byte for byte, and the copies are pinned by hash in every control this packet writes, so a reviewer can see that
// the code that ran is the code 144 ran.
//
// This is a one-time derivation step. It writes only new files under tools/gtos-dsh-147/ and refuses to overwrite anything.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const TOOLS = path.resolve(HERE, '..').replace(/\\/g, '/');
const SRC = TOOLS + '/gtos-dsh-144';
const DST = TOOLS + '/gtos-dsh-147';

const ROOT_144 = 'native-complete-guest-semantics-and-task-finality-20261028-a';
const ROOT_147 = 'native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// The substitutions, applied in this order. Each is a whole-token change; none of them touches the chain logic.
const SUBS = [
  // the report root, before anything else, so its own `144` is not caught by the number rule
  [ROOT_144, ROOT_147],
  // the two native roots
  ['gtos-dsh-144-e759-finality-view', 'gtos-dsh-147-e759-scene-bound-view'],
  ['gtos-dsh-144-e759-finality', 'gtos-dsh-147-e759-scene-bound'],
  // the packet identity
  ['GTOS-DSH-144', 'GTOS-DSH-147'],
  ['gtos-dsh-144', 'gtos-dsh-147'],
  // every remaining name: record suffixes, stage tags, module names, the image name
  ['144', '147'],
];

const rewrite = (text) => {
  let out = text;
  for (const [from, to] of SUBS) out = out.split(from).join(to);
  return out;
};

const derive = (relative, { transform = rewrite } = {}) => {
  const from = SRC + '/' + relative;
  const to = DST + '/' + relative.replace(/144/g, '147');
  const bytes = fs.readFileSync(from);
  const out = transform(bytes.toString('utf8'));
  fs.mkdirSync(path.dirname(to), { recursive: true });
  if (fs.existsSync(to)) throw new Error('refusing to overwrite ' + to);
  fs.writeFileSync(to, out, { encoding: 'utf8', flag: 'wx' });
  return { from: from.replace(/^.*artifacts\//, '…/'), to: to.replace(/^.*worktree-147\//, ''), bytes: Buffer.byteLength(out), sha256: sha256(Buffer.from(out)) };
};

// The reference modules the bridge and the scene admission need. They are copied with the same substitution, because the
// bridge asserts the task name it was handed against its own constant, and a bridge that still said 144 would refuse every
// 147 stage -- which is the correct behaviour and would cost a full guarded round to discover.
const REFERENCES = [
  'references/host-path-144.mjs',
  'references/native-request-144.mjs',
  'references/prerequisite-consumption-144.mjs',
  'references/private-boot-checkpoints-144.mjs',
  'references/resource-semantics-144.mjs',
  'references/scene-admission-144.mjs',
];

const PYTHON = [
  'static-gate-144.py',
  'static-gate-144b.py',
  'static-judge-144.py',
  'staging-144-01.py',
  'relocate-144.py',
  'verify-runtime-144.py',
];

const MJS = [
  'run-144-r01.mjs',
  'native-144-01.mjs',
  'drive-144.mjs',
  'publish-144.mjs',
];

const rows = [];
for (const r of REFERENCES) rows.push(derive(r));
for (const r of PYTHON) rows.push(derive(r));
for (const r of MJS) rows.push(derive(r));

console.log(JSON.stringify({ derived: rows.length, rows }, null, 2));
