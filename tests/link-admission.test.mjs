// GTOS-DSH-128 J1: the link entry refuses bad input sets and claims its outputs, before any linker runs.
//
// In 127 the link was real and correct, but the only checks between the caller and ld were "non-empty" and "distinct". A
// missing object, an extra object, a reordering or a host-test object would have gone straight to the linker, and the
// outputs were written over whatever was there.
//
// Every negative here is refused at the same entry the business link uses, and each is refused for its own reason: the test
// starts from a real admitted baseline and breaks exactly one obligation, so the first refusal names that obligation.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as la from '../tools/link-admission.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-trusted-observation-link-admission-runtime-recovery-20261026-a';
// Where this run own fixtures go, kept apart from R because R is where the 128 link record this test reads lives and that
// namespace is read here, never written.
const F = process.argv[5] || R;
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const hostOf = (p) => {
  if (typeof p !== 'string') return p;
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (m) return m[1].toUpperCase() + ':/' + m[2];
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
};
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(hostOf(p)); } catch { /* retry */ } }
  return b;
};
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };

// ---------------------------------------------------------------- a real contract over real objects
// The individuals are real object files from this packet's own link record, so the hashes and sizes are real and a hash
// negative is a real hash negative.
const linkRecordPath = R + '/default-link-idl1.json';
const linkRecord = JSON.parse(readBytes(linkRecordPath).toString('utf8'));
const individuals = linkRecord.provenance.map((p) => p.path);
check('the link record names 61 individuals', individuals.length === 61, individuals.length);

const contractIndividuals = individuals.map((p) => {
  const b = readBytes(p);
  if (b === null) throw new Error('an individual is not on disk: ' + p);
  return { logical: p.split('/').pop(), path: p, bytes: b.length, sha256: sha(b) };
});
const contract = {
  task: 'GTOS-DSH-128', kind: 'link_input_contract',
  order_from: 'the frozen baseline link spec',
  default_profile: { defines: [] },
  individuals: contractIndividuals,
};

// ---------------------------------------------------------------- the positive
const good = la.admitLink({ individuals }, contract);
check('the real individual set is admitted', good.admitted === true, good.problems);
check('the admission counted 61 on both sides',
  good.counts.expected === 61 && good.counts.actual === 61, good.counts);

// ---------------------------------------------------------------- one obligation broken at a time
function expectRefusal(name, mutate, obligation) {
  const request = { individuals: mutate(individuals.slice()) };
  const v = la.admitLink(request, contract);
  check('refused at ' + obligation + ': ' + name,
    v.admitted === false && v.first_refusal && v.first_refusal.obligation === obligation, v.first_refusal);
}

expectRefusal('an object is missing', (list) => list.filter((_, i) => i !== 30), 'missing');
expectRefusal('an object is duplicated', (list) => [...list, list[10]], 'duplicate');
expectRefusal('two objects are swapped', (list) => {
  const out = list.slice();
  const t = out[5]; out[5] = out[6]; out[6] = t;
  return out;
}, 'order');
// The same logical name, a path where the file is not. A different basename would be a different object and would be
// refused as missing instead, which would not test absence.
expectRefusal('an object is not on disk', (list) => {
  const out = list.slice();
  const name = list[20].split('/').pop();
  out[20] = F + '/test-only/j1/' + name;
  return out;
}, 'absent');

// A hash mismatch needs a real file whose bytes differ from the contract. A TEST_ONLY copy of a real object with one byte
// changed is exactly that, and it is a real file rather than a fabricated hash.
{
  const T = F + '/test-only/j1';
  fs.mkdirSync(T, { recursive: true });
  const victim = individuals[40];
  const original = readBytes(victim);
  const drifted = Buffer.from(original);
  drifted[drifted.length - 1] = drifted[drifted.length - 1] ^ 0xff;
  // The same basename, so the object is the same logical individual and only its bytes differ.
  const driftedPath = T + '/' + victim.split('/').pop();
  fs.writeFileSync(driftedPath, drifted);
  check('the drifted copy is the same size as the original', drifted.length === original.length,
    { drifted: drifted.length, original: original.length });

  const out = individuals.slice();
  out[40] = driftedPath;
  const v = la.admitLink({ individuals: out }, contract);
  check('refused at hash: an individual whose bytes differ from the contract, at unchanged size',
    v.admitted === false && v.first_refusal && v.first_refusal.obligation === 'hash', v.first_refusal);
}

// A contract that claims a default profile but names macros is refused, because a default link defines nothing.
{
  const badContract = JSON.parse(JSON.stringify(contract));
  badContract.default_profile.defines = ['GTOS_MEMORY_TEST'];
  const v = la.admitLink({ individuals }, badContract);
  check('refused at profile: a contract that calls a host-test build the default profile',
    v.admitted === false && v.first_refusal && v.first_refusal.obligation === 'profile', v.first_refusal);
}

// A contract with no individuals at all is refused before anything else is examined.
{
  const v = la.admitLink({ individuals }, { task: 'GTOS-DSH-128', individuals: [] });
  check('refused at contract: no ordered individual contract',
    v.admitted === false && v.first_refusal.obligation === 'contract', v.first_refusal);
}

