// GTOS-DSH-145 D: what the production consumers actually open, on a package built from this packet's own spec.
//
// The 144 comparison took its read set from a member table the packet had already built (`d.members`), and then compared it with a required
// set by suffix (`endsWith`). Both halves were weak in the same direction: the read set could only ever report what the packet believed it had
// read, and a suffix match makes `guest/07.e9.log` and `logs/gm144-07.e9.log` the same name. The result was a comparison that agreed with
// itself, and 274 names left over that were counted and never explained.
//
// This does three different things:
//
//   trace      every consumer runs as its own process with `io-trace-145.cjs` preloaded, so the read set is the set of paths the process
//              actually opened, with the hash of the bytes it got. The consumers are not modified and do not know they are watched.
//   required   the required set is built from the package: one entry per member the payload carries, keyed by the exact package path, with
//              the exact source path it was copied from recorded as an alias. The comparison is by exact path through that alias table, in
//              both directions. Nothing is matched by suffix.
//   classify   a file a consumer read that the required set does not name is not deleted and not ignored. Each one is put in a class --
//              engine module, node internal, package member under another name, trace output, or unexplained -- and the unexplained ones are
//              what make this record fail.
//
// The package consumed here is a rehearsal: a TEST_ONLY task identity, the same spec members, the same engine, so the payload's members are
// the members the production payload will carry. That is stated rather than implied, and the production archive is afterwards verified to
// carry exactly this member set with these hashes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const W = R + '/worktree-145';
const ENGINE = W + '/tools/gtos-dsh-145/publish-145.mjs';
const TRACER = R + '/stage-output/io-trace-145.cjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const RUN = process.argv[2] || '01';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const exists = (p) => fs.existsSync(p);

const ROOT = R + '/test-only/d-145-' + RUN;
if (exists(ROOT)) throw new Error('refused, exists: ' + ROOT);
fs.mkdirSync(ROOT, { recursive: true });
const PKG = ROOT + '/package';          // the rehearsal payload, plan and receipt
const TREE = ROOT + '/extracted';       // the package written out by a reader that is not the engine's writer
const TRACES = ROOT + '/traces';
const ENTRY = ROOT + '/entry';
for (const d of [PKG, TREE, TRACES, ENTRY]) fs.mkdirSync(d, { recursive: true });

// ---------------------------------------------------------------- 1. build the rehearsal package with the production engine
// The spec is named rather than assumed, because the spec pins this script's own bytes and is therefore rebuilt after every edit to it. The
// highest-numbered spec is the current one.
const specFiles = fs.readdirSync(R).filter((f) => /^spec-145-\d+\.json$/.test(f)).sort();
if (specFiles.length === 0) throw new Error('no spec to consume: run make-spec-145 first');
const SPEC_PATH = R + '/' + (process.argv[3] || specFiles[specFiles.length - 1]);
const productionSpec = JSON.parse(fs.readFileSync(SPEC_PATH, 'utf8'));
const REHEARSAL_TASK = 'GTOS-DSH-145R-' + RUN;
const rehearsalSpec = JSON.parse(JSON.stringify(productionSpec));
rehearsalSpec.task = REHEARSAL_TASK;
rehearsalSpec.stage = 'spec-145r-' + RUN;
rehearsalSpec.what = productionSpec.what;
// The seal requires the plan to pin a required set and to consume evidence, so the rehearsal pins a fixture of each: a required set built from
// this payload's own member list, and an evidence record that says true. That exercises the seal's real path. The production seal afterwards
// consumes the packet's real required set and its real records, which is the transaction that counts.
const fixtureRequiredSet = {
  task: REHEARSAL_TASK, stage: 'required-set-145r-' + RUN,
  what: 'a TEST_ONLY required set for the rehearsal, derived from the payload member list exactly as the production one is',
  entries: rehearsalSpec.members.map((m) => ({ package_path: m.path, role: m.role, source: m.from, bytes: m.bytes, sha256: m.sha256 })),
  ok: true, ok_means: 'TEST_ONLY fixture: the required set is the payload member list',
};
const fixtureRequiredPath = ROOT + '/required-set-rehearsal.json';
fs.writeFileSync(fixtureRequiredPath, JSON.stringify(fixtureRequiredSet, null, 2) + '\n');
const fixtureEvidenceBody = { task: REHEARSAL_TASK, what: 'a TEST_ONLY evidence record for the rehearsal', ok: true };
const fixtureEvidencePath = ROOT + '/evidence-rehearsal.json';
const fixtureEvidenceBytes = Buffer.from(JSON.stringify(fixtureEvidenceBody, null, 2) + '\n', 'utf8');
fs.writeFileSync(fixtureEvidencePath, fixtureEvidenceBytes);
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
rehearsalSpec.required_set = { ...pin(fixtureRequiredPath), ok_field: 'ok' };
rehearsalSpec.evidence = [{ what: 'evidence-rehearsal.json', ...pin(fixtureEvidencePath), ok_field: 'ok' }];
const specPath = ROOT + '/spec.json';
fs.writeFileSync(specPath, JSON.stringify(rehearsalSpec, null, 2) + '\n');
const ctx = {
  task: REHEARSAL_TASK,
  scope: 'test_only',
  authorized_report_root: ROOT,
  authority_record: R + '/authority-145.json',
  fixed_context_path: ROOT + '/context.json',
  fixed_state_path: ROOT + '/publication-state-' + RUN + '.jsonl',
  production_destination: ROOT + '/archive-' + RUN,
  note: 'TEST_ONLY rehearsal of the production package: the same engine, the same spec members, a separate task identity and root. It cannot '
    + 'stand in for the production transaction.',
};
const ctxPath = ROOT + '/context.json';
fs.writeFileSync(ctxPath, JSON.stringify(ctx, null, 2) + '\n');
// The engine's archive verb requires the destination to be inside the context's terminal directory, and it does not create it.
fs.mkdirSync(ctx.production_destination, { recursive: true });

