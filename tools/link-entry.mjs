// GTOS-DSH-135 B2: one business link entry, used by the normal case and by every failure case.
//
// The 129 linkgate was a second code path. It called the admission function directly, hardcoded `ld_started: false` in the
// record, and built its "host-test object" by writing text to a file. That proves the admission refuses a set. It does not
// prove that the entry the business uses refuses, and it does not prove that a refusal leaves the linker unlaunched,
// because no linker was ever involved.
//
// This module is the entry itself: validate -> claim -> launch -> consume, in that order, with each step's evidence coming
// from the step that produced it.
//
//   - Validation is the admission module's, on a contract whose order was read from the frozen baseline spec.
//   - The claim is an exclusive marker on disk.
//   - The launch goes through the frozen runner as a subprocess, so the transport, the guard and the process group are the
//     real ones.
//   - The observation of "did a tool start" comes from the runner's own result document, which records transport_started
//     and the measured process group. It is never a literal this module writes.
//
// A refusal returns before the runner is invoked, so the absence of an attempt file for that stage is itself the evidence
// that nothing launched.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

import * as TC from './terminal-consumption-135.mjs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// A host path as the child will see it. The bridge and the linker both take Linux paths.
function hostToLinux(p) {
  if (typeof p !== 'string') return p;
  const s = p.replace(/\\/g, '/');
  const unc = /^\/\/wsl\$\/[^/]+\/(.*)$/i.exec(s);
  if (unc) return '/' + unc[1];
  const drive = /^([A-Za-z]):\/(.*)$/.exec(s);
  if (drive) return '/mnt/' + drive[1].toLowerCase() + '/' + drive[2];
  return s;
}

function readBytes(p) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
}

export function hostOf(p) {
  if (typeof p !== 'string') return p;
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (m) return m[1].toUpperCase() + ':/' + m[2];
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
}

// ---------------------------------------------------------------- the entry

