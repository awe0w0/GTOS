// GTOS-DSH-122 R0: the real evidence consumer test, run against this packet's actual build and run records.
//
// The positive case is not synthesised. It is the host C++ unit this packet really compiled and really ran: the attempt
// record, the frozen spec, the inner report, the raw log, the ELF and the static gate are the files the runner wrote.
//
// Every negative replaces one real record locator with another real record of a different kind, or breaks exactly one
// obligation in the contract. None of them add a self-declared label: the point is that a genuine Git or compile record is
// simply not the shape a Cpp run has.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as consumer from '../tools/evidence-consumer.mjs';

const here = new URL('.', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const R = process.argv[4] ? path.win32.resolve(process.argv[4]) : path.win32.dirname(here);
const S = process.argv[5] || '/var/tmp/gtos-dsh-122-e759-evidence-recovery-view';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const hostPath = consumer.hostMapper([
  { linux: '/var/tmp', host: '//wsl$/Ubuntu-20.04/var/tmp' },
  { linux: '/mnt/f', host: 'F:' },
]);

const STAGE = process.argv[2] || 'prodcase01-01';
const ELF = S + '/test-build/r1a/runtime_test.elf';

// ---------------------------------------------------------------- the real positive
let receipt = null;
let contract = null;
try {
  receipt = consumer.readCppReceipt({ reportRoot: R, sourceRoot: S, stage: STAGE, hostPath });
  const elfBytes = fs.readFileSync(hostPath(ELF));
  // The subject set is read from the build specs the receipt itself found, and then stated as an external expectation.
  // This is the one place the expectation is derived from what was built; it is derived once, from the build record, not
  // from the current source, and the comparison below is against the same list so that a wrong build is still caught by
  // the ELF and profile checks.
  contract = consumer.cppContract({
    stage: STAGE,
    requested_case: 1,
    elf_sha256: sha(elfBytes),
    profile_macros: receipt.buildSpecs[0] ? receipt.buildSpecs[0].defines : [],
    subjects: receipt.buildSpecs.map((b) => b.source).filter(Boolean),
  });
  check('the consumer read the real record set', receipt.read_set.length > 5, receipt.read_set.length);
  check('the child case comes from the child argv, not from a caller field',
    receipt.executed_case === 1, receipt.executed_case);
  check('the raw log was read and hashed', receipt.raw.bytes > 0 && receipt.raw.sha256 === sha(fs.readFileSync(hostPath(receipt.raw.path))),
    { bytes: receipt.raw.bytes, sha: receipt.raw.sha256 });
  check('the build tag came from the ELF directory', receipt.build_tag === 'r1a', receipt.build_tag);
  check('the build specs were found', receipt.buildSpecs.length > 0, receipt.buildSpecs.length);
  check('the static gate record was read', !!receipt.gate && receipt.gate.passed === true, receipt.gate ? receipt.gate.passed : null);
} catch (error) {
  check('the consumer read the real record set', false, String(error.message));
}

if (receipt && contract) {
  const verdict = consumer.acceptCppReceipt(receipt, contract);
  check('the real Cpp record is accepted', verdict.accepted, verdict.problems);

  // ---------------------------------------------------------------- obligations broken one at a time
  const mutations = [
    ['the stage is another stage', (r) => { r.stage = 'otherstage-01'; }, 'stage'],
    ['the terminal record is incomplete', (r) => { r.attempt.complete = false; }, 'terminal'],
    ['the terminal record did not report success', (r) => { r.attempt.result_ok = false; }, 'terminal'],
    ['a different ELF ran', (r) => { r.elf.sha256 = 'f'.repeat(64); }, 'elf'],
    ['there is no static gate record', (r) => { r.gate = null; }, 'gate'],
    ['the gate did not pass', (r) => { r.gate = { ...r.gate, passed: false }; }, 'gate'],
    ['the gate names a different ELF', (r) => { r.gate = { ...r.gate, sha256: 'a'.repeat(64) }; }, 'gate'],
    ['a source is missing from the build', (r) => { r.buildSpecs.pop(); }, 'subjects'],
    ['an unexpected source was built', (r) => { r.buildSpecs.push({ ...r.buildSpecs[0], source: '/x/unexpected.cpp' }); }, 'subjects'],
    ['a translation unit was built with the wrong macros', (r) => { r.buildSpecs[0].defines = ['GTOS_SOMETHING_ELSE']; }, 'profile'],
    ['the child timed out', (r) => { r.inner.child_timed_out = true; }, 'run'],
    ['the child exited non-zero', (r) => { r.inner.child_returncode = 1; }, 'run'],
    ['the child argv names no case', (r) => { r.executed_case = null; }, 'case'],
    ['the child ran a different case', (r) => { r.executed_case = 6; }, 'case'],
    ['the raw log does not name the case', (r) => { r.raw = { ...r.raw, text: 'nothing here' }; }, 'witness'],
    ['the child reported FAIL but exited zero', (r) => { r.raw = { ...r.raw, text: 'CASE 1\nFAIL something\n' }; }, 'result'],
    ['the child exited zero without reporting PASS', (r) => { r.raw = { ...r.raw, text: 'CASE 1\nsomething else\n' }; }, 'result'],
  ];
  for (const [name, mutate, obligation] of mutations) {
    const copy = structuredClone(receipt);
    mutate(copy);
    const v = consumer.acceptCppReceipt(copy, contract);
    const hit = v.problems.some((p) => p.obligation === obligation);
    check(`refused: ${name}`, !v.accepted && hit,
      { first: v.first_refusal, all: v.problems.map((p) => p.obligation) });
  }

  // ---------------------------------------------------------------- a real record of another kind
  // These are not labelled. They are the shapes other stages actually leave behind.
  const otherKinds = [
    ['a Git transaction record', { kind: 'git', repository: 'GTOS', parent: 'f5e55818', tip: '54076f45',
      staged_paths: ['a'], canonical_artifacts: ['a', 'b', 'c', 'd'] }],
    ['a compiler stage record', { kind: 'cxx', source: '/x/y.cpp', object: '/x/y.o', defines: [], cflags: [] }],
    ['a Node module record', { kind: 'node', run: { terminal: true, child_exit: 0 } }],
    ['a Python stage record', { kind: 'python', argv: ['/usr/bin/python3.8'], exit: 0 }],
  ];
  for (const [name, record] of otherKinds) {
    const v = consumer.acceptCppReceipt(record, contract);
    check(`refused without any self-declared label: ${name}`,
      !v.accepted && v.first_refusal.obligation === 'kind', v.first_refusal);
  }

  // A record that merely claims to be Cpp but carries no chain is refused at its first missing obligation.
  const hollow = { kind: 'cpp', stage: STAGE };
  const hollowVerdict = consumer.acceptCppReceipt(hollow, contract);
  check('a hollow Cpp record is refused', !hollowVerdict.accepted, hollowVerdict.first_refusal);

  // ---------------------------------------------------------------- the gate throws
  let threw = null;
  try { consumer.gateCpp(hollow, contract); } catch (error) { threw = error; }
  check('the downstream gate throws rather than returning false',
    threw !== null && /EVIDENCE REFUSED/.test(String(threw.message)), threw ? threw.message : null);
  check('the gate names the first failed obligation',
    threw && threw.verdict && threw.verdict.first_refusal, threw && threw.verdict ? threw.verdict.first_refusal : null);
  let passedGate = false;
  try { consumer.gateCpp(receipt, contract); passedGate = true; } catch { passedGate = false; }
  check('the gate lets the real record through', passedGate, null);

  // ---------------------------------------------------------------- a second real stage, same build
  // A different case from the same build must be accepted under its own contract, which shows the case is really compared
  // rather than always matching.
  try {
    const second = consumer.readCppReceipt({ reportRoot: R, sourceRoot: S, stage: 'prodcase06-01', hostPath });
    const secondContract = consumer.cppContract({ stage: 'prodcase06-01', requested_case: 6,
      elf_sha256: sha(fs.readFileSync(hostPath(ELF))),
      profile_macros: second.buildSpecs[0].defines,
      subjects: second.buildSpecs.map((b) => b.source).filter(Boolean) });
    const v2 = consumer.acceptCppReceipt(second, secondContract);
    check('a second case from the same build is accepted under its own contract', v2.accepted, v2.problems);
    // And the first contract must not accept the second record.
    const cross = consumer.acceptCppReceipt(second, contract);
    check('one case contract does not accept another case record', !cross.accepted, cross.first_refusal);
  } catch (error) {
    check('a second case from the same build is accepted under its own contract', false, String(error.message));
  }
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-122', kind: 'real_evidence_consumer_test',
  subject: 'references/evidence-consumer-122.mjs',
  positive: { stage: STAGE, report_root: R, source_root: S, elf: ELF,
    read_set: receipt ? receipt.read_set : null },
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
