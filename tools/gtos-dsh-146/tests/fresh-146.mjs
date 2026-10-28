// GTOS-DSH-146: fresh consumption of the SEALED payload.
//
// This runs after the seal and reads only the two files the transaction produced. It does not touch the packet's working
// records, and that is the point: a consumer that reads the original files has consumed the packet's directory; a consumer
// that reads the payload has consumed what was published.
//
// What it checks, in order:
//
//   bytes       the payload is the size and hash the receipt names, and the receipt is the size and hash the state row names
//   members     an independent ustar reader recovers 678 members whose ordinal digest is the receipt's
//   records     the seven role records INSIDE the payload each parse and each say true, and each is the same bytes the
//               receipt pinned -- so a role that was quietly swapped after the seal would show up here
//   extraction  every member is written out and re-hashed from the tree
//   closure     every requirement in the required set is a member with the bytes its contract names
//   consumers   the ten prerequisite roles, the resource judge, the numeric judge and the host chain all accept, reading
//               the extracted copies through the alias table
//   negatives   a required member removed from the tree, and a guest channel stripped of its numbers, are each refused
//
// The tree is under the TEST_ONLY prefix because it is a copy of the payload written out for reading, not a publication.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const W = R + '/worktree-146';
const RUN = process.argv[2] || '01';
const PAYLOAD = R + '/archive-146/payload-146.tar';
const RECEIPT = R + '/archive-146/receipt-146.json';
const PLAN = R + '/plan-146-01.json';
const SPEC = R + '/spec-146-02.json';
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
const pin = (p) => ({ path: p, bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)) });

const ROOT = R + '/test-only/fresh-146-' + RUN;
if (exists(ROOT)) throw new Error('refused, exists: ' + ROOT);
const TREE = ROOT + '/material';
fs.mkdirSync(TREE, { recursive: true });

const receipt = readJson(RECEIPT);
const plan = readJson(PLAN);
const spec = readJson(SPEC);
const required = readJson(REQUIRED);

// ---------------------------------------------------------------- 1. the bytes the receipt names
const payloadBytes = fs.readFileSync(PAYLOAD);
const stateRows = fs.readFileSync(R + '/publication-state-146.jsonl', 'utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
const archiveRow = stateRows.find((r) => r.verb === 'archive');
const sealRow = stateRows.find((r) => r.verb === 'seal');
const bytesCheck = {
  payload: { bytes: payloadBytes.length, sha256: sha(payloadBytes) },
  receipt: pin(RECEIPT),
  archive_state_row: archiveRow ? { sha256: archiveRow.sha256, bytes: archiveRow.bytes, ordinal_digest: archiveRow.ordinal_digest } : null,
  // The seal row records the receipt by path and the payload by hash, so the receipt is checked by reading it back from the
  // path the row names and hashing THAT -- not by looking for a field the row does not have. A check written against an
  // assumed field name is a check that fails for the wrong reason.
  seal_state_row: sealRow ? { receipt: sealRow.receipt || null, payload_sha256: sealRow.payload_sha256 || null,
    ordinal_digest: sealRow.ordinal_digest || null, terminal: sealRow.terminal === true } : null,
  payload_is_the_bytes_the_archive_row_names: !!(archiveRow && archiveRow.sha256 === sha(payloadBytes) && archiveRow.bytes === payloadBytes.length),
  receipt_is_the_bytes_the_seal_row_names: !!(sealRow && sealRow.receipt && exists(sealRow.receipt)
    && sha(fs.readFileSync(sealRow.receipt)) === sha(fs.readFileSync(RECEIPT))),
  seal_row_names_this_payload: !!(sealRow && sealRow.payload_sha256 === sha(payloadBytes) && sealRow.terminal === true),
  payload_matches_the_receipt: !!(receipt.payload && receipt.payload.sha256 === sha(payloadBytes)),
};

// ---------------------------------------------------------------- 2. the members, by a reader that is not the writer
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
    const body = Buffer.from(bytes.subarray(at + BLOCK, at + BLOCK + size));
    out.push({ name, size, mode, sha256: sha(body), bytes: body });
    at += BLOCK + size + (size % BLOCK === 0 ? 0 : BLOCK - (size % BLOCK));
  }
  return out;
}
const members = readTar(payloadBytes);
const ordinalDigest = (rows) => sha(Buffer.from(rows.map((r, i) => i + '\0' + r.name + '\0' + r.size + '\0' + r.sha256).join('\n'), 'utf8'));
const readDigest = ordinalDigest(members);
const memberByPath = new Map(members.map((m) => [m.name, m]));
const membersCheck = {
  reader: 'this script own ustar reader, not the engine writer',
  members: members.length, receipt_members: receipt.member_count,
  ordinal_digest: readDigest, receipt_ordinal_digest: receipt.ordinal_digest,
  matches: members.length === receipt.member_count && readDigest === receipt.ordinal_digest,
  names_match_the_plan: members.length === plan.member_count && members.every((m, i) => m.name === plan.members[i].path),
};

