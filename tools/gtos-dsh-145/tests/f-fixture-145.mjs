// GTOS-DSH-145 F: exercise the real publisher entry in TEST_ONLY, before any production step.
//
// Every case here calls the same engine the production run will call -- `tools/gtos-dsh-145/publish-145.mjs` -- with the same verbs and
// the same argument shapes. Nothing in this file reimplements the engine, and no case writes a green record the engine did not produce:
// each case records the engine's own exit status, its own stdout and its own stderr, and then reads the filesystem back to see what the
// attempt actually left behind.
//
// The cases are grouped the way the taskbook groups them:
//
//   valid      one complete fixture transaction: plan, archive, seal, verify, and an independent read of the payload by a parser that is
//              not the engine's writer.
//   prearchive a real, parseable payload against a state file that is absent, that is zero bytes, and that exists and parses but holds
//              no archive row for this task. All three must refuse on the pre-archive condition -- not on an absent payload.
//   postseal   after one fixture transaction completes, every way of starting a second one at once: a different terminal directory, a
//              different state file, a different spec, a different receipt name, a copy of the payload, and a new authority and context
//              path. The task and the authorized root stay the same, which is the case that matters.
//   faults     a different task, a same-size drift after the plan, a drift after the plan with a different size, a missing member, a
//              replay of a completed seal, an evidence file that says false, and an evidence file that is absent. Each must refuse, and
//              each must refuse naming its own condition rather than a generic "something was absent".
//
// Every case runs in its own directory under R/test-only/, and every directory is created by this script. A case that needs a completed
// transaction builds its own from scratch rather than borrowing another case's, so one case cannot be green because another one ran
// first.
//
// What is snapshotted: for each case, the bytes, hash and mtime of every file the case could plausibly touch, before and after the
// attempt. The comparison is over the whole directory listing rather than a named trio, because the 144 negatives compared three files
// and would not have noticed a new target appearing beside them.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const W = R + '/worktree-145';
const ENGINE = W + '/tools/gtos-dsh-145/publish-145.mjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const TEST_ONLY = R + '/test-only';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
// A run tag, so that a superseded run's trees and record stay on disk beside this one's instead of being overwritten. A failed run is
// evidence of what went wrong, and the taskbook says failures are preserved rather than deleted.
const RUN = process.argv[2] || '01';
const TAG = '-' + RUN;

// ---------------------------------------------------------------- the tree
function snapshot(dir) {
  const rows = [];
  const walk = (d) => {
    if (!fs.existsSync(d)) return;
    for (const e of fs.readdirSync(d, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) { walk(p); continue; }
      const st = fs.statSync(p);
      const b = fs.readFileSync(p);
      rows.push({ path: path.relative(dir, p).split(path.sep).join('/'), bytes: b.length, sha256: sha(b), mtime_ms: st.mtimeMs });
    }
  };
  walk(dir);
  return rows;
}
function dirDigest(rows) {
  return sha(Buffer.from(rows.map((r) => `${r.path}\0${r.bytes}\0${r.sha256}`).join('\n'), 'utf8'));
}

// ---------------------------------------------------------------- a fixture context and its members
// The members are real files from this worktree, so the payload is real tar with real content and a real reader can parse it. A fixture
// built from invented bytes would prove the engine can hash a string, not that it can publish a packet.
const MEMBER_FILES = [
  'tools/gtos-dsh-145/publish-145.mjs',
  'tools/gtos-dsh-144/references/resource-semantics-144.mjs',
  'tools/gtos-dsh-144/publish-144.mjs',
  'tools/gtos-dsh-144/host-consume-144.mjs',
  'tools/gtos-dsh-144/roles-144.mjs',
];

