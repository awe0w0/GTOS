// GTOS-DSH-139 T: the prerequisite set is a set, and its members are read.
//
// This file is the companion to tests/prerequisite-consumption.test.mjs. That one is about what a single role consumer
// does with the artifacts it is handed; this one is about the set as a whole — which roles must be present, what makes a
// member wrong, and what it means for ten members to be one claim rather than ten green lights.
//
// 138 wrote the positive case here as ten hand-written JSON records: a source list naming `src/a.cpp`, a tool list naming
// `/p/g++`, `all_match: true`, `slots_read: 61`, and an empty pin object. Every one of those was a declaration, and the
// checks read the declaration. This file now builds its positive case from bytes: tests/prerequisite-substrate.mjs writes
// the files, hashes them after writing, and the members carry those hashes. The difference is the whole point — the old
// positive case would pass a gate that opened nothing, and this one cannot.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as SA from '../tools/scene-admission.mjs';
import { buildSubstrate, membersFrom } from './prerequisite-substrate.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-mandatory-evidence-hardware-recovery-and-portable-closure-20261027-a';
const OUT = process.argv[3] || null;
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const ROOT = path.join(R, 'test-only', 'prereq-set-' + process.pid + '-' + Date.now());
const F = buildSubstrate(ROOT);
const members = membersFrom(F);
const CTX = { build_started_at: F.buildStartedAt, runner_path: F.tools.find((t) => t.role === 'runner').path };

// Write a record into the fixture tree and return the member that names it, so a case can vary exactly one record while
// the other nine stay the real positive ones.
let counter = 0;
function memberFor(role, record) {
  counter += 1;
  const p = path.join(ROOT, 'variants', role + '-' + counter + '.json');
  fs.mkdirSync(path.dirname(p), { recursive: true });
  const b = Buffer.from(JSON.stringify(record, null, 2) + '\n', 'utf8');
  fs.writeFileSync(p, b, { flag: 'wx' });
  return { role, path: p, sha256: sha(b) };
}
function withRecord(role, mutate) {
  const rec = JSON.parse(JSON.stringify(F.records[role]));
  mutate(rec);
  return SA.admitPrerequisiteSet(members.map((m) => (m.role === role ? memberFor(role, rec) : m)), CTX);
}
function expectRefusal(name, set, pattern) {
  const hit = set.problems.find((p) => pattern.test(String(p.why)));
  check(name, set.complete === false && !!hit, set.problems.map((p) => p.why).slice(0, 3));
}

// ---------------------------------------------------------------- 1. a complete set of ten real records
{
  const set = SA.admitPrerequisiteSet(members, CTX);
  check('a complete set of ten real records is admitted', set.complete === true, set.problems);
  check('the set reports all ten roles', set.roles.length === 10, set.roles.length);
  check('the set is bound to one contract, one image and one tool generation',
    set.binding.bound === true && !!set.binding.contract && !!set.binding.image && !!set.binding.subject,
    { bound: set.binding.bound });
  // The facts are ten different facts. A gate that asked "did the JSON parse" ten times would report ten details that are
  // all the same shape; these are not.
  const details = set.roles.map((r) => JSON.stringify(r.fact && r.fact.detail));
  check('the ten roles report ten distinct facts', new Set(details).size === 10, new Set(details).size);
  check('every role reports a fact it established', set.roles.every((r) => r.fact && r.fact.ok === true),
    set.roles.filter((r) => !(r.fact && r.fact.ok)).map((r) => r.role));
  // And the binding is named by several roles rather than asserted once, which is what makes it a binding.
  check('the contract is named by more than one role', set.binding.named.contract > 1, set.binding.named);
  check('the image is named by more than one role', set.binding.named.image > 1, set.binding.named);
}

// ---------------------------------------------------------------- 2. every role is required
{
  for (const role of SA.REQUIRED_ROLES) {
    const set = SA.admitPrerequisiteSet(members.filter((m) => m.role !== role), CTX);
    check('omitting the ' + role + ' role is refused by name',
      set.complete === false && set.problems.some((p) => p.why === 'the required role is omitted: ' + role),
      set.problems.map((p) => p.why).slice(0, 2));
  }
  check('the required set is exactly the ten declared roles', SA.REQUIRED_ROLES.length === 10, SA.REQUIRED_ROLES.length);
  check('the required set is frozen', Object.isFrozen(SA.REQUIRED_ROLES) === true);
}