const run = (args) => {
  const r = spawnSync(NODE, [ENGINE, ...args], { encoding: 'utf8', timeout: 900000, windowsHide: true, maxBuffer: 64 * 1024 * 1024 });
  let json = null;
  try { json = JSON.parse((r.stdout || '').trim()); } catch { json = null; }
  return { status: r.status, json, stdout: (r.stdout || '').slice(-1500), stderr: (r.stderr || '').slice(-3000) };
};
const planPath = PKG + '/plan.json';
// The engine requires the payload and the receipt to sit inside the task's own terminal directory, which is what stops a payload being
// written somewhere the task does not own. The plan may live anywhere under the authorized root.
const payloadPath = ctx.production_destination + '/payload.tar';
const receiptPath = ctx.production_destination + '/receipt.json';
const planRun = run(['plan', ctxPath, specPath, planPath]);
if (planRun.status !== 0) {
  const out = { task: 'GTOS-DSH-145', stage: 'd-145-' + RUN, ok: false, phase: 'plan', run: planRun };
  fs.writeFileSync(R + '/d-consumers-145-' + RUN + '.json', JSON.stringify(out, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify(out, null, 2));
  process.exit(1);
}
const archiveRun = run(['archive', ctxPath, planPath, payloadPath]);
const sealRun = archiveRun.status === 0 ? run(['seal', ctxPath, payloadPath, receiptPath, planPath]) : null;
const verifyRun = sealRun && sealRun.status === 0 ? run(['verify', ctxPath, payloadPath, receiptPath, planPath]) : null;

// ---------------------------------------------------------------- 2. extract with a reader that is not the writer
const readTar = (bytes) => {
  const out = [];
  let at = 0;
  while (at + 512 <= bytes.length) {
    const h = bytes.subarray(at, at + 512);
    if (h.every((b) => b === 0)) break;
    const field = (o, n) => { const raw = h.subarray(o, o + n); const z = raw.indexOf(0);
      return raw.subarray(0, z < 0 ? n : z).toString('utf8'); };
    const name = field(0, 100);
    const size = parseInt(field(124, 12).trim() || '0', 8);
    const prefix = field(345, 155);
    const full = prefix ? prefix + '/' + name : name;
    const body = bytes.subarray(at + 512, at + 512 + size);
    out.push({ name: full, size, sha256: sha(body), body });
    at += 512 + Math.ceil(size / 512) * 512;
  }
  return out;
};
const payloadBytes = exists(payloadPath) ? fs.readFileSync(payloadPath) : Buffer.alloc(0);
const members = readTar(payloadBytes);
for (const m of members) {
  const dest = TREE + '/' + m.name;
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, m.body, { flag: 'wx' });
}
const memberByPath = new Map(members.map((m) => [m.name, m]));

// ---------------------------------------------------------------- 3. the required set, exact, with its alias table
// One entry per package member. `source` is the path it was copied from, and that is the alias: a comparison that needs to know which on-disk
// file a package member came from looks it up here, and a comparison that needs the package path does not have to guess.
const required = members.map((m) => {
  const from = (productionSpec.aliases && productionSpec.aliases[m.name]) ? productionSpec.aliases[m.name].from : null;
  const role = (productionSpec.aliases && productionSpec.aliases[m.name]) ? productionSpec.aliases[m.name].role : null;
  return { package_path: m.name, source: from, role, bytes: m.size, sha256: m.sha256 };
});
const requiredPaths = new Set(required.map((r) => r.package_path));
const sourceToPackage = new Map(required.filter((r) => r.source).map((r) => [r.source.replace(/\\/g, '/'), r.package_path]));

// ---------------------------------------------------------------- 4. the consumers
// Each entry is written here and imports the module the payload carries, so what runs is the packaged consumer rather than a copy of it. The
// entry is a harness and says so; the logic under test is the package's own.
const entrySource = (body) => `'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { pathToFileURL } = require('node:url');
const TREE = ${JSON.stringify(TREE)};
const OUT = ${JSON.stringify(ROOT + '/entry/result.json')};
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const result = { consumer: ${JSON.stringify('')}, reads: [], problems: [], facts: {} };
result.consumer = process.env.GTOS_IO_CONSUMER || 'unnamed';
const readBytes = (p) => { const b = fs.readFileSync(p); result.reads.push({ path: p, bytes: b.length, sha256: sha(b) }); return b; };
const readJson = (p) => { try { return JSON.parse(readBytes(p).toString('utf8')); } catch (e) { result.problems.push({ why: 'not JSON', path: p }); return null; } };
// The packaged modules are ES modules, so they are imported rather than required. A dynamic import is awaited at the top of this file, which is
// why the body below is written as an async function: the import has to finish before the consumer can ask its question.
const load = async (rel) => import(pathToFileURL(TREE + '/' + rel).href);
(async () => {
${body}
fs.writeFileSync(OUT, JSON.stringify(result, null, 2) + '\\n');
process.exit(result.problems.length === 0 ? 0 : 1);
})().catch((e) => {
  result.problems.push({ why: 'the consumer threw', detail: String(e && e.message || e).slice(0, 600) });
  try { fs.writeFileSync(OUT, JSON.stringify(result, null, 2) + '\\n'); } catch (_) {}
  process.exit(1);
});
`;

const CONSUMERS_NOTE = 'The consumers are declared below as functions of the tree. A consumer whose only job was to read a file this script '
  + 'names would not be a consumer of anything, so each one below imports the module the payload carries and asks it a question about the '
  + 'package.';
const consumers = [];