function makeFixture(name, task) {
  const dir = TEST_ONLY + '/' + name + TAG;
  if (fs.existsSync(dir)) throw new Error('refused, exists: ' + dir);
  fs.mkdirSync(dir + '/members', { recursive: true });
  const members = [];
  MEMBER_FILES.forEach((rel, i) => {
    const src = W + '/' + rel;
    const buf = fs.readFileSync(src);
    const dest = dir + '/members/' + String(i).padStart(2, '0') + '-' + path.basename(rel);
    fs.writeFileSync(dest, buf);
    members.push({ path: 'members/' + String(i).padStart(2, '0') + '-' + path.basename(rel), role: 'tool',
      from: dest, bytes: buf.length, sha256: sha(buf) });
  });
  const tag = /-(\d+)$/.exec(task);
  const slug = tag ? tag[1] : task.toLowerCase().replace(/[^a-z0-9]+/g, '-');
  const ctx = {
    task,
    scope: 'test_only',
    authorized_report_root: dir,
    authority_record: R + '/authority-145.json',
    fixed_context_path: dir + '/context.json',
    fixed_state_path: dir + '/publication-state-' + slug + '.jsonl',
    production_destination: dir + '/archive-' + slug,
    note: 'TEST_ONLY: a separate task identity with its own root, its own state and its own terminal directory. It cannot stand in for '
      + 'the production task and the engine refuses it if it tries.',
  };
  fs.writeFileSync(dir + '/context.json', JSON.stringify(ctx, null, 2) + '\n');
  return { name: name + TAG, dir, ctx, members, contextPath: dir + '/context.json', statePath: ctx.fixed_state_path,
    destDir: ctx.production_destination, specPath: dir + '/spec.json' };
}

function writeSpec(fx, extra) {
  const spec = { task: fx.ctx.task, members: fx.members, ...(extra || {}) };
  fs.writeFileSync(fx.specPath, JSON.stringify(spec, null, 2) + '\n');
  return fx.specPath;
}

// An evidence file the seal is required to consume. It is a real record with a real boolean in a named field.
function writeEvidence(fx, name, okValue, field) {
  const p = fx.dir + '/evidence-' + name + '.json';
  const body = { task: fx.ctx.task, what: name, ok: okValue, [field || 'ok']: okValue };
  const bytes = Buffer.from(JSON.stringify(body, null, 2) + '\n', 'utf8');
  fs.writeFileSync(p, bytes);
  return { what: name, path: p, bytes: bytes.length, sha256: sha(bytes), ok_field: field || 'ok' };
}

// ---------------------------------------------------------------- running the engine
function run(args) {
  const r = spawnSync(NODE, [ENGINE, ...args], { encoding: 'utf8', timeout: 600000, windowsHide: true, maxBuffer: 32 * 1024 * 1024 });
  let json = null;
  try { json = JSON.parse((r.stdout || '').trim()); } catch { json = null; }
  let refused = null;
  try { refused = JSON.parse((r.stderr || '').trim().split('\n').filter(Boolean).pop() || 'null'); } catch { refused = null; }
  return { argv: args, status: r.status, stdout: (r.stdout || '').trim(), stderr: (r.stderr || '').trim(), json, refused };
}

const refusedFor = (r, needle) => r.status !== 0 && !!r.refused && r.refused.refused === true
  && String(r.refused.why || '').toLowerCase().includes(needle.toLowerCase());

// ---------------------------------------------------------------- the cases
const cases = [];
const record = (c) => { cases.push(c); return c; };

// A completed fixture transaction, used by the cases that need one. It is rebuilt per case rather than shared, so a case's result cannot
// depend on another case having run.
// One transaction, one plan. The evidence is pinned before the plan is frozen, because the plan is where the seal reads it from; a second
// plan over the same members would carry the same ordinal digest and the engine would rightly call it the same payload.
function completeTransaction(fx) {
  const required = writeEvidence(fx, 'required-set', true);
  const consumer = writeEvidence(fx, 'consumer-chain', true);
  const spec = { task: fx.ctx.task, members: fx.members, required_set: required, evidence: [consumer] };
  fs.writeFileSync(fx.specPath, JSON.stringify(spec, null, 2) + '\n');
  const plan = fx.dir + '/plan.json';
  const payload = fx.destDir + '/payload.tar';
  const receipt = fx.destDir + '/receipt.json';
  const steps = [];
  steps.push({ step: 'plan', r: run(['plan', fx.contextPath, fx.specPath, plan]) });
  steps.push({ step: 'archive', r: run(['archive', fx.contextPath, plan, payload]) });
  steps.push({ step: 'seal', r: run(['seal', fx.contextPath, payload, receipt, plan]) });
  steps.push({ step: 'verify', r: run(['verify', fx.contextPath, payload, receipt, plan]) });
  return { steps, plan, payload, receipt };
}

