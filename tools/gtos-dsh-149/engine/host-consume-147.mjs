// GTOS-DSH-147 H: the production host consumer of the host-test chain.
//
// This module decides whether the host chain this packet produced actually holds together, and it decides it from the artefacts.
// The previous version of this file read the return code out of the guard's result record and the argv out of the bridge's
// summary of the inner report. Those are records about a run, and a record can say anything. The executor writes an
// inner-report-<stage>.json of its own for every launch; that file is the run's own account, and this consumer opens it by the
// path the record gives, re-hashes it, and then reads the child's return code, the child's actual argv, the timeout flag and the
// raw channel's location and hash out of it. A record whose summary and whose inner report disagree is refused, because the two
// cannot both be true.
//
// The same rule applies to the build: the source files, the linker script, the profile and the seam are re-read from disk and
// re-hashed, so "built from the current tree" is a fact about bytes rather than a sentence in a description field.
//
// And the required set is the consumer's own. The caller names which stage ran each case, and that is all it names. How many
// cases the runtime unit has, and that boot_diagnostic and safe_tss are required at all, are constants here: a caller cannot make
// the chain pass by declaring a smaller requirement, and shrinking both its expectation and its case list changes nothing.
//
// The one thing this module never does is start a process. It counts its own attempts and reports the count; the harness compares
// that with the guard's own run counter taken across the call. A refusal that launched something to find out would not be a
// refusal of the chain; it would be another run.

import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import path from 'node:path';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// The one place a spawn could happen. It is never called.
let spawnAttempts = 0;
export const spawnAttemptCount = () => spawnAttempts;

// The required set, owned here rather than by the caller.
// Each unit says how it is launched and what it prints for itself, because the three are not the same shape: the runtime unit is
// launched once per case with a selector and prints CASE <n>, the other two are launched once with no selector and print their
// own markers. A single shape assumed for all three would have made two of them uncheckable.
export const REQUIRED_UNITS = [
  { role: 'runtime', cases: 12, selector: true, case_marker: true, marker: 'RUNTIME UNIT PASS',
    what: 'the NativeRuntime host unit, every case, including the slot-observation case this packet added' },
  { role: 'boot_diagnostic', cases: 1, selector: false, case_marker: false, marker: 'BOOTDIAG HOST UNIT PASS',
    what: 'the BootDiagnostic host unit' },
  { role: 'safe_tss', cases: 1, selector: false, case_marker: false, marker: 'SAFE TSS UNIT PASS',
    what: 'the safe-TSS observation host unit' },
];

