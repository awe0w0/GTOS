// GTOS-DSH-125 F1b: refusal at the real dispatcher, proved by this call's own observer.
//
// No process table is read here and no shell is started to count anything. The observer is attached to the same transport
// call the business path makes; the positive case requires it to report a launch, and each negative requires it to report
// nothing. A refusal that happened after a launch would be visible as a launch event, so "no launch" is a fact about this
// request rather than an absence of evidence.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as dispatch from '../tools/request-dispatch.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-producer-business-wiring-runtime-recovery-20261026-a';
const S = process.argv[5] || '/var/tmp/gtos-dsh-125-e759-producer-recovery-view';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const hostOf = (linux) => {
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(linux);
  return m ? m[1].toUpperCase() + ':/' + m[2] : '//wsl$/Ubuntu-20.04' + linux;
};

const identity = JSON.parse(fs.readFileSync(R + '/identity/identity-125.json', 'utf8'));
check('the identity was read from disk', identity.kind === 'selected_image_identity',
  identity.identity.slice(0, 16));

const goodRequest = {
  identity: identity.identity,
  image: identity.image.linux,
  image_pin: { linux: identity.image.linux, host: identity.image.host,
    bytes: identity.image.bytes, sha256: identity.image.sha256 },
  map_pin: { linux: identity.map.linux, host: identity.map.host,
    bytes: identity.map.bytes, sha256: identity.map.sha256 },
};

// ---------------------------------------------------------------- the positives prove the observer works
// A `version` request is a real child of this request: the bundled loader runs the real QEMU with --version. It starts no
// machine, so it is the cheapest honest proof that the observer sees a launch.
const bridge = await import('file:///' + R + '/references/private-boot-checkpoints-125.mjs');
const A = JSON.parse(fs.readFileSync(R + '/authority-125.json', 'utf8'));
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
const control = {
  task: A.task, report_root: R, work_root: A.work_root, source_root: A.source_root,
  distribution: 'Ubuntu-20.04', authority: pin(R + '/authority-125.json'),
  request_module: R + '/references/native-request-125.mjs',
  tools: { python_realpath: A.python.realpath },
  guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
  runner_module: R + '/frozen-runners/run-125-r01.mjs',
  binding_module: pin(R + '/references/private-boot-checkpoints-125.mjs'),
  profile_makefile: R + '/profile-125/Makefile', profile_cxxflags: R + '/profile-125/kernel-cxxflags',
  log_max_bytes: 524288,
  // The bridge requires a fresh report path under the report root for each call, so it is derived from the stage.
  report_path: R + '/evidence/ver-01.json',
};

let positiveEvents = null;
try {
  const r = await dispatch.dispatchRequest({
    bridge, control, stage: 'ver-01', kind: 'version',
    stageOptions: { limit: 60 }, identity, request: goodRequest, host: hostOf,
  });
  positiveEvents = r.events;
  check('the admitted request really launched', r.launched === true, r.events.map((e) => e.event));
  check('the observer saw a real transport settle', r.events.some((e) => e.event === 'settled'),
    r.events.filter((e) => e.event === 'settled'));
  check('the launch names the bundled loader as cmd0',
    /ld-linux/.test(String(r.events.find((e) => e.event === 'launch').cmd0)),
    r.events.find((e) => e.event === 'launch').cmd0);
} catch (error) {
  check('the admitted request really launched', false, String(error.message));
}

// ---------------------------------------------------------------- the negatives, at the same entry
const TEST_ONLY = R + '/test-only/dispatch';
fs.mkdirSync(TEST_ONLY, { recursive: true });
const swapped = TEST_ONLY + '/swapped.elf';
if (!fs.existsSync(swapped)) {
  const b = Buffer.from(fs.readFileSync(hostOf(identity.image.linux)));
  b[b.length - 1] = b[b.length - 1] ^ 0xff;
  fs.writeFileSync(swapped, b);
}

