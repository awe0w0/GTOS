// GTOS-DSH-139 T: the ten roles are read, and the whole set is mandatory.
//
// The two defects this file exists to catch are different in kind, and both were found by review rather than by the 138
// tests, which passed.
//
// The first is a bypass. `admitScene` checked the prerequisite set only when the request carried the property at all, so a
// request that omitted it was admitted, and `scene-admission.test.mjs` asserted that behaviour as correct. The fix makes the
// property mandatory and refuses its five spellings of absence — missing, undefined, null, empty, not-a-list — each with the
// obligation named. The tests below call the real entry and require the first refusal to be about the prerequisites, so a
// refusal that arrives later for some other reason cannot be mistaken for this one.
//
// The second is a substitution. The 138 role checks read the record's own fields, so a record could carry `all_passed: true`,
// `slots_read: 61` and an empty pin object and be admitted. Nothing was opened. The 139 consumer opens the artifacts: it
// re-reads every pin from disk, recomputes every count from the members, finds the multiboot header in the image bytes, and
// resolves every depfile's sources. The regression suite at the bottom feeds it the ten hand-written green records that 138
// accepted and requires every one of them to be refused.
//
// What this file does not claim: that a real guest ran, or that the production chain produced these bytes. The substrate is
// built by tests/prerequisite-substrate.mjs from deterministic content, and the consumer's job is to read whatever is on
// disk. A separate test in the drive verifies that the production chain's own records pass the same consumer.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as SA from '../tools/scene-admission.mjs';
import * as PC from '../tools/prerequisite-consumption.mjs';
import { buildSubstrate, membersFrom } from './prerequisite-substrate.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-mandatory-evidence-hardware-recovery-and-portable-closure-20261027-a';
const OUT = process.argv[3] || null;
// A fresh fixture root per run. The substrate refuses to build into an existing directory, so a second run cannot
// silently reuse the first run's bytes.
const FROOT = path.join(R, 'test-only', 'prereq-' + process.pid + '-' + Date.now());
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const F = buildSubstrate(FROOT);
const members = membersFrom(F);
const runnerTool = F.tools.find((t) => t.role === 'runner');
const CTX = { build_started_at: F.buildStartedAt, runner_path: runnerTool.path };

// A request the scene gate can be asked about: a declared scene, the right core count, and the selected identity the
// scene needs. Only `prerequisites` varies between cases, so a refusal about prerequisites cannot be confused with a
// refusal about anything else.
const identity = {
  kind: 'selected_image_identity', identity: 'sel-139',
  image: { linux: '/mnt/f/x/image/default.elf', bytes: F.elf.bytes, sha256: F.elf.sha256 },
  map: { linux: '/mnt/f/x/image/default.map', bytes: F.map.bytes, sha256: F.map.sha256 },
};
function request(overrides) {
  return {
    scene: 'runtime-init-smp1',
    identity,
    image: identity.image.linux,
    map: identity.map.linux,
    prerequisites: members,
    prerequisite_context: CTX,
    ...overrides,
  };
}
// The first refusal of a request, or null. Used so a case states which obligation refused first rather than only that
// something did.
function firstRefusalOf(req) {
  const r = SA.admitScene(req);
  return { admitted: r.admitted, first: r.first_refusal, problems: r.problems };
}

