// GTOS-DSH-149: the pre-archive rehearsal.
//
// WHY THIS EXISTS. The 148 review's sixth finding was that three meaningful corrections were committed AFTER the seal, which
// means the bytes that were sealed were not the bytes that had been qualified. The taskbook's answer is that every
// qualification program must be tested, frozen and committed before the archive is taken. The post-seal receipt is one of
// those programs, and it cannot be tested by the production seal: by the time that seal exists, running the receipt is no
// longer a test, it is the thing itself. So this file takes the same transaction the production run will take -- a real
// context, a real spec over the real package tree, a real plan, a real archive, a real seal, a real verify -- under a
// TEST_ONLY identity, and then runs the post-seal receipt over that payload.
//
// WHAT IT PROVES, AND WHAT IT DOES NOT. It proves that the receipt's code path works on a payload with the production
// payload's shape: the same member names, the same `engine/` harness, the same `judgements/` records, the same required set.
// It does NOT prove anything about the production payload, and it is not offered as evidence about it -- the receipt that
// matters is the one the sealed payload gets. What it removes is the 148 situation, where the receipt met its payload for
// the first time after the seal was already written.
//
// The transaction is a real one: a fixture identity is a task identity of its own, the engine refuses a TEST_ONLY context
// that claims the production task, and nothing here touches the production state file or the production destination.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { makeFixture, phase, rotateFixtureRoot } from './drive-149.mjs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/));
const nextFree = (series) => {
  for (let i = 1; i < 100; i += 1) {
    const p = R + '/' + series + '-' + String(i).padStart(2, '0') + '.json';
    if (!exists(p)) return p;
  }
  throw new Error('no free record name in the series ' + series);
};
const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const PKG = R + '/package-149';
const HERE = path.dirname(fileURLToPath(import.meta.url));

// The seven roles the seal requires, and where the package carries the record each one wrote.
const ROLES = ['prerequisite_full', 'producer_and_native_chain_full', 'host_chain_full', 'static_byte_gate_full',
  'guest_resource_semantics_full', 'independent_physical_closure', 'publication_and_related_tests'];

const roleEntry = (role) => {
  const p = PKG + '/judgements/roles-149/' + role + '/' + role + '.json';
  if (!exists(p)) throw new Error('the package carries no record for the role ' + role + ' at ' + p);
  const b = fs.readFileSync(p);
  const rec = readJson(p);
  for (const k of ['subject', 'consumer_version', 'entry']) {
    if (typeof rec[k] !== 'string' || rec[k].length === 0) {
      throw new Error('the packaged record for ' + role + ' pins no ' + k + ', so the seal would refuse it');
    }
  }
  return { role, path: p, bytes: b.length, sha256: sha(b), ok_field: 'ok',
    subject: rec.subject, consumer_version: rec.consumer_version, entry: rec.entry };
};

const requiredSetEntry = () => {
  const dir = PKG + '/judgements';
  const held = fs.readdirSync(dir).filter((n) => /^required-set-149-\d+\.json$/.test(n)).sort()
    .map((n) => ({ n, p: dir + '/' + n })).filter((x) => readJson(x.p).ok === true);
  if (!held.length) throw new Error('the package carries no required set that held');
  const last = held[held.length - 1];
  const b = fs.readFileSync(last.p);
  return { path: last.p, bytes: b.length, sha256: sha(b), ok_field: 'ok', name: last.n };
};

// Every member of the package, at the same relative path. This is the point of the rehearsal: the payload the receipt sees
// has to be the payload the receipt will see, or the rehearsal tests a different transaction.
const membersOf = (root) => {
  const out = [];
  const walk = (d, rel) => {
    for (const n of fs.readdirSync(d).sort()) {
      const full = d + '/' + n;
      const r = rel ? rel + '/' + n : n;
      if (fs.statSync(full).isDirectory()) { walk(full, r); continue; }
      out.push({ from: full, path: r, role: 'member' });
    }
  };
  walk(root, '');
  return out;
};

