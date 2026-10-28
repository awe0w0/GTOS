// GTOS-DSH-141 mandatory host-gate consumer.
//
// The 140 host gate listed boot_diagnostic and safe_tss and left the runtime unit out, so the one unit that was failing was the
// one the gate did not require. The requirement here is a set, not a list the caller supplies: it holds the three units by
// role, no argument removes one, and the verdict is recomputed from the run itself rather than from the summary a runner
// wrote about it. For every case of the runtime unit the consumer re-reads the case's evidence record, re-reads the raw
// channel from disk and hashes it, and compares all three against the row that the runner published. A row that says zero
// while the evidence says otherwise, or a recorded identity that does not match the bytes on disk, is a refusal.
//
// The mutations at the end are the point of the file. A gate that has never refused anything is not known to be able to
// refuse; each mutation changes exactly one condition in a copy of the collected set and the same judge must reject it.

const fs = require('fs');
const crypto = require('crypto');

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const UNIT_FILE = R + '/runtime-host-unit-141d.json';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const sleep = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };
function readWithRetry(p, encoding) {
  for (let i = 0; i < 60; i++) { try { return fs.readFileSync(p, encoding); } catch { sleep(250); } }
  return null;
}
// The stage records speak Linux paths; the host reaches them either through the drive letter of a mount or through the
// distribution's own share. Both are the same bytes.
const toHost = (l) => (/^\/mnt\/[a-z]\//.test(String(l))
  ? String(l).replace(/^\/mnt\/([a-z])\//, (m, d) => d.toUpperCase() + ':/')
  : '\\\\wsl$\\Ubuntu-20.04' + String(l));

function stageEvidence(stage) {
  const p = R + '/evidence/' + stage + '.json';
  if (!fs.existsSync(p)) return null;
  const d = JSON.parse(fs.readFileSync(p, 'utf8'));
  const rawBytes = d.raw && d.raw.path ? readWithRetry(d.raw.path, null) : null;
  const inner = d.inner_report && d.inner_report.path ? readWithRetry(d.inner_report.path, 'utf8') : null;
  return {
    stage,
    argv: d.subject && d.subject.argv ? d.subject.argv : null,
    elf_linux: d.subject && d.subject.argv ? d.subject.argv[0] : null,
    tool_returncode: d.verdict ? d.verdict.tool_returncode : null,
    accepted: d.verdict ? d.verdict.accepted === true : false,
    guard_row_present: d.guard_row !== null && d.guard_row !== undefined,
    inner_present: d.inner_report ? d.inner_report.present === true : false,
    inner_recorded_sha256: d.inner_report ? d.inner_report.sha256 : null,
    inner_bytes: d.inner_report ? d.inner_report.bytes : null,
    inner_text: inner,
    inner_readable: inner !== null,
    raw_recorded_sha256: d.raw ? d.raw.sha256 : null,
    raw_recorded_bytes: d.raw ? d.raw.bytes : null,
    raw_disk_sha256: rawBytes ? sha(rawBytes) : null,
    raw_disk_bytes: rawBytes ? rawBytes.length : null,
    raw_text: rawBytes ? rawBytes.toString('utf8') : null,
    raw_readable: rawBytes !== null,
  };
}

// ---------------------------------------------------------------- the requirement set
const REQUIRED = Object.freeze([
  { role: 'runtime', what: 'the NativeRuntime host unit, every case' },
  { role: 'boot_diagnostic', what: 'the BootDiagnostic host unit' },
  { role: 'safe_tss', what: 'the safe-TSS observation host unit' },
]);
const RUNTIME_CASES = 11;

function collect() {
  if (!fs.existsSync(UNIT_FILE)) throw new Error('the runtime unit record is absent');
  const runtime = JSON.parse(fs.readFileSync(UNIT_FILE, 'utf8'));
  const cases = runtime.rows.map((r) => {
    const ev = stageEvidence(r.stage);
    return {
      case: r.case, stage: r.stage,
      // what the runner published
      row_tool_returncode: r.tool_returncode,
      row_unit_pass_marker: r.unit_pass_marker,
      row_unit_fail_marker: r.unit_fail_marker,
      row_fail_lines: r.fail_lines,
      row_raw_sha256: r.raw_sha256,
      row_raw_bytes: r.raw_bytes,
      // what the case's own evidence says, re-read and re-hashed here
      evidence_present: ev !== null,
      evidence_tool_returncode: ev ? ev.tool_returncode : null,
      evidence_accepted: ev ? ev.accepted : false,
      evidence_argv: ev ? ev.argv : null,
      elf_linux: ev ? ev.elf_linux : null,
      raw_recorded_sha256: ev ? ev.raw_recorded_sha256 : null,
      raw_disk_sha256: ev ? ev.raw_disk_sha256 : null,
      raw_disk_bytes: ev ? ev.raw_disk_bytes : null,
      raw_disk_text_has_pass: ev && ev.raw_text ? /RUNTIME UNIT PASS/.test(ev.raw_text) : false,
      raw_disk_text_has_fail: ev && ev.raw_text ? /RUNTIME UNIT FAIL/.test(ev.raw_text) : false,
      raw_disk_fail_lines: ev && ev.raw_text
        ? ev.raw_text.split(/\r?\n/).filter((l) => /^FAIL /.test(l)).length : null,
    };
  });
  return {
    task: 'GTOS-DSH-141',
    units: {
      runtime: {
        role: 'runtime',
        record_path: UNIT_FILE,
        record_sha256: sha(fs.readFileSync(UNIT_FILE)),
        record_bytes: fs.statSync(UNIT_FILE).size,
        recorded_passed: runtime.passed,
        cases,
      },
      boot_diagnostic: { role: 'boot_diagnostic', ...(stageEvidence('bdrun141-01') || { absent: true }) },
      safe_tss: { role: 'safe_tss', ...(stageEvidence('saferun141-02') || { absent: true }) },
    },
  };
}

// ---------------------------------------------------------------- the judge
function judge(set) {
  const problems = [];
  const seen = [];
  for (const req of REQUIRED) {
    const unit = set.units[req.role];
    if (!unit || unit.absent) { problems.push('required unit absent: ' + req.role + ' (' + req.what + ')'); continue; }
    seen.push(req.role);
    if (req.role === 'runtime') {
      const cases = unit.cases || [];
      if (cases.length !== RUNTIME_CASES) {
        problems.push('the runtime unit lists ' + cases.length + ' cases, the requirement is ' + RUNTIME_CASES);
      }
      if (unit.recorded_passed !== RUNTIME_CASES) {
        problems.push('the runtime unit record claims ' + unit.recorded_passed + ' passes');
      }
      if (!/^[0-9a-f]{64}$/.test(String(unit.record_sha256))) problems.push('the runtime unit record has no identity');
      let elf = null;
      for (let n = 1; n <= RUNTIME_CASES; n++) {
        const c = cases.find((x) => x.case === n);
        if (!c) { problems.push('the runtime unit is missing case ' + n); continue; }
        if (!c.evidence_present) { problems.push('case ' + n + ' has no evidence record'); continue; }
        // the row and the run must agree, in both directions
        if (c.row_tool_returncode !== c.evidence_tool_returncode) {
          problems.push('case ' + n + ': the row says exit ' + c.row_tool_returncode + ', the evidence says '
            + c.evidence_tool_returncode);
        }
        if (c.row_tool_returncode !== 0) problems.push('case ' + n + ' exited ' + c.row_tool_returncode);
        if (!c.evidence_accepted) problems.push('case ' + n + ' was not an accepted guarded stage');
        // the recorded channel identity must be the bytes on disk, and on disk is where the verdict is read
        if (c.row_raw_sha256 !== c.raw_disk_sha256) {
          problems.push('case ' + n + ': the row identity is not the raw channel on disk');
        }
        if (c.row_raw_bytes !== c.raw_disk_bytes) problems.push('case ' + n + ': the row byte count is not the one on disk');
        if (c.raw_recorded_sha256 !== c.raw_disk_sha256) {
          problems.push('case ' + n + ': the evidence identity is not the raw channel on disk');
        }
        if (!c.raw_disk_text_has_pass) problems.push('case ' + n + ': the raw channel carries no pass marker');
        if (c.raw_disk_text_has_fail) problems.push('case ' + n + ': the raw channel carries the fail marker');
        if (c.raw_disk_fail_lines) problems.push('case ' + n + ': the raw channel carries ' + c.raw_disk_fail_lines + ' FAIL lines');
        if (c.row_fail_lines && c.row_fail_lines.length) problems.push('case ' + n + ': the row lists FAIL lines');
        if (!c.row_unit_pass_marker) problems.push('case ' + n + ': the row has no pass marker');
        if (c.row_unit_fail_marker) problems.push('case ' + n + ': the row has the fail marker');
        // one unit binary for every case: a case cannot have run a different image
        const argv = Array.isArray(c.evidence_argv) ? c.evidence_argv : [];
        if (!argv.length) problems.push('case ' + n + ': the evidence names no argv');
        else if (elf === null) elf = argv[0];
        else if (argv[0] !== elf) problems.push('case ' + n + ' ran ' + argv[0] + ' instead of ' + elf);
        if (argv[1] !== String(n)) problems.push('case ' + n + ' was selected with ' + argv[1]);
      }
      if (elf !== null) {
        const p = toHost(elf);
        if (!fs.existsSync(p)) problems.push('the unit binary the cases ran is not on disk: ' + p);
        else set.units.runtime.elf = { linux: elf, host: p, bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)) };
      }
    } else {
      if (unit.tool_returncode !== 0) problems.push(req.role + ' exited ' + unit.tool_returncode);
      if (!unit.accepted) problems.push(req.role + ' was not an accepted guarded stage');
      if (!unit.guard_row_present) problems.push(req.role + ' has no guard row');
      if (!unit.raw_readable) problems.push(req.role + ' has no readable raw channel');
      else {
        if (unit.raw_recorded_sha256 !== unit.raw_disk_sha256) {
          problems.push(req.role + ': the recorded identity is not the raw channel on disk');
        }
        if (!/PASS/.test(unit.raw_text)) problems.push(req.role + ' raw channel contains no PASS marker');
        if (/FAIL/.test(unit.raw_text)) problems.push(req.role + ' raw channel contains FAIL');
      }
      if (!unit.inner_readable) problems.push(req.role + ' has no readable inner report');
      else if (!/^[0-9a-f]{64}$/.test(String(unit.inner_recorded_sha256))) {
        problems.push(req.role + ' inner report has no identity');
      }
    }
  }
  return { ok: problems.length === 0, problems, units_seen: seen, required: REQUIRED.map((r) => r.role) };
}

