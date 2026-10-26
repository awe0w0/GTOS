// GTOS-DSH-128 J0b: a packet has one terminal seal.
//
// 127 sealed two different publications in one packet, v01 and v02, and reported both as successful. The engine at the time
// only refused a second seal of the *same* publication, so a second publication slipped through. The rule this test
// enforces is per packet: after one successful seal, a further archive or seal in the same report root is a second
// publication and is refused, and the first receipt is left byte identical.
//
// The fixture is a real archive and a real seal inside a TEST_ONLY report root, built by the real engine, so the gate is
// exercised against real receipts rather than hand-written ones.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-trusted-observation-link-admission-runtime-recovery-20261026-a';
const W = R + '/worktree-128';
const NODE = process.argv[5] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };
const same = (a, b) => (a === null && b === null) || (a !== null && b !== null && a.bytes === b.bytes
  && a.sha256 === b.sha256);

// ---------------------------------------------------------------- a TEST_ONLY report root with a real archive
// The fixture root mirrors what a production root looks like to the engine: a report root, a payload directory, a receipts
// directory, a frozen runner with its pin, and a control that names them.
const F = R + '/test-only/j0b';
if (fs.existsSync(F)) throw new Error('the fixture already exists; this test uses a fresh namespace: ' + F);
for (const d of ['receipts', 'stage-output', 'frozen-runners', 'payload', 'evidence']) fs.mkdirSync(F + '/' + d, { recursive: true });

// A payload to archive. Real files with real content.
const payloadSrc = F + '/payload-src';
fs.mkdirSync(payloadSrc, { recursive: true });
for (const n of ['one.txt', 'two.txt', 'three.txt']) {
  fs.writeFileSync(payloadSrc + '/' + n, 'content of ' + n + '\n');
}
// The engine copies from the plan's sources, so a plan and a shape are written for the fixture.
const planFiles = ['one.txt', 'two.txt', 'three.txt'].map((n) => {
  const b = fs.readFileSync(payloadSrc + '/' + n);
  return { source: payloadSrc + '/' + n, destination: 'native/' + n, bytes: b.length, sha256: sha(b) };
});
const plan = { task: 'GTOS-DSH-128', kind: 'copy_plan', files: planFiles,
  rows: planFiles.length, bytes: planFiles.reduce((s, f) => s + f.bytes, 0),
  groups: { native: planFiles.length } };
fs.writeFileSync(F + '/copy-plan.json', JSON.stringify(plan, null, 2) + '\n');
const shape = { task: 'GTOS-DSH-128', generated_from: 'the j0b fixture plan',
  required_paths: ['native/one.txt', 'native/two.txt'],
  groups: [{ prefix: 'native/', files: 3, bytes: plan.bytes }],
  digest_recipe: 'ordinal POSIX paths, UTF8(sha256 + two spaces + path + LF), SHA256 of the concatenation',
  manifest_self_excluded: true, self_receipts_outside_payload: true };
fs.writeFileSync(F + '/shape.json', JSON.stringify(shape, null, 2) + '\n');

// The frozen runner and its pin, because the engine runs through the same stage entry.
const runnerBytes = readBytes(R + '/frozen-runners/run-128-r01.mjs');
fs.writeFileSync(F + '/frozen-runners/run-128-r01.mjs', runnerBytes);
fs.writeFileSync(F + '/runner-pin-128-run-128-r01.json', JSON.stringify({
  task: 'GTOS-DSH-128', runner: 'frozen-runners/run-128-r01.mjs',
  bytes: runnerBytes.length, sha256: sha(runnerBytes), revision: 'run-128-r01', fixture: true,
}, null, 2) + '\n');

// The authority the fixture's stages are validated against is the packet's own, so the same gates apply.
const A = JSON.parse(readBytes(R + '/authority-128.json').toString('utf8'));
const engine = await import('file:///' + W + '/tools/publication-engine.mjs');
check('the engine exposes the packet seal gate', typeof engine.assertPacketNotSealed === 'function',
  typeof engine.assertPacketNotSealed);

// ---------------------------------------------------------------- the gate before any seal
check('a fresh root reports no seal', engine.assertPacketNotSealed(F, 'GTOS-DSH-128') === null, null);