// ---- case 1: the complete valid chain, and an independent read of the payload
{
  const fx = makeFixture('valid', 'GTOS-DSH-FIXVALID-01');
  const t = completeTransaction(fx);
  const before = snapshot(fx.dir);
  const steps = t.steps.map((s) => ({ step: s.step, status: s.r.status, ok: s.r.json ? s.r.json.ok : null,
    refused: s.r.refused ? s.r.refused.why : null }));

  // An independent read: a tar parser written here, not the engine's reader, over the payload's own bytes. If this disagrees with the
  // plan, the payload is not what the receipt says and the chain is not valid however green the engine's own verify was.
  const bytes = fs.readFileSync(t.payload);
  const plan = JSON.parse(fs.readFileSync(t.plan, 'utf8'));
  const independent = [];
  let at = 0;
  while (at + 512 <= bytes.length) {
    const h = bytes.subarray(at, at + 512);
    if (h.every((b) => b === 0)) break;
    const z = (from, len) => { const s = h.subarray(from, from + len); const i = s.indexOf(0); return s.subarray(0, i < 0 ? s.length : i).toString('utf8'); };
    const name = z(0, 100);
    const prefix = z(345, 155);
    const size = parseInt(z(124, 12).trim() || '0', 8);
    const body = bytes.subarray(at + 512, at + 512 + size);
    independent.push({ name: prefix ? prefix + '/' + name : name, size, sha256: sha(body) });
    at += 512 + Math.ceil(size / 512) * 512;
  }
  const byName = new Map(independent.map((m) => [m.name, m]));
  const independentProblems = [];
  if (independent.length !== plan.member_count) independentProblems.push({ why: 'member count', want: plan.member_count, got: independent.length });
  for (const m of plan.members) {
    const got = byName.get(m.path);
    if (!got) { independentProblems.push({ why: 'missing', member: m.path }); continue; }
    if (got.size !== m.bytes) independentProblems.push({ why: 'size', member: m.path, want: m.bytes, got: got.size });
    else if (got.sha256 !== m.sha256) independentProblems.push({ why: 'bytes', member: m.path });
  }
  const verifyStep = t.steps.find((s) => s.step === 'verify');
  record({
    case: 'valid', fixture: fx.name, task: fx.ctx.task,
    steps,
    all_steps_ok: t.steps.every((s) => s.r.status === 0),
    verify_ok: verifyStep.r.json ? verifyStep.r.json.ok : null,
    verify_failed_obligations: verifyStep.r.json ? verifyStep.r.json.failed_obligations : null,
    verify_obligation_count: verifyStep.r.json ? Object.keys(verifyStep.r.json.obligations).length : null,
    independent_read: { members: independent.length, plan_members: plan.member_count, problems: independentProblems },
    tree_after: { files: before.length, digest: dirDigest(before) },
    ok: t.steps.every((s) => s.r.status === 0) && verifyStep.r.json && verifyStep.r.json.ok === true
      && independentProblems.length === 0,
    ok_means: 'the same engine the production run will call completed plan, archive, seal and verify in a TEST_ONLY transaction, verify '
      + 'reported every one of its named obligations true, and a tar parser written in this file read the payload back with the same '
      + 'members, sizes and hashes as the plan',
  });
}

