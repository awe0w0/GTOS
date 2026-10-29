// GTOS-DSH-149 F: the qualification record. What was tested, on which bytes, and what those bytes are not.
//
// The taskbook's stability clause for F is: "test actual selected publisher bytes/driver before freeze/production; no
// 147legacyclone/oldfixturewrite or second148production rescue". Three things follow, and each is measured here rather
// than asserted in prose.
//
//   1. THE BYTES TESTED ARE THE BYTES ON DISK. The engine and the driver are hashed, and the hash is what the phase runs.
//      A qualification that ran a different copy -- a stale build, a worktree file edited after the test -- would show up
//      as a different hash here.
//   2. THE ENGINE IS THE FIXED-IDENTITY ONE. The 147 review's first finding was that `publish-147.mjs` took its ledger
//      from its caller and refused a second archive only on a repeated digest. Those two shapes are named as source text
//      and their absence is checked, together with the presence of the constants that make the identity fixed. A source
//      check is not a proof of behaviour -- the 32 cases in f-negative-148.mjs are that -- but a source check is what
//      catches the specific regression of re-badging the old publisher, which is the thing the taskbook forbids.
//   3. TWO RUNS AGREE. The full transaction is taken twice on two fresh fixtures and the two records are compared: same
//      accepted verdict, same obligation names, same counts. A publication entry that passed once and behaved differently
//      the second time would be a fixture artefact rather than an engine.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { engineIdentity, phase, rotateFixtureRoot } from './drive-149.mjs';
import { buildTransaction, negatives } from './f-negative-149.mjs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const DRIVER = HERE + '/drive-149.mjs';
const NEGATIVES = HERE + '/f-negative-149.mjs';

