// GTOS-DSH-110 A0 test: the real production normaliser and request builder.
//
// Every expectation below calls the actual functions in references/native-request-110.mjs. No algorithm is copied and
// no model of the scan is substituted. The four 109 root causes are each reproduced as a named regression.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const results = [];
let failed = 0;
function check(name, ok, detail) {
  results.push({ name, ok: Boolean(ok), detail });
  if (!ok) failed += 1;
}
function refuses(name, fn, matcher) {
  let error = null;
  try { fn(); } catch (e) { error = String(e && e.message ? e.message : e); }
  const ok = error !== null && (!matcher || matcher.test(error));
  check(name, ok, error === null ? 'did not refuse' : error.slice(0, 130));
  return error;
}

const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const R = control.report_root;
const FIX = control.fixture_root;
if (fs.existsSync(FIX)) throw new Error('the fixture root must not exist yet: ' + FIX);
fs.mkdirSync(FIX, { recursive: true });

const subjectPath = control.request_module;
const subjectBefore = { path: subjectPath, bytes: fs.statSync(subjectPath).size,
  sha256: sha(fs.readFileSync(subjectPath)) };
const N = await import(pathToFileURL(subjectPath).href);

// ================================================================ 1. path identity
check('the module exports the normaliser surface',
  ['hostToLinux', 'linuxToHost', 'describeFile', 'parseToolRoles', 'requireRole', 'execSetFor',
    'assertNoShadowing', 'linuxiseDeep', 'assertNoHostPaths', 'buildLinuxControl', 'claimWave',
    'isSetupContainer', 'subjectPin', 'assertSubjectUnchanged']
    .every((k) => typeof N[k] === 'function'), true);

check('a Windows host path becomes the WSL mount form',
  N.hostToLinux('F:/GTOS-Chromium/artifacts/x.json') === '/mnt/f/GTOS-Chromium/artifacts/x.json',
  N.hostToLinux('F:/GTOS-Chromium/artifacts/x.json'));
check('backslashes are accepted and normalised',
  N.hostToLinux('F:\\GTOS-Chromium\\a\\b.cpp') === '/mnt/f/GTOS-Chromium/a/b.cpp',
  N.hostToLinux('F:\\GTOS-Chromium\\a\\b.cpp'));
check('a //wsl$/ form becomes the plain Linux path',
  N.hostToLinux('//wsl$/Ubuntu-20.04/var/tmp/x') === '/var/tmp/x',
  N.hostToLinux('//wsl$/Ubuntu-20.04/var/tmp/x'));
check('a Linux path is returned unchanged',
  N.hostToLinux('/var/tmp/a/b') === '/var/tmp/a/b', true);
check('the round trip recovers the host identity',
  N.linuxToHost('/mnt/f/GTOS-Chromium/a.json') === 'F:/GTOS-Chromium/a.json',
  N.linuxToHost('/mnt/f/GTOS-Chromium/a.json'));
check('a non-mounted Linux path round trips through the wsl host form',
  N.linuxToHost('/var/tmp/x') === '//wsl$/Ubuntu-20.04/var/tmp/x',
  N.linuxToHost('/var/tmp/x'));
refuses('a drive-escaping path refuses', () => N.hostToLinux('F:/a/../../etc/passwd'), /escapes its drive/);
refuses('an unrecognised path form refuses', () => N.hostToLinux('relative/path'), /unrecognised/);
refuses('an empty path refuses', () => N.hostToLinux(''), /non-empty/);
refuses('a NUL in a path refuses', () => N.hostToLinux('F:/a\0b'), /NUL/);

// ================================================================ 2. file description carries all five facts
const described = N.describeFile({ logical: 'src/kernel.cpp', host: 'F:/GTOS-Chromium/src/kernel.cpp',
  role: 'kernel-source', bytes: 12, sha256: 'a'.repeat(64) });