// ---------------------------------------------------------------- 1. the bypass, closed
{
  // The exact defect: no `prerequisites` property at all.
  const r = firstRefusalOf(request({ prerequisites: undefined }));
  // `prerequisites: undefined` still creates the property, so this case is about the value rather than the absence; the
  // absence case follows and is the one 138 admitted.
  check('a request whose prerequisite set is undefined is refused',
    r.admitted === false && r.first && r.first.obligation === 'prerequisite', r.first);

  const bare = { scene: 'runtime-init-smp1', identity, image: identity.image.linux, map: identity.map.linux };
  const r2 = firstRefusalOf(bare);
  check('a request that omits the prerequisite property entirely is refused',
    r2.admitted === false && r2.first && r2.first.obligation === 'prerequisite'
    && /carries no prerequisite set/.test(String(r2.first.why)), r2.first);

  const r3 = firstRefusalOf(request({ prerequisites: null }));
  check('a null prerequisite set is refused',
    r3.admitted === false && r3.first && r3.first.obligation === 'prerequisite'
    && /null/.test(String(r3.first.why)), r3.first);

  const r4 = firstRefusalOf(request({ prerequisites: [] }));
  check('an empty prerequisite set is refused',
    r4.admitted === false && r4.first && r4.first.obligation === 'prerequisite'
    && /empty/.test(String(r4.first.why)), r4.first);

  const r5 = firstRefusalOf(request({ prerequisites: 'ten roles' }));
  check('a prerequisite set that is not a list is refused',
    r5.admitted === false && r5.first && r5.first.obligation === 'prerequisite'
    && /not a list/.test(String(r5.first.why)), r5.first);

  const r6 = firstRefusalOf(request({ prerequisites: {} }));
  check('a prerequisite set given as an object is refused',
    r6.admitted === false && r6.first && r6.first.obligation === 'prerequisite', r6.first);
}

// ---------------------------------------------------------------- 2. the complete real set passes
{
  const r = SA.admitScene(request({}));
  check('the complete current set is admitted', r.admitted === true, r.problems);
  check('the admitted scene carries the command line its declaration derives',
    r.admitted === true && r.cmdline === 'serial bootdiag-stop=runtime-init', String(r.cmdline));
  const set = SA.admitPrerequisiteSet(members, CTX);
  check('all ten roles are read and the set is bound to one contract, image and subject',
    set.complete === true && set.roles.length === 10 && set.binding.bound === true,
    { roles: set.roles.length, binding: set.binding && { bound: set.binding.bound } });
  // Each role reports a fact it established, and the facts are distinct: the count of distinct detail keys is the evidence
  // that the ten roles are ten facts rather than one fact asked ten times.
  const details = set.roles.map((x) => JSON.stringify(x.fact && x.fact.detail));
  check('each of the ten roles reports a distinct fact', new Set(details).size === 10, new Set(details).size);
}

// ---------------------------------------------------------------- 3. missing, duplicate and wrong-typed members
{
  const drop = (role) => members.filter((m) => m.role !== role);
  for (const role of SA.REQUIRED_ROLES) {
    const set = SA.admitPrerequisiteSet(drop(role), CTX);
    check('omitting the ' + role + ' role is refused by name',
      set.complete === false && set.problems.some((p) => p.why === 'the required role is omitted: ' + role),
      set.problems.map((p) => p.why).slice(0, 2));
  }
  const dup = [...members, members[0]];
  const set = SA.admitPrerequisiteSet(dup, CTX);
  check('a role that appears twice is refused',
    set.complete === false && set.problems.some((p) => /appears more than once/.test(p.why)),
    set.problems.map((p) => p.why).slice(0, 2));

  const foreign = [...members.slice(1), { role: 'something_else', path: F.roleFiles.static_image.path }];
  const set2 = SA.admitPrerequisiteSet(foreign, CTX);
  check('a role this packet does not declare is refused',
    set2.complete === false && set2.problems.some((p) => /does not declare/.test(p.why)),
    set2.problems.map((p) => p.why).slice(0, 2));

  // The wrong-kind case needs the record's own hash to be honest, or the "changed after it was read" check fires first and
  // the case never reaches the kind check. Both refusals are correct; this one is about the kind.
  const wrongType = members.map((m) => (m.role === 'static_image'
    ? { role: 'static_image', path: F.roleFiles.default_link.path, sha256: F.roleFiles.default_link.sha256 } : m));
  const set3 = SA.admitPrerequisiteSet(wrongType, CTX);
  check('a record filed under the wrong role is refused as the wrong kind',
    set3.complete === false && set3.problems.some((p) => /wrong kind/.test(p.why)),
    set3.problems.map((p) => p.why).slice(0, 2));
}

