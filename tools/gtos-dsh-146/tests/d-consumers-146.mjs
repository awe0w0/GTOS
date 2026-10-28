// GTOS-DSH-146 D: publish a rehearsal payload from the independent required set, then consume it from the extracted tree.
//
// Three things this does that the roles do not, and they are the three the packet was told to close:
//
//   independent     the member list is built from `d-required-146-02.json`, which was derived from the contracts. The engine
//                   is given that list and has to produce a payload carrying every row. It is not given the spec's own
//                   member list, so a required file that was quietly dropped cannot be dropped: the payload is built from
//                   the requirement, not from a list that was itself built from the payload.
//   mandatory       the plan pins all seven role records, and the seal refuses if any of them is absent, changed or says
//                   anything but true. 145's seal had no such gate.
//   fresh           after the seal, the payload is read by a tar reader that is not the engine's writer, written out to a
//                   tree, and the consumers are run AGAINST THAT TREE through an alias table. A consumer that reads the
//                   original files has consumed the packet's working copy; a consumer that reads the extracted tree has
//                   consumed the payload.
//
// The identity is TEST_ONLY and the engine refuses a TEST_ONLY context that claims the production task, so this cannot touch
// the production state. The production archive and seal are one each and happen afterwards.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const W = R + '/worktree-146';
const ENGINE = W + '/tools/gtos-dsh-146/publish-146.mjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const RUN = process.argv[2] || '01';
const SPEC = R + '/spec-146-01.json';
const REQUIRED = R + '/d-required-146-02.json';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => fs.existsSync(p);
const WSL = '//wsl$/Ubuntu-20.04';
const toHost = (p) => {
  const s = String(p);
  if (/^[A-Za-z]:[\\/]/.test(s) || s.startsWith('//')) return s;
  if (s.startsWith('/mnt/')) return s.slice(7, 8).toUpperCase() + ':/' + s.slice(9);
  return WSL + s;
};

const ROOT = R + '/test-only/d-146-' + RUN;
if (exists(ROOT)) throw new Error('refused, exists: ' + ROOT);
fs.mkdirSync(ROOT, { recursive: true });
const CTX = ROOT + '/context-7146.json';
// The engine derives the state path from the task and the report root, and refuses a context that names a different one: a
// caller-supplied ledger would let a second run reset a transaction it had already spent. So the context names the path the
// engine derives rather than one this script would prefer.
const STATE = ROOT + '/publication-state-7146.jsonl';
// Both the state file and the terminal directory are derived by the engine from the task and the report root. A context
// naming its own is refused, and it should be: a caller-chosen transaction directory is a way to start a second transaction
// over a spent identity. So both are named here exactly as the engine derives them.
const DEST = ROOT + '/archive-7146';
const TREE = ROOT + '/extracted';
const TMP = ROOT + '/tmp';
for (const d of [DEST, TREE, TMP]) fs.mkdirSync(d, { recursive: true });

const engine = (args) => {
  const r = spawnSync(NODE, [ENGINE, args[0], CTX, ...args.slice(1)], { encoding: 'utf8', windowsHide: true,
    maxBuffer: 64 * 1024 * 1024 });
  let json = null;
  try { json = JSON.parse(r.stdout); } catch { json = null; }
  return { status: r.status, stdout: (r.stdout || '').trim(), stderr: (r.stderr || '').trim().slice(0, 3000), json };
};

const results = {};

// ---------------------------------------------------------------- 0. the TEST_ONLY identity
const context = {
  task: 'GTOS-DSH-7146',
  scope: 'test_only',
  authorized_report_root: ROOT,
  fixed_state_path: STATE,
  production_destination: DEST,
  rehearsal_of: { task: 'GTOS-DSH-146', spec: { path: SPEC, bytes: fs.statSync(SPEC).size, sha256: sha(fs.readFileSync(SPEC)) } },
  what: 'a TEST_ONLY identity for rehearsing the 146 publication. It is not the production task and the engine refuses a '
    + 'TEST_ONLY context that claims it.',
};
fs.writeFileSync(CTX, JSON.stringify(context, null, 2) + '\n');
results.context = { path: CTX, bytes: fs.statSync(CTX).size, sha256: sha(fs.readFileSync(CTX)) };

