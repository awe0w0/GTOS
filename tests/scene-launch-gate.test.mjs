// GTOS-DSH-139 T: a machine may only be started from an admitted scene.
//
// `admitScene` refusing a request is worth nothing if the launch boundary will still start a machine for a caller who
// skipped it. Before this packet it would: `emulatorRequest` built the checkpoint argv from whatever it was handed, and
// `options.extra` was spliced straight into that argv, so a caller could append any emulator token it liked. The scene
// gate sat beside the launch path rather than in it.
//
// This file tests the boundary itself, calling the real `emulatorRequest` the bridge calls. It does not start a machine:
// every case either refuses before an argv exists, or builds an argv that is never executed. The claim is about the
// boundary's decisions, not about a guest.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as SA from '../tools/scene-admission.mjs';
import * as BP from '../tools/private-boot-checkpoints.mjs';
import { buildSubstrate, membersFrom } from './prerequisite-substrate.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-mandatory-evidence-hardware-recovery-and-portable-closure-20261027-a';
const OUT = process.argv[3] || null;
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const ROOT = path.join(R, 'test-only', 'launch-gate-' + process.pid + '-' + Date.now());
const F = buildSubstrate(ROOT);
const CTX = { build_started_at: F.buildStartedAt, runner_path: F.tools.find((t) => t.role === 'runner').path };

// The control object the bridge reads. Only the fields the checkpoint path touches are present.
const control = {
  source_root: ROOT,
  work_root: ROOT,
  report_root: R,
};
// The runtime tree the checkpoint path names is built by the substrate, under the same names the bridge expects, so the
// argv this file inspects is the one the bridge would really build.

const imageBytes = fs.readFileSync(F.elf.path);
// The bridge speaks Linux paths, because the child it describes is a Linux process. The fixture files are on the Windows
// side of this environment, so the identity names their /mnt alias. Nothing is executed, so the alias never has to resolve
// — but it is the same path the rest of the packet would use, not a placeholder.
const linuxOf = (p) => {
  const m = /^([A-Za-z]):[\\/](.*)$/.exec(p);
  return m ? '/mnt/' + m[1].toLowerCase() + '/' + m[2].replace(/\\/g, '/') : p.replace(/\\/g, '/');
};
const identity = {
  kind: 'selected_image_identity', identity: 'sel-139',
  image: { linux: linuxOf(F.elf.path), bytes: imageBytes.length, sha256: sha(imageBytes) },
  map: { linux: linuxOf(F.map.path), bytes: F.map.bytes, sha256: F.map.sha256 },
};
const logs = { e9: linuxOf(path.join(ROOT, 'e9.log')), serial: linuxOf(path.join(ROOT, 'serial.log')) };

function admittedScene(name) {
  const req = {
    scene: name, identity,
    image: identity.image.linux, map: identity.map.linux,
    prerequisites: membersFrom(F), prerequisite_context: CTX,
  };
  const r = SA.admitScene(req);
  if (r.admitted !== true) throw new Error('the fixture scene was not admitted: ' + JSON.stringify(r.problems));
  return r;
}
// Call the boundary and report whether it refused, and why. A refusal is a thrown Error, which is what the bridge's
// `need` does; the message is the reason.
function boundary(options) {
  try {
    const plan = BP.emulatorRequest(control, {}, options);
    return { refused: false, plan };
  } catch (e) {
    return { refused: true, why: e.message };
  }
}

const scene = admittedScene('runtime-init-smp1');
const append = SA.commandLine(SA.scene('runtime-init-smp1'));

// ---------------------------------------------------------------- 1. an admitted scene starts a machine
{
  const r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append, vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene, stage: 'lg-01' });
  check('an admitted scene builds the checkpoint argv', r.refused === false, r.why);
  check('the argv carries the derived append line', r.refused === false
    && r.plan.argv[r.plan.argv.indexOf('-append') + 1] === append, r.refused ? r.why : r.plan.append);
  check('the argv starts a machine and says so', r.refused === false && r.plan.machine_started === true);
  check('the argv carries the declared core count', r.refused === false
    && r.plan.argv[r.plan.argv.indexOf('-smp') + 1] === '1');
  // The version and help kinds must not require a scene: they ask the emulator a question and start no machine.
  const v = boundary({ kind: 'version', stage: 'lg-02' });
  check('a version request needs no scene and starts no machine',
    v.refused === false && v.plan.machine_started === false);
  const h = boundary({ kind: 'help', stage: 'lg-03' });
  check('a help request needs no scene and starts no machine',
    h.refused === false && h.plan.machine_started === false);
}

// ---------------------------------------------------------------- 2. the boundary refuses what the gate refused
{
  // No scene at all.
  let r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append, vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, stage: 'lg-04' });
  check('a checkpoint with no admitted scene is refused',
    r.refused === true && /needs the admitted scene/.test(r.why), r.why);

  // A forged scene: exactly the shape, none of the provenance.
  const forged = { admitted: true, scene: SA.scene('runtime-init-smp1'), cmdline: append, vcpus: 1,
    problems: [], first_refusal: null };
  r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append, vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene: forged, stage: 'lg-05' });
  check('a forged scene object cannot start a machine',
    r.refused === true && /did not come from admitScene/.test(r.why), r.why);

  // Caller-supplied extra emulator tokens: the 138 hole, closed.
  r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append, vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene,
    extra: ['-append', 'yes'], stage: 'lg-06' });
  check('caller-supplied extra emulator tokens are refused',
    r.refused === true && /extra emulator tokens/.test(r.why), r.why);

  // An append line that is not the scene's.
  r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append: 'serial bootdiag-stop=memory', vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene, stage: 'lg-07' });
  check('an append line that is not the scene command line is refused',
    r.refused === true && /not the one the admitted scene derives/.test(r.why), r.why);

  // A core count that is not the scene's.
  r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append, vcpus: 2,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene, stage: 'lg-08' });
  check('a core count that is not the scene core count is refused',
    r.refused === true && /core count is not the one the admitted scene declares/.test(r.why), r.why);

  // An append line that carries the scene's tokens but the wrong stop.
  r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append: 'bootdiag-stop=runtime-init', vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene, stage: 'lg-09' });
  check('an append line missing the scene serial token is refused',
    r.refused === true, r.why);
}

// ---------------------------------------------------------------- 3. the scene cannot be re-derived for another stop
{
  // Two scenes are declared with different stops. An admission for one cannot be used to run the other: the append line
  // check is against the scene the object names, and the object's cmdline must equal that scene's derivation.
  const a = admittedScene('runtime-init-smp1');
  const b = admittedScene('tables-smp1');
  check('two different scenes derive two different command lines',
    a.cmdline !== b.cmdline && a.cmdline === 'serial bootdiag-stop=runtime-init'
    && b.cmdline === 'serial bootdiag-stop=tables', [a.cmdline, b.cmdline]);
  // Using scene a's object with scene b's append line is refused.
  const r = boundary({ kind: 'checkpoint', image: identity.image.linux, logs, append: b.cmdline, vcpus: 1,
    identity, image_pin: { bytes: imageBytes.length, sha256: sha(imageBytes) }, scene: a, stage: 'lg-10' });
  check('an admission for one scene cannot run another scene stop',
    r.refused === true && /not the one the admitted scene derives/.test(r.why), r.why);
}

// ---------------------------------------------------------------- report
const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-139', kind: 'scene_launch_gate_test',
  subject: 'tools/private-boot-checkpoints.mjs emulatorRequest checkpoint boundary',
  fixture_root: ROOT,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
  not_claimed: ['that a machine was started', 'that a guest ran', 'that the argv was executed'],
};
if (OUT) {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