// ---------------------------------------------------------------- 4. the lower layer cannot be reached around the gate
{
  // An object shaped exactly like an admission is not one. The WeakSet is module-private, so the argv builder refuses it.
  const forged = { admitted: true, scene: SA.scene('runtime-init-smp1'),
    cmdline: SA.commandLine(SA.scene('runtime-init-smp1')), vcpus: 1, problems: [], first_refusal: null };
  let threw = null;
  try { SA.sceneArgv(forged, { identity, logs: { e9: '/x/e9', serial: '/x/s' }, limit: 60 }); } catch (e) { threw = e; }
  check('an object that merely says admitted has no argv', threw !== null && /did not come from admitScene/.test(threw.message),
    threw && threw.message);

  let threw2 = null;
  try { SA.assertAdmittedScene(forged); } catch (e) { threw2 = e; }
  check('an object that merely says admitted cannot start a machine',
    threw2 !== null && /did not come from admitScene/.test(threw2.message), threw2 && threw2.message);

  // The refused request produces no admission, so nothing downstream can be derived from it.
  const refused = SA.admitScene({ scene: 'runtime-init-smp1', identity, image: identity.image.linux, map: identity.map.linux });
  check('a refused request yields no admitted object at all', refused.admitted === false);
  let threw3 = null;
  try { SA.sceneArgv(refused, { identity, logs: { e9: '/x/e9', serial: '/x/s' }, limit: 60 }); } catch (e) { threw3 = e; }
  check('a refused request cannot be turned into an argv', threw3 !== null, threw3 && threw3.message);
}

// ---------------------------------------------------------------- 5. the 138 regression: ten green records
//
// These are the records packet 138 accepted. Each is hand-written, each is green, and each names nothing that can be opened.
// They are reproduced here as a regression suite: if the 139 consumer ever admits one of them again, the substitution has
// come back.
const GREEN_138 = {
  source_profile_contract: {
    task: 'GTOS-DSH-139', kind: 'producer_closure_contract', sources: [], headers: [], order: [],
    order_from: { sha256: '' }, frozen_before_build: true,
  },
  producer_chain: { task: 'GTOS-DSH-139', kind: 'producer_closure', individuals: 61, provenance: [], rebuilt: 61, forwarded: 0 },
  dependency_chain: { task: 'GTOS-DSH-139', kind: 'link_input_read', slots_read: 61, slots_expected: 61,
    all_present: true, all_relocatable_i386: true, summary: { depfiles_present: 61, depfiles_with_missing_sources: [] } },
  link_input_consumption: { task: 'GTOS-DSH-139', kind: 'entry_link',
    record: { read_set: { roles: ['spec', 'terminal', 'helper_report', 'bridge', 'inner_report', 'raw'], pins: {} },
      elf: {}, map: {} } },
  typed_terminal_validation: { task: 'GTOS-DSH-139', kind: 'link_gate', ok: true,
    cases: [{ admitted: true }], image_exists: true, link_exit: 0 },
  default_link: { task: 'GTOS-DSH-139', kind: 'default_link', image_exists: true, link_exit: 0, elf: 'image.elf', individuals: 61 },
  static_image: { task: 'GTOS-DSH-139', kind: 'static_image_inspection', elf_type: 2, machine: 3,
    interp_present: false, dynamic_present: false, multiboot: { offset: 0, flags: 7 }, inspection_passed: true,
    elf: { sha256: 'a'.repeat(64) }, entry: '0x100000' },
  related_host_tests: { task: 'GTOS-DSH-139', kind: 'host_unit_execution',
    summary: { all_passed: true, safe_tss_cases_named: 16, runtime_cases_passed: '10/10' } },
  tooling_subject: { task: 'GTOS-DSH-139', kind: 'tool_pin',
    subjects: ['runner', 'guard', 'compiler', 'assembler', 'linker', 'emulator', 'loader']
      .map((role) => ({ role, path: '/p/' + role, bytes: 1, sha256: 'b'.repeat(64) })) },
  runtime_environment: { task: 'GTOS-DSH-139', kind: 'runtime_materialization',
    all_match: true, files: 206, bytes: 94050160 },
};
{
  // Written as real files so the gate reads them the same way it reads any other member: the refusal has to be about the
  // record's content, not about a path that does not exist.
  const greenDir = path.join(FROOT, 'green-138');
  fs.mkdirSync(greenDir, { recursive: true });
  const greenMembers = [];
  for (const [role, rec] of Object.entries(GREEN_138)) {
    const p = path.join(greenDir, role + '.json');
    const b = Buffer.from(JSON.stringify(rec, null, 2) + '\n', 'utf8');
    fs.writeFileSync(p, b, { flag: 'wx' });
    greenMembers.push({ role, path: p, sha256: sha(b) });
  }
  const set = SA.admitPrerequisiteSet(greenMembers, CTX);
  check('the ten hand-written green records of 138 are all refused', set.complete === false, set.complete);
  check('every one of the ten green roles is refused on its own fact',
    set.roles.length === 10 && set.roles.every((r) => r.fact && r.fact.ok === false),
    set.roles.filter((r) => !(r.fact && r.fact.ok === false)).map((r) => r.role));
  // And the gate refuses the whole request, not just the set: a green set is not a licence to launch.
  const r = firstRefusalOf(request({ prerequisites: greenMembers }));
  check('a request carrying the green records is refused at the entry',
    r.admitted === false && r.first && r.first.obligation === 'prerequisite', r.first);
}

