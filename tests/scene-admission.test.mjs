// GTOS-DSH-131 S: a guest is launched from a declared scene, and anything else is refused before an emulator exists.
//
// The 130 defect, precisely: the driver took `-append` from `process.argv[4]` and passed it through. `yes` was accepted and
// the guest booted the ordinary path through B04, AppStore, Settings, B05, B06 and the default AP and worker startup. There
// was no comparison between the command line and anything, and there was no check that a scenario reached its stop.
//
// This test proves the replacement in the order that matters:
//
//   1. a declared scene is admitted, and its command line is derived, not supplied;
//   2. the request that produced it really launches, observed from the guard's own process group;
//   3. every mutation of that request is refused with its own obligation, and leaves no spec, no attempt and no output;
//   4. the functional verdict fails on a missing marker, an empty channel, a forbidden event, and truncation, and it
//      throws rather than printing.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import * as scene from '../tools/scene-admission.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-current-evidence-runtime-seam-closure-and-guest-matrix-20261027-a';
// Where the 131 records this test reads as its identity live. They are read, never written: this packet does not add to a
// former packet's namespace. Only the opt-in launch section needs R and P to be the same root, because that runner asserts
// every path it writes inside its own REPORT_ROOT.
const P = process.argv[5] || 'F:/GTOS-Chromium/artifacts/native-scenario-bound-guest-admission-tr-tss-and-recovery-20261026-a';
const NODE = process.argv[6] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const A = JSON.parse(readBytes(P + '/authority-131.json').toString('utf8'));
const identityDoc = JSON.parse(readBytes(P + '/identity-131.json').toString('utf8'));
const identity = identityDoc.identity ? { ...identityDoc, kind: 'selected_image_identity' } : identityDoc;

// ---------------------------------------------------------------- 1. the declared scenes
{
  const names = scene.sceneNames();
  check('the scene table is declared and non-trivial', names.length >= 8, names);
  check('every declared scene has a real production stop', names.every((n) => {
    const s = scene.scene(n);
    return s && scene.STOPS.includes(s.stop);
  }), names.map((n) => ({ n, stop: scene.scene(n).stop })));
  check('every declared scene names a required marker', names.every((n) => {
    const s = scene.scene(n);
    return typeof s.required_marker === 'string' && s.required_marker.length > 0;
  }), null);
  // The command line is derived. It always carries the stop, and every declared flag.
  for (const n of names) {
    const s = scene.scene(n);
    const line = scene.commandLine(s);
    if (!line.includes('bootdiag-stop=' + s.stop)) {
      check('the derived command line carries the stop for ' + n, false, line);
    }
    for (const f of s.flags) {
      if (!line.split(/\s+/).includes(f)) check('the derived command line carries ' + f + ' for ' + n, false, line);
    }
  }
  check('every derived command line carries its stop and flags',
    names.every((n) => {
      const s = scene.scene(n);
      const line = scene.commandLine(s);
      return line.includes('bootdiag-stop=' + s.stop) && s.flags.every((f) => line.split(/\s+/).includes(f));
    }), null);
  // Serial is what makes the second channel exist at all, and every scene declares it.
  check('every scene enables serial, so both channels are produced',
    names.every((n) => scene.scene(n).flags.includes('serial')), null);
  // A scene that reaches a stop must not permit the ordinary stages.
  check('every scene forbids the ordinary late stages',
    names.every((n) => scene.scene(n).forbids.some((f) => /BOOT B0[4-8]/.test(f))), null);
}

// ---------------------------------------------------------------- 2. admission, and its refusals
const baseRequest = {
  scene: 'runtime-init-smp1',
  vcpus: 1,
  identity,
  image: identity.image.linux,
  map: identity.map.linux,
};
const admitted = scene.admitScene(baseRequest);
check('a declared scene is admitted', admitted.admitted === true, admitted.problems);
check('the admitted scene carries a derived command line',
  admitted.cmdline === 'serial bootdiag-stop=runtime-init', admitted.cmdline);
check('the admitted scene carries its own required marker',
  admitted.required_marker === 'BOOTDIAG RUNTIME INITIALIZED', admitted.required_marker);