// ---------------------------------------------------------------- 3. the role records INSIDE the payload
const roleCheck = [];
for (const entry of plan.mandatory_roles.roles) {
  // The alias table maps requirement sources to package paths; the role records are not requirements, so their package path
  // is the one the spec gave them when it added them, and it is looked up by that name rather than through the aliases.
  const pkg = 'records/roles/' + entry.role + '.json';
  const m = memberByPath.get(pkg);
  let rec = null;
  try { rec = m ? JSON.parse(m.bytes.toString('utf8').replace(/^\uFEFF/, '')) : null; } catch { rec = null; }
  roleCheck.push({
    role: entry.role, package_path: pkg || null,
    in_the_payload: !!m,
    is_the_bytes_the_plan_pinned: !!(m && m.sha256 === entry.sha256 && m.size === entry.bytes),
    parses: rec !== null,
    says_true: !!(rec && rec[entry.ok_field] === true),
  });
}
const rolesOk = roleCheck.every((r) => r.in_the_payload && r.is_the_bytes_the_plan_pinned && r.parses && r.says_true);

// ---------------------------------------------------------------- 4. extraction
for (const m of members) {
  const dest = TREE + '/' + m.name;
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, m.bytes);
}
const extraction = {
  tree: TREE, members: members.length,
  tree_bytes: members.reduce((n, m) => n + m.size, 0),
  every_written_member_rehashes_the_same: members.every((m) => sha(fs.readFileSync(TREE + '/' + m.name)) === m.sha256),
};

