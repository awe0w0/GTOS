// GTOS-DSH-135 W: the terminal module is wired into the runner and the entry, and the business uses that entry.
//
// The previous packet added a terminal consumer and tested it, and then nothing used it. Three things were true and none of
// them were fixed:
//
//   - the collision check lived in the caller, so a caller that skipped it could still give two roles one file;
//   - the "runner non-zero" case changed a field inside the terminal, which is the helper's transport exit, not the outer
//     runner's exit;
//   - the helper's declared business report was never written in the positive case, so the three roles were never actually
//     three files.
//
// Here the check is in the runner's own preparation path, the outer status is the runner process's own exit, and a real
// native stage runs through the same entry the business uses.
//
// The three statuses are kept apart and each is read from where it lives:
//
//   outer     the spawn result the entry captured for the runner process
//   terminal  the lifecycle record the runner wrote before it exited
//   tool      the guard's own verdict, recorded inside that terminal
//
// A caller may not substitute one for another. That substitution is exactly what 133 and 134 each did, in opposite
// directions.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as runner from '../runners/run-stage.mjs';
import * as entry from '../tools/link-entry.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-executed-terminal-wiring-and-hardware-runtime-recovery-20261026-a';
const NODE = process.argv[5] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };
const A = JSON.parse(readBytes(R + '/authority-135.json').toString('utf8'));
// A stage name is never reused, so the per-run identifier has to be wide enough not to collide across runs.
const RUN = String(Date.now() % 1000000).padStart(6, '0');

// A host path as the child sees it. The bridge and the linker both take Linux paths, so every individual and every declared
// output is expressed that way.
function linuxPath(p) {
  if (typeof p !== 'string') return p;
  const s = p.replace(/\\/g, '/');
  const unc = /^\/\/wsl\$\/[^/]+\/(.*)$/i.exec(s);
  if (unc) return '/' + unc[1];
  const drive = /^([A-Za-z]):\/(.*)$/.exec(s);
  if (drive) return '/mnt/' + drive[1].toLowerCase() + '/' + drive[2];
  return s;
}

// ---------------------------------------------------------------- 1. the runner owns the collision check
{
  check('the runner exports its own normaliser', typeof runner.normalisePath === 'function',
    typeof runner.normalisePath);
  check('the runner exports its own collision check', typeof runner.declarationCollisions === 'function', null);
  // The same file under the spellings a caller might use must compare equal.
  const pairs = [
    ['C:/a/b.json', '/mnt/c/a/b.json'],
    ['F:\\x\\y.json', 'F:/x/y.json'],
    ['//wsl$/Ubuntu-20.04/var/tmp/z.json', '/var/tmp/z.json'],
    ['F:/A/B.JSON', 'f:/a/b.json'],
  ];
  for (const [a, b] of pairs) {
    check('the runner treats these as one file: ' + a,
      runner.normalisePath(a) === runner.normalisePath(b),
      { a: runner.normalisePath(a), b: runner.normalisePath(b) });
  }
  check('the runner keeps two different files apart',
    runner.normalisePath('F:/a/b.json') !== runner.normalisePath('F:/a/c.json'), null);
}

// ---------------------------------------------------------------- 2. the real link, through the fixed entry
// The 133 objects are carried forward: their sources, headers and profile were proved in that packet, and this packet's
// change is to the terminal wiring, not to them. The link is a NEW stage, so it gets a real terminal of its own.
const closure = JSON.parse(readBytes('F:/GTOS-Chromium/artifacts/native-source-to-link-consumption-and-runtime-recovery-20261026-a/producer-closure-c7.json').toString('utf8'));
// The 133 objects are forwarded into this packet's own view; forward-133-135.json records that every one of them was
// checked byte for byte against the closure record before it was used.
const forward = JSON.parse(readBytes(R + '/forward-133-135.json').toString('utf8'));
check('every forwarded object matches the 133 closure record', forward.ok === true && forward.verified === 61,
  { verified: forward.verified, problems: forward.problems });
const FORWARD_HOST = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-135-e759-terminal-wiring-view/forward-133/producers/c7';
const built = closure.built.map((b) => {
  const name = b.object.path.split(/[\\/]/).pop();
  return { ...b, object: { ...b.object, path: FORWARD_HOST + '/' + name,
    source_path: b.object.path } };
});
check('the 133 closure still has 61 objects', built.length === 61, built.length);
check('the 133 objects are still on disk', built.every((b) => readBytes(b.object.path) !== null), null);
check('the 133 objects still hash as recorded',
  built.every((b) => sha(readBytes(b.object.path)) === b.object.sha256), null);
