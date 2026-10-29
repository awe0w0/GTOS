// GTOS-DSH-148 F: the fixtures. Every case here runs the actual engine over a real transaction.
//
// The shape of a case is: build a complete, valid TEST_ONLY transaction (a real spec, a real plan, a real payload, real
// role records, a real required set), take the step under test through the engine, and require the engine to refuse for
// the obligation the case is about. "Refused" alone is not a pass: each case names the message it expects, so a refusal
// for a different reason -- a bad stage name, a missing directory, EEXIST on the wrong file -- fails the case. That is the
// distinction the 147 review made when it found its eleven publication negatives were testing "a payload that does not
// exist" rather than a task terminal.
//
// The positives come first. A negative suite over a transaction that never worked proves nothing about the negatives, so
// the first case is the full transaction, and every later case starts from a fixture that passed the same build.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { makeFixture, phase, readVerifyRecord, runEngine, step, SchemaRefused, engineIdentity } from './drive-148.mjs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const ROLES = ['prerequisite_full', 'producer_and_native_chain_full', 'host_chain_full', 'static_byte_gate_full',
  'guest_resource_semantics_full', 'independent_physical_closure', 'publication_and_related_tests'];
const SUBJECT = 'fixture-subject-' + 'a'.repeat(16);
const VERSION = '148.1';

// ---------------------------------------------------------------- building a real transaction
//
// The material is real bytes -- this file, the engine, the driver, the role records -- so the payload is a real tar over
// real files and the plan's hashes are the hashes of things that exist.
const writeRoleRecords = (fx, { roles = ROLES, okValue = true, subject = SUBJECT, version = VERSION } = {}) => {
  const dir = fx.dir + '/roles';
  fs.mkdirSync(dir, { recursive: true });
  const pins = [];
  for (const role of roles) {
    const record = {
      task: fx.task, role, subject, consumer_version: version,
      entry: 'roles-148.mjs#' + role,
      business_called: true,
      obligations: [{ name: 'the fixture role decided its own obligation', value: okValue }],
      ok: okValue,
      what: 'a fixture role record: the entry it names, the subject it judged, and the obligations it decided',
    };
    const p = dir + '/role-' + role + '.json';
    fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
    const b = fs.readFileSync(p);
    pins.push({ role, path: p, bytes: b.length, sha256: sha(b), ok_field: 'ok', subject, consumer_version: version,
      entry: record.entry });
  }
  return pins;
};

const writeRequiredSet = (fx, { members = 4, requiredNotRead = 0, okValue = true, independent = true } = {}) => {
  const p = fx.dir + '/required-set.json';
  const rec = {
    task: fx.task,
    ok: okValue,
    closure: {
      independent_of_spec: independent, independent_of_payload: independent,
      members, required_not_read: requiredNotRead, resolved: members - requiredNotRead,
    },
    what: 'the fixture required set, with the closure block the seal now requires',
  };
  fs.writeFileSync(p, JSON.stringify(rec, null, 2) + '\n', { flag: 'wx' });
  const b = fs.readFileSync(p);
  return { path: p, bytes: b.length, sha256: sha(b), ok_field: 'ok' };
};