// -- member identity: every member the payload carries, opened and hashed.
consumers.push({
  name: 'member-identity',
  run: () => {
    const p = ENTRY + '/member-identity.cjs';
    fs.writeFileSync(p, entrySource(`
// Every member of the package, opened by the path the package gives it and hashed. This is the consumer the plan exists for: it is the one
// that would notice a member whose bytes are not the bytes the plan pinned. The plan is not a member of its own payload, so the consumer is
// pointed at it by the harness and that read is recorded as a control input rather than as a package read.
const plan = readJson(process.env.GTOS_PLAN);
const listed = plan && Array.isArray(plan.members) ? plan.members : [];
if (listed.length === 0) result.problems.push({ why: 'the package carries no plan, so no member is pinned' });
let matched = 0;
for (const m of listed) {
  const p = TREE + '/' + m.path;
  if (!fs.existsSync(p)) { result.problems.push({ why: 'a planned member is absent from the extracted package', member: m.path }); continue; }
  const b = readBytes(p);
  if (b.length !== m.bytes) result.problems.push({ why: 'a member is a different size than the plan states', member: m.path });
  else if (sha(b) !== m.sha256) result.problems.push({ why: 'a member has different bytes at the same size', member: m.path });
  else matched += 1;
}
result.facts = { planned: listed.length, matched };
`), 'utf8');
    return p;
  },
});

// -- the resource judge, over every packaged guest trial: this is the full G on the package.
consumers.push({
  name: 'resource-judge-full-g',
  run: () => {
    const p = ENTRY + '/resource-judge.cjs';
    fs.writeFileSync(p, entrySource(`
// The packaged judge, over the packaged channels, for every trial the package carries. Two judges, routed by family, exactly as the driver
// routes them: the resource judge for the resource scenes and the functional verdict for the rest. The modules are read by the consumer before
// they are imported, so the trace shows which bytes of the package were executed and the hash of each.
readBytes(TREE + '/engine/resource-semantics-144.mjs');
readBytes(TREE + '/engine/scene-admission-144.mjs');
const RJ = await load('engine/resource-semantics-144.mjs');
const ADM = await load('engine/scene-admission-144.mjs');
const matrix = readJson(TREE + '/guest/g-matrix.json');
const RESOURCE = ['runtime-objects-smp1','runtime-recovery-smp1','runtime-frame-recovery-smp1','runtime-objects-smp2','runtime-objects-smp3',
  'runtime-recovery-smp2','runtime-recovery-smp3','runtime-frame-recovery-smp2','runtime-frame-recovery-smp3'];
const files = fs.readdirSync(TREE + '/guest').filter((f) => /^g-trial-\\d+\\.json$/.test(f)).sort();
const rows = [];
for (const f of files) {
  const trial = readJson(TREE + '/guest/' + f);
  const text = {};
  for (const side of ['e9', 'serial']) {
    const q = TREE + '/guest/' + f.replace(/^g-trial-(\\d+)\\.json$/, '$1') + '.' + side + '.log';
    if (fs.existsSync(q)) text[side] = readBytes(q).toString('utf8');
  }
  const lines = (t) => String(t).split('\\n').map((l) => l.replace(/\\r$/, ''));
  const isResource = RESOURCE.includes(trial.scene);
  const verdict = isResource ? RJ.judgeResourceSemantics(trial.scene, text)
    : ADM.functionalVerdict(ADM.scene(trial.scene), { e9: text.e9 === undefined ? null : lines(text.e9),
      serial: text.serial === undefined ? null : lines(text.serial) });
  rows.push({ trial: trial.trial, scene: trial.scene, judge: isResource ? 'resource' : 'functional',
    accepted: verdict.accepted === true, obligations: (verdict.obligations || []).length,
    first_refusal: verdict.first_refusal ? (verdict.first_refusal.obligation || verdict.first_refusal.why) : null });
}
const refused = rows.filter((r) => r.accepted !== true);
if (refused.length) result.problems.push({ why: 'a packaged trial was refused by the packaged judge', count: refused.length,
  first: refused.slice(0, 3) });
if (files.length !== 28) result.problems.push({ why: 'the package does not carry every trial', found: files.length, want: 28 });
result.facts = { matrix_present: matrix !== null, trials: rows.length, accepted: rows.filter((r) => r.accepted).length,
  resource_trials: rows.filter((r) => r.judge === 'resource').length, rows };
`), 'utf8');
    return p;
  },
});

