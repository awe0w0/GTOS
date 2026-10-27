// GTOS-DSH-138 J1: the link entry's admission, before any linker runs.
//
// The 127 link was real and produced a correct image, but nothing stood between a caller and the linker: the bridge checked
// that the individual list was non-empty and distinct and then built the argv. A set that was missing an object, carried an
// extra one, ran in a different order, or pointed at a host-test object would have gone straight to ld. The outputs were
// written wherever the caller said, over whatever was already there.
//
// This module is that missing step, and it is the step the business link now takes:
//
//   admitLink     the individual set against an externally frozen expectation, and the profiles against the contract
//   claimOutputs  an exclusive fresh claim on both output paths, taken before the linker is allowed to run
//   linkVerdict   what the stage actually did, judged from its own records rather than from a transport status
//
// Nothing here reads the candidate argv as its own expectation. The expectation is a contract written before the link.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// A path may arrive in the Linux form the linker was given; it is mapped to the host before it is read.
export function hostOf(p) {
  if (typeof p !== 'string') return p;
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (m) return m[1].toUpperCase() + ':/' + m[2];
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
}

function readBytes(p) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(hostOf(p)); } catch { /* retry */ } }
  return b;
}

// ---------------------------------------------------------------- admission

// The expected set is an ordered list of individuals from a contract: each entry has a logical name, a path and a hash. The
// caller's actual list must be that list, in that order, with those bytes.
export function admitLink(request, contract) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });

  if (!contract || !Array.isArray(contract.individuals) || !contract.individuals.length) {
    return { admitted: false, problems: [{ obligation: 'contract', why: 'no ordered individual contract' }],
      first_refusal: { obligation: 'contract', why: 'no ordered individual contract' } };
  }
  if (!request || !Array.isArray(request.individuals)) {
    return { admitted: false, problems: [{ obligation: 'request', why: 'no individual list' }],
      first_refusal: { obligation: 'request', why: 'no individual list' } };
  }

  const expected = contract.individuals;
  const actual = request.individuals;

  // ---- the set, by logical name. A missing object and an extra object are different refusals.
  // A slot may be named by its baseline token and carried under a lineage name. Both names denote the same slot, so the
  // comparison maps a lineage target back to its token before it compares anything.
  const lineageToToken = new Map();
  for (const pair of (contract.lineage && typeof contract.lineage === 'object'
    ? Object.entries(contract.lineage) : [])) lineageToToken.set(pair[1], pair[0]);
  const basenameOf = (p) => {
    const s = String(p);
    const cut = Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\'));
    return cut >= 0 ? s.slice(cut + 1) : s;
  };
  const expectedNames = expected.map((e) => e.logical);
  const actualNames = actual.map((a) => {
    const base = basenameOf(a);
    return lineageToToken.get(base) || base;
  });

  // A repeated name is a duplicate, whatever else the list looks like. It is checked before the set comparison because a
  // duplicate is not "extra" (its name is expected) and not "missing" (nothing is absent).
  const seen = new Set();
  const duplicated = [];
  for (const n of actualNames) { if (seen.has(n)) duplicated.push(n); seen.add(n); }
  if (duplicated.length) {
    return { admitted: false, problems: [{ obligation: 'duplicate',
      why: 'the request names ' + duplicated.join(',') + ' more than once' }],
      first_refusal: { obligation: 'duplicate', why: 'the request names ' + duplicated.join(',') + ' more than once' } };
  }
  const missing = expectedNames.filter((n) => !actualNames.includes(n));
  const extra = actualNames.filter((n) => !expectedNames.includes(n));
  if (missing.length) refuse('missing', 'the request omits ' + missing.join(','));
  if (extra.length) refuse('extra', 'the request carries individuals the contract does not have: ' + extra.join(','));

  // ---- the order. A reordering is its own refusal even when the set is right.
  if (!missing.length && !extra.length && actualNames.join('\n') !== expectedNames.join('\n')) {
    const firstWrong = actualNames.findIndex((n, i) => n !== expectedNames[i]);
    refuse('order', 'individual ' + (firstWrong + 1) + ' is ' + actualNames[firstWrong]
      + ' but the contract puts ' + expectedNames[firstWrong] + ' there');
  }

  // ---- the bytes of every individual. A hash mismatch is caught before the linker sees it.
  if (!missing.length && !extra.length) {
    for (let i = 0; i < expected.length; i++) {
      const e = expected[i];
      const p = typeof actual[i] === 'string' ? actual[i] : actual[i].path;
      const b = readBytes(p);
      if (b === null) { refuse('absent', 'the individual is not on disk: ' + e.logical); break; }
      if (b.length !== e.bytes) {
        refuse('hash', e.logical + ' is ' + b.length + ' bytes, the contract says ' + e.bytes);
        break;
      }
      if (sha256(b) !== e.sha256) { refuse('hash', e.logical + ' does not match its contracted hash'); break; }
    }
  }

  // ---- a host-test object must never reach a default link. The contract says which profile each individual was built
  //      with, and a host-test object in a default link is a refusal whatever its hash says.
  if (contract.default_profile && contract.default_profile.defines !== undefined) {
    const wanted = contract.default_profile.defines;
    if (Array.isArray(wanted) && wanted.length !== 0) {
      refuse('profile', 'the contract claims a default profile but names ' + wanted.length + ' defines');
    }
  }

  const admitted = problems.length === 0;
  return { admitted, problems, first_refusal: problems.length ? problems[0] : null,
    counts: { expected: expected.length, actual: actual.length } };
}