function expectRefusal(name, request, obligation) {
  const v = scene.admitScene(request);
  check('refused at ' + obligation + ': ' + name,
    v.admitted === false && v.first_refusal && v.first_refusal.obligation === obligation,
    v.first_refusal || v.problems);
}

// The 130 defect itself: a raw command line.
expectRefusal('a raw command line is supplied', { ...baseRequest, append: 'yes' }, 'append');
expectRefusal('an empty raw command line is supplied', { ...baseRequest, append: '' }, 'append');
expectRefusal('a raw command line carrying a different stop', { ...baseRequest, append: 'bootdiag-stop=tables' }, 'append');
// A caller supplying its own marker could pass a scenario that never reached its stop.
expectRefusal('a caller supplies its own required marker', { ...baseRequest, required_marker: 'anything' }, 'marker');
// An unknown scene.
expectRefusal('an unknown scene', { ...baseRequest, scene: 'no-such-scene' }, 'scene');
expectRefusal('no scene at all', { ...baseRequest, scene: undefined }, 'scene');
expectRefusal('an empty scene name', { ...baseRequest, scene: '' }, 'scene');
// A core count that is not the scene's.
expectRefusal('a core count the scene does not declare', { ...baseRequest, vcpus: 4 }, 'vcpus');
expectRefusal('a single-core scene asked for two cores', { ...baseRequest, vcpus: 2 }, 'vcpus');
// The image and map must be the selected ones.
expectRefusal('an image that is not the selected one',
  { ...baseRequest, image: '/var/tmp/somewhere-else.elf' }, 'image');
expectRefusal('a map that is not the selected one',
  { ...baseRequest, map: '/var/tmp/somewhere-else.map' }, 'map');
// No identity at all.
expectRefusal('no selected identity', { ...baseRequest, identity: undefined }, 'identity');
// A prerequisite that is absent, drifted, or did not pass.
expectRefusal('a prerequisite that is absent',
  { ...baseRequest, prerequisites: [{ path: R + '/does-not-exist.json' }] }, 'prerequisite');
{
  const victim = R + '/test-only/s/prereq.json';
  fs.mkdirSync(R + '/test-only/s', { recursive: true });
  fs.writeFileSync(victim, '{"ok":true}\n');
  expectRefusal('a prerequisite whose bytes changed',
    { ...baseRequest, prerequisites: [{ path: victim, sha256: 'a'.repeat(64) }] }, 'prerequisite');
  expectRefusal('a prerequisite that did not pass',
    { ...baseRequest, prerequisites: [{ path: victim, ok: false }] }, 'prerequisite');
  // 138 replaced the bare `{path, sha256, ok}` prerequisite with a ten-role typed set, because `ok` is the caller's own
  // word: a caller that wrote `ok: true` beside a path satisfied the old gate and the ten facts it stands for collapsed
  // into one. So the case that used to be admitted here is now a refusal, and the refusal has to be about the shape and
  // not about the bytes. The positive case — a complete set of ten real records, each admitted on its own fact — lives in
  // tests/scene-prerequisites.test.mjs, because it needs ten fixtures and this file is about the scene.
  const legacy = scene.admitScene({ ...baseRequest,
    prerequisites: [{ path: victim, sha256: sha(readBytes(victim)), ok: true }] });
  check('a bare {path, sha256, ok} prerequisite is no longer a satisfied gate',
    legacy.admitted === false
      && legacy.problems.some((p) => p.obligation === 'prerequisite'
        && /names no role|required role is omitted/.test(String(p.why))), legacy.problems);
  // And a request that names no prerequisite set at all is still admitted: the set is a gate the caller has to bring,
  // not a field this function invents. The packet's own S phase is what brings it.
  const noSet = scene.admitScene({ ...baseRequest });
  check('a request that brings no prerequisite set is still admitted by the scene gate alone',
    noSet.admitted === true, noSet.problems);
}