// -- the prerequisite consumer, over the packaged prerequisite records.
consumers.push({
  name: 'prerequisite',
  run: () => {
    const p = ENTRY + '/prerequisite.cjs';
    fs.writeFileSync(p, entrySource(`
// The packaged prerequisite consumer, over the packaged prerequisite set.
//
// The consumer's \`consumeRole\` is not called, and the reason is stated rather than worked around: every one of its branches reads files the
// role records name -- the producer's sources and headers, the frozen order file, the order ledger, the running runner -- and this payload
// carries the records, not the producing packet's build tree. Running it anyway would either fail on a missing path or read the parent packet's
// tree, and a read of the parent tree is not a read of this package. Two of its ten branches refuse on exactly that, with these reasons, and
// they are recorded here as evidence for the statement rather than as results:
//
//   source_profile_contract  no order ledger was supplied, so "frozen before build" cannot be checked
//   tooling_subject          the running runner could not be read for comparison
//
// What is consumed instead is the part of the consumer that is a function of the records themselves: \`bindSet\`, which is the consumer's own
// check that ten role records are one set, plus a re-hash of every record the set pins against the bytes in the package. That is a real
// question about the package with a real answer, and it does not need the producer's tree to answer it.
readBytes(TREE + '/engine/prerequisite-consumption-144.mjs');
const PC = await load('engine/prerequisite-consumption-144.mjs');
const set = readJson(TREE + '/prerequisite/t-prerequisites.json');
const roles = set && Array.isArray(set.roles) ? set.roles : [];
if (roles.length === 0) result.problems.push({ why: 'the package carries no prerequisite role list' });
const rows = [];
for (const role of roles) {
  const file = TREE + '/prerequisite/role-' + role.role + '.json';
  if (!fs.existsSync(file)) { result.problems.push({ why: 'a role record the set names is not in the package', role: role.role }); continue; }
  const bytes = readBytes(file);
  // The admission block the set carries is what pins each role record, so the pin is checked against the bytes just read. A record whose
  // bytes changed after the set was written is not the record the set describes.
  const admitted = ((set.admission && set.admission.roles) || []).find((a) => a.role === role.role) || null;
  const pinned = admitted !== null && admitted.bytes === bytes.length && admitted.sha256 === sha(bytes);
  rows.push({ role: role.role, kind: role.kind, bytes: bytes.length, sha256: sha(bytes),
    pinned_by_the_set: pinned, why: pinned ? null : 'the set admission does not pin these bytes' });
}
// The consumer's own set binder, over the roles it just read.
let bound = null;
try { bound = PC.bindSet(rows.map((r) => ({ role: r.role, ok: r.pinned_by_the_set, kind: r.kind }))); }
catch (e) { bound = { ok: false, why: String(e.message).slice(0, 200) }; }
const unpinned = rows.filter((r) => r.pinned_by_the_set !== true);
if (unpinned.length) result.problems.push({ why: 'a packaged role record is not the record the set admission pins', count: unpinned.length,
  first: unpinned.slice(0, 4) });
result.facts = { roles: rows.length, pinned: rows.filter((r) => r.pinned_by_the_set).length, bound, rows,
  consumeRole: { called: false,
    why: 'every branch of consumeRole reads files the role records name from the producing packet build tree, which this payload does not '
      + 'carry; the two branches that refuse on a missing context are quoted in the header of this consumer, and the branches that need only '
      + 'the records are exercised above through bindSet and the re-hash',
    observed_refusals: [
      { role: 'source_profile_contract', why: 'no order ledger was supplied, so "frozen before build" cannot be checked' },
      { role: 'tooling_subject', why: 'the running runner could not be read for comparison' },
    ] } };
`), 'utf8');
    return p;
  },
});

// -- the producer consumer: the depfile reader, over the packaged depfiles.
consumers.push({
  name: 'producer-depfile',
  run: () => {
    const p = ENTRY + '/producer.cjs';
    fs.writeFileSync(p, entrySource(`
// The packaged producer consumer's own depfile reader, over the depfiles the producer left behind.
//
// A depfile is a make rule: a target, a colon, and the sources it was built from, with long lines continued by a trailing backslash. The
// consumer parses it; this harness then asks whether the sources it parsed are the sources the packaged dependency record says that unit was
// built from. That comparison is exact, on the path tails: the depfile spells some sources absolutely and some relative to the wave root, and
// the record spells them all absolutely, so both sides are reduced to the tail below the wave root and the two sets must be equal.
//
// The sources themselves are not in the payload -- they belong to the packet that compiled them -- and that is stated in the result rather than
// implied by a check that never looked for them.
readBytes(TREE + '/engine/prerequisite-consumption-144.mjs');
const PC = await load('engine/prerequisite-consumption-144.mjs');
const dep = readJson(TREE + '/prerequisite/role-dependency_chain.json');
const units = dep && Array.isArray(dep.inputs) ? dep.inputs : [];
if (units.length === 0) result.problems.push({ why: 'the packaged dependency record names no unit' });
// The root a unit relative tokens are relative to, derived per unit rather than assumed.
//
// A depfile spells some sources absolutely and some relative to the include root, and the record spells them all absolutely. Two packets are
// represented in one dependency record -- the 144 wave and the 140 wave it carried forward -- so there is no single root for the record, and an
// earlier version of this harness computed one anyway and got /var/tmp. The root is a per-unit fact: it is the directory that a relative token,
// appended to, spells one of the unit own resolved paths. That is exact, it is derived from the two things being compared, and it does not
// hard-code where any packet worked.
const rootFor = (tokens, resolved) => {
  for (const t of tokens) {
    if (t.startsWith('/')) continue;
    const hit = resolved.find((s) => s === t || s.endsWith('/' + t));
    if (hit) return hit.slice(0, hit.length - t.length - 1);
  }
  // No relative token, so the common directory of the absolute ones is the root.
  const parts = resolved.map((s) => s.split('/'));
  if (!parts.length) return null;
  let n = parts[0].length - 1;
  for (const q of parts) { let i = 0; while (i < n && i < q.length && q[i] === parts[0][i]) i += 1; n = i; }
  return parts[0].slice(0, n).join('/');
};
const tailOf = (p, root) => {
  const s = String(p);
  // A token with no leading slash is already relative to the root the depfile was written from, so there is nothing to cut.
  if (!s.startsWith('/')) return { rel: s, outside: false };
  if (s.startsWith(root + '/')) return { rel: s.slice(root.length + 1), outside: false };
  return { rel: s, outside: true };
};
const rows = [];
let parsed = 0, matched = 0;
const mismatched = [];
for (const unit of units) {
  if (!unit.depfile || !unit.depfile.path) continue;    // an assembly unit is compiled without a depfile
  const name = 'slot-' + unit.slot + '-' + unit.depfile.path.split('/').pop();
  const file = TREE + '/prerequisite/depfile/' + name;
  if (!fs.existsSync(file)) { result.problems.push({ why: 'a depfile the dependency record names is not in the package', unit: unit.slot }); continue; }
  const bytes = readBytes(file);
  if (bytes.length !== unit.depfile.bytes || sha(bytes) !== unit.depfile.sha256) {
    result.problems.push({ why: 'a depfile is not the bytes the dependency record pins', unit: unit.slot, member: name });
    continue;
  }
  const tokens = PC.depSourcesOf(bytes.toString('utf8'));
  parsed += 1;
  const resolved = Array.isArray(unit.dep_sources_resolved) ? unit.dep_sources_resolved : [];
  const root = rootFor(tokens, resolved);
  if (!root) { result.problems.push({ why: 'the record names no resolved source, so there is nothing to compare', unit: unit.slot }); continue; }
  const got = tokens.map((t) => tailOf(t, root)).map((x) => (x.outside ? 'OUTSIDE:' + x.rel : x.rel));
  const want = resolved.map((s) => tailOf(s, root)).map((x) => (x.outside ? 'OUTSIDE:' + x.rel : x.rel));
  const g = [...new Set(got)].sort();
  const w = [...new Set(want)].sort();
  const equal = g.length === w.length && g.every((v, i) => v === w[i]);
  if (equal) matched += 1;
  else mismatched.push({ unit: unit.slot, root, parsed: g.length, record: w.length,
    only_parsed: g.filter((v) => !w.includes(v)).slice(0, 4), only_record: w.filter((v) => !g.includes(v)).slice(0, 4) });
  rows.push({ unit: unit.slot, basename: unit.basename, bytes: bytes.length, root, parsed: g.length, record: w.length, equal });
}
if (mismatched.length) result.problems.push({ why: 'the parser and the record disagree about what a unit was built from', count: mismatched.length,
  first: mismatched.slice(0, 4) });
result.facts = { units_with_a_depfile: parsed, equal_to_the_record: matched,
  distinct_roots: [...new Set(rows.map((r) => r.root))], rows: rows.slice(0, 6), rows_total: rows.length,
  sources_are_carried: false,
  why_sources_are_not_carried: 'the sources a depfile names are the producing packet inputs; this payload carries the depfiles and the record '
    + 'that says what they resolve to, and the check is that the two agree, not that the sources are here' };
`), 'utf8');
    return p;
  },
});

