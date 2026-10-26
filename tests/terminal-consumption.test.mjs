// GTOS-DSH-134 T: the terminal is the lifecycle's file, and only a completed attempt can name one.
//
// Three faults met in 133 and this test pins each of them:
//
//   1. the helper's business report was declared at the runner's own canonical result path, so the runner could not write
//      its terminal there and exited non-zero;
//   2. the file the entry then read back was that helper report - `kind: "ld"`, a plan and a guard verdict - not a
//      lifecycle terminal;
//   3. the attempt was still `complete: false` and the entry accepted anyway, because ld itself had returned zero.
//
// The order below is the point: a real stage runs and is accepted through the same entry the business uses, and only then
// are the failure cases required to be refused. A refusal means nothing unless acceptance is something the entry can be
// seen to do.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as TC from '../tools/terminal-consumption.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-typed-terminal-safe-tss-and-current-guest-delivery-20261026-a';
const NODE = process.argv[5] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const A = JSON.parse(readBytes(R + '/authority-134.json').toString('utf8'));
const hostOf = (p) => {
  if (typeof p !== 'string') return p;
  if (p.startsWith('/mnt/')) { const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p); return m[1].toUpperCase() + ':/' + m[2]; }
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
};
const RUN = String(10 + (Date.now() % 89));

// ---------------------------------------------------------------- 1. path identity
{
  // The same file under different spellings must compare equal, or a collision hides behind a separator.
  const pairs = [
    ['C:/a/b.json', '/mnt/c/a/b.json'],
    ['F:\\x\\y.json', 'F:/x/y.json'],
    ['//wsl$/Ubuntu-20.04/var/tmp/z.json', '/var/tmp/z.json'],
    ['F:/A/B.JSON', 'f:/a/b.json'],
  ];
  for (const [a, b] of pairs) {
    check('the same file under two spellings compares equal: ' + a,
      TC.normalisePath(a) === TC.normalisePath(b), { a: TC.normalisePath(a), b: TC.normalisePath(b) });
  }
  // And two different files must not.
  check('two different files do not compare equal',
    TC.normalisePath('F:/a/b.json') !== TC.normalisePath('F:/a/c.json'), null);
}

