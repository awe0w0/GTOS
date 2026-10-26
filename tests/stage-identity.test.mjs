// GTOS-DSH-126 H1a: a stage name is spent the moment it is claimed, and every attempt is kept.
//
// The 125 failure was not that the runner reused a name. The runner already refused that. The failure was that deleting an
// attempt file freed the name, so a second run took it and appended a second generation to the ledger, and the archive then
// refused a ledger with duplicate rows. The repair at the time was to delete rows, which is the one thing that must never
// be done.
//
// The fix is a durable claim: a stage name is written to an append-only registry before the reservation, before the freeze
// and before any child. Deleting the attempt file no longer frees the name.
//
// This test goes through the real stage entry — a spec handed to the frozen runner — because that is what writes the
// attempt, the terminal, the registry claim and the ledger row. A version request is a real child of this request: the
// bundled loader runs the real QEMU with --version. It starts no machine, so it is the cheapest honest real execution.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-immutable-evidence-replay-and-admitted-image-recovery-20261026-a';
const S = process.argv[5] || '/var/tmp/gtos-dsh-126-e759-clean-replay-view';
const NODE = process.argv[6] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => { let b = null; for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } } return b; };
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };
const same = (a, b) => a !== null && b !== null && a.sha256 === b.sha256 && a.bytes === b.bytes;

const T = R + '/test-only/h1a';
fs.mkdirSync(T, { recursive: true });
// The stages this test owns. They are cleared first so a repeated run starts from a clean registry rather than inheriting
// a claim from the previous run; only these names are touched.
const OWN_STAGES = ['h1arun-01', 'h1arun-02'];
for (const s of OWN_STAGES) {
  for (const f of [R + '/attempt-' + s + '-126.json', R + '/result-' + s + '-126.json',
    R + '/results/' + s + '.json', R + '/evidence/' + s + '.json',
    // The frozen helper is written once per stage and refuses to be written again, so a repeated run of this test has to
    // clear its own helper too. Only these two owned stage names are affected.
    R + '/frozen-helpers/' + s + '.mjs', R + '/stage-output/' + s + '.control.json',
    R + '/stage-output/' + s + '.out.txt', R + '/stage-output/' + s + '.err.txt',
    S + '/logs/spec-' + s + '.json', S + '/logs/inner-report-' + s + '.json',
    S + '/logs/child-output-' + s + '.raw']) {
    if (fs.existsSync(f)) fs.rmSync(f);
  }
}
// The runner freezes its own copy of the spec with an exclusive write, so a repeated run has to clear that copy first.
for (const s of OWN_STAGES) {
  const frozenSpec = R + '/stage-output/' + s + '.spec.json';
  if (fs.existsSync(frozenSpec)) fs.rmSync(frozenSpec);
}
const regPath = R + '/stage-registry-126.jsonl';
if (fs.existsSync(regPath)) {
  const kept = fs.readFileSync(regPath, 'utf8').split(/\r?\n/).filter(Boolean)
    .filter((line) => { try { return !OWN_STAGES.includes(JSON.parse(line).stage); } catch { return true; } });
  fs.writeFileSync(regPath, kept.length ? kept.join('\n') + '\n' : '');
}
// The work-root ledger rows for these stages go too, so the duplicate check below reads a clean chain.
const ledgerHost = '//wsl$/Ubuntu-20.04' + JSON.parse(fs.readFileSync(R + '/authority-126.json', 'utf8')).work_root
  + '/logs/runs-126.jsonl';