// ---------------------------------------------------------------- 6. one obligation broken at a time
//
// Each case takes the real positive substrate, breaks exactly one obligation, and requires the refusal to be about that
// obligation. The other fields stay valid, so a refusal that fires for an unrelated reason is visible as a mismatch.
function withRecord(role, mutate) {
  const rec = JSON.parse(JSON.stringify(F.records[role]));
  mutate(rec);
  const p = path.join(FROOT, 'broken', role + '-' + Math.random().toString(36).slice(2, 8) + '.json');
  fs.mkdirSync(path.dirname(p), { recursive: true });
  const b = Buffer.from(JSON.stringify(rec, null, 2) + '\n', 'utf8');
  fs.writeFileSync(p, b, { flag: 'wx' });
  const out = members.map((m) => (m.role === role ? { role, path: p, sha256: sha(b) } : m));
  return SA.admitPrerequisiteSet(out, CTX);
}
function refusedFor(set, pattern, what) {
  const hit = set.problems.find((p) => pattern.test(String(p.why)));
  check(what, set.complete === false && !!hit, set.problems.map((p) => p.why).slice(0, 3));
}

{
  // -- an empty pin object is not a read.
  let s = withRecord('link_input_consumption', (r) => {
    r.record.read_set.pins = { spec: {}, terminal: {}, helper_report: {}, bridge: {}, inner_report: {}, raw: {} };
  });
  refusedFor(s, /pins no path/, 'an empty pin object is refused: a pin without a path is not a read');

  // -- a source that does not exist.
  s = withRecord('source_profile_contract', (r) => { r.sources[0].path = path.join(FROOT, 'nowhere', 'gone.cpp'); });
  refusedFor(s, /not on disk/, 'a contract naming a source that does not exist is refused');

  // -- a tool that does not exist.
  s = withRecord('tooling_subject', (r) => { r.subjects[2].path = path.join(FROOT, 'nowhere', 'g++'); });
  refusedFor(s, /not on disk/, 'a tooling subject that does not exist is refused');

  // -- same size, different bytes. The pin is the same length, so a size-only check would pass it.
  const driftTarget = F.roleFiles.dependency_chain.path;
  const driftBytes = fs.readFileSync(driftTarget);
  s = (() => {
    // Break a produced object's bytes without changing its length.
    const objPath = F.slots[7].object.path;
    const b = fs.readFileSync(objPath);
    b[0] = b[0] ^ 0xFF;
    fs.writeFileSync(objPath, b);
    return SA.admitPrerequisiteSet(members, CTX);
  })();
  refusedFor(s, /not the bytes/, 'a same-size byte change in a produced object is refused');
  // Restore, so the later cases run against the real positive chain.
  {
    const objPath = F.slots[7].object.path;
    const b = fs.readFileSync(objPath);
    b[0] = b[0] ^ 0xFF;
    fs.writeFileSync(objPath, b);
  }
  check('the restored object makes the set complete again',
    SA.admitPrerequisiteSet(members, CTX).complete === true);

  // -- the record changed after the caller read it. The edit has to change the bytes, or there is nothing to notice.
  s = (() => {
    const rec = JSON.parse(JSON.stringify(F.records.default_link));
    rec.link_exit = 0;
    rec.edited_after_the_caller_read_it = true;
    const p = F.roleFiles.default_link.path;
    fs.writeFileSync(p, JSON.stringify(rec, null, 2) + '\n');
    // The caller still holds the old hash, which is now wrong.
    return SA.admitPrerequisiteSet(members, CTX);
  })();
  refusedFor(s, /changed after it was read/, 'a record edited after the caller read it is refused');
  // Put it back, so the later cases read the real positive chain.
  fs.writeFileSync(F.roleFiles.default_link.path, JSON.stringify(F.records.default_link, null, 2) + '\n');

  // -- the contract was frozen after the build began. The relation is the check, not the boolean.
  s = (() => {
    const earlier = '2026-10-27T00:58:00.000Z';
    return SA.admitPrerequisiteSet(members, { build_started_at: earlier, runner_path: runnerTool.path });
  })();
  refusedFor(s, /frozen after the build began/, 'a contract frozen after the build began is refused');

  // -- no build event at all: the relation cannot be checked, so the claim cannot be made.
  s = SA.admitPrerequisiteSet(members, { runner_path: runnerTool.path });
  refusedFor(s, /no build event/, 'a claim of "frozen before build" with no build event is refused');

  // -- the runner pin is not the runner that is running.
  s = SA.admitPrerequisiteSet(members, { build_started_at: F.buildStartedAt, runner_path: F.tools[1].path });
  refusedFor(s, /runner pin is not the runner that is running/, 'a runner pin that is not the running runner is refused');

  // -- a static record pointing at a file that is not a multiboot image. Two refusals are correct here: the file is not
  // the image the rest of the set names, and the file carries no multiboot header. Either is a refusal, and both are
  // recorded so the case says which one fired.
  s = withRecord('static_image', (r) => { r.elf = F.hostElf; });
  {
    const hit = s.problems.find((p) => /different image_sha256|no multiboot header/.test(String(p.why)));
    check('a static record describing another image is refused', s.complete === false && !!hit,
      s.problems.map((p) => p.why).slice(0, 3));
  }

  // -- and a static record pointing at a *valid* second image: nothing is wrong with the file, but it is not the image the
  // rest of the set names, so the binding refuses it.
  s = (() => {
    const p = path.join(FROOT, 'image', 'second.elf');
    const b = Buffer.from(fs.readFileSync(F.elf.path));
    b.writeUInt32LE(0x41414141, 4096);
    fs.writeFileSync(p, b, { flag: 'wx' });
    const pin = { path: p, bytes: b.length, sha256: sha(b) };
    const rec = JSON.parse(JSON.stringify(F.records.static_image));
    rec.elf = pin;
    const rp = path.join(FROOT, 'broken', 'static-second.json');
    fs.writeFileSync(rp, JSON.stringify(rec, null, 2) + '\n', { flag: 'wx' });
    const out = members.map((m) => (m.role === 'static_image'
      ? { role: 'static_image', path: rp, sha256: sha(fs.readFileSync(rp)) } : m));
    return SA.admitPrerequisiteSet(out, CTX);
  })();
  refusedFor(s, /different image_sha256/, 'a valid but different image in the set is refused by the binding');

  // -- a different contract generation: one role names a different contract, so the set is not one set.
  s = withRecord('producer_chain', (r) => { r.contract_sha256 = 'f'.repeat(64); });
  refusedFor(s, /different contract_sha256/, 'a set whose members name different contracts is refused');

  // -- a different image generation: one role names a different image.
  s = withRecord('default_link', (r) => { r.elf = F.hostElf2; });
  refusedFor(s, /not the bytes it recorded|different image_sha256/, 'a set whose members name different images is refused');

  // -- the multiboot header is not in the image, whatever the record says about itself.
  s = (() => {
    const p = path.join(FROOT, 'image', 'nomultiboot.elf');
    const b = Buffer.from(fs.readFileSync(F.elf.path));
    b.writeUInt32LE(0, F.multibootOffset);
    fs.writeFileSync(p, b, { flag: 'wx' });
    const pin = { path: p, bytes: b.length, sha256: sha(b) };
    const rec = JSON.parse(JSON.stringify(F.records.static_image));
    rec.elf = pin;
    const rp = path.join(FROOT, 'broken', 'static-nomb.json');
    fs.writeFileSync(rp, JSON.stringify(rec, null, 2) + '\n', { flag: 'wx' });
    const out = members.map((m) => (m.role === 'static_image' ? { role: 'static_image', path: rp, sha256: sha(fs.readFileSync(rp)) } : m));
    return SA.admitPrerequisiteSet(out, CTX);
  })();
  refusedFor(s, /no multiboot header/, 'an image with no multiboot header is refused even when the record claims one');

  // -- the host unit exited non-zero: a host unit is not a guest, so a non-zero exit is a failure.
  s = withRecord('related_host_tests', (r) => { r.units[0].exit_code = 1; });
  refusedFor(s, /exited 1/, 'a host unit that exited non-zero is refused');

  // -- the summary is greener than the units under it.
  s = withRecord('related_host_tests', (r) => { r.summary.safe_tss_cases_named = 99; });
  refusedFor(s, /more named cases than the units carry/, 'a summary claiming more cases than the units carry is refused');

  // -- the runtime counts are recomputed, not read off the record.
  s = withRecord('runtime_environment', (r) => { r.files = 206; r.bytes = 94050160; });
  refusedFor(s, /runtime files|runtime bytes/, 'a runtime record whose counts do not match its manifest is refused');

  // -- a depfile naming a source that is not on disk.
  const depOriginal = fs.readFileSync(F.slots[3].depfile.path);
  s = (() => {
    const depPath = F.slots[3].depfile.path;
    const text = 'obj/slot04.o: ' + path.join(FROOT, 'nowhere', 'missing.cpp') + '\n';
    fs.writeFileSync(depPath, text);
    const rec = JSON.parse(JSON.stringify(F.records.dependency_chain));
    rec.inputs[3].depfile = { path: depPath, bytes: Buffer.byteLength(text), sha256: sha(Buffer.from(text)) };
    const rp = path.join(FROOT, 'broken', 'dep-missing.json');
    fs.writeFileSync(rp, JSON.stringify(rec, null, 2) + '\n', { flag: 'wx' });
    const out = members.map((m) => (m.role === 'dependency_chain' ? { role: 'dependency_chain', path: rp, sha256: sha(fs.readFileSync(rp)) } : m));
    return SA.admitPrerequisiteSet(out, CTX);
  })();
  refusedFor(s, /source that is not on disk/, 'a depfile naming a source that is not on disk is refused');
  // Restore the depfile, so the final case reads the real positive chain rather than the broken one.
  fs.writeFileSync(F.slots[3].depfile.path, depOriginal);

  // -- a producer closure that forwarded everything proves nothing about the current sources.
  s = withRecord('producer_chain', (r) => { r.rebuilt = 0; r.forwarded = 61; });
  refusedFor(s, /rebuilt nothing/, 'a closure that rebuilt nothing is refused');
}

// ---------------------------------------------------------------- 7. the entry stays usable after the refusals
{
  const r = SA.admitScene(request({}));
  check('the real entry still admits the complete set after every refusal above', r.admitted === true, r.problems);
  check('and the admitted object still derives the scene command line', r.cmdline === 'serial bootdiag-stop=runtime-init');
}

// ---------------------------------------------------------------- report
const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-139', kind: 'prerequisite_consumption_test',
  subject: 'tools/prerequisite-consumption.mjs + tools/scene-admission.mjs',
  fixture_root: FROOT,
  roles: Object.keys(F.roleFiles).length, slots: F.slots.length,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
  not_claimed: ['that a real guest ran', 'that these fixture bytes are the production chain',
    'that a passing unit test proves the kernel behaves'],
};
if (OUT) {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