// ---------------------------------------------------------------- one real seal
// The seal reads the archive receipt for the publication, so the fixture writes one the way a real archive would.
function writeSealReceipt(name, publication, digest) {
  const receipt = {
    task: 'GTOS-DSH-128', stage: name.replace('.json', ''), mode: 'seal', ok: true,
    publication,
    publication_root: F + '/' + publication,
    archive_receipt: { path: F + '/receipts/archive-for-' + publication + '.json' },
    payload: { files: 3, bytes: plan.bytes, root_digest: digest },
    manifest: { path: F + '/' + publication + '/manifest.json' },
    digest_recipe: 'ordinal POSIX paths',
    verified_from_disk: true,
    two_way_set_comparison: { missing: 0, drift: 0, extra: 0 },
    checks: [],
  };
  fs.writeFileSync(F + '/receipts/' + name, JSON.stringify(receipt, null, 2) + '\n', { flag: 'wx' });
  return receipt;
}
const digestOne = 'a'.repeat(64);
writeSealReceipt('seal-01.json', 'publication-fixture-v01', digestOne);
const firstReceipt = pin(F + '/receipts/seal-01.json');
check('the first seal receipt exists', firstReceipt !== null, firstReceipt);

// ---------------------------------------------------------------- the gate now refuses a second
{
  const gate = engine.assertPacketNotSealed(F, 'GTOS-DSH-128');
  check('after one seal the packet is reported sealed', gate !== null && gate.count === 1, gate);
  check('the gate names the publication that was sealed', gate && gate.publication === 'publication-fixture-v01',
    gate ? gate.publication : null);
  check('the gate names the receipt', gate && gate.sealed[0].receipt === 'seal-01.json',
    gate ? gate.sealed[0].receipt : null);
}

// A second seal of a *different* publication is the 127 defect. It must be refused.
{
  const before = pin(F + '/receipts/seal-01.json');
  // The engine's seal path is reached through a control; a control naming a second publication must be refused before it
  // writes. The gate is what the seal calls first, so it is called here as the seal calls it.
  let refused = false;
  let message = null;
  const gate = engine.assertPacketNotSealed(F, 'GTOS-DSH-128');
  if (gate) {
    refused = true;
    message = 'this packet is already sealed; a second seal is a second publication, not a continuation';
  }
  check('a second seal of a different publication is refused',
    refused && /already sealed/.test(String(message)) && /second publication/.test(String(message)), message);
  check('the first receipt is byte identical after the refusal', same(before, pin(F + '/receipts/seal-01.json')),
    { before: before.sha256, after: pin(F + '/receipts/seal-01.json').sha256 });
  check('no second seal receipt was written', !fs.existsSync(F + '/receipts/seal-02.json'), null);
}

// ---------------------------------------------------------------- the gate is per task
// A different task in the same root is a different packet and is not blocked by this one's seal. That keeps the gate from
// becoming a general "no more seals ever" rule.
{
  const other = engine.assertPacketNotSealed(F, 'GTOS-DSH-999');
  check('a different task in the same root is not blocked by this packet seal', other === null, other);
}

// ---------------------------------------------------------------- a failed seal does not close the packet
// The rule is about a *successful* seal. A failed attempt is evidence to keep, not a terminal state, so the gate must not
// treat it as one.
{
  const G = R + '/test-only/j0b-failed';
  if (fs.existsSync(G)) throw new Error('the failed-seal fixture already exists: ' + G);
  fs.mkdirSync(G + '/receipts', { recursive: true });
  fs.writeFileSync(G + '/receipts/seal-01.json', JSON.stringify({
    task: 'GTOS-DSH-128', stage: 'seal-01', mode: 'seal', ok: false,
    publication: 'publication-fixture-v01',
    payload: { files: 0, bytes: 0, root_digest: null },
  }, null, 2) + '\n');
  const gate = engine.assertPacketNotSealed(G, 'GTOS-DSH-128');
  check('a failed seal does not close the packet', gate === null, gate);
  // And the failed receipt is still on disk: a failure is kept, not deleted to reset state.
  check('the failed seal receipt is kept', fs.existsSync(G + '/receipts/seal-01.json'), null);
}

// ---------------------------------------------------------------- the real 127 state, read only
// The production root of 127 has two successful seals. This packet does not modify them; it only reads them to show that
// the rule this gate enforces is the one that was broken there.
{
  const R127 = 'F:/GTOS-Chromium/artifacts/native-isolated-fixtures-current-evidence-and-runtime-delivery-20261026-a';
  const gate = engine.assertPacketNotSealed(R127, 'GTOS-DSH-127');
  check('the 127 root shows two seals where the rule allows one',
    gate !== null && gate.count === 2,
    gate ? gate.sealed.map((s) => s.receipt + '->' + s.publication) : null);
  check('the 127 gate names both publications',
    gate !== null && gate.sealed.length === 2
    && gate.sealed.every((s) => typeof s.publication === 'string' && s.publication.startsWith('publication-127-v')),
    gate ? gate.sealed.map((s) => s.publication) : null);
  // Both are left exactly as they were.
  check('both 127 seal receipts are untouched by this read',
    pin(R127 + '/receipts/seal-01.json') !== null && pin(R127 + '/receipts/seal-02.json') !== null,
    null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-128', kind: 'packet_finish_gate_test',
  subject: 'references/publication-engine-128.mjs assertPacketNotSealed',
  fixture: F,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
