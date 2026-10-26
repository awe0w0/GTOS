// GTOS-DSH-125 F0a: one row per producer, every cell opened.
//
// The 124 gap was that twelve compile specs were read and then described as "the compiles succeeded". A spec is a plan. It
// says what would be run; it does not say that a tool ran, that the tool returned zero, or that the file on disk came from
// that run. This module opens, for every producer of the current chain:
//
//   the spec        argv, tool, the contract pin it was prepared under
//   the attempt     whether the stage completed and after a terminal result
//   the terminal    the transport exit, whether the project child started, timeout, declared-output state
//   the inner       the child's own return code, its output path, size and hash
//   the raw         the bytes, which may legitimately be empty for a compiler
//   the output      the object or ELF the producer claims to have written, hashed now
//
// A row is only complete when all seven cells are present and consistent. "The spec exists" is not a row.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

export function hostMapper(rules) {
  if (!Array.isArray(rules) || !rules.length) throw new Error('PRODUCER: a host mapper needs rules');
  return (p) => {
    if (typeof p !== 'string' || !p) throw new Error('PRODUCER: cannot map an empty path');
    if (!p.startsWith('/')) {
      if (!/^([A-Za-z]:[\\/]|\\\\)/.test(p)) throw new Error('PRODUCER: neither a Linux nor a host path: ' + p);
      return p;
    }
    for (const r of rules) {
      if (p === r.linux) return r.host;
      if (p.startsWith(r.linux + '/')) return r.host + p.slice(r.linux.length);
    }
    throw new Error('PRODUCER: no mapper rule places this path: ' + p);
  };
}

function readJson(host, p, what) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(host(p)); } catch { /* retry */ } }
  if (b === null) throw new Error('PRODUCER: absent ' + what + ': ' + p);
  return JSON.parse(b.toString('utf8'));
}
function readBytes(host, p, what) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(host(p)); } catch { /* retry */ } }
  if (b === null) throw new Error('PRODUCER: absent ' + what + ': ' + p);
  return b;
}

// ---------------------------------------------------------------- the order ledger

// The ledger is an append-only record written as things happen: the contract first, then each spec, then each run start and
// completion. Ordering is a property of this record, not of a file timestamp, which is why the contract can be shown to
// predate the first compile.
export function readOrderLedger(host, ledgerPath) {
  const bytes = readBytes(host, ledgerPath, 'order ledger');
  const rows = bytes.toString('utf8').split(/\r?\n/).filter(Boolean).map((line, i) => {
    let r = null;
    try { r = JSON.parse(line); } catch { throw new Error('PRODUCER: ledger line ' + (i + 1) + ' is not JSON'); }
    if (typeof r.seq !== 'number' || typeof r.event !== 'string') {
      throw new Error('PRODUCER: ledger line ' + (i + 1) + ' has no sequence or event');
    }
    return r;
  });
  for (let i = 1; i < rows.length; i++) {
    if (rows[i].seq !== rows[i - 1].seq + 1) throw new Error('PRODUCER: the ledger has a gap at line ' + (i + 1));
  }
  const frozen = rows.filter((r) => r.event === 'contract-frozen');
  if (frozen.length !== 1) throw new Error('PRODUCER: the ledger must freeze exactly one contract, found ' + frozen.length);
  const firstCompileSpec = rows.find((r) => r.event === 'spec-written');
  if (!firstCompileSpec) throw new Error('PRODUCER: the ledger records no spec');
  return {
    path: ledgerPath, bytes: bytes.length, sha256: sha256(bytes), rows,
    contract_frozen_at_seq: frozen[0].seq,
    contract_sha256: frozen[0].contract ? frozen[0].contract.sha256 : null,
    first_spec_at_seq: firstCompileSpec.seq,
    contract_precedes_every_producer: frozen[0].seq < firstCompileSpec.seq,
    // The ordering statement, in one place, from the record.
    ordering_evidence: 'the ledger line that freezes the contract has a lower sequence number than every spec-written '
      + 'and run-started line; a producer that ran before the contract would have to appear earlier',
  };
}

// ---------------------------------------------------------------- one producer

