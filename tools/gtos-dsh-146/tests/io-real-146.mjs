// GTOS-DSH-146 IO: real per-consumer traces over the extracted sealed payload.
//
// 145's finding was that "member identity hashed all 244, and the trace's exists/stat/open also populated allRead, so
// required_not_read zero does not prove semantic consumption by the owning consumer". The classifier this packet already has
// separates the tiers; what was missing was running it on the REAL consumers reading the REAL package instead of on a
// fixture. That is all this does.
//
// Four families, four processes, four traces. Each process is the packet's own consumer module under `--require
// io-trace-146.cjs`, reading the tree the sealed payload extracts to. Nothing is graded in the process being watched: the
// classifier runs here, afterwards, over the trace file.
//
// What is then reported, per requirement member:
//
//   tier        the best of semantic_read / hash_only / metadata_only / absent across the four traces
//   owner       which family produced that tier
//   semantic    whether the member's role requires a parse, and whether one was attributed to it
//
// And the negatives, which are the point: a trace with a fabricated row, a truncated trace, a read attributed to an unknown
// descriptor, an undeclared read outside the package, and a hash-only member presented as semantic. Each has to be refused.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const W = R + '/worktree-146';
const NODE = 'C:/Program Files/nodejs/node.exe';
const RUN = process.argv[2] || '01';
const SPEC = R + '/spec-146-02.json';
const REQUIRED = R + '/d-required-146-02.json';
const FRESH = R + '/fresh-146-01.json';
const TRACER = W + '/tools/gtos-dsh-146/io-trace-146.cjs';
const CONSUMER = W + '/tools/gtos-dsh-146/tests/io-real-146-consumer.mjs';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => fs.existsSync(p);

const ROOT = R + '/test-only/io-real-146-' + RUN;
if (exists(ROOT)) throw new Error('refused, exists: ' + ROOT);
fs.mkdirSync(ROOT + '/traces', { recursive: true });

const fresh = readJson(FRESH);
if (fresh.ok !== true) throw new Error('the sealed payload was not consumed successfully, so there is no tree to trace');
const TREE = fresh.extraction.tree;
if (!exists(TREE)) throw new Error('the extracted tree is gone: ' + TREE);

const spec = readJson(SPEC);
const required = readJson(REQUIRED);
const IO = await import(pathToFileURL(W + '/tools/gtos-dsh-146/io-semantic-146.mjs').href);

// ---------------------------------------------------------------- the alias table, both spellings
const aliasMap = {};
const linuxForm = (p) => {
  const s = String(p);
  const m = /^\/\/wsl\$\/([^/]+)(\/.*)$/.exec(s);
  if (m) return m[2];
  const d = /^([A-Za-z]):\/(.*)$/.exec(s);
  if (d) return '/mnt/' + d[1].toLowerCase() + '/' + d[2];
  return null;
};
for (const [source, pkg] of Object.entries(spec.aliases)) {
  aliasMap[source] = TREE + '/' + pkg;
  const alt = linuxForm(source);
  if (alt && aliasMap[alt] === undefined) aliasMap[alt] = TREE + '/' + pkg;
}
const aliasPath = ROOT + '/alias-map.json';
fs.writeFileSync(aliasPath, JSON.stringify(aliasMap, null, 2) + '\n');

// ---------------------------------------------------------------- the four families
const FAMILIES = ['prerequisite', 'resource', 'numeric', 'host'];
const runs = [];
for (const family of FAMILIES) {
  const tracePath = ROOT + '/traces/' + family + '.jsonl';
  const outPath = ROOT + '/traces/' + family + '-result.json';
  const r = spawnSync(NODE, ['--require', TRACER, CONSUMER, family, TREE, aliasPath, outPath], {
    encoding: 'utf8', windowsHide: true, maxBuffer: 64 * 1024 * 1024,
    env: { ...process.env, GTOS_IO_TRACE: tracePath, GTOS_IO_CONSUMER: family, GTOS_IO_ROLE: family },
  });
  let result = null;
  try { result = readJson(outPath); } catch { result = null; }
  runs.push({ family, status: r.status, trace: tracePath, result_path: outPath,
    stdout: (r.stdout || '').trim().slice(0, 400), stderr: (r.stderr || '').trim().slice(0, 1200),
    accepted: result ? result.accepted : null, total: result ? result.total : null,
    consumer_ok: result ? result.ok : null,
    trace_rows: exists(tracePath) ? fs.readFileSync(tracePath, 'utf8').split(/\r?\n/).filter(Boolean).length : 0 });
}

