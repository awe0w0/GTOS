// GTOS-DSH-138 C: one business link entry, used by the normal case and by every failure case.
//
// The 137 review accepted the path wiring and refused the consumption. In 137 this entry opened three documents — the
// lifecycle terminal, the helper report and the bridge evidence — and never opened the frozen spec, the inner report or
// the raw stream. It then built the tool's return code out of the helper's *transport* exit code:
//
//     inner: { returncode: transport.exit_code }
//
// The transport exit code is the wrapper's. It is zero when the guard finished, whatever the tool inside it did. So the
// final verdict was a verdict on the wrapper wearing the name of a verdict on ld.
//
// This entry consumes the chain the way the taskbook asks: one call, from the attempt to the typed terminal, then helper,
// bridge, frozen spec, inner report, the raw stream the inner report names, and the image outputs. For each of them it
// checks task, stage, route, tool identity, argv, cwd, subject, return code, timeout, raw hash, output ownership and
// process-group cleanup — and it uses the tool's own return code, taken from the inner report the guard wrote.
//
// The three statuses stay three:
//
//   outer   the entry's own spawn result for the runner process
//   helper  the lifecycle transport, flat on the runner terminal
//   tool    the inner report's child.returncode, which is ld's own exit status
//
// No layer's zero covers another layer's failure.
//
// Every document is pinned when it is first read, and the pin is what later steps compare against, so a file replaced
// with same-size bytes between the read and the use is caught rather than passing.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

import * as TC from './terminal-consumption.mjs';
import * as HP from './host-path.mjs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

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

const arrEq = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length
  && a.every((v, i) => v === b[i]);

// ---------------------------------------------------------------- one document, read once, pinned

// The bytes are read once and the pin is computed from those bytes, so every later comparison in this call compares
// against what was actually read, not against a second read of a file that may have changed in between.
function pinnedRead(label, p, expectSha) {
  const bytes = readBytes(p);
  if (bytes === null) {
    return { label, path: p, present: false, bytes: null, sha256: null, doc: null, reason: 'not readable' };
  }
  const pin = { label, path: p, present: true, bytes: bytes.length, sha256: sha256(bytes) };
  let doc = null;
  try { doc = JSON.parse(bytes.toString('utf8')); } catch (e) {
    return { ...pin, doc: null, reason: 'not JSON: ' + String(e.message) };
  }
  if (expectSha && pin.sha256 !== expectSha) {
    return { ...pin, doc, reason: 'the saved pin does not match: saved ' + expectSha + ', read ' + pin.sha256 };
  }
  return { ...pin, doc, reason: null };
}