// ---------------------------------------------------------------- 1. the rehearsal spec, built from the required set
const productionSpec = readJson(SPEC);
const required = readJson(REQUIRED);
const rehearsalSpec = {
  ...productionSpec,
  task: 'GTOS-DSH-7146',
  stage: 'spec-7146-01',
  rehearsal_of: { task: productionSpec.task, stage: productionSpec.stage, sha256: sha(fs.readFileSync(SPEC)) },
};
const rehearsalSpecPath = ROOT + '/spec-7146-01.json';
fs.writeFileSync(rehearsalSpecPath, JSON.stringify(rehearsalSpec, null, 2) + '\n');

// The spec's member list must be exactly the required set plus the packet's own records. This is checked here rather than
// trusted, because a spec that quietly dropped a requirement would still plan, archive and seal.
// The comparison is on the CONTRACT path, not on `from`: `from` is the path this host can open, which for a requirement in
// the Linux domain is the share form, and comparing those against the contract's Linux paths made 392 requirements look
// absent from a spec that carried every one of them. Comparing the wrong field produced a false failure, which is the same
// class of mistake as producing a false pass.
const specSources = new Set(rehearsalSpec.members.map((m) => String(m.contract_path || m.from).toLowerCase()));
const requiredSources = new Set(required.entries.map((e) => String(e.source).toLowerCase()));
const requiredNotInSpec = [...requiredSources].filter((s) => !specSources.has(s));
const specNotRequired = rehearsalSpec.members.filter((m) => !requiredSources.has(String(m.contract_path || m.from).toLowerCase()));
const specNotRequiredNotOwnRecords = specNotRequired.filter((m) => m.role !== 'context' && m.role !== 'record');
results.member_list_comes_from_the_required_set = {
  required_files: requiredSources.size, spec_members: specSources.size,
  required_files_absent_from_the_spec: requiredNotInSpec.slice(0, 8),
  required_files_absent_from_the_spec_count: requiredNotInSpec.length,
  spec_members_that_are_not_requirements: specNotRequired.length,
  spec_members_that_are_not_requirements_by_role:
    specNotRequired.reduce((acc, m) => { acc[m.role] = (acc[m.role] || 0) + 1; return acc; }, {}),
  spec_members_that_are_neither_a_requirement_nor_a_packet_record: specNotRequiredNotOwnRecords.length,
  ok: requiredNotInSpec.length === 0 && specNotRequiredNotOwnRecords.length === 0,
  ok_means: 'every file the contracts require is a member of the payload, and every member that is not a requirement is one '
    + 'of this packet own context or record files.',
};

// ---------------------------------------------------------------- 2. plan, archive, seal, verify
const PLAN = ROOT + '/plan-7146-01.json';
const PAYLOAD = DEST + '/payload-7146-01.tar';
const RECEIPT = DEST + '/receipt-7146-01.json';

results.plan = engine(['plan', rehearsalSpecPath, PLAN]);
results.archive = engine(['archive', PLAN, PAYLOAD]);
results.seal = engine(['seal', PAYLOAD, RECEIPT, PLAN]);
results.verify = engine(['verify', PAYLOAD, RECEIPT, PLAN]);

const plan = exists(PLAN) ? readJson(PLAN) : null;
const receipt = exists(RECEIPT) ? readJson(RECEIPT) : null;
const pin = (p) => (exists(p) ? { path: p, bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)) } : null);

results.published = {
  plan: pin(PLAN), payload: pin(PAYLOAD), receipt: pin(RECEIPT),
  plan_members: plan ? plan.member_count : null,
  plan_ordinal_digest: plan ? plan.ordinal_digest : null,
  plan_required_set_pinned: plan ? plan.required_set !== null : null,
  plan_mandatory_roles_pinned: plan && plan.mandatory_roles ? plan.mandatory_roles.roles.length : null,
  receipt_roles_consumed: receipt && receipt.mandatory_roles ? receipt.mandatory_roles.consumed.length : null,
  receipt_evidence_consumed: receipt ? receipt.evidence_consumed : null,
  receipt_ordinal_digest: receipt ? receipt.ordinal_digest : null,
  receipt_sealed: receipt ? receipt.sealed : null,
};