// ---------------------------------------------------------------- the fresh claim
{
  const T = F + '/test-only/j1-claim';
  if (fs.existsSync(T)) throw new Error('the claim fixture already exists: ' + T);
  fs.mkdirSync(T, { recursive: true });

  // A fresh pair is claimed.
  const elf = T + '/image.elf';
  const map = T + '/image.map';
  let claim = null;
  try { claim = la.claimOutputs(elf, map); } catch (error) {
    check('a fresh output pair is claimed', false, String(error.message));
  }
  check('a fresh output pair is claimed', claim !== null && fs.existsSync(claim.marker), claim && claim.marker);
  check('the claim marker names both outputs',
    claim !== null && readBytes(claim.marker).toString('utf8').includes('image.elf')
    && readBytes(claim.marker).toString('utf8').includes('image.map'), null);

  // A second claim on the same pair is refused.
  let secondRefused = false;
  let secondMessage = null;
  try { la.claimOutputs(elf, map); } catch (error) {
    secondRefused = true; secondMessage = String(error.message);
  }
  check('a second claim on the same pair is refused', secondRefused && /already claimed/.test(String(secondMessage)),
    secondMessage);

  // An output that already exists is refused: a link never overwrites a previous image.
  fs.writeFileSync(T + '/existing.elf', 'previous image\n');
  let existedRefused = false;
  let existedMessage = null;
  try { la.claimOutputs(T + '/existing.elf', T + '/existing.map'); } catch (error) {
    existedRefused = true; existedMessage = String(error.message);
  }
  check('a claim on an existing output is refused', existedRefused && /already exists/.test(String(existedMessage)),
    existedMessage);
  check('the existing output is byte identical after the refusal',
    readBytes(T + '/existing.elf').toString('utf8') === 'previous image\n', null);

  // A claim whose parent does not exist is refused.
  let parentRefused = false;
  let parentMessage = null;
  try { la.claimOutputs(T + '/no-such-dir/x.elf', T + '/no-such-dir/x.map'); } catch (error) {
    parentRefused = true; parentMessage = String(error.message);
  }
  check('a claim under a missing parent is refused', parentRefused && /parent/.test(String(parentMessage)),
    parentMessage);
}

// ---------------------------------------------------------------- the verdict is not a transport status
{
  // A zero exit with no output is a failure. This is the shape 124 caught and it must stay caught.
  const v = la.linkVerdict({
    terminal: { transport_started: true, timed_out: false, ok: true },
    inner: { returncode: 0 },
    elf: null, map: null,
    expectedElf: '/x/a.elf', expectedMap: '/x/a.map',
  });
  check('a zero exit with no ELF is refused', v.accepted === false && v.first_refusal.obligation === 'elf',
    v.first_refusal);

  // Only one of the two outputs is a failure.
  const v2 = la.linkVerdict({
    terminal: { transport_started: true, timed_out: false, ok: true },
    inner: { returncode: 0 },
    elf: { path: '/x/a.elf', bytes: 100, sha256: 'a'.repeat(64) }, map: null,
    expectedElf: '/x/a.elf', expectedMap: '/x/a.map',
  });
  check('an ELF with no map is refused', v2.accepted === false && v2.first_refusal.obligation === 'map',
    v2.first_refusal);

  // The right outputs written somewhere else is a failure of attribution.
  const v3 = la.linkVerdict({
    terminal: { transport_started: true, timed_out: false, ok: true },
    inner: { returncode: 0 },
    elf: { path: '/elsewhere/a.elf', bytes: 100, sha256: 'a'.repeat(64) },
    map: { path: '/x/a.map', bytes: 10, sha256: 'b'.repeat(64) },
    expectedElf: '/x/a.elf', expectedMap: '/x/a.map',
  });
  check('outputs written to the wrong place are refused',
    v3.accepted === false && v3.first_refusal.obligation === 'elf', v3.first_refusal);

  // A non-zero ld is a failure even when the outputs exist.
  const v4 = la.linkVerdict({
    terminal: { transport_started: true, timed_out: false, ok: false },
    inner: { returncode: 1 },
    elf: { path: '/x/a.elf', bytes: 100, sha256: 'a'.repeat(64) },
    map: { path: '/x/a.map', bytes: 10, sha256: 'b'.repeat(64) },
    expectedElf: '/x/a.elf', expectedMap: '/x/a.map',
  });
  check('a non-zero ld is refused even though both outputs exist',
    v4.accepted === false && v4.first_refusal.obligation === 'terminal', v4.first_refusal);

  // The real shape is accepted.
  const v5 = la.linkVerdict({
    terminal: { transport_started: true, timed_out: false, ok: true },
    inner: { returncode: 0 },
    elf: { path: '/x/a.elf', bytes: 745124, sha256: 'c'.repeat(64) },
    map: { path: '/x/a.map', bytes: 262015, sha256: 'd'.repeat(64) },
    expectedElf: '/x/a.elf', expectedMap: '/x/a.map',
  });
  check('the well-formed link shape is accepted', v5.accepted === true, v5.problems);

  // The gate throws rather than returning false.
  let threw = null;
  try { la.gateLink(v); } catch (error) { threw = error; }
  check('the gate throws', threw !== null && /LINK REFUSED/.test(String(threw.message)),
    threw ? threw.message : null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-128', kind: 'link_admission_test',
  subject: 'references/link-admission-128.mjs',
  contract_source: linkRecordPath,
  individuals: contractIndividuals.length,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