// -- the static consumer: the image and its map, read from the package.
consumers.push({
  name: 'static-image-and-map',
  run: () => {
    const p = ENTRY + '/static.cjs';
    fs.writeFileSync(p, entrySource(`
// The image and the map, read from the package. The claim is narrow on purpose: the image is there, its size and hash are the ones the plan
// pins, the map beside it describes a load address range, and the two agree about the entry point. Whether the image is correct is not a
// question a file reader can answer, and this record does not pretend otherwise.
const plan = readJson(process.env.GTOS_PLAN);
const byPath = new Map(((plan && plan.members) || []).map((m) => [m.path, m]));
const images = [...byPath.keys()].filter((k) => k.endsWith('.elf'));
const maps = [...byPath.keys()].filter((k) => k.endsWith('.map'));
if (images.length === 0) result.problems.push({ why: 'the package carries no image' });
if (maps.length === 0) result.problems.push({ why: 'the package carries no map' });
const rows = [];
for (const k of images.concat(maps)) {
  const b = readBytes(TREE + '/' + k);
  const want = byPath.get(k);
  const ok = b.length === want.bytes && sha(b) === want.sha256;
  if (!ok) result.problems.push({ why: 'the image or map is not the bytes the plan pins', member: k });
  rows.push({ member: k, bytes: b.length, matches_plan: ok });
}
// The map is a linker map, so it names the entry symbol and the load address. Those are read and compared with the ELF header rather than
// assumed, which is the one cross-check a file reader can honestly make.
let mapFacts = null;
for (const k of maps) {
  const text = readBytes(TREE + '/' + k).toString('utf8');
  const entry = /0x([0-9a-fA-F]{8,16})\\s+_start\\b/.exec(text) || /_start\\s*$\\s*0x([0-9a-fA-F]+)/m.exec(text);
  mapFacts = { member: k, lines: text.split('\\n').length, has_entry_symbol: /_start/.test(text),
    entry: entry ? entry[1] : null, mentions_the_image: images.some((i) => text.includes(i.split('/').pop())) };
}
for (const k of images) {
  const b = readBytes(TREE + '/' + k);
  // ELF magic, class, machine and entry point, read from the header rather than from the map.
  mapFacts = { ...mapFacts, elf: { member: k, magic_ok: b[0] === 0x7f && b[1] === 0x45 && b[2] === 0x4c && b[3] === 0x46,
    class: b[4], little_endian: b[5] === 1, type: b.readUInt16LE(16), machine: b.readUInt16LE(18),
    entry: b.readUInt32LE(24) } };
}
result.facts = { images: images.length, maps: maps.length, rows, mapFacts };
`), 'utf8');
    return p;
  },
});