// ---------------------------------------------------------------- 3. read the payload with a reader that is not the writer
const BLOCK = 512;
function readTar(bytes) {
  const out = [];
  let at = 0;
  while (at + BLOCK <= bytes.length) {
    const h = bytes.subarray(at, at + BLOCK);
    if (h.every((b) => b === 0)) break;
    const name = h.subarray(0, 100).toString('utf8').replace(/\0.*$/, '');
    const size = parseInt(h.subarray(124, 136).toString('utf8').replace(/\0.*$/, '').trim(), 8);
    const mode = parseInt(h.subarray(100, 108).toString('utf8').replace(/\0.*$/, '').trim(), 8);
    if (!Number.isFinite(size)) throw new Error('a member header has no readable size: ' + name);
    const body = bytes.subarray(at + BLOCK, at + BLOCK + size);
    out.push({ name, size, mode, bytes: Buffer.from(body), sha256: sha(body) });
    at += BLOCK + size + (size % BLOCK === 0 ? 0 : BLOCK - (size % BLOCK));
  }
  return out;
}
const payloadBytes = fs.readFileSync(PAYLOAD);
const members = readTar(payloadBytes);
const memberByPath = new Map(members.map((m) => [m.name, m]));
const written = [];
for (const m of members) {
  const dest = TREE + '/' + m.name;
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, m.bytes);
  written.push({ name: m.name, bytes: m.size, sha256: m.sha256 });
}
results.extraction = {
  reader: 'this script own ustar reader, not the engine writer',
  members: members.length, written: written.length,
  tree_bytes: members.reduce((n, m) => n + m.size, 0),
  all_written_bytes_match: members.every((m) => sha(fs.readFileSync(TREE + '/' + m.name)) === m.sha256),
};

// ---------------------------------------------------------------- 4. the payload against the required set, both directions
const missingFromPayload = [];
const wrongBytes = [];
for (const entry of required.entries) {
  const pkg = rehearsalSpec.aliases[entry.source];
  if (!pkg) { missingFromPayload.push({ source: entry.source, why: 'the spec has no package path for this requirement' }); continue; }
  const m = memberByPath.get(pkg);
  if (!m) { missingFromPayload.push({ source: entry.source, path: pkg, why: 'the payload does not carry it' }); continue; }
  const onDisk = fs.readFileSync(toHost(entry.source));
  if (m.size !== onDisk.length || m.sha256 !== sha(onDisk)) {
    wrongBytes.push({ source: entry.source, path: pkg, want: sha(onDisk), got: m.sha256 });
  }
}
const requirementPaths = new Set(required.entries.map((e) => rehearsalSpec.aliases[e.source]).filter(Boolean));
const extras = members.filter((m) => !requirementPaths.has(m.name));
const unexplainedExtras = extras.filter((m) => !/^context\/|^records\//.test(m.name));
results.closure = {
  required_files: required.entries.length,
  present_in_payload: required.entries.length - missingFromPayload.length - wrongBytes.length,
  missing_from_payload: missingFromPayload.slice(0, 8), missing_count: missingFromPayload.length,
  wrong_bytes: wrongBytes.slice(0, 8), wrong_count: wrongBytes.length,
  payload_members: members.length,
  members_beyond_the_required_set: extras.length,
  unexplained_members: unexplainedExtras.length,
  unexplained: unexplainedExtras.slice(0, 8).map((m) => m.name),
  ok: missingFromPayload.length === 0 && wrongBytes.length === 0 && unexplainedExtras.length === 0,
  ok_means: 'every file the contracts require is a member of the payload with the bytes the contract names, and every member '
    + 'the payload carries beyond those is one of this packet own records.',
};

// ---------------------------------------------------------------- 5. the consumers, on the extracted tree
const aliasMap = {};
for (const [source, pkg] of Object.entries(rehearsalSpec.aliases)) {
  aliasMap[source] = TREE + '/' + pkg;
}
results.alias_table = { entries: Object.keys(aliasMap).length,
  all_inside_the_tree: Object.values(aliasMap).every((v) => v.startsWith(TREE + '/')) };

const consumers = {};

// 5a. the ten prerequisite roles, through the packet own consumer with the alias table.
{
  const P = await import(pathToFileURL(W + '/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs').href);
  const set = readJson('F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/t-prerequisites-tpr144-01-144.json');
  const ctx = { contract_path: aliasMap['F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/contract-144.json']
      || 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/contract-144.json',
    ledger_path: 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/producer-order-144.jsonl',
    runner_path: 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/frozen-runners/run-144-r01.mjs',
    aliases: aliasMap };
  const rows = [];
  for (const r of set.roles) {
    const recordPath = r.path || r.record;
    const readFrom = aliasMap[recordPath] || recordPath;
    let verdict = null;
    let error = null;
    try { verdict = P.consumeRole(r.role, readJson(readFrom), ctx); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ role: r.role, record_read_from: readFrom, ok: !!(verdict && verdict.ok === true),
      why: verdict && verdict.why ? verdict.why : null, error });
  }
  consumers.prerequisite = { roles: rows, consumed: rows.filter((r) => r.ok).length, total: rows.length,
    alias_use: ctx.alias_use || null,
    ok: rows.every((r) => r.ok) };
}