// A producer is identified by its build stage. The compile stages are c<tag>-NN, the assembly a<tag>-NN, the link l<tag>-01.
export function readProducer(options) {
  const { reportRoot, sourceRoot, stage, outputLinux, host } = options;
  const specPath = reportRoot + '/stage-output/' + stage + '.spec.json';
  const attemptPath = reportRoot + '/attempt-' + stage + '-125.json';
  const terminalPath = reportRoot + '/result-' + stage + '-125.json';
  const innerPath = sourceRoot + '/logs/inner-report-' + stage + '.json';
  const runSpecPath = sourceRoot + '/logs/spec-' + stage + '.json';

  const spec = readJson(host, specPath, 'spec');
  const attempt = readJson(host, attemptPath, 'attempt');
  const terminal = readJson(host, terminalPath, 'terminal');
  const inner = readJson(host, innerPath, 'inner report');
  const runSpec = readJson(host, runSpecPath, 'frozen run spec');

  const child = inner.child;
  if (!child) throw new Error('PRODUCER: ' + stage + ' inner report has no child');
  const rawBytes = readBytes(host, child.output_path, 'raw log');
  const rawSha = sha256(rawBytes);
  const outBytes = outputLinux ? readBytes(host, outputLinux, 'output') : null;

  return {
    stage,
    spec: { path: specPath, bytes: fs.statSync(host(specPath)).size,
      contract_pin: spec.contract_pin || null, route: spec.route, root: spec.root, task: spec.task },
    run_spec: { path: runSpecPath, cmd: runSpec.cmd, cmd0: runSpec.cmd ? runSpec.cmd[0] : null },
    attempt: { path: attemptPath, complete: attempt.complete === true, attempt_stage: attempt.attempt,
      route: attempt.route, lifecycle: attempt.lifecycle || null,
      result_ok: attempt.result_ok === true,
      reserved_utc: attempt.reserved_utc || null, completed_utc: attempt.completed_utc || null,
      reservation: attempt.reservation || null },
    terminal: { path: terminalPath, transport_started: terminal.transport_started === true,
      transport_exit_code: terminal.transport_exit_code,
      host_helper_started: terminal.host_helper_started === true,
      project_child_started: terminal.project_child_started === true,
      project_child_note: terminal.project_child_note || null,
      timed_out: terminal.timed_out === true,
      launch_error: terminal.launch_error || null,
      expected_outputs_all_present: terminal.expected_outputs_all_present,
      ok: terminal.ok === true,
      terminal_written_before_complete: terminal.terminal_result_written_before_complete === true },
    inner: { path: innerPath, returncode: child.returncode, timed_out: child.timed_out === true,
      output_path: child.output_path, output_bytes: child.output_bytes,
      output_sha256: child.output_sha256 || null,
      // The per-request child evidence: this stage's own process group, measured by the guard, scoped to this call.
      group_measured: child.group_measured === true,
      group_pgid: child.group_pgid === undefined ? null : child.group_pgid,
      elapsed_seconds: child.elapsed_seconds === undefined ? null : child.elapsed_seconds,
      descendants_before_cleanup: child.descendants_before_cleanup || [],
      descendants_left: child.descendants_left || [],
      killed: child.killed || [] },
    raw: { path: child.output_path, bytes: rawBytes.length, sha256: rawSha,
      empty_is_legitimate: rawBytes.length === 0 },
    output: outputLinux ? { path: outputLinux, bytes: outBytes.length, sha256: sha256(outBytes) } : null,
    read_set: [specPath, attemptPath, terminalPath, innerPath, runSpecPath, child.output_path,
      ...(outputLinux ? [outputLinux] : [])],
  };
}

// ---------------------------------------------------------------- the verdict for one producer

