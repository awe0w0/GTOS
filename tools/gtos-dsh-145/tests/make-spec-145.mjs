// GTOS-DSH-145: the payload spec.
//
// The spec is the ordered list of what the packet hands forward, and it is built from a rule rather than typed out, so re-running it after a
// document is written produces the same list plus that document. Every member is pinned here by bytes and hash, which is what lets `plan`
// refuse a member that changed between this record and the archive.
//
// What the package carries, and why each family is in it:
//
//   context   the packet's own binding: the publication context, the authority it was checked against, the authorization, the dispatch and
//             the acceptance objectives. A reader of the payload has to be able to see what the packet was asked to do.
//   inputs    the files the taskbook named, all 52 of them, copied so the package is self-contained. The exact source path of each is kept in
//             `required_set` as an alias, because a member path inside the payload and the path the packet read are not the same string and
//             pretending otherwise is how a read comparison goes wrong.
//   engine    the publisher this packet wrote, and the reference modules it reuses from 144 -- including the judge it changed.
//   tests     the scripts that test the engine and the judge, so the payload carries its own tests rather than only their results.
//   guest     every channel, trial record and matrix entry the 144 guest run produced, because the G re-consumption reads them and a package
//             whose G can only be re-derived by reaching outside itself is not a closure.
//   records   the packet's audits, its root records, and the evidence the seal consumes -- named, not globbed.
//   report    the packet's report.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const RUN = process.argv[2] || '01';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

const members = [];
const aliases = {};
const problems = [];
const add = (role, memberPath, from, why) => {
  if (aliases[memberPath]) {
    problems.push({ why: 'two members would share one path', path: memberPath, from: [aliases[memberPath].from, from] });
    return null;
  }
  if (!fs.existsSync(from) || !fs.statSync(from).isFile()) {
    problems.push({ why: 'a member source is absent', path: memberPath, from });
    return null;
  }
  const bytes = fs.readFileSync(from);
  aliases[memberPath] = { from, role, why };
  const m = { path: memberPath, role, from, bytes: bytes.length, sha256: sha(bytes) };
  members.push(m);
  return m;
};
const base = (p) => String(p).split(/[\\/]/).pop();

// ---------------------------------------------------------------- context
for (const name of ['publication-context-145.json', 'authority-145.json', 'authorization-145.json',
  'dispatch-145.json', 'acceptance-objectives-145.json', 'assembly-source-contract-145.json',
  'candidate-commit-identities-145.json', 'candidate-source-and-test-materials-145.json',
  'current-default-materials-145.json', 'execution-identities-145.json', 'reference-index-145.json',
  'history-and-forward-materials-144.json', 'build-material-catalog-145.json']) {
  add('context', 'context/' + name, R + '/' + name, 'a record that states what this packet is and what it was allowed to do');
}

// ---------------------------------------------------------------- inputs, the 52 the taskbook named
const audit = JSON.parse(fs.readFileSync(R + '/input-audit-145.json', 'utf8'));
const rows = Array.isArray(audit.rows) ? audit.rows.slice() : [];
rows.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
let inputOrdinal = 0;
for (const row of rows) {
  inputOrdinal += 1;
  const n = String(inputOrdinal).padStart(2, '0');
  add('input', 'inputs/' + n + '-' + base(row.path), row.path, 'one of the files the taskbook names, copied so the package stands alone');
}

// ---------------------------------------------------------------- engine and tests
add('engine', 'engine/publish-145.mjs', R + '/worktree-145/tools/gtos-dsh-145/publish-145.mjs',
  'the publisher this packet wrote: plan, archive, seal, verify, state');
for (const name of ['resource-semantics-144.mjs', 'scene-admission-144.mjs', 'host-path-144.mjs',
  'prerequisite-consumption-144.mjs', 'native-request-144.mjs', 'private-boot-checkpoints-144.mjs']) {
  add('engine', 'engine/' + name, R + '/worktree-145/tools/gtos-dsh-144/references/' + name,
    name === 'resource-semantics-144.mjs'
      ? 'the frame and resource judge, with this packet corrections: the exact unsigned member enumeration, the range check, and the slot and identifier numbers'
      : 'a reference module the production engines import, carried unchanged so the package can be read without the worktree');
}
for (const name of ['r-members-145.mjs', 'r-negatives-145.mjs', 'g-reconsume-145.mjs', 'f-fixture-145.mjs',
  'io-trace-145.cjs', 'd-consumers-145.mjs', 'make-spec-145.mjs', 'required-set-145.mjs']) {
  add('test', 'tests/' + name, R + '/stage-output/' + name, 'a test or tool this packet wrote, carried with its results');
}

