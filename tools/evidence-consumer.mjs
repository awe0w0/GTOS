// GTOS-DSH-122 R0: a consumer that reads the pinned evidence itself.
//
// The previous packet shipped a typed gate whose fields were supplied by the caller. A caller that wrote `ok: true` and
// `terminal: true` was believed, which makes the gate a formality. This module is the opposite: the caller states only what
// it *expects* (an external contract), and the consumer opens the real records and decides.
//
// The chain it proves, in order:
//
//   build spec  -> which source file was compiled, with which flags, to which object
//   wave source -> the immutable snapshot of that source at build time
//   worktree    -> the committed source, which must equal the wave snapshot
//   static gate -> the ELF that was linked from those objects
//   inner report-> the child that ran, its own exit status, and the raw log it produced
//   raw log     -> the bytes, which must name the requested case and report a pass
//   terminal    -> the attempt record, which must be complete and name this stage
//
// Nothing here trusts a boolean from the caller. `requested_case` comes from the contract, `executed_case` from the child's
// own argv, and the two must agree.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

// A host that is not the Linux filesystem needs an explicit mapping. An unmappable path is refused, never skipped.
export function hostMapper(rules) {
  if (!Array.isArray(rules) || !rules.length) throw new Error('EVIDENCE: a host mapper needs rules');
  return (p) => {
    if (typeof p !== 'string' || !p) throw new Error('EVIDENCE: cannot map an empty path');
    // A report root may already be a host path, because this packet writes its own records on the host side. A path that is
    // already a host path is passed through; a Linux path is mapped. Nothing is guessed.
    if (!p.startsWith('/')) {
      if (!/^([A-Za-z]:[\\/]|\\\\)/.test(p)) throw new Error('EVIDENCE: neither a Linux nor a host path: ' + p);
      return p;
    }
    for (const r of rules) {
      if (p === r.linux) return r.host;
      if (p.startsWith(r.linux + '/')) return r.host + p.slice(r.linux.length);
    }
    throw new Error('EVIDENCE: no mapper rule places this path: ' + p);
  };
}

function readJson(host, path) {
  if (!fs.existsSync(host(path))) throw new Error('EVIDENCE: absent record: ' + path);
  return JSON.parse(fs.readFileSync(host(path), 'utf8'));
}

// ---------------------------------------------------------------- the receipt