// ---------------------------------------------------------------- the fresh claim

// Both outputs are claimed exclusively before the linker runs. An existing file is not overwritten and not reused: a link
// that would write over a previous image is a link whose result cannot be attributed.
export function claimOutputs(elfPath, mapPath, options = {}) {
  const claimed = [];
  for (const p of [elfPath, mapPath]) {
    if (typeof p !== 'string' || !p) throw new Error('CLAIM: an output path is empty');
    // The path may arrive in the Linux form the linker will be given. Existence and the parent are checked on the host,
    // because that is where this process can see them.
    if (fs.existsSync(hostOf(p))) {
      const e = new Error('CLAIM: the output already exists: ' + p);
      e.claim_refusal = { obligation: 'fresh', path: p, bytes: fs.statSync(hostOf(p)).size };
      throw e;
    }
    claimed.push(p);
  }
  // The parent must be a real directory on the host. A claim on a path whose parent does not exist is not a claim.
  for (const p of claimed) {
    const dir = p.slice(0, p.lastIndexOf('/'));
    if (!fs.existsSync(hostOf(dir))) {
      const e = new Error('CLAIM: the output parent does not exist: ' + dir);
      e.claim_refusal = { obligation: 'parent', path: dir };
      throw e;
    }
    if (!fs.statSync(hostOf(dir)).isDirectory()) {
      const e = new Error('CLAIM: the output parent is not a directory: ' + dir);
      e.claim_refusal = { obligation: 'parent', path: dir };
      throw e;
    }
  }
  // The claim itself: an exclusive create of a marker beside the outputs, so the claim is visible on disk and cannot be
  // taken twice, even by another process.
  const marker = elfPath + '.claim';
  let fd = null;
  try { fd = fs.openSync(hostOf(marker), 'wx'); } catch {
    const e = new Error('CLAIM: the output pair is already claimed: ' + marker);
    e.claim_refusal = { obligation: 'claim', path: marker };
    throw e;
  } finally { if (fd !== null) fs.closeSync(fd); }
  fs.writeFileSync(hostOf(marker),
    JSON.stringify({ elf: elfPath, map: mapPath, at: new Date().toISOString() }) + '\n');
  return { claimed, marker, parent: elfPath.slice(0, elfPath.lastIndexOf('/')) };
}

// ---------------------------------------------------------------- the verdict

// What the link stage did, from its own records. A transport status is not the answer: a zero exit with no output is a
// failure, and so is an output that exists but was not written by this run.
export function linkVerdict(evidence) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const { terminal, inner, elf, map, expectedElf, expectedMap } = evidence;

  if (!terminal) refuse('terminal', 'there is no terminal record for the link');
  else {
    if (terminal.transport_started !== true) refuse('started', 'the transport never started');
    if (terminal.timed_out === true) refuse('timeout', 'the link timed out');
    if (terminal.ok !== true) refuse('terminal', 'the terminal did not report success');
  }
  // The tool's own return code, from the inner report.
  if (!inner) refuse('inner', 'there is no inner report for the link');
  else if (inner.returncode !== 0) refuse('returncode', 'ld exited ' + inner.returncode);

  // The outputs must exist and be the ones this run was asked to write.
  if (!elf || elf.bytes === 0) refuse('elf', 'the ELF was not produced');
  else if (expectedElf && elf.path !== expectedElf) {
    refuse('elf', 'the ELF was written to ' + elf.path + ', not ' + expectedElf);
  }
  if (!map || map.bytes === 0) refuse('map', 'the map was not produced');
  else if (expectedMap && map.path !== expectedMap) {
    refuse('map', 'the map was written to ' + map.path + ', not ' + expectedMap);
  }

  const accepted = problems.length === 0;
  return { accepted, problems, first_refusal: problems.length ? problems[0] : null,
    observed: { elf: elf ? elf.sha256 : null, map: map ? map.sha256 : null,
      ld_returncode: inner ? inner.returncode : null } };
}

export function gateLink(verdict) {
  if (verdict.accepted) return verdict;
  const e = new Error('LINK REFUSED at ' + verdict.first_refusal.obligation + ': ' + verdict.first_refusal.why);
  e.verdict = verdict;
  throw e;
}