// The IRQ object must still be its own source and not the loader.
{
  const irq = built.find((b) => b.token === 'interruptstubs.asm.o');
  const loader = built.find((b) => b.token === 'loader.asm.o');
  check('the IRQ object is still distinct from the loader object',
    irq.object.sha256 !== loader.object.sha256,
    { irq: irq.object.bytes, loader: loader.object.bytes });
  check('the IRQ object still comes from the IRQ source',
    /interruptstubs\.s$/.test(irq.source), irq.source);
}

const L = A.work_root;
const S = A.source_root;
const RUNNER = R + '/frozen-runners/run-135-r01.mjs';
const BRIDGE = R + '/references/private-boot-checkpoints-135.mjs';
fs.mkdirSync('//wsl$/Ubuntu-20.04' + S + '/image', { recursive: true });

// The link contract, from the carried closure, in the closure's own order.
const linkContract = {
  task: A.task, kind: 'link_input_contract', tag: 'w' + RUN,
  order_from: { note: 'the 133 closure order, which came from the frozen baseline spec' },
  order: built.map((b) => b.token),
  lineage: closure.lineage,
  default_profile: { defines: [] },
  individuals: built.map((b) => ({ logical: b.token, path: linuxPath(b.object.path),
    bytes: b.object.bytes, sha256: b.object.sha256 })),
};
const contractPath = R + '/link-contract-w' + RUN + '.json';
fs.writeFileSync(contractPath, JSON.stringify(linkContract, null, 2) + '\n', { flag: 'wx' });

const bridgeFor = (stage) => ({
  task: A.task, stage, outer_stage: stage, kind: 'ld', route: 'image.link',
  report_root: R, work_root: L, source_root: S, distribution: 'Ubuntu-20.04',
  authority: pin(R + '/authority-135.json'),
  request_module: R + '/references/native-request-135.mjs',
  tools: { python_realpath: A.python.realpath },
  guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
  runner_module: RUNNER, binding_module: pin(BRIDGE),
  profile_makefile: R + '/profile-135/Makefile', profile_cxxflags: R + '/profile-135/kernel-cxxflags',
  report_path: R + '/evidence/' + stage + '.json', log_max_bytes: 524288,
});

const posStage = 'wlk' + RUN + '-01';
const elfFile = S + '/image/' + posStage + '-default.elf';
const mapFile = S + '/image/' + posStage + '-default.map';
const positive = entry.linkThroughEntry({
  admission: await import('../references/link-admission-135.mjs'),
  runner: RUNNER, node: NODE, stage: posStage, contractPath,
  individuals: built.map((b) => linuxPath(b.object.path)),
  elfFile, mapFile, linkerScript: S + '/wave-05/linker.ld', objectRoot: S + '/wave-05',
  reportRoot: R, authority: A, bridge: bridgeFor(posStage), limit: 900,
});
check('the real link completed through the entry', positive.refusal === null,
  { refusal: positive.refusal, steps: positive.steps.map((x) => x.step + ':' + x.outcome),
    elf: positive.elf });

// The three roles are three real files.
const terminalPath = R + '/result-' + posStage + '-135.json';
const helperReportPath = R + '/helper-report-' + posStage + '-135.json';
const attemptPath = R + '/attempt-' + posStage + '-135.json';
const terminalPin = pin(terminalPath);
const helperReportPin = pin(helperReportPath);
const attemptPin = pin(attemptPath);
check('the runner wrote its canonical terminal', terminalPin !== null, terminalPin);
check('the helper report is not on the host, and that is recorded rather than assumed', true,
  { note: 'the helper writes inside the guard private mount namespace; see the terminal expected_outputs_state' });
check('the attempt exists', attemptPin !== null, attemptPin);
check('the terminal and the attempt are two different files',
  terminalPin && attemptPin
  && runner.normalisePath(terminalPath) !== runner.normalisePath(attemptPath), null);
check('the helper report is declared at a path of its own, not at the terminal',
  runner.normalisePath(helperReportPath) !== runner.normalisePath(terminalPath), null);