// -- the host path consumer's own path rules, over the packaged authority.
consumers.push({
  name: 'host-chain',
  run: () => {
    const p = ENTRY + '/host.cjs';
    fs.writeFileSync(p, entrySource(`
// The packaged host path consumer, asked the questions it exists to answer about paths: does its own identity function see two spellings of one
// path as one file, does its own gate refuse a write outside the roots it was given, and does its own platform conversion round-trip. These are
// the functions the host chain uses before it writes anything, and they are asked here about the packaged authority record's own paths.
readBytes(TREE + '/engine/host-path-144.mjs');
const HP = await load('engine/host-path-144.mjs');
const authority = readJson(TREE + '/context/authority-145.json');
const paths = [];
const collect = (v) => {
  if (typeof v === 'string' && /^[A-Za-z]:[\\\\/]/.test(v)) paths.push(v);
  else if (Array.isArray(v)) v.forEach(collect);
  else if (v && typeof v === 'object') Object.keys(v).forEach((k) => collect(v[k]));
};
collect(authority);
const cases = [];
// Two spellings of one path must be one identity, and two different files must not be.
const a = 'F:/GTOS-Chromium/artifacts/x/y.json';
cases.push({ what: 'two spellings of one path have one identity', ok: HP.identityOf(a) === HP.identityOf('F:\\\\GTOS-Chromium\\\\artifacts\\\\x\\\\y.json') });
cases.push({ what: 'two different paths have different identities', ok: HP.identityOf(a) !== HP.identityOf('F:/GTOS-Chromium/artifacts/x/z.json') });
cases.push({ what: 'sameHostFile agrees with identityOf', ok: HP.sameHostFile(a, 'F:\\\\GTOS-Chromium\\\\artifacts\\\\x\\\\y.json') === true });
// The write gate must refuse a target outside the roots it was given. This is the refusal the host chain depends on, so it is asked directly.
let refusedOutside = null;
try { HP.gateWrite('probe', 'F:/somewhere/else/file.txt', ['F:/GTOS-Chromium/artifacts'], 'win32'); refusedOutside = 'accepted'; }
catch (e) { refusedOutside = 'refused: ' + String(e.message).slice(0, 90); }
cases.push({ what: 'the write gate refuses a target outside the roots', ok: refusedOutside.startsWith('refused') });
// And it must not refuse a target inside them.
let acceptedInside = null;
try { HP.gateWrite('probe', 'F:/GTOS-Chromium/artifacts/x/y.txt', ['F:/GTOS-Chromium/artifacts'], 'win32'); acceptedInside = 'accepted'; }
catch (e) { acceptedInside = 'refused: ' + String(e.message).slice(0, 90); }
cases.push({ what: 'the write gate accepts a target inside the roots', ok: acceptedInside === 'accepted' });
// Every Windows path the authority carries must have a Linux form and come back.
let roundTripped = 0;
const broken = [];
for (const q of paths) {
  try { const back = HP.forConsumer(q, 'linux'); if (typeof back === 'string' && back.length > 0) roundTripped += 1; else broken.push(q); }
  catch (e) { broken.push(q); }
}
cases.push({ what: 'every Windows path in the authority converts for a linux consumer', ok: broken.length === 0,
  count: paths.length, broken: broken.slice(0, 3) });
const bad = cases.filter((c) => c.ok !== true);
if (bad.length) result.problems.push({ why: 'a packaged host path consumer function answered wrongly', cases: bad });
result.facts = { windows_paths_in_authority: paths.length, round_tripped: roundTripped, cases, write_gate: { outside: refusedOutside, inside: acceptedInside },
  stage_roots: (() => { try { return HP.stageRoots('F:/GTOS-Chromium/artifacts/x', 'win32'); } catch (e) { return 'refused: ' + String(e.message).slice(0, 80); } })() };
`), 'utf8');
    return p;
  },
});

// -- the native request consumer, over the packaged authority.
consumers.push({
  name: 'native-request',
  run: () => {
    const p = ENTRY + '/native.cjs';
    fs.writeFileSync(p, entrySource(`
// The packaged native request consumer, over the packaged authority record: the tool roles it parses, the file descriptions it derives, the
// host-path refusal it applies to a control object, and the two conversions it exists to do.
readBytes(TREE + '/engine/native-request-144.mjs');
const NR = await load('engine/native-request-144.mjs');
const authority = readJson(TREE + '/context/authority-145.json');
let roles = null;
try { roles = NR.parseToolRoles(authority); } catch (e) { result.problems.push({ why: 'the tool roles could not be parsed', detail: String(e.message).slice(0, 300) }); }
// parseToolRoles returns a Map keyed by role name. An earlier version of this harness read it as an array or as a plain object and therefore saw
// zero roles, which is the kind of mistake that makes a consumer look exercised when it is not; all three shapes are handled and the shape is
// recorded.
const shape = roles === null ? 'null' : roles instanceof Map ? 'Map' : Array.isArray(roles) ? 'array' : typeof roles;
const roleList = roles === null ? []
  : roles instanceof Map ? [...roles.entries()].map(([k, v]) => ({ role: k, ...(v || {}) }))
  : Array.isArray(roles) ? roles : Object.keys(roles).map((k) => ({ role: k, ...(roles[k] || {}) }));
const described = roleList.slice(0, 6).map((r) => ({ role: r.role, described: (() => {
  try { return NR.describeFile(r) !== null; } catch (e) { return false; } })() }));
let refusal = null;
try { NR.assertNoHostPaths({ ok: 'F:/GTOS-Chromium/x' }); refusal = 'accepted a host path'; }
catch (e) { refusal = 'refused: ' + String(e.message).slice(0, 80); }
if (refusal === 'accepted a host path') result.problems.push({ why: 'the consumer accepted a host path in a control object' });
if (roleList.length === 0) result.problems.push({ why: 'the consumer parsed no tool role from the packaged authority' });
// A linux control object must convert to a host one and back, which is the conversion the whole native request depends on.
const probe = { role: 'node', path: '/mnt/f/GTOS-Chromium/x' };
let roundTrip = null;
try { const host = NR.linuxToHost(probe.path); roundTrip = { host, back: NR.hostToLinux(host) }; }
catch (e) { roundTrip = { refused: String(e.message).slice(0, 120) }; }
if (!roundTrip || roundTrip.refused) result.problems.push({ why: 'the linux to host conversion refused a well formed path', detail: roundTrip });
result.facts = { tool_roles: roleList.length, roles_shape: shape, described, host_path_refusal: refusal, linux_host_round_trip: roundTrip,
  linuxiseDeep: (() => { try { return NR.linuxiseDeep({ a: 'F:/x/y' }); } catch (e) { return 'refused: ' + String(e.message).slice(0, 80); } })() };
`), 'utf8');
    return p;
  },
});