// ---------------------------------------------------------------- 3. each way a member can be wrong
{
  // Not an object.
  let set = SA.admitPrerequisiteSet([null, ...members.slice(1)], CTX);
  expectRefusal('a member that is not an object is refused', set, /not an object/);

  // No role name.
  set = SA.admitPrerequisiteSet([{ path: F.roleFiles.default_link.path }, ...members.slice(1)], CTX);
  expectRefusal('a member that names no role is refused', set, /names no role/);

  // A role this packet does not declare.
  set = SA.admitPrerequisiteSet([...members.slice(1), { role: 'not_a_role', path: F.roleFiles.static_image.path }], CTX);
  expectRefusal('a role this packet does not declare is refused', set, /does not declare/);

  // The same role twice.
  set = SA.admitPrerequisiteSet([...members, members[0]], CTX);
  expectRefusal('a role that appears twice is refused', set, /appears more than once/);

  // No path.
  set = SA.admitPrerequisiteSet(members.map((m) => (m.role === 'default_link' ? { role: 'default_link' } : m)), CTX);
  expectRefusal('a member with no path is refused', set, /has no path/);

  // A path that is not on disk.
  set = SA.admitPrerequisiteSet(members.map((m) => (m.role === 'default_link'
    ? { role: 'default_link', path: path.join(ROOT, 'nowhere', 'absent.json') } : m)), CTX);
  expectRefusal('a member whose file is not on disk is refused', set, /absent on disk/);

  // An empty file.
  const emptyPath = path.join(ROOT, 'variants', 'empty.json');
  fs.mkdirSync(path.dirname(emptyPath), { recursive: true });
  fs.writeFileSync(emptyPath, '', { flag: 'wx' });
  set = SA.admitPrerequisiteSet(members.map((m) => (m.role === 'default_link'
    ? { role: 'default_link', path: emptyPath, sha256: sha(Buffer.alloc(0)) } : m)), CTX);
  expectRefusal('a member whose record is empty is refused', set, /record is empty/);

  // Bytes that are not JSON.
  const notJsonPath = path.join(ROOT, 'variants', 'notjson.json');
  fs.writeFileSync(notJsonPath, 'this is not json at all\n', { flag: 'wx' });
  set = SA.admitPrerequisiteSet(members.map((m) => (m.role === 'default_link'
    ? { role: 'default_link', path: notJsonPath, sha256: sha(fs.readFileSync(notJsonPath)) } : m)), CTX);
  expectRefusal('a member whose record is not JSON is refused', set, /not JSON/);

  // The caller's hash does not match the bytes.
  set = SA.admitPrerequisiteSet(members.map((m) => (m.role === 'default_link'
    ? { ...m, sha256: 'c'.repeat(64) } : m)), CTX);
  expectRefusal('a member whose bytes changed after the caller read them is refused', set, /changed after it was read/);

  // The wrong kind of record for the role.
  set = SA.admitPrerequisiteSet(members.map((m) => (m.role === 'static_image'
    ? { role: 'static_image', path: F.roleFiles.default_link.path, sha256: F.roleFiles.default_link.sha256 } : m)), CTX);
  expectRefusal('a record of the wrong kind for its role is refused', set, /wrong kind/);

  // A record from another task.
  set = withRecord('default_link', (r) => { r.task = 'GTOS-DSH-138'; });
  expectRefusal('a record from another task is refused', set, /belongs to another task/);
}