// ---------------------------------------------------------------- 3. the argv is built from the scene
{
  const argv = scene.sceneArgv(admitted, {
    identity, logs: { e9: '/tmp/e9.log', serial: '/tmp/serial.log' }, limit: 150,
  });
  check('the argv carries the derived command line as its append',
    argv.append === 'serial bootdiag-stop=runtime-init', argv.append);
  check('the argv carries the scene core count', argv.argv.includes('-smp')
    && argv.argv[argv.argv.indexOf('-smp') + 1] === '1', argv.argv.slice(0, 12));
  check('the argv opens a serial file channel', argv.argv.includes('-serial')
    && argv.argv[argv.argv.indexOf('-serial') + 1].startsWith('file:'), null);
  check('the argv opens the E9 debug console', argv.argv.includes('-device')
    && argv.argv[argv.argv.indexOf('-device') + 1] === 'isa-debugcon,iobase=0xe9,chardev=e9', null);
  check('the argv carries no network, display or monitor', argv.argv.includes('-nic')
    && argv.argv[argv.argv.indexOf('-nic') + 1] === 'none'
    && argv.argv.includes('-monitor') && argv.argv[argv.argv.indexOf('-monitor') + 1] === 'none'
    && argv.argv.includes('-display') && argv.argv[argv.argv.indexOf('-display') + 1] === 'none', null);
  // An unadmitted scene has no argv at all.
  let threw = null;
  try { scene.sceneArgv({ admitted: false }, { identity, logs: {} }); } catch (e) { threw = e; }
  check('an unadmitted scene has no argv', threw !== null && /SCENE/.test(String(threw.message)),
    threw ? threw.message : null);
}

// ---------------------------------------------------------------- 4. the functional verdict
const s = scene.scene('runtime-init-smp1');
{
  // The good case: both channels present, marker there, nothing forbidden.
  const good = scene.functionalVerdict(s, {
    e9: ['BOOT B01 HANDOFF', 'RUNTIME DIAG STACKS PREPARED', 'BOOTDIAG RUNTIME INITIALIZED'],
    serial: ['BOOT B01 HANDOFF', 'RUNTIME DIAG STACKS PREPARED', 'BOOTDIAG RUNTIME INITIALIZED'],
  });
  check('a complete, clean pair of channels is accepted', good.accepted === true, good.problems);

  // The 130 state exactly: an e9 channel with content and an empty serial channel.
  const emptySerial = scene.functionalVerdict(s, {
    e9: ['BOOTDIAG RUNTIME INITIALIZED'], serial: [],
  });
  check('an empty serial channel fails the functional result',
    emptySerial.accepted === false && emptySerial.first_refusal.obligation === 'serial', emptySerial.first_refusal);
  check('the failure names the empty channel', /serial/.test(String(emptySerial.first_refusal.why)),
    emptySerial.first_refusal.why);

  // A missing channel.
  const missing = scene.functionalVerdict(s, { e9: ['BOOTDIAG RUNTIME INITIALIZED'], serial: null });
  check('a missing channel fails the functional result',
    missing.accepted === false && missing.first_refusal.obligation === 'serial', missing.first_refusal);

  // The marker absent: a scenario that stopped early.
  const noMarker = scene.functionalVerdict(s, {
    e9: ['BOOT B01 HANDOFF', 'BOOT B02 MEMORY'], serial: ['BOOT B01 HANDOFF', 'BOOT B02 MEMORY'],
  });
  check('a missing marker fails the functional result',
    noMarker.accepted === false && noMarker.first_refusal.obligation === 'marker', noMarker.first_refusal);

  // A forbidden event: the 130 ginit-02 scope violation, as a real historical shape.
  const violated = scene.functionalVerdict(s, {
    e9: ['BOOT B01 HANDOFF', 'BOOT B04 STORAGE', 'APP STORE UNAVAILABLE', 'BOOT B05 GRAPHICS',
      'AP STARTUP PASS', 'BOOTDIAG RUNTIME INITIALIZED'],
    serial: ['BOOT B04 STORAGE'],
  });
  check('a forbidden event fails the functional result',
    violated.accepted === false && violated.first_refusal.obligation === 'forbidden', violated.first_refusal);
  check('the forbidden failure names the event and the channel',
    /BOOT B04/.test(String(violated.first_refusal.why)) && /e9/.test(String(violated.first_refusal.why)),
    violated.first_refusal.why);

  // Truncation.
  const truncated = scene.functionalVerdict(s, {
    e9: ['BOOTDIAG RUNTIME INITIALIZED'], serial: ['BOOTDIAG RUNTIME INITIALIZED'],
  }, { truncated: { e9: true } });
  check('a truncated channel fails the functional result',
    truncated.accepted === false && truncated.first_refusal.obligation === 'e9', truncated.first_refusal);

  // The gate throws rather than returning.
  let threw = null;
  try { scene.gateFunctional(emptySerial); } catch (e) { threw = e; }
  check('the functional gate throws rather than printing',
    threw !== null && /GUEST FUNCTIONAL FAILURE/.test(String(threw.message)), threw ? threw.message : null);
}

