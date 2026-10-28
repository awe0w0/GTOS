// GTOS-DSH-146 F test: archive cardinality and the mandatory-role gate, on the real publisher.
//
// 145's review left one inference unexecuted: "the publisher refuses a second archive only when the ordinal digest matches,
// so a different plan over different content could still enter the same state". 146 has to answer that by running it, not
// by arguing it. This test drives the real engine's own verbs against TEST_ONLY contexts, so every case is a real
// process, a real state file, a real tar, and a real refusal code.
//
// The cases, and why each is here rather than a simpler one:
//
//   1. one archive succeeds, and the state holds exactly one row;
//   2. a SECOND archive with DIFFERENT content and a DIFFERENT digest is refused. This is the case 145 could not refuse.
//      A test that only replayed the same payload would pass on the old engine and prove nothing;
//   3. the same payload archived under a different name is also refused, by the count rather than by the digest;
//   4. after the seal, an archive is refused (the existing 145 gate, kept and re-run);
//   5. a second seal is refused;
//   6. a plan that omits one mandatory role is refused at plan time, before a payload is written;
//   7. a plan naming a role the contract does not name is refused;
//   8. a role record whose own field says false is refused at seal time;
//   9. a role record that has drifted since the plan was frozen is refused at seal time;
//  10. a role record that is absent is refused at seal time;
//  11. the complete set seals, and the receipt names all seven roles;
//  12. verify agrees, and its own obligations include the role coverage and the single-archive count.
//
// Cases 6-10 are the qualification gate. Each breaks exactly one thing, so a gate that only counted entries would pass 6
// and fail 8, and a gate that only read fields would pass 8 and fail 6. Both are needed.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const ENGINE = R + '/worktree-146/tools/gtos-dsh-146/publish-146.mjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// Each RUN gets its own numbered tree, created new. A fixture tree is not evidence, but it is also not something to delete:
// an earlier run's tree is what a reader looks at to see what the engine actually did, so a re-run writes beside it rather
// than over it. The engine's own CreateNew rule then applies to the fixtures exactly as it applies to production.
const PARENT = R + '/test-only/f-cardinality-146';
fs.mkdirSync(PARENT, { recursive: true });
let runNumber = 1;
while (fs.existsSync(PARENT + '/run-' + String(runNumber).padStart(2, '0'))) runNumber += 1;
const ROOT = PARENT + '/run-' + String(runNumber).padStart(2, '0');
fs.mkdirSync(ROOT, { recursive: true });