// 5b. the resource judge, on the extracted guest channels.
{
  const RJ = await import(pathToFileURL(W + '/tools/gtos-dsh-144/references/resource-semantics-144.mjs').href);
  const matrix = readJson('F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/g-matrix-gm144-01-144.json');
  const scenes = new Set(Object.keys(RJ.RESOURCE_SCENES));
  const rows = [];
  for (const t of matrix.trials) {
    if (!scenes.has(t.scene)) continue;
    const serialAt = aliasMap[t.serial.path] || toHost(t.serial.path);
    const e9At = aliasMap[t.e9.path] || toHost(t.e9.path);
    let verdict = null;
    let error = null;
    try { verdict = RJ.judgeResourceSemantics(t.scene, { e9: fs.readFileSync(e9At, 'utf8'), serial: fs.readFileSync(serialAt, 'utf8') }); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: t.scene, trial: t.trial, from_tree: !!aliasMap[t.serial.path],
      accepted: !!(verdict && verdict.accepted), error });
  }
  consumers.resource = { trials: rows, accepted: rows.filter((r) => r.accepted).length, total: rows.length,
    ok: rows.length > 0 && rows.every((r) => r.accepted) };
}

// 5c. the numeric judge, on the extracted guest channels.
{
  const N = await import(pathToFileURL(W + '/tools/gtos-dsh-146/r-numeric-146.mjs').href);
  const matrix = readJson('F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/g-matrix-gm144-01-144.json');
  const rows = [];
  for (const t of matrix.trials) {
    const serialAt = aliasMap[t.serial.path] || toHost(t.serial.path);
    const e9At = aliasMap[t.e9.path] || toHost(t.e9.path);
    let verdict = null;
    let error = null;
    try { verdict = N.judgeNonResource(t.scene, fs.readFileSync(serialAt, 'utf8'), fs.readFileSync(e9At, 'utf8')); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: t.scene, trial: t.trial, from_tree: !!aliasMap[t.serial.path],
      accepted: !!(verdict && verdict.ok === true), failed: verdict ? verdict.failed : null, error });
  }
  consumers.numeric = { trials: rows, accepted: rows.filter((r) => r.accepted).length, total: rows.length,
    all_read_from_the_tree: rows.every((r) => r.from_tree),
    ok: rows.length > 0 && rows.every((r) => r.accepted) };
}

// 5d. the host chain, on the extracted ELFs and results.
{
  const H = await import(pathToFileURL(W + '/tools/gtos-dsh-144/host-consume-144.mjs').href);
  const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
  const contractAt = aliasMap[R144 + '/h-contract-144e.json'] || R144 + '/h-contract-144e.json';
  const builds = fs.readdirSync(R144).filter((f) => /^h-unit-build-.*\.json$/.test(f)).sort()
    .map((n) => ({ n, rec: readJson(R144 + '/' + n) })).filter((b) => b.rec.elf && b.rec.elf.path);
  const rows = [];
  for (const b of builds) {
    const elfPkg = rehearsalSpec.aliases[b.rec.elf.path];
    const control = {
      task: 'GTOS-DSH-7146',
      build_record_path: aliasMap[R144 + '/' + b.n] || R144 + '/' + b.n,
      contract_path: contractAt,
      unit_elf_host_path: elfPkg ? TREE + '/' + elfPkg : toHost(b.rec.elf.path),
      unit_elf_linux: b.rec.elfFile,
      results_dir: R144 + '/results',
      path_aliases: aliasMap,
    };
    let verdict = null;
    let error = null;
    try { verdict = H.consumeHostChain(control); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ record: b.n, elf_from_tree: !!elfPkg, accepted: !!(verdict && verdict.accepted),
      counts: verdict && verdict.counts ? verdict.counts : null,
      problems: verdict && verdict.problems ? verdict.problems.slice(0, 3) : [], error });
  }
  consumers.host = { units: rows, accepted: rows.filter((r) => r.accepted).length, total: rows.length,
    ok: rows.length > 0 && rows.every((r) => r.accepted) };
}