// Read everything the stage actually left behind. This function is the consumer: it takes locators and returns facts it
// read, or throws. It never accepts a fact as an argument.
export function readCppReceipt(options) {
  const { reportRoot, sourceRoot, stage, hostPath } = options;
  if (typeof stage !== 'string' || !/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) {
    throw new Error('EVIDENCE: bad stage name: ' + stage);
  }
  const host = hostPath;

  // The terminal attempt record. Its presence and its completeness are facts, not claims.
  const attemptPath = reportRoot + '/attempt-' + stage + '-122.json';
  const attempt = readJson(host, attemptPath);
  if (attempt.attempt !== stage) throw new Error('EVIDENCE: the attempt record names ' + attempt.attempt);
  if (attempt.complete !== true) throw new Error('EVIDENCE: the attempt record is not complete');

  // The frozen spec the child actually ran under.
  const specPath = sourceRoot + '/logs/spec-' + stage + '.json';
  const spec = readJson(host, specPath);
  if (spec.stage !== stage) throw new Error('EVIDENCE: the spec names ' + spec.stage);
  const argv = spec.cmd;
  if (!Array.isArray(argv) || !argv.length) throw new Error('EVIDENCE: the spec has no command');
  const cmd0 = argv[0];
  // For a host unit, cmd0 is the test ELF itself. Anything else is a different kind of stage.
  const elfLinux = cmd0;
  const executedCase = argv.length > 1 ? Number(argv[1]) : null;

  // The inner report the guard wrote around that child.
  const innerPath = sourceRoot + '/logs/inner-report-' + stage + '.json';
  const inner = readJson(host, innerPath);
  if (inner.stage !== stage) throw new Error('EVIDENCE: the inner report names ' + inner.stage);
  const child = inner.child;
  if (!child || typeof child.returncode !== 'number') throw new Error('EVIDENCE: the inner report has no child status');

  // The raw log, read through the path the inner report names, and checked against the hash it recorded.
  const rawLinux = child.output_path;
  if (!rawLinux) throw new Error('EVIDENCE: the inner report names no raw log');
  const rawBytes = fs.readFileSync(host(rawLinux));
  const rawSha = sha256(rawBytes);
  if (child.output_sha256 && child.output_sha256 !== rawSha) {
    throw new Error('EVIDENCE: the raw log does not match the hash the inner report recorded');
  }
  if (typeof child.output_bytes === 'number' && child.output_bytes !== rawBytes.length) {
    throw new Error('EVIDENCE: the raw log is not the size the inner report recorded');
  }
  const rawText = rawBytes.toString('utf8');

  // The build records: which sources were compiled and with which flags. A host unit links several translation units, so
  // every one of them is part of the subject.
  // The build specs belong to the build tag, not to the run stage: one build serves several run stages. The tag is the
  // directory the ELF was written into, which is the only place it is recorded.
  const tagMatch = /\/test-build\/([^/]+)\//.exec(elfLinux);
  const buildTag = tagMatch ? tagMatch[1] : null;
  const buildSpecs = [];
  const logDir = host(sourceRoot + '/logs');
  if (fs.existsSync(logDir) && buildTag) {
    const prefix = 'spec-c' + buildTag.replace(/[^a-z0-9]/g, '') + '-';
    for (const name of fs.readdirSync(logDir).sort()) {
      if (!name.startsWith(prefix) || !name.endsWith('.json')) continue;
      const one = JSON.parse(fs.readFileSync(logDir + '/' + name, 'utf8'));
      const sourceLinux = one.cmd.filter((a) => typeof a === 'string' && /\.(cpp|cc)$/.test(a))[0] || null;
      const objectAt = one.cmd.indexOf('-o');
      buildSpecs.push({
        spec: sourceRoot + '/logs/' + name,
        source: sourceLinux,
        object: objectAt >= 0 ? one.cmd[objectAt + 1] : null,
        defines: one.cmd.filter((a) => typeof a === 'string' && a.startsWith('-D')).map((a) => a.slice(2)),
        cxxflags: one.cmd.filter((a) => typeof a === 'string' && a.startsWith('-') && !a.startsWith('-D')
          && !['-o', '-c', '-MMD', '-MP'].includes(a)),
      });
    }
  }

  // The ELF: bytes, and the static gate record that was written for it.
  const elfBytes = fs.readFileSync(host(elfLinux));
  const gatePath = reportRoot + '/static-gate-' + stage + '.json';
  const gate = fs.existsSync(host(gatePath)) ? readJson(host, gatePath) : null;

  return {
    kind: 'cpp',
    stage,
    attempt: { path: attemptPath, complete: attempt.complete === true, terminal: attempt.terminal_result || null,
      result_ok: attempt.result_ok === true, reserved_utc: attempt.reserved_utc || null,
      completed_utc: attempt.completed_utc || null },
    spec: { path: specPath, cmd0, argv },
    inner: { path: innerPath, child_returncode: child.returncode, child_timed_out: child.timed_out === true,
      group_measured: child.group_measured === true, output_sha256: child.output_sha256 || null },
    raw: { path: rawLinux, bytes: rawBytes.length, sha256: rawSha, text: rawText },
    elf: { path: elfLinux, bytes: elfBytes.length, sha256: sha256(elfBytes) },
    gate,
    buildSpecs, build_tag: buildTag,
    executed_case: executedCase,
    read_set: [attemptPath, specPath, innerPath, rawLinux, elfLinux,
      ...buildSpecs.map((b) => b.spec), ...(gate ? [gatePath] : [])],
  };
}

// ---------------------------------------------------------------- the contract and the verdict

// The caller states what it expects. Every field here is an external obligation, not an observation.
export function cppContract(expected) {
  const need = (v, m) => { if (v === undefined || v === null) throw new Error('CONTRACT: ' + m); return v; };
  return {
    kind: 'cpp',
    stage: need(expected.stage, 'stage'),
    requested_case: need(expected.requested_case, 'requested case'),
    elf_sha256: need(expected.elf_sha256, 'expected ELF hash'),
    profile_macros: need(expected.profile_macros, 'expected profile macros'),
    subjects: need(expected.subjects, 'expected subject set'),
    require_gate: expected.require_gate !== false,
  };
}