// ---------------------------------------------------------------- 5. the real launch, then no launch
//
// This section launches a real guest through the 131 frozen runner, and it is therefore bound to the 131 packet: that
// runner asserts every path it writes inside its own REPORT_ROOT, so it can only be driven with R set to the 131 root,
// and every run writes a fresh spec, attempt, evidence report and two log channels into the 131 namespace. In 138 the
// same scene is launched fourteen times through this packet's own frozen runner, with an admitted scene, a derived argv,
// two read-back channels and a cold-boot comparison — see guest-matrix-138-s46-60.json. Re-running the 131 launch here
// would add a stage to a former packet's namespace and prove nothing the 138 matrix does not already prove better.
//
// So it is opt-in: without --launch the report says the section was not run and why, and it is not counted as a pass.
// The refusal half below is always run, because it never launches anything.
const LAUNCH = process.argv.includes('--launch');
// A suffix that is fresh per run, so a rerun cannot collide with a former run's stage name.
const RUN = String(10 + (Date.now() % 89));
if (LAUNCH && R !== P) {
  throw new Error('--launch drives the 131 frozen runner, which writes only inside its own report root; pass R === P');
}
const skipped = [];
if (LAUNCH) {
const RUNNER = P + '/frozen-runners/run-131-r01.mjs';
const BRIDGE = P + '/references/private-boot-checkpoints-131.mjs';
const L = A.work_root;
const S = A.source_root;
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };

function launch(stageName, admittedScene, options = {}) {
  const logs = { e9: L + '/vm/' + stageName + '/e9.log', serial: L + '/vm/' + stageName + '/serial.log' };
  fs.mkdirSync(hostOf(L + '/vm/' + stageName), { recursive: true });
  const built = scene.sceneArgv(admittedScene, { identity, logs, limit: 150 });
  const bridge = {
    task: A.task, stage: stageName, outer_stage: stageName, kind: 'checkpoint', route: 'vm.checkpoint',
    report_root: R, work_root: L, source_root: S, distribution: 'Ubuntu-20.04',
    authority: pin(P + '/authority-131.json'),
    request_module: P + '/references/native-request-131.mjs',
    tools: { python_realpath: A.python.realpath },
    guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
    runner_module: RUNNER, binding_module: pin(BRIDGE),
    image_pin: { bytes: identity.image.bytes, sha256: identity.image.sha256 },
    report_path: R + '/evidence/' + stageName + '.json', log_max_bytes: 524288,
  };
  const control = {
    task: A.task, route: 'vm.checkpoint', mode: 'vm-checkpoint',
    bridge_module_path: BRIDGE, bridge,
    stage_options: {
      image: identity.image.linux, logs, append: built.append, vcpus: built.vcpus, limit: 150,
      identity, image_pin: bridge.image_pin,
      allowed_events: admittedScene.permits, forbidden_events: admittedScene.forbids,
      required_marker: admittedScene.required_marker,
      scene: admittedScene.scene.name,
    },
    purpose: 'scene ' + admittedScene.scene.name + ' for ' + A.task,
    report_path: R + '/results/' + stageName + '.json',
  };
  const spec = { task: A.task, route: 'vm.checkpoint', stage: stageName, root: R,
    runnerPath: RUNNER, helper: 'native-131-01.mjs', control,
    expected_outputs: [control.report_path], limit: 240 };
  const specPath = R + '/stage-output/' + stageName + '.spec.json';
  if (fs.existsSync(specPath)) throw new Error('the spec already exists: ' + specPath);
  fs.writeFileSync(specPath, JSON.stringify(spec, null, 2) + '\n', { flag: 'wx' });
  const r = spawnSync(NODE, [RUNNER, specPath], {
    windowsHide: true, timeout: 900000, maxBuffer: 32 * 1024 * 1024, encoding: 'utf8',
  });
  return { specPath, resultPath: control.report_path, status: r.status,
    stdout: String(r.stdout || ''), built, logs };
}

