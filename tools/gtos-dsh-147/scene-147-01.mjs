// GTOS-DSH-147 G: the guest scene helper.
//
// It implements no project logic and it decides nothing about the guest. It assembles the scene request from the control it was
// given, has the frozen admission gate judge that request, hands the same request to the frozen bridge so the bridge admits it
// again at its own launch boundary, starts the machine, and then reads the two channels the machine wrote and asks the frozen
// functional judge what they say.
//
// Two admissions of one request is not redundancy for its own sake. The gate here is the one that can report a refusal before a
// machine exists; the bridge's is the one that cannot be bypassed, because the emulator argv is only built from an object that
// came out of `admitScene`. A trial therefore records the gate's answer, the bridge's own answer, and the channels, and all three
// have to agree.
//
// The functional verdict is separate from the transport verdict. A launch that exited zero and printed nothing is a failed
// scene, and a scene whose marker is present but which also printed a forbidden event is a failed scene; neither is visible in
// an exit code.

import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
import * as HP from './host-path-147.mjs';

const TASK = 'GTOS-DSH-147';

function lines(text) {
  if (typeof text !== 'string') return null;
  return text.split('\n').map((l) => l.replace(/\r$/, ''));
}

async function main() {
  const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  if (control.task !== TASK) throw new Error(`control is for '${control.task}'`);
  if (!control.bridge || typeof control.bridge !== 'object') throw new Error('the stage control carries no bridge control');
  if (typeof control.report_root !== 'string' || !control.report_root) {
    throw new Error('the stage control declares no report_root, so this helper has no authorised write root');
  }
  const so = control.stage_options || {};
  const SA = await import(pathToFileURL(so.scene_module_path).href);
  const bridge = await import(pathToFileURL(control.bridge_module_path).href);
  if (typeof bridge.runBootStage !== 'function') throw new Error('no runBootStage');
  if (typeof SA.admitScene !== 'function') throw new Error('no admitScene');

  const request = so.scene_request;
  if (!request || typeof request !== 'object') throw new Error('the stage control carries no scene request');

  // The gate, before anything is started.
  const gate = SA.admitScene(request);
  const declared = gate.scene || null;
  const summary = {
    task: TASK, stage: control.stage, kind: 'checkpoint',
    scene: request.scene,
    gate: {
      admitted: gate.admitted === true,
      problems: gate.problems || [],
      first_refusal: gate.first_refusal || null,
      cmdline: gate.cmdline || null,
      vcpus: gate.vcpus === undefined ? null : gate.vcpus,
      required_marker: gate.required_marker || null,
    },
    launch: null,
    functional: null,
    transport: null,
    channels: null,
    ok: false,
  };
  if (!gate.admitted) {
    summary.ok_means = 'the scene request was refused by the gate, so no machine was started';
    HP.writeGatedFile('scene-helper:report', control.report_path, [control.report_root], 'win32',
      JSON.stringify(summary, null, 2) + '\n');
    console.log(JSON.stringify({ ok: false, stage: control.stage, refused_by_gate: gate.first_refusal }));
    process.exitCode = 1;
    return;
  }

  const bc = control.bridge;
  const options = {
    ...so,
    purpose: control.purpose,
    // The bridge admits the request itself; the gate's answer travels beside it and has to name the same scene, the same
    // command line and the same core count.
    scene_request: request,
    scene_admission: { scene: gate.scene.name, cmdline: gate.cmdline, vcpus: gate.vcpus },
  };
  const report = await bridge.runBootStage(bc, bc.stage, bc.kind, options);
  summary.launch = {
    cmd0: report.plan.cmd0, image: report.plan.image, argv_tokens: report.plan.argv_tokens,
    outputs: report.plan.outputs, limit_seconds: report.plan.limit_seconds,
    exec_set: report.plan.exec_set,
    append: so.append, vcpus: so.vcpus,
    session: so.session === undefined ? null : so.session,
  };
  summary.transport = report.transport;
  summary.bridge_verdict = report.verdict;
  summary.inner_report = report.inner_report;
  summary.raw = report.raw;

  // The two channels the machine itself wrote.
  const e9Path = so.logs_host && so.logs_host.e9;
  const serialPath = so.logs_host && so.logs_host.serial;
  // The emulator is given Linux paths because it runs inside the distribution. This helper reads the same two files from the
  // Windows host, so the control carries the host form of each as well. The frozen path module refuses to convert a POSIX path
  // for a Windows consumer, which is right: the conversion belongs where the producer knew both locations, and that is the
  // driver that chose the log names.
  const readText = (p) => {
    if (typeof p !== 'string') return null;
    const host = p;
    for (let i = 0; i < 40; i++) {
      try { return fs.readFileSync(host, 'utf8'); } catch {
        const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, 150);
      }
    }
    return null;
  };
  const e9Text = readText(e9Path);
  const serialText = readText(serialPath);
  const e9Lines = lines(e9Text);
  const serialLines = lines(serialText);
  summary.channels = {
    e9: { path: e9Path, linux_path: so.logs && so.logs.e9, present: e9Text !== null,
      bytes: e9Text === null ? 0 : Buffer.byteLength(e9Text, 'utf8'),
      lines: e9Lines === null ? null : e9Lines.length },
    serial: { path: serialPath, linux_path: so.logs && so.logs.serial, present: serialText !== null,
      bytes: serialText === null ? 0 : Buffer.byteLength(serialText, 'utf8'),
      lines: serialLines === null ? null : serialLines.length },
  };
  // A log bound is a truncation risk, and a truncated channel cannot be judged as complete.
  const bound = bc.log_max_bytes;
  const truncated = {
    e9: typeof bound === 'number' && summary.channels.e9.bytes >= bound,
    serial: typeof bound === 'number' && summary.channels.serial.bytes >= bound,
  };
  const functional = SA.functionalVerdict(declared, { e9: e9Lines, serial: serialLines }, { truncated });
  summary.functional = functional;
  summary.ok = functional.accepted === true && report.ok === true;
  summary.ok_means = 'the gate admitted the scene, the bridge admitted the same request at its own boundary, the machine '
    + 'started and exited as the transport records, and both channels carry the scene own marker with no forbidden event';
  HP.writeGatedFile('scene-helper:report', control.report_path, [control.report_root], 'win32',
    JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ ok: summary.ok, stage: control.stage, scene: request.scene,
    functional: functional.accepted, first_refusal: functional.first_refusal,
    e9_lines: summary.channels.e9.lines, serial_lines: summary.channels.serial.lines,
    report: control.report_path }));
  if (!summary.ok) process.exitCode = 1;
}

main().catch((error) => {
  console.error(String(error && error.stack ? error.stack : error));
  process.exitCode = 1;
});
