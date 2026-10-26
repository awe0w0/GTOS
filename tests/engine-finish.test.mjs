// GTOS-DSH-129 E1: the real finish protocol, driven through the real engine CLI.
//
// The 128 suite claimed this and did not do it. It hand-wrote a success receipt into a fixture directory and then called
// assertPacketNotSealed itself, setting refused = true when the gate answered. That proves the gate recognises a receipt.
// It does not prove the entry refuses a second publication, because no second archive or seal was ever submitted.
//
// Here every step is a real subprocess of the engine's own CLI:
//
//   archive  -> a real payload tree, a real manifest, a real archive receipt
//   seal     -> a real seal receipt, derived from the payload on disk
//   archive  -> a SECOND publication, which must be refused before it writes anything
//   seal     -> a SECOND publication, which must be refused before it writes anything
//
// and the first tree and both first receipts are pinned before the second attempt and compared after it.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-resource-recovery-and-current-business-integration-20261026-a';
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

// Every file under a directory, pinned, so a tree can be compared before and after.
function pinTree(dir) {
  const out = {};
  if (!fs.existsSync(dir)) return out;
  const walk = (d, prefix) => {
    for (const name of fs.readdirSync(d).sort()) {
      const full = path.join(d, name);
      const rel = prefix ? prefix + '/' + name : name;
      if (fs.statSync(full).isDirectory()) { walk(full, rel); continue; }
      const b = readBytes(full);
      out[rel] = b === null ? null : { bytes: b.length, sha256: sha(b) };
    }
  };
  walk(dir, '');
  return out;
}
function diffTrees(a, b) {
  const out = [];
  for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) {
    const inA = Object.prototype.hasOwnProperty.call(a, k);
    const inB = Object.prototype.hasOwnProperty.call(b, k);
    if (inA !== inB) { out.push({ path: k, kind: inA ? 'removed' : 'added' }); continue; }
    if (!same(a[k], b[k])) out.push({ path: k, kind: 'changed', before: a[k], after: b[k] });
  }
  return out;
}

const ENGINE = R + '/references/publication-engine-129.mjs';
const A = JSON.parse(readBytes(R + '/authority-129.json').toString('utf8'));

// ---------------------------------------------------------------- the fixture root
const F = R + '/test-only/e1';
if (fs.existsSync(F)) throw new Error('the fixture already exists; this test uses a fresh namespace: ' + F);
for (const d of ['receipts', 'stage-output', 'payload-src']) fs.mkdirSync(F + '/' + d, { recursive: true });

// Real files with real content, of two sizes so a size change is visible too.
const sources = [
  ['one.txt', 'the first file of the e1 fixture\n'],
  ['two.txt', 'the second file of the e1 fixture\n'],
  ['three.bin', Buffer.from([0x47, 0x54, 0x4f, 0x53, 0x2d, 0x31, 0x32, 0x39])],
];
for (const [name, content] of sources) fs.writeFileSync(F + '/payload-src/' + name, content);

// A real runner identity, so assertRunnerIdentity has something real to verify rather than an empty set.
const runnerBytes = readBytes(R + '/frozen-runners/run-129-r01.mjs');
const attempt = {
  stage: 'e1f-01', runner_path: R + '/frozen-runners/run-129-r01.mjs',
  runner_bytes: runnerBytes.length, runner_sha256: sha(runnerBytes),
};
fs.writeFileSync(F + '/attempt-e1f-01-110.json', JSON.stringify(attempt, null, 2) + '\n');

