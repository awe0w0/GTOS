// Derive the remaining 147 tools from their 144 originals.
//
// Same rule as the first derivation: copy the code, change the packet identity, pin the result. These four are the tools the
// later phases call -- the selection that decides which of the 61 slots are reused and which are rebuilt, the host consumer,
// the role consumer, and the single-scene runner -- so they have to be the 144 code rather than a new implementation of it.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const SRC = HERE + '/../gtos-dsh-144';
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

const SUBS = [
  ['native-complete-guest-semantics-and-task-finality-20261028-a', 'native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a'],
  ['gtos-dsh-144-e759-finality-view', 'gtos-dsh-147-e759-scene-bound-view'],
  ['gtos-dsh-144-e759-finality', 'gtos-dsh-147-e759-scene-bound'],
  ['GTOS-DSH-144', 'GTOS-DSH-147'],
  ['gtos-dsh-144', 'gtos-dsh-147'],
  ['144', '147'],
];

const rows = [];
for (const name of ['p-selection-144.mjs', 'host-consume-144.mjs', 'roles-144.mjs', 'scene-144-01.mjs']) {
  const bytes = fs.readFileSync(SRC + '/' + name);
  let text = bytes.toString('utf8');
  for (const [from, to] of SUBS) text = text.split(from).join(to);
  const to = HERE + '/' + name.replace(/144/g, '147');
  if (fs.existsSync(to)) {
    const existing = fs.readFileSync(to);
    if (!existing.equals(Buffer.from(text))) throw new Error('refusing to overwrite ' + to);
    rows.push({ to: path.basename(to), bytes: existing.length, sha256: sha256(existing), unchanged: true });
    continue;
  }
  fs.writeFileSync(to, text, { encoding: 'utf8', flag: 'wx' });
  rows.push({ to: path.basename(to), bytes: Buffer.byteLength(text), sha256: sha256(Buffer.from(text)), unchanged: false });
}
console.log(JSON.stringify({ derived: rows.length, rows }, null, 2));
