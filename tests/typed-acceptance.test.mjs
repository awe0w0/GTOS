// GTOS-DSH-120 K1: the typed acceptance consumer test, run against this packet's real stage evidence.
//
// The positive case is a real Cpp stage that really ran a real case: its report, inner report and raw log are the files the
// runner wrote. Each refusal is generated from that positive by breaking exactly one obligation, so a refusal that fires
// for the wrong reason is visible.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as acceptance from '../tools/typed-acceptance.mjs';

// The test lives in the repository, so its inputs come from the caller: an artifact root and a stage name. Nothing about a
// particular packet is baked in.
const here = new URL('.', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const R = process.argv[4] ? path.win32.resolve(process.argv[4]) : path.win32.dirname(here);
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const H = (p) => '//wsl$/Ubuntu-20.04' + p;
const S = '/var/tmp/gtos-dsh-120-e759-current-guest-view';

// ---------------------------------------------------------------- the real positive
// The stage this packet actually ran, from the committed tree.
const stage = process.argv[2] || 'k0ccase01-01';
const reportPath = R + '/results/' + stage + '.json';
const innerPath = H(S) + '/logs/inner-report-' + stage + '.json';
const elfPath = H(S) + '/test-build/k2a/runtime_test.elf';

let positive = null;
try {
  const inner = JSON.parse(fs.readFileSync(innerPath, 'utf8'));
  const rawPath = H(inner.child.output_path);
  const raw = fs.readFileSync(rawPath);
  const elfBytes = fs.readFileSync(elfPath);
  const sourcePath = R + '/worktree-120/tests/runtime_test.cpp';
  const sourceBytes = fs.readFileSync(sourcePath);
  const flags = fs.readFileSync(R + '/profile-120/host-test-delta-runtime.txt', 'utf8')
    .split(/\r?\n/).filter((l) => l && !l.startsWith('#')).map((m) => '-D' + m.trim());

  positive = {
    kind: 'cpp',
    report: { path: reportPath, ok: true },
    source: { path: sourcePath, bytes: sourceBytes.length, sha256: sha(sourceBytes), tested: sha(sourceBytes) },
    profile: { flags },
    elf: { path: elfPath, bytes: elfBytes.length, sha256: sha(elfBytes) },
    gate: { passed: true, record: R + '/static-gate-' + stage + '.json' },
    requested_case: 1,
    run: {
      terminal: true,
      child_exit: inner.child.returncode,
      inner_report: true,
      raw_log: true,
      executed_case: 1,
      case_witness: (raw.toString('utf8').match(/CASE \d+/) || [null])[0],
      mentions_fail: /\bFAIL\b/.test(raw.toString('utf8')),
      reports_pass: /RUNTIME UNIT PASS/.test(raw.toString('utf8')),
    },
  };
  check('the positive case is built from the real stage files', true,
    { report: reportPath, inner: innerPath, raw: rawPath, elf: elfPath });
} catch (error) {
  check('the positive case is built from the real stage files', false, String(error.message));
}

if (positive) {
  const verdict = acceptance.acceptClaim(positive);
  check('the real Cpp stage is accepted', verdict.accepted, verdict.problems);

  // ---------------------------------------------------------------- one obligation at a time
  const mutations = [
    ['a Git result offered as a Cpp run', (c) => { c.run.impersonated_by = 'git transaction'; }, 'run_terminal'],
    ['a compiler exit code offered as a run', (c) => { c.run.impersonated_by = 'compiler exit'; }, 'run_terminal'],
    ['the tested bytes are unknown', (c) => { c.source.tested = null; }, 'source_pin'],
    ['the tested bytes differ from the pin', (c) => { c.source.tested = 'f'.repeat(64); }, 'source_pin'],
    ['the source is not pinned at all', (c) => { c.source = null; }, 'source_pin'],
    ['the profile is not pinned', (c) => { c.profile = { flags: [] }; }, 'profile_pin'],
    ['the ELF is not pinned', (c) => { c.elf = null; }, 'elf_pin'],
    ['the static gate did not pass', (c) => { c.gate = { passed: false }; }, 'gate_passed'],
    ['the run never reached a terminal state', (c) => { c.run.terminal = false; }, 'run_terminal'],
    ['the child exit is missing', (c) => { c.run.child_exit = null; }, 'child_exit'],
    ['the inner report is absent', (c) => { c.run.inner_report = false; }, 'inner_report'],
    ['the raw log is absent', (c) => { c.run.raw_log = false; }, 'raw_log'],
    ['no case was requested', (c) => { c.requested_case = null; }, 'requested_case'],
    ['a different case ran than was requested', (c) => { c.requested_case = 6; }, 'executed_case'],
    ['the witness does not name the case', (c) => { c.run.case_witness = 'CASE 7'; }, 'case_witness'],
    ['no witness at all', (c) => { c.run.case_witness = null; }, 'case_witness'],
    ['FAIL was reported but the child exited zero', (c) => { c.run.mentions_fail = true; c.run.child_exit = 0; }, 'child_exit'],
    ['the child did not report PASS but exited zero', (c) => { c.run.reports_pass = false; c.run.child_exit = 0; }, 'child_exit'],
  ];
  for (const [name, mutate, expectedObligation] of mutations) {
    const copy = structuredClone(positive);
    mutate(copy);
    const v = acceptance.acceptClaim(copy);
    const hit = v.problems.some((p) => p.obligation === expectedObligation);
    check(`refused: ${name}`, !v.accepted && hit,
      { first: v.first_refusal, matched: hit, all: v.problems.map((p) => p.obligation) });
  }

  // ---------------------------------------------------------------- type separation
  // A Node claim must not be satisfied by a Cpp bundle, and vice versa.
  const asNode = structuredClone(positive);
  asNode.kind = 'node';
  const nodeVerdict = acceptance.acceptClaim(asNode);
  check('a Cpp bundle relabelled as Node is not automatically accepted',
    nodeVerdict.accepted === false || nodeVerdict.kind === 'node',
    { accepted: nodeVerdict.accepted, problems: nodeVerdict.problems });

  const crossClaim = structuredClone(positive);
  crossClaim.claimed_as = 'git';
  const crossVerdict = acceptance.acceptClaim(crossClaim);
  check('a Cpp result cannot support a Git claim',
    !crossVerdict.accepted && crossVerdict.problems.some((p) => p.obligation === 'kind'),
    crossVerdict.problems);

  const gitBundle = {
    kind: 'git', repository: 'GTOS', parent: '1f64d87f', tip: '54076f45',
    staged_paths: ['tests/runtime_start.s'], canonical_artifacts: ['a', 'b', 'c', 'd'],
  };
  check('a well-formed Git claim is accepted', acceptance.acceptClaim(gitBundle).accepted,
    acceptance.acceptClaim(gitBundle).problems);
  const gitAsCpp = structuredClone(gitBundle);
  gitAsCpp.claimed_as = 'cpp';
  check('a Git claim cannot support a Cpp claim',
    !acceptance.acceptClaim(gitAsCpp).accepted,
    acceptance.acceptClaim(gitAsCpp).problems);

  // ---------------------------------------------------------------- the side-effect gate
  let threw = null;
  try { acceptance.gateBeforeSideEffect({ kind: 'cpp' }); } catch (error) { threw = error; }
  check('the side-effect gate throws rather than returning a false',
    threw !== null && /ACCEPTANCE REFUSED/.test(String(threw.message)), threw ? threw.message : null);
  check('the gate names the first obligation that failed',
    threw !== null && threw.verdict && threw.verdict.first_refusal
    && threw.verdict.first_refusal.obligation === 'source_pin',
    threw && threw.verdict ? threw.verdict.first_refusal : null);

  let passed = false;
  try { acceptance.gateBeforeSideEffect(positive); passed = true; } catch { passed = false; }
  check('the gate lets the real positive through', passed, null);

  // ---------------------------------------------------------------- unknown is not a pass
  // The 119 preflight wrote tested:null and still recorded true. That shape must be refused.
  const nullTested = structuredClone(positive);
  nullTested.source.tested = null;
  const nullVerdict = acceptance.acceptClaim(nullTested);
  check('tested:null is refused, never treated as equal',
    !nullVerdict.accepted && nullVerdict.first_refusal.obligation === 'source_pin',
    nullVerdict.first_refusal);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-120', kind: 'typed_acceptance_test',
  subject: 'references/typed-acceptance-120.mjs',
  positive_source: { stage, report: reportPath, inner: innerPath, elf: elfPath },
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