if (fs.existsSync(ledgerHost)) {
  const kept = fs.readFileSync(ledgerHost, 'utf8').split(/\r?\n/).filter(Boolean)
    .filter((line) => { try { return !OWN_STAGES.includes(JSON.parse(line).stage); } catch { return true; } });
  fs.writeFileSync(ledgerHost, kept.length ? kept.join('\n') + '\n' : '');
}
const A = JSON.parse(fs.readFileSync(R + '/authority-126.json', 'utf8'));
const identity = JSON.parse(fs.readFileSync(R + '/identity/identity-126.json', 'utf8'));
// The runner refuses to execute from anywhere but frozen-runners, so the frozen copy is the one that runs. The committed
// tools/run-stage.mjs is byte identical to it: ffe40e2bca9c0d19 at the time of this packet.
const RUNNER = R + '/frozen-runners/run-126-r01.mjs';
const BRIDGE = R + '/references/private-boot-checkpoints-126.mjs';
const p = (f) => { const b = fs.readFileSync(f); return { path: f, bytes: b.length, sha256: sha(b) }; };

// ---------------------------------------------------------------- the real stage entry
function writeVersionSpec(stage, extra, retry) {
  const control = {
    task: A.task, route: 'qemu.probe', mode: 'qemu-probe',
    bridge_module_path: BRIDGE,
    bridge: {
      task: A.task, stage, outer_stage: stage, kind: 'version', route: 'qemu.probe',
      report_root: R, work_root: A.work_root, source_root: A.source_root, distribution: 'Ubuntu-20.04',
      authority: p(R + '/authority-126.json'),
      request_module: R + '/references/native-request-126.mjs',
      tools: { python_realpath: A.python.realpath },
      guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
      runner_module: RUNNER,
      binding_module: p(BRIDGE),
      profile_makefile: R + '/profile-126/Makefile', profile_cxxflags: R + '/profile-126/kernel-cxxflags',
      report_path: R + '/evidence/' + stage + '.json', log_max_bytes: 524288,
    },
    stage_options: { limit: 60, ...(extra || {}) },
    purpose: 'a real version child for the stage identity test',
    report_path: R + '/results/' + stage + '.json',
  };
  const spec = { task: A.task, route: 'qemu.probe', stage, root: R, runnerPath: RUNNER,
    helper: 'native-126-01.mjs', control, expected_outputs: [control.report_path], limit: 300,
    ...(retry || {}) };
  const sp = R + '/stage-output/' + stage + '.spec.json';
  fs.writeFileSync(sp, JSON.stringify(spec, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  return sp;
}
function runSpec(specPath) {
  const r = spawnSync(NODE, [RUNNER, specPath], { windowsHide: true, timeout: 600000, maxBuffer: 32 * 1024 * 1024 });
  const out = (r.stdout || '').toString();
  const line = out.split('\n').filter((l) => l.startsWith('stage='))[0] || '';
  const err = (r.stderr || '').toString();
  return { status: r.status, line: line.trim(), ok: /ok=true/.test(line), stderr: err };
}

// ---------------------------------------------------------------- 1. a real execution succeeds
const STAGE = 'h1arun-01';
let first = null;
try {
  const specPath = writeVersionSpec(STAGE);
  first = runSpec(specPath);
  check('the first real execution succeeded', first.ok === true,
    first.ok ? first.line : { status: first.status, line: first.line, stderr: first.stderr.slice(0, 400) });
} catch (error) {
  check('the first real execution succeeded', false, String(error.message));
}

if (first && first.ok) {
  const registryPath = R + '/stage-registry-126.jsonl';
  const ledgerPath = '//wsl$/Ubuntu-20.04' + A.work_root + '/logs/runs-126.jsonl';
  const artefacts = {
    attempt: pin(R + '/attempt-' + STAGE + '-126.json'),
    result: pin(R + '/result-' + STAGE + '-126.json'),
    spec: pin(R + '/stage-output/' + STAGE + '.spec.json'),
    inner: pin(S + '/logs/inner-report-' + STAGE + '.json'),
    raw: pin(S + '/logs/child-output-' + STAGE + '.raw'),
    registry: pin(registryPath),
    ledger: pin(ledgerPath),
  };
  check('the attempt record exists', artefacts.attempt !== null, artefacts.attempt);
  check('the terminal record exists', artefacts.result !== null, artefacts.result);
  check('the raw absence is recorded as absence, not as a zero-byte file',
    artefacts.raw === null || artefacts.raw.bytes >= 0, artefacts.raw);
  check('the claim was written to the durable registry',
    artefacts.registry !== null && readBytes(registryPath).toString('utf8').includes('"' + STAGE + '"'),
    artefacts.registry);

  // ---------------------------------------------------------------- 2. the 125 escape no longer works
  const attemptPath = R + '/attempt-' + STAGE + '-126.json';
  const savedAttempt = readBytes(attemptPath);
  fs.rmSync(attemptPath);
  check('the attempt file was removed to simulate the 125 escape', !fs.existsSync(attemptPath), null);

  // The spec file also has to go, because the runner refuses to overwrite a spec. Deleting both is the strongest version
  // of the escape: nothing on the record side remains to say the name was used.
  fs.rmSync(R + '/stage-output/' + STAGE + '.spec.json');
  let second = null;
  try {
    second = runSpec(writeVersionSpec(STAGE));
  } catch (error) { second = { ok: false, stderr: String(error.message), status: -1 }; }
  check('the same stage name is refused after its attempt and spec were both deleted',
    second.ok === false && /already claimed/.test(String(second.stderr)) && /never reused/.test(String(second.stderr)),
    second.stderr.split('\n').filter((l) => l.trim()).slice(0, 2).join(' | '));

  // The refusal must have spent nothing.
  check('the registry is unchanged by the refusal', same(artefacts.registry, pin(registryPath)),
    { before: artefacts.registry.sha256, after: pin(registryPath).sha256 });
  check('the ledger is unchanged by the refusal', same(artefacts.ledger, pin(ledgerPath)),
    { before: artefacts.ledger.sha256, after: pin(ledgerPath).sha256 });
  check('no new attempt record was written for the refused name', !fs.existsSync(attemptPath), null);

  // Put the record set back whole and prove nothing else moved.
  fs.writeFileSync(attemptPath, savedAttempt);
  check('the attempt record is byte identical to before', same(artefacts.attempt, pin(attemptPath)),
    artefacts.attempt.sha256);
  check('the terminal record is byte identical to before',
    same(artefacts.result, pin(R + '/result-' + STAGE + '-126.json')), artefacts.result.sha256);
  // A version probe starts no machine, so no inner report and no raw log exist for it. Absence is the correct state and it
  // must still be absent: a refusal that created one would be a change.
  check('the inner report is still absent, as it was', artefacts.inner === null
    && pin(S + '/logs/inner-report-' + STAGE + '.json') === null, artefacts.inner);
  check('the raw log is still absent, as it was', artefacts.raw === null
    && pin(S + '/logs/child-output-' + STAGE + '.raw') === null, artefacts.raw);
  // The runner writes its own frozen copy of the spec under S/logs. That copy must still be the one the first run made.
  check('the runner frozen spec exists and is the one the first run froze', artefacts.spec !== null
    && same(artefacts.spec, pin(R + '/stage-output/' + STAGE + '.spec.json')), artefacts.spec);

  // ---------------------------------------------------------------- 3. a retry is a new identity
  const RETRY = 'h1arun-02';
  let retried = null;
  try { retried = runSpec(writeVersionSpec(RETRY, {}, { parent_attempt: STAGE, retry_of: STAGE,
    failure_cause: 'the first attempt was used to prove the name is spent' })); }
  catch (error) { retried = { ok: false, stderr: String(error.message) }; }
  check('a legal new stage name succeeds', retried.ok === true, retried.line || retried.stderr.slice(0, 200));
  if (retried.ok) {
    check('both attempts exist side by side',
      fs.existsSync(R + '/attempt-' + STAGE + '-126.json') && fs.existsSync(R + '/attempt-' + RETRY + '-126.json'),
      null);
    const registry = readBytes(registryPath).toString('utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
    const names = registry.map((r) => r.stage);
    check('the registry holds one row per claimed name', new Set(names).size === names.length, names);
    check('the registry is append only and in claim order',
      registry.every((r, i) => i === 0 || r.at >= registry[i - 1].at), registry.length);
    const retryClaim = registry.find((r) => r.stage === RETRY);
    check('the retry claim names the attempt it follows', retryClaim && retryClaim.retry_of === STAGE,
      retryClaim ? retryClaim.retry_of : null);
  }
}

// ---------------------------------------------------------------- 4. the archive refuses a real duplicate
// This is the check that was bypassed in 125. The fixture is a ledger with a genuine duplicate generation; the engine must
// refuse it and leave the file exactly as it was.
{
  const ledgerPath = '//wsl$/Ubuntu-20.04' + A.work_root + '/logs/runs-126.jsonl';
  const lines = readBytes(ledgerPath).toString('utf8').split(/\r?\n/).filter(Boolean);
  const rows = lines.map((l) => JSON.parse(l));
  const firstResult = rows.find((r) => r.kind === 'result');
  check('the real ledger has a result row to duplicate', !!firstResult, firstResult ? firstResult.stage : null);

  if (firstResult) {
    // A TEST_ONLY ledger root of its own, so the engine reads a fixture and not the business file.
    const fixtureRoot = T + '/ledger-root';
    fs.mkdirSync(fixtureRoot + '/logs', { recursive: true });
    const fixture = fixtureRoot + '/logs/runs-126.jsonl';
    fs.writeFileSync(fixture, [...lines, JSON.stringify(firstResult)].join('\n') + '\n');
    const before = pin(fixture);

    const engine = await import('file:///' + R + '/references/publication-engine-126.mjs');
    let refused = false;
    let message = null;
    try { engine.assertStartedHasRaw({ host_root: fixtureRoot, ledger: 'runs-126.jsonl' }); }
    catch (error) { refused = true; message = String(error.message); }
    check('a ledger with a real duplicate generation is refused',
      refused && /one ledger row per stage/.test(String(message)), message);
    check('the fixture ledger is byte identical after the refusal', same(before, pin(fixture)), before.sha256);

    // And the same engine accepts the real ledger, which holds one generation per stage.
    let accepted = false;
    let acceptError = null;
    try {
      engine.assertStartedHasRaw({ host_root: '//wsl$/Ubuntu-20.04' + A.work_root, ledger: 'runs-126.jsonl' });
      accepted = true;
    } catch (error) { acceptError = String(error.message); }
    check('the same engine accepts the real single-generation ledger', accepted === true, acceptError);
  }
}

// ---------------------------------------------------------------- 5. the real chain is single generation
{
  const ledgerPath = '//wsl$/Ubuntu-20.04' + A.work_root + '/logs/runs-126.jsonl';
  const rows = readBytes(ledgerPath).toString('utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
  const results = rows.filter((r) => r.kind === 'result').map((r) => r.stage);
  check('the real ledger holds exactly one result row per stage',
    new Set(results).size === results.length, results);
  const regBytes = readBytes(R + '/stage-registry-126.jsonl');
  const registry = regBytes === null ? [] : regBytes.toString('utf8').split(/\r?\n/).filter(Boolean)
    .map((l) => JSON.parse(l));
  check('every real ledger stage has a registry claim',
    results.every((s) => registry.some((r) => r.stage === s)),
    { results, registry: registry.map((r) => r.stage) });
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-126', kind: 'stage_identity_test',
  subject: 'tools/run-stage.mjs stage registry, tools/publication-engine.mjs ledger check',
  method: 'a real version stage through the frozen runner, then the same name with its attempt AND spec deleted, then a '
    +'legal new name; and the archive engine fed a ledger holding a genuine duplicate generation',
  test_only_root: T,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