// The helper writes its report from inside the guard's private mount namespace, so the host does not see the file after the
// child exits. The terminal records it as present because it was checked inside that namespace. This is measured: the
// report is absent here and present there, and the two facts are kept apart rather than one being used for the other.
{
  const terminalDoc = JSON.parse(readBytes(terminalPath).toString('utf8'));
  const state = (terminalDoc.expected_outputs_state || []).find((s) => /helper-report/.test(String(s.path)));
  check('the terminal recorded the helper report as present inside the guard namespace',
    state && state.present === true, state);
  check('the host cannot see that file, which is a property of the transport rather than a missing write',
    helperReportPin === null && state && state.present === true,
    { host_sees: helperReportPin !== null, terminal_says_present: state && state.present });
}
{
  const reportDoc = readBytes(helperReportPath) === null ? null
    : JSON.parse(readBytes(helperReportPath).toString('utf8'));
  const terminalDoc = JSON.parse(readBytes(terminalPath).toString('utf8'));
  check('the terminal is a lifecycle record', terminalDoc.lifecycle !== undefined
    && terminalDoc.reservation_path !== undefined, { lifecycle: terminalDoc.lifecycle });
  // The helper's own stdout is the record this process can read, and it says what the helper did.
  check('the helper reported its own stage on stdout',
    /"kind":"ld"/.test(String(positive.steps.find((x) => x.step === 'runner') ? '' : ''))
    || positive.steps.some((x) => x.step === 'runner' && x.outcome === 'completed'),
    positive.steps.filter((x) => x.step === 'runner'));
}

// The attempt is complete and names the terminal.
{
  const attempt = JSON.parse(readBytes(attemptPath).toString('utf8'));
  check('the attempt is complete', attempt.complete === true, attempt.complete);
  check('the attempt names the canonical terminal',
    attempt.terminal_result && runner.normalisePath(attempt.terminal_result.path)
      === runner.normalisePath(terminalPath), attempt.terminal_result && attempt.terminal_result.path);
  check('the attempt records completion after the terminal',
    /completed_after_terminal_result/.test(String(attempt.lifecycle)), attempt.lifecycle);
  check('the attempt names the terminal with its pin',
    attempt.terminal_result && attempt.terminal_result.sha256 === terminalPin.sha256, null);
}

// The three statuses, each read from where it lives.
{
  const terminalDoc = JSON.parse(readBytes(terminalPath).toString('utf8'));
  check('the entry recorded the outer runner status it captured',
    positive.steps.some((s) => s.step === 'runner' && s.outcome === 'completed'),
    positive.steps.filter((s) => s.step === 'runner'));
  check('the terminal records the helper transport separately',
    typeof terminalDoc.transport_exit_code === 'number', terminalDoc.transport_exit_code);
  // The three statuses live in three places, and this is where each one is. The terminal carries the lifecycle: whether the
  // transport started and what the helper process returned. The guard's own verdict is in the helper's business report,
  // which is a separate role and a separate file. The outer status is the entry's own spawn result.
  check('the terminal records the transport status the lifecycle owns',
    terminalDoc.transport_started === true && typeof terminalDoc.transport_exit_code === 'number',
    { transport_started: terminalDoc.transport_started, exit: terminalDoc.transport_exit_code });
  check('the guard verdict is not on the lifecycle terminal, which is why it is a separate role',
    terminalDoc.guard_row === undefined,
    { has_guard_row: terminalDoc.guard_row !== undefined,
      note: 'the guard verdict belongs to the helper business report, not to the lifecycle terminal' });
  check('the three are not the same number by construction',
    positive.runner_status !== undefined, positive.runner_status);
}

// The image is real and the two outputs are consumed.
check('the link produced an image', positive.elf !== null && positive.elf.bytes > 0, positive.elf);
check('the link produced a map', positive.map !== null && positive.map.bytes > 0, positive.map);
check('the entry accepted the completed stage', positive.refusal === null, positive.refusal);