check('a described file carries host, Linux, logical, role and pin together',
  described.host === 'F:/GTOS-Chromium/src/kernel.cpp'
  && described.linux === '/mnt/f/GTOS-Chromium/src/kernel.cpp'
  && described.logical === 'src/kernel.cpp' && described.role === 'kernel-source'
  && described.pin.bytes === 12 && described.pin.sha256 === 'a'.repeat(64), described);
refuses('an absolute logical identity refuses',
  () => N.describeFile({ logical: '/abs/x', host: 'F:/a', role: 'r', bytes: 1, sha256: 'a'.repeat(64) }),
  /safe relative path/);
refuses('a traversing logical identity refuses',
  () => N.describeFile({ logical: '../x', host: 'F:/a', role: 'r', bytes: 1, sha256: 'a'.repeat(64) }),
  /safe relative path/);
refuses('a missing role refuses',
  () => N.describeFile({ logical: 'a', host: 'F:/a', bytes: 1, sha256: 'a'.repeat(64) }), /role required/);
refuses('a missing pin refuses',
  () => N.describeFile({ logical: 'a', host: 'F:/a', role: 'r', bytes: 1 }), /sha256 required/);

// ================================================================ 3. tool roles from the real authority
const authority = JSON.parse(fs.readFileSync(control.authority_path, 'utf8'));
const roles = N.parseToolRoles(authority);
check('the real authority yields five tool roles', roles.size === 5, roles.size);
check('every real tool role is present',
  ['g++', 'as', 'loader', 'cc1plus', 'ld.bfd'].every((r) => roles.has(r)), [...roles.keys()]);
check('the loader role resolves from tools[]',
  N.requireRole(roles, 'loader').linux === '/usr/lib/x86_64-linux-gnu/ld-2.31.so',
  N.requireRole(roles, 'loader').linux);
check('the system-loader alias resolves through the same table',
  N.requireRole(roles, 'system-loader').linux === N.requireRole(roles, 'loader').linux, true);
check('the authority system_loader alias agrees with the tools[] loader role',
  authority.system_loader.path === N.requireRole(roles, 'loader').linux
  && authority.system_loader.sha256 === N.requireRole(roles, 'loader').sha256, true);

// 109 root cause 1: the bridge read a key that did not exist and continued.
refuses('a missing required role refuses',
  () => N.requireRole(N.parseToolRoles({ tools: [{ role: 'g++', path: '/usr/bin/g++', bytes: 1, sha256: 'a'.repeat(64) }] }), 'loader'),
  /required role loader is absent/);
refuses('a duplicate role refuses',
  () => N.parseToolRoles({ tools: [
    { role: 'g++', path: '/a', bytes: 1, sha256: 'a'.repeat(64) },
    { role: 'g++', path: '/b', bytes: 1, sha256: 'b'.repeat(64) }] }), /duplicate role g\+\+/);
refuses('a tool with no role refuses',
  () => N.parseToolRoles({ tools: [{ path: '/a', bytes: 1, sha256: 'a'.repeat(64) }] }), /no role/);
refuses('a tool with no pin refuses',
  () => N.parseToolRoles({ tools: [{ role: 'g++', path: '/a', bytes: 1 }] }), /no pin/);
refuses('an empty tools array refuses', () => N.parseToolRoles({ tools: [] }), /no tools/);
refuses('a relative tool path refuses',
  () => N.parseToolRoles({ tools: [{ role: 'g++', path: 'usr/bin/g++', bytes: 1, sha256: 'a'.repeat(64) }] }),
  /absolute Linux path/);

// ================================================================ 4. EXEC sets are file-level and exact
const ctx = { runtime_root: '/var/tmp/x/runtime', test_elf_linux: '/var/tmp/x/test.elf' };
for (const [kind, expectCount] of [['python', 2], ['cxx', 5], ['as', 3], ['ld', 3], ['qemu', 4], ['hostunit', 3]]) {
  const set = N.execSetFor(kind, authority, roles, ctx);
  check(`the ${kind} EXEC set has ${expectCount} distinct absolute files`,
    set.length === expectCount && new Set(set).size === expectCount
    && set.every((f) => f.startsWith('/') && !f.endsWith('/')), set.length);
}
check('the cxx set carries g++, cc1plus and as',
  N.execSetFor('cxx', authority, roles, ctx).includes('/usr/lib/gcc/x86_64-linux-gnu/9/cc1plus'), true);