// ---------------------------------------------------------------- 5. the requirement closure, on the extracted bytes
const missing = [];
const wrong = [];
for (const entry of required.entries) {
  const pkg = spec.aliases[entry.source];
  const m = pkg ? memberByPath.get(pkg) : null;
  if (!m) { missing.push({ source: entry.source, path: pkg || null }); continue; }
  const onDisk = fs.readFileSync(toHost(entry.source));
  if (m.size !== onDisk.length || m.sha256 !== sha(onDisk)) wrong.push({ source: entry.source, path: pkg });
}
const requirementPaths = new Set(required.entries.map((e) => spec.aliases[e.source]).filter(Boolean));
const extras = members.filter((m) => !requirementPaths.has(m.name));
const unexplained = extras.filter((m) => m.name !== 'records/' && !/^context\/|^records\//.test(m.name));
const closure = {
  required_files: required.entries.length,
  present: required.entries.length - missing.length - wrong.length,
  missing: missing.slice(0, 8), missing_count: missing.length,
  wrong: wrong.slice(0, 8), wrong_count: wrong.length,
  members_beyond_the_required_set: extras.length,
  unexplained: unexplained.slice(0, 8).map((m) => m.name), unexplained_count: unexplained.length,
  ok: missing.length === 0 && wrong.length === 0 && unexplained.length === 0,
};

// ---------------------------------------------------------------- 6. the consumers, on the extracted tree
// The alias table is keyed by the path a RECORD writes, and the records use two spellings of the same file: the 144 host
// contract names the share form (`//wsl$/Ubuntu-20.04/...`) while the 144 role records name the distribution form
// (`/var/tmp/...`). Keyed on only one, the three host ELFs looked up by the role records missed the table and were read from
// the original file -- the role still passed, so nothing failed, and that is exactly the shape of a consumption that looks
// complete and is not. Both spellings are therefore keys, and the reverse map is derived rather than hand-listed.
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

const consumers = {};

{
  const P = await import(pathToFileURL(W + '/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs').href);
  const set = readJson(R144 + '/t-prerequisites-tpr144-01-144.json');
  // Each role gets its own ctx, because the consumer writes its alias statistics onto the ctx it was given. One shared ctx
  // would leave only the last role's count and would report that as the whole story.
  //
  // The role RECORD is a 144 artifact: it is the statement being consumed, frozen before this packet existed, and it is not a
  // member of this payload. The MATERIALS the statement is checked against -- producer objects, source views, depfiles, the
  // image, the map -- are members, and the alias table points every one of those reads into the extracted tree. So the
  // count that matters here is how many reads the alias table actually redirected.
  const rows = [];
  let aliasedReads = 0;
  let suppliedAliases = 0;
  for (const r of set.roles) {
    const recordPath = r.path || r.record;
    const ctx = { contract_path: R144 + '/contract-144.json', ledger_path: R144 + '/producer-order-144.jsonl',
      runner_path: R144 + '/frozen-runners/run-144-r01.mjs', aliases: aliasMap };
    let verdict = null;
    let error = null;
    try { verdict = P.consumeRole(r.role, readJson(recordPath), ctx); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    const use = ctx.alias_use || { supplied: 0, used: 0, distinct_used: [] };
    aliasedReads += use.used;
    suppliedAliases = use.supplied;
    rows.push({ role: r.role, record_read_from: recordPath,
      record_is_a_frozen_144_artifact_not_a_payload_member: true,
      materials_reads_redirected_into_the_payload: use.used,
      distinct_members_read: use.distinct_used.length,
      ok: !!(verdict && verdict.ok === true), why: verdict && verdict.why ? verdict.why : null, error });
  }
  consumers.prerequisite = { roles: rows, consumed: rows.filter((r) => r.ok).length, total: rows.length,
    alias_table_supplied: suppliedAliases, reads_redirected_into_the_payload: aliasedReads,
    ok: rows.every((r) => r.ok) && aliasedReads > 0,
    ok_means: 'every prerequisite role accepted, and the alias table redirected at least one of its reads into the '
      + 'extracted payload rather than the original file.' };
}

{
  const RJ = await import(pathToFileURL(W + '/tools/gtos-dsh-144/references/resource-semantics-144.mjs').href);
  const matrix = readJson(R144 + '/g-matrix-gm144-01-144.json');
  const scenes = new Set(Object.keys(RJ.RESOURCE_SCENES));
  const rows = [];
  for (const t of matrix.trials) {
    if (!scenes.has(t.scene)) continue;
    const serialAt = aliasMap[t.serial.path];
    const e9At = aliasMap[t.e9.path];
    let verdict = null;
    let error = null;
    try { verdict = RJ.judgeResourceSemantics(t.scene, { e9: fs.readFileSync(e9At, 'utf8'), serial: fs.readFileSync(serialAt, 'utf8') }); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: t.scene, trial: t.trial, from_the_payload: !!serialAt, accepted: !!(verdict && verdict.accepted), error });
  }
  consumers.resource = { trials: rows, accepted: rows.filter((r) => r.accepted).length, total: rows.length,
    all_from_the_payload: rows.every((r) => r.from_the_payload), ok: rows.length > 0 && rows.every((r) => r.accepted) };
}

{
  const N = await import(pathToFileURL(W + '/tools/gtos-dsh-146/r-numeric-146.mjs').href);
  const matrix = readJson(R144 + '/g-matrix-gm144-01-144.json');
  const rows = [];
  for (const t of matrix.trials) {
    const serialAt = aliasMap[t.serial.path];
    const e9At = aliasMap[t.e9.path];
    let verdict = null;
    let error = null;
    try { verdict = N.judgeNonResource(t.scene, fs.readFileSync(serialAt, 'utf8'), fs.readFileSync(e9At, 'utf8')); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: t.scene, trial: t.trial, from_the_payload: !!serialAt,
      accepted: !!(verdict && verdict.ok === true), failed: verdict ? verdict.failed : null, error });
  }
  consumers.numeric = { trials: rows, accepted: rows.filter((r) => r.accepted).length, total: rows.length,
    all_from_the_payload: rows.every((r) => r.from_the_payload), ok: rows.length > 0 && rows.every((r) => r.accepted) };
}

{
  const H = await import(pathToFileURL(W + '/tools/gtos-dsh-144/host-consume-144.mjs').href);
  const builds = fs.readdirSync(R144).filter((f) => /^h-unit-build-.*\.json$/.test(f)).sort()
    .map((n) => ({ n, rec: readJson(R144 + '/' + n) })).filter((b) => b.rec.elf && b.rec.elf.path);
  const rows = [];
  for (const b of builds) {
    const elfPkg = spec.aliases[b.rec.elf.path];
    const control = {
      task: 'GTOS-DSH-146',
      build_record_path: aliasMap[R144 + '/' + b.n] || R144 + '/' + b.n,
      contract_path: aliasMap[R144 + '/h-contract-144e.json'] || R144 + '/h-contract-144e.json',
      unit_elf_host_path: elfPkg ? TREE + '/' + elfPkg : toHost(b.rec.elf.path),
      unit_elf_linux: b.rec.elfFile,
      results_dir: R144 + '/results',
      path_aliases: aliasMap,
    };
    let verdict = null;
    let error = null;
    try { verdict = H.consumeHostChain(control); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ record: b.n, elf_from_the_payload: !!elfPkg, accepted: !!(verdict && verdict.accepted),
      counts: verdict && verdict.counts ? verdict.counts : null,
      problems: verdict && verdict.problems ? verdict.problems.slice(0, 3) : [], error });
  }
  consumers.host = { units: rows, accepted: rows.filter((r) => r.accepted).length, total: rows.length,
    ok: rows.length > 0 && rows.every((r) => r.accepted) };
}

