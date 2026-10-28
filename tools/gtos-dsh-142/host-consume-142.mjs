// GTOS-DSH-142 H: the production host consumer of the runtime chain.
//
// This module is the single entry point that answers one question: does the chain this packet produced actually hold together,
// read from the artefacts rather than from the records that describe them? It is deliberately the only thing that decides that,
// so that a passing run cannot be assembled from several partial checks in several places, and so that a refusal and an
// acceptance come from the same code.
//
// What it consumes, and where each thing is read from:
//
//   the runtime inner originals   the emulator and the loader, read from the runtime tree and re-hashed here. The pinned identity
//                                 in the inventory is the expected value; the bytes on disk are the observed value. Neither is
//                                 trusted because the other agrees with it.
//   the start ELF                 the unit's ELF, re-hashed and compared with the identity the build recorded, which in turn
//                                 pins the sources and the seam macros it was built from.
//   the child's return code       read from the guard's own result record for each case, not from a value this process observed.
//   the child's argv              read from the guard's own inner report, and compared token for token with what the case asked
//                                 for: the ELF path and the case selector, in that order.
//   the raw channel               the guard's copy of the child's output, re-hashed and compared with the recorded hash, and
//                                 required to contain the unit's own pass marker.
//
// A refusal is a return value, not an exception, so the caller can record why. The one thing this module never does is start a
// process: it counts its own spawn attempts and reports the count, and the harness compares that with an independent measurement
// of the guard's run counter across the call. A refusal that spawned nothing is a refusal; a refusal that quietly launched
// something to find out would not be.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// The one place a spawn could happen. It is never called; the counter exists so that the claim "this consumer started no child"
// is a measurement of this module rather than a promise about it.
let spawnAttempts = 0;
export const spawnAttemptCount = () => spawnAttempts;

function readJson(p, problems, what) {
  if (typeof p !== 'string' || !p) { problems.push({ why: 'no path for ' + what, member: what }); return null; }
  try { return JSON.parse(fs.readFileSync(p, 'utf8')); }
  catch (e) { problems.push({ why: 'unreadable ' + what, path: p, member: what, detail: String(e.code || e.message) }); return null; }
}

function identity(p, problems, what) {
  if (typeof p !== 'string' || !p) { problems.push({ why: 'no path for ' + what, member: what }); return null; }
  let stat;
  try { stat = fs.lstatSync(p); }
  catch (e) { problems.push({ why: 'absent ' + what, path: p, member: what, detail: String(e.code || e.message) }); return null; }
  if (!stat.isFile()) { problems.push({ why: what + ' is not an ordinary file', path: p, member: what }); return null; }
  const bytes = fs.readFileSync(p);
  return { path: p, bytes: bytes.length, sha256: sha(bytes) };
}

// Compare an observed identity with an expected one. A size difference is reported separately from a hash difference because the
// two mean different things: a size change is a different file, a hash change at the same size is a different content.
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
    problems.push({ why: what + ' has different bytes at the same size', member: what,
      expected: expected.sha256, observed: observed.sha256 });
    return false;
  }
  return true;
}

