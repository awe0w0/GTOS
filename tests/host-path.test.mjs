// GTOS-DSH-136 E: the path belongs to its consumer, and a write belongs inside its roots.
//
// The 135 defect in one line: the entry converted a host path to a Linux path and gave it to a Windows child, which resolved
// `/mnt/f/...` against the current drive and created `F:/mnt/f/...`. The review found the file there, 16929 bytes, and this
// packet preserved a copy by exact name without touching the original.
//
// What this test pins:
//
//   - a POSIX path handed to a Windows consumer is refused, which is the defect itself;
//   - a host path and its /mnt alias are the same identity, while two distributions and two ext4 casings are not;
//   - a write outside the declared roots is refused before anything is opened, including a sibling that merely shares a
//     prefix and a target that walks out with ..;
//   - the refusals happen on the platform that would perform the write, not on a string.
//
// The test never writes under F:/mnt. The wrong-domain cases are built in this packet's own TEST_ONLY tree.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as hp from '../tools/host-path.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-host-path-evidence-consumption-and-hardware-runtime-recovery-20261026-a';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};

// ---------------------------------------------------------------- 1. the defect, exactly
{
  let threw = null;
  try { hp.forConsumer('/mnt/f/GTOS-Chromium/artifacts/x/helper-report.json', 'win32'); } catch (e) { threw = e; }
  check('a POSIX path handed to a Windows consumer is refused',
    threw !== null && threw.obligation === 'domain', threw ? threw.message : 'it was accepted');
  check('the refusal names the path that would have been reinterpreted',
    threw !== null && threw.refused_path === '/mnt/f/GTOS-Chromium/artifacts/x/helper-report.json',
    threw ? threw.refused_path : null);
  // The exact string that became F:/mnt/f/... in 135.
  check('the path that created F:/mnt/f is refused rather than rewritten',
    threw !== null && !/^F:\/mnt/.test(String(hp.forConsumerSafe
      ? hp.forConsumerSafe('/mnt/f/x', 'win32')
      : '')) && true, null);
}

// A converter that never throws, for the one assertion above that needs a value.
{
  let value = null;
  try { value = hp.forConsumer('/mnt/f/x', 'win32'); } catch { value = null; }
  check('there is no silent conversion of /mnt/f into a Windows path', value === null, value);
}

// ---------------------------------------------------------------- 2. conversions at real boundaries
{
  check('a host path becomes a Linux path for the linker',
    hp.forConsumer('F:/GTOS-Chromium/x.o', 'linux') === '/mnt/f/GTOS-Chromium/x.o',
    hp.forConsumer('F:/GTOS-Chromium/x.o', 'linux'));
  check('a host path stays a host path for a Windows consumer',
    hp.forConsumer('F:/GTOS-Chromium/x.json', 'win32') === 'F:/GTOS-Chromium/x.json',
    hp.forConsumer('F:/GTOS-Chromium/x.json', 'win32'));
  check('a Linux path stays a Linux path for the linker',
    hp.forConsumer('/var/tmp/x.o', 'linux') === '/var/tmp/x.o', null);
  check('a UNC path becomes a Linux path for the linker',
    hp.forConsumer('//wsl$/Ubuntu-20.04/var/tmp/x.o', 'linux') === '/var/tmp/x.o', null);
  check('a UNC path stays a UNC path for a Windows consumer',
    hp.forConsumer('//wsl$/Ubuntu-20.04/var/tmp/x.json', 'win32') === '//wsl$/Ubuntu-20.04/var/tmp/x.json', null);
  // An unknown platform is a refusal, not a default.
  let threw = null;
  try { hp.forConsumer('F:/x', 'darwin'); } catch (e) { threw = e; }
  check('an unknown consumer platform is refused', threw !== null && threw.obligation === 'platform',
    threw ? threw.message : null);
  // A relative path is refused for both platforms: neither can be given one.
  for (const platform of ['win32', 'linux']) {
    let err = null;
    try { hp.forConsumer('relative/x.json', platform); } catch (e) { err = e; }
    check('a relative path is refused for ' + platform, err !== null && err.obligation === 'domain', null);
  }
}

// ---------------------------------------------------------------- 3. identity is for checking, not for opening
{
  check('a drive path and its /mnt alias are the same identity',
    hp.sameHostFile('F:/a/b.json', '/mnt/f/a/b.json'), null);
  check('a drive path and its UNC alias are the same identity',
    hp.sameHostFile('F:/a/b.json', '//wsl$/Ubuntu-20.04/mnt/f/a/b.json'), null);
  check('two different distributions are not the same identity',
    !hp.sameHostFile('//wsl$/Ubuntu-20.04/var/tmp/x', '//wsl$/Debian/var/tmp/x'), null);
  // ext4 keeps case, so two casings are two files there. This is the case the previous normaliser folded away.
  check('two ext4 casings are not the same identity',
    !hp.sameHostFile('/var/tmp/A.json', '/var/tmp/a.json'), null);
  check('two host casings are the same identity, because Windows is case-insensitive',
    hp.sameHostFile('F:/a/B.json', 'f:/A/b.json'), null);
  // And identity is not a path: the conversion still refuses the wrong domain for the same file.
  let threw = null;
  try { hp.forConsumer('/mnt/f/a/b.json', 'win32'); } catch (e) { threw = e; }
  check('identity does not license a wrong-domain conversion', threw !== null, null);
}

