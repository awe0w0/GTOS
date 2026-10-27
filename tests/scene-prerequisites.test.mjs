// GTOS-DSH-138 S: the scene prerequisite set, and the argv that cannot be obtained without an admission.
//
// Before this packet, `admitScene` took a list of `{path, sha256, ok}` prerequisites. That shape has one defect: `ok` is
// the caller's own word. A caller that wrote `ok: true` next to a path satisfied the gate, and the ten things the gate is
// supposed to stand for — the contract, the producer closure, the dependency chain, the link input read, the typed
// terminal, the default link, the static image, the host tests, the tooling subject and the runtime environment — collapsed
// into one fact: "some JSON parsed".
//
// This test proves the replacement, in the order that matters:
//
//   1. a complete set of ten real records is admitted, and each role's own fact is read from the record, not from `ok`;
//   2. every role is required: omitting any one of the ten is refused, and the refusal names the role;
//   3. each way a member can be wrong is refused with its own obligation — absent, empty, drifted, not JSON, wrong kind,
//      another task's record, a duplicate, a role this packet does not declare, and a member that is only `ok: true`;
//   4. a record whose file changed after the caller read it is refused, so the gate reads the disk rather than the claim;
//   5. `sceneArgv` has no argv for an object that did not come from `admitScene`, so a hand-made `{admitted: true}` cannot
//      reach the emulator.
//
// The fixtures live under the packet's own test-only root and are written once. Nothing here reads or writes a business
// path, and nothing here launches anything.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as scene from '../tools/scene-admission.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-current-evidence-runtime-seam-closure-and-guest-matrix-20261027-a';
const TESTONLY = R + '/test-only/scene-prerequisites';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

fs.mkdirSync(TESTONLY, { recursive: true });

// A fixture is written once. A rerun that would change it is a refusal, not an overwrite: the case's meaning depends on
// the bytes it was written with.
function writeOnce(p, text) {
  const b = Buffer.from(text, 'utf8');
  if (fs.existsSync(p)) {
    const onDisk = fs.readFileSync(p);
    if (!onDisk.equals(b)) throw new Error('a test-only fixture changed under a fixed name: ' + p);
    return p;
  }
  fs.writeFileSync(p, b, { flag: 'wx' });
  return p;
}

// ---------------------------------------------------------------- 1. a complete set of ten real records
//
// Each fixture is the shape that role's own production record has. They are deliberately different from one another: if
// the gate were reading one generic field, one fixture would satisfy all ten.
const FIXTURES = {
  source_profile_contract: {
    kind: 'producer_closure_contract', task: 'GTOS-DSH-138', frozen_before_build: true,
    sources: [{ path: 'src/a.cpp', sha256: 'a'.repeat(64) }],
    order_from: { path: 'contract.json', sha256: 'b'.repeat(64) },
    order: ['a'], headers: ['include/a.h'],
  },
  producer_chain: {
    kind: 'producer_closure', task: 'GTOS-DSH-138', individuals: 2, rebuilt: 2, forwarded: 0,
    contract_sha256: 'c'.repeat(64),
    provenance: [{ slot: 1, token: 't1' }, { slot: 2, token: 't2' }],
  },
  dependency_chain: {
    kind: 'link_input_read', task: 'GTOS-DSH-138', slots_read: 61, slots_expected: 61,
    all_present: true, all_relocatable_i386: true,
    summary: { depfiles_present: 61, depfiles_with_missing_sources: [] },
  },
  link_input_consumption: {
    kind: 'entry_link', task: 'GTOS-DSH-138',
    record: {
      read_set: { roles: ['spec', 'terminal', 'helper_report', 'bridge', 'inner_report', 'raw'],
        pins: { spec: {}, terminal: {}, helper_report: {}, bridge: {}, inner_report: {}, raw: {} } },
      elf: { sha256: 'd'.repeat(64) }, map: { sha256: 'e'.repeat(64) },
    },
  },
  typed_terminal_validation: {
    kind: 'link_gate', task: 'GTOS-DSH-138', ok: true,
    cases: [{ name: 'baseline', admitted: true }, { name: 'stale', admitted: false }],
  },
  default_link: {
    kind: 'default_link', task: 'GTOS-DSH-138', image_exists: true, link_exit: 0,
    individuals: 61, elf: 'f'.repeat(64),
  },
  static_image: {
    kind: 'static_image_inspection', task: 'GTOS-DSH-138', elf_type: 2, machine: 3,
    interp_present: false, dynamic_present: false, inspection_passed: true, entry: 0x100000,
    multiboot: { offset: 0, flags: 7 }, script_symbols: ['start'],
    elf: { path: '/var/tmp/x.elf', bytes: 745748, sha256: '1'.repeat(64) },
  },
  related_host_tests: {
    kind: 'host_unit_execution', task: 'GTOS-DSH-138',
    summary: { all_passed: true, safe_tss_cases_named: 16, runtime_cases_passed: '10/10' },
  },
  tooling_subject: {
    kind: 'runner_pin', task: 'GTOS-DSH-138',
    subjects: [
      { role: 'runner', path: '/p/runner.mjs', bytes: 1, sha256: '2'.repeat(64) },
      { role: 'guard', path: '/p/guard.py', bytes: 1, sha256: '3'.repeat(64) },
      { role: 'compiler', path: '/p/g++', bytes: 1, sha256: '4'.repeat(64) },
      { role: 'assembler', path: '/p/as', bytes: 1, sha256: '5'.repeat(64) },
      { role: 'linker', path: '/p/ld.bfd', bytes: 1, sha256: '6'.repeat(64) },
      { role: 'emulator', path: '/p/qemu', bytes: 1, sha256: '7'.repeat(64) },
      { role: 'loader', path: '/p/ld-linux.so.2', bytes: 1, sha256: '8'.repeat(64) },
    ],
  },
  runtime_environment: {
    kind: 'runtime_materialization', task: 'GTOS-DSH-138', all_match: true,
    files: 206, bytes: 94050160,
  },
};