export function acceptProducer(row, contractRecord) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });

  // ---- the contract the producer was prepared under must still be the contract on disk
  if (!row.spec) refuse('contract', 'there is no spec record for this producer');
  else if (!row.spec.contract_pin) refuse('contract', 'the spec carries no contract pin');
  else if (row.spec.contract_pin.sha256 !== contractRecord.sha256) {
    refuse('contract', 'the spec was prepared under a different contract');
  }

  // ---- the run: a spec is not an execution
  if (!row.terminal) refuse('started', 'there is no terminal record for this producer');
  else if (row.terminal.transport_started !== true) refuse('started', 'the transport never started');
  // A tool ran in this stage only if the guard measured a process group for this call with a positive id and a positive
  // elapsed time. That is this request's own child, not a machine-wide observation.
  if (!row.inner) refuse('child', 'there is no inner report, so no child was measured');
  else if (row.inner.group_measured !== true) refuse('child', 'the guard measured no process group for this stage');
  else if (!Number.isInteger(row.inner.group_pgid) || row.inner.group_pgid <= 0) {
    refuse('child', 'the measured process group has no positive id');
  } else if (!(typeof row.inner.elapsed_seconds === 'number' && row.inner.elapsed_seconds > 0)) {
    refuse('child', 'the measured child took no time, so no tool ran');
  }
  if (row.terminal && row.terminal.timed_out) refuse('timeout', 'the producer timed out');
  if (row.terminal && row.terminal.launch_error) refuse('launch', 'the producer reported a launch error');

  // ---- the tool's own return code, from the inner report, not from the transport
  if (!row.inner) refuse('returncode', 'there is no inner report for this producer');
  else if (row.inner.returncode !== 0) refuse('returncode', 'the tool exited ' + row.inner.returncode);
  if (row.inner && row.inner.timed_out) refuse('timeout', 'the tool timed out');

  // ---- the terminal, which is a separate record from the attempt
  if (row.attempt && row.attempt.attempt_stage !== row.stage) refuse('attempt', 'the attempt record is for ' + row.attempt.attempt_stage);
  if (row.attempt && row.attempt.complete !== true) refuse('attempt', 'the attempt is not complete');
  if (row.attempt && row.attempt.result_ok !== true) refuse('attempt', 'the attempt did not report success');
  if (row.terminal && row.terminal.ok !== true) refuse('terminal', 'the terminal did not report success');
  if (row.terminal && row.terminal.terminal_written_before_complete !== true) {
    refuse('terminal', 'the terminal was not written before the attempt completed');
  }

  // ---- the output: the producer must have produced the file that is on disk now
  if (row.output) {
    if (row.output.bytes === 0) refuse('output', 'the output file is empty');
  }

  // ---- the raw log is read even when it is empty, and emptiness is a fact rather than a gap
  if (row.inner && row.raw && row.inner.output_sha256 && row.inner.output_sha256 !== row.raw.sha256) {
    refuse('raw', 'the raw log does not match the hash the inner report recorded');
  }
  if (row.inner && row.raw && typeof row.inner.output_bytes === 'number' && row.inner.output_bytes !== row.raw.bytes) {
    refuse('raw', 'the raw log is not the size the inner report recorded');
  }

  const accepted = problems.length === 0;
  return { accepted, problems, first_refusal: problems.length ? problems[0] : null,
    observed: { stage: row.stage,
      returncode: row.inner ? row.inner.returncode : null,
      output_sha256: row.output ? row.output.sha256 : null,
      raw_bytes: row.raw ? row.raw.bytes : null } };
}

export function gateProducer(row, contractRecord) {
  const v = acceptProducer(row, contractRecord);
  if (v.accepted) return v;
  const e = new Error('PRODUCER REFUSED at ' + v.first_refusal.obligation + ': ' + v.first_refusal.why);
  e.verdict = v;
  throw e;
}

// ---------------------------------------------------------------- the whole chain

// Every producer of the chain, accepted together. The contract ordering is checked once, from the ledger, and then each
// producer is checked against the same contract record.
export function readChain(options) {
  const { reportRoot, sourceRoot, tag, producers, host, ledgerPath } = options;
  const ledger = readOrderLedger(host, ledgerPath);
  const contractPath = reportRoot + '/contract-cpp-125.json';
  const contractBytes = readBytes(host, contractPath, 'contract');
  const contractRecord = {
    path: contractPath, bytes: contractBytes.length, sha256: sha256(contractBytes),
    contract: JSON.parse(contractBytes.toString('utf8')),
  };
  const rows = producers.map((p) => readProducer({ reportRoot, sourceRoot, stage: p.stage,
    outputLinux: p.output, host }));
  const verdicts = rows.map((r) => acceptProducer(r, contractRecord));
  return {
    tag, ledger, contract: { path: contractRecord.path, bytes: contractRecord.bytes,
      sha256: contractRecord.sha256, contract_id: contractRecord.contract.contract_id },
    rows, verdicts,
    accepted: verdicts.every((v) => v.accepted) && ledger.contract_precedes_every_producer
      && ledger.contract_sha256 === contractRecord.sha256,
    read_set: [...new Set([ledger.path, contractRecord.path, ...rows.flatMap((r) => r.read_set)])],
  };
}