// The complete valid transaction. `mutate` runs after the fixture is built and before any engine call, which is how a case
// damages exactly one thing in a transaction that is otherwise known to work.
export const buildTransaction = (num, mutate) => {
  const fx = makeFixture({ num });
  fs.mkdirSync(fx.dir + '/material', { recursive: true });
  const engine = engineIdentity();
  fs.copyFileSync(engine.path, fx.dir + '/material/publish-148.mjs');
  const driverSrc = path.dirname(engine.path) + '/drive-148.mjs';
  fs.copyFileSync(driverSrc, fx.dir + '/material/drive-148.mjs');
  fs.writeFileSync(fx.dir + '/material/notes.txt', 'fixture material: real bytes, so the payload is a real payload\n');

  const roles = writeRoleRecords(fx);
  const requiredSet = writeRequiredSet(fx);
  const spec = {
    task: fx.task,
    members: [
      { from: fx.dir + '/material/publish-148.mjs', path: 'engine/publish-148.mjs', role: 'publisher' },
      { from: fx.dir + '/material/drive-148.mjs', path: 'engine/drive-148.mjs', role: 'driver' },
      { from: fx.dir + '/material/notes.txt', path: 'material/notes.txt', role: 'material' },
      { from: fx.contextPath, path: 'control/publication-context.json', role: 'context' },
      ...roles.map((r) => ({ from: r.path, path: 'records/role-' + r.role + '.json', role: 'role_record' })),
      { from: requiredSet.path, path: 'control/required-set.json', role: 'required_set' },
    ],
    required_set: requiredSet,
    evidence: [],
    mandatory_roles: { roles },
  };
  const specPath = fx.dir + '/spec.json';
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n', { flag: 'wx' });
  const tx = {
    fx, spec, specPath, roles, requiredSet,
    planPath: fx.destDir + '/plan.json',
    payloadPath: fx.destDir + '/GTOS-DSH-' + fx.tag + '-payload.tar',
    receiptPath: fx.destDir + '/GTOS-DSH-' + fx.tag + '-seal.json',
    verifyPath: fx.dir + '/verify-' + fx.tag + '.json',
  };
  if (mutate) mutate(tx);
  return tx;
};

// The snapshot a negative has to prove it did not disturb: the bytes, hash and mtime of the original payload, receipt and
// state, plus every entry of the target directory.
const snapshot = (tx) => {
  const file = (p) => (exists(p) ? (() => { const b = fs.readFileSync(p); const st = fs.statSync(p);
    return { path: p, bytes: b.length, sha256: sha(b), mtime_ms: Math.round(st.mtimeMs) }; })() : null);
  const dir = (d) => (exists(d) ? fs.readdirSync(d).sort().map((n) => {
    const st = fs.statSync(d + '/' + n);
    return { name: n, bytes: st.isFile() ? st.size : null, is_file: st.isFile(), mtime_ms: Math.round(st.mtimeMs) };
  }) : null);
  return { payload: file(tx.payloadPath), receipt: file(tx.receiptPath), state: file(tx.fx.statePath),
    destination: dir(tx.fx.destDir) };
};

const sameSnapshot = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// ---------------------------------------------------------------- running
const runTx = (tx) => phase({ tag: 'f-' + tx.fx.tag, contextPath: tx.fx.contextPath, specPath: tx.specPath,
  planPath: tx.planPath, payloadPath: tx.payloadPath, receiptPath: tx.receiptPath, outPath: tx.verifyPath });