const paths = {};
for (const [role, record] of Object.entries(FIXTURES)) {
  paths[role] = writeOnce(TESTONLY + '/' + role + '.json', JSON.stringify(record, null, 2) + '\n');
}
const member = (role, overrides = {}) => ({
  role, path: paths[role], sha256: sha(fs.readFileSync(paths[role])), record: FIXTURES[role], ...overrides,
});
const fullSet = () => scene.REQUIRED_ROLES.map((role) => member(role));
// The set with one role's member removed, so a case can substitute its own record for that role instead of appending a
// second member for a role that is already present.
const without = (role) => fullSet().filter((m) => m.role !== role);

check('the packet declares ten roles', scene.REQUIRED_ROLES.length === 10, scene.REQUIRED_ROLES);
{
  const v = scene.admitPrerequisiteSet(fullSet());
  check('a complete set of ten real records is admitted', v.complete === true, v.problems);
  check('every role is reported back with the fact it was admitted on',
    v.roles.length === 10 && v.roles.every((r) => r.fact && r.fact.ok === true),
    v.roles.filter((r) => !r.fact || !r.fact.ok));
  // The ten facts are ten different facts, not one. Two roles whose detail is identical would mean the gate read the
  // same field twice and the set is smaller than it claims.
  const details = v.roles.map((r) => JSON.stringify(r.fact.detail));
  check('the ten roles are admitted on ten different facts', new Set(details).size === 10, details);
}

// ---------------------------------------------------------------- 2. every role is required
for (const role of scene.REQUIRED_ROLES) {
  const v = scene.admitPrerequisiteSet(fullSet().filter((m) => m.role !== role));
  check('omitting ' + role + ' is refused',
    v.complete === false && v.problems.some((p) => p.obligation === 'prerequisite'
      && p.why.includes(role)), v.problems);
}
{
  const v = scene.admitPrerequisiteSet([]);
  check('an empty set is refused once per role, not once overall',
    v.complete === false && v.problems.length === 10, v.problems.length);
}

// ---------------------------------------------------------------- 3. each way a member can be wrong
function expectRefusal(name, members, why) {
  const v = scene.admitPrerequisiteSet(members);
  const hit = v.problems.some((p) => p.obligation === 'prerequisite' && String(p.why).includes(why));
  check(name, v.complete === false && hit, { problems: v.problems });
}
expectRefusal('a member that is not an object',
  [...fullSet(), null], 'not an object');
