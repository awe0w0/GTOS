// Run every case of the runtime host unit, one case per process, each as its own guarded stage.
//
// One case per process is not a convenience: a successful Activate publishes a process-wide owner with no destroy path, so
// two activations cannot share a process. The unit refuses a second one, which is what the selector exists to make possible.
//
// The verdict printed here is read from the guard's raw channel, where the unit wrote its own output. A case passes when the
// unit printed no FAIL and the process exit status was zero; the two are checked separately because a unit that prints FAIL
// and still exits zero would otherwise look healthy.

const fs = require('fs');
const { spawnSync } = require('child_process');

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const DRIVE = R + '/stage-output/drive-141.mjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const [tag, firstStage, count, outName] = process.argv.slice(2);
const n = Number(count || 11);
const startNum = Number(String(firstStage).replace(/^.*-/, ''));
const prefix = String(firstStage).replace(/-[0-9]+$/, '');
const sleep = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };
const readWithRetry = (p) => {
  for (let i = 0; i < 60; i++) { try { return fs.readFileSync(p, 'utf8'); } catch { sleep(250); } }
  return null;
};

const rows = [];
for (let i = 0; i < n; i++) {
  const stage = prefix + '-' + String(startNum + i).padStart(2, '0');
  const caseSelector = i + 1;
  const run = spawnSync(NODE, [DRIVE, 'runcase', stage, tag, String(caseSelector)],
    { windowsHide: true, timeout: 900000, maxBuffer: 32 * 1024 * 1024 });
  let evidence = null;
  try { evidence = JSON.parse(fs.readFileSync(R + '/evidence/' + stage + '.json', 'utf8')); } catch { }
  const rawText = evidence && evidence.raw && evidence.raw.path ? readWithRetry(evidence.raw.path) : null;
  const fails = rawText ? rawText.split(/\r?\n/).filter((l) => /^FAIL /.test(l)) : [];
  const unitFail = rawText ? /RUNTIME UNIT FAIL/.test(rawText) : false;
  const unitPass = rawText ? /RUNTIME UNIT PASS/.test(rawText) : false;
  const rc = evidence && evidence.verdict ? evidence.verdict.tool_returncode : null;
  rows.push({
    case: caseSelector, stage,
    tool_returncode: rc,
    exit_status_matches_selector: rc === 0,
    fail_lines: fails,
    unit_pass_marker: unitPass, unit_fail_marker: unitFail,
    raw_path: evidence && evidence.raw ? evidence.raw.path : null,
    raw_bytes: evidence && evidence.raw ? evidence.raw.bytes : null,
    raw_sha256: evidence && evidence.raw ? evidence.raw.sha256 : null,
    truly_passed: rc === 0 && fails.length === 0 && unitPass && !unitFail,
    stage_ok: evidence ? evidence.ok === true : false,
    raw_text: rawText,
  });
  console.log('case ' + caseSelector + ' (' + stage + '): rc=' + rc + ' fails=' + fails.length
    + ' pass=' + unitPass + ' fail=' + unitFail);
}

const out = {
  task: 'GTOS-DSH-141',
  what: 'every case of the runtime host unit, one guarded process each, with the verdict read from the unit own raw channel',
  tag, cases: n,
  passed: rows.filter((r) => r.truly_passed).length,
  failed: rows.filter((r) => !r.truly_passed).map((r) => r.case),
  rows,
};
const target = R + '/' + (outName || 'runtime-host-unit-141.json');
if (fs.existsSync(target)) throw new Error('refused: exists ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ wrote: target, passed: out.passed, failed: out.failed }, null, 2));