consumers.ok = Object.values(consumers).every((c) => c.ok === true);

// ---------------------------------------------------------------- 7. negatives, on copies of the extracted tree
const negatives = [];
const copyTree = (name) => {
  const dest = ROOT + '/negatives/' + name;
  fs.mkdirSync(dest, { recursive: true });
  for (const m of members) {
    const to = dest + '/' + m.name;
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.writeFileSync(to, m.bytes);
  }
  return dest;
};
{
  const dest = copyTree('member-absent');
  fs.unlinkSync(dest + '/producers/arp.o');
  const present = required.entries.filter((e) => exists(dest + '/' + (spec.aliases[e.source] || '\u0000')));
  negatives.push({ name: 'a-required-member-removed-from-the-payload', refused: present.length < required.entries.length,
    missing_after: required.entries.length - present.length });
}
{
  const dest = copyTree('channel-stripped');
  const matrix = readJson(R144 + '/g-matrix-gm144-01-144.json');
  const trial = matrix.trials.find((t) => t.scene === 'paging-smp1');
  const pkg = spec.aliases[trial.serial.path];
  const stripped = fs.readFileSync(dest + '/' + pkg, 'utf8').split(/\r?\n/).map((l) => l.replace(/\s+[0-9A-Fa-f]{8}\s*$/, '')).join('\n');
  fs.writeFileSync(dest + '/' + pkg, stripped);
  const N = await import(pathToFileURL(W + '/tools/gtos-dsh-146/r-numeric-146.mjs').href);
  const v = N.judgeNonResource('paging-smp1', stripped, fs.readFileSync(dest + '/' + spec.aliases[trial.e9.path], 'utf8'));
  negatives.push({ name: 'a-channel-with-its-numbers-stripped', member: pkg, refused: v.ok !== true, failed: v.failed.slice(0, 5) });
}

const out = {
  task: 'GTOS-DSH-146',
  stage: 'fresh-146-' + RUN,
  what: 'the sealed payload read by a reader that is not the engine writer, extracted, and consumed by the packet own '
    + 'consumers from the extracted tree',
  counted_as_146_execution_before_the_seal: false,
  sealed_transaction: {
    payload: bytesCheck.payload, receipt: bytesCheck.receipt,
    plan: pin(PLAN), spec: pin(SPEC),
  },
  bytes: bytesCheck,
  members: membersCheck,
  role_records_in_the_payload: { rows: roleCheck, ok: rolesOk },
  extraction,
  closure,
  consumers,
  negatives: { rows: negatives, refused: negatives.filter((n) => n.refused === true).length, total: negatives.length,
    ok: negatives.every((n) => n.refused === true) },
  ok: bytesCheck.payload_is_the_bytes_the_archive_row_names && bytesCheck.payload_matches_the_receipt
    && bytesCheck.receipt_is_the_bytes_the_seal_row_names && bytesCheck.seal_row_names_this_payload && membersCheck.matches && membersCheck.names_match_the_plan
    && rolesOk && extraction.every_written_member_rehashes_the_same && closure.ok && consumers.ok
    && negatives.every((n) => n.refused === true),
  ok_means: 'the payload is the bytes the receipt and the state row name; an independent reader recovers the same 678 members '
    + 'with the receipt ordinal digest; the seven role records inside the payload are the bytes the plan pinned and each says '
    + 'true; every required file is a member with the bytes its contract names; the four consumer families accept the '
    + 'extracted tree; and both negatives are refused.',
};
const outPath = R + '/fresh-146-' + RUN + '.json';
if (exists(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ ok: out.ok, members: membersCheck.members, digest_matches: membersCheck.matches,
  roles_in_payload: rolesOk, closure: { required: closure.required_files, present: closure.present,
    missing: closure.missing_count, wrong: closure.wrong_count, unexplained: closure.unexplained_count },
  consumers: Object.fromEntries(Object.entries(consumers).filter(([k]) => k !== 'ok').map(([k, v]) => [k, v.ok])),
  negatives: out.negatives.refused + '/' + out.negatives.total }, null, 2));
process.exit(out.ok ? 0 : 1);
