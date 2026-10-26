// GTOS-DSH-120 K1: the typed acceptance consumer.
//
// The bridge's own classify() decides whether a stage was *delivered*: did the transport start, did the guard exit as
// expected, was there a guard row, an inner report and a raw log. That is a necessary gate and it is not a sufficient one.
// It cannot tell a C++ unit that really ran nine cases from a C++ unit that exited zero having run nothing, and it says
// nothing about whether the ELF that ran was built from the source under test. A stage can satisfy every transport
// condition while proving nothing about behaviour.
//
// This module is the second gate. It consumes a stage's evidence and decides whether the evidence supports the *kind of
// claim being made*, with the claim's own obligations. It is deliberately typed: a Cpp claim, a Node claim and a Git claim
// have different obligations, and a Node or Git result can never satisfy a Cpp one.
//
// The consumer is written to be called by the thing that actually needs the answer, not to be a report about reports. Its
// verdict is what a commit or guest step checks before it proceeds.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

// ---------------------------------------------------------------- what a claim of each type owes

// A Cpp claim means: this exact ELF, built from this exact source with this exact profile, really ran this exact case and
// really reported this result. Every element is required; none can be inferred from another.
export const CPP_OBLIGATIONS = [
  'source_pin',          // the source under test, with bytes
  'profile_pin',         // the flags that built it
  'elf_pin',             // the exact ELF that ran
  'gate_passed',         // the static gate on that ELF
  'run_terminal',        // the run reached a terminal state
  'child_exit',          // the child's own exit status
  'inner_report',        // the runner's inner report
  'raw_log',             // the child's raw output
  'requested_case',      // which case was asked for
  'executed_case',       // which case actually ran
  'case_witness',        // evidence that the requested case ran
];

// A Node claim means: this exact module really ran and really reported this. It needs no ELF and no child, and it can
// never be offered as a Cpp claim.
export const NODE_OBLIGATIONS = ['source_pin', 'run_terminal', 'child_exit', 'raw_log', 'executed_case'];

// A Git claim means: this repository, at this parent, produced this commit containing exactly these paths.
export const GIT_OBLIGATIONS = ['repository', 'parent', 'tip', 'staged_paths', 'canonical_artifacts'];

export const OBLIGATIONS = { cpp: CPP_OBLIGATIONS, node: NODE_OBLIGATIONS, git: GIT_OBLIGATIONS };

// ---------------------------------------------------------------- the typed gate

// Read one evidence bundle and answer whether it supports its claim. Every refusal names the first obligation that failed,
// because "some obligation failed" is not actionable.
export function acceptClaim(bundle) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });

  if (!bundle || typeof bundle !== 'object') return { accepted: false, kind: null, problems: [{ obligation: 'bundle', why: 'no evidence bundle' }] };
  const kind = bundle.kind;
  if (!OBLIGATIONS[kind]) return { accepted: false, kind, problems: [{ obligation: 'kind', why: 'unknown claim type: ' + kind }] };

  // A claim may never be satisfied by evidence of another type. This is the single most important rule here: a Git
  // transaction and a compiler exit status both end in zero, and neither is a test run.
  if (bundle.claimed_as && bundle.claimed_as !== kind) {
    refuse('kind', 'a ' + kind + ' result cannot support a ' + bundle.claimed_as + ' claim');
  }

  // ---- source
  // A Git claim is about a repository transition, not about source bytes under test, so it has its own obligations below and
  // must not be judged by the source rule.
  if (kind !== 'git') {
  if (!bundle.source || !bundle.source.bytes || !bundle.source.sha256) {
    refuse('source_pin', 'the source under test is not pinned');
  } else if (bundle.source.tested === null || bundle.source.tested === undefined) {
    // The 119 preflight recorded tested:null and still wrote a true. An unknown is not a pass.
    refuse('source_pin', 'the tested bytes are unknown; null is not equality');
  } else if (bundle.source.tested !== bundle.source.sha256) {
    refuse('source_pin', 'the tested bytes differ from the source pin');
  }
  }

  // ---- type-specific
  if (kind === 'cpp') {
    if (!bundle.profile || !bundle.profile.flags || !bundle.profile.flags.length) {
      refuse('profile_pin', 'the build profile is not pinned');
    }
    if (!bundle.elf || !bundle.elf.sha256 || !bundle.elf.bytes) {
      refuse('elf_pin', 'the ELF that ran is not pinned');
    }
    if (!bundle.gate || bundle.gate.passed !== true) {
      refuse('gate_passed', 'the static gate did not pass on this ELF');
    }
    // A Git transaction or a compiler exit code can never stand in for a run.
    if (bundle.run && bundle.run.impersonated_by) {
      refuse('run_terminal', 'a ' + bundle.run.impersonated_by + ' result was offered in place of a run');
    }
    if (!bundle.run || bundle.run.terminal !== true) {
      refuse('run_terminal', 'the run did not reach a terminal state');
    }
    if (!bundle.run || typeof bundle.run.child_exit !== 'number') {
      refuse('child_exit', 'the child exit status is not recorded');
    }
    if (!bundle.run || bundle.run.inner_report !== true) {
      refuse('inner_report', 'the inner report is absent');
    }
    if (!bundle.run || bundle.run.raw_log !== true) {
      refuse('raw_log', 'the raw log is absent');
    }
    // The requested case and the executed case must both be known and must agree.
    const requested = bundle.requested_case;
    const executed = bundle.run ? bundle.run.executed_case : null;
    if (requested === null || requested === undefined) {
      refuse('requested_case', 'no case was requested');
    } else if (executed === null || executed === undefined) {
      refuse('executed_case', 'no case is recorded as executed');
    } else if (requested !== executed) {
      refuse('executed_case', 'requested case ' + requested + ' but case ' + executed + ' ran');
    }
    // A witness, not a summary: the child's own output must name the case.
    if (!bundle.run || !bundle.run.case_witness) {
      refuse('case_witness', 'the child output does not witness the case');
    } else if (!String(bundle.run.case_witness).includes('CASE ' + executed)) {
      refuse('case_witness', 'the witness does not name case ' + executed);
    }
    // A FAIL that exited zero is still a FAIL, and must not be summarised into a pass.
    if (bundle.run && bundle.run.mentions_fail === true && bundle.run.child_exit === 0) {
      refuse('child_exit', 'the child reported FAIL but exited zero');
    }
    if (bundle.run && bundle.run.reports_pass === false && bundle.run.child_exit === 0) {
      refuse('child_exit', 'the child did not report PASS but exited zero');
    }
  }

  if (kind === 'node') {
    if (!bundle.run || bundle.run.terminal !== true) refuse('run_terminal', 'the module did not reach a terminal state');
    if (!bundle.run || typeof bundle.run.child_exit !== 'number') refuse('child_exit', 'the exit status is not recorded');
    if (!bundle.run || bundle.run.raw_log !== true) refuse('raw_log', 'the raw output is absent');
    if (bundle.run && bundle.run.executed_case === undefined) refuse('executed_case', 'what ran is not recorded');
  }

  if (kind === 'git') {
    if (!bundle.repository) refuse('repository', 'the repository is not named');
    if (!bundle.parent) refuse('parent', 'the parent is not declared');
    if (!bundle.tip) refuse('tip', 'the resulting tip is not declared');
    if (!Array.isArray(bundle.staged_paths) || bundle.staged_paths.length === 0) {
      refuse('staged_paths', 'no staged paths are recorded');
    }
    if (!Array.isArray(bundle.canonical_artifacts) || bundle.canonical_artifacts.length !== 4) {
      refuse('canonical_artifacts', 'fewer than four canonical artifacts');
    }
  }

  const accepted = problems.length === 0;
  return { accepted, kind, obligations: OBLIGATIONS[kind], problems,
    // A refusal is always a first refusal, so the caller can act on one thing.
    first_refusal: problems.length ? problems[0] : null };
}