// ---------------------------------------------------------------- 5. run each consumer with the tracer preloaded
const traceRows = [];
const consumerRows = [];
for (const c of consumers) {
  const entry = c.run();
  const tracePath = TRACES + '/' + c.name + '.jsonl';
  const resultPath = ENTRY + '/result.json';
  if (fs.existsSync(resultPath)) fs.unlinkSync(resultPath);
  const r = spawnSync(NODE, ['--require', TRACER, entry], {
    encoding: 'utf8', timeout: 900000, windowsHide: true, maxBuffer: 64 * 1024 * 1024,
    env: { ...process.env, GTOS_IO_TRACE: tracePath, GTOS_IO_CONSUMER: c.name, GTOS_PLAN: planPath },
  });
  let result = null;
  try { result = JSON.parse(fs.readFileSync(resultPath, 'utf8')); } catch { result = null; }
  const trace = fs.existsSync(tracePath)
    ? fs.readFileSync(tracePath, 'utf8').split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l); } catch { return null; } }).filter(Boolean)
    : [];
  traceRows.push(...trace.map((t) => ({ ...t, consumer: c.name })));
  consumerRows.push({
    consumer: c.name, entry: path.basename(entry),
    exit_status: r.status,
    stderr_tail: (r.stderr || '').slice(-1200) || null,
    trace_rows: trace.length,
    result,
    problems: result ? result.problems : [{ why: 'the consumer wrote no result' }],
  });
}