// ---------------------------------------------------------------- 4. the 138 regression: ten green records
//
// The records 138 accepted. Reproduced so the regression is pinned in the file that used to hold them as the positive
// case: these are what "green" looked like when nothing was opened.
const GREEN_138 = {
  source_profile_contract: { kind: 'producer_closure_contract', sources: [], headers: [], order: [],
    order_from: { sha256: '' }, frozen_before_build: true },
  producer_chain: { kind: 'producer_closure', individuals: 61, provenance: [], rebuilt: 61, forwarded: 0 },
  dependency_chain: { kind: 'link_input_read', slots_read: 61, slots_expected: 61, all_present: true,
    all_relocatable_i386: true, summary: { depfiles_present: 61, depfiles_with_missing_sources: [] } },
  link_input_consumption: { kind: 'entry_link',
    record: { read_set: { roles: ['spec', 'terminal', 'helper_report', 'bridge', 'inner_report', 'raw'], pins: {} },
      elf: {}, map: {} } },
  typed_terminal_validation: { kind: 'link_gate', ok: true, cases: [{ admitted: true }], image_exists: true, link_exit: 0 },
  default_link: { kind: 'default_link', image_exists: true, link_exit: 0, elf: 'image.elf', individuals: 61 },
  static_image: { kind: 'static_image_inspection', elf_type: 2, machine: 3, interp_present: false,
    dynamic_present: false, multiboot: { offset: 0, flags: 7 }, inspection_passed: true,
    elf: { sha256: 'a'.repeat(64) }, entry: '0x100000' },
  related_host_tests: { kind: 'host_unit_execution',
    summary: { all_passed: true, safe_tss_cases_named: 16, runtime_cases_passed: '10/10' } },
  tooling_subject: { kind: 'tool_pin', subjects: ['runner', 'guard', 'compiler', 'assembler', 'linker', 'emulator',
    'loader'].map((role) => ({ role, path: '/p/' + role, bytes: 1, sha256: 'b'.repeat(64) })) },
  runtime_environment: { kind: 'runtime_materialization', all_match: true, files: 206, bytes: 94050160 },
};
{
  const greenMembers = Object.entries(GREEN_138).map(([role, rec]) =>
    memberFor(role, { task: 'GTOS-DSH-139', ...rec }));
  const set = SA.admitPrerequisiteSet(greenMembers, CTX);
  check('the ten hand-written green records of 138 are refused as a set', set.complete === false, set.complete);
  check('all ten green roles are refused on their own fact',
    set.roles.length === 10 && set.roles.every((r) => r.fact && r.fact.ok === false),
    set.roles.filter((r) => !(r.fact && r.fact.ok === false)).map((r) => r.role));
  // The refusal has to be about content, not about a missing or unreadable file: these records exist, they parse, and the
  // caller's hashes match them. So the assertion is the negative one — none of the ten may be refused for absence,
  // unreadability or drift. What is left is the consumer saying, in its own words, that the record does not show the fact.
  const structuralOnly = /absent on disk|not JSON|changed after it was read|record is empty|has no path/;
  const structural = set.roles.filter((r) => r.fact && structuralOnly.test(String(r.fact.why)));
  check('the green records are refused because they name nothing openable, not because they are absent',
    structural.length === 0, structural.map((r) => r.role + ': ' + r.fact.why));
  check('each green record is refused with a reason of its own',
    new Set(set.roles.map((r) => String(r.fact && r.fact.why))).size === 10,
    new Set(set.roles.map((r) => String(r.fact && r.fact.why))).size);
}

// ---------------------------------------------------------------- 5. the binding, broken one way at a time
{
  // One role names a different contract. Everything else is valid, so the refusal is the binding's.
  let set = withRecord('producer_chain', (r) => { r.contract_sha256 = 'f'.repeat(64); });
  expectRefusal('a set whose members name different contracts is refused', set, /different contract_sha256/);

  // One role names a different image.
  set = withRecord('default_link', (r) => { r.elf = F.hostElf2; });
  expectRefusal('a set whose members name different images is refused', set, /different image_sha256/);

  // One role names a different runner. The consumer refuses this on the runner comparison, which is the stricter and more
  // specific check, and that is the refusal recorded here.
  const altTool = path.join(ROOT, 'tools', 'other-runner.mjs');
  fs.mkdirSync(path.dirname(altTool), { recursive: true });
  fs.writeFileSync(altTool, 'different runner\n', { flag: 'wx' });
  set = withRecord('tooling_subject', (r) => {
    const i = r.subjects.findIndex((s) => s.role === 'runner');
    r.subjects[i] = { role: 'runner', path: altTool, bytes: fs.statSync(altTool).size,
      sha256: sha(fs.readFileSync(altTool)) };
  });
  expectRefusal('a set naming a different runner is refused', set, /runner pin is not the runner that is running/);

  // A set where nothing names the contract is not bound to one.
  set = SA.admitPrerequisiteSet(members.map((m) => {
    if (!['source_profile_contract', 'producer_chain', 'dependency_chain', 'link_input_consumption',
      'typed_terminal_validation', 'default_link', 'static_image', 'related_host_tests'].includes(m.role)) return m;
    const rec = JSON.parse(JSON.stringify(F.records[m.role]));
    delete rec.contract_sha256;
    return memberFor(m.role, rec);
  }), CTX);
  check('a set in which no role names the contract is refused', set.complete === false
    && set.problems.some((p) => /no role names the contract/.test(String(p.why))),
    set.problems.map((p) => p.why).slice(0, 3));
}

// ---------------------------------------------------------------- 6. the argv needs a real admission
{
  const forged = { admitted: true, scene: SA.scene('runtime-init-smp1'),
    cmdline: SA.commandLine(SA.scene('runtime-init-smp1')), vcpus: 1, problems: [], first_refusal: null };
  let threw = null;
  try { SA.sceneArgv(forged, { identity: { image: { linux: '/x' } }, logs: { e9: '/x', serial: '/y' }, limit: 60 }); }
  catch (e) { threw = e; }
  check('a forged admission has no argv', threw !== null && /did not come from admitScene/.test(threw.message),
    threw && threw.message);
}

// ---------------------------------------------------------------- report
const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-139', kind: 'scene_prerequisites_test',
  subject: 'tools/scene-admission.mjs + tools/prerequisite-consumption.mjs',
  fixture_root: ROOT,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
if (OUT) {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