// The records name Linux paths, because the tools that wrote them ran inside the distribution. This consumer runs on the Windows
// host, so every path is translated before it is opened. The first attempt at this read the Linux form directly and reported every
// raw channel as absent, which reads like a missing artefact rather than like a translation nobody did.
// A relocation map says: this record names the original path, the bytes are at this other path. The keys and the values are both
// in whatever form the record uses, and the comparison is made on the translated form so that a Linux path and its UNC spelling
// are the same key. A map whose value equals its key is not a relocation, and is refused rather than quietly accepted.
export function normalizePath(p) {
  const h = hostPath(p);
  return typeof h === 'string' ? h.replace(/\//g, '\\').toLowerCase() : h;
}
export function aliasResolver(map) {
  const out = new Map();
  for (const [from, to] of Object.entries(map || {})) {
    if (typeof from !== 'string' || typeof to !== 'string') continue;
    out.set(normalizePath(from), to);
  }
  const stats = { supplied: out.size, used: 0, unused: 0, keys: [...out.keys()] };
  const resolve = (p) => {
    if (typeof p !== 'string' || !p) return p;
    const hit = out.get(normalizePath(p));
    if (hit === undefined) return p;
    stats.used += 1;
    return hit;
  };
  resolve.stats = stats;
  return resolve;
}

export function hostPath(p) {
  if (typeof p !== 'string' || !p) return p;
  if (/^[\\/]{2}[^\\/]/.test(p)) return p.replace(/\//g, '\\');
  const mount = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (mount) return mount[1].toUpperCase() + ':/' + mount[2];
  if (p.startsWith('/')) return '\\\\wsl$\\Ubuntu-20.04' + p.replace(/\//g, '\\');
  return p;
}

// A read that survives the transient UNC failures this environment produces without ever turning an absence into a success.
function sleep(ms) { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); }
function readBytes(p) {
  if (typeof p !== 'string' || !p) return null;
  p = hostPath(p);
  for (let i = 0; i < 8; i++) {
    try { return fs.readFileSync(p); } catch { sleep(120); }
  }
  return null;
}
function identity(p, problems, what) {
  if (typeof p !== 'string' || !p) { problems.push({ why: 'no path for ' + what, member: what }); return null; }
  let stat;
  try { stat = fs.lstatSync(hostPath(p)); } catch (e) { problems.push({ why: 'absent ' + what, path: p, member: what, detail: String(e.code || e.message) }); return null; }
  if (!stat.isFile()) { problems.push({ why: what + ' is not an ordinary file', path: p, member: what }); return null; }
  const bytes = readBytes(p);
  if (bytes === null) { problems.push({ why: 'unreadable ' + what, path: p, member: what }); return null; }
  return { path: p, bytes: bytes.length, sha256: sha(bytes) };
}
function agree(observed, expected, problems, what) {
  if (!observed || !expected) return false;
  if (typeof expected.bytes !== 'number' || typeof expected.sha256 !== 'string') {
    problems.push({ why: what + ' has no expected identity to compare with', member: what });
    return false;
  }
  if (observed.bytes !== expected.bytes) {
    problems.push({ why: what + ' is a different size', member: what, expected: expected.bytes, observed: observed.bytes });
    return false;
  }
  if (observed.sha256 !== expected.sha256) {
    problems.push({ why: what + ' has different bytes at the same size', member: what, expected: expected.sha256,
      observed: observed.sha256 });
    return false;
  }
  return true;
}
function readJson(p, problems, what) {
  const b = readBytes(p);
  if (b === null) { problems.push({ why: 'unreadable ' + what, path: p, member: what }); return null; }
  try { return JSON.parse(b.toString('utf8')); }
  catch (e) { problems.push({ why: what + ' is not JSON', path: p, member: what, detail: String(e.message) }); return null; }
}

// ---------------------------------------------------------------- the entry
export function consumeHostChain(control) {
  const problems = [];
  const spawnBefore = spawnAttempts;
  const c = control || {};
  // The relocation map, when the caller supplies one. With no map this is the identity function and nothing changes.
  const resolve = aliasResolver(c.path_aliases);
  if (c.path_aliases && Object.keys(c.path_aliases).length > 0) {
    for (const [from, to] of Object.entries(c.path_aliases)) {
      if (normalizePath(from) === normalizePath(to)) {
        problems.push({ why: 'the relocation map does not move this path, so it is not a relocation', path: from });
      }
    }
  }

  const build = readJson(c.build_record_path, problems, 'build record');
  const contract = readJson(c.contract_path, problems, 'contract');
  const observed = { build_inputs: [], units: [], cases: [] };
  const seen = { build_inputs: 0, units: 0, cases: 0, inner_reports: 0, raw_channels: 0, return_codes: 0, argvs: 0,
    exit_layers: 0 };

  // 1. The build inputs: every file the unit was compiled from, re-read and re-hashed. The build record names them because the
  // build stage is what compiled them; this consumer opens each one and compares.
  if (build) {
    const inputs = Array.isArray(build.inputs) ? build.inputs : [];
    if (inputs.length === 0) problems.push({ why: 'the build record names no inputs, so nothing built this ELF that can be checked' });
    for (const input of inputs) {
      seen.build_inputs += 1;
      const got = identity(resolve(input.host_path), problems, 'build input ' + (input.logical || input.host_path));
      if (!got) continue;
      const ok = agree(got, { bytes: input.bytes, sha256: input.sha256 }, problems, 'build input ' + input.logical);
      observed.build_inputs.push({ logical: input.logical, role: input.role, bytes: got.bytes, sha256: got.sha256,
        matches_build: ok });
    }
    // The ELF itself, against the build record.
    const elf = identity(c.unit_elf_host_path, problems, 'unit ELF');
    if (elf) {
      agree(elf, build.elf, problems, 'unit ELF');
      observed.unit_elf = { bytes: elf.bytes, sha256: elf.sha256 };
    }
  }

  // 2. The required units, from the consumer's own list. A unit the contract does not describe is a refusal, not a skip.
  const described = (contract && Array.isArray(contract.units)) ? contract.units : [];
  if (!contract) {
    problems.push({ why: 'no contract, so no unit is described' });
  } else {
    const byRole = new Map();
    for (const unit of described) {
      if (!unit || typeof unit.role !== 'string') { problems.push({ why: 'a described unit names no role' }); continue; }
      if (byRole.has(unit.role)) problems.push({ why: 'the contract describes a unit twice', role: unit.role });
      byRole.set(unit.role, unit);
    }
    for (const required of REQUIRED_UNITS) {
      const unit = byRole.get(required.role);
      if (!unit) {
        problems.push({ why: 'the required unit is absent from the contract', role: required.role, what: required.what });
        continue;
      }
      seen.units += 1;
      const cases = Array.isArray(unit.cases) ? unit.cases : [];
      // The count is the consumer's, not the contract's. A contract that says it expects fewer cases than the unit has is
      // refused, and a contract that lists fewer cases than it expects is refused too.
      if (cases.length !== required.cases) {
        problems.push({ why: 'the unit does not carry every case', role: required.role, required: required.cases,
          listed: cases.length });
      }
      const numbers = cases.map((x) => x && x.case);
      const unique = new Set(numbers);
      if (unique.size !== numbers.length) problems.push({ why: 'a case appears twice', role: required.role });
      for (let n = 1; n <= required.cases; n++) {
        if (!unique.has(n)) problems.push({ why: 'the unit is missing a case', role: required.role, case: n });
      }
      const unitRow = { role: required.role, required_cases: required.cases, listed_cases: cases.length,
        cases: [], elf: null };
      observed.units.push(unitRow);

      // The unit's own ELF, if the contract names one; a unit that names no ELF is a claim about a binary nobody can find.
      if (unit.elf && unit.elf.path) {
        const got = identity(resolve(unit.elf.path), problems, 'ELF for ' + required.role);
        if (got) {
          const ok = agree(got, unit.elf, problems, 'ELF for ' + required.role);
          unitRow.elf = { bytes: got.bytes, sha256: got.sha256, matches: ok };
        }
      } else {
        problems.push({ why: 'the unit names no ELF', role: required.role });
      }

      for (const want of cases) {
        if (!want || typeof want.case !== 'number') { problems.push({ why: 'a case is not numbered', role: required.role }); continue; }
        seen.cases += 1;
        const row = { role: required.role, case: want.case, stage: want.stage };
        const result = readJson((c.results_dir || '') + '/' + want.stage + '.json', problems,
          'result for ' + required.role + ' case ' + want.case);
        if (!result) { unitRow.cases.push({ ...row, verdict: 'no result record' }); continue; }

        // The three exit layers. A run has three places it can stop: the transport that carried the request, the guard that
        // confined it, and the child itself. All three have to say zero, and the child's own report has to agree with both.
        const transportExit = result.transport && typeof result.transport.exit_code === 'number' ? result.transport.exit_code : null;
        const guardExit = result.guard_row && typeof result.guard_row.exit_code === 'number' ? result.guard_row.exit_code : null;
        row.transport_exit = transportExit;
        row.guard_exit = guardExit;
        if (transportExit === null) problems.push({ why: 'no transport exit recorded', case: want.case });
        if (guardExit === null) problems.push({ why: 'no guard exit recorded', case: want.case });
        if (result.transport && result.transport.timed_out === true) problems.push({ why: 'the transport timed out', case: want.case });
        if (result.guard_row && result.guard_row.refused === true) problems.push({ why: 'the guard refused the launch', case: want.case });

        // The inner report: the executor's own account of the run. It is opened by the path the record gives, re-hashed, and
        // then read. Nothing is taken from the record's summary of it.
        const recorded = result.inner_report;
        if (!recorded || !recorded.path) {
          problems.push({ why: 'the result names no inner report, so the child was never asked what happened', case: want.case });
          unitRow.cases.push({ ...row, verdict: 'no inner report' });
          continue;
        }
        const innerId = identity(resolve(recorded.path), problems, 'inner report for case ' + want.case);
        if (!innerId) { unitRow.cases.push({ ...row, verdict: 'inner report absent' }); continue; }
        seen.inner_reports += 1;
        const innerOk = agree(innerId, { bytes: recorded.bytes, sha256: recorded.sha256 }, problems,
          'inner report for case ' + want.case);
        row.inner_bytes = innerId.bytes;
        row.inner_sha256 = innerId.sha256;
        row.inner_matches_record = innerOk;
        const inner = readJson(resolve(recorded.path), problems, 'inner report for case ' + want.case);
        if (!inner) { unitRow.cases.push({ ...row, verdict: 'inner report unreadable' }); continue; }

        // What the inner report says about itself.
        if (inner.stage !== want.stage) {
          problems.push({ why: 'the inner report is from a different stage', case: want.case, expected: want.stage,
            observed: inner.stage });
        }
        const child = inner.child || {};
        const childRc = typeof child.returncode === 'number' ? child.returncode : null;
        row.child_return_code = childRc;
        if (childRc === null) problems.push({ why: 'the inner report carries no child return code', case: want.case });
        else {
          seen.return_codes += 1;
          if (childRc !== 0) problems.push({ why: 'the child did not exit zero', case: want.case, observed: childRc });
        }
        if (child.timed_out === true) problems.push({ why: 'the child timed out', case: want.case });
        // The three layers have to agree with each other, not merely each be zero.
        const layers = [transportExit, guardExit, childRc];
        if (layers.every((v) => typeof v === 'number')) {
          seen.exit_layers += 1;
          if (new Set(layers).size !== 1) {
            problems.push({ why: 'the three exit layers disagree', case: want.case,
              transport: transportExit, guard: guardExit, child: childRc });
          }
        }

        // The argv the child was actually launched with, out of the inner report rather than out of a summary.
        const argv = Array.isArray(inner.argv) ? inner.argv : null;
        row.argv = argv;
        if (!argv) problems.push({ why: 'the inner report carries no argv', case: want.case });
        else {
          seen.argvs += 1;
          // The ELF in argv is the unit own ELF, named by the contract, not a single ELF assumed for the whole set.
          const unitElfLinux = unit.elf_linux || c.unit_elf_linux;
          if (typeof unitElfLinux !== 'string' || !unitElfLinux) {
            problems.push({ why: 'the unit names no ELF path for the child argv', role: required.role });
          }
          const wantArgv = required.selector ? [unitElfLinux, String(want.case)] : [unitElfLinux];
          const same = argv.length === wantArgv.length && wantArgv.every((token, i) => argv[i] === token);
          // With a relocation map the recorded argv names the original path; the resolution of that name is what has to match
          // the resolved unit ELF, and both the recorded and the resolved forms are kept in the row.
          const resolvedArgv = argv.map((t) => resolve(t));
          const resolvedWant = wantArgv.map((t) => resolve(t));
          const sameResolved = resolvedArgv.length === resolvedWant.length
            && resolvedWant.every((token, i) => resolvedArgv[i] === token);
          row.argv = argv;
          row.argv_resolved = resolvedArgv;
          if (!same && !sameResolved) problems.push({ why: 'the child was launched with a different argv', case: want.case,
            expected: wantArgv, observed: argv, resolved_expected: resolvedWant, resolved_observed: resolvedArgv });
          if (same !== sameResolved) {
            problems.push({ why: 'the argv agrees with one resolution of the paths and not the other', case: want.case,
              recorded_matches: same, resolved_matches: sameResolved });
          }
        }

        // The raw channel, at the path the inner report names, with the hash and size the inner report records.
        const rawPath = child.output_path;
        if (!rawPath) {
          problems.push({ why: 'the inner report names no raw channel', case: want.case });
        } else {
          const rawId = identity(resolve(rawPath), problems, 'raw channel for case ' + want.case);
          if (!rawId) { unitRow.cases.push({ ...row, verdict: 'raw absent' }); continue; }
          seen.raw_channels += 1;
          const rawOk = agree(rawId, { bytes: child.output_bytes, sha256: child.output_sha256 }, problems,
            'raw channel for case ' + want.case);
          row.raw_bytes = rawId.bytes;
          row.raw_sha256 = rawId.sha256;
          row.raw_matches_inner = rawOk;
          const text = readBytes(resolve(rawPath)).toString('utf8');
          row.carries_case_marker = required.case_marker
            ? new RegExp('CASE ' + want.case + '\\b').test(text) : null;
          if (required.case_marker && !row.carries_case_marker) {
            problems.push({ why: 'the raw channel is not from this case', case: want.case });
          }
          // The pass marker each unit prints for itself.
          const marker = required.marker;
          row.carries_pass_marker = text.includes(marker);
          if (!row.carries_pass_marker) problems.push({ why: 'the raw channel carries no pass marker', case: want.case,
            marker });
          // A failing unit says so, and a channel that says so is not a pass however the exit code reads.
          row.carries_fail_marker = /UNIT FAIL/.test(text);
          if (row.carries_fail_marker) problems.push({ why: 'the raw channel reports a failure', case: want.case });
        }
        unitRow.cases.push({ ...row, verdict: 'read' });
      }
    }
  }

  const spawnAfter = spawnAttempts;
  const refused = problems.length > 0;
  const aliasStats = resolve.stats || { supplied: 0, used: 0, unused: 0, keys: [] };
  const aliasReport = {
    supplied: aliasStats.supplied,
    used: aliasStats.used,
    keys: aliasStats.keys.length,
    // A map that was supplied and never used would mean the consumer read the originals, which is the thing this must not do.
    every_supplied_alias_used: aliasStats.supplied === 0 ? null : aliasStats.used > 0,
    what: 'the paths the records name, and the relocated path each one was opened at',
  };
  if (aliasStats.supplied > 0 && aliasStats.used === 0) {
    problems.push({ why: 'a relocation map was supplied and no read used it, so the consumer read the originals', aliasReport });
  }
  const refused2 = problems.length > 0;
  return {
    task: c.task,
    entry: 'consumeHostChain',
    required_set: REQUIRED_UNITS.map((u) => ({ role: u.role, cases: u.cases })),
    observed,
    counts: seen,
    problems,
    refused: refused2,
    accepted: !refused2,
    spawn_attempts: spawnAfter - spawnBefore,
    path_aliases: aliasReport,
    ok_means: 'every build input, unit ELF, case, inner report, exit layer, argv and raw channel was opened from the path a '
      + 'record names, re-hashed, and compared with what that record claims; the required units and their case counts are this '
      + 'module own constants, so no caller can shrink the requirement',
  };
}

// The older entry point, kept because records were produced through it. It delegates: there is one decision in this file, and a
// second name for it is a name, not a second implementation.
export function consumeRuntimeChain(control) {
  return consumeHostChain(control);
}

const SELF_PATH = fileURLToPath(import.meta.url);
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH)) {
  const controlPath = process.argv[2];
  const outPath = process.argv[3];
  if (!controlPath || !outPath) {
    console.error('usage: host-consume-147.mjs <control.json> <verdict.json>');
    process.exit(2);
  }
  const control = JSON.parse(fs.readFileSync(controlPath, 'utf8'));
  const verdict = consumeHostChain(control);
  fs.writeFileSync(outPath, JSON.stringify(verdict, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ accepted: verdict.accepted, refused: verdict.refused,
    problems: verdict.problems.slice(0, 6), problem_count: verdict.problems.length,
    counts: verdict.counts, spawn_attempts: verdict.spawn_attempts }, null, 2));
  process.exit(verdict.accepted ? 0 : 1);
}