// ---------------------------------------------------------------- the entry

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
    read_set: { roles: [], pins: {} },
  };
  const step = (name, outcome, detail) => record.steps.push({ step: name, outcome, detail: detail || null });
  const refuse = (obligation, why, extra) => {
    record.refusal = { obligation, why, ...(extra || {}) };
    step('consume', 'refused', record.refusal);
    record.launched = false;
    return record;
  };
  const addPin = (role, pin) => {
    record.read_set.roles.push(role);
    record.read_set.pins[role] = { path: pin.path, bytes: pin.bytes, sha256: pin.sha256 };
  };

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

  // ---- 3. the declaration, then the spec. Three distinct roles, three distinct files.
  const specPath = path.join(reportRoot, 'stage-output', stage + '.spec.json');
  const resultPath = path.join(reportRoot, 'result-' + stage + '-138.json');
  const helperReportPath = path.join(reportRoot, 'helper-report-' + stage + '-138.json');
  const attemptPath = path.join(reportRoot, 'attempt-' + stage + '-138.json');
  const frozenHelperPath = path.join(reportRoot, 'frozen-helpers', stage + '.mjs');
  const controlPath = path.join(reportRoot, 'stage-output', stage + '.control.json');

  // The child the runner spawns is a Windows Node process, so the report it is told to write is a host path, and the
  // gate that decides that is the shared module's, not a conversion written here.
  const helperReportForChild = HP.gateWrite('link-entry:helper-report', helperReportPath,
    [reportRoot], 'win32');

  try {
    TC.assertDistinctRoles({
      canonical_terminal: resultPath, helper_report: helperReportPath,
      control: controlPath, frozen_helper: frozenHelperPath, attempt: attemptPath,
    });
    step('declaration', 'distinct');
  } catch (error) {
    record.refusal = { obligation: 'declaration', why: String(error.message), collisions: error.collisions };
    step('declaration', 'refused', record.refusal);
    return record;
  }

  const spec = {
    task: authority.task, route: 'image.link', stage, root: reportRoot,
    runnerPath: runner, helper: 'native-138-01.mjs',
    control: {
      task: authority.task, route: 'image.link', stage,
      mode: 'image-link', bridge_module_path: bridge.binding_module.path, bridge,
      stage_options: { individuals, mapFile, elfFile, linkerScript, objectRoot },
      purpose: 'the business link entry, ' + stage,
      report_root: reportRoot,
      report_path: helperReportForChild,
    },
    expected_outputs: [helperReportForChild],
    limit: limit || 900,
  };
  fs.mkdirSync(path.dirname(specPath), { recursive: true });
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  step('spec', 'written', { path: specPath, helper_report: helperReportPath, terminal: resultPath });

  // The spec is read back and pinned. From here the run is judged against these bytes, not the object still in memory.
  const specPin = pinnedRead('spec', specPath);
  if (!specPin.present || !specPin.doc) {
    return refuse('spec', 'the spec this entry wrote is not readable back', { path: specPath });
  }
  addPin('spec', specPin);
  if (specPin.doc.task !== authority.task || specPin.doc.stage !== stage || specPin.doc.route !== 'image.link') {
    return refuse('spec_identity', 'the frozen spec is not this task/stage/route',
      { task: specPin.doc.task, stage: specPin.doc.stage, route: specPin.doc.route });
  }

  const r = spawnSync(node, [runner, specPath], {
    windowsHide: true, timeout: (limit || 900) * 1000, maxBuffer: 32 * 1024 * 1024, encoding: 'utf8',
  });
  // The outer status is this entry's own spawn result. It is not the helper's and not the tool's.
  const outerStatus = r.status;
  step('runner', outerStatus === 0 ? 'completed' : 'failed', { status: outerStatus });

  // The production path produces and then consumes, and the consumption is one function. The negative cases below need to
  // be run against the same consumption the business path runs, which means being able to enter it over a chain that has
  // already been produced. That is what this call boundary is for: the business path calls it with the chain it just made,
  // and a negative case calls it with a frozen chain from a real run plus exactly one mutation. The function is the same
  // function either way, so a refusal here is the business refusal and not a test double.
  return consumeLinkChain({ record, admission, authority, stage, reportRoot, attemptPath,
    resultPath, helperReportPath, elfFile, mapFile, outerStatus, step, refuse, addPin,
    specPath, specSha256: specPin.sha256 });
}