// ---------------------------------------------------------------- the prerequisite records the guest trials were admitted under
// Every guest trial record names the prerequisite set it ran under, and that record in turn names ten role records. They are carried so the
// prerequisite consumer has the real records to consume instead of a description of them.
const trial07 = JSON.parse(fs.readFileSync(R144 + '/g-trial-gm144-07-144.json', 'utf8'));
const prereqPath = trial07.prerequisites.record.path;
add('prerequisite', 'prerequisite/t-prerequisites.json', prereqPath,
  'the ten prerequisite role records the guest trials were admitted under');
const prereq = JSON.parse(fs.readFileSync(prereqPath, 'utf8'));
for (const role of (prereq.roles || [])) {
  if (!role.path || !fs.existsSync(role.path)) { problems.push({ why: 'a prerequisite role record is absent', role: role.role, path: role.path }); continue; }
  add('prerequisite', 'prerequisite/role-' + role.role + '.json', role.path,
    'one role record of the prerequisite set, which the prerequisite consumer reads and re-hashes');
}

// ---------------------------------------------------------------- the depfiles the producer left behind
// The dependency record names one depfile per compiled unit. They are the input to the producer consumer's own depfile parser, so they are
// carried: without them that parser has nothing real to read, and a parser tested on a string this packet wrote itself would be testing the
// packet. Each is named by its slot, because two units in different directories can share a basename.
const depRole = (prereq.roles || []).find((r) => r.role === 'dependency_chain');
if (!depRole) problems.push({ why: 'the prerequisite set names no dependency chain record, so there are no depfiles to carry' });
else {
  const depRec = JSON.parse(fs.readFileSync(depRole.path, 'utf8'));
  for (const unit of (depRec.inputs || [])) {
    if (!unit.depfile || !unit.depfile.path) continue;   // an assembly unit is compiled without a depfile
    const from = '//wsl$/Ubuntu-20.04' + unit.depfile.path;
    if (!fs.existsSync(from)) { problems.push({ why: 'a depfile the dependency record names is not on disk', slot: unit.slot, path: from }); continue; }
    add('depfile', 'prerequisite/depfile/slot-' + unit.slot + '-' + unit.depfile.path.split('/').pop(), from,
      'the depfile one compiled unit left behind, which the producer consumer parses');
  }
}

// ---------------------------------------------------------------- the image and its map
// The packet does not build a new image -- that is not what it is for -- so the image its claims are about is the one the 144 guest run booted,
// carried here so a consumer of the package can read the image and the map without reaching outside it. It is pinned by bytes and hash like
// every other member, and the record that says which image it is sits beside it in `context/`.
const IMAGE_DIR = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-144-e759-finality-view/image';
for (const [role, name] of [['image', 'p144-final.elf'], ['image_map', 'p144-final.map']]) {
  const from = IMAGE_DIR + '/' + name;
  if (!fs.existsSync(from)) { problems.push({ why: 'the image or map the packet is about is absent', path: from }); continue; }
  add(role, 'image/' + name, from, 'the linked image the guest run booted, or the map beside it');
}

// ---------------------------------------------------------------- guest material, so G can be re-derived from the payload
const matrixPath = R144 + '/g-matrix-gm144-01-144.json';
add('guest', 'guest/g-matrix.json', matrixPath, 'the matrix the 28 trials belong to');
let trials = 0;
let channels = 0;
for (let i = 1; i <= 28; i++) {
  const n = String(i).padStart(2, '0');
  const trialPath = R144 + '/g-trial-gm144-' + n + '-144.json';
  if (!fs.existsSync(trialPath)) { problems.push({ why: 'a trial record the matrix names is absent', path: trialPath }); continue; }
  trials += 1;
  add('guest', 'guest/g-trial-' + n + '.json', trialPath, 'one recorded trial: the scene, its declarations and the identity of every channel');
  const trial = JSON.parse(fs.readFileSync(trialPath, 'utf8'));
  for (const side of ['e9', 'serial']) {
    const c = trial.channels && trial.channels[side] ? trial.channels[side] : null;
    if (!c || !c.path) continue;
    const m = add('guest', 'guest/' + n + '.' + side + '.log', c.path, 'one channel of one recorded trial');
    if (m && m.sha256 !== c.sha256) {
      problems.push({ why: 'a channel is not the bytes the trial record pins', path: c.path, want: c.sha256, got: m.sha256 });
    }
    if (m) channels += 1;
  }
}