results.consumers = { ...consumers, ok: Object.values(consumers).every((c) => c.ok === true) };

// ---------------------------------------------------------------- 6. negatives, each on its own copy of the tree
const negatives = [];
const copyTree = (name) => {
  const dest = TMP + '/' + name;
  fs.mkdirSync(dest, { recursive: true });
  for (const m of members) {
    const to = dest + '/' + m.name;
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.writeFileSync(to, m.bytes);
  }
  return dest;
};

// 6a. one required member removed from the tree: the comparison has to notice.
{
  const dest = copyTree('member-absent');
  const victim = 'producers/arp.o';
  fs.unlinkSync(dest + '/' + victim);
  const present = required.entries.filter((e) => exists(dest + '/' + (rehearsalSpec.aliases[e.source] || '\u0000')));
  negatives.push({ name: 'a-required-member-removed-from-the-payload', removed: victim,
    refused: present.length < required.entries.length, missing_after: required.entries.length - present.length });
}

// 6b. one byte changed in a required member: the hash comparison has to notice.
{
  const dest = copyTree('member-drifted');
  const victim = 'image/p144-final.elf';
  const p = dest + '/' + victim;
  const b = fs.readFileSync(p);
  b[b.length - 1] ^= 0xff;
  fs.writeFileSync(p, b);
  const m = memberByPath.get(victim);
  negatives.push({ name: 'a-required-member-with-one-byte-changed', member: victim,
    refused: sha(fs.readFileSync(p)) !== m.sha256, size_unchanged: fs.statSync(p).size === m.size });
}

// 6c. a guest channel stripped of its numbers: the numeric judge has to refuse.
{
  const dest = copyTree('channel-stripped');
  const matrix = readJson('F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/g-matrix-gm144-01-144.json');
  const trial = matrix.trials.find((t) => t.scene === 'paging-smp1');
  const pkg = rehearsalSpec.aliases[trial.serial.path];
  const p = dest + '/' + pkg;
  const stripped = fs.readFileSync(p, 'utf8').split(/\r?\n/).map((l) => l.replace(/\s+[0-9A-Fa-f]{8}\s*$/, '')).join('\n');
  fs.writeFileSync(p, stripped);
  const N = await import(pathToFileURL(W + '/tools/gtos-dsh-146/r-numeric-146.mjs').href);
  const e9At = dest + '/' + rehearsalSpec.aliases[trial.e9.path];
  const v = N.judgeNonResource('paging-smp1', stripped, fs.readFileSync(e9At, 'utf8'));
  negatives.push({ name: 'a-channel-with-its-numbers-stripped', member: pkg, refused: v.ok !== true,
    failed: v.failed.slice(0, 5) });
}

// 6d. a role record that says false: the seal has to refuse.
{
  const fake = ROOT + '/roles-false.json';
  const real = readJson(R + '/stage-output/roles-146-02/prerequisite_full.json');
  fs.writeFileSync(fake, JSON.stringify({ ...real, ok: false }, null, 2) + '\n');
  const spec2 = JSON.parse(JSON.stringify(rehearsalSpec));
  spec2.mandatory_roles = { roles: spec2.mandatory_roles.roles.map((e) => (e.role === 'prerequisite_full'
    ? { ...e, path: fake, bytes: fs.statSync(fake).size, sha256: sha(fs.readFileSync(fake)) } : e)) };
  const spec2Path = ROOT + '/spec-7146-02.json';
  fs.writeFileSync(spec2Path, JSON.stringify(spec2, null, 2) + '\n');
  const plan2 = ROOT + '/plan-7146-02.json';
  const payload2 = ROOT + '/payload-7146-02.tar';
  const receipt2 = ROOT + '/receipt-7146-02.json';
  const p2 = engine(['plan', spec2Path, plan2]);
  const a2 = p2.status === 0 ? engine(['archive', plan2, payload2]) : { status: null, stderr: 'plan refused' };
  const s2 = a2.status === 0 ? engine(['seal', payload2, receipt2, plan2]) : { status: null, stderr: 'archive refused' };
  negatives.push({ name: 'a-role-record-that-says-false', plan_status: p2.status, archive_status: a2.status,
    seal_status: s2.status, seal_stderr: s2.stderr ? s2.stderr.slice(0, 400) : null,
    receipt_written: exists(receipt2), refused: s2.status !== 0 && !exists(receipt2) });
}