// ---- cases 2-4: a real payload against a state file with no archive row
// These are the three the taskbook names. The payload is a real one this engine archived a moment earlier in a different fixture, so it
// parses and is complete; only the state file differs. That is what isolates the pre-archive condition -- the 144 negatives passed a
// payload path that did not exist and were refused for being absent, which tested nothing about the condition they were named after.
function preArchiveCase(label, mutate) {
  const fx = makeFixture(label, 'GTOS-DSH-FIXPRE-' + label.slice(0, 3).toUpperCase());
  const t = completeTransaction(fx);
  // The transaction is completed first, so the state file really does carry an archive row for this payload. The mutation then removes
  // or replaces that row, which is the only difference between these three cases and the valid one. The payload stays a real, parseable,
  // complete payload the same engine wrote.
  const setupOk = t.steps.slice(0, 2).every((s) => s.r.status === 0);
  mutate(fx, t);
  const before = snapshot(fx.dir);
  const sealPath = fx.destDir + '/receipt2.json';
  const r = run(['seal', fx.contextPath, t.payload, sealPath, t.plan]);
  const after = snapshot(fx.dir);
  record({
    case: 'prearchive/' + label, fixture: fx.name, task: fx.ctx.task,
    setup: { completed_the_archive: setupOk,
      archive_ordinal_digest: t.steps[1].r.json ? t.steps[1].r.json.ordinal_digest : null },
    state_as_the_seal_saw_it: {
      path: fx.statePath,
      exists: fs.existsSync(fx.statePath),
      bytes: fs.existsSync(fx.statePath) ? fs.statSync(fx.statePath).size : null,
      rows: fs.existsSync(fx.statePath)
        ? fs.readFileSync(fx.statePath, 'utf8').split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l).verb; } catch { return 'unreadable'; } })
        : [],
    },
    attempt: { status: r.status, refused: r.status !== 0, why: r.refused ? r.refused.why : null, refusal: r.refused },
    refused_on_the_pre_archive_condition: refusedFor(r, 'never archived'),
    refused_because_a_path_was_absent: !!(r.refused && /^absent:/.test(String(r.refused.why || ''))),
    receipt_written: fs.existsSync(sealPath),
    tree_unchanged: dirDigest(before) === dirDigest(after),
    ok: setupOk && r.status !== 0 && refusedFor(r, 'never archived') && !fs.existsSync(sealPath) && dirDigest(before) === dirDigest(after),
    ok_means: 'a real, parseable, complete payload whose successful archive row was removed from the state was refused on the '
      + 'pre-archive condition, no receipt was written, and the tree is byte-identical',
  });
}

// absent: the state file does not exist at all.
preArchiveCase('absent', (fx) => {
  if (fs.existsSync(fx.statePath)) fs.unlinkSync(fx.statePath);
});
// empty: the state file exists, is zero bytes, and parses as no rows.
preArchiveCase('empty', (fx) => {
  fs.writeFileSync(fx.statePath, '');
});
// otherrows: the state file exists, parses, and holds legal rows that are not an archive of this task's payload.
preArchiveCase('otherrows', (fx) => {
  const rows = [
    { at: new Date().toISOString(), task: 'GTOS-DSH-OTHER-99', verb: 'archive', dest: fx.destDir + '/other.tar',
      ordinal_digest: 'a'.repeat(64), sha256: 'b'.repeat(64), members: 1, bytes: 1024 },
    { at: new Date().toISOString(), task: fx.ctx.task, verb: 'note', what: 'a legal row that is not an archive' },
  ];
  fs.writeFileSync(fx.statePath, rows.map((r) => JSON.stringify(r)).join('\n') + '\n');
});

