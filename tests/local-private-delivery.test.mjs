// GTOS-DSH-111 G0: consumer pre-reservation before any Git mutation, plus strict current-native joins.
//
// Two separate obligations live here.
//
//   1. The delivery transaction must reserve its canonical consumer BEFORE it creates a commit object or moves a
//      branch tip. 110 discovered the old shape's flaw the hard way: the commit was created, then the artifact write
//      collided with the previous commit's consumer directory, so the transaction ended "commit created but delivery
//      incomplete". A pre-reservation makes that outcome impossible for a controlled writer.
//
//   2. The engine's native certification must join every current child's evidence by identity, not by presence. A
//      fixture with a ledger, an inner report and a raw but no spec and no same-name attempt must be refused.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const R = 'F:/GTOS-Chromium/artifacts/native-ap-handshake-paging-checkpoints-and-atomic-delivery-20261025-a';
const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
if (control.task !== 'GTOS-DSH-111') throw new Error('control is for ' + control.task);
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };

const out = [];
function check(name, ok, detail) {
  out.push({ name, ok: Boolean(ok), detail: detail === undefined ? null : detail });
}

// ================================================================ 1. consumer reservation
const delivery = await import('file:///' + R + '/references/local-private-delivery-111.mjs');

// The real module must expose a reservation step, and it must be callable before any mutation.
check('the delivery module exposes a consumer reservation', typeof delivery.reserveConsumer === 'function',
  Object.keys(delivery).filter((k) => /reserv|consumer/i.test(k)));

if (typeof delivery.reserveConsumer === 'function') {
  const fix = R + '/fixtures/g0-reservation';
  if (fs.existsSync(fix)) throw new Error('fixture exists');
  fs.mkdirSync(fix, { recursive: true });

  // A fresh consumer reserves cleanly and records its intent.
  const first = delivery.reserveConsumer(fix, 'git-commit', 'g0test-01');
  check('a fresh consumer reserves', first && first.reserved === true, first);
  check('the reservation records its consumer', first && first.consumer === 'git-commit', first && first.consumer);
  check('the reservation records the stage that owns it', first && first.stage === 'g0test-01', first && first.stage);
  check('the reservation names its canonical directory', typeof (first && first.directory) === 'string', first && first.directory);
  check('the reservation is on disk', first && fs.existsSync(first.record), first && first.record);

  // The same consumer cannot be reserved twice: that is the 110 failure, caught before any mutation.
  let secondRefused = false, secondError = null;
  try { delivery.reserveConsumer(fix, 'git-commit', 'g0test-02'); }
  catch (e) { secondRefused = true; secondError = String(e.message || e); }
  check('a second reservation of the same consumer refuses', secondRefused, secondError);
  check('the refusal names the owning stage', secondError !== null && /g0test-01/.test(secondError), secondError);

  // A different consumer is independent, which is what a second commit needs.
  const other = delivery.reserveConsumer(fix, 'git-commit-2', 'g0test-03');
  check('a different consumer reserves independently', other && other.reserved === true, other && other.consumer);
  check('the two consumers have distinct directories',
    first && other && first.directory !== other.directory, first && other && [first.directory, other.directory]);

  // A malformed consumer name refuses, so a reservation cannot escape into a path.
  let badRefused = false;
  try { delivery.reserveConsumer(fix, '../escape', 'g0test-04'); } catch { badRefused = true; }
  check('a path-like consumer name refuses', badRefused, true);

  // Releasing a reservation is explicit and lets a later stage reserve the same consumer again.
  if (typeof delivery.releaseConsumer === 'function') {
    const released = delivery.releaseConsumer(fix, 'git-commit-2', 'g0test-03');
    check('a reservation can be released by its owner', released === true, released);
    let reRefused = false;
    try { delivery.releaseConsumer(fix, 'git-commit-2', 'g0test-wrong'); } catch { reRefused = true; }
    check('a foreign stage cannot release another stage reservation', reRefused, true);
  }
}