export const negatives = () => {
  const results = [];
  const record = (name, expect, detail) => results.push({ case: name, expect, ...detail,
    ok: detail.refused === true && detail.names_the_expected_obligation === true });

  // ---------------------------------------------------------------- 0. the positive control
  const p0 = buildTransaction('9148');
  const p0tx = runTx(p0);
  results.push({ case: 'positive-full-transaction', expect: null, accepted: p0tx.accepted,
    failed_checks: p0tx.failed_checks, verify_ok: p0tx.verify ? p0tx.verify.ok : null,
    obligation_count: p0tx.verify ? p0tx.verify.obligation_count : null,
    engine_unchanged: p0tx.engine.unchanged, ok: p0tx.accepted === true });

  // ---------------------------------------------------------------- 1. a real payload with no archive behind it
  // The payload is real and parseable: it is the payload case 0 archived. It is moved into a fresh transaction, so the
  // question is exactly "may this be sealed here", not "does this file exist".
  {
    const src = buildTransaction('9149');
    const first = runTx(src);
    const dst = buildTransaction('9150');
    fs.copyFileSync(src.payloadPath, dst.payloadPath);
    fs.copyFileSync(src.planPath, dst.planPath);
    const before = snapshot(dst);
    const r = step({ verb: 'seal', args: [dst.fx.contextPath, dst.payloadPath, dst.receiptPath, dst.planPath],
      expect: 'never archived in this task' });
    const after = snapshot(dst);
    record('real-payload-in-a-transaction-that-never-archived-it', 'never archived in this task',
      { ...r, first_transaction_accepted: first.accepted, payload_bytes: fs.statSync(dst.payloadPath).size,
        state_untouched: sameSnapshot(before, after) });
  }

  // 2. the same, with a state file that exists and is zero bytes. The transaction archives first, so the payload is the
  //    real archived payload; the ledger is then truncated to zero bytes, which is a state that parses as empty.
  {
    const dst = buildTransaction('9151');
    const planned = runEngine('plan', [dst.fx.contextPath, dst.specPath, dst.planPath]);
    const archived = runEngine('archive', [dst.fx.contextPath, dst.planPath, dst.payloadPath]);
    fs.writeFileSync(dst.fx.statePath, '');
    const before = snapshot(dst);
    const r = step({ verb: 'seal', args: [dst.fx.contextPath, dst.payloadPath, dst.receiptPath, dst.planPath],
      expect: 'never archived in this task' });
    record('a-legal-zero-byte-state', 'never archived in this task',
      { ...r, plan_ok: planned.status === 0, archive_ok: archived.status === 0,
        state_untouched: sameSnapshot(before, snapshot(dst)) });
  }

  // 3. a state that parses and holds another transaction's archive. The payload here is this transaction's own real
  //    payload; what is wrong is the ledger, which describes a different task.
  {
    const other = buildTransaction('9152');
    const otherTx = runTx(other);
    const dst = buildTransaction('9153');
    runEngine('plan', [dst.fx.contextPath, dst.specPath, dst.planPath]);
    runEngine('archive', [dst.fx.contextPath, dst.planPath, dst.payloadPath]);
    const otherRow = fs.readFileSync(other.fx.statePath, 'utf8');
    fs.writeFileSync(dst.fx.statePath, otherRow);
    const r = step({ verb: 'seal', args: [dst.fx.contextPath, dst.payloadPath, dst.receiptPath, dst.planPath],
      expect: 'never archived in this task' });
    record('a-state-that-holds-another-transactions-archive', 'never archived in this task',
      { ...r, other_transaction_accepted: otherTx.accepted });
  }

  // ---------------------------------------------------------------- 4. cardinality before the seal
  {
    const tx = buildTransaction('9154');
    const plan = runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    const arch = runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
    // A second, genuinely different payload: one more member, so a different ordinal digest and a different byte length.
    fs.writeFileSync(tx.fx.dir + '/material/second.txt', 'a second publication attempt\n', { flag: 'wx' });
    const spec2 = JSON.parse(fs.readFileSync(tx.specPath, 'utf8'));
    spec2.members.push({ from: tx.fx.dir + '/material/second.txt', path: 'material/second.txt', role: 'material' });
    const spec2Path = tx.fx.dir + '/spec-second.json';
    fs.writeFileSync(spec2Path, JSON.stringify(spec2, null, 2) + '\n', { flag: 'wx' });
    const plan2Path = tx.fx.destDir + '/plan-second.json';
    const payload2Path = tx.fx.destDir + '/second-payload.tar';
    const p2 = runEngine('plan', [tx.fx.contextPath, spec2Path, plan2Path]);
    const before = snapshot(tx);
    const r = step({ verb: 'archive', args: [tx.fx.contextPath, plan2Path, payload2Path],
      expect: 'already been archived once' });
    record('a-second-different-archive-before-the-seal', 'already been archived once',
      { ...r, first_archive_ok: arch.status === 0, second_plan_ok: p2.status === 0,
        second_payload_written: exists(payload2Path), first_transaction_untouched: sameSnapshot(before, snapshot(tx)),
        plan_ok: plan.status === 0 });
  }

  // 5. the same plan archived twice under two names.
  {
    const tx = buildTransaction('9155');
    runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
    const before = snapshot(tx);
    const r = step({ verb: 'archive', args: [tx.fx.contextPath, tx.planPath, tx.fx.destDir + '/again.tar'],
      expect: 'already been archived once' });
    record('the-same-plan-archived-twice-under-two-names', 'already been archived once',
      { ...r, untouched: sameSnapshot(before, snapshot(tx)) });
  }

  // ---------------------------------------------------------------- 6-14. the qualification gate
  //
  // The damage is applied BEFORE the plan is frozen, not after. That ordering is the point of these cases: the plan pins
  // the damaged record's bytes, so the seal cannot refuse on a hash mismatch -- it has to read the record and refuse
  // because of what the record says. A case that damaged the record after the plan would be testing the byte check again.
  const roleCases = [
    ['omitted-role', 'complete set of mandatory consumer roles', (tx) => {
      const six = tx.roles.filter((r) => r.role !== 'independent_physical_closure');
      tx.spec.mandatory_roles.roles = six;
      fs.writeFileSync(tx.specPath, JSON.stringify(tx.spec, null, 2) + '\n');
    }, 'plan'],
    ['role-record-says-false', 'cannot consume its mandatory consumer roles', (tx) => {
      const rec = JSON.parse(fs.readFileSync(tx.roles[2].path, 'utf8'));
      rec.ok = false;
      fs.writeFileSync(tx.roles[2].path, JSON.stringify(rec, null, 2) + '\n');
    }, 'seal'],
    ['role-record-judged-a-different-subject', 'different subject', (tx) => {
      const rec = JSON.parse(fs.readFileSync(tx.roles[4].path, 'utf8'));
      rec.subject = 'a different image';
      fs.writeFileSync(tx.roles[4].path, JSON.stringify(rec, null, 2) + '\n');
    }, 'seal'],
    ['role-record-written-by-an-older-consumer', 'different consumer version', (tx) => {
      const rec = JSON.parse(fs.readFileSync(tx.roles[0].path, 'utf8'));
      rec.consumer_version = '147.1';
      fs.writeFileSync(tx.roles[0].path, JSON.stringify(rec, null, 2) + '\n');
    }, 'seal'],
    ['role-record-names-another-entry', 'different business entry', (tx) => {
      const rec = JSON.parse(fs.readFileSync(tx.roles[1].path, 'utf8'));
      rec.entry = 'roles-148.mjs#somethingElse';
      fs.writeFileSync(tx.roles[1].path, JSON.stringify(rec, null, 2) + '\n');
    }, 'seal'],
    ['role-record-decided-nothing', 'decided no obligation', (tx) => {
      const rec = JSON.parse(fs.readFileSync(tx.roles[3].path, 'utf8'));
      rec.obligations = [];
      fs.writeFileSync(tx.roles[3].path, JSON.stringify(rec, null, 2) + '\n');
    }, 'seal'],
    ['role-record-imported-but-not-called', 'does not state that the business entry was actually called', (tx) => {
      const rec = JSON.parse(fs.readFileSync(tx.roles[5].path, 'utf8'));
      rec.business_called = false;
      fs.writeFileSync(tx.roles[5].path, JSON.stringify(rec, null, 2) + '\n');
    }, 'seal'],
    ['role-record-same-size-byte-drift', 'sha256 differs', (tx) => {
      // This one is deliberately the byte check: the record is damaged after the plan, at the same length, so the only
      // thing that can catch it is the hash the plan pinned.
      const b = fs.readFileSync(tx.roles[6].path);
      const at = b.indexOf(Buffer.from('"ok"'));
      b[at + 5] = b[at + 5] === 0x74 ? 0x66 : 0x74; // t <-> f, same length
      fs.writeFileSync(tx.roles[6].path, b);
    }, 'seal-after-plan'],
    ['role-record-absent', 'absent', (tx) => {
      fs.unlinkSync(tx.roles[2].path);
    }, 'seal-after-plan'],
  ];

  // The plan pins each role record's bytes. For a case that damages the record *before* the plan, the pin has to be
  // recomputed, or the seal would refuse on the hash and never reach the field the case is about.
  const repin = (tx) => {
    for (const pin of tx.roles) {
      const b = fs.readFileSync(pin.path);
      pin.bytes = b.length;
      pin.sha256 = sha(b);
    }
    tx.spec.mandatory_roles.roles = tx.roles;
    fs.writeFileSync(tx.specPath, JSON.stringify(tx.spec, null, 2) + '\n');
  };

  roleCases.forEach(([name, expect, damage, when], i) => {
    const tx = buildTransaction(String(9160 + i));
    if (when === 'seal-after-plan') {
      runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
      runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
      damage(tx);
    } else {
      damage(tx);
      if (when === 'seal') {
        repin(tx);
        // The plan and the archive have to succeed on the damaged transaction, otherwise the seal is not what refused.
        const planned = runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
        const archived = runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
        if (planned.status !== 0 || archived.status !== 0) {
          results.push({ case: name, expect, refused: false, names_the_expected_obligation: false,
            ok: false, note: 'the damaged transaction could not even be planned and archived, so the seal was never reached',
            plan_status: planned.status, archive_status: archived.status });
          return;
        }
      }
    }
    const before = snapshot(tx);
    const r = when === 'plan'
      ? step({ verb: 'plan', args: [tx.fx.contextPath, tx.specPath, tx.planPath], expect })
      : step({ verb: 'seal', args: [tx.fx.contextPath, tx.payloadPath, tx.receiptPath, tx.planPath], expect });
    record(name, expect, { ...r, untouched: sameSnapshot(before, snapshot(tx)) });
  });

  // ---------------------------------------------------------------- 15-16. the required set
  {
    const tx = buildTransaction('9180');
    delete tx.spec.required_set;
    fs.writeFileSync(tx.specPath, JSON.stringify(tx.spec, null, 2) + '\n');
    runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
    const r = step({ verb: 'seal', args: [tx.fx.contextPath, tx.payloadPath, tx.receiptPath, tx.planPath],
      expect: 'pins no required set' });
    record('the-plan-pins-no-required-set', 'pins no required set', r);
  }
  {
    const tx = buildTransaction('9181');
    // The required set is rewritten with a closure that admits it required two members it never read, and the plan is
    // frozen over that record, so the record still says ok: true and the seal has to read the closure to refuse it.
    const rec = JSON.parse(fs.readFileSync(tx.requiredSet.path, 'utf8'));
    rec.closure.required_not_read = 2;
    rec.closure.resolved = rec.closure.members - 2;
    fs.writeFileSync(tx.requiredSet.path, JSON.stringify(rec, null, 2) + '\n');
    tx.requiredSet = { path: tx.requiredSet.path, bytes: fs.statSync(tx.requiredSet.path).size,
      sha256: sha(fs.readFileSync(tx.requiredSet.path)), ok_field: 'ok' };
    tx.spec.required_set = tx.requiredSet;
    fs.writeFileSync(tx.specPath, JSON.stringify(tx.spec, null, 2) + '\n');
    const planned = runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    const archived = runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
    const before = snapshot(tx);
    const r = step({ verb: 'seal', args: [tx.fx.contextPath, tx.payloadPath, tx.receiptPath, tx.planPath],
      expect: 'is not a closure' });
    record('a-required-set-that-admits-unread-requirements', 'is not a closure',
      { ...r, plan_ok: planned.status === 0, archive_ok: archived.status === 0,
        untouched: sameSnapshot(before, snapshot(tx)) });
  }

  // ---------------------------------------------------------------- 17-18. drift between the plan and the archive
  {
    const tx = buildTransaction('9182');
    runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    const b = fs.readFileSync(tx.fx.dir + '/material/notes.txt');
    b[0] = b[0] === 0x66 ? 0x67 : 0x66; // same length, different byte
    fs.writeFileSync(tx.fx.dir + '/material/notes.txt', b);
    const r = step({ verb: 'archive', args: [tx.fx.contextPath, tx.planPath, tx.payloadPath],
      expect: 'changed after the plan was frozen' });
    record('a-member-changed-at-the-same-size-after-the-plan', 'changed after the plan was frozen',
      { ...r, payload_written: exists(tx.payloadPath) });
  }
  {
    const tx = buildTransaction('9183');
    runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    fs.appendFileSync(tx.fx.dir + '/material/notes.txt', 'and one more line\n');
    const r = step({ verb: 'archive', args: [tx.fx.contextPath, tx.planPath, tx.payloadPath],
      expect: 'changed size after the plan was frozen' });
    record('a-member-that-grew-after-the-plan', 'changed size after the plan was frozen', r);
  }

  // ---------------------------------------------------------------- 19-23. after the seal, nothing reopens it
  {
    const tx = buildTransaction('9184');
    const first = runTx(tx);
    const before = snapshot(tx);
    const tries = [];
    tries.push(step({ verb: 'archive', args: [tx.fx.contextPath, tx.planPath, tx.fx.destDir + '/after.tar'],
      expect: 'sealed, so nothing may be archived' }));
    tries.push(step({ verb: 'seal', args: [tx.fx.contextPath, tx.payloadPath, tx.fx.destDir + '/second-seal.json', tx.planPath],
      expect: 'already sealed' }));
    // A payload copy under another name: the same bytes, a different path.
    const copyPath = tx.fx.destDir + '/payload-copy.tar';
    fs.copyFileSync(tx.payloadPath, copyPath);
    tries.push(step({ verb: 'seal', args: [tx.fx.contextPath, copyPath, tx.fx.destDir + '/third-seal.json', tx.planPath],
      expect: 'already sealed' }));
    // A context copy at another path: same task, same root, so the same identity and the same state.
    const ctxCopy = tx.fx.dir + '/context-copy.json';
    fs.copyFileSync(tx.fx.contextPath, ctxCopy);
    tries.push(step({ verb: 'seal', args: [ctxCopy, tx.payloadPath, tx.fx.destDir + '/fourth-seal.json', tx.planPath],
      expect: 'already sealed' }));
    // A new spec and a new plan, then an archive of it.
    fs.writeFileSync(tx.fx.dir + '/material/late.txt', 'late\n', { flag: 'wx' });
    const spec2 = JSON.parse(fs.readFileSync(tx.specPath, 'utf8'));
    spec2.members.push({ from: tx.fx.dir + '/material/late.txt', path: 'material/late.txt', role: 'material' });
    const spec2Path = tx.fx.dir + '/spec-late.json';
    fs.writeFileSync(spec2Path, JSON.stringify(spec2, null, 2) + '\n', { flag: 'wx' });
    const plan2 = tx.fx.destDir + '/plan-late.json';
    const planned = runEngine('plan', [tx.fx.contextPath, spec2Path, plan2]);
    tries.push(step({ verb: 'archive', args: [tx.fx.contextPath, plan2, tx.fx.destDir + '/late.tar'],
      expect: 'sealed, so nothing may be archived' }));
    record('after-the-seal-nothing-reopens-the-transaction', 'sealed',
      { refused: tries.every((t) => t.refused), names_the_expected_obligation: tries.every((t) => t.names_the_expected_obligation),
        first_transaction_accepted: first.accepted, late_plan_ok: planned.status === 0,
        attempts: tries.map((t) => ({ verb: t.verb, argv_tail: t.argv.slice(-1)[0], refused: t.refused, refusal: t.refusal })),
        state_untouched: sameSnapshot(before, snapshot(tx)) });
  }

  // ---------------------------------------------------------------- 24-27. the schema propagates
  {
    // A role record changed after the seal: verify must fail on its own named obligation, and the phase must not accept.
    const tx = buildTransaction('9185');
    const first = runTx(tx);
    const victim = tx.roles[3];
    const rec = JSON.parse(fs.readFileSync(victim.path, 'utf8'));
    rec.subject = 'changed after the seal';
    fs.writeFileSync(victim.path, JSON.stringify(rec, null, 2) + '\n');
    const v = runEngine('verify', [tx.fx.contextPath, tx.payloadPath, tx.receiptPath, tx.planPath],
      { GTOS_VERIFY_OUT: tx.fx.dir + '/verify-after-change.json' });
    let parsed = null;
    try { parsed = JSON.parse(v.stdout); } catch { parsed = null; }
    const failed = parsed && Array.isArray(parsed.failed_obligations) ? parsed.failed_obligations : [];
    record('a-role-record-changed-after-the-seal', 'mandatory_role_records_are_unchanged',
      { refused: v.status === 1 && parsed !== null && parsed.ok === false,
        names_the_expected_obligation: failed.includes('mandatory_role_records_are_unchanged'),
        verify_ok: parsed ? parsed.ok : null, failed_obligations: failed, first_transaction_accepted: first.accepted });
  }
  {
    // The payload truncated after the seal: bytes_match fails, and every reader of the record sees ok:false.
    const tx = buildTransaction('9186');
    const first = runTx(tx);
    const b = fs.readFileSync(tx.payloadPath);
    fs.writeFileSync(tx.payloadPath, b.subarray(0, b.length - 1024));
    const v = runEngine('verify', [tx.fx.contextPath, tx.payloadPath, tx.receiptPath, tx.planPath],
      { GTOS_VERIFY_OUT: tx.fx.dir + '/verify-after-truncation.json' });
    let parsed = null;
    try { parsed = JSON.parse(v.stdout); } catch { parsed = null; }
    const failed = parsed && Array.isArray(parsed.failed_obligations) ? parsed.failed_obligations : [];
    record('a-payload-truncated-after-the-seal', 'receipt_payload_bytes',
      { refused: v.status === 1 && parsed !== null && parsed.ok === false,
        names_the_expected_obligation: failed.includes('receipt_payload_bytes') || failed.includes('bytes_match'),
        verify_ok: parsed ? parsed.ok : null, failed_obligations: failed, first_transaction_accepted: first.accepted });
  }
  {
    // A verify record with a field removed. The reader must refuse it rather than read what is left.
    const tx = buildTransaction('9187');
    const first = runTx(tx);
    const rec = JSON.parse(fs.readFileSync(tx.verifyPath, 'utf8'));
    const stripped = { ...rec };
    delete stripped.ok;
    delete stripped.schema_version;
    const strippedPath = tx.fx.dir + '/verify-stripped.json';
    fs.writeFileSync(strippedPath, JSON.stringify(stripped, null, 2) + '\n', { flag: 'wx' });
    let refusal = null;
    try { readVerifyRecord(strippedPath); } catch (e) { refusal = e instanceof SchemaRefused ? e.problems : null; }
    record('a-verify-record-with-its-fields-removed', 'ok',
      { refused: refusal !== null && refusal.length > 0,
        names_the_expected_obligation: refusal !== null && refusal.some((p) => p.field === 'ok'),
        problems: refusal, first_transaction_accepted: first.accepted });
  }
  {
    // The reader must also refuse a record from another generation.
    const tx = buildTransaction('9188');
    const first = runTx(tx);
    const rec = JSON.parse(fs.readFileSync(tx.verifyPath, 'utf8'));
    rec.schema_version = '147.1';
    const p = tx.fx.dir + '/verify-other-generation.json';
    fs.writeFileSync(p, JSON.stringify(rec, null, 2) + '\n', { flag: 'wx' });
    let refusal = null;
    try { readVerifyRecord(p); } catch (e) { refusal = e instanceof SchemaRefused ? e.problems : null; }
    record('a-verify-record-from-another-generation', 'schema_version',
      { refused: refusal !== null, names_the_expected_obligation: refusal !== null && refusal.some((x) => x.field === 'schema_version'),
        problems: refusal, first_transaction_accepted: first.accepted });
  }

  // ---------------------------------------------------------------- 28-33. the identity is not the caller's
  {
    const tx = buildTransaction('9189');
    const ctx = JSON.parse(fs.readFileSync(tx.fx.contextPath, 'utf8'));
    ctx.fixed_state_path = tx.fx.dir + '/a-ledger-i-chose.jsonl';
    const p = tx.fx.dir + '/context-with-a-ledger.json';
    fs.writeFileSync(p, JSON.stringify(ctx, null, 2) + '\n', { flag: 'wx' });
    record('a-context-that-names-its-own-ledger', 'does not name the state file this task and root derive',
      step({ verb: 'state', args: [p], expect: 'does not name the state file' }));
  }
  {
    const tx = buildTransaction('9190');
    const ctx = JSON.parse(fs.readFileSync(tx.fx.contextPath, 'utf8'));
    ctx.production_destination = tx.fx.dir + '/a-transaction-i-chose';
    const p = tx.fx.dir + '/context-with-a-destination.json';
    fs.writeFileSync(p, JSON.stringify(ctx, null, 2) + '\n', { flag: 'wx' });
    record('a-context-that-names-its-own-transaction', 'does not name the terminal directory',
      step({ verb: 'state', args: [p], expect: 'does not name the terminal directory' }));
  }
  {
    const tx = buildTransaction('9191');
    const r = step({ verb: 'state', args: [tx.fx.contextPath, 'a-second-ledger.jsonl'], expect: 'exact arity' });
    record('a-second-argument-to-a-verb-that-takes-none', 'exact arity', r);
  }
  {
    const outside = R + '/test-only-outside-' + Date.now();
    fs.mkdirSync(outside, { recursive: true });
    const ctx = { task: 'GTOS-DSH-9192', scope: 'test_only', authorized_report_root: outside,
      fixed_state_path: outside + '/publication-state-9192.jsonl',
      production_destination: outside + '/archive-9192' };
    const p = outside + '/context.json';
    fs.writeFileSync(p, JSON.stringify(ctx, null, 2) + '\n', { flag: 'wx' });
    record('a-test-only-context-outside-the-prefix', 'must live under the TEST_ONLY prefix',
      step({ verb: 'state', args: [p], expect: 'must live under the TEST_ONLY prefix' }));
  }
  {
    const tx = buildTransaction('9193');
    const ctx = JSON.parse(fs.readFileSync(tx.fx.contextPath, 'utf8'));
    ctx.task = 'GTOS-DSH-148';
    ctx.fixed_state_path = tx.fx.dir + '/publication-state-148.jsonl';
    ctx.production_destination = tx.fx.dir + '/archive-148';
    const p = tx.fx.dir + '/context-claiming-the-production-task.json';
    fs.writeFileSync(p, JSON.stringify(ctx, null, 2) + '\n', { flag: 'wx' });
    record('a-test-only-context-claiming-the-production-task', 'may not claim the production task',
      step({ verb: 'state', args: [p], expect: 'may not claim the production task' }));
  }
  {
    // A copy of the production context at another path. The production identity is the path and the bytes together, so a
    // copy is not the production context, and the engine refuses before writing anything.
    const prod = R + '/publication-context-148.json';
    const copy = R + '/test-only/production-context-copy.json';
    fs.mkdirSync(R + '/test-only', { recursive: true });
    fs.copyFileSync(prod, copy);
    const r = step({ verb: 'state', args: [copy], expect: 'not the production context' });
    const prodStateExists = exists(R + '/publication-state-148.jsonl');
    record('a-copy-of-the-production-context', 'not the production context', { ...r, production_state_created: prodStateExists });
  }
  {
    const tx = buildTransaction('9194');
    runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
    const r = step({ verb: 'archive', args: [tx.fx.contextPath, tx.planPath, tx.fx.dir + '/outside.tar'],
      expect: 'outside the directory this task fixes' });
    record('a-payload-written-outside-the-transaction', 'outside the directory this task fixes',
      { ...r, written: exists(tx.fx.dir + '/outside.tar') });
  }
  {
    const tx = buildTransaction('9195');
    fs.writeFileSync(tx.planPath, '{}\n', { flag: 'wx' });
    const r = step({ verb: 'plan', args: [tx.fx.contextPath, tx.specPath, tx.planPath], expect: 'output already exists' });
    record('a-plan-output-that-already-exists', 'output already exists', r);
  }

  const failed = results.filter((r) => !r.ok);
  return {
    task: 'GTOS-DSH-148', kind: 'f_negatives',
    what: 'every negative the packet names for publication, each taken through the actual engine over a transaction that '
      + 'was built complete and valid first, with the refusal message checked against the obligation under test',
    engine: engineIdentity(),
    cases: results.length,
    passed: results.filter((r) => r.ok).length,
    failed,
    results,
    ok: failed.length === 0,
    ok_means: 'every case was refused by the engine and the refusal named the obligation the case damaged. A case that was '
      + 'refused for another reason, or not refused at all, is in failed',
  };
};

// ---------------------------------------------------------------- CLI
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) {
  const outPath = process.argv[2];
  const result = negatives();
  const text = JSON.stringify(result, null, 2) + '\n';
  if (outPath) {
    if (exists(outPath)) throw new Error('REFUSED: ' + outPath);
    fs.writeFileSync(outPath, text, { flag: 'wx' });
  }
  console.log(JSON.stringify({ cases: result.cases, passed: result.passed, ok: result.ok,
    failed: result.failed.map((f) => ({ case: f.case, expect: f.expect, refusal: f.refusal })) }, null, 2));
  process.exit(result.ok ? 0 : 1);
}
