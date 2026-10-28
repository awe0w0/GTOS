// GTOS-DSH-141 R: cold repeats of the recovery cases, each in its own process.
//
// A repeat that shares a process proves the code is re-entrant, not that it is repeatable: the allocator, the paging object and
// the slot table are file-scope storage in the unit, and a second case in the same process would inherit whatever the first one
// left. Every run here is a fresh process, admitted as its own guarded stage, and what is compared is the bytes each process
// wrote to its own raw channel.
//
// Two questions, and they are different:
//
//   identical bytes      does the same case produce the same output when nothing is shared between the runs?
//   identical frame sets does the recovery reach the same set of frames, read out of the output rather than assumed from it?
//
// The second is the one that matters for the recovery claim, so it is checked separately: two runs could print different
// incidental text and still recover the same frames, and a matching digest is a stronger statement than matching text.

const fs = require('fs');
const crypto = require('crypto');
const { spawnSync } = require('child_process');

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const DRIVE = R + '/stage-output/drive-141.mjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const TAG = process.argv[2] || 'h148';
const FIRST = Number(process.argv[3] || 49);
const REPEATS = Number(process.argv[4] || 3);
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const sleep = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };
const toHost = (l) => (/^\/mnt\/[a-z]\//.test(String(l))
  ? String(l).replace(/^\/mnt\/([a-z])\//, (m, d) => d.toUpperCase() + ':/')
  : '\\\\wsl$\\Ubuntu-20.04' + String(l));

// The cases whose recovery is being repeated. Case 9 is the exact baseline after every object was reaped; case 11 is the
// constructed leak, which must be reproduced exactly for the negative to mean anything.
const CASES = [9, 11];

function runCase(caseNo, stage) {
  const run = spawnSync(NODE, [DRIVE, 'runcase', stage, TAG, String(caseNo)],
    { windowsHide: true, timeout: 900000, maxBuffer: 32 * 1024 * 1024 });
  const p = R + '/evidence/' + stage + '.json';
  if (!fs.existsSync(p)) throw new Error('no evidence for ' + stage + (run.stderr ? ': ' + String(run.stderr).slice(0, 400) : ''));
  const d = JSON.parse(fs.readFileSync(p, 'utf8'));
  let bytes = null;
  for (let i = 0; i < 60 && bytes === null; i++) {
    try { bytes = fs.readFileSync(toHost(d.raw.linux_path)); } catch { sleep(250); }
  }
  if (bytes === null) throw new Error('no raw channel for ' + stage);
  const text = bytes.toString('utf8');
  const sets = {};
  const re = /FRAMESET ([a-z-]+) count ([0-9A-F]{8}) digest ([0-9A-F]{8})/g;
  let m;
  while ((m = re.exec(text)) !== null) sets[m[1]] = { count: parseInt(m[2], 16), digest: parseInt(m[3], 16) };
  return {
    stage, case: caseNo,
    tool_returncode: d.verdict ? d.verdict.tool_returncode : null,
    accepted: d.verdict ? d.verdict.accepted === true : false,
    raw_linux: d.raw.linux_path,
    raw_bytes: bytes.length,
    raw_sha256: sha256(bytes),
    pass_marker: /RUNTIME UNIT PASS/.test(text),
    fail_marker: /RUNTIME UNIT FAIL/.test(text),
    frame_sets: sets,
    raw_text: text,
  };
}

let stageNo = FIRST;
const runs = [];
for (const caseNo of CASES) {
  for (let i = 0; i < REPEATS; i++) {
    const stage = 'rc141-' + String(stageNo++).padStart(2, '0');
    runs.push(runCase(caseNo, stage));
    console.log('case ' + caseNo + ' repeat ' + (i + 1) + ' (' + stage + ')');
  }
}

// The verdict per case: identical bytes across every repeat, and the frame sets identical as well.
const verdicts = CASES.map((caseNo) => {
  const group = runs.filter((r) => r.case === caseNo);
  const hashes = [...new Set(group.map((r) => r.raw_sha256))];
  const texts = [...new Set(group.map((r) => r.raw_text))];
  const setKeys = [...new Set(group.flatMap((r) => Object.keys(r.frame_sets)))].sort();
  const setMismatches = [];
  for (const k of setKeys) {
    const seen = group.map((r) => (r.frame_sets[k] ? r.frame_sets[k].count + '/' + r.frame_sets[k].digest : 'absent'));
    if (new Set(seen).size !== 1) setMismatches.push({ set: k, seen });
  }
  return {
    case: caseNo,
    repeats: group.length,
    stages: group.map((r) => r.stage),
    all_accepted: group.every((r) => r.accepted && r.tool_returncode === 0),
    all_pass_marker: group.every((r) => r.pass_marker && !r.fail_marker),
    distinct_raw_hashes: hashes.length,
    identical_bytes: hashes.length === 1 && texts.length === 1,
    raw_sha256: hashes,
    frame_sets: group[0].frame_sets,
    frame_sets_identical: setMismatches.length === 0,
    frame_set_mismatches: setMismatches,
  };
});

const out = {
  task: 'GTOS-DSH-141',
  what: 'cold repeats of the two recovery cases, each run a fresh guarded process, comparing both the bytes each process wrote '
    + 'and the frame sets those bytes publish',
  unit_tag: TAG,
  why_separate_processes: 'the allocator, the paging object and the slot table are file-scope storage in the unit, so a '
    + 'second case in one process would inherit state; every repeat here is its own admitted stage',
  cases: CASES,
  repeats: REPEATS,
  runs: runs.map((r) => ({ case: r.case, stage: r.stage, tool_returncode: r.tool_returncode, accepted: r.accepted,
    raw_linux: r.raw_linux, raw_bytes: r.raw_bytes, raw_sha256: r.raw_sha256, frame_sets: r.frame_sets })),
  verdicts,
  ok: verdicts.every((v) => v.all_accepted && v.all_pass_marker && v.identical_bytes && v.frame_sets_identical),
};
const target = R + '/cold-repeats-141.json';
if (fs.existsSync(target)) throw new Error('refused: exists ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ wrote: target, ok: out.ok,
  verdicts: verdicts.map((v) => ({ case: v.case, identical_bytes: v.identical_bytes,
    frame_sets_identical: v.frame_sets_identical, raw_sha256: v.raw_sha256 })) }, null, 2));
process.exit(out.ok ? 0 : 1);