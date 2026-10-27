// GTOS-DSH-139 G: derive the matrix's command lines from the scene declarations, as a recorded artifact.
//
// The append line for every row comes from here and nowhere else. That is deliberate. An earlier version of the runner
// composed `serial bootdiag-stop=$STOP` in shell, which silently dropped the e99 scene's `bootlog-fail` token and ran the
// memory scene under the e99 name; the row then failed for a reason that had nothing to do with the kernel. Deriving the
// line once, from the declaration, and writing it to a file makes the mismatch impossible to reintroduce quietly: the
// verifier reads the same file and compares it to what the guest was actually given.
//
// The output is a TSV with a header, one row per scene, in boot order. It is written with flag 'wx' so a second run into
// the same directory fails rather than overwriting the record of the first.

import fs from 'node:fs';
import path from 'node:path';
import * as SA from './scene-admission.mjs';

const OUT = process.argv[2];
if (!OUT) { console.error('usage: derive-scenes-139.mjs <out.tsv>'); process.exit(2); }

// The packet's declared matrix, in boot order. This is not the module's whole scene table: a scene added to the module for
// some other purpose must not silently join this run, and a scene named here that the module does not declare must stop
// the derivation rather than be skipped.
const MATRIX = [
  'runtime-init-smp1', 'runtime-init-smp2', 'runtime-init-smp3',
  'runtime-recovery-smp1', 'runtime-recovery-smp2',
  'runtime-objects-smp1', 'runtime-objects-smp2', 'runtime-objects-smp3',
  'tables-smp1', 'paging-smp1', 'ap-fresh-smp2',
  'workers-smp1', 'workers-smp2', 'workers-smp3',
  'kernel-invalid', 'e99-priority',
];

const rows = [];
const problems = [];
for (const name of MATRIX) {
  const s = SA.scene(name);
  if (!s) { problems.push('the matrix names a scene the module does not declare: ' + name); continue; }
  const append = SA.commandLine(s);
  // The command line has to round-trip: the scene's own stop and flags must be recoverable from the string the guest is
  // given, or the string and the declaration have already drifted.
  if (!append.includes('bootdiag-stop=' + s.stop)) {
    problems.push(name + ': the derived command line does not carry the scene stop');
  }
  for (const flag of s.flags) {
    if (!append.split(' ').includes(flag)) problems.push(name + ': the derived command line drops the flag ' + flag);
  }
  rows.push({ name, vcpus: s.vcpus, stop: s.stop, append, marker: s.required_marker,
    also_required: s.also_required || [], forbids: s.forbids });
}
if (problems.length) { problems.forEach((p) => console.error('  ' + p)); process.exit(1); }

// Two rows must never share a command line: if they did, the matrix would be running the same scenario twice under two
// names, and the "matrix" would be one data point reported fourteen times.
const seen = new Map();
for (const r of rows) {
  const key = r.append + '|' + r.vcpus;
  if (seen.has(key)) problems.push('two scenes derive the same command line and core count: '
    + seen.get(key) + ' and ' + r.name + ' -> ' + key);
  seen.set(key, r.name);
}
if (problems.length) { problems.forEach((p) => console.error('  ' + p)); process.exit(1); }

const lines = ['scene\tvcpus\tstop\tappend\tmarker'];
for (const r of rows) lines.push([r.name, r.vcpus, r.stop, r.append, r.marker].join('\t'));
fs.mkdirSync(path.dirname(OUT), { recursive: true });
fs.writeFileSync(OUT, lines.join('\n') + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: true, scenes: rows.length, out: OUT }));