// The request is what a caller has: an individual list, an output pair, a contract path and a stage name. The entry decides
// everything else. It returns a record of what each step did, including whether a tool started and where that was observed.
export function linkThroughEntry(options) {
  const {
    admission, runner, node, stage, contractPath, individuals, elfFile, mapFile,
    linkerScript, objectRoot, reportRoot, authority, bridge, limit,
  } = options;
  const record = {
    stage,
    contract: contractPath,
    steps: [],
    launched: null,
    launch_observation: null,
    refusal: null,
    elf: null,
    map: null,
  };
  const step = (name, outcome, detail) => record.steps.push({ step: name, outcome, detail: detail || null });

  // ---- 1. validate. The contract is read from disk, so the order in it is the order that was written.
  const contractBytes = readBytes(contractPath);
  if (contractBytes === null) {
    record.refusal = { obligation: 'contract', why: 'the contract is not on disk: ' + contractPath };
    step('validate', 'refused', record.refusal);
    return record;
  }
  const contract = JSON.parse(contractBytes.toString('utf8'));
  const verdict = admission.admitLink({ individuals }, contract);
  if (!verdict.admitted) {
    record.refusal = verdict.first_refusal;
    step('validate', 'refused', verdict.first_refusal);
    return record;
  }
  step('validate', 'admitted', verdict.counts);

  // ---- 2. claim. An existing output is refused, and nothing is launched.
  let claim = null;
  try {
    claim = admission.claimOutputs(elfFile, mapFile);
  } catch (error) {
    record.refusal = error.claim_refusal || { obligation: 'claim', why: String(error.message) };
    step('claim', 'refused', record.refusal);
    return record;
  }
  record.claim = claim;
  step('claim', 'claimed', { marker: claim.marker });

  // ---- 3. launch, through the frozen runner. The spec is written only after the claim, so a stage that was refused never
  //         has a spec and never has an attempt.
  const specPath = path.join(reportRoot, 'stage-output', stage + '.spec.json');
  // Three distinct roles, three distinct files. The canonical terminal belongs to the runner's lifecycle; the helper's
  // business report is its own document; and the helper's outputs are the artefacts it produced. In 133 the report was
  // declared at the terminal's own path, so the file the entry read was a helper report wearing a result's name.
  const resultPath = path.join(reportRoot, 'result-' + stage + '-135.json');
  const helperReportPath = path.join(reportRoot, 'helper-report-' + stage + '-135.json');
  // The runner's control and its expected outputs are read by a child that sees Linux paths, so both are expressed that way.
  const helperReportLinux = hostToLinux(helperReportPath);
  const attemptPath = path.join(reportRoot, 'attempt-' + stage + '-135.json');
  const frozenHelperPath = path.join(reportRoot, 'frozen-helpers', stage + '.mjs');

  // The preparation gate, before the freeze and before any child.
  try {
    TC.assertDistinctRoles({
      canonical_terminal: resultPath, helper_report: helperReportPath,
      control: path.join(reportRoot, 'stage-output', stage + '.control.json'),
      frozen_helper: frozenHelperPath, attempt: attemptPath,
    });
    step('declaration', 'distinct');
  } catch (error) {
    record.refusal = { obligation: 'declaration', why: String(error.message), collisions: error.collisions };
    step('declaration', 'refused', record.refusal);
    return record;
  }

  const spec = {
    task: authority.task, route: 'image.link', stage, root: reportRoot,
    runnerPath: runner, helper: 'native-135-01.mjs',
    control: {
      task: authority.task, route: 'image.link', stage,
      mode: 'image-link', bridge_module_path: bridge.binding_module.path, bridge,
      stage_options: { individuals, mapFile, elfFile, linkerScript, objectRoot },
      purpose: 'the business link entry, ' + stage,
      report_path: helperReportLinux,
    },
    // The helper's outputs are the artefacts it was asked to produce. The terminal is not one of them: it is written by
    // the lifecycle afterwards, and declaring it here is what let 133 treat a report as a terminal.
    expected_outputs: [helperReportLinux],
    limit: limit || 900,
  };
  fs.mkdirSync(path.dirname(specPath), { recursive: true });
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  step('spec', 'written', { path: specPath, helper_report: helperReportPath, terminal: resultPath });

  const r = spawnSync(node, [runner, specPath], {
    windowsHide: true, timeout: (limit || 900) * 1000, maxBuffer: 32 * 1024 * 1024, encoding: 'utf8',
  });
  step('runner', r.status === 0 ? 'completed' : 'failed', { status: r.status });

  // ---- 4. consume the lifecycle terminal, typed. The attempt must be complete, must name this terminal, and the
  //         terminal must be a lifecycle record of the right stage. A helper report beside it is not a substitute, and a
  //         tool that returned zero inside a runner that failed is not a success.
  const consumed = TC.consumeTerminal({
    attemptPath,
    expect: { task: authority.task, stage, route: 'image.link', canonical_terminal: resultPath },
  });
  record.terminal_consumption = consumed;
  if (!consumed.accepted) {
    record.refusal = consumed.first_refusal;
    record.launched = false;
    step('consume', 'refused', consumed.first_refusal);
    return record;
  }
  step('consume', 'terminal_accepted', consumed.terminal);
  const result = JSON.parse(readBytes(consumed.terminal.path).toString('utf8'));
  const resultBytes = readBytes(consumed.terminal.path);
  // The lifecycle terminal records the transport as flat fields; the helper's business report nests it. Both shapes are
  // read, and the lifecycle's own field is the authority because that is the file this consumed.
  const transport = result.transport || {
    started: result.transport_started,
    exit_code: result.transport_exit_code,
    timed_out: result.timed_out,
    launch_error: result.launch_error,
  };
  const group = (result.guard_row && result.guard_row.group) || {};
  const runnerVerdict = result.verdict || {};
  record.result = {
    path: resultPath, bytes: resultBytes.length, sha256: sha256(resultBytes),
    transport_started: transport.started, transport_exit_code: transport.exit_code,
    timed_out: transport.timed_out, ok: result.ok,
    verdict,
  };
  // "A tool started" is the transport having started AND a process group having been measured. project_child_started is
  // the OS guest classification and is false for every host stage, so it is recorded but not used as the test.
  // A tool started if the pre-launch gate did not refuse, the host helper started, it took measurable time, and the
  // transport started. Each of those is recorded by the runner for this invocation. The process group is not available
  // from this orchestrator, so it is reported as the runner reports it and is not part of the test.
  // A tool started if the transport started and the guard measured a real process group for it: a positive pgid, a
  // measurement recorded as taken, and that group empty after cleanup. Every one of those is written by the guard for this
  // invocation, not by this module.
  record.launched = transport.started === true
    && group.measured === true
    && typeof group.pgid === 'number' && group.pgid > 0;
  record.launch_observation = {
    source: 'the runner result document and its guard row, for this invocation',
    transport_started: transport.started,
    transport_exit_code: transport.exit_code,
    timed_out: transport.timed_out,
    group_pgid: typeof group.pgid === 'number' ? group.pgid : null,
    group_measured: group.measured,
    group_final_measured: group.final_measured,
    group_descendants_left: Array.isArray(group.descendants_left) ? group.descendants_left.length : null,
    guard_seconds: typeof result.guard_row === 'object' && result.guard_row ? result.guard_row.seconds : null,
    verdict_accepted: runnerVerdict.accepted,
  };

  // ---- 5. the runner's own status. A tool that returned zero inside a runner that failed did not succeed as a stage,
  //         and this is the check 133 was missing: it read the helper report, saw ok, and never looked at the exit.
  if (r.status !== 0) {
    record.refusal = { obligation: 'runner', why: 'the runner exited ' + r.status + ', so the stage did not complete',
      terminal_exit: result.transport_exit_code, runner_status: r.status };
    step('consume', 'refused', record.refusal);
    return record;
  }
  const runnerState = TC.runnerStatus(result);
  record.runner_status = runnerState;
  if (!runnerState.ok) {
    record.refusal = { obligation: 'runner', why: 'the terminal records a stage that did not succeed', state: runnerState };
    step('consume', 'refused', record.refusal);
    return record;
  }

  // ---- 6. the outputs. A zero exit with no image is a failure, which is what linkVerdict is for.
  const elfBytes = readBytes(hostOf(elfFile));
  const mapBytes = readBytes(hostOf(mapFile));
  record.elf = elfBytes === null ? null
    : { path: elfFile, bytes: elfBytes.length, sha256: sha256(elfBytes) };
  record.map = mapBytes === null ? null
    : { path: mapFile, bytes: mapBytes.length, sha256: sha256(mapBytes) };
  const finalVerdict = admission.linkVerdict({
    terminal: { transport_started: transport.started, timed_out: transport.timed_out, ok: result.ok === true },
    inner: { returncode: transport.exit_code },
    elf: record.elf, map: record.map, expectedElf: elfFile, expectedMap: mapFile,
  });
  if (!finalVerdict.accepted) {
    record.refusal = finalVerdict.first_refusal;
    step('consume', 'refused', finalVerdict.first_refusal);
    return record;
  }
  step('consume', 'accepted', finalVerdict.observed);
  return record;
}