// ================================================================ 2. strict native joins
const engine = await import('file:///' + R + '/references/publication-engine-111.mjs');
check('the engine exposes a native certification entry point',
  typeof engine.certifyNativeRoot === 'function',
  Object.keys(engine).filter((k) => /native|certif/i.test(k)));

if (typeof engine.certifyNativeRoot === 'function') {
  const build = (name, shape) => {
    const root = R + '/fixtures/g0-native/' + name;
    if (fs.existsSync(root)) throw new Error('fixture exists: ' + name);
    fs.mkdirSync(root + '/logs', { recursive: true });
    shape(root);
    return root;
  };

  // A complete child: ledger, spec, same-name attempt, terminal, inner and raw all present and agreeing.
  const complete = build('complete', (root) => {
    const runnerPath = R + '/frozen-runners/run-111-r02.mjs';
    const runnerPin = pin(runnerPath);
    const helperPath = R + '/frozen-helpers/g0fake-01.mjs';
    fs.writeFileSync(helperPath, 'export const x = 1;\n', { encoding: 'utf8' });
    const helperPin = pin(helperPath);
    const argv = ['/usr/bin/python3.8', '-B', '-S', '/var/tmp/x/helper.py', '/var/tmp/x/control.json'];
    const raw = 'silent success\n';
    fs.writeFileSync(root + '/logs/child-output-g0ok-01.raw', raw, { encoding: 'utf8' });
    fs.writeFileSync(root + '/logs/spec-g0ok-01.json', JSON.stringify({
      stage: 'g0ok-01', category: 'native_metadata', operation: 'qemu.metadata',
      argv, exec_set: ['/usr/bin/python3.8'], work_root: '/var/tmp/x' }, null, 2) + '\n', { encoding: 'utf8' });
    fs.writeFileSync(root + '/logs/inner-report-g0ok-01.json', JSON.stringify({
      child: { returncode: 0, timed_out: false, output_path: '/var/tmp/x/logs/child-output-g0ok-01.raw',
        output_file_bytes: Buffer.byteLength(raw), output_sha256: sha(Buffer.from(raw)) } }, null, 2) + '\n',
      { encoding: 'utf8' });
    fs.writeFileSync(root + '/logs/runs-111.jsonl', JSON.stringify({
      stage: 'g0ok-01', kind: 'result', exit_code: 0, argv, refused: false, timed_out: false,
      landlock_ok: true, probe_checks: { ok: true } }) + '\n', { encoding: 'utf8' });
    fs.writeFileSync(R + '/attempt-g0ok-01-111.json', JSON.stringify({
      task: 'GTOS-DSH-111', attempt: 'g0ok-01', complete: true, executor: 'guard',
      category: 'native_metadata', operation_id: 'qemu.metadata', operation_mode: 'native-metadata',
      runner_path: runnerPath, runner_bytes: runnerPin.bytes, runner_sha256: runnerPin.sha256,
      frozen: { frozen_path: helperPath, frozen_bytes: helperPin.bytes, frozen_sha256: helperPin.sha256 },
      declared_outputs: [R + '/results/g0ok-01.json'] }, null, 2) + '\n', { encoding: 'utf8' });
    fs.writeFileSync(R + '/result-g0ok-01-111.json', JSON.stringify({
      task: 'GTOS-DSH-111', attempt: 'g0ok-01', category: 'native_metadata',
      operation_id: 'qemu.metadata', operation_mode: 'native-metadata',
      transport_exit_code: 0, timed_out: false, ok: true }, null, 2) + '\n', { encoding: 'utf8' });
  });
  const okResult = engine.certifyNativeRoot({
    task: 'GTOS-DSH-111', name: 'complete',
    host_root: complete, report_root: R, ledger: 'runs-111.jsonl',
  });
  check('a complete child certifies', okResult && okResult.certified === true,
    okResult && okResult.reasons);

  // Each missing piece must be refused, one at a time, with the piece named.
  const missing = [
    ['no-spec', (root) => { /* spec is simply absent */ }],
    ['no-attempt', (root) => { /* attempt is absent for this stage */ }],
    ['no-terminal', (root) => { /* terminal result is absent */ }],
    ['no-inner', (root) => { /* inner report is absent */ }],
    ['no-raw', (root) => { /* raw capture is absent */ }],
    ['no-ledger', (root) => { /* ledger is absent */ }],
  ];
  for (const [name] of missing) {
    // Build a fixture that is complete except for the named piece.
    const root = R + '/fixtures/g0-native/' + name;
    if (fs.existsSync(root)) throw new Error('fixture exists: ' + name);
    fs.mkdirSync(root + '/logs', { recursive: true });
    const stage = 'g0' + name.replace(/[^a-z]/g, '').slice(0, 4) + '-01';
    const runnerPath = R + '/frozen-runners/run-111-r02.mjs';
    const runnerPin = pin(runnerPath);
    const argv = ['/usr/bin/python3.8', '-B', '-S', '/var/tmp/x/helper.py', '/var/tmp/x/control.json'];
    const raw = 'output\n';
    if (name !== 'no-raw') fs.writeFileSync(root + '/logs/child-output-' + stage + '.raw', raw, { encoding: 'utf8' });
    if (name !== 'no-spec') {
      fs.writeFileSync(root + '/logs/spec-' + stage + '.json', JSON.stringify({
        stage, category: 'native_metadata', operation: 'qemu.metadata', argv,
        exec_set: ['/usr/bin/python3.8'], work_root: '/var/tmp/x' }, null, 2) + '\n', { encoding: 'utf8' });
    }
    if (name !== 'no-inner') {
      fs.writeFileSync(root + '/logs/inner-report-' + stage + '.json', JSON.stringify({
        child: { returncode: 0, timed_out: false, output_file_bytes: Buffer.byteLength(raw),
          output_sha256: sha(Buffer.from(raw)) } }, null, 2) + '\n', { encoding: 'utf8' });
    }
    if (name !== 'no-ledger') {
      fs.writeFileSync(root + '/logs/runs-111.jsonl', JSON.stringify({
        stage, kind: 'result', exit_code: 0, argv, refused: false, timed_out: false,
        landlock_ok: true }) + '\n', { encoding: 'utf8' });
    }
    if (name !== 'no-attempt') {
      fs.writeFileSync(R + '/attempt-' + stage + '-111.json', JSON.stringify({
        task: 'GTOS-DSH-111', attempt: stage, complete: true, executor: 'guard',
        category: 'native_metadata', operation_id: 'qemu.metadata', operation_mode: 'native-metadata',
        runner_path: runnerPath, runner_bytes: runnerPin.bytes, runner_sha256: runnerPin.sha256 },
        null, 2) + '\n', { encoding: 'utf8' });
    }
    if (name !== 'no-terminal') {
      fs.writeFileSync(R + '/result-' + stage + '-111.json', JSON.stringify({
        task: 'GTOS-DSH-111', attempt: stage, transport_exit_code: 0, ok: true }, null, 2) + '\n',
        { encoding: 'utf8' });
    }
    let refused = false, reason = null;
    try {
      const r = engine.certifyNativeRoot({ task: 'GTOS-DSH-111', name, host_root: root,
        report_root: R, ledger: 'runs-111.jsonl' });
      if (!r.certified) { refused = true; reason = (r.reasons || []).join('; '); }
    } catch (e) { refused = true; reason = String(e.message || e); }
    check('a child with ' + name.replace('no-', 'no ') + ' is refused', refused, reason);
  }
}

const summary = { task: 'GTOS-DSH-111', kind: 'g0_transaction_and_native_joins',
  checks: out.length, failed: out.filter((c) => !c.ok).length, results: out,
  ok: out.every((c) => c.ok) };
const p = control.result_path;
if (fs.existsSync(p)) throw new Error('result exists: ' + p);
fs.writeFileSync(p, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: summary.ok, checks: summary.checks, failed: summary.failed }, null, 2));
for (const c of out.filter((x) => !x.ok)) console.log('  FAIL ' + c.name + ' :: ' + JSON.stringify(c.detail));
if (!summary.ok) process.exitCode = 1;