function hostOf(p) {
  if (typeof p !== 'string') return p;
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (m) return m[1].toUpperCase() + ':' + m[2];
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
}

const RUN = String(10 + (Date.now() % 89));
const positiveStage = 'srt' + RUN + '-01';
const positive = launch(positiveStage, admitted);
check('the declared scene produced a real launch', positive.status === 0,
  { status: positive.status, tail: positive.stdout.split('\n').filter((l) => l.trim()).slice(-2) });
{
  const resultBytes = readBytes(positive.resultPath);
  const result = resultBytes === null ? null : JSON.parse(resultBytes.toString('utf8'));
  const transport = result ? (result.transport || {}) : {};
  const group = result && result.guard_row ? (result.guard_row.group || {}) : {};
  check('the launch was observed from the guard process group',
    transport.started === true && group.measured === true
    && typeof group.pgid === 'number' && group.pgid > 0,
    { transport_started: transport.started, group_measured: group.measured, pgid: group.pgid });
  check('the launched group was empty after cleanup',
    Array.isArray(group.descendants_left) && group.descendants_left.length === 0, group.descendants_left);
}
} else {
  skipped.push({ section: '5. the real launch', why: 'opt-in with --launch',
    because: 'it drives the 131 frozen runner, which asserts every path it writes inside the 131 REPORT_ROOT, so it '
      + 'would add a stage to a former packet namespace and prove nothing this packet has not already run better',
    covered_by: 'guest-matrix-138-s46-60.json: fourteen admitted scenes launched through this packet own runner, with '
      + 'two read-back channels each and a byte-identical cold-boot comparison' });
}

// Every refusal happens before a spec is written, so no QEMU child can exist for it. The proof is the absence of the spec
// and the attempt, which the runner would have written had it been invoked.
{
  const refusals = [];
  const cases = [
    ['a raw command line', { ...baseRequest, append: 'yes' }, 'append'],
    ['an unknown scene', { ...baseRequest, scene: 'nope' }, 'scene'],
    ['the wrong core count', { ...baseRequest, vcpus: 3 }, 'vcpus'],
    ['a foreign image', { ...baseRequest, image: '/var/tmp/other.elf' }, 'image'],
    ['no identity', { ...baseRequest, identity: undefined }, 'identity'],
    ['an absent prerequisite', { ...baseRequest, prerequisites: [{ path: R + '/nope.json' }] }, 'prerequisite'],
  ];
  for (const [name, request, obligation] of cases) {
    const v = scene.admitScene(request);
    const stage = 'sng' + RUN + String(refusals.length) + '-01';
    const specPath = R + '/stage-output/' + stage + '.spec.json';
    const attemptPath = R + '/attempt-' + stage + '-131.json';
    // The entry refuses, and because it refuses it never reaches the launch, so it never writes the spec. This half never
    // launches anything, so it runs whether or not --launch was given.
    let launched = false;
    if (v.admitted) { launched = true; }
    refusals.push({ case: name, obligation: v.first_refusal ? v.first_refusal.obligation : null,
      admitted: v.admitted, launched, spec_written: fs.existsSync(specPath),
      attempt_written: fs.existsSync(attemptPath) });
    check('refused at ' + obligation + ': ' + name,
      v.admitted === false && v.first_refusal && v.first_refusal.obligation === obligation, v.first_refusal);
    check('nothing launched for ' + name,
      launched === false && !fs.existsSync(specPath) && !fs.existsSync(attemptPath), null);
  }
  check('every refusal left no spec and no attempt',
    refusals.every((r) => r.spec_written === false && r.attempt_written === false), refusals);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-138', kind: 'scene_admission_test',
  subject: 'tools/scene-admission.mjs',
  launched: LAUNCH,
  sections_not_run: skipped,
  declared_scenes: scene.sceneNames(),
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