check('the qemu set carries the staged bundled loader and the real QEMU',
  N.execSetFor('qemu', authority, roles, ctx)[2] === '/var/tmp/x/runtime/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'
  && N.execSetFor('qemu', authority, roles, ctx)[3] === '/var/tmp/x/runtime/root/usr/bin/qemu-system-i386', true);
refuses('a host unit without a gated test ELF refuses',
  () => N.execSetFor('hostunit', authority, roles, { runtime_root: '/var/tmp/x' }), /gated test ELF/);
refuses('an unknown execution kind refuses',
  () => N.execSetFor('shell', authority, roles, ctx), /unknown execution kind/);

// ================================================================ 5. control shape
// 109 root cause 2: a control key named require shadowed the helper's assertion function.
refuses('a top-level control key named require refuses',
  () => N.assertNoShadowing({ files: [], require: 'fresh wave' }), /would shadow/);
refuses('a nested control key named require refuses',
  () => N.assertNoShadowing({ a: { b: { require: 1 } } }), /would shadow/);
refuses('a shadowing key inside an array refuses',
  () => N.assertNoShadowing({ list: [{ digest: 'x' }] }), /would shadow/);
check('an ordinary control passes the shadow check',
  N.assertNoShadowing({ files: [], precondition: 'fresh wave', note: 'ok' }) !== undefined, true);

// 109 root cause 3: only the top-level argv was converted, so nested F:/ paths reached Python.
const hostControl = {
  task: 'GTOS-DSH-110',
  source_root: '/var/tmp/gtos-dsh-110-e759-boot-view',
  files: [
    { logical: 'src/kernel.cpp', source: 'F:/GTOS-Chromium/artifacts/x/src/kernel.cpp',
      bytes: 12, sha256: 'a'.repeat(64) },
    { logical: 'include/a.h', source: 'F:\\GTOS-Chromium\\artifacts\\x\\include\\a.h',
      bytes: 8, sha256: 'b'.repeat(64) },
  ],
  nested: { deeper: { report_path: 'F:/GTOS-Chromium/artifacts/x/out.json' } },
};
const linuxControl = N.buildLinuxControl(hostControl);
check('the top-level host paths become Linux paths',
  linuxControl.files[0].source === '/mnt/f/GTOS-Chromium/artifacts/x/src/kernel.cpp',
  linuxControl.files[0].source);
check('backslash host paths inside the control also become Linux paths',
  linuxControl.files[1].source === '/mnt/f/GTOS-Chromium/artifacts/x/include/a.h',
  linuxControl.files[1].source);
check('a deeply nested host path becomes a Linux path',
  linuxControl.nested.deeper.report_path === '/mnt/f/GTOS-Chromium/artifacts/x/out.json',
  linuxControl.nested.deeper.report_path);
check('pins and logical identities are untouched',
  linuxControl.files[0].sha256 === 'a'.repeat(64) && linuxControl.files[0].logical === 'src/kernel.cpp', true);
check('the Linux control carries no Windows path anywhere',
  !JSON.stringify(linuxControl).includes('F:/') && !JSON.stringify(linuxControl).includes('F:\\\\'), true);
refuses('a control that still carries a Windows path refuses',
  () => N.assertNoHostPaths({ a: { b: 'F:/still/here' } }), /still carries a Windows path/);
refuses('buildLinuxControl refuses a shadowing key before converting',
  () => N.buildLinuxControl({ files: [], require: 'x' }), /would shadow/);

// ================================================================ 6. wave ownership
// 109 root cause 4: setup pre-created wave-01, so the fresh-wave precondition failed on the first real attempt.
check('a wave id is not a setup container',
  N.isSetupContainer('wave-01') === false && N.isSetupContainer('logs') === true, true);