const clone = (v) => JSON.parse(JSON.stringify(v));

// ---------------------------------------------------------------- one condition each
const MUTATIONS = [
  { name: 'drop-runtime-unit', why: 'the unit the gate exists for is left out',
    apply: (s) => { s.units.runtime = { absent: true }; } },
  { name: 'drop-runtime-case-10', why: 'a case is quietly removed instead of repaired',
    apply: (s) => { s.units.runtime.cases = s.units.runtime.cases.filter((c) => c.case !== 10); } },
  { name: 'row-exit-rewritten-to-zero', why: 'the row claims success the run did not report',
    apply: (s) => { const c = s.units.runtime.cases.find((x) => x.case === 10); c.row_tool_returncode = 0; c.evidence_tool_returncode = 81; } },
  { name: 'raw-identity-fabricated', why: 'a made-up hash is offered as the channel',
    apply: (s) => { const c = s.units.runtime.cases.find((x) => x.case === 5); c.row_raw_sha256 = 'f'.repeat(64); } },
  { name: 'raw-bytes-shrunk', why: 'an empty channel is counted as a silent pass',
    apply: (s) => { const c = s.units.runtime.cases.find((x) => x.case === 5); c.row_raw_bytes = 0; } },
  { name: 'pass-marker-fabricated', why: 'the verdict is read from a summary instead of the channel',
    apply: (s) => { const c = s.units.runtime.cases.find((x) => x.case === 5); c.row_unit_pass_marker = true; c.raw_disk_text_has_pass = false; } },
  { name: 'swap-runtime-elf', why: 'a case ran a different image from its siblings',
    apply: (s) => { const c = s.units.runtime.cases.find((x) => x.case === 5); c.evidence_argv = ['/mnt/f/elsewhere/runtime_test.elf', '5']; } },
  { name: 'wrong-selector', why: 'the case number in the record is not the one the unit was given',
    apply: (s) => { const c = s.units.runtime.cases.find((x) => x.case === 5); c.evidence_argv = [c.evidence_argv[0], '6']; } },
  { name: 'drop-boot-diagnostic', why: 'a required unit disappears and the summary still says all passed',
    apply: (s) => { s.units.boot_diagnostic = { absent: true }; } },
  { name: 'raw-says-pass-exit-says-fail', why: 'the marker is left in place beside a non-zero exit status',
    apply: (s) => { s.units.safe_tss.raw_text = 'SAFE TSS UNIT PASS\n'; s.units.safe_tss.tool_returncode = 1; } },
  { name: 'safe-tss-raw-removed', why: 'the channel the verdict is read from is emptied',
    apply: (s) => { s.units.safe_tss.raw_readable = false; s.units.safe_tss.raw_text = null; } },
  { name: 'safe-tss-inner-removed', why: 'the tool return code has no source left',
    apply: (s) => { s.units.safe_tss.inner_readable = false; } },
  { name: 'not-an-accepted-stage', why: 'a stage the bridge did not accept is counted anyway',
    apply: (s) => { s.units.boot_diagnostic.accepted = false; } },
  { name: 'boot-diagnostic-no-marker', why: 'a silent run is counted as a pass',
    apply: (s) => { s.units.boot_diagnostic.raw_text = 'BOOTDIAG HOST UNIT\n'; } },
];