const negatives = [
  ['the identity is another identity', (r) => { r.identity = 'f'.repeat(64); }, 'identity'],
  ['the image path is not the selected path', (r) => { r.image = '/var/tmp/elsewhere.elf'; }, 'path'],
  ['the image path is not a Linux path', (r) => { r.image = 'F:/somewhere.elf'; }, 'path'],
  ['the image pin is another image', (r) => { r.image_pin = { ...r.image_pin, sha256: 'a'.repeat(64) }; }, 'image'],
  ['the image pin is another size', (r) => { r.image_pin = { ...r.image_pin, bytes: 1 }; }, 'image'],
  ['the map pin is another map', (r) => { r.map_pin = { ...r.map_pin, sha256: 'b'.repeat(64) }; }, 'map'],
  ['the request carries no image pin', (r) => { delete r.image_pin; }, 'image'],
];

for (const [name, mutate, obligation] of negatives) {
  const request = JSON.parse(JSON.stringify(goodRequest));
  mutate(request);
  const events = [];
  let refused = false;
  let reason = null;
  try {
    await dispatch.dispatchRequest({ bridge, control, stage: 'ver-02', kind: 'version',
      stageOptions: { limit: 60 }, identity, request, host: hostOf, observer: (e) => events.push(e) });
    reason = { passedUnexpectedly: true };
  } catch (error) {
    refused = true;
    reason = error.admission ? error.admission.first_refusal : { obligation: 'throw', why: String(error.message) };
  }
  check(`refused at ${obligation}: ${name}`, refused && reason.obligation === obligation, reason);
  check(`this request never reached the transport: ${name}`, events.length === 0, events);
}

// ---------------------------------------------------------------- the same path, different bytes
// The pin still says the approved hash, so only the independent byte read can catch it. The approved path is temporarily
// pointed at a TEST_ONLY file by using a host mapper that resolves the selected Linux path to the swapped bytes.
{
  const events = [];
  let refused = false;
  let reason = null;
  try {
    await dispatch.dispatchRequest({ bridge, control, stage: 'ver-03', kind: 'version',
      stageOptions: { limit: 60 }, identity, request: goodRequest,
      host: (linux) => (linux === identity.image.linux ? swapped : hostOf(linux)),
      observer: (e) => events.push(e) });
    reason = { passedUnexpectedly: true };
  } catch (error) {
    refused = true;
    reason = error.admission ? error.admission.first_refusal : { obligation: 'throw', why: String(error.message) };
  }
  check('refused when the approved path carries different bytes', refused && reason.obligation === 'image', reason);
  check('this request never reached the transport when the bytes were swapped', events.length === 0, events);
}

// ---------------------------------------------------------------- a different path with the approved bytes
{
  const events = [];
  let refused = false;
  let reason = null;
  const request = JSON.parse(JSON.stringify(goodRequest));
  request.image = '/mnt/f/GTOS-Chromium/artifacts/native-producer-business-wiring-runtime-recovery-20261026-a/test-only/dispatch/other-image.elf';
  fs.copyFileSync(hostOf(identity.image.linux), hostOf(request.image));
  try {
    await dispatch.dispatchRequest({ bridge, control, stage: 'ver-04', kind: 'version',
      stageOptions: { limit: 60 }, identity, request, host: hostOf, observer: (e) => events.push(e) });
    reason = { passedUnexpectedly: true };
  } catch (error) {
    refused = true;
    reason = error.admission ? error.admission.first_refusal : { obligation: 'throw', why: String(error.message) };
  }
  check('refused when the path is not the selected path even with the same bytes',
    refused && reason.obligation === 'path', reason);
  check('this request never reached the transport when the path differed', events.length === 0, events);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-125', kind: 'dispatcher_refusal_test',
  subject: 'references/request-dispatch-125.mjs',
  method: 'an observer attached to this request only; no process table is read and no shell counts anything',
  identity: { path: R + '/identity/identity-125.json', id: identity.identity,
    image: identity.image.sha256, objects: identity.objects.length },
  positive_events: positiveEvents,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