check('the setup container list contains no wave',
  N.SETUP_CONTAINERS.every((c) => !/^wave-[0-9]{2}$/.test(c)), N.SETUP_CONTAINERS);
const waveCtx = { source_root: '/var/tmp/gtos-dsh-110-test-wave' };
const waveHost = '//wsl$/Ubuntu-20.04' + waveCtx.source_root;
fs.mkdirSync(waveHost, { recursive: true });
refuses('a malformed wave id refuses', () => N.claimWave(waveCtx, 'wave-1'), /wave-NN/);
const first = N.claimWave(waveCtx, 'wave-01');
check('a fresh wave is created exclusively',
  first.created === true && first.pre_existing === false && fs.existsSync(waveHost + '/wave-01'), first.linux);
fs.writeFileSync(waveHost + '/wave-01/marker.txt', 'kept\n');
const markerBefore = fs.readFileSync(waveHost + '/wave-01/marker.txt');
refuses('an existing wave refuses', () => N.claimWave(waveCtx, 'wave-01'), /already exists/);
check('the refused wave keeps its content byte for byte',
  fs.readFileSync(waveHost + '/wave-01/marker.txt').equals(markerBefore), true);
const second = N.claimWave(waveCtx, 'wave-02');
check('a different fresh wave succeeds while the first is occupied',
  second.created === true && fs.existsSync(waveHost + '/wave-02'), second.linux);

// ================================================================ 7. subject pinning
const subjectPin = N.subjectPin(subjectPath);
check('the subject pin matches the module actually imported',
  subjectPin.sha256 === subjectBefore.sha256 && subjectPin.bytes === subjectBefore.bytes,
  subjectPin.sha256.slice(0, 16));
check('an unchanged subject passes the posthash', N.assertSubjectUnchanged(subjectPin).unchanged === true, true);
const tampered = { ...subjectPin, sha256: 'f'.repeat(64) };
refuses('a changed subject is caught', () => N.assertSubjectUnchanged(tampered), /changed during the call/);

// ================================================================ 8. the 109 assembly failures, as regressions
// Each of these is the exact shape that failed in 109, expressed against the current module.
check('regression: the 109 loader-key bug cannot recur because roles come from tools[]',
  (() => { try { N.requireRole(N.parseToolRoles({ tools: [] }), 'loader'); return false; }
    catch { return true; } })(), true);
check('regression: the 109 nested-path bug cannot recur because conversion is recursive',
  N.buildLinuxControl({ a: [{ b: 'F:/x/y' }] }).a[0].b === '/mnt/f/x/y', true);
check('regression: the 109 shadow-key bug cannot recur because keys are rejected',
  (() => { try { N.buildLinuxControl({ require: 1 }); return false; } catch { return true; } })(), true);
check('regression: the 109 pre-created-wave bug cannot recur because setup never creates one',
  !N.isSetupContainer('wave-01'), true);

// ================================================================ 9. determinism
const a1 = JSON.stringify(N.buildLinuxControl(hostControl));
const a2 = JSON.stringify(N.buildLinuxControl(hostControl));
check('the same input produces byte-identical controls', a1 === a2, a1.length);

const summary = {
  task: 'GTOS-DSH-110', kind: 'a0_normalizer_unit',
  subject: subjectBefore,
  subject_after: { path: subjectPath, bytes: fs.statSync(subjectPath).size,
    sha256: sha(fs.readFileSync(subjectPath)) },
  subject_unchanged: sha(fs.readFileSync(subjectPath)) === subjectBefore.sha256,
  checks: results.length, failed, results,
  ok: failed === 0,
};
fs.writeFileSync(control.result_path, JSON.stringify(summary, null, 2) + '\n',
  { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: summary.ok, checks: summary.checks, failed,
  subject: summary.subject.sha256.slice(0, 16) }));
if (failed > 0) process.exitCode = 1;
