// GTOS-DSH-146: one real consumer family, run over the extracted payload, with the IO tracer preloaded.
//
// This is the process the tracer watches. It is deliberately dumb: it takes a family name, the extracted tree, and an alias
// table, and it runs the packet's REAL consumer modules against that tree. It does not classify anything and it does not
// write a verdict -- the classifier runs afterwards, in another process, over the trace this process leaves behind. A
// consumer that graded its own trace would be the thing the review objected to.
//
// The guest families also read the per-trial RECORD out of the tree and check its channel pins against the channel bytes
// they are about to judge. That is not decoration: a trial record the payload carries but nobody opens is a record whose
// pins nothing ever compares, and the 145 review's complaint about carried-but-unconsumed members is exactly that.
//
// Usage: node --require io-trace-146.cjs io-real-146-consumer.mjs <family> <tree> <aliasMap.json> <out.json>

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [family, TREE, aliasPath, outPath] = process.argv.slice(2);
if (!family || !TREE || !aliasPath || !outPath) {
  console.error('usage: <family> <tree> <aliasMap.json> <out.json>');
  process.exit(2);
}

const W = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a/worktree-146';
const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const aliasMap = readJson(aliasPath);
const spec = readJson(R + '/spec-146-02.json');

const rows = [];
const result = { family, tree: TREE, rows };
const at = (p) => aliasMap[p] || p;

// A trial record is read from the tree and its channel pins are compared with the bytes the judge is about to parse. A pin
// that does not match is a refusal, not a warning: it would mean the record and the channel in the same payload disagree
// about what the channel is.
function trialRecord(t) {
  const recordPath = t.record && t.record.path ? at(t.record.path) : null;
  if (!recordPath) return { read: false, why: 'the trial names no record' };
  const rec = readJson(recordPath);
  const problems = [];
  if (rec.scene !== t.scene) problems.push('the record names another scene: ' + rec.scene);
  // The matrix numbers its trials (`1`, `2`) and the record names them (`runtime-init-smp1-t1`). Comparing the two fields
  // directly would refuse every trial in the packet for a difference in spelling rather than in fact, so the record's name is
  // checked against the name its scene and number produce, and its stage against the matrix stage.
  const expectedTrial = t.scene + '-t' + t.trial;
  if (rec.trial !== expectedTrial) problems.push('the record names another trial: ' + rec.trial + ', expected ' + expectedTrial);
  if (t.stage && rec.stage !== t.stage) problems.push('the record names another stage: ' + rec.stage + ', expected ' + t.stage);
  const pins = rec.channels || {};
  for (const which of ['serial', 'e9']) {
    const pin = pins[which];
    const named = t[which];
    if (!pin || !named) { problems.push('the record pins no ' + which + ' channel'); continue; }
    if (pin.sha256 !== named.sha256) problems.push('the record and the matrix disagree about the ' + which + ' channel hash');
  }
  return { read: true, path: recordPath, scene: rec.scene, trial: rec.trial, problems, ok: problems.length === 0 };
}