// ---------------------------------------------------------------- this packet's records
// The carried records are named, not globbed. A glob made the member list a function of everything that happened to be in the root when the spec
// ran: every new spec and every new attempt at a test record added a member, so the list could never settle and a spec could not pin a required
// set derived from a list the next spec would change. What is carried is what the packet stands behind -- its audits, its root records, and the
// evidence the seal consumes -- and the spec itself and the required set are pinned by the plan and by the `required_set` field instead of being
// members of their own payload.
//
// Superseded and failed attempts stay on disk. They are evidence of what was tried, and this list is evidence of what is claimed.
const CARRIED_RECORDS = [
  ['input-audit-145.json', 'the audit of the 52 taskbook inputs, with the verdict on each'],
  ['setup-roots-145.json', 'the roots as they were before the packet created them'],
  ['setup-roots-145b.json', 'the same check after the packet created its roots'],
  ['observe-roots-145.json', 'the observation roots, recorded so a later reader can see what was watched'],
  ['publication-context-145.json', 'the production context this packet publishes under'],
];
for (const [name, why] of CARRIED_RECORDS) add('record', 'records/' + name, R + '/' + name, why);
// The evidence records, carried because the seal names them and because a reader of the payload should be able to check the seal's own premises
// without leaving the package. They are named through the same environment variable the seal pin uses, so the two can never disagree.
for (const name of (process.env.GTOS_EVIDENCE || '').split(',').map((s) => s.trim()).filter(Boolean)) {
  add('record', 'records/' + name, R + '/' + name, 'a record the seal consumes as evidence, carried so the claim and its premise travel together');
}

// ---------------------------------------------------------------- the report, if it exists yet
const reportPath = R + '/report-145.md';
if (fs.existsSync(reportPath)) {
  add('report', 'report/report-145.md', reportPath, 'the packet report');
}

// The ordered plan: by path, which is the order the payload will carry.
members.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
const roles = {};
for (const m of members) roles[m.role] = (roles[m.role] || 0) + 1;

// The required-set pin, when a required set has already been written. The first pass has none, which is why this script is run twice: once to
// produce the member list, once to pin the record that was derived from it.
const requiredSetFiles = fs.readdirSync(R).filter((f) => /^required-set-145-\d+\.json$/.test(f)).sort();
const requiredSetName = process.env.GTOS_REQUIRED_SET || (requiredSetFiles.length ? requiredSetFiles[requiredSetFiles.length - 1] : null);
let requiredSetPin = null;
if (requiredSetName && fs.existsSync(R + '/' + requiredSetName)) {
  const bytes = fs.readFileSync(R + '/' + requiredSetName);
  requiredSetPin = { path: R + '/' + requiredSetName, bytes: bytes.length, sha256: sha(bytes), ok_field: 'ok' };
}
// Evidence records the seal consumes, each required to say true in a named field. They are named through the environment rather than assumed,
// because a seal that pins a record the packet did not write is a seal over someone else's work.
const specEvidence = (process.env.GTOS_EVIDENCE || '').split(',').map((s) => s.trim()).filter(Boolean).map((name) => {
  const p = R + '/' + name;
  const bytes = fs.readFileSync(p);
  return { what: name, path: p, bytes: bytes.length, sha256: sha(bytes), ok_field: 'ok' };
});

const spec = {
  task: 'GTOS-DSH-145',
  stage: 'spec-145-' + RUN,
  what: 'the members this packet hands forward, each pinned by size and hash, with the exact source path of every member beside it',
  members: members.map((m) => ({ path: m.path, role: m.role, from: m.from, bytes: m.bytes, sha256: m.sha256 })),
  roles,
  total_bytes: members.reduce((n, m) => n + m.bytes, 0),
  member_count: members.length,
  // The digest of the member list alone, so the two passes can be compared: the pass that produces the list, and the pass that pins the required
  // set derived from it. If this digest differs between them, the required set was derived from a list that no longer exists and the seal would
  // be a receipt for something else.
  member_list_ordinal_digest: sha(Buffer.from(members.map((m, i) => `${i}\0${m.path}\0${m.bytes}\0${m.sha256}`).join('\n'), 'utf8')),
  // The alias table is the point of the `from` field: a member path inside the payload and the path on disk are different strings, and a
  // consumer read set compared with the required set by suffix would match both to anything. Every comparison this packet makes is exact,
  // through this table.
  aliases: Object.fromEntries(Object.entries(aliases).map(([k, v]) => [k, { from: v.from, role: v.role }])),
  // The required set the seal is required to consume. It is built by `required-set-145.mjs` from this member list, and it is pinned here by
  // bytes and hash, so a seal over a payload whose required set changed after the plan is a refusal. On the first pass it does not exist yet,
  // which is why the spec is rebuilt after it is written: the member list is what the required set is derived from, so the two passes agree on
  // everything except this one field.
  required_set: requiredSetPin,
  evidence: Array.isArray(specEvidence) ? specEvidence : [],
  counts: { inputs: inputOrdinal, guest_trials: trials, guest_channels: channels },
  problems,
  ok: problems.length === 0 && members.length > 0,
  ok_means: 'every member exists, no two members share a path, and every guest channel is byte-identical to what its 144 trial record pins',
};
const target = R + '/spec-145-' + RUN + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(spec, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: spec.ok, members: spec.member_count, roles, total_bytes: spec.total_bytes,
  counts: spec.counts, problems }, null, 2));
process.exit(spec.ok ? 0 : 1);