const base = collect();
const baseVerdict = judge(base);
const mutations = MUTATIONS.map((m) => {
  const copy = clone(base);
  m.apply(copy);
  const v = judge(copy);
  return { name: m.name, why: m.why, refused: v.ok === false,
    problems: v.problems.length > 6 ? v.problems.slice(0, 6).concat(['(' + (v.problems.length - 6) + ' more)']) : v.problems };
});

const out = {
  task: 'GTOS-DSH-141',
  what: 'the mandatory related host tests, judged from each unit own evidence and raw channel rather than from the summary '
    + 'a runner wrote, plus fourteen single-condition mutations of the collected set that the same judge must refuse',
  requirement: {
    set: REQUIRED.map((r) => ({ role: r.role, what: r.what })),
    runtime_cases: RUNTIME_CASES,
    note: 'the set is independent of the caller: no argument removes a unit, and the runtime unit is required rather than '
      + 'optional, which is the 140 defect this file exists to correct',
  },
  sources: {
    runtime_unit: { path: UNIT_FILE, bytes: base.units.runtime.record_bytes, sha256: base.units.runtime.record_sha256 },
    runtime_cases: base.units.runtime.cases.map((c) => ({ case: c.case, stage: c.stage })),
    boot_diagnostic_stage: 'bdrun141-01',
    safe_tss_stage: 'saferun141-02',
  },
  base: {
    ok: baseVerdict.ok, problems: baseVerdict.problems, units_seen: baseVerdict.units_seen,
    runtime_cases: base.units.runtime.cases.length,
    runtime_elf: base.units.runtime.elf || null,
    boot_diagnostic: { tool_returncode: base.units.boot_diagnostic.tool_returncode,
      raw_sha256: base.units.boot_diagnostic.raw_disk_sha256,
      inner_sha256: base.units.boot_diagnostic.inner_recorded_sha256,
      accepted: base.units.boot_diagnostic.accepted },
    safe_tss: { tool_returncode: base.units.safe_tss.tool_returncode,
      raw_sha256: base.units.safe_tss.raw_disk_sha256,
      inner_sha256: base.units.safe_tss.inner_recorded_sha256,
      accepted: base.units.safe_tss.accepted },
  },
  mutations,
  mutations_refused: mutations.filter((m) => m.refused).length,
  mutations_total: mutations.length,
  ok: baseVerdict.ok && mutations.length > 0 && mutations.every((m) => m.refused),
};
const target = R + '/related-host-tests-141d.json';
if (fs.existsSync(target)) throw new Error('refused: exists ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ wrote: target, base_ok: out.base.ok, base_problems: out.base.problems,
  runtime_elf: out.base.runtime_elf, mutations_refused: out.mutations_refused, mutations_total: out.mutations_total,
  ok: out.ok, not_refused: mutations.filter((m) => !m.refused).map((m) => m.name) }, null, 2));
process.exit(out.ok ? 0 : 1);