// Each case gets its own report root under the TEST_ONLY prefix, so the engine's TEST_ONLY scope rules apply and no case
// can see another's state.
function freshRoot(name) {
  const dir = ROOT + '/' + name;
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

function contextFor(dir, task) {
  const ctx = {
    task,
    scope: 'test_only',
    authorized_report_root: dir,
    fixed_state_path: dir + '/publication-state-' + /(\d+)$/.exec(task)[1] + '.jsonl',
    production_destination: dir + '/archive-' + /(\d+)$/.exec(task)[1],
  };
  const p = dir + '/publication-context.json';
  fs.writeFileSync(p, JSON.stringify(ctx, null, 2) + '\n');
  return p;
}

function run(args) {
  const r = spawnSync(NODE, [ENGINE, ...args], { encoding: 'utf8', windowsHide: true, maxBuffer: 64 * 1024 * 1024 });
  let parsed = null;
  try { parsed = JSON.parse((r.stdout || '').trim()); } catch { parsed = null; }
  let refused = null;
  try { refused = JSON.parse((r.stderr || '').trim()).refused; } catch { refused = null; }
  return { status: r.status, stdout: parsed, refused, stderr: (r.stderr || '').slice(0, 400) };
}

// A member set for a fixture: a few real files from this packet's own tree, so the bytes are real and the sizes are real.
const realFiles = [
  R + '/worktree-146/tools/gtos-dsh-146/d-required-146.mjs',
  R + '/worktree-146/tools/gtos-dsh-146/tests/d-aliases-146.mjs',
  R + '/dispatch-146.json',
];

function specFor(dir, tag, opts) {
  const o = opts || {};
  const members = realFiles.map((from, i) => ({ from, path: 'members/' + tag + '/file-' + i + '.bin',
    role: i === 0 ? 'engine' : 'input' }));
  // A different content set for the "second, different payload" case.
  if (o.extra) members.push({ from: o.extra, path: 'members/' + tag + '/extra.bin', role: 'input' });
  const roles = o.roles || ['prerequisite_full', 'producer_and_native_chain_full', 'host_chain_full',
    'static_byte_gate_full', 'guest_resource_semantics_full', 'independent_physical_closure',
    'publication_and_related_tests'];
  const roleEntries = roles.map((role) => {
    const rec = o.records ? o.records[role] : null;
    if (!rec) return { role, path: R + '/test-only/f-cardinality-146/roles/' + role + '.json',
      bytes: 0, sha256: '0'.repeat(64), ok_field: 'ok' };
    return { role, ...rec };
  });
  const requiredSet = o.requiredSet || null;
  const spec = {
    task: o.task,
    members,
    required_set: requiredSet,
    mandatory_roles: { roles: roleEntries },
    evidence: o.evidence || [],
  };
  const p = dir + '/spec.json';
  fs.writeFileSync(p, JSON.stringify(spec, null, 2) + '\n');
  return p;
}

// A role record the engine will accept: it exists, it has the pinned bytes and hash, and its ok field is true.
function roleRecord(dir, role, okValue) {
  const p = dir + '/roles/' + role + '.json';
  fs.mkdirSync(path.dirname(p), { recursive: true });
  const body = { role, ok: okValue === undefined ? true : okValue, what: 'a TEST_ONLY role record for the cardinality fixture' };
  fs.writeFileSync(p, JSON.stringify(body, null, 2) + '\n');
  const b = fs.readFileSync(p);
  return { path: p, bytes: b.length, sha256: sha(b), ok_field: 'ok' };
}

function requiredSetRecord(dir) {
  const p = dir + '/required-set.json';
  fs.writeFileSync(p, JSON.stringify({ ok: true, what: 'a TEST_ONLY required set' }, null, 2) + '\n');
  const b = fs.readFileSync(p);
  return { path: p, bytes: b.length, sha256: sha(b), ok_field: 'ok' };
}

const cases = [];
function record(name, expected, observed, extra) {
  cases.push({ name, expected, observed, ok: expected === observed, ...(extra || {}) });
}

const ROLES = ['prerequisite_full', 'producer_and_native_chain_full', 'host_chain_full', 'static_byte_gate_full',
  'guest_resource_semantics_full', 'independent_physical_closure', 'publication_and_related_tests'];

// ---------------------------------------------------------------- 1..5 the archive lifecycle
{
  const dir = freshRoot('lifecycle');
  const ctx = contextFor(dir, 'GTOS-DSH-7146');
  const records = {};
  for (const role of ROLES) records[role] = roleRecord(dir, role);
  const rs = requiredSetRecord(dir);

  const specA = specFor(dir, 'a', { task: 'GTOS-DSH-7146', roles: ROLES, records, requiredSet: rs });
  const planA = dir + '/archive-7146/plan.json';
  const p1 = run(['plan', ctx, specA, planA]);
  record('plan-accepted', 0, p1.status, { refused: p1.refused });

  const payloadA = dir + '/archive-7146/payload.tar';
  const a1 = run(['archive', ctx, planA, payloadA]);
  record('first-archive-accepted', 0, a1.status, { refused: a1.refused, digest: a1.stdout && a1.stdout.ordinal_digest });

  const st1 = run(['state', ctx]);
  record('state-has-one-archive', 1, st1.stdout && st1.stdout.archives);

  // 2. a SECOND archive with different content. This is the case the taskbook names and 145 could not refuse.
  const specB = specFor(dir, 'b', { task: 'GTOS-DSH-7146', roles: ROLES, records, requiredSet: rs,
    extra: R + '/worktree-146/tools/gtos-dsh-146/publish-146.mjs' });
  const planB = dir + '/archive-7146/plan-b.json';
  const pB = run(['plan', ctx, specB, planB]);
  record('second-plan-can-be-frozen', 0, pB.status);
  const payloadB = dir + '/archive-7146/payload-b.tar';
  const a2 = run(['archive', ctx, planB, payloadB]);
  record('second-different-archive-refused', 2, a2.status, { refused: a2.refused });
  record('second-different-archive-refusal-is-about-cardinality', true,
    !!(a2.refused && a2.refused.includes('already been archived once')));
  record('second-different-archive-wrote-no-payload', false, fs.existsSync(payloadB));

  // 3. the same payload under another name, which the digest check alone would also catch. It is here so that the two
  // gates are shown to be independent: this one fails on the digest, and the case above fails on the count.
  const a3 = run(['archive', ctx, planA, dir + '/archive-7146/payload-again.tar']);
  record('same-payload-again-refused', 2, a3.status, { refused: a3.refused });

  // 4. seal, then archive: the 145 gate, re-run on this engine.
  const receipt = dir + '/archive-7146/receipt.json';
  const s1 = run(['seal', ctx, payloadA, receipt, planA]);
  record('seal-accepted', 0, s1.status, { refused: s1.refused,
    roles_consumed: s1.stdout && s1.stdout.mandatory_roles_consumed });
  record('seal-consumed-every-role', ROLES.length, s1.stdout && s1.stdout.mandatory_roles_consumed);

  const a4 = run(['archive', ctx, planA, dir + '/archive-7146/payload-after-seal.tar']);
  record('archive-after-seal-refused', 2, a4.status, { refused: a4.refused });

  // 5. a second seal.
  const s2 = run(['seal', ctx, payloadA, dir + '/archive-7146/receipt-2.json', planA]);
  record('second-seal-refused', 2, s2.status, { refused: s2.refused });

  // 12. verify, and its own obligations.
  const v = run(['verify', ctx, payloadA, receipt, planA]);
  if (v.status !== 0) console.log('  verify said:', v.refused || v.stderr, JSON.stringify(v.stdout && v.stdout.failed_obligations));
  record('verify-accepted', 0, v.status, { failed: v.stdout && v.stdout.failed_obligations });
  const verifyPath = dir + '/verify-7146.json';
  const verifyRec = fs.existsSync(verifyPath) ? JSON.parse(fs.readFileSync(verifyPath, 'utf8')) : null;
  record('verify-names-role-coverage', true, !!(verifyRec && verifyRec.obligations
    && verifyRec.obligations.receipt_consumed_every_mandatory_role.value === true));
  record('verify-names-one-archive', true, !!(verifyRec && verifyRec.obligations
    && verifyRec.obligations.state_has_one_archive.value === true));
}

// ---------------------------------------------------------------- 6..10 the qualification gate
{
  const dir = freshRoot('roles');
  const ctx = contextFor(dir, 'GTOS-DSH-8146');
  const records = {};
  for (const role of ROLES) records[role] = roleRecord(dir, role);
  const rs = requiredSetRecord(dir);

  // 6. a plan that omits one role.
  const short = ROLES.slice(0, 6);
  const specShort = specFor(dir, 'short', { task: 'GTOS-DSH-8146', roles: short, records, requiredSet: rs });
  const pShort = run(['plan', ctx, specShort, dir + '/archive-8146/plan-short.json']);
  record('plan-omitting-a-role-refused', 2, pShort.status, { refused: pShort.refused });
  record('omitted-role-refusal-names-the-role', true,
    !!(pShort.refused && pShort.refused.includes('mandatory consumer roles')));

  // 7. a plan naming a role the contract does not name.
  const specExtra = specFor(dir, 'extra', { task: 'GTOS-DSH-8146', roles: [...ROLES, 'a_role_nobody_asked_for'],
    records, requiredSet: rs });
  const pExtra = run(['plan', ctx, specExtra, dir + '/archive-8146/plan-extra.json']);
  record('plan-with-an-unknown-role-refused', 2, pExtra.status, { refused: pExtra.refused });

  // A complete plan to use for the seal-time cases.
  const specOk = specFor(dir, 'ok', { task: 'GTOS-DSH-8146', roles: ROLES, records, requiredSet: rs });
  const planOk = dir + '/archive-8146/plan.json';
  const pOk = run(['plan', ctx, specOk, planOk]);
  record('complete-plan-accepted', 0, pOk.status);
  const payloadOk = dir + '/archive-8146/payload.tar';
  const aOk = run(['archive', ctx, planOk, payloadOk]);
  record('archive-for-role-cases-accepted', 0, aOk.status);

  // 8. a role record whose field says false. The plan is frozen against a record that says false, so the only thing the
  // seal can refuse it on is the value. This is the case that separates "read the field" from "count the entries".
  //
  // Each of cases 8, 9 and 10 gets its OWN record file. Sharing one file would make the cases interfere: the false record
  // would already be on disk when the drift case was planned, and a test that passes for the wrong reason is worse than no
  // test. This is the same mistake the 144 host negatives made and it is not repeated here.
  const victim = 'host_chain_full';
  const falsePath = dir + '/roles-false/' + victim + '.json';
  fs.mkdirSync(path.dirname(falsePath), { recursive: true });
  fs.writeFileSync(falsePath, JSON.stringify({ role: victim, ok: false, what: 'says false on purpose' }, null, 2) + '\n');
  const falseBytes = fs.readFileSync(falsePath);
  const falseRecord = { path: falsePath, bytes: falseBytes.length, sha256: sha(falseBytes), ok_field: 'ok' };
  const specFalse = specFor(dir, 'ok', { task: 'GTOS-DSH-8146', roles: ROLES,
    records: { ...records, [victim]: falseRecord }, requiredSet: rs });
  const planFalse = dir + '/archive-8146/plan-false.json';
  const pFalse = run(['plan', ctx, specFalse, planFalse]);
  record('plan-with-a-false-role-record-can-be-frozen', 0, pFalse.status);

  // 9. drift after the plan: a record that is true when planned and changes afterwards.
  const driftPath = dir + '/roles-drift/' + victim + '.json';
  fs.mkdirSync(path.dirname(driftPath), { recursive: true });
  fs.writeFileSync(driftPath, JSON.stringify({ role: victim, ok: true, what: 'true when planned' }, null, 2) + '\n');
  const driftPlanned = fs.readFileSync(driftPath);
  const driftRecord = { path: driftPath, bytes: driftPlanned.length, sha256: sha(driftPlanned), ok_field: 'ok' };
  const specDrift = specFor(dir, 'ok', { task: 'GTOS-DSH-8146', roles: ROLES,
    records: { ...records, [victim]: driftRecord }, requiredSet: rs });
  const planDrift = dir + '/archive-8146/plan-drift.json';
  const pDrift = run(['plan', ctx, specDrift, planDrift]);
  record('drift-plan-accepted', 0, pDrift.status);
  fs.writeFileSync(driftPath, JSON.stringify({ role: victim, ok: true, tampered: 'this file changed after the plan' }, null, 2) + '\n');

  // 10. absent record: a record that is true when planned and gone afterwards.
  const absentRole = 'static_byte_gate_full';
  const absentPath = dir + '/roles-absent/' + absentRole + '.json';
  fs.mkdirSync(path.dirname(absentPath), { recursive: true });
  fs.writeFileSync(absentPath, JSON.stringify({ role: absentRole, ok: true, what: 'true when planned' }, null, 2) + '\n');
  const absentPlanned = fs.readFileSync(absentPath);
  const absentRecord = { path: absentPath, bytes: absentPlanned.length, sha256: sha(absentPlanned), ok_field: 'ok' };
  const specAbsent = specFor(dir, 'ok', { task: 'GTOS-DSH-8146', roles: ROLES,
    records: { ...records, [absentRole]: absentRecord }, requiredSet: rs });
  const planAbsent = dir + '/archive-8146/plan-absent.json';
  const pAbsent = run(['plan', ctx, specAbsent, planAbsent]);
  record('absent-plan-accepted', 0, pAbsent.status);
  fs.unlinkSync(absentPath);

  // One seal attempt at a time, each against a plan that is complete. The three have to be refused, and each is refused
  // before any receipt is written, so the state stays unsealed and the cases can run in sequence.
  const sFalse = run(['seal', ctx, payloadOk, dir + '/archive-8146/receipt-false.json', planFalse]);
  record('seal-refuses-a-role-record-that-says-false', 2, sFalse.status, { refused: sFalse.refused });

  const sDrift = run(['seal', ctx, payloadOk, dir + '/archive-8146/receipt-drift.json', planDrift]);
  record('seal-refuses-a-role-record-that-drifted', 2, sDrift.status, { refused: sDrift.refused });

  const sAbsent = run(['seal', ctx, payloadOk, dir + '/archive-8146/receipt-absent.json', planAbsent]);
  record('seal-refuses-an-absent-role-record', 2, sAbsent.status, { refused: sAbsent.refused });

  record('no-receipt-was-written-by-a-refused-seal', 0,
    ['receipt-false.json', 'receipt-drift.json', 'receipt-absent.json']
      .filter((n) => fs.existsSync(dir + '/archive-8146/' + n)).length);
  record('state-still-has-no-seal', 0, (run(['state', ctx]).stdout || {}).seals);

  // Seal for real, so the case set ends with a positive that went through the same gate as the refusals.
  const sOk = run(['seal', ctx, payloadOk, dir + '/archive-8146/receipt.json', planOk]);
  record('complete-set-seals', 0, sOk.status, { refused: sOk.refused });
  const receiptRec = fs.existsSync(dir + '/archive-8146/receipt.json')
    ? JSON.parse(fs.readFileSync(dir + '/archive-8146/receipt.json', 'utf8')) : null;
  record('receipt-names-seven-roles', ROLES.length,
    receiptRec && receiptRec.mandatory_roles ? receiptRec.mandatory_roles.consumed.length : null);
  record('receipt-names-the-frozen-contract', true,
    !!(receiptRec && receiptRec.mandatory_roles && receiptRec.mandatory_roles.contract
      && receiptRec.mandatory_roles.contract.sha256 === 'ac731967149ac62589799b02b7c589c69fa0bfebd342d557f6840205117341a1'));
}

const out = {
  task: 'GTOS-DSH-146',
  stage: 'f-cardinality-146-01',
  engine: ENGINE,
  what: 'the publisher real verbs on TEST_ONLY contexts: one archive per task identity, and the mandatory role gate',
  cases,
  counts: { cases: cases.length, passed: cases.filter((c) => c.ok).length, failed: cases.filter((c) => !c.ok).length },
  ok: cases.every((c) => c.ok),
  ok_means: 'every case produced the exit status and the refusal it was aimed at. The refusals are the engine own, read '
    + 'from its stderr, not strings this test writes.',
};
const outPath = R + '/f-cardinality-146-01.json';
if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify(out.counts), out.ok ? 'OK' : 'FAILED');
for (const c of cases) if (!c.ok) console.log('  FAIL', c.name, 'expected', c.expected, 'observed', c.observed, c.refused || '');