// ---------------------------------------------------------------- the requirement members, as tree paths
const memberRows = [];
for (const m of spec.members) {
  const treePath = TREE + '/' + m.path;
  memberRows.push({ path: treePath, package_path: m.path, role: m.role, sha256: m.sha256, bytes: m.bytes,
    contract_path: m.contract_path || m.from });
}
// The declared external set: what a consumer may read that this payload does not carry. Named individually rather than by
// prefix, because a prefix is exactly how a whole-tree excuse gets in.
// Named, and named narrowly. The 144 root is where the ROLE RECORDS and the frozen evidence live -- the statements being
// consumed, which are not payload members and never were. The runtime and the two 144 stage logs are declared one directory
// at a time rather than by naming the whole view root: declaring the view root would excuse a payload material being read
// from the original file, which is the exact substitution this record exists to detect.
const EXTERNAL = [
  { path: 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a',
    why: 'the 144 role records, contract, ledger and frozen evidence: the statements this packet consumes, frozen before it existed' },
  { path: 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a',
    why: 'this packet own spec, plan and records' },
  { path: '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-144-e759-finality-view/runtime',
    why: 'the runtime the packet pins as external: 206 files, not carried by the payload' },
  { path: '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-144-e759-finality-view/logs',
    why: 'the 144 stage inner report and raw channel: execution evidence of a stage, not a payload material' },
  { path: 'F:/GTOS-Chromium/artifacts/native-guarded-frame-recovery-repeatability-and-portable-consumption-20261027-a',
    why: 'the upstream 140 packet whose source and profile the 144 contract names' },
  { path: 'F:/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a',
    why: 'the capture guard the 144 runner names' },
  { path: '//wsl$/Ubuntu-20.04/usr/bin', why: 'the compiler, assembler and linker the tooling subject names' },
  { path: 'C:/Program Files/nodejs', why: 'the interpreter running the consumers' },
];

const EXTERNAL_PATHS = EXTERNAL.map((e) => e.path);
const classified = [];
for (const run of runs) {
  if (!exists(run.trace)) { classified.push({ family: run.family, error: 'no trace was written' }); continue; }
  const c = IO.classify(run.trace, { required: memberRows, packageRoot: TREE, external: EXTERNAL_PATHS });
  classified.push({ family: run.family, trace: c.trace, required: c.required, by_tier: c.by_tier,
    semantic: c.semantic, not_semantic: c.not_semantic, outside_reads: c.outside_reads.length,
    outside_reads_undeclared: c.outside_reads.filter((o) => !o.allowed).length,
    fabricated: c.fabricated.length, refusals: c.refusals, ok: c.ok, members: c.members });
}

// ---------------------------------------------------------------- per member, the best tier across the families
const TIER_RANK = { absent: 0, metadata_only: 1, read_wrong_bytes: 2, hash_only: 3, semantic_read: 4 };
const best = new Map();
for (const c of classified) {
  if (!c.members) continue;
  for (const m of c.members) {
    const prev = best.get(m.path);
    if (!prev || TIER_RANK[m.tier] > TIER_RANK[prev.tier]) {
      best.set(m.path, { tier: m.tier, owner: c.family, reads: m.reads, reads_with_the_required_hash: m.reads_with_the_required_hash,
        parses_attributed: m.parses_attributed, metadata_operations: m.metadata_operations });
    }
  }
}

// What each role's consumption has to be. A record-like member is consumed when it is parsed; a byte artifact is consumed
// when its exact bytes are read by the consumer that owns it, because byte identity IS its whole obligation; a support module
// is the code that ran, so no read of it is expected and none is claimed.
const SEMANTIC_ROLES = ['guest_trial_record', 'record', 'context', 'host_result', 'host_inner'];
const BYTE_ROLES = ['producer_object', 'source_view', 'depfile', 'dep_source', 'image', 'image_map', 'host_unit_elf',
  'host_raw', 'host_build_input', 'host_seam', 'guest_channel'];