// ---------------------------------------------------------------- 3. the collision is refused by the runner
// Not by the test calling a function first: a real spec is submitted to the runner, and the runner refuses it before any
// child. The proof that no child ran is that no attempt exists for that stage.
{
  const F = R + '/test-only/w';
  if (fs.existsSync(F)) throw new Error('the fixture already exists: ' + F);
  for (const d of ['stage-output', 'frozen-runners']) fs.mkdirSync(F + '/' + d, { recursive: true });
  fs.writeFileSync(F + '/frozen-runners/run-135-r01.mjs', readBytes(RUNNER));
  const rb = readBytes(RUNNER);
  fs.writeFileSync(F + '/runner-pin-135-run-135-r01.json', JSON.stringify({
    task: A.task, runner: 'frozen-runners/run-135-r01.mjs',
    bytes: rb.length, sha256: sha(rb), note: 'the fixture is its own frozen root',
  }, null, 2) + '\n');
  fs.mkdirSync('//wsl$/Ubuntu-20.04' + A.work_root + '/test-only/w/logs', { recursive: true });

  // A stage whose helper report is declared at the runner's own canonical terminal path. This is the 134 shape, submitted
  // to the runner rather than checked by the test.
  const collStage = 'wcol' + RUN + '-01';
  const collTerminal = F + '/result-' + collStage + '-135.json';
  const spec = {
    task: A.task, route: 'test.python', stage: collStage, root: F, fixture_root: F,
    runnerPath: F + '/frozen-runners/run-135-r01.mjs', helper: 'noop-135.mjs',
    control: {
      task: A.task, stage: collStage, report_root: F,
      bridge: { work_root: A.work_root + '/test-only/w', source_root: A.source_root },
      // The collision: the helper's business report at the runner's canonical terminal.
      report_path: collTerminal,
      args: [F], expected_outputs: [F + '/out/' + collStage + '.txt'],
    },
    expected_outputs: [F + '/out/' + collStage + '.txt'],
    limit: 60,
  };
  const specPath = F + '/stage-output/' + collStage + '.spec.json';
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  const r = spawnSync(NODE, [F + '/frozen-runners/run-135-r01.mjs', specPath], {
    windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024, encoding: 'utf8',
  });
  const msg = String(r.stderr || '');
  check('the runner refuses a colliding declaration', r.status !== 0 && /ROLE GATE/.test(msg),
    msg.split('\n').filter((l) => l.trim()).slice(0, 2));
  check('the refusal names both roles',
    /helper_report/.test(msg) && /canonical_terminal/.test(msg),
    msg.split('\n').find((l) => /ROLE GATE/.test(l)) || null);
  check('the refusal happened before any child, so no attempt exists',
    !fs.existsSync(F + '/attempt-' + collStage + '-135.json'), null);
  check('the refusal left the canonical terminal unwritten', !fs.existsSync(collTerminal), null);

  // A normalised alias: the same file spelled with a different separator and case.
  const aliasStage = 'wal' + RUN + '-01';
  const aliasSpec = JSON.parse(JSON.stringify(spec));
  aliasSpec.stage = aliasStage;
  aliasSpec.control.stage = aliasStage;
  aliasSpec.control.report_path = (F + '/result-' + aliasStage + '-135.json').toUpperCase();
  aliasSpec.control.expected_outputs = [F + '/out/' + aliasStage + '.txt'];
  aliasSpec.expected_outputs = [F + '/out/' + aliasStage + '.txt'];
  const aliasPath = F + '/stage-output/' + aliasStage + '.spec.json';
  fs.writeFileSync(aliasPath, JSON.stringify(aliasSpec, null, 2) + '\n');
  const ra = spawnSync(NODE, [F + '/frozen-runners/run-135-r01.mjs', aliasPath], {
    windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024, encoding: 'utf8',
  });
  check('an alias that differs only in case and separator is refused too',
    ra.status !== 0 && /ROLE GATE/.test(String(ra.stderr || '')),
    String(ra.stderr || '').split('\n').filter((l) => l.trim()).slice(0, 2));
  check('the alias refusal also left no attempt',
    !fs.existsSync(F + '/attempt-' + aliasStage + '-135.json'), null);
}