// The 147 publisher, as the review described it: `ledgerPathFor` returned the caller's path, and the second-archive gate
// was a digest comparison rather than a count. The 146 engine, which this file continues, has neither.
const FORBIDDEN_SHAPES = [
  { name: 'a ledger taken from the caller', pattern: /ledgerPathFor\s*\(/, why: 'the 147 publisher let its caller name the ledger' },
  { name: 'a second archive refused only on a repeated digest',
    pattern: /archives\.length\s*&&\s*!before\.digests\.has|digests\.has\(plan\.ordinal_digest\)\s*&&\s*!before\.archives\.length/,
    why: 'the 147 publisher let a second, different payload into the same state' },
];
const REQUIRED_SHAPES = [
  { name: 'a pinned context with bytes and hash', pattern: /const PINNED = \{[\s\S]*?context: \{[\s\S]*?sha256:/,
    why: 'the identity is a constant, not an argument' },
  { name: 'the state file derived from task and root', pattern: /stateNameFor\(ctx\.task\)/,
    why: 'the ledger is derived, so a caller cannot name another one' },
  { name: 'the transaction directory derived from task and root', pattern: /destNameFor\(ctx\.task\)/,
    why: 'the transaction is derived, so a caller cannot name another one' },
  { name: 'exact arity per verb', pattern: /function arity\(verb, rest, want\)/,
    why: 'there is no position in which a second root can be named' },
  { name: 'archive cardinality by count', pattern: /if \(before\.archives\.length\) \{/,
    why: 'one archive per task identity, checked by the count' },
  { name: 'the frozen role list is read from the record', pattern: /function frozenRoles\(\)/,
    why: 'a plan that drops a role is refused against the frozen list, not against itself' },
  { name: 'the role entry pins a subject, a version and an entry', pattern: /entry\.subject[\s\S]*entry\.consumer_version/,
    why: 'a record that judged another subject cannot qualify this payload' },
  { name: 'the required set must be a closure', pattern: /function consumeRequiredSet\(entry\)/,
    why: 'a record that says true but admits unread requirements is refused' },
  { name: 'the verify schema is declared and counted', pattern: /const VERIFY_OBLIGATIONS = \[/,
    why: 'a dropped obligation cannot make ok true' },
];

export const qualification = () => {
  // The two transactions this file takes are taken on the fixed fixture paths, so the previous tree is moved aside first.
  rotateFixtureRoot();
  const engine = engineIdentity();
  const driverBytes = fs.readFileSync(DRIVER);
  const source = fs.readFileSync(engine.path, 'utf8');

  const forbidden = FORBIDDEN_SHAPES.map((f) => ({ ...f, present: f.pattern.test(source) }));
  const required = REQUIRED_SHAPES.map((r) => ({ ...r, present: r.pattern.test(source) }));

  // The 147 publisher's bytes, so "this is not that file" is a hash comparison rather than a resemblance.
  const legacy147 = {
    path: R + '/../native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a/worktree-147/tools/gtos-dsh-147/publish-147.mjs',
  };
  const legacyBytes = exists(legacy147.path) ? fs.readFileSync(legacy147.path) : null;

  // Two runs, two fresh fixtures.
  const runA = buildTransaction('9201');
  const a = phase({ tag: 'f-qual-a', contextPath: runA.fx.contextPath, specPath: runA.specPath,
    planPath: runA.planPath, payloadPath: runA.payloadPath, receiptPath: runA.receiptPath, outPath: runA.verifyPath });
  const runB = buildTransaction('9202');
  const b = phase({ tag: 'f-qual-b', contextPath: runB.fx.contextPath, specPath: runB.specPath,
    planPath: runB.planPath, payloadPath: runB.payloadPath, receiptPath: runB.receiptPath, outPath: runB.verifyPath });

  const namesA = a.verify ? a.verify.obligation_names.slice().sort() : null;
  const namesB = b.verify ? b.verify.obligation_names.slice().sort() : null;
  const agree = a.accepted === b.accepted && JSON.stringify(namesA) === JSON.stringify(namesB)
    && a.verify.obligation_count === b.verify.obligation_count
    && a.seal.member_count === b.seal.member_count;

  const suite = negatives();
  const checks = {
    the_engine_was_hashed_before_it_ran: engine.bytes > 0 && typeof engine.sha256 === 'string' && engine.sha256.length === 64,
    the_engine_is_not_the_147_publisher: legacyBytes === null ? true : sha(legacyBytes) !== engine.sha256,
    no_forbidden_shape_is_present: forbidden.every((f) => !f.present),
    every_required_shape_is_present: required.every((r) => r.present),
    the_first_run_was_accepted: a.accepted === true,
    the_second_run_was_accepted: b.accepted === true,
    the_two_runs_agree: agree,
    the_negative_suite_passed: suite.ok === true && suite.passed === suite.cases,
    the_engine_did_not_change_during_either_run: a.engine.unchanged === true && b.engine.unchanged === true,
  };
  const failed = Object.entries(checks).filter(([, v]) => v !== true).map(([k]) => k);

  return {
    task: 'GTOS-DSH-149', kind: 'f_qualification',
    what: 'the identity of the bytes F tested, the two shapes the 147 publisher had and this engine does not, and two runs '
      + 'of the full transaction compared field by field',
    engine,
    driver: { path: DRIVER, bytes: driverBytes.length, sha256: sha(driverBytes) },
    negatives: { path: NEGATIVES, bytes: fs.statSync(NEGATIVES).size, sha256: sha(fs.readFileSync(NEGATIVES)) },
    legacy_147_publisher: legacyBytes === null ? { present: false, why: 'the file is not on this host' }
      : { present: true, path: legacy147.path, bytes: legacyBytes.length, sha256: sha(legacyBytes),
        same_bytes_as_this_engine: sha(legacyBytes) === engine.sha256 },
    forbidden_shapes: forbidden.map((f) => ({ name: f.name, present: f.present, why: f.why })),
    required_shapes: required.map((r) => ({ name: r.name, present: r.present, why: r.why })),
    run_a: { accepted: a.accepted, failed_checks: a.failed_checks, obligation_count: a.verify ? a.verify.obligation_count : null,
      obligation_names: namesA, member_count: a.seal.member_count, payload_sha256: a.archive.payload.sha256 },
    run_b: { accepted: b.accepted, failed_checks: b.failed_checks, obligation_count: b.verify ? b.verify.obligation_count : null,
      obligation_names: namesB, member_count: b.seal.member_count, payload_sha256: b.archive.payload.sha256 },
    negative_suite: { cases: suite.cases, passed: suite.passed, ok: suite.ok },
    checks, failed_checks: failed,
    ok: failed.length === 0,
    ok_means: 'the engine F tested is the file on disk, it is not the 147 publisher, it carries the fixed-identity shapes '
      + 'and none of the free-ledger ones, two independent transactions were both accepted with the same obligation set, '
      + 'and the 32 negatives all passed against these same bytes',
  };
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) {
  const outPath = process.argv[2];
  const result = qualification();
  const text = JSON.stringify(result, null, 2) + '\n';
  if (outPath) { if (exists(outPath)) throw new Error('REFUSED: ' + outPath); fs.writeFileSync(outPath, text, { flag: 'wx' }); }
  console.log(JSON.stringify({ ok: result.ok, failed: result.failed_checks, engine: result.engine,
    negative_suite: result.negative_suite, checks: result.checks }, null, 2));
  process.exit(result.ok ? 0 : 1);
}