expectRefusal('a member that names no role',
  [...fullSet(), { path: paths.static_image }], 'names no role');
expectRefusal('a role this packet does not declare',
  [...fullSet(), member('static_image')].map((m, i) => (i === 10 ? { ...m, role: 'invented_role' } : m)),
  'does not declare');
expectRefusal('the same role twice',
  [...fullSet(), member('static_image')], 'more than once');
expectRefusal('a role with no path',
  fullSet().map((m) => (m.role === 'producer_chain' ? { ...m, path: undefined } : m)), 'no path');
expectRefusal('a role whose file is absent',
  fullSet().map((m) => (m.role === 'producer_chain' ? { ...m, path: TESTONLY + '/not-here.json' } : m)),
  'absent on disk');
expectRefusal('a role record that is empty',
  [...without('runtime_environment'), { role: 'runtime_environment', path: writeOnce(TESTONLY + '/empty.json', '') }],
  'empty');
expectRefusal('a role record that is not JSON',
  [...without('runtime_environment'),
    { role: 'runtime_environment', path: writeOnce(TESTONLY + '/not-json.json', 'this is not json\n') }],
  'not JSON');
// A record whose bytes changed after the caller read it. The gate reads the disk, so the caller's hash is what is
// compared, and a mismatch means the file the caller described is not the file that is there now.
expectRefusal('a role record that changed after it was read',
  fullSet().map((m) => (m.role === 'dependency_chain' ? { ...m, sha256: 'f'.repeat(64) } : m)),
  'changed after it was read');
// The wrong-typed role: a link record filed where the runtime environment belongs. Both are valid JSON of a declared
// kind, so only a per-role kind check can tell them apart.
expectRefusal('a record of the wrong kind for its role',
  // The hash has to be the substituted file's own, or the case would be refused for drift and never reach the kind check.
  fullSet().map((m) => (m.role === 'runtime_environment'
    ? { ...m, path: paths.link_input_consumption, sha256: sha(fs.readFileSync(paths.link_input_consumption)) }
    : m)),
  'wrong kind');
expectRefusal('a record that belongs to another task',
  [...without('runtime_environment'),
    { role: 'runtime_environment', path: writeOnce(TESTONLY + '/other-task.json',
      JSON.stringify({ kind: 'runtime_materialization', task: 'GTOS-DSH-131', all_match: true,
        files: 206, bytes: 94050160 }, null, 2) + '\n') }],
  'another task');
// The defect this whole gate replaces: a caller's `ok` is not the fact. The record is the right kind and the right task
// and carries `ok: true`, and it still has to show the fact.
expectRefusal('a caller\'s ok alone does not satisfy a role',
  [...without('related_host_tests'),
    { role: 'related_host_tests', path: writeOnce(TESTONLY + '/ok-only.json',
      JSON.stringify({ kind: 'host_unit_execution', task: 'GTOS-DSH-138', ok: true }, null, 2) + '\n') }],
  'does not show the fact');
// And each role's fact is its own: a runtime record that is otherwise correct but is not the declared runtime is refused
// on the numbers, not on the kind.
expectRefusal('a runtime that is not the declared one',
  [...without('runtime_environment'),
    { role: 'runtime_environment', path: writeOnce(TESTONLY + '/runtime-wrong.json',
      JSON.stringify({ kind: 'runtime_materialization', task: 'GTOS-DSH-138', all_match: true,
        files: 205, bytes: 94050160 }, null, 2) + '\n') }],
  'not the declared');
// A gate that admitted its baseline and refused nothing would be a gate that cannot refuse.
expectRefusal('a link gate that admitted everything',
  [...without('typed_terminal_validation'),
    { role: 'typed_terminal_validation', path: writeOnce(TESTONLY + '/gate-all-admitted.json',
      JSON.stringify({ kind: 'link_gate', task: 'GTOS-DSH-138', ok: true,
        cases: [{ name: 'a', admitted: true }, { name: 'b', admitted: true }] }, null, 2) + '\n') }],
  'admitted more than the baseline');
