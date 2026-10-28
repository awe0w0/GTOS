// Install the frozen artefacts of the 147 chain.
//
// The driver runs the frozen runner and the frozen reference modules, and asserts that each frozen copy is byte-identical to
// the copy committed beside the driver. That is what makes "the chain 147 ran is the chain 144 ran" checkable rather than
// asserted: the committed copies are derived from 144's committed copies by the identity substitution only, and the frozen
// copies are byte-identical to those.
//
// Everything this writes is CreateNew. A second run refuses rather than overwriting a frozen artefact, because a frozen
// artefact whose bytes changed under a pin is the failure the pin exists to catch.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const WORKTREE = path.resolve(HERE, '..', '..').replace(/\\/g, '/');
const R = path.resolve(WORKTREE, '..').replace(/\\/g, '/');
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

const install = (from, to) => {
  const bytes = fs.readFileSync(from);
  fs.mkdirSync(path.dirname(to), { recursive: true });
  if (fs.existsSync(to)) {
    const existing = fs.readFileSync(to);
    if (existing.equals(bytes)) return { to: to.replace(R + '/', ''), bytes: bytes.length, sha256: sha256(bytes), unchanged: true };
    throw new Error('refusing to overwrite a frozen artefact: ' + to);
  }
  fs.writeFileSync(to, bytes, { flag: 'wx' });
  return { to: to.replace(R + '/', ''), bytes: bytes.length, sha256: sha256(bytes), unchanged: false };
};

const rows = [];
for (const rel of ['references/host-path-147.mjs', 'references/native-request-147.mjs',
  'references/prerequisite-consumption-147.mjs', 'references/private-boot-checkpoints-147.mjs',
  'references/resource-semantics-147.mjs', 'references/scene-admission-147.mjs']) {
  rows.push(install(HERE + '/' + rel, R + '/' + rel));
}
rows.push(install(HERE + '/staging-147-01.py', R + '/staging-147-01.py'));
rows.push(install(HERE + '/native-147-01.mjs', R + '/native-147-01.mjs'));
// The frozen runner lives under frozen-runners/ with the shared path module beside it, because a frozen helper may import
// exactly one relative module and the runner freezes that module next to the helper it freezes.
rows.push(install(HERE + '/run-147-r01.mjs', R + '/frozen-runners/run-147-r01.mjs'));
rows.push(install(HERE + '/references/host-path-147.mjs', R + '/frozen-runners/host-path-147.mjs'));

// The runner pin. Its name is derived from the task and the runner revision, and it lives beside the frozen root rather than
// inside it, because that is where assertRunnerPin looks: the owner of the runner is the frozen-runners directory and the pin is
// its parent's child. A pin written one directory lower would be invisible to the gate that needs it.
const runnerPath = R + '/frozen-runners/run-147-r01.mjs';
const runnerBytes = fs.readFileSync(runnerPath);
const pinPath = R + '/runner-pin-147-run-147-r01.json';
const pin = {
  task: 'GTOS-DSH-147',
  runner: 'run-147-r01',
  path: runnerPath,
  bytes: runnerBytes.length,
  sha256: sha256(runnerBytes),
  derived_from: {
    task: 'GTOS-DSH-144',
    path: 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/frozen-runners/run-144-r01.mjs',
    rule: 'the packet identity only: GTOS-DSH-144 -> GTOS-DSH-147, the report root, the two native roots, and the 144 -> 147 '
      + 'record suffix. The lifecycle, the gates and the freeze are unchanged.',
  },
};
if (fs.existsSync(pinPath)) {
  const existing = fs.readFileSync(pinPath, 'utf8');
  const wanted = JSON.stringify(pin, null, 2) + '\n';
  if (existing !== wanted) throw new Error('the runner pin already exists with different content');
  rows.push({ to: pinPath.replace(R + '/', ''), bytes: existing.length, sha256: sha256(Buffer.from(existing)), unchanged: true });
} else {
  const text = JSON.stringify(pin, null, 2) + '\n';
  fs.writeFileSync(pinPath, text, { flag: 'wx' });
  rows.push({ to: pinPath.replace(R + '/', ''), bytes: Buffer.byteLength(text), sha256: sha256(Buffer.from(text)), unchanged: false });
}

console.log(JSON.stringify({ installed: rows.length, rows }, null, 2));