// The consumption half of the link entry. Entered by the business path immediately after the producer finishes, and by the
// negative cases over a frozen chain.
export function consumeLinkChain(ctx) {
  const { record, admission, authority, stage, reportRoot, attemptPath, resultPath, helperReportPath,
    elfFile, mapFile, outerStatus, specPath, specSha256 } = ctx;
  const { step, refuse } = ctx;
  // A role read twice is one role. The spec is read before the launch and again after it, and both reads are the spec; if
  // the list counted reads rather than roles, "the acceptance path reads six documents" would be a claim about the number
  // of reads and a reader could satisfy it by reading one document six times.
  const addPin = (role, pin) => {
    if (!record.read_set.roles.includes(role)) record.read_set.roles.push(role);
    record.read_set.pins[role] = { path: pin.path, bytes: pin.bytes, sha256: pin.sha256 };
  };

  // ---- 3b. the frozen spec, re-read at the end of the run.
  //
  // The producer wrote it and pinned it before the launch; that pin says the spec was right when the run began. Re-reading
  // it here says it is still the same file after the run. Those are two different facts, and the second one is what makes
  // "the run was judged against this spec" true rather than "the run was judged against a spec that has since changed".
  if (typeof specPath === 'string') {
    const again = pinnedRead('spec', specPath);
    if (!again.present) {
      return refuse('spec', 'the frozen spec this stage was run under is no longer readable',
        { path: specPath });
    }
    if (typeof specSha256 === 'string' && again.sha256 !== specSha256) {
      return refuse('spec', 'the frozen spec changed between the launch and the consumption',
        { path: specPath, pinned_sha256: specSha256, read_sha256: again.sha256,
          pinned_bytes: null, read_bytes: again.bytes });
    }
    addPin('spec', again);
  }

  // ---- 4. the typed lifecycle terminal.
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
  const terminalPin = pinnedRead('terminal', consumed.terminal.path);
  if (!terminalPin.doc) return refuse('terminal', 'the terminal is not readable as JSON', { path: consumed.terminal.path });
  addPin('terminal', terminalPin);
  const result = terminalPin.doc;

  const transport = result.transport || {
    started: result.transport_started, exit_code: result.transport_exit_code,
    timed_out: result.timed_out, launch_error: result.launch_error,
  };

  // ---- 5. the helper's business report, opened from its own file.
  const reportPin = pinnedRead('helper_report', helperReportPath);
  if (!reportPin.present) {
    return refuse('helper_report', 'the helper report the stage declared is not on disk',
      { expected: helperReportPath });
  }
  addPin('helper_report', reportPin);
  const report = reportPin.doc;
  if (!report) return refuse('helper_report', 'the helper report is not JSON', { path: helperReportPath });
  if (report.kind !== 'ld' || report.lifecycle !== undefined) {
    return refuse('helper_report_type', 'the helper report is not a business report',
      { kind: report.kind, lifecycle: report.lifecycle });
  }
  if (report.stage !== stage) {
    return refuse('helper_report_stage', 'the helper report is for another stage',
      { expected: stage, got: report.stage });
  }
  if (report.task !== authority.task) {
    return refuse('helper_report_task', 'the helper report is for another task',
      { expected: authority.task, got: report.task });
  }

  // ---- 6. the bridge evidence the report names, opened from its own path.
  const bridgePath = report.bridge_report;
  if (typeof bridgePath !== 'string' || !bridgePath) {
    return refuse('bridge', 'the helper report names no bridge evidence', { named: bridgePath ?? null });
  }
  const bridgePin = pinnedRead('bridge', bridgePath);
  if (!bridgePin.present) {
    return refuse('bridge', 'the bridge evidence the helper report names is not readable', { named: bridgePath });
  }
  addPin('bridge', bridgePin);
  const bridgeDoc = bridgePin.doc;
  if (!bridgeDoc) return refuse('bridge', 'the bridge evidence is not JSON', { named: bridgePath });
  if (bridgeDoc.task !== authority.task || bridgeDoc.stage !== stage) {
    return refuse('bridge_identity', 'the bridge evidence is not this task/stage',
      { task: bridgeDoc.task, stage: bridgeDoc.stage });
  }
  if (bridgeDoc.kind !== 'ld') {
    return refuse('bridge_kind', 'the bridge evidence is not a link', { kind: bridgeDoc.kind });
  }

  // ---- 7. the inner report. 137 never opened this. It is the only place the tool's own return code appears, and the
  //         bridge now records its path and pin, so this entry reads the file the bridge wrote rather than rebuilding a
  //         name out of the work root.
  const innerRef = report.inner_report || bridgeDoc.inner_report || null;
  if (!innerRef || typeof innerRef !== 'object' || typeof innerRef.path !== 'string') {
    return refuse('inner_report', 'neither the helper report nor the bridge names an inner report',
      { report_field: report.inner_report === undefined ? null : typeof report.inner_report,
        bridge_field: bridgeDoc.inner_report === undefined ? null : typeof bridgeDoc.inner_report });
  }
  const innerPin = pinnedRead('inner_report', innerRef.path, innerRef.sha256);
  if (!innerPin.present) {
    return refuse('inner_report', 'the inner report the bridge named is not readable',
      { named: innerRef.path, saved_sha256: innerRef.sha256 });
  }
  if (innerPin.reason) {
    return refuse('inner_report_pin', 'the inner report does not match the pin the bridge saved for it',
      { named: innerRef.path, saved_sha256: innerRef.sha256, read_sha256: innerPin.sha256 });
  }
  addPin('inner_report', innerPin);
  const inner = innerPin.doc;
  if (!inner) return refuse('inner_report', 'the inner report is not JSON', { named: innerRef.path });

  // ---- 8. the raw stream, checked against both documents that recorded it.
  const rawRef = report.raw || bridgeDoc.raw || null;
  if (!rawRef || typeof rawRef !== 'object' || typeof rawRef.path !== 'string') {
    return refuse('raw', 'neither the helper report nor the bridge names a raw stream',
      { report_field: report.raw === undefined ? null : typeof report.raw });
  }
  const rawBytes = readBytes(rawRef.path);
  if (rawBytes === null) return refuse('raw', 'the raw stream is not readable', { named: rawRef.path });
  const rawSha = sha256(rawBytes);
  addPin('raw', { path: rawRef.path, bytes: rawBytes.length, sha256: rawSha });
  const rawSaid = [
    { by: 'bridge', sha256: rawRef.sha256, bytes: rawRef.bytes },
    { by: 'inner_report', sha256: inner.child ? inner.child.output_sha256 : null,
      bytes: inner.child ? inner.child.output_file_bytes : null },
  ];
  for (const one of rawSaid) {
    if (one.sha256 !== null && one.sha256 !== undefined && one.sha256 !== rawSha) {
      return refuse('raw_hash', 'the raw stream does not hash as ' + one.by + ' recorded it',
        { by: one.by, recorded: one.sha256, read: rawSha, path: rawRef.path });
    }
    if (one.bytes !== null && one.bytes !== undefined && one.bytes !== rawBytes.length) {
      return refuse('raw_bytes', 'the raw stream is not the size ' + one.by + ' recorded',
        { by: one.by, recorded: one.bytes, read: rawBytes.length, path: rawRef.path });
    }
  }
  record.raw = { path: rawRef.path, bytes: rawBytes.length, sha256: rawSha };

  // ---- 9. consistency across the chain. Each of these is a fact one document states and another must agree with.
  const subject = bridgeDoc.subject || null;
  if (!subject) return refuse('subject', 'the bridge evidence records no subject for this stage');
  if (subject.kind !== 'ld') return refuse('subject_kind', 'the subject is not a link', { kind: subject.kind });

  // The argv the stage asked for, the argv the bridge built, the argv the guard ran, and the argv the tool saw.
  const argvSaid = [
    { by: 'bridge.subject', argv: subject.argv },
    { by: 'bridge.plan', argv: bridgeDoc.plan ? bridgeDoc.plan.argv : null },
    { by: 'guard_row', argv: bridgeDoc.guard_row ? bridgeDoc.guard_row.argv : null },
    { by: 'inner_report', argv: inner.argv },
  ].filter((one) => Array.isArray(one.argv));
  if (argvSaid.length < 3) {
    return refuse('argv_present', 'fewer than three documents state the argv',
      { stated_by: argvSaid.map((one) => one.by) });
  }
  const argv0 = argvSaid[0].argv;
  for (const one of argvSaid.slice(1)) {
    if (!arrEq(one.argv, argv0)) {
      return refuse('argv', 'the argv in ' + one.by + ' is not the argv in ' + argvSaid[0].by,
        { left: argvSaid[0].by, right: one.by, left_tokens: argv0.length, right_tokens: one.argv.length });
    }
  }
  record.argv = { tokens: argv0.length, cmd0: argv0[0], stated_by: argvSaid.map((one) => one.by) };

  // The tool identity. The stage asked for a tool; the argv says which; the subject says what it thought that tool was.
  const toolId = subject.tool_identity || null;
  if (!toolId) return refuse('tool_identity', 'the bridge recorded no tool identity');
  if (toolId.linux_path !== argv0[0]) {
    return refuse('tool_identity', 'the tool identity is not the tool the argv names',
      { subject_tool: toolId.linux_path, argv0: argv0[0] });
  }
  record.tool = toolId;

  // The working directory the tool ran in.
  const cwdSaid = [
    { by: 'bridge.subject', cwd: subject.cwd },
    { by: 'bridge.plan', cwd: bridgeDoc.plan ? bridgeDoc.plan.cwd : null },
    { by: 'guard_row', cwd: bridgeDoc.guard_row ? bridgeDoc.guard_row.cwd : null },
    { by: 'inner_report', cwd: inner.cwd },
  ].filter((one) => typeof one.cwd === 'string');
  if (cwdSaid.length < 3) {
    return refuse('cwd_present', 'fewer than three documents state the working directory',
      { stated_by: cwdSaid.map((one) => one.by) });
  }
  for (const one of cwdSaid.slice(1)) {
    if (one.cwd !== cwdSaid[0].cwd) {
      return refuse('cwd', 'the working directory in ' + one.by + ' is not the one in ' + cwdSaid[0].by,
        { left: cwdSaid[0].by, right: one.by, left_cwd: cwdSaid[0].cwd, right_cwd: one.cwd });
    }
  }
  record.cwd = cwdSaid[0].cwd;

  // The work root, so a stage cannot be judged against another root's evidence.
  if (inner.work_root && bridgeDoc.plan && inner.work_root !== bridgeDoc.plan.work_root) {
    return refuse('work_root', 'the inner report ran under a different work root',
      { inner: inner.work_root, bridge: bridgeDoc.plan.work_root });
  }

  // The tool's own return code and timeout. This is the number 137 filled with the helper transport exit code.
  if (!inner.child || typeof inner.child.returncode !== 'number') {
    return refuse('tool_returncode', 'the inner report records no tool return code',
      { child_keys: inner.child ? Object.keys(inner.child) : null });
  }
  const toolReturncode = inner.child.returncode;
  const toolTimedOut = inner.child.timed_out === true;

  // The process group. The guard measured it and the inner report measured the same one, so the group reported here is a
  // group two documents agree on and both say is empty afterwards.
  const guardGroup = (bridgeDoc.guard_row && bridgeDoc.guard_row.group) || null;
  const innerGroup = inner.child ? { pgid: inner.child.group_pgid,
    measured: inner.child.group_measured, measured_final: inner.child.group_measured_final,
    descendants_left: inner.child.descendants_left, killed: inner.child.killed } : null;
  if (!guardGroup) return refuse('group', 'the guard row records no process group');
  if (!innerGroup) return refuse('group', 'the inner report records no process group');
  if (typeof guardGroup.pgid !== 'number' || guardGroup.pgid <= 0) {
    return refuse('group', 'the guard recorded no positive process group', { pgid: guardGroup.pgid });
  }
  if (guardGroup.measured !== true || guardGroup.final_measured !== true) {
    return refuse('group', 'the guard did not measure the group before and after cleanup',
      { measured: guardGroup.measured, final_measured: guardGroup.final_measured });
  }
  if (Array.isArray(guardGroup.descendants_left) && guardGroup.descendants_left.length > 0) {
    return refuse('group', 'the guard left descendants behind',
      { descendants_left: guardGroup.descendants_left.length });
  }
  if (typeof innerGroup.pgid === 'number' && innerGroup.pgid > 0 && innerGroup.pgid !== guardGroup.pgid) {
    return refuse('group', 'the guard and the inner report measured different process groups',
      { guard_pgid: guardGroup.pgid, inner_pgid: innerGroup.pgid });
  }
  record.group = { pgid: guardGroup.pgid, measured: true, final_measured: true, descendants_left: 0,
    landlock_ok: bridgeDoc.guard_row.verdict ? bridgeDoc.guard_row.verdict.landlock_ok === true : null,
    probes_ok: bridgeDoc.guard_row.verdict ? bridgeDoc.guard_row.verdict.probes_ok === true : null };

  // The guard's own verdict must be used, not merely recorded. 137 kept it and decided nothing with it.
  const guardVerdict = bridgeDoc.verdict || {};
  if (guardVerdict.accepted !== true) {
    return refuse('guard', 'the bridge did not accept its own evidence', { verdict: guardVerdict });
  }

  record.guard = { verdict: guardVerdict, group: record.group, tool_exit: bridgeDoc.guard_row.exit_code,
    note: 'the guard row is read from the bridge evidence, which is where it is written' };
  record.bridge_evidence = { path: bridgePath, ok: bridgeDoc.ok, verdict: guardVerdict,
    stage: bridgeDoc.stage, kind: bridgeDoc.kind };
  record.statuses = {
    outer_runner: { source: "this entry's own spawn result for the runner process", status: outerStatus },
    helper_transport: { source: 'the lifecycle terminal, flat fields', started: transport.started,
      exit_code: transport.exit_code, timed_out: transport.timed_out },
    tool: { source: "the inner report's child.returncode, which is ld's own exit status",
      returncode: toolReturncode, timed_out: toolTimedOut },
    note: 'three statuses, three sources. No one of them is used to fill another.',
  };
  record.launched = transport.started === true && guardGroup.measured === true
    && typeof guardGroup.pgid === 'number' && guardGroup.pgid > 0;
  record.launch_observation = {
    source: 'the bridge evidence and the inner report, for this invocation',
    transport_started: transport.started, transport_exit_code: transport.exit_code,
    tool_returncode: toolReturncode, group_pgid: guardGroup.pgid,
    group_measured: guardGroup.measured, group_final_measured: guardGroup.final_measured,
    guard_seconds: bridgeDoc.guard_row.seconds,
  };

  // ---- 10. the outer runner's own status. A tool that returned zero inside a runner that failed did not succeed.
  if (outerStatus !== 0) {
    return refuse('runner', 'the runner exited ' + outerStatus + ', so the stage did not complete',
      { outer_status: outerStatus, helper_transport_exit: transport.exit_code, tool_returncode: toolReturncode });
  }
  const runnerState = TC.runnerStatus(result);
  record.runner_status = runnerState;
  if (!runnerState.ok) {
    return refuse('runner', 'the terminal records a stage that did not succeed', { state: runnerState });
  }

  // ---- 11. the outputs. A zero exit with no image is a failure.
  const elfBytes = readBytes(hostOf(elfFile));
  const mapBytes = readBytes(hostOf(mapFile));
  record.elf = elfBytes === null ? null
    : { path: elfFile, bytes: elfBytes.length, sha256: sha256(elfBytes) };
  record.map = mapBytes === null ? null
    : { path: mapFile, bytes: mapBytes.length, sha256: sha256(mapBytes) };

  // ---- 12. the final verdict, on the tool's own return code.
  const finalVerdict = admission.linkVerdict({
    terminal: { transport_started: transport.started, timed_out: transport.timed_out,
      ok: result.ok === true && !toolTimedOut },
    inner: { returncode: toolReturncode },
    elf: record.elf, map: record.map, expectedElf: elfFile, expectedMap: mapFile,
  });
  if (!finalVerdict.accepted) {
    record.refusal = finalVerdict.first_refusal;
    step('consume', 'refused', finalVerdict.first_refusal);
    return record;
  }
  step('consume', 'accepted', finalVerdict.observed);
  record.final = finalVerdict.observed;
  return record;
}