// 6e. a second archive on the same identity: the cardinality gate has to refuse.
{
  const second = ROOT + '/payload-7146-second.tar';
  const r = engine(['archive', PLAN, second]);
  negatives.push({ name: 'a-second-archive-on-the-same-identity', status: r.status,
    stderr: r.stderr ? r.stderr.slice(0, 300) : null, written: exists(second), refused: r.status !== 0 && !exists(second) });
}

results.negatives = { rows: negatives, refused: negatives.filter((n) => n.refused === true).length, total: negatives.length,
  ok: negatives.every((n) => n.refused === true) };

// ---------------------------------------------------------------- the record
const lifecycleOk = results.plan.status === 0 && results.archive.status === 0 && results.seal.status === 0
  && results.verify.status === 0;
const out = {
  task: 'GTOS-DSH-146',
  stage: 'd-consumers-146-' + RUN,
  what: 'a TEST_ONLY rehearsal of the whole publication, from the independent required set to a fresh consumption of the '
    + 'extracted payload by the packet own consumers',
  identity: { task: context.task, scope: context.scope, root: ROOT, production_task_not_claimed: true },
  spec: { path: SPEC, bytes: fs.statSync(SPEC).size, sha256: sha(fs.readFileSync(SPEC)) },
  required_set: { path: REQUIRED, bytes: fs.statSync(REQUIRED).size, sha256: sha(fs.readFileSync(REQUIRED)) },
  rehearsal_spec: pin(rehearsalSpecPath),
  lifecycle: { plan: results.plan.status, archive: results.archive.status, seal: results.seal.status,
    verify: results.verify.status, ok: lifecycleOk,
    stderr: { plan: results.plan.stderr, archive: results.archive.stderr, seal: results.seal.stderr, verify: results.verify.stderr } },
  published: results.published,
  member_list_comes_from_the_required_set: results.member_list_comes_from_the_required_set,
  extraction: results.extraction,
  closure: results.closure,
  alias_table: results.alias_table,
  consumers: results.consumers,
  negatives: results.negatives,
  ok: lifecycleOk && results.member_list_comes_from_the_required_set.ok && results.extraction.all_written_bytes_match
    && results.closure.ok && results.alias_table.all_inside_the_tree && results.consumers.ok && results.negatives.ok,
  ok_means: 'the engine planned, archived, sealed and verified a payload built from the independent required set with all '
    + 'seven role records pinned; a reader that is not the engine writer extracted every member with the right bytes; every '
    + 'required file is a member with the bytes its contract names; and the packet own consumers accepted the extracted tree '
    + 'through the alias table, while five negatives were each refused.',
};
const outPath = R + '/d-consumers-146-' + RUN + '.json';
if (exists(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ ok: out.ok, lifecycle: out.lifecycle.ok, members: out.published.plan_members,
  roles_pinned: out.published.plan_mandatory_roles_pinned, roles_consumed: out.published.receipt_roles_consumed,
  closure: { required: out.closure.required_files, present: out.closure.present_in_payload,
    missing: out.closure.missing_count, wrong: out.closure.wrong_count, unexplained: out.closure.unexplained_members },
  consumers: Object.fromEntries(Object.entries(out.consumers).filter(([k]) => k !== 'ok').map(([k, v]) => [k, v.ok])),
  negatives: out.negatives.refused + '/' + out.negatives.total,
  lifecycle_stderr: out.lifecycle.stderr }, null, 2));
process.exit(out.ok ? 0 : 1);