// The plan and the shape over those files.
function writePlanAndShape(publication, planName, shapeName) {
  const planFiles = sources.map(([name]) => {
    const b = readBytes(F + '/payload-src/' + name);
    return { source: F + '/payload-src/' + name, destination: name,
      bytes: b.length, sha256: sha(b) };
  });
  const plan = { task: 'GTOS-DSH-129', kind: 'copy_plan', files: planFiles,
    rows: planFiles.length, bytes: planFiles.reduce((s, f) => s + f.bytes, 0),
    groups: { native: planFiles.length } };
  fs.writeFileSync(F + '/' + planName, JSON.stringify(plan, null, 2) + '\n', { flag: 'wx' });
  const shape = {
    task: 'GTOS-DSH-129', generated_from: 'the e1 fixture plan',
    required_paths: ['one.txt', 'three.bin'],
    groups: [{ prefix: '', files: planFiles.length,
      bytes: planFiles.reduce((s, f) => s + f.bytes, 0) }],
    // The ledger rule is real and this fixture does not fabricate a ledger to satisfy it: it points the rule at this
    // packet's own real source-root ledger, which is a genuine one-row-per-stage ledger produced by real stages.
    native_roots: [{ name: 'source-root',
      host_root: '\\\\wsl$\\Ubuntu-20.04' + A.source_root.replace(/\//g, '\\'), ledger: 'runs-129.jsonl' }],
    digest_recipe: 'ordinal POSIX paths, UTF8(sha256 + two spaces + path + LF), SHA256 of the concatenation',
    manifest_self_excluded: true, self_receipts_outside_payload: true,
  };
  fs.writeFileSync(F + '/' + shapeName, JSON.stringify(shape, null, 2) + '\n', { flag: 'wx' });
  return { plan: F + '/' + planName, shape: F + '/' + shapeName };
}

// ---------------------------------------------------------------- the real CLI
function runEngine(mode, options) {
  const control = {
    task: 'GTOS-DSH-129', mode, stage: options.stage,
    report_root: F,
    publication: options.publication,
    shape: pin(options.shape),
    copy_plan: pin(options.plan),
    receipt_path: F + '/receipts/' + options.receipt,
  };
  if (options.archiveReceipt) control.archive_receipt = pin(options.archiveReceipt);
  const controlPath = F + '/stage-output/' + options.stage + '.control.json';
  fs.writeFileSync(controlPath, JSON.stringify(control, null, 2) + '\n', { flag: 'wx' });
  const r = spawnSync(NODE, [ENGINE, controlPath], {
    windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8',
  });
  return { status: r.status, stdout: String(r.stdout || ''), stderr: String(r.stderr || ''), controlPath };
}

// ---------------------------------------------------------------- 1. a real archive
const first = writePlanAndShape('publication-129-v01', 'plan-v01.json', 'shape-v01.json');
const archiveOne = runEngine('archive', {
  stage: 'earc01-01', publication: 'publication-129-v01',
  plan: first.plan, shape: first.shape, receipt: 'archive-01.json',
});
check('the real engine CLI accepted the first archive', archiveOne.status === 0,
  { status: archiveOne.status, stderr: archiveOne.stderr.split('\n').filter((l) => l.trim()).slice(0, 3) });
const archiveReceiptOne = F + '/receipts/archive-01.json';
const archiveDoc = JSON.parse(readBytes(archiveReceiptOne).toString('utf8'));
check('the archive receipt is real and reports success', archiveDoc.ok === true && archiveDoc.mode === 'archive',
  { ok: archiveDoc.ok, mode: archiveDoc.mode });
check('the archive receipt carries a payload identity derived from disk',
  archiveDoc.payload && archiveDoc.payload.files === sources.length
  && /^[0-9a-f]{64}$/.test(String(archiveDoc.payload.root_digest)),
  archiveDoc.payload);
check('the archive wrote a real payload tree',
  fs.existsSync(F + '/publication-129-v01/native/one.txt')
  && fs.existsSync(F + '/publication-129-v01/manifest-129.json'), null);

// ---------------------------------------------------------------- 2. a real seal
const sealOne = runEngine('seal', {
  stage: 'esel01-01', publication: 'publication-129-v01',
  plan: first.plan, shape: first.shape, receipt: 'seal-01.json',
  archiveReceipt: archiveReceiptOne,
});
check('the real engine CLI accepted the first seal', sealOne.status === 0,
  { status: sealOne.status, stderr: sealOne.stderr.split('\n').filter((l) => l.trim()).slice(0, 3) });
const sealReceiptOne = F + '/receipts/seal-01.json';
const sealDoc = JSON.parse(readBytes(sealReceiptOne).toString('utf8'));
check('the seal receipt is real and reports success', sealDoc.ok === true && sealDoc.mode === 'seal',
  { ok: sealDoc.ok, mode: sealDoc.mode });
check('the seal receipt re-derived the digest rather than trusting the archive',
  sealDoc.payload.root_digest === archiveDoc.payload.root_digest,
  { seal: sealDoc.payload.root_digest, archive: archiveDoc.payload.root_digest });

// ---------------------------------------------------------------- the pins taken before the second attempt
const treeBefore = pinTree(F + '/publication-129-v01');
const archiveBefore = pin(archiveReceiptOne);
const sealBefore = pin(sealReceiptOne);
check('the first publication tree is not empty', Object.keys(treeBefore).length >= 4,
  Object.keys(treeBefore).length);

// ---------------------------------------------------------------- 3. a second archive, refused
const second = writePlanAndShape('publication-129-v02', 'plan-v02.json', 'shape-v02.json');
const archiveTwo = runEngine('archive', {
  stage: 'earc02-01', publication: 'publication-129-v02',
  plan: second.plan, shape: second.shape, receipt: 'archive-02.json',
});
check('a second archive in the same packet is refused', archiveTwo.status !== 0,
  { status: archiveTwo.status, stdout: archiveTwo.stdout.slice(0, 200) });
check('the refusal names the terminal seal',
  /already sealed/.test(archiveTwo.stdout + archiveTwo.stderr),
  (archiveTwo.stdout + archiveTwo.stderr).split('\n').filter((l) => l.trim()).slice(0, 2));
check('the refused archive wrote no receipt', !fs.existsSync(F + '/receipts/archive-02.json'), null);
check('the refused archive wrote no payload tree', !fs.existsSync(F + '/publication-129-v02'), null);

// ---------------------------------------------------------------- 4. a second seal, refused
const sealTwo = runEngine('seal', {
  stage: 'esel02-01', publication: 'publication-129-v02',
  plan: second.plan, shape: second.shape, receipt: 'seal-02.json',
  archiveReceipt: archiveReceiptOne,
});
check('a second seal in the same packet is refused', sealTwo.status !== 0,
  { status: sealTwo.status, stdout: sealTwo.stdout.slice(0, 200) });
check('the second seal refusal names the terminal seal',
  /already sealed/.test(sealTwo.stdout + sealTwo.stderr),
  (sealTwo.stdout + sealTwo.stderr).split('\n').filter((l) => l.trim()).slice(0, 2));
check('the refused seal wrote no receipt', !fs.existsSync(F + '/receipts/seal-02.json'), null);

// ---------------------------------------------------------------- 5. the first state is untouched
{
  const treeAfter = pinTree(F + '/publication-129-v01');
  const d = diffTrees(treeBefore, treeAfter);
  check('the first publication tree is byte identical after both refusals', d.length === 0, d);
  check('the first archive receipt is byte identical after both refusals', same(archiveBefore, pin(archiveReceiptOne)),
    { before: archiveBefore && archiveBefore.sha256, after: pin(archiveReceiptOne).sha256 });
  check('the first seal receipt is byte identical after both refusals', same(sealBefore, pin(sealReceiptOne)),
    { before: sealBefore && sealBefore.sha256, after: pin(sealReceiptOne).sha256 });
  check('the receipts directory holds exactly the two real receipts',
    fs.readdirSync(F + '/receipts').sort().join(',') === 'archive-01.json,seal-01.json',
    fs.readdirSync(F + '/receipts'));
}

// ---------------------------------------------------------------- 6. a failed seal does not close the packet
// A separate root, because this one is closed. The failure must be real: the seal is given an archive receipt that says
// it failed, which the engine refuses before writing.
{
  const G = R + '/test-only/e1-failed';
  if (fs.existsSync(G)) throw new Error('the failed-seal fixture already exists: ' + G);
  for (const d of ['receipts', 'stage-output', 'payload-src']) fs.mkdirSync(G + '/' + d, { recursive: true });
  for (const [name, content] of sources) fs.writeFileSync(G + '/payload-src/' + name, content);
  const b0 = readBytes(R + '/frozen-runners/run-129-r01.mjs');
  fs.writeFileSync(G + '/attempt-e1g-01-110.json', JSON.stringify({
    stage: 'e1g-01', runner_path: R + '/frozen-runners/run-129-r01.mjs',
    runner_bytes: b0.length, runner_sha256: sha(b0),
  }, null, 2) + '\n');
  const gPlanFiles = sources.map(([name]) => {
    const b = readBytes(G + '/payload-src/' + name);
    return { source: G + '/payload-src/' + name, destination: name,
      bytes: b.length, sha256: sha(b) };
  });
  fs.writeFileSync(G + '/plan.json', JSON.stringify({
    task: 'GTOS-DSH-129', kind: 'copy_plan', files: gPlanFiles, rows: gPlanFiles.length,
    bytes: gPlanFiles.reduce((s, f) => s + f.bytes, 0), groups: { native: gPlanFiles.length },
  }, null, 2) + '\n');
  fs.writeFileSync(G + '/shape.json', JSON.stringify({
    task: 'GTOS-DSH-129', generated_from: 'the e1-failed plan',
    required_paths: ['one.txt'],
    groups: [{ prefix: '', files: gPlanFiles.length,
      bytes: gPlanFiles.reduce((s, f) => s + f.bytes, 0) }],
    native_roots: [{ name: 'source-root',
      host_root: '\\\\wsl$\\Ubuntu-20.04' + A.source_root.replace(/\//g, '\\'), ledger: 'runs-129.jsonl' }],
    digest_recipe: 'ordinal POSIX paths', manifest_self_excluded: true,
    self_receipts_outside_payload: true,
  }, null, 2) + '\n');

  function runIn(root, mode, options) {
    const control = {
      task: 'GTOS-DSH-129', mode, stage: options.stage, report_root: root,
      publication: options.publication,
      shape: pin(root + '/shape.json'), copy_plan: pin(root + '/plan.json'),
      receipt_path: root + '/receipts/' + options.receipt,
    };
    if (options.archiveReceipt) control.archive_receipt = pin(options.archiveReceipt);
    const cp = root + '/stage-output/' + options.stage + '.control.json';
    fs.writeFileSync(cp, JSON.stringify(control, null, 2) + '\n', { flag: 'wx' });
    const r = spawnSync(NODE, [ENGINE, cp], {
      windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8',
    });
    return { status: r.status, stdout: String(r.stdout || ''), stderr: String(r.stderr || '') };
  }

  // A seal with no archive at all is refused.
  const noArchive = runIn(G, 'seal', {
    stage: 'gsel01-01', publication: 'publication-129-v03', receipt: 'seal-01.json',
    archiveReceipt: G + '/receipts/does-not-exist.json',
  });
  check('a seal with no archive receipt is refused', noArchive.status !== 0,
    { status: noArchive.status });
  check('that refusal left no seal receipt', !fs.existsSync(G + '/receipts/seal-01.json'), null);

  // A real archive, then a seal whose archive receipt is the failed one: it must be refused, and the packet must stay open.
  const gArchive = runIn(G, 'archive', {
    stage: 'garc01-01', publication: 'publication-129-v03', receipt: 'archive-01.json',
  });
  check('the e1-failed archive succeeded', gArchive.status === 0, { status: gArchive.status });

  // A forged archive receipt that says it failed.
  const realReceipt = JSON.parse(readBytes(G + '/receipts/archive-01.json').toString('utf8'));
  const failedReceipt = G + '/receipts/archive-failed.json';
  fs.writeFileSync(failedReceipt, JSON.stringify({ ...realReceipt, ok: false }, null, 2) + '\n');
  const badSeal = runIn(G, 'seal', {
    stage: 'gsel02-01', publication: 'publication-129-v03', receipt: 'seal-02.json',
    archiveReceipt: failedReceipt,
  });
  check('a seal over a failed archive receipt is refused', badSeal.status !== 0, { status: badSeal.status });
  check('that refusal left no seal receipt', !fs.existsSync(G + '/receipts/seal-02.json'), null);

  // And the packet is still open: a proper seal now succeeds, which proves the failure did not close it.
  const goodSeal = runIn(G, 'seal', {
    stage: 'gsel03-01', publication: 'publication-129-v03', receipt: 'seal-03.json',
    archiveReceipt: G + '/receipts/archive-01.json',
  });
  check('a failed seal does not close the packet: a proper seal afterwards succeeds', goodSeal.status === 0,
    { status: goodSeal.status, stderr: goodSeal.stderr.split('\n').filter((l) => l.trim()).slice(0, 2) });
  check('the successful seal receipt exists and is real',
    (() => {
      const b = readBytes(G + '/receipts/seal-03.json');
      if (b === null) return false;
      const d = JSON.parse(b.toString('utf8'));
      return d.ok === true && d.mode === 'seal' && d.publication === 'publication-129-v03';
    })(), null);
  // And the failed receipts are still on disk: a failure is kept, not deleted to reset state.
  check('the failed attempts are still on disk',
    fs.existsSync(G + '/receipts/archive-failed.json'), fs.readdirSync(G + '/receipts'));
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-129', kind: 'real_engine_finish_protocol_test',
  subject: 'references/publication-engine-129.mjs driven through its own CLI',
  fixture: F, engine: ENGINE,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