// A tooling subject that names a path but no hash is a claim about a file that has since been free to change.
expectRefusal('a tooling subject with no hash',
  [...without('tooling_subject'),
    { role: 'tooling_subject', path: writeOnce(TESTONLY + '/tooling-no-hash.json',
      JSON.stringify({ kind: 'runner_pin', task: 'GTOS-DSH-138', subjects: [
        { role: 'runner', path: '/p/r.mjs', bytes: 1, sha256: '2'.repeat(64) },
        { role: 'guard', path: '/p/g.py', bytes: 1, sha256: '3'.repeat(64) },
        { role: 'compiler', path: '/p/g++', bytes: 1, sha256: '4'.repeat(64) },
        { role: 'assembler', path: '/p/as', bytes: 1, sha256: '5'.repeat(64) },
        { role: 'linker', path: '/p/ld.bfd', bytes: 1, sha256: '6'.repeat(64) },
        { role: 'emulator', path: '/p/qemu', bytes: 1, sha256: '7'.repeat(64) },
        { role: 'loader', path: '/p/ld-linux.so.2', bytes: 1 },
      ] }, null, 2) + '\n') }],
  'no usable hash');
// And a subject set that omits a capability is refused by name, so a run under an unnamed tool cannot pass.
expectRefusal('a tooling subject that omits the emulator',
  [...without('tooling_subject'),
    { role: 'tooling_subject', path: writeOnce(TESTONLY + '/tooling-no-emulator.json',
      JSON.stringify({ kind: 'runner_pin', task: 'GTOS-DSH-138', subjects: [
        { role: 'runner', path: '/p/r.mjs', bytes: 1, sha256: '2'.repeat(64) },
        { role: 'guard', path: '/p/g.py', bytes: 1, sha256: '3'.repeat(64) },
        { role: 'compiler', path: '/p/g++', bytes: 1, sha256: '4'.repeat(64) },
        { role: 'assembler', path: '/p/as', bytes: 1, sha256: '5'.repeat(64) },
        { role: 'linker', path: '/p/ld.bfd', bytes: 1, sha256: '6'.repeat(64) },
        { role: 'loader', path: '/p/ld-linux.so.2', bytes: 1, sha256: '8'.repeat(64) },
      ] }, null, 2) + '\n') }],
  'omits the emulator');

// ---------------------------------------------------------------- 4. the argv needs an admission
//
// The 138 defect: `sceneArgv` took any object with `admitted: true`. A caller that wrote that literal by hand got the
// argv, and the emulator launched a scene that had never been admitted. The gate is now the object's provenance, so the
// refusal is about where the object came from and not about a field it carries.
{
  let threw = null;
  try {
    scene.sceneArgv({ admitted: true, scene: 'runtime-init-smp1', vcpus: 1, commandLine: 'serial bootdiag-stop=runtime-init' },
      { identity: { image: { linux: '/var/tmp/x.elf', bytes: 1, sha256: 'a'.repeat(64) } },
        logs: { e9: '/tmp/e9.log', serial: '/tmp/serial.log' }, limit: 150 });
  } catch (e) { threw = e; }
  check('a hand-made admitted object has no argv',
    threw !== null && /SCENE/.test(String(threw.message)), threw ? threw.message : null);
}
{
  let threw = null;
  try { scene.sceneArgv(scene.admitScene({}), { logs: {}, identity: {} }); } catch (e) { threw = e; }
  check('a refused admission has no argv', threw !== null && /SCENE/.test(String(threw.message)),
    threw ? threw.message : null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-138', kind: 'scene_prerequisite_test',
  subject: 'tools/scene-admission.mjs',
  required_roles: scene.REQUIRED_ROLES,
  fixtures: Object.keys(FIXTURES).map((role) => ({ role, path: paths[role],
    bytes: fs.statSync(paths[role]).size })),
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
  note: 'these are the prerequisite and argv gates in one module, run against real files under this packet own '
    + 'test-only root. Nothing here launches an emulator, reads a business path, or stands for a guest run.',
};
const out = process.argv[2];
if (out && out !== '--') {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  if (fs.existsSync(out)) throw new Error('REFUSED exists: ' + out);
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed,
  failures: failures.map((f) => f.name) }));
process.exit(report.ok ? 0 : 1);