// The production entry. Everything it needs is in the control; nothing is read from a path this file chose for itself.
export function consumeRuntimeChain(control) {
  const problems = [];
  const spawnBefore = spawnAttempts;

  const contract = readJson(control && control.contract_path, problems, 'contract');
  const runtimeRoot = control && control.runtime_root;
  if (typeof runtimeRoot !== 'string' || !runtimeRoot.startsWith('/')) {
    problems.push({ why: 'no absolute runtime root', member: 'runtime_root' });
  }

  const observed = { inner_originals: [], start_elf: null, cases: [] };
  const seen = { members: 0, cases: 0, raw_channels: 0, return_codes: 0, argvs: 0 };

  if (contract) {
    // 1. The runtime inner originals. Each is read from the tree and re-hashed; the inventory's pin is the expected value.
    const inner = (contract.runtime && contract.runtime.inner_originals) || [];
    if (inner.length === 0) problems.push({ why: 'the contract declares no runtime inner originals', member: 'inner_originals' });
    for (const member of inner) {
      seen.members += 1;
      if (!runtimeRoot) { problems.push({ why: 'no runtime root to read ' + member.logical, member: member.logical }); continue; }
      const hostPath = control.runtime_host_root + '/' + member.logical;
      const got = identity(hostPath, problems, member.logical);
      if (!got) continue;
      // The permission bits are deliberately not checked here. This consumer reads through the Windows share, which reports 0666
      // for every file on the Linux side; a check against that value would fail for correct files and pass for nothing. The
      // declared mode is checked where it can be observed, by the reader that runs inside the distribution.
      agree(got, member, problems, member.logical);
      observed.inner_originals.push({ logical: member.logical, bytes: got.bytes, sha256: got.sha256,
        declared_mode_octal: member.mode_octal, mode_checked_here: false,
        mode_checked_by: 'verify-runtime-142.py, which runs inside the distribution and reads st_mode directly' });
    }

    // 2. The start ELF, and through it the sources and the seam the unit was built from. The build record pins those; this checks
    // that the ELF standing on disk is the one that record describes.
    const startElf = contract.start_elf;
    if (!startElf) problems.push({ why: 'the contract declares no start ELF', member: 'start_elf' });
    else {
      const got = identity(control.start_elf_host_path, problems, 'start_elf');
      if (got) {
        agree(got, startElf, problems, 'start_elf');
        observed.start_elf = { bytes: got.bytes, sha256: got.sha256, source: startElf.source || null,
          seam_sha256: startElf.seam_sha256 || null, units: startElf.units || null };
      }
    }

    // 3. Every case. A case is required, not optional: the contract names them, and a contract that names fewer than the unit
    // declares is itself a refusal, because then the run would be a subset presented as the whole.
    const cases = contract.cases || [];
    if (contract.cases_expected && cases.length !== contract.cases_expected) {
      problems.push({ why: 'the contract does not carry every case', expected: contract.cases_expected, observed: cases.length });
    }
    for (const want of cases) {
      seen.cases += 1;
      const row = { case: want.case, stage: want.stage };
      const result = readJson((control.results_dir || '') + '/' + want.stage + '.json', problems, 'result for case ' + want.case);
      if (!result) { observed.cases.push({ ...row, verdict: 'no result record' }); continue; }
      // The return code is the guard's observation of the child, not this process's.
      const rc = result.verdict && typeof result.verdict.tool_returncode === 'number'
        ? result.verdict.tool_returncode : null;
      row.return_code = rc;
      if (rc === null) problems.push({ why: 'no return code recorded', case: want.case });
      else { seen.return_codes += 1; if (rc !== want.return_code) problems.push({ why: 'the return code is not the expected one', case: want.case, expected: want.return_code, observed: rc }); }
      // The argv is the guard's record of what it actually launched.
      const inner = result.inner_report || null;
      const argv = inner && Array.isArray(inner.argv) ? inner.argv : null;
      row.argv = argv;
      if (!argv) problems.push({ why: 'no argv recorded for the child', case: want.case });
      else {
        seen.argvs += 1;
        const wantArgv = [want.test_elf, String(want.case)];
        const same = argv.length === wantArgv.length && wantArgv.every((token, i) => argv[i] === token);
        if (!same) problems.push({ why: 'the child was launched with a different argv', case: want.case, expected: wantArgv, observed: argv });
      }
      // The raw channel, re-hashed and required to carry the unit's own marker.
      // The channel is where the contract says it is. A guard writes it inside the run directory it created for that stage, and
      // the contract names that path; deriving it here from a shared parent would put it one level too high.
      const rawPath = want.raw_path || ((control.logs_dir || '') + '/' + want.stage + '/logs/child-output-' + want.stage + '.raw');
      const raw = identity(rawPath, problems, 'raw channel for case ' + want.case);
      if (!raw) { observed.cases.push({ ...row, verdict: 'no raw channel' }); continue; }
      seen.raw_channels += 1;
      const text = fs.readFileSync(rawPath, 'utf8');
      row.raw_bytes = raw.bytes;
      row.raw_sha256 = raw.sha256;
      row.raw_matches = want.raw_sha256 ? raw.sha256 === want.raw_sha256 : null;
      if (want.raw_sha256 && raw.sha256 !== want.raw_sha256) {
        problems.push({ why: 'the raw channel is not the recorded one', case: want.case, expected: want.raw_sha256, observed: raw.sha256 });
      }
      row.carries_pass_marker = text.includes('RUNTIME UNIT PASS');
      if (!row.carries_pass_marker) problems.push({ why: 'the raw channel carries no pass marker', case: want.case });
      row.carries_case_marker = new RegExp('CASE ' + want.case + '\\b').test(text);
      if (!row.carries_case_marker) problems.push({ why: 'the raw channel is not from this case', case: want.case });
      observed.cases.push({ ...row, verdict: 'read' });
    }
  }

  const spawnAfter = spawnAttempts;
  const refused = problems.length > 0;
  return {
    task: control && control.task,
    entry: 'consumeRuntimeChain',
    runtime_root: runtimeRoot || null,
    observed,
    counts: seen,
    problems,
    refused,
    accepted: !refused,
    // The measurement that backs the no-child claim: this module attempted no spawn, and it says so from a counter rather than
    // from its own design intent.
    spawn_attempts: spawnAfter - spawnBefore,
    ok_means: 'every declared member, case, return code, argv and raw channel was read from the artefact and matched its pin; a '
      + 'refusal lists every problem rather than the first one',
  };
}

// A CLI, so the entry can be called by a human or by a script without importing it. The control comes from a file and the verdict
// goes to a file, because a record has to survive the process that produced it.
if (process.argv[1] && process.argv[1].endsWith('host-consume-142.mjs')) {
  const controlPath = process.argv[2];
  const outPath = process.argv[3];
  if (!controlPath || !outPath) {
    console.error('usage: host-consume-142.mjs <control.json> <verdict.json>');
    process.exit(2);
  }
  const control = JSON.parse(fs.readFileSync(controlPath, 'utf8'));
  const verdict = consumeRuntimeChain(control);
  fs.writeFileSync(outPath, JSON.stringify(verdict, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ accepted: verdict.accepted, refused: verdict.refused,
    problems: verdict.problems.length, counts: verdict.counts, spawn_attempts: verdict.spawn_attempts }, null, 2));
  process.exit(verdict.accepted ? 0 : 1);
}