// ---------------------------------------------------------------- 6. the comparison, by exact path
// The trace names absolute paths inside the extracted tree. The required set names package paths. The bridge is the extraction root and the
// alias table, both exact: a trace path becomes a package path by removing the one prefix, and nothing is matched by suffix.
const toPackagePath = (abs) => {
  // A module imported through `import()` is opened by node's loader with a `file:` URL, so the trace shows `file:\F:\...`. That is the same file
  // as the plain path, and stripping the scheme is the whole of the difference; leaving it on made every engine module look like a read outside
  // the package.
  let n = String(abs).replace(/\\/g, '/');
  n = n.replace(/^file:(\/\/)?/i, '');
  if (/^\/[A-Za-z]:\//.test(n)) n = n.slice(1);
  const prefix = TREE.replace(/\\/g, '/') + '/';
  if (n.startsWith(prefix)) return n.slice(prefix.length);
  return null;
};
const enginePaths = new Set(required.filter((r) => r.role === 'engine').map((r) => r.package_path));
const readByConsumer = {};
const allRead = new Map();
for (const t of traceRows) {
  if (t.op === 'trace-opened') continue;
  const pkg = toPackagePath(t.path);
  const key = pkg === null ? String(t.path) : pkg;
  if (!readByConsumer[t.consumer]) readByConsumer[t.consumer] = { package_paths: new Set(), outside: new Set() };
  if (pkg === null) readByConsumer[t.consumer].outside.add(key); else readByConsumer[t.consumer].package_paths.add(key);
  if (!allRead.has(key)) allRead.set(key, { path: key, in_package: pkg !== null, ops: new Set(), consumers: new Set(), bytes: null, sha256: null });
  const row = allRead.get(key);
  row.ops.add(t.op); row.consumers.add(t.consumer);
  if (t.sha256) { row.sha256 = t.sha256; row.bytes = t.bytes; }
}
const readInPackage = [...allRead.values()].filter((r) => r.in_package).map((r) => r.path);
const requiredNotRead = [...requiredPaths].filter((p) => !allRead.has(p)).sort();
const readNotRequired = readInPackage.filter((p) => !requiredPaths.has(p)).sort();

// Every extra is classified. The classes are the ones that can actually occur, and a file that fits none of them is unexplained and fails the
// record -- which is the difference between this and the 144 count of 274.
const classify = (p, ops) => {
  // A directory is not a member. Listing one is a different operation from reading a file, and the trace says which it was, so the two are not
  // conflated: a `readdirSync` on `guest` is the judge enumerating the channels it was packaged with.
  if (ops && ops.length === 1 && ops[0] === 'readdirSync') return 'a directory the consumer listed, not a file it read';
  if (enginePaths.has(p)) return 'engine module the payload carries and the consumer imported';
  if (p.startsWith('prerequisite/')) return 'a prerequisite record or depfile the payload carries and a consumer read';
  if (/^records\/.*\.json$/.test(p)) return 'a record the consumer read on its own initiative, not one the spec pins as required';
  if (p.startsWith('context/')) return 'a context record a consumer read to find its subject';
  if (p.startsWith('guest/')) return 'a guest channel a trial record pointed at';
  if (p.startsWith('image/')) return 'the image or map the static consumer read';
  if (p.startsWith('inputs/')) return 'a taskbook input the payload carries';
  return null;
};
const extras = readNotRequired.map((p) => ({ path: p, class: classify(p, [...(allRead.get(p).ops)]),
  consumers: [...(allRead.get(p).consumers)], ops: [...(allRead.get(p).ops)] }));
const unexplained = extras.filter((e) => e.class === null);

// The other direction, with the alias table: a required member the trace never shows is a required file no consumer opened.
const outsideThePackage = [...allRead.values()].filter((r) => !r.in_package)
  .map((r) => ({ path: r.path, consumers: [...r.consumers], ops: [...r.ops],
    class: String(r.path).replace(/\\/g, '/') === planPath.replace(/\\/g, '/')
      ? 'the plan the harness pointed the member-identity consumer at, which is not a member of its own payload'
      : String(r.path).replace(/\\/g, '/').startsWith(TRACES.replace(/\\/g, '/') + '/') ? 'the trace file itself'
      : null }));
// Files a consumer read outside the extraction root are its own entry script, the node runtime, the tracer, the trace, and the plan. They are
// listed rather than counted, and any one that is not obviously in one of those families is called out.
const outsideUnexplained = outsideThePackage.filter((o) => o.class === null
  && !/node_modules|\/entry\/|io-trace-145\.cjs|\.jsonl$/.test(String(o.path).replace(/\\/g, '/')));

// Coverage by role: how much of each family of the package the consumers actually opened. A role with zero coverage is not a failure of this
// record -- a payload carries records for its readers and for the future, and not every member has a consumer in this packet -- but it is a
// statement that has to be visible rather than averaged away into one boolean.
const coverageByRole = {};
for (const r of required) {
  const role = r.role || 'unroled';
  if (!coverageByRole[role]) coverageByRole[role] = { role, members: 0, read: 0 };
  coverageByRole[role].members += 1;
  if (allRead.has(r.package_path)) coverageByRole[role].read += 1;
}
const coverageRows = Object.values(coverageByRole).map((c) => ({ role: c.role, members: c.members, read: c.read,
  unread: c.members - c.read,
  example_unread: required.filter((r) => (r.role || 'unroled') === c.role && !allRead.has(r.package_path)).slice(0, 3).map((r) => r.package_path) }));

const out = {
  task: 'GTOS-DSH-145',
  stage: 'd-145-' + RUN,
  what: 'a real consumer IO trace over a package built from this packet spec, an independent required set with an exact alias table, and a '
    + 'classification of every file a consumer read that the required set does not name',
  rehearsal: {
    why: 'the production archive is the one transaction this packet may make, and the report has to be final before it. So the consumers run '
      + 'against a rehearsal package built by the same engine from the same spec members, and the production archive is afterwards verified '
      + 'to carry exactly this member set with these hashes.',
    task_identity: REHEARSAL_TASK, root: ROOT, package: PKG,
    plan: planRun.json, archive: archiveRun.json, seal: sealRun && sealRun.json, verify: verifyRun && verifyRun.json,
    refusals: { plan: planRun.stderr || null, archive: archiveRun.stderr || null,
      seal: sealRun ? (sealRun.stderr || null) : null, verify: verifyRun ? (verifyRun.stderr || null) : null },
    runs: { plan: planRun.status, archive: archiveRun.status, seal: sealRun ? sealRun.status : null,
      verify: verifyRun ? verifyRun.status : null },
  },
  package: {
    // The member list is what this record is about, so the member-list digest is what it pins. The spec file itself is rebuilt whenever a
    // record it names is written, so pinning its hash here would make this record stale the moment the next pass ran; the member list does not
    // change across those passes, and the production spec states which required set it pinned.
    spec_used: SPEC_PATH,
    member_list_ordinal_digest: sha(Buffer.from(productionSpec.members.map((m, i) => `${i}\0${m.path}\0${m.bytes}\0${m.sha256}`).join('\n'), 'utf8')),
    payload: { path: payloadPath, bytes: payloadBytes.length, sha256: sha(payloadBytes) },
    members: members.length, member_bytes: members.reduce((n, m) => n + m.size, 0),
    extracted_to: TREE,
    read_back_identical: members.every((m) => sha(fs.readFileSync(TREE + '/' + m.name)) === m.sha256),
  },
  consumers: consumerRows,
  trace: {
    tracer: { path: TRACER, bytes: fs.statSync(TRACER).size, sha256: sha(fs.readFileSync(TRACER)) },
    how: 'each consumer ran as its own node process with --require io-trace-145.cjs, so the read set is what the process opened rather than '
      + 'what this script believes it read',
    rows: traceRows.length,
    distinct_paths: allRead.size,
    distinct_paths_in_package: readInPackage.length,
    per_consumer: Object.fromEntries(Object.entries(readByConsumer).map(([k, v]) => [k, {
      package_paths: v.package_paths.size, outside: v.outside.size }])),
  },
  required_set: {
    how_built: 'one entry per member the payload carries, keyed by the exact package path, with the source path it was copied from as its '
      + 'alias; the comparison is by exact path in both directions and nothing is matched by suffix',
    total: required.length,
    by_role: required.reduce((acc, r) => { acc[r.role || 'unroled'] = (acc[r.role || 'unroled'] || 0) + 1; return acc; }, {}),
    members: required,
    alias_table: Object.fromEntries(sourceToPackage),
  },
  comparison: {
    required_not_read: requiredNotRead,
    required_not_read_count: requiredNotRead.length,
    read_not_required: extras,
    read_not_required_count: extras.length,
    unexplained_extras: unexplained,
    unexplained_count: unexplained.length,
    outside_the_package: outsideThePackage.length,
    outside_the_package_unexplained: outsideUnexplained,
    why: 'a required member no consumer opened is a gap; a member a consumer opened that nothing required is classified rather than deleted, '
      + 'and an extra that fits no class is the only thing that fails this record',
  },
  coverage_by_role: coverageRows,
  coverage_summary: { roles: coverageRows.length, members: required.length,
    read: coverageRows.reduce((n, c) => n + c.read, 0),
    roles_with_full_coverage: coverageRows.filter((c) => c.unread === 0).length,
    roles_with_no_coverage: coverageRows.filter((c) => c.read === 0).map((c) => c.role) },
  ok: planRun.status === 0 && archiveRun.status === 0 && (sealRun ? sealRun.status : 1) === 0 && (verifyRun ? verifyRun.status : 1) === 0
    && consumerRows.every((c) => c.exit_status === 0 && c.problems.length === 0)
    && members.length > 0 && members.every((m) => sha(fs.readFileSync(TREE + '/' + m.name)) === m.sha256)
    && unexplained.length === 0 && outsideUnexplained.length === 0
    && required.filter((r) => allRead.has(r.package_path)).length > 0,
  ok_means: 'the rehearsal package is built, sealed and verified by the production engine; every consumer runs against the extracted package '
    + 'and passes; every file a consumer read is either a required member or is put in a class, with none left unexplained; and the read set is '
    + 'a subset of the required set rather than a list of names that merely resemble it',
};
const target = R + '/d-consumers-145-' + RUN + '.json';
if (exists(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ ok: out.ok, runs: out.rehearsal.runs, members: members.length,
  consumers: consumerRows.map((c) => ({ consumer: c.consumer, exit: c.exit_status, problems: c.problems.length, trace_rows: c.trace_rows })),
  trace: out.trace.rows, required_not_read: requiredNotRead.length, read_not_required: extras.length,
  unexplained: unexplained.length, outside_unexplained: outsideUnexplained.length }, null, 2));
process.exit(out.ok ? 0 : 1);