// ---- case 5: after a completed transaction, every route to a second one at once
{
  const fx = makeFixture('postseal', 'GTOS-DSH-FIXSEALED-02');
  const t = completeTransaction(fx);
  const completed = t.steps.every((s) => s.r.status === 0);
  const sealedState = { bytes: fs.statSync(fx.statePath).size, sha256: sha(fs.readFileSync(fx.statePath)) };
  const sealedPayload = { bytes: fs.statSync(t.payload).size, sha256: sha(fs.readFileSync(t.payload)) };
  const sealedReceipt = { bytes: fs.statSync(t.receipt).size, sha256: sha(fs.readFileSync(t.receipt)) };

  const attempts = [];

  // 5a: a second terminal directory, with the state left alone. The context cannot name it, so the attempt is made by editing the context
  // and pointing the engine at the edited copy -- which is what a caller would really do.
  {
    const alt = fx.dir + '/archive-alt';
    const ctxCopy = JSON.parse(fs.readFileSync(fx.contextPath, 'utf8'));
    ctxCopy.production_destination = alt;
    const altCtx = fx.dir + '/context-alt.json';
    fs.writeFileSync(altCtx, JSON.stringify(ctxCopy, null, 2) + '\n');
    const r = run(['archive', altCtx, t.plan, alt + '/payload.tar']);
    attempts.push({ what: 'a second transaction directory', argv_note: 'context edited to name archive-alt',
      status: r.status, why: r.refused ? r.refused.why : null, refused: r.status !== 0,
      refused_on_the_derived_identity: refusedFor(r, 'derive'), alt_dir_created: fs.existsSync(alt) });
  }

  // 5b: a second state file, likewise by editing the context.
  {
    const altState = fx.dir + '/publication-state-alt.jsonl';
    const ctxCopy = JSON.parse(fs.readFileSync(fx.contextPath, 'utf8'));
    ctxCopy.fixed_state_path = altState;
    const altCtx = fx.dir + '/context-altstate.json';
    fs.writeFileSync(altCtx, JSON.stringify(ctxCopy, null, 2) + '\n');
    const r = run(['seal', altCtx, t.payload, fx.destDir + '/receipt-altstate.json', t.plan]);
    attempts.push({ what: 'a second ledger', argv_note: 'context edited to name a second state file',
      status: r.status, why: r.refused ? r.refused.why : null, refused: r.status !== 0,
      refused_on_the_derived_identity: refusedFor(r, 'derive'), alt_state_created: fs.existsSync(altState) });
  }

  // 5c: the real context, a copy of the payload under a new name, a new receipt name. This is the "renamed receipt / copied payload"
  // route: nothing about the context changes, so only the content checks can stop it.
  {
    const copy = fx.destDir + '/payload-copy.tar';
    fs.copyFileSync(t.payload, copy);
    const r = run(['seal', fx.contextPath, copy, fx.destDir + '/receipt-copy.json', t.plan]);
    attempts.push({ what: 'a copied payload under a new receipt name', status: r.status, why: r.refused ? r.refused.why : null,
      refused: r.status !== 0, refused_as_already_sealed: refusedFor(r, 'already sealed'),
      receipt_written: fs.existsSync(fx.destDir + '/receipt-copy.json') });
  }

  // 5d: a second archive of a new plan in the sealed task.
  {
    const r = run(['plan', fx.contextPath, writeSpec(fx, { note: 'after seal' }), fx.dir + '/plan-after.json']);
    const a = run(['archive', fx.contextPath, fx.dir + '/plan-after.json', fx.destDir + '/payload-after.tar']);
    attempts.push({ what: 'a second archive after the seal', plan_status: r.status, status: a.status,
      why: a.refused ? a.refused.why : null, refused: a.status !== 0, refused_as_sealed: refusedFor(a, 'sealed'),
      payload_written: fs.existsSync(fx.destDir + '/payload-after.tar') });
  }

  // 5e: a new authority and context path -- a whole new pair of files claiming to be this task, at new paths, with the same task and root.
  {
    const newAuth = fx.dir + '/authority-new.json';
    fs.writeFileSync(newAuth, JSON.stringify({ task: fx.ctx.task, report_root: fx.dir, fresh: true }, null, 2) + '\n');
    const newCtx = fx.dir + '/context-new.json';
    fs.writeFileSync(newCtx, JSON.stringify({ ...fx.ctx, authority_record: newAuth, fresh: true }, null, 2) + '\n');
    const r = run(['archive', newCtx, t.plan, fx.destDir + '/payload-newctx.tar']);
    attempts.push({ what: 'a new authority and a new context path', status: r.status, why: r.refused ? r.refused.why : null,
      refused: r.status !== 0, refused_as_sealed_or_duplicate: refusedFor(r, 'sealed') || refusedFor(r, 'already been archived'),
      payload_written: fs.existsSync(fx.destDir + '/payload-newctx.tar') });
  }

  // 5f: a spec that claims the production task, from inside a TEST_ONLY context. This is the escape route by rename.
  {
    const r = run(['plan', fx.contextPath, writeSpec(fx, { task: 'GTOS-DSH-145' }), fx.dir + '/plan-prod.json']);
    attempts.push({ what: 'a spec that claims the production task', status: r.status, why: r.refused ? r.refused.why : null,
      refused: r.status !== 0, refused_on_task_identity: refusedFor(r, 'another task') });
  }

  const stateAfter = { bytes: fs.statSync(fx.statePath).size, sha256: sha(fs.readFileSync(fx.statePath)) };
  const payloadAfter = { bytes: fs.statSync(t.payload).size, sha256: sha(fs.readFileSync(t.payload)) };
  const receiptAfter = { bytes: fs.statSync(t.receipt).size, sha256: sha(fs.readFileSync(t.receipt)) };
  record({
    case: 'postseal', fixture: fx.name, task: fx.ctx.task,
    completed_transaction: completed,
    attempts,
    every_attempt_refused: attempts.every((a) => a.refused),
    state_unchanged: JSON.stringify(sealedState) === JSON.stringify(stateAfter),
    payload_unchanged: JSON.stringify(sealedPayload) === JSON.stringify(payloadAfter),
    receipt_unchanged: JSON.stringify(sealedReceipt) === JSON.stringify(receiptAfter),
    no_new_artifact: attempts.every((a) => !a.payload_written && !a.receipt_written && !a.alt_dir_created && !a.alt_state_created),
    ok: completed && attempts.every((a) => a.refused) && JSON.stringify(sealedState) === JSON.stringify(stateAfter)
      && JSON.stringify(sealedPayload) === JSON.stringify(payloadAfter)
      && JSON.stringify(sealedReceipt) === JSON.stringify(receiptAfter)
      && attempts.every((a) => !a.payload_written && !a.receipt_written && !a.alt_dir_created && !a.alt_state_created),
    ok_means: 'after one completed TEST_ONLY transaction, six simultaneous routes to a second one -- a new terminal directory, a new '
      + 'state file, a copied payload under a new receipt name, a second archive, a new authority and context path, and a spec claiming '
      + 'the production task -- were all refused, none wrote anything, and the sealed state, payload and receipt are byte-identical',
  });
}