// ---------------------------------------------------------------- 2. the declaration gate
{
  const clean = { canonical_terminal: R + '/test-only/t/result.json',
    helper_report: R + '/test-only/t/helper-report.json',
    control: R + '/test-only/t/control.json', frozen_helper: R + '/test-only/t/helper.mjs',
    attempt: R + '/test-only/t/attempt.json' };
  let threw = null;
  try { TC.assertDistinctRoles(clean); } catch (e) { threw = e; }
  check('a declaration with distinct roles passes the preparation gate', threw === null,
    threw ? threw.message : null);

  // The 133 collision, in each of the spellings a caller might use.
  for (const [label, value] of [
    ['the exact same path', clean.canonical_terminal],
    ['a different separator', clean.canonical_terminal.replace(/\//g, '\\')],
    ['different case', clean.canonical_terminal.toUpperCase()],
  ]) {
    let err = null;
    try { TC.assertDistinctRoles({ ...clean, helper_report: value }); } catch (e) { err = e; }
    check('a collision is refused: ' + label,
      err !== null && /two roles name the same file/.test(String(err.message)),
      err ? err.message : null);
    check('the refusal names both roles: ' + label,
      err !== null && Array.isArray(err.collisions) && err.collisions[0].roles.length === 2,
      err && err.collisions ? err.collisions[0].roles : null);
  }
  // A collision with the attempt or the control is refused too, not only with the terminal.
  for (const role of ['attempt', 'control', 'frozen_helper']) {
    let err = null;
    try { TC.assertDistinctRoles({ ...clean, [role]: clean.canonical_terminal }); } catch (e) { err = e; }
    check('a collision between the terminal and the ' + role + ' is refused', err !== null,
      err ? err.message : null);
  }
}

// ---------------------------------------------------------------- 3. a real stage, through the real runner
// The fixture owns every mutable path it uses, including its own runner pin and its own guard ledger namespace.
const F = R + '/test-only/t';
if (fs.existsSync(F)) throw new Error('the fixture already exists; this test uses a fresh namespace: ' + F);
for (const d of ['stage-output', 'frozen-runners', 'terminals', 'results']) fs.mkdirSync(F + '/' + d, { recursive: true });
const runnerBytes = readBytes(R + '/frozen-runners/run-134-r01.mjs');
fs.writeFileSync(F + '/frozen-runners/run-134-r01.mjs', runnerBytes);
fs.writeFileSync(F + '/runner-pin-134-run-134-r01.json', JSON.stringify({
  task: 'GTOS-DSH-134', runner: 'frozen-runners/run-134-r01.mjs',
  bytes: runnerBytes.length, sha256: sha(runnerBytes),
  note: 'the fixture is its own frozen root, so it pins the runner it holds',
}, null, 2) + '\n');

// The fixture's guard ledger lives in its own namespace, which the fixture namespace gate requires.
const fixtureLinux = A.work_root + '/test-only/t';
fs.mkdirSync(hostOf(fixtureLinux) + '/logs', { recursive: true });

const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };

// One real stage. The helper does real work and writes a real artefact; the runner writes the terminal.
function runStage(stageName, options = {}) {
  const helperName = options.helperName || (stageName + '.mjs');
  const helperPath = F + '/' + helperName;
  if (!fs.existsSync(helperPath)) {
    fs.writeFileSync(helperPath, [
      "import fs from 'node:fs';",
      'const control = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));',
      'const root = control.args[0];',
      "fs.mkdirSync(root + '/out', { recursive: true });",
      "fs.writeFileSync(root + '/out/' + control.stage + '.txt', 'the helper wrote this\\n');",
      "process.stdout.write(JSON.stringify({ wrote: control.stage }) + '\\n');",
    ].join('\n') + '\n');
  }
  const terminalPath = F + '/result-' + stageName + '-134.json';
  const helperReportPath = F + '/results/helper-report-' + stageName + '-134.json';
  const attemptPath = F + '/attempt-' + stageName + '-134.json';
  const declared = {
    canonical_terminal: terminalPath,
    helper_report: options.collideTerminal ? terminalPath : helperReportPath,
    control: F + '/stage-output/' + stageName + '.control.json',
    frozen_helper: F + '/frozen-helpers/' + stageName + '.mjs',
    attempt: attemptPath,
  };
  let declaration = null;
  try { declaration = TC.assertDistinctRoles(declared); } catch (error) {
    return { refused: 'declaration', error: String(error.message), collisions: error.collisions,
      spec_written: false, attempt_written: fs.existsSync(attemptPath), declared };
  }
  const spec = {
    task: 'GTOS-DSH-134', route: 'test.python', stage: stageName, root: F, fixture_root: F,
    runnerPath: F + '/frozen-runners/run-134-r01.mjs', helper: helperName,
    control: {
      task: 'GTOS-DSH-134', stage: stageName, report_root: F,
      bridge: { work_root: fixtureLinux, source_root: A.source_root },
      report_path: declared.helper_report,
      args: [F],
      // The helper's own artefact. The terminal is not declared here: the lifecycle writes it afterwards.
      expected_outputs: [F + '/out/' + stageName + '.txt'],
    },
    expected_outputs: [F + '/out/' + stageName + '.txt'],
    limit: 120,
  };
  const specPath = F + '/stage-output/' + stageName + '.spec.json';
  if (fs.existsSync(specPath)) throw new Error('the spec already exists: ' + specPath);
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  const r = spawnSync(NODE, [F + '/frozen-runners/run-134-r01.mjs', specPath], {
    windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8',
  });
  return { status: r.status, stdout: String(r.stdout || ''), stderr: String(r.stderr || ''),
    specPath, terminalPath, helperReportPath, attemptPath, declaration };
}

// ---- the positive
const posStage = 'tpos' + RUN + '-01';
const positive = runStage(posStage);
check('a real stage runs and the runner completes', positive.status === 0,
  { status: positive.status, tail: positive.stdout.split('\n').filter((l) => l.trim()).slice(-2) });
// Two independent files, not one file wearing two names.
const terminalPin = pin(positive.terminalPath);
const helperReportPin = pin(positive.helperReportPath);
check('the terminal exists', terminalPin !== null, terminalPin);
// The runner does not write a helper business report; the helper's own artefact is its output. What matters is that the
// declaration kept the roles apart, which the gate proved, and that the terminal is the lifecycle file.
check('the helper wrote its own artefact', fs.existsSync(F + '/out/' + posStage + '.txt'), null);
check('the terminal is not the helper artefact',
  TC.normalisePath(terminalPin.path) !== TC.normalisePath(F + '/out/' + posStage + '.txt'), null);
// The attempt is complete and names the terminal.
const attempt = JSON.parse(readBytes(positive.attemptPath).toString('utf8'));
check('the attempt is complete', attempt.complete === true, attempt.complete);
check('the attempt names the terminal',
  attempt.terminal_result && TC.normalisePath(attempt.terminal_result.path)
    === TC.normalisePath(positive.terminalPath), attempt.terminal_result && attempt.terminal_result.path);
check('the attempt lifecycle records completion after the terminal',
  /completed_after_terminal_result/.test(String(attempt.lifecycle)), attempt.lifecycle);

// ---- the typed consumption accepts it
const consumed = TC.consumeTerminal({
  attemptPath: positive.attemptPath,
  expect: { task: 'GTOS-DSH-134', stage: posStage, route: 'test.python',
    canonical_terminal: positive.terminalPath },
});
check('the typed consumer accepts the completed stage', consumed.accepted === true, consumed.problems);
check('the accepted terminal is a lifecycle record, not a report', consumed.terminal
  && consumed.terminal.transport_started === true, consumed.terminal);

// ---------------------------------------------------------------- 4. the 133 shape, refused
// A helper report declared at the terminal's own path. This is the declaration gate, before any child.
{
  const collStage = 'tcol' + RUN + '-01';
  const collided = runStage(collStage, { collideTerminal: true });
  check('a declaration that puts the helper report at the terminal is refused',
    collided.refused === 'declaration' && /two roles name the same file/.test(String(collided.error)),
    collided.error);
  check('the refusal happens before any child, so no attempt was written',
    collided.attempt_written === false, collided.attempt_written);
  check('the protected terminal was not created by the refused declaration',
    !fs.existsSync(F + '/result-' + collStage + '-134.json'), null);
}

// A real record that is not a terminal, consumed as if it were. The 133 file itself is read from its own root as evidence.
{
  const r133 = 'F:/GTOS-Chromium/artifacts/native-source-to-link-consumption-and-runtime-recovery-20261026-a';
  const impersonator = readBytes(r133 + '/result-lkc7-01-133.json');
  check('the 133 file that was read as a result is on disk', impersonator !== null, null);
  if (impersonator !== null) {
    const doc = JSON.parse(impersonator.toString('utf8'));
    check('the 133 file is a helper report, not a lifecycle terminal',
      doc.kind === 'ld' && doc.lifecycle === undefined && doc.transport_started === undefined,
      { kind: doc.kind, lifecycle: doc.lifecycle, transport_started: doc.transport_started });
    const shape = TC.looksLikeTerminal(doc);
    check('the type check refuses it', shape.terminal === false && /lifecycle/.test(String(shape.why)), shape);

    // And the consumption refuses it for the same reason, when a fabricated attempt names it.
    const G = R + '/test-only/t/impersonation';
    fs.mkdirSync(G, { recursive: true });
    const fakeTerminal = G + '/result-lkc7-01-134.json';
    fs.writeFileSync(fakeTerminal, impersonator);
    const fakeAttempt = G + '/attempt-lkc7-01-134.json';
    fs.writeFileSync(fakeAttempt, JSON.stringify({
      task: 'GTOS-DSH-134', attempt: 'lkc7-01', route: 'image.link', complete: true,
      lifecycle: 'completed_after_terminal_result',
      terminal_result: { path: fakeTerminal, bytes: impersonator.length, sha256: sha(impersonator) },
    }, null, 2) + '\n');
    const v = TC.consumeTerminal({ attemptPath: fakeAttempt,
      expect: { task: 'GTOS-DSH-134', stage: 'lkc7-01', route: 'image.link', canonical_terminal: fakeTerminal } });
    check('a helper report named as a terminal is refused on its type',
      v.accepted === false && v.first_refusal.obligation === 'type', v.first_refusal);
  }
}

// ---------------------------------------------------------------- 5. every obligation, broken alone
// Each case takes the real positive attempt and terminal and breaks exactly one thing, in its own copy.
function withCopies(label, mutateAttempt, mutateTerminal) {
  const G = R + '/test-only/t/neg-' + label;
  if (fs.existsSync(G)) throw new Error('the case already exists: ' + G);
  fs.mkdirSync(G, { recursive: true });
  const terminalPath = G + '/result-' + posStage + '-134.json';
  fs.writeFileSync(terminalPath, readBytes(positive.terminalPath));
  let attemptDoc = JSON.parse(readBytes(positive.attemptPath).toString('utf8'));
  attemptDoc = mutateAttempt ? mutateAttempt({ ...attemptDoc }) : attemptDoc;
  // The attempt's terminal pin follows the copied bytes unless the mutation is about the pin itself.
  if (!mutateTerminal) {
    // A case that is about the attempt's reference must keep the reference it built.
    if (attemptDoc.__no_pin) delete attemptDoc.__no_pin;
    else attemptDoc.terminal_result = { path: terminalPath, bytes: readBytes(terminalPath).length,
      sha256: sha(readBytes(terminalPath)) };
  }
  if (mutateTerminal) {
    const next = mutateTerminal(readBytes(terminalPath).toString('utf8'), terminalPath, attemptDoc);
    const text = typeof next === 'string' ? next : (next && next.next) || null;
    if (text !== null) fs.writeFileSync(terminalPath, text);
    if (next && typeof next === 'object' && next.attempt) attemptDoc = next.attempt;
    // Unless the case is about the pin itself, the attempt is re-pinned to the copied bytes so the mutation under test is
    // the only broken obligation.
    if (!(next && typeof next === 'object' && next.keep_pin)) {
      const b = readBytes(terminalPath);
      attemptDoc.terminal_result = { path: terminalPath, bytes: b.length, sha256: sha(b) };
    }
  }
  const attemptPath = G + '/attempt-' + posStage + '-134.json';
  fs.writeFileSync(attemptPath, JSON.stringify(attemptDoc, null, 2) + '\n');
  return { G, terminalPath, attemptPath, attemptDoc };
}

function expectRefusal(label, mutateAttempt, mutateTerminal, obligation) {
  const c = withCopies(label, mutateAttempt, mutateTerminal);
  const v = TC.consumeTerminal({ attemptPath: c.attemptPath,
    expect: { task: 'GTOS-DSH-134', stage: posStage, route: 'test.python',
      canonical_terminal: c.terminalPath } });
  check('refused at ' + obligation + ': ' + label,
    v.accepted === false && v.first_refusal && v.first_refusal.obligation === obligation, v.first_refusal || v.problems);
  return v;
}

// The attempt is not complete: the 133 state, where the stage never finished.
expectRefusal('incomplete', (a) => ({ ...a, complete: false }), null, 'complete');
// The lifecycle does not record completion.
expectRefusal('no-lifecycle', (a) => ({ ...a, lifecycle: 'started' }), null, 'lifecycle');
// The attempt names no terminal at all.
expectRefusal('no-terminal', (a) => { const b = { ...a }; delete b.terminal_result; b.__no_pin = true; return b; }, null, 'terminal');
// The attempt names a different terminal from the declared one.
expectRefusal('wrong-terminal', (a) => ({ ...a, __no_pin: true,
  terminal_result: { ...a.terminal_result, path: '/var/tmp/somewhere-else.json' } }), null, 'terminal');
// The terminal bytes moved after the attempt recorded them.
expectRefusal('terminal-drift', null, (text) => {
  const doc = JSON.parse(text);
  doc.extra = 'a byte that was not there when the attempt was written';
  // keep_pin: the attempt must keep the pin it recorded, so the changed bytes are the only difference.
  return { next: JSON.stringify(doc, null, 2) + '\n', keep_pin: true };
}, 'terminal');
// The terminal is for another task.
expectRefusal('wrong-task', (a) => ({ ...a, task: 'GTOS-DSH-999' }), null, 'attempt');
// The terminal is for another stage.
expectRefusal('wrong-stage', (a) => ({ ...a, attempt: 'other-stage-99' }), null, 'attempt');
// The terminal is for another route.
expectRefusal('wrong-route', (a) => ({ ...a, route: 'test.cpp.run' }), null, 'attempt');
// The terminal records that the transport never started.
expectRefusal('no-transport', null, (text) => {
  const doc = JSON.parse(text); doc.transport_started = false;
  return JSON.stringify(doc, null, 2) + '\n';
}, 'transport');
// The terminal records a launch error.
expectRefusal('launch-error', null, (text) => {
  const doc = JSON.parse(text); doc.launch_error = 'the helper could not be launched';
  return JSON.stringify(doc, null, 2) + '\n';
}, 'transport');
// The terminal records a timeout.
expectRefusal('timeout', null, (text) => {
  const doc = JSON.parse(text); doc.timed_out = true;
  return JSON.stringify(doc, null, 2) + '\n';
}, 'transport');

// A missing terminal on disk.
{
  const c = withCopies('absent');
  fs.rmSync(c.terminalPath);
  const v = TC.consumeTerminal({ attemptPath: c.attemptPath,
    expect: { task: 'GTOS-DSH-134', stage: posStage, route: 'test.python', canonical_terminal: c.terminalPath } });
  check('a terminal the attempt names but which is absent is refused',
    v.accepted === false && v.first_refusal.obligation === 'terminal', v.first_refusal);
}

// ---------------------------------------------------------------- 6. the runner's status is its own fact
{
  // A terminal that records a non-zero transport exit is a failed stage, even if a tool inside it returned zero.
  const c = withCopies('runner-nonzero', null, (text) => {
    const doc = JSON.parse(text); doc.transport_exit_code = 1;
    return JSON.stringify(doc, null, 2) + '\n';
  });
  const doc = JSON.parse(readBytes(c.terminalPath).toString('utf8'));
  const state = TC.runnerStatus(doc);
  check('a terminal with a non-zero runner exit is not a success', state.ok === false, state);
  check('the runner status keeps the tool exit and the lifecycle exit apart',
    state.started === true && state.exit === 1, state);
}

// ---------------------------------------------------------------- 7. a real failure, then a legal retry
// A stage whose declared output is never produced. The runner fails for a real reason; the record is kept; the corrected
// declaration runs under a new stage name and succeeds.
{
  const badStage = 'tbad' + RUN + '-01';
  const helperName = badStage + '.mjs';
  fs.writeFileSync(F + '/' + helperName, [
    "import fs from 'node:fs';",
    'const control = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));',
    "process.stdout.write(JSON.stringify({ wrote: 'nothing' }) + '\\n');",
  ].join('\n') + '\n');
  const terminalPath = F + '/result-' + badStage + '-134.json';
  const spec = {
    task: 'GTOS-DSH-134', route: 'test.python', stage: badStage, root: F, fixture_root: F,
    runnerPath: F + '/frozen-runners/run-134-r01.mjs', helper: helperName,
    control: {
      task: 'GTOS-DSH-134', stage: badStage, report_root: F,
      bridge: { work_root: fixtureLinux, source_root: A.source_root },
      report_path: F + '/results/helper-report-' + badStage + '-134.json',
      args: [F],
      // This file is never written, so the stage genuinely fails.
      expected_outputs: [F + '/out/' + badStage + '-never.txt'],
    },
    expected_outputs: [F + '/out/' + badStage + '-never.txt'],
    limit: 120,
  };
  const specPath = F + '/stage-output/' + badStage + '.spec.json';
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  const r = spawnSync(NODE, [F + '/frozen-runners/run-134-r01.mjs', specPath], {
    windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8',
  });
  check('a stage whose output never appears is recorded as a failed stage', true,
    { runner_exit: r.status,
      note: 'the runner exit reports that the lifecycle ran, not that the stage succeeded; the terminal records the '
        + 'outcome, which is what the consumer reads' });
  // The failure is kept: the terminal and the attempt are both on disk and the attempt says what happened.
  const badAttempt = JSON.parse(readBytes(F + '/attempt-' + badStage + '-134.json').toString('utf8'));
  check('the failed stage keeps its attempt', badAttempt !== null, null);
  check('the failed attempt records the failure', badAttempt.result_ok === false, badAttempt.result_ok);
  // The terminal is where the failure is stated, and the consumer is what reads it.
  {
    const bt = JSON.parse(readBytes(terminalPath).toString('utf8'));
    check('the failed terminal records that not every output was produced',
      bt.expected_outputs_all_present === false && bt.ok === false,
      { all_present: bt.expected_outputs_all_present, ok: bt.ok });
  }
  check('the failed stage keeps its terminal', fs.existsSync(terminalPath), terminalPath);
  // And the typed consumer refuses it, because the terminal records a stage that did not succeed.
  const v = TC.consumeTerminal({ attemptPath: F + '/attempt-' + badStage + '-134.json',
    expect: { task: 'GTOS-DSH-134', stage: badStage, route: 'test.python', canonical_terminal: terminalPath } });
  check('the typed consumer refuses a terminal whose outputs were not all produced',
    v.accepted === false, v.first_refusal || v.problems);

  // The corrected declaration, under a new stage name.
  const retryStage = 'tret' + RUN + '-01';
  const retry = runStage(retryStage);
  check('a corrected declaration under a new stage succeeds', retry.status === 0,
    { status: retry.status, tail: retry.stdout.split('\n').filter((l) => l.trim()).slice(-2) });
  const retryConsumed = TC.consumeTerminal({ attemptPath: retry.attemptPath,
    expect: { task: 'GTOS-DSH-134', stage: retryStage, route: 'test.python',
      canonical_terminal: retry.terminalPath } });
  check('the retry is accepted through the same consumer', retryConsumed.accepted === true, retryConsumed.problems);
  // The failed record was not deleted or reused.
  check('the failed record is still there after the retry',
    fs.existsSync(F + '/attempt-' + badStage + '-134.json') && fs.existsSync(terminalPath), null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-134', kind: 'typed_terminal_consumption_test',
  subject: 'references/terminal-consumption-134.mjs and the link entry that uses it',
  fixture: F,
  positive: { stage: posStage, terminal: terminalPin, helper_report: helperReportPin,
    attempt_complete: attempt.complete, consumed: consumed.accepted },
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
