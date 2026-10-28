// GTOS-DSH-147 native stage helper.
//
// It implements no project logic. It hands the frozen bridge control to the production bridge and records what the
// bridge returns.
//
// It runs on Windows Node, so every path it touches with fs must be a host path, and it must be inside the root this stage
// was given. The gate runs before the report's mkdir and before the open, so a wrong-domain or outside-root declaration is
// refused before this process creates anything.
//
// The write goes through HP.writeGatedFile, which is the same function the negatives call. 138 had the gate here and then
// a separate writer in the test, so the test proved that a writer refuses rather than that this one does.

import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
// The frozen copy of this helper runs from frozen-helpers/, and the shared path module is frozen beside it, so this import
// names a sibling. The runner refuses any other relative import.
import * as HP from './host-path-147.mjs';

const TASK = 'GTOS-DSH-147';

async function main() {
  const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  if (control.task !== TASK) throw new Error(`control is for '${control.task}'`);
  if (!control.bridge || typeof control.bridge !== 'object') {
    throw new Error('the stage control carries no bridge control');
  }
  // The root this stage may write inside, declared by the caller that gave it the work.
  if (typeof control.report_root !== 'string' || !control.report_root) {
    throw new Error('the stage control declares no report_root, so this helper has no authorised write root');
  }
  // The gate runs inside writeGatedFile, before its mkdir and before its open. Nothing is created if it refuses.
  const declared = control.report_path;

  const bridge = await import(pathToFileURL(control.bridge_module_path).href);
  if (typeof bridge.runBootStage !== 'function') throw new Error('no runBootStage');

  const bc = control.bridge;
  const report = await bridge.runBootStage(bc, bc.stage, bc.kind,
    { ...(control.stage_options || {}), purpose: control.purpose });

  const summary = {
    task: TASK, route: control.route, route_mode: control.mode,
    stage: report.stage, kind: report.kind,
    bridge_report: bc.report_path,
    // The subject the bridge actually built, so a consumer can compare the tool that ran with the tool this stage meant.
    subject: report.subject,
    plan: report.plan, transport: report.transport,
    guard_row: report.guard_row,
    // The two channels with the identity the bridge observed, not a name the consumer rebuilds.
    inner_report: report.inner_report,
    raw: report.raw,
    inner_report_present: report.inner_report_present,
    raw_present: report.raw_present, raw_bytes: report.raw_bytes, raw_sha256: report.raw_sha256,
    verdict: report.verdict,
    ok: report.ok === true,
    ok_means: 'the bridge accepted the transport, the tool return code and both channels. It says nothing about project behaviour.',
  };
  const written = HP.writeGatedFile('native-helper:report', declared, [control.report_root], 'win32',
    JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ ok: summary.ok, stage: summary.stage, kind: summary.kind,
    raw: summary.raw_bytes, report: written.path, report_sha256: written.sha256 }));
  if (!summary.ok) process.exitCode = 1;
}

main().catch((error) => {
  console.error(String(error && error.stack ? error.stack : error));
  process.exitCode = 1;
});