// ---- case 6: faults that must each refuse on their own condition
{
  const faults = [];

  // 6a: a spec for another task, in a context whose own task is different.
  {
    const fx = makeFixture('fault-task', 'GTOS-DSH-FIXTASK-03');
    const r = run(['plan', fx.contextPath, writeSpec(fx, { task: 'GTOS-DSH-SOMETHING-ELSE-04' }), fx.dir + '/plan.json']);
    faults.push({ what: 'a spec for another task', status: r.status, why: r.refused ? r.refused.why : null,
      refused_on_task: refusedFor(r, 'another task'), ok: r.status !== 0 && refusedFor(r, 'another task') });
  }

  // 6b: a member changed to the same size with different bytes after the plan was frozen.
  {
    const fx = makeFixture('fault-samesize', 'GTOS-DSH-FIXDRIFT-05');
    run(['plan', fx.contextPath, writeSpec(fx), fx.dir + '/plan.json']);
    const victim = fx.members[2].from;
    const b = fs.readFileSync(victim);
    b[b.length - 2] = b[b.length - 2] === 0x20 ? 0x21 : 0x20;   // one byte, same length
    fs.writeFileSync(victim, b);
    const r = run(['archive', fx.contextPath, fx.dir + '/plan.json', fx.destDir + '/payload.tar']);
    faults.push({ what: 'a same-size byte drift after the plan', status: r.status, why: r.refused ? r.refused.why : null,
      refused_on_the_drift: refusedFor(r, 'changed after the plan'), payload_written: fs.existsSync(fx.destDir + '/payload.tar'),
      ok: r.status !== 0 && refusedFor(r, 'changed after the plan') && !fs.existsSync(fx.destDir + '/payload.tar') });
  }

  // 6c: a member changed to a different size after the plan was frozen.
  {
    const fx = makeFixture('fault-size', 'GTOS-DSH-FIXSIZE-06');
    run(['plan', fx.contextPath, writeSpec(fx), fx.dir + '/plan.json']);
    fs.appendFileSync(fx.members[1].from, 'x');
    const r = run(['archive', fx.contextPath, fx.dir + '/plan.json', fx.destDir + '/payload.tar']);
    faults.push({ what: 'a size drift after the plan', status: r.status, why: r.refused ? r.refused.why : null,
      refused_on_the_size: refusedFor(r, 'changed size after the plan'), payload_written: fs.existsSync(fx.destDir + '/payload.tar'),
      ok: r.status !== 0 && refusedFor(r, 'changed size after the plan') && !fs.existsSync(fx.destDir + '/payload.tar') });
  }

  // 6d: a planned member removed after the plan was frozen.
  {
    const fx = makeFixture('fault-missing', 'GTOS-DSH-FIXMISS-07');
    run(['plan', fx.contextPath, writeSpec(fx), fx.dir + '/plan.json']);
    fs.unlinkSync(fx.members[3].from);
    const r = run(['archive', fx.contextPath, fx.dir + '/plan.json', fx.destDir + '/payload.tar']);
    faults.push({ what: 'a planned member removed after the plan', status: r.status, why: r.refused ? r.refused.why : null,
      refused: r.status !== 0, payload_written: fs.existsSync(fx.destDir + '/payload.tar'),
      ok: r.status !== 0 && !fs.existsSync(fx.destDir + '/payload.tar') });
  }

  // 6e: a replay of a completed seal. Two of them, because the engine has two independent guards and a replay with everything intact
  // reaches only the first one: the receipt name already exists, so CreateNew refuses before the state is read. Deleting the receipt and
  // replaying again reaches the state guard, which is the one that stops a second transaction under a new name. A single replay would
  // have shown one guard and left the other unmeasured.
  {
    const fx = makeFixture('fault-replay', 'GTOS-DSH-FIXREPLAY-08');
    const t = completeTransaction(fx);
    const same = run(['seal', fx.contextPath, t.payload, t.receipt, t.plan]);
    fs.unlinkSync(t.receipt);
    const afterDelete = run(['seal', fx.contextPath, t.payload, t.receipt, t.plan]);
    faults.push({ what: 'a replay of a completed seal, everything intact', status: same.status,
      why: same.refused ? same.refused.why : null, refused: same.status !== 0,
      refused_by_the_output_name: refusedFor(same, 'CreateNew refused'),
      refused_by_the_state: refusedFor(same, 'already sealed'),
      ok: same.status !== 0 && (refusedFor(same, 'CreateNew refused') || refusedFor(same, 'already sealed')) });
    faults.push({ what: 'a replay of a completed seal with the receipt removed', status: afterDelete.status,
      why: afterDelete.refused ? afterDelete.refused.why : null, refused: afterDelete.status !== 0,
      refused_by_the_state: refusedFor(afterDelete, 'already sealed'),
      receipt_rewritten: fs.existsSync(t.receipt),
      ok: afterDelete.status !== 0 && refusedFor(afterDelete, 'already sealed') && !fs.existsSync(t.receipt) });
  }

  // 6f: an evidence file the seal must consume that says false in its own field.
  {
    const fx = makeFixture('fault-evidence-false', 'GTOS-DSH-FIXEVF-09');
    const req = writeEvidence(fx, 'required-set', true);
    const bad = writeEvidence(fx, 'consumer-chain', false);
    const spec = { task: fx.ctx.task, members: fx.members, required_set: req, evidence: [bad] };
    fs.writeFileSync(fx.specPath, JSON.stringify(spec, null, 2) + '\n');
    run(['plan', fx.contextPath, fx.specPath, fx.dir + '/plan.json']);
    run(['archive', fx.contextPath, fx.dir + '/plan.json', fx.destDir + '/payload.tar']);
    const r = run(['seal', fx.contextPath, fx.destDir + '/payload.tar', fx.destDir + '/receipt.json', fx.dir + '/plan.json']);
    faults.push({ what: 'an evidence file whose own field is false', status: r.status, why: r.refused ? r.refused.why : null,
      refused_on_evidence: refusedFor(r, 'cannot consume its evidence'), detail: r.refused ? r.refused.detail : null,
      receipt_written: fs.existsSync(fx.destDir + '/receipt.json'),
      ok: r.status !== 0 && refusedFor(r, 'cannot consume its evidence') && !fs.existsSync(fx.destDir + '/receipt.json') });
  }

  // 6g: an evidence file the seal must consume that is absent.
  {
    const fx = makeFixture('fault-evidence-absent', 'GTOS-DSH-FIXEVA-10');
    const req = writeEvidence(fx, 'required-set', true);
    const gone = writeEvidence(fx, 'consumer-chain', true);
    const spec = { task: fx.ctx.task, members: fx.members, required_set: req, evidence: [gone] };
    fs.writeFileSync(fx.specPath, JSON.stringify(spec, null, 2) + '\n');
    run(['plan', fx.contextPath, fx.specPath, fx.dir + '/plan.json']);
    run(['archive', fx.contextPath, fx.dir + '/plan.json', fx.destDir + '/payload.tar']);
    fs.unlinkSync(gone.path);
    const r = run(['seal', fx.contextPath, fx.destDir + '/payload.tar', fx.destDir + '/receipt.json', fx.dir + '/plan.json']);
    faults.push({ what: 'an evidence file that is absent', status: r.status, why: r.refused ? r.refused.why : null,
      refused_on_evidence: refusedFor(r, 'cannot consume its evidence'), detail: r.refused ? r.refused.detail : null,
      receipt_written: fs.existsSync(fx.destDir + '/receipt.json'),
      ok: r.status !== 0 && refusedFor(r, 'cannot consume its evidence') && !fs.existsSync(fx.destDir + '/receipt.json') });
  }

  // 6h: a receipt that names the wrong payload, so verify has to report the obligation false rather than a green.
  {
    const fx = makeFixture('fault-verify', 'GTOS-DSH-FIXVERIFY-11');
    const t = completeTransaction(fx);
    const rec = JSON.parse(fs.readFileSync(t.receipt, 'utf8'));
    rec.payload.sha256 = 'c'.repeat(64);
    const tampered = fx.dir + '/receipt-tampered.json';
    fs.writeFileSync(tampered, JSON.stringify(rec, null, 2) + '\n');
    const r = run(['verify', fx.contextPath, t.payload, tampered, t.plan]);
    faults.push({ what: 'a receipt that names another payload', status: r.status,
      verify_ok: r.json ? r.json.ok : null,
      failed_obligations: r.json ? r.json.failed_obligations : null,
      named_the_right_one: !!(r.json && r.json.failed_obligations && r.json.failed_obligations.includes('receipt_payload_sha256')),
      ok: r.status !== 0 && r.json && r.json.ok === false && r.json.failed_obligations
        && r.json.failed_obligations.includes('receipt_payload_sha256') });
  }

  record({
    case: 'faults',
    faults,
    all_refused: faults.every((f) => f.ok),
    distinct_conditions: [...new Set(faults.map((f) => f.why || '(verify reported false)'))],
    ok: faults.every((f) => f.ok),
    ok_means: 'each fault refused on its own condition -- another task, a same-size drift, a size drift, a missing member, a replay, '
      + 'evidence that says false, evidence that is absent, and a receipt naming another payload -- and no fault was accepted',
  });
}