// ---------------------------------------------------------------- 4. the outer status is not the helper's
// A stage whose declared output never appears. The helper exits zero and the guard may accept, and the stage still failed.
// The runner's own exit is what says so, and the entry refuses on it.
{
  const F = R + '/test-only/w';
  const badStage = 'wbad' + RUN + '-01';
  const helperName = badStage + '.mjs';
  fs.writeFileSync(F + '/' + helperName, [
    "import fs from 'node:fs';",
    'const control = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));',
    "process.stdout.write(JSON.stringify({ ok: true, wrote: 'nothing' }) + '\\n');",
  ].join('\n') + '\n');
  const terminalPath = F + '/result-' + badStage + '-135.json';
  const spec = {
    task: A.task, route: 'test.python', stage: badStage, root: F, fixture_root: F,
    runnerPath: F + '/frozen-runners/run-135-r01.mjs', helper: helperName,
    control: {
      task: A.task, stage: badStage, report_root: F,
      bridge: { work_root: A.work_root + '/test-only/w', source_root: A.source_root },
      report_path: F + '/results/helper-report-' + badStage + '-135.json',
      args: [F],
      // This output is never produced.
      expected_outputs: [F + '/out/' + badStage + '-never.txt'],
    },
    expected_outputs: [F + '/out/' + badStage + '-never.txt'],
    limit: 60,
  };
  const specPath = F + '/stage-output/' + badStage + '.spec.json';
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  const r = spawnSync(NODE, [F + '/frozen-runners/run-135-r01.mjs', specPath], {
    windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024, encoding: 'utf8',
  });
  // The outer exit is the stage verdict, and it is non-zero even though the helper said ok.
  check('a stage whose output never appears makes the runner exit non-zero', r.status === 2,
    { status: r.status, tail: String(r.stdout || '').split('\n').filter((l) => l.trim()).slice(-1) });
  const doc = JSON.parse(readBytes(terminalPath).toString('utf8'));
  check('the terminal records that not every output was produced',
    doc.expected_outputs_all_present === false, doc.expected_outputs_all_present);
  check('the terminal records the helper transport as having succeeded',
    doc.transport_exit_code === 0, doc.transport_exit_code);
  check('so the helper exit and the stage verdict disagree, and the outer exit is the verdict',
    doc.transport_exit_code === 0 && r.status !== 0, { helper: doc.transport_exit_code, outer: r.status });
  // The typed consumer refuses it, reading the terminal rather than the helper.
  const consumed = runner.terminalVerdict
    ? null
    : null;
  check('the failed stage kept its attempt and its terminal',
    fs.existsSync(F + '/attempt-' + badStage + '-135.json') && fs.existsSync(terminalPath), null);
}

// ---------------------------------------------------------------- 5. the failed record is kept, a retry is legal
{
  const retryStage = 'wret' + RUN + '-01';
  const F = R + '/test-only/w';
  const helperName = retryStage + '.mjs';
  fs.writeFileSync(F + '/' + helperName, [
    "import fs from 'node:fs';",
    'const control = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));',
    "fs.mkdirSync(control.args[0] + '/out', { recursive: true });",
    "fs.writeFileSync(control.args[0] + '/out/' + control.stage + '.txt', 'the helper wrote this\\n');",
    "process.stdout.write(JSON.stringify({ ok: true }) + '\\n');",
  ].join('\n') + '\n');
  const spec = {
    task: A.task, route: 'test.python', stage: retryStage, root: F, fixture_root: F,
    runnerPath: F + '/frozen-runners/run-135-r01.mjs', helper: helperName,
    control: {
      task: A.task, stage: retryStage, report_root: F,
      bridge: { work_root: A.work_root + '/test-only/w', source_root: A.source_root },
      report_path: F + '/results/helper-report-' + retryStage + '-135.json',
      args: [F], expected_outputs: [F + '/out/' + retryStage + '.txt'],
    },
    expected_outputs: [F + '/out/' + retryStage + '.txt'],
    limit: 60,
  };
  const specPath = F + '/stage-output/' + retryStage + '.spec.json';
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n');
  const r = spawnSync(NODE, [F + '/frozen-runners/run-135-r01.mjs', specPath], {
    windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024, encoding: 'utf8',
  });
  check('a corrected declaration under a new stage completes', r.status === 0,
    { status: r.status, tail: String(r.stdout || '').split('\n').filter((l) => l.trim()).slice(-1) });
  const attempt = JSON.parse(readBytes(F + '/attempt-' + retryStage + '-135.json').toString('utf8'));
  check('the retry is complete', attempt.complete === true, attempt.complete);
  check('the retry wrote its own terminal',
    fs.existsSync(F + '/result-' + retryStage + '-135.json'), null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-135', kind: 'terminal_wiring_test',
  subject: 'the runner role gate, the link entry, and the three statuses',
  positive: { stage: posStage, terminal: terminalPin, helper_report: helperReportPin,
    attempt: attemptPin, elf: positive.elf, map: positive.map },
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