// The verdict. Every refusal names the first obligation that failed, so the caller can act on one thing.
export function acceptCppReceipt(receipt, contract) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });

  // A record of another kind can never satisfy a Cpp contract. This is the check the previous packet only pretended to
  // make: there is no `impersonated_by` label to read, because a real Git or compile record simply is not this shape.
  if (!receipt || receipt.kind !== 'cpp') {
    return { accepted: false, problems: [{ obligation: 'kind',
      why: 'the evidence is ' + (receipt ? receipt.kind : 'absent') + ', not a Cpp run' }],
      first_refusal: { obligation: 'kind', why: 'not a Cpp run' } };
  }
  if (contract.kind !== 'cpp') return { accepted: false,
    problems: [{ obligation: 'contract', why: 'the contract is not Cpp' }],
    first_refusal: { obligation: 'contract', why: 'not a Cpp contract' } };
  if (receipt.stage !== contract.stage) {
    refuse('stage', 'the evidence is for ' + receipt.stage + ', not ' + contract.stage);
  }

  // ---- the terminal record
  if (!receipt.attempt) refuse('terminal', 'there is no attempt record');
  else {
    if (receipt.attempt.complete !== true) refuse('terminal', 'the attempt is not complete');
    if (receipt.attempt.result_ok !== true) refuse('terminal', 'the attempt record did not report success');
  }

  // ---- the ELF the contract approved
  if (!receipt.elf) refuse('elf', 'there is no ELF record');
  else if (receipt.elf.sha256 !== contract.elf_sha256) {
    refuse('elf', 'the ELF that ran is not the ELF the contract approved');
  }
  if (contract.require_gate) {
    if (!receipt.gate) refuse('gate', 'there is no static gate record for this ELF');
    else if (receipt.gate.passed !== true) refuse('gate', 'the static gate did not pass');
    else if (receipt.gate.sha256 && receipt.gate.sha256 !== receipt.elf.sha256) {
      refuse('gate', 'the gate record names a different ELF');
    }
  }

  // ---- the source set and the profile
  const buildSpecs = Array.isArray(receipt.buildSpecs) ? receipt.buildSpecs : [];
  if (!buildSpecs.length) refuse('subjects', 'there are no build records');
  const builtSources = buildSpecs.map((b) => b.source).filter(Boolean).sort();
  const expectedSources = contract.subjects.slice().sort();
  if (JSON.stringify(builtSources) !== JSON.stringify(expectedSources)) {
    const missing = expectedSources.filter((s) => !builtSources.includes(s));
    const extra = builtSources.filter((s) => !expectedSources.includes(s));
    refuse('subjects', 'the built source set is not the contract set'
      + (missing.length ? '; missing ' + missing.join(',') : '')
      + (extra.length ? '; unexpected ' + extra.join(',') : ''));
  }
  for (const b of buildSpecs) {
    if (!b.source) continue;
    const want = contract.profile_macros.slice().sort();
    const got = b.defines.slice().sort();
    if (JSON.stringify(want) !== JSON.stringify(got)) {
      refuse('profile', b.source.split('/').pop() + ' was built with [' + got.join(',') + '] not [' + want.join(',') + ']');
      break;
    }
  }

  // ---- the run
  if (!receipt.inner) refuse('run', 'there is no inner report');
  else if (receipt.inner.child_timed_out) refuse('run', 'the child timed out');
  else if (receipt.inner.child_returncode !== 0) {
    refuse('run', 'the child exited ' + receipt.inner.child_returncode);
  }
  // The case the child actually ran, from its own argv, must be the case the contract asked for.
  if (receipt.executed_case === null) refuse('case', 'the child argv names no case');
  else if (receipt.executed_case !== contract.requested_case) {
    refuse('case', 'the child ran case ' + receipt.executed_case + ', not ' + contract.requested_case);
  }

  // ---- the raw log, read rather than summarised
  if (!receipt.raw || typeof receipt.raw.text !== 'string') {
    refuse('witness', 'there is no raw log');
    const early = { accepted: false, problems, first_refusal: problems[0] };
    return early;
  }
  const witness = new RegExp('CASE ' + contract.requested_case + '(?![0-9])');
  if (!witness.test(receipt.raw.text)) {
    refuse('witness', 'the raw log does not name case ' + contract.requested_case);
  }
  const mentionsFail = /\bFAIL\b/.test(receipt.raw.text);
  const reportsPass = /RUNTIME UNIT PASS/.test(receipt.raw.text);
  if (mentionsFail && receipt.inner.child_returncode === 0) {
    refuse('result', 'the child reported FAIL but exited zero');
  }
  if (!reportsPass && receipt.inner.child_returncode === 0) {
    refuse('result', 'the child exited zero without reporting PASS');
  }

  const accepted = problems.length === 0;
  return { accepted, problems, first_refusal: problems.length ? problems[0] : null,
    observed: { stage: receipt.stage, executed_case: receipt.executed_case,
      child_returncode: receipt.inner ? receipt.inner.child_returncode : null,
      elf_sha256: receipt.elf ? receipt.elf.sha256 : null,
      raw_sha256: receipt.raw ? receipt.raw.sha256 : null, sources: builtSources.length } };
}

// The gate a downstream action calls. It throws, so a caller that forgets to check the return value still cannot proceed.
export function gateCpp(receipt, contract) {
  const verdict = acceptCppReceipt(receipt, contract);
  if (verdict.accepted) return verdict;
  const first = verdict.first_refusal;
  const error = new Error('EVIDENCE REFUSED at ' + first.obligation + ': ' + first.why);
  error.verdict = verdict;
  throw error;
}