const DECLARED_NOT_READ_ROLES = ['support_module'];

// The requirement members are the ones the contracts derive. The packet's own context and record files are members too, but
// they are not requirements: counting them as satisfied-by-consumption would inflate the closure, and counting them as
// unsatisfied would report a gap where none exists. They are reported in their own block, and a record is checked against
// the seal receipt, which is the thing that actually consumed it.
const requirementTreePaths = new Set();
for (const e of required.entries) {
  const pkg = spec.aliases[e.source];
  if (pkg) requirementTreePaths.add((TREE + '/' + pkg).replace(/\\/g, '/').toLowerCase());
}
const receipt = readJson(R + '/archive-146/receipt-146.json');
const consumedByTheSeal = new Set();
for (const e of receipt.evidence_consumed || []) consumedByTheSeal.add(String(e.path).replace(/\\/g, '/').toLowerCase());
for (const e of (receipt.mandatory_roles && receipt.mandatory_roles.consumed) || []) {
  consumedByTheSeal.add(String(e.path).replace(/\\/g, '/').toLowerCase());
}
const requirementRows = memberRows.filter((m) => requirementTreePaths.has(m.path.replace(/\\/g, '/').toLowerCase()));
const packetRecordRows = memberRows.filter((m) => !requirementTreePaths.has(m.path.replace(/\\/g, '/').toLowerCase()));

const perMember = requirementRows.map((m) => {
  const b = best.get(m.path) || { tier: 'absent', owner: null, reads: 0, reads_with_the_required_hash: 0, parses_attributed: 0 };
  let requiredTier;
  if (SEMANTIC_ROLES.includes(m.role)) requiredTier = 'semantic_read';
  else if (BYTE_ROLES.includes(m.role)) requiredTier = 'byte_read';
  else if (DECLARED_NOT_READ_ROLES.includes(m.role)) requiredTier = 'executed_not_read';
  else requiredTier = 'unknown_role';
  const satisfied = requiredTier === 'semantic_read' ? b.tier === 'semantic_read'
    : requiredTier === 'byte_read' ? (b.tier === 'semantic_read' || b.tier === 'hash_only')
    : requiredTier === 'executed_not_read' ? true
    : false;
  return { package_path: m.package_path, role: m.role, required_tier: requiredTier, observed_tier: b.tier,
    owner: b.owner, reads: b.reads, reads_with_the_required_hash: b.reads_with_the_required_hash,
    parses_attributed: b.parses_attributed, satisfied };
});

const byRole = {};
for (const m of perMember) {
  const r = (byRole[m.role] = byRole[m.role] || { members: 0, satisfied: 0, tiers: {}, required_tier: m.required_tier });
  r.members += 1;
  if (m.satisfied) r.satisfied += 1;
  r.tiers[m.observed_tier] = (r.tiers[m.observed_tier] || 0) + 1;
}
const unsatisfied = perMember.filter((m) => !m.satisfied);
const packetRecords = packetRecordRows.map((m) => {
  const b = best.get(m.path) || { tier: 'absent', owner: null };
  // A context record is carried so a reader of the package can see the identity the packet ran under; nothing consumes it as
  // data, and this record says so rather than inventing an owner for it.
  const requiredTier = m.role === 'record' ? 'consumed_by_the_seal' : 'carried_for_the_reader';
  const satisfied = requiredTier === 'consumed_by_the_seal' ? consumedByTheSeal.has(m.contract_path.toLowerCase()) : true;
  return { package_path: m.package_path, role: m.role, required_tier: requiredTier, observed_tier: b.tier,
    satisfied };
});
const packetRecordsUnsatisfied = packetRecords.filter((m) => !m.satisfied);