// ---------------------------------------------------------------- reading a real stage's evidence

// Turn a bridge report plus the files it points at into an evidence bundle. This is the function the business side calls:
// it takes the stage record that already exists and asks whether it supports a Cpp claim about a named case.
export function bundleFromStage(options) {
  const { reportPath, requestedCase, sourcePin, profileFlags, elfPath, gateRecord, readFile } = options;
  const read = readFile || ((p) => fs.readFileSync(p));
  const report = JSON.parse(read(reportPath).toString('utf8'));
  const run = report.verdict || {};
  const innerPath = options.innerPath || null;
  let inner = null;
  try { inner = innerPath ? JSON.parse(read(innerPath).toString('utf8')) : null; } catch { inner = null; }
  const rawPath = options.rawPath || null;
  let raw = null;
  try { raw = rawPath ? read(rawPath).toString('utf8') : null; } catch { raw = null; }
  const elfBytes = elfPath ? read(elfPath) : null;
  return {
    kind: 'cpp',
    report: { path: reportPath, ok: report.ok === true, transport_exit: run.transport_exit },
    source: sourcePin || null,
    profile: profileFlags ? { flags: profileFlags } : null,
    elf: elfBytes ? { path: elfPath, bytes: elfBytes.length, sha256: sha256(elfBytes) } : null,
    gate: gateRecord || null,
    requested_case: requestedCase === undefined ? null : requestedCase,
    run: {
      terminal: run.transport_started === true && run.timed_out === false,
      child_exit: inner && typeof inner.child === 'object' && inner.child
        ? inner.child.returncode : (run.transport_exit === undefined ? null : run.transport_exit),
      inner_report: report.inner_report_present === true,
      raw_log: report.raw_present === true,
      executed_case: inner && inner.requested_case !== undefined ? inner.requested_case
        : (options.executedCase === undefined ? null : options.executedCase),
      case_witness: raw === null ? null : (raw.match(/CASE \d+/) || [null])[0],
      mentions_fail: raw === null ? null : /\bFAIL\b/.test(raw),
      reports_pass: raw === null ? null : /RUNTIME UNIT PASS/.test(raw),
    },
  };
}

// The typed gate a commit or guest step calls. It is a single function so there is one answer, not several.
export function gateBeforeSideEffect(claim) {
  const verdict = acceptClaim(claim);
  if (verdict.accepted) return verdict;
  const error = new Error('ACCEPTANCE REFUSED at ' + (verdict.first_refusal ? verdict.first_refusal.obligation : 'unknown')
    + ': ' + (verdict.first_refusal ? verdict.first_refusal.why : 'no reason'));
  error.verdict = verdict;
  throw error;
}