// ---------------------------------------------------------------- the record
const out = {
  task: 'GTOS-DSH-145',
  what: 'the real publisher entry exercised in TEST_ONLY before any production step',
  engine: { path: ENGINE, bytes: fs.statSync(ENGINE).size, sha256: sha(fs.readFileSync(ENGINE)) },
  node: { path: NODE, version: process.version },
  fixture_root: TEST_ONLY,
  scope: 'every case runs in its own directory under the TEST_ONLY prefix; no case reads or writes the production state file or the '
    + 'production terminal directory',
  cases,
  counts: {
    cases: cases.length,
    prearchive_cases: cases.filter((c) => c.case.startsWith('prearchive/')).length,
    postseal_attempts: (cases.find((c) => c.case === 'postseal') || { attempts: [] }).attempts.length,
    fault_cases: (cases.find((c) => c.case === 'faults') || { faults: [] }).faults.length,
  },
  all_ok: cases.every((c) => c.ok === true),
  ok: cases.every((c) => c.ok === true),
  ok_means: 'the production entry, called exactly as the production run will call it, completed a valid transaction and refused every '
    + 'negative on the condition each negative is named after',
};
const target = R + '/f-fixtures-145' + TAG + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: out.ok, counts: out.counts,
  cases: cases.map((c) => ({ case: c.case, ok: c.ok })) }, null, 2));
process.exit(out.ok ? 0 : 1);