// ---------------------------------------------------------------- negatives, on copies of the real traces
const negatives = [];
const copyTrace = (family, name, mutate) => {
  const src = ROOT + '/traces/' + family + '.jsonl';
  const rows = fs.readFileSync(src, 'utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
  const out = mutate(rows.slice());
  const p = ROOT + '/traces/neg-' + name + '.jsonl';
  fs.writeFileSync(p, out.map((r) => JSON.stringify(r)).join('\n') + '\n');
  return p;
};
const classifyNeg = (p) => IO.classify(p, { required: memberRows, packageRoot: TREE, external: EXTERNAL_PATHS });

{
  const p = copyTrace('numeric', 'fabricated-row', (rows) => {
    rows.push({ consumer: 'numeric', op: 'readFileSync', via: 'path', path: TREE + '/guest/channels/does-not-exist.log',
      fd_attributed: true, bytes: 128, sha256: sha(Buffer.from('x')) });
    return rows;
  });
  const c = classifyNeg(p);
  negatives.push({ name: 'a-trace-row-for-a-file-that-is-not-there', refused: !c.ok,
    obligations: c.refusals.map((r) => r.obligation) });
}
{
  const p = copyTrace('numeric', 'truncated', (rows) => rows.filter((r) => r.op !== 'trace-closed'));
  const c = classifyNeg(p);
  negatives.push({ name: 'a-trace-that-was-truncated', refused: !c.ok,
    obligations: c.refusals.map((r) => r.obligation) });
}
{
  const p = copyTrace('prerequisite', 'unknown-descriptor', (rows) => {
    rows.push({ consumer: 'prerequisite', op: 'readFileSync', via: 'fd', path: 'fd:99', fd_attributed: false,
      bytes: 64, sha256: null });
    return rows;
  });
  const c = classifyNeg(p);
  negatives.push({ name: 'a-read-attributed-to-an-unknown-descriptor', refused: !c.ok,
    obligations: c.refusals.map((r) => r.obligation) });
}
{
  const p = copyTrace('host', 'undeclared-outside-read', (rows) => {
    rows.push({ consumer: 'host', op: 'readFileSync', via: 'path', path: 'C:/Windows/System32/config/SAM',
      fd_attributed: true, bytes: 1024, sha256: null });
    return rows;
  });
  const c = classifyNeg(p);
  negatives.push({ name: 'a-read-outside-the-package-that-was-not-declared', refused: !c.ok,
    obligations: c.refusals.map((r) => r.obligation) });
}
{
  // The one the review is about: a member that was read and hashed but whose parse rows are gone must not still count as
  // semantic. The rows removed are the parses attributable to one channel file, so that member drops from semantic_read to
  // hash_only and the semantic requirement has to fail on it.
  const victimPkg = perMember.find((m) => m.role === 'guest_trial_record' && m.observed_tier === 'semantic_read');
  if (!victimPkg) {
    negatives.push({ name: 'a-hash-only-member-presented-as-semantic', refused: false,
      why: 'no member was observed as semantic_read, so the case could not be constructed' });
  } else {
    const victim = TREE + '/' + victimPkg.package_path;
    const victimBytes = fs.readFileSync(victim);
    const victimHash = sha(victimBytes);
    const family = victimPkg.owner;
    // The classifier attributes a parse to a file either by exact hash or by "the parse input is a substring of the file's
    // bytes", and the second rule is what catches a record read through a BOM-stripping reader: the parsed string is not the
    // file's bytes, so its hash is not the file's hash. Removing only the exact-hash rows therefore removed nothing, and the
    // case silently failed to construct. Both rules are honoured here, so the rows removed are the rows the classifier would
    // have counted.
    const containsBytes = (hay, needle) => {
      if (!needle.length) return false;
      const i = hay.indexOf(needle);
      return i >= 0;
    };
    const p = copyTrace(family, 'hash-only', (rows) => rows.filter((r) => {
      if (r.op !== 'json.parse') return true;
      if (r.sha256 === victimHash) return false;
      const prefix = Buffer.from(String(r.prefix || ''), 'utf8');
      return !containsBytes(victimBytes, prefix);
    }));
    const c = classifyNeg(p);
    const row = c.members.find((m) => String(m.path).replace(/\\/g, '/').toLowerCase() === victim.replace(/\\/g, '/').toLowerCase());
    negatives.push({ name: 'a-member-read-and-hashed-but-not-parsed', member: victimPkg.package_path, family,
      tier_after: row ? row.tier : null, still_semantic: !!(row && row.tier === 'semantic_read'),
      refused: !!(row && row.tier !== 'semantic_read') });
  }
}

// ---------------------------------------------------------------- the record
const familiesOk = runs.every((r) => r.consumer_ok === true);
const tracesOk = classified.every((c) => c.refusals && c.refusals.length === 0);
const out = {
  task: 'GTOS-DSH-146',
  stage: 'io-real-146-' + RUN,
  what: 'the four consumer families run under the IO tracer against the tree the sealed payload extracts to, classified per '
    + 'requirement member into semantic / byte / metadata / absent, with five negatives on copies of the real traces',
  tree: { path: TREE, from: { path: FRESH, sha256: sha(fs.readFileSync(FRESH)) },
    payload: fresh.sealed_transaction ? fresh.sealed_transaction.payload : null },
  tracer: { path: TRACER, bytes: fs.statSync(TRACER).size, sha256: sha(fs.readFileSync(TRACER)) },
  classifier: { path: W + '/tools/gtos-dsh-146/io-semantic-146.mjs', bytes: fs.statSync(W + '/tools/gtos-dsh-146/io-semantic-146.mjs').size,
    sha256: sha(fs.readFileSync(W + '/tools/gtos-dsh-146/io-semantic-146.mjs')) },
  runs: runs.map((r) => ({ family: r.family, status: r.status, accepted: r.accepted, total: r.total,
    consumer_ok: r.consumer_ok, trace_rows: r.trace_rows, trace: r.trace })),
  traces: classified.map((c) => ({ family: c.family, rows: c.trace ? c.trace.rows : null,
    opened: c.trace ? c.trace.opened : null, closed: c.trace ? c.trace.closed : null,
    reads: c.trace ? c.trace.reads : null, metadata_operations: c.trace ? c.trace.metadata_operations : null,
    parses: c.trace ? c.trace.parses : null, by_tier: c.by_tier || null, semantic: c.semantic,
    outside_reads: c.outside_reads, outside_reads_undeclared: c.outside_reads_undeclared,
    fabricated: c.fabricated, refusals: c.refusals || [] })),
  members: {
    total: perMember.length,
    satisfied: perMember.length - unsatisfied.length,
    unsatisfied: unsatisfied.slice(0, 20).map((m) => ({ path: m.package_path, role: m.role, required: m.required_tier,
      observed: m.observed_tier, owner: m.owner })),
    unsatisfied_count: unsatisfied.length,
    by_role: byRole,
  },
  packet_records: {
    total: packetRecords.length,
    satisfied: packetRecords.length - packetRecordsUnsatisfied.length,
    unsatisfied: packetRecordsUnsatisfied.map((m) => ({ path: m.package_path, role: m.role, required: m.required_tier })),
    by_role: packetRecords.reduce((acc, m) => { acc[m.role] = (acc[m.role] || 0) + 1; return acc; }, {}),
    ok_means: 'every record the packet carries beyond the required set was consumed by the seal, and every context record is '
      + 'carried for a reader rather than claimed as consumed by a consumer.',
  },
  declared_external: EXTERNAL,
  negatives: { rows: negatives, refused: negatives.filter((n) => n.refused === true).length, total: negatives.length,
    ok: negatives.every((n) => n.refused === true) },
  ok: familiesOk && tracesOk && unsatisfied.length === 0 && packetRecordsUnsatisfied.length === 0
    && negatives.every((n) => n.refused === true),
  ok_means: 'all four consumer families accepted the extracted payload; every trace is complete, every row is attributed and '
    + 'none is fabricated or reads outside the package undeclared; every requirement member reaches the tier its role requires; '
    + 'and five ways of making a trace lie are each refused.',
};
const outPath = R + '/io-real-146-' + RUN + '.json';
if (exists(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ ok: out.ok, families: runs.map((r) => r.family + ':' + r.accepted + '/' + r.total),
  members: out.members.satisfied + '/' + out.members.total, unsatisfied: out.members.unsatisfied_count,
  by_role: Object.fromEntries(Object.entries(byRole).map(([k, v]) => [k, v.satisfied + '/' + v.members])),
  negatives: out.negatives.refused + '/' + out.negatives.total,
  traces: out.traces.map((t) => ({ f: t.family, rows: t.rows, reads: t.reads, parses: t.parses, refusals: t.refusals.length })) },
  null, 2));
process.exit(out.ok ? 0 : 1);