// ---------------------------------------------------------------- 4. the authorisation gate
{
  const T = R + '/test-only/e';
  if (fs.existsSync(T)) throw new Error('the fixture already exists; this test uses a fresh namespace: ' + T);
  fs.mkdirSync(T, { recursive: true });
  const roots = [T];

  // Inside is allowed.
  let ok = null;
  try { ok = hp.assertInsideRoots(T + '/inside.json', roots, 'win32'); } catch (e) { ok = null; }
  check('a write inside the declared root is allowed', ok !== null && ok.ok === true, ok);
  check('the allowed target is the consumer spelling', ok && ok.target === T + '/inside.json',
    ok ? ok.target : null);

  // The root itself is allowed, because a stage may write the root's own entries.
  let atRoot = null;
  try { atRoot = hp.assertInsideRoots(T + '/x.json', roots, 'win32'); } catch { atRoot = null; }
  check('a file directly under the root is allowed', atRoot !== null, null);

  // A sibling that merely shares a prefix is not inside. This is the boundary test a string prefix would fail.
  let sibling = null;
  try { hp.assertInsideRoots(T + 'evil/x.json', roots, 'win32'); } catch (e) { sibling = e; }
  check('a sibling that shares the root prefix is refused',
    sibling !== null && sibling.obligation === 'root', sibling ? sibling.message : 'it was allowed');

  // Outside entirely.
  let outside = null;
  try { hp.assertInsideRoots('F:/mnt/f/GTOS-Chromium/x.json', roots, 'win32'); } catch (e) { outside = e; }
  check('a target outside every root is refused', outside !== null && outside.obligation === 'root',
    outside ? outside.message : null);

  // The 135 location, named exactly: it is outside this packet's root and is refused.
  let legacy = null;
  try {
    hp.assertInsideRoots('F:/mnt/f/GTOS-Chromium/artifacts/native-executed-terminal-wiring-and-hardware-runtime-'
      + 'recovery-20261026-a/helper-report-wlk082173-01-135.json', roots, 'win32');
  } catch (e) { legacy = e; }
  check('the 135 outside-root location is refused by the gate',
    legacy !== null && legacy.obligation === 'root', legacy ? legacy.message : null);

  // A traversal is refused before the prefix test, because normalisation would otherwise move it back inside.
  let traversal = null;
  try { hp.assertInsideRoots(T + '/../e-evil/x.json', roots, 'win32'); } catch (e) { traversal = e; }
  check('a target that walks out with .. is refused',
    traversal !== null && traversal.obligation === 'traversal', traversal ? traversal.message : null);
  let traversal2 = null;
  try { hp.assertInsideRoots(T + '/a/../../x.json', roots, 'win32'); } catch (e) { traversal2 = e; }
  check('a deeper traversal is refused too', traversal2 !== null && traversal2.obligation === 'traversal', null);

  // No roots declared at all is a refusal, not a permission.
  let noRoots = null;
  try { hp.assertInsideRoots(T + '/x.json', [], 'win32'); } catch (e) { noRoots = e; }
  check('a write with no declared roots is refused',
    noRoots !== null && noRoots.obligation === 'roots', noRoots ? noRoots.message : null);

  // The gate is checked on the platform that writes: the same Linux path is outside a Linux root.
  let linuxOutside = null;
  try { hp.assertInsideRoots('/var/tmp/somewhere/x.json', [T], 'linux'); } catch (e) { linuxOutside = e; }
  check('the gate applies on the Linux side too', linuxOutside !== null && linuxOutside.obligation === 'root', null);

  // Nothing was created by any of the refusals.
  const created = fs.readdirSync(T);
  check('no refusal created a file', created.length === 0, created);
  check('no sibling tree was created either', !fs.existsSync(T + 'evil') && !fs.existsSync(T + '-evil'),
    null);
}

// ---------------------------------------------------------------- 5. the real report, read only
{
  const record = JSON.parse(readBytes(R + '/i0-outside-root-write-136.json').toString('utf8'));
  check('the outside-root write is recorded', record.outside_authorized_roots_write === true, null);
  check('the original location is named exactly', typeof record.original.path === 'string'
    && /^F:\/mnt\/f\//.test(record.original.path), record.original.path);
  check('the original bytes are pinned as the review recorded them',
    record.original.bytes === 16929
    && record.original.sha256 === '6540d5ebc7f7122b32d698e95bce0237e6feced9c918f426eae5309cf685d4f1',
    { bytes: record.original.bytes, sha256: record.original.sha256 });
  const copy = readBytes(record.preserved_copy.path);
  check('the preserved copy is byte identical to the original',
    copy !== null && sha(copy) === record.original.sha256, copy === null ? null : sha(copy));
  check('the copy is inside this packet root',
    record.preserved_copy.path.startsWith(R), record.preserved_copy.path);
  check('the record states what was not done',
    record.not_authorized.some((x) => /F:\/mnt/.test(x)) && record.not_authorized.some((x) => /delet|moving/.test(x)),
    record.not_authorized);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-136', kind: 'host_path_test',
  subject: 'references/host-path-136.mjs',
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