const rehearsal = () => {
  rotateFixtureRoot();
  const fx = makeFixture({ num: '9499' });
  const roles = ROLES.map(roleEntry);
  const requiredSet = requiredSetEntry();
  const members = membersOf(PKG);
  const spec = {
    task: fx.task,
    members,
    required_set: { path: requiredSet.path, bytes: requiredSet.bytes, sha256: requiredSet.sha256,
      ok_field: requiredSet.ok_field },
    evidence: [],
    mandatory_roles: { roles },
    what: 'a TEST_ONLY rehearsal of the production transaction shape: the package tree as the payload, the package\'s own '
      + 'role records and required set as the seal\'s inputs',
  };
  const specPath = fx.dir + '/spec.json';
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n', { flag: 'wx' });
  const tx = {
    tag: 'r' + fx.tag,
    contextPath: fx.contextPath,
    specPath,
    planPath: fx.destDir + '/plan.json',
    payloadPath: fx.destDir + '/GTOS-DSH-' + fx.tag + '-payload.tar',
    receiptPath: fx.destDir + '/GTOS-DSH-' + fx.tag + '-seal.json',
    outPath: fx.dir + '/verify-' + fx.tag + '.json',
  };
  const taken = phase(tx);

  // The post-seal receipt, over the payload the rehearsal just sealed. It runs as a child, exactly as it will after the
  // production seal, with its run directory declared so it does not write into the archive tree that does not exist yet.
  //
  // The receipt's record name is taken from the series rather than written down, because the receipt refuses to overwrite a
  // record: a fixed path would make the second rehearsal fail on CreateNew and then compare against the first rehearsal's
  // receipt, which is the shape of a green result that measured nothing.
  const runDir = fx.dir + '/postseal';
  const recordPath = nextFree('rehearse-postseal-149');
  const child = spawnSync(process.execPath, [HERE + '/postseal-149.mjs', tx.payloadPath, tx.planPath, tx.receiptPath], {
    encoding: 'utf8',
    env: { ...process.env, GTOS_POSTSEAL_RUN: runDir, GTOS_POSTSEAL_RECORD: recordPath },
    windowsHide: true, maxBuffer: 64 * 1024 * 1024,
  });
  const receipt = exists(recordPath) ? readJson(recordPath) : null;

  const checks = {
    the_rehearsal_transaction_was_accepted: taken.accepted === true,
    the_rehearsal_payload_is_the_package_tree: members.length > 1000,
    the_postseal_ran: child.status !== null,
    the_postseal_wrote_a_receipt: receipt !== null,
    the_receipt_extracted_every_member: !!(receipt && receipt.extracted
      && receipt.extracted.identical_to_the_payload === receipt.extracted.members),
    the_receipt_found_no_problem: !!(receipt && receipt.ok === true),
    the_roles_were_re_run_from_the_payload: !!(receipt && receipt.roles && receipt.roles.ran === true
      && receipt.roles.green === 7),
    the_re_run_reproduced_the_payload_records: !!(receipt && receipt.roles
      && receipt.roles.agrees_with_the_payload && receipt.roles.agrees_with_the_payload.ok === true),
    the_forbidden_reads_were_measured_and_zero: !!(receipt && receipt.no_fallback
      && receipt.no_fallback.forbidden_reads_measured === true && receipt.no_fallback.forbidden_reads === 0),
    the_chain_was_re_judged_from_the_payload: !!(receipt && receipt.chain && receipt.chain.ok === true
      && receipt.chain.judge_is_the_payload_member === true),
  };
  const failed = Object.entries(checks).filter(([, v]) => v !== true).map(([k]) => k);
  return {
    task: 'GTOS-DSH-149', kind: 'pre_archive_rehearsal',
    what: 'the production transaction shape taken under a TEST_ONLY identity and then handed to the post-seal receipt, so the '
      + 'receipt is exercised on a payload of the production payload\'s shape before the production archive exists',
    fixture: { dir: fx.dir, task: fx.task, members: members.length },
    transaction: { accepted: taken.accepted, failed_checks: taken.failed_checks,
      verify_ok: taken.verify ? taken.verify.ok : null,
      obligation_count: taken.verify ? taken.verify.obligation_count : null,
      payload_bytes: exists(tx.payloadPath) ? fs.statSync(tx.payloadPath).size : null,
      member_count: taken.seal ? taken.seal.member_count : null },
    postseal: { record: recordPath, status: child.status, ok: receipt ? receipt.ok === true : null,
      problem_count: receipt ? receipt.problem_count : null,
      problems: receipt ? receipt.problems : null,
      roles_green: receipt && receipt.roles ? receipt.roles.green : null,
      forbidden_reads: receipt && receipt.no_fallback ? receipt.no_fallback.forbidden_reads : null,
      stdout_tail: (child.stdout || '').slice(-1200), stderr_tail: (child.stderr || '').slice(-1200) },
    checks, failed_checks: failed,
    ok: failed.length === 0,
    ok_means: 'the receipt program takes a payload of the production payload\'s shape, extracts it, re-executes the roles the '
      + 'payload carries against it, reproduces the decisions the payload records, and measures zero forbidden reads -- all '
      + 'before the production archive exists, so the sealed payload is not the first payload the receipt ever saw',
  };
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) {
  const outPath = process.argv[2];
  const result = rehearsal();
  const text = JSON.stringify(result, null, 2) + '\n';
  if (outPath) { if (exists(outPath)) throw new Error('REFUSED: ' + outPath); fs.writeFileSync(outPath, text, { flag: 'wx' }); }
  console.log(JSON.stringify({ ok: result.ok, failed: result.failed_checks, transaction: result.transaction,
    postseal: { ok: result.postseal.ok, problems: result.postseal.problems,
      roles_green: result.postseal.roles_green, forbidden_reads: result.postseal.forbidden_reads },
    checks: result.checks }, null, 2));
  process.exit(result.ok ? 0 : 1);
}