if (family === 'prerequisite') {
  const P = await import(pathToFileURL(W + '/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs').href);
  const set = readJson(R144 + '/t-prerequisites-tpr144-01-144.json');
  for (const r of set.roles) {
    const recordPath = r.path || r.record;
    const ctx = { contract_path: R144 + '/contract-144.json', ledger_path: R144 + '/producer-order-144.jsonl',
      runner_path: R144 + '/frozen-runners/run-144-r01.mjs', aliases: aliasMap };
    let verdict = null;
    let error = null;
    try { verdict = P.consumeRole(r.role, readJson(recordPath), ctx); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ role: r.role, ok: !!(verdict && verdict.ok === true),
      redirected: ctx.alias_use ? ctx.alias_use.used : 0, error });
  }
} else if (family === 'resource') {
  const RJ = await import(pathToFileURL(W + '/tools/gtos-dsh-144/references/resource-semantics-144.mjs').href);
  const matrix = readJson(R144 + '/g-matrix-gm144-01-144.json');
  const scenes = new Set(Object.keys(RJ.RESOURCE_SCENES));
  for (const t of matrix.trials) {
    if (!scenes.has(t.scene)) continue;
    let verdict = null;
    let error = null;
    const rec = trialRecord(t);
    try {
      const serialBytes = fs.readFileSync(at(t.serial.path));
      const e9Bytes = fs.readFileSync(at(t.e9.path));
      if (t.serial.sha256 && sha(serialBytes) !== t.serial.sha256) throw new Error('the serial channel in the payload is not the bytes the matrix pins');
      if (t.e9.sha256 && sha(e9Bytes) !== t.e9.sha256) throw new Error('the e9 channel in the payload is not the bytes the matrix pins');
      verdict = RJ.judgeResourceSemantics(t.scene, { e9: e9Bytes.toString('utf8'), serial: serialBytes.toString('utf8') });
    } catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: t.scene, trial: t.trial, record_read_from_the_payload: rec.read,
      record_pins_ok: rec.ok === true, record_problems: rec.problems || [],
      accepted: !!(verdict && verdict.accepted), error });
  }
} else if (family === 'numeric') {
  const N = await import(pathToFileURL(W + '/tools/gtos-dsh-146/r-numeric-146.mjs').href);
  const matrix = readJson(R144 + '/g-matrix-gm144-01-144.json');
  for (const t of matrix.trials) {
    let verdict = null;
    let error = null;
    const rec = trialRecord(t);
    try {
      const serialBytes = fs.readFileSync(at(t.serial.path));
      const e9Bytes = fs.readFileSync(at(t.e9.path));
      if (t.serial.sha256 && sha(serialBytes) !== t.serial.sha256) throw new Error('the serial channel in the payload is not the bytes the matrix pins');
      if (t.e9.sha256 && sha(e9Bytes) !== t.e9.sha256) throw new Error('the e9 channel in the payload is not the bytes the matrix pins');
      verdict = N.judgeNonResource(t.scene, serialBytes.toString('utf8'), e9Bytes.toString('utf8'));
    } catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: t.scene, trial: t.trial, record_read_from_the_payload: rec.read,
      record_pins_ok: rec.ok === true, record_problems: rec.problems || [],
      accepted: !!(verdict && verdict.ok === true), obligations: verdict ? verdict.obligation_count : 0,
      failed: verdict ? verdict.failed : null, error });
  }
} else if (family === 'host') {
  const H = await import(pathToFileURL(W + '/tools/gtos-dsh-144/host-consume-144.mjs').href);
  const builds = fs.readdirSync(R144).filter((f) => /^h-unit-build-.*\.json$/.test(f)).sort()
    .map((n) => ({ n, rec: readJson(R144 + '/' + n) })).filter((b) => b.rec.elf && b.rec.elf.path);
  for (const b of builds) {
    const elfPkg = spec.aliases[b.rec.elf.path] || spec.aliases['//wsl$/Ubuntu-20.04' + b.rec.elf.path];
    let verdict = null;
    let error = null;
    try {
      verdict = H.consumeHostChain({
        task: 'GTOS-DSH-146',
        build_record_path: at(R144 + '/' + b.n),
        contract_path: at(R144 + '/h-contract-144e.json'),
        unit_elf_host_path: elfPkg ? TREE + '/' + elfPkg : b.rec.elf.path,
        unit_elf_linux: b.rec.elfFile,
        // The case results come from the payload, not from the 144 directory. Reading them from the 144 directory would let
        // the host chain pass while the payload's own copies were never opened.
        results_dir: TREE + '/host/results',
        path_aliases: aliasMap,
      });
    } catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ record: b.n, accepted: !!(verdict && verdict.accepted),
      counts: verdict && verdict.counts ? verdict.counts : null,
      problems: verdict && verdict.problems ? verdict.problems.slice(0, 4) : [], error });
  }
} else {
  console.error('unknown family: ' + family);
  process.exit(2);
}

const accepted = rows.filter((r) => r.ok === true || r.accepted === true).length;
const recordsOk = rows.every((r) => r.record_pins_ok !== false);
const out = { family, tree: TREE, rows, accepted, total: rows.length, records_ok: recordsOk,
  ok: rows.length > 0 && accepted === rows.length && recordsOk };
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ family, accepted, total: rows.length, records_ok: recordsOk, ok: out.ok }));
process.exit(out.ok ? 0 : 1);
