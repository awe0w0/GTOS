// GTOS-DSH-146 F: the publisher, keeping 145's fixed identity and adding the two gates 145 left open.
//
// 145's engine is the one this packet keeps using, and it is not edited: it is a sealed packet's source and re-writing it
// would make the sealed receipt describe a file that no longer exists. This file is the same engine with two additions,
// and both additions are corrections of things 145's own review named as still open.
//
// ADDITION 1 -- archive cardinality.
//
// 145 refused a second archive only when the new payload had the same ordinal digest as one already archived:
//
//     if (before.digests.has(plan.ordinal_digest)) die('this payload has already been archived...')
//
// So a *different* plan over *different* content could be archived into the same state, before any seal, and the state
// would then hold two archives. The taskbook's terminal cardinality is one archive and one seal per task identity. A
// different payload is not a repeat of the first, it is a second publication attempt, and the count is the thing being
// asserted. The check here is therefore on the count, not on the digest: one archive per context, full stop. The digest
// check stays, because it produces a better message for the repeat case, but it is no longer the only gate.
//
// ADDITION 2 -- the seal consumes the mandatory consumer roles.
//
// 145's seal consumed a required set and four records (R/G/F/D-adjacent). Its own review said the seal had no
// qualification gate for the mandatory consumer roles, and that omitting a role was not refused. Here the plan pins
// `mandatory_roles`: one entry per role in the frozen `mandatory-consumer-roles-146.json`, each naming the record that
// role's consumer actually wrote, its bytes, its hash, and the field in it that says it passed. The seal reads every one
// of them and refuses if any is absent, changed, or says anything other than true. The role list itself is pinned by hash,
// so a plan that drops a role is refused by comparing against the frozen list rather than against itself.
//
// What this file still refuses to do: it does not decide whether the payload is correct. It records what was archived and
// seals what the evidence says. The evidence is produced by consumers that run before it, and the seal is the statement
// that they ran on these bytes.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const TASK = 'GTOS-DSH-146';
const TEST_ONLY_PREFIX = R + '/test-only/';

// The identities this engine will accept. They are constants here rather than arguments, because a publisher that takes
// its identity from its caller can be pointed at any identity.
const PINNED = {
  context: {
    path: R + '/publication-context-146.json',
    bytes: 1452,
    sha256: '923ec826b59477c8e05980a7484888011eb1dceee815ca1ceb915f02ed77f1c3',
  },
  authority: {
    path: R + '/authority-146.json',
    bytes: 13364,
    sha256: 'f14bc7c1bdf329bbfd9488167ed9422137bc8d0fb68e0ec95c01e9795ce57a43',
  },
  mandatory_roles: {
    path: R + '/mandatory-consumer-roles-146.json',
    bytes: 951,
    sha256: 'ac731967149ac62589799b02b7c589c69fa0bfebd342d557f6840205117341a1',
  },
};
const PRODUCTION = { task: TASK, root: R, refusal_audit: R + '/publication-refusals-146.jsonl' };

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const BLOCK = 512;
const ZERO = Buffer.alloc(BLOCK);
const same = (a, b) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
const inside = (root, p) => {
  const a = path.resolve(root).toLowerCase();
  const b = path.resolve(p).toLowerCase();
  return b === a || b.startsWith(a.endsWith(path.sep) ? a : a + path.sep);
};

function tagFor(task) {
  const m = /^GTOS-DSH-(\d+)$/.exec(String(task || ''));
  if (!m) die('the task name is not of the form GTOS-DSH-<number>', { task });
  return m[1];
}
const stateNameFor = (task) => 'publication-state-' + tagFor(task) + '.jsonl';
const destNameFor = (task) => 'archive-' + tagFor(task);

// Every refusal goes to a ledger before it exits, so a refused step is a fact on disk rather than a message in a
// terminal. The ledger is append-only and is written before the process exits with a non-zero code.
function die(message, extra) {
  const row = { at: new Date().toISOString(), verb: process.argv[3] || null, context: process.argv[4] || null,
    refused: message, ...(extra || {}) };
  try {
    fs.appendFileSync(process.env.GTOS_REFUSAL_AUDIT || PRODUCTION.refusal_audit, JSON.stringify(row) + '\n');
  } catch { /* a refusal that cannot be logged is still a refusal */ }
  process.stderr.write(JSON.stringify({ refused: message, ...(extra || {}) }, null, 2) + '\n');
  process.exit(2);
}

function readJson(p) {
  if (!fs.existsSync(p)) die('absent: ' + p);
  const bytes = fs.readFileSync(p);
  try { return JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, '')); }
  catch (e) { die('not JSON: ' + p, { detail: String(e.message) }); }
}

function writeNew(p, bytes) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, bytes, { flag: 'wx' });
  return { path: p, bytes: bytes.length, sha256: sha(bytes) };
}

function loadContext(p) {
  if (!p) die('the context path is required: this engine has no other way to know which task it is publishing');
  if (!fs.existsSync(p)) die('absent: ' + p, { what: 'the context' });
  const bytes = fs.readFileSync(p);
  let ctx;
  try { ctx = JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, '')); }
  catch (e) { die('not JSON: ' + p, { detail: String(e.message) }); }
  const got = sha(bytes);

  for (const k of ['task', 'scope', 'authorized_report_root', 'fixed_state_path', 'production_destination']) {
    if (typeof ctx[k] !== 'string' || ctx[k].length === 0) {
      die('the context does not declare ' + k, { context: p, has: Object.keys(ctx) });
    }
  }

  if (ctx.scope === 'production') {
    if (!same(p, PINNED.context.path)) die('this is not the production context the authority names', { got: p, want: PINNED.context.path });
    if (got !== PINNED.context.sha256) die('the production context is not the bytes the authority froze', { want: PINNED.context.sha256, got });
    if (ctx.task !== PRODUCTION.task) die('the production context names another task', { want: PRODUCTION.task, got: ctx.task });
    if (!same(ctx.authorized_report_root, PRODUCTION.root)) {
      die('the production context does not name the authorized report root', { want: PRODUCTION.root, got: ctx.authorized_report_root });
    }
    if (!fs.existsSync(PINNED.authority.path)) die('the authority record is absent', { path: PINNED.authority.path });
    const aBytes = fs.readFileSync(PINNED.authority.path);
    if (aBytes.length !== PINNED.authority.bytes || sha(aBytes) !== PINNED.authority.sha256) {
      die('the authority record is not the bytes the dispatch froze', { path: PINNED.authority.path,
        want: PINNED.authority.sha256, got: sha(aBytes) });
    }
    const authority = JSON.parse(aBytes.toString('utf8'));
    if (authority.task !== TASK || !same(authority.report_root, PRODUCTION.root)) {
      die('the authority record does not authorize this root for this task', { task: authority.task, root: authority.report_root });
    }
    ctx.__authority = { path: PINNED.authority.path, bytes: aBytes.length, sha256: sha(aBytes) };
  } else if (ctx.scope === 'test_only') {
    if (ctx.task === TASK) {
      die('a TEST_ONLY context may not claim the production task', { task: ctx.task,
        why: 'renaming the production task as a fixture is the way past a sealed state, so it is refused rather than allowed' });
    }
    if (!inside(TEST_ONLY_PREFIX, p)) die('a TEST_ONLY context must live under the TEST_ONLY prefix', { got: p, prefix: TEST_ONLY_PREFIX });
    if (!inside(TEST_ONLY_PREFIX, ctx.authorized_report_root)) {
      die('a TEST_ONLY context must keep its report root under the TEST_ONLY prefix',
        { got: ctx.authorized_report_root, prefix: TEST_ONLY_PREFIX });
    }
  } else {
    die('the context declares an unknown scope', { scope: ctx.scope, known: ['production', 'test_only'] });
  }

  ctx.statePath = path.join(ctx.authorized_report_root, stateNameFor(ctx.task));
  ctx.destDir = path.join(ctx.authorized_report_root, destNameFor(ctx.task));
  if (!same(ctx.fixed_state_path, ctx.statePath)) {
    die('the context does not name the state file this task and root derive', { declared: ctx.fixed_state_path, derived: ctx.statePath,
      why: 'a caller-supplied ledger cannot reset the transaction, so a context that names a different one is refused' });
  }
  if (!same(ctx.production_destination, ctx.destDir)) {
    die('the context does not name the terminal directory this task and root derive',
      { declared: ctx.production_destination, derived: ctx.destDir,
        why: 'a caller-supplied transaction directory cannot reset the transaction, so a context that names a different one is refused' });
  }

  ctx.__path = p;
  ctx.__bytes = bytes.length;
  ctx.__sha256 = got;
  ctx.__id = sha(Buffer.from(JSON.stringify([ctx.task, ctx.authorized_report_root]), 'utf8'));
  ctx.__refusal_audit = ctx.scope === 'production' ? PRODUCTION.refusal_audit
    : path.join(ctx.authorized_report_root, 'publication-refusals.jsonl');
  return ctx;
}

function requireInside(root, p, what) {
  if (!inside(root, p)) die('the ' + what + ' is outside the directory this task fixes', { got: p, root });
}

function arity(verb, rest, want) {
  if (rest.length !== want) {
    die('the ' + verb + ' verb takes ' + want + ' argument(s) after the context and got ' + rest.length,
      { argv: process.argv.slice(2), why: 'exact arity: there is no position in which a ledger, a transaction or a second root can be named' });
  }
}

function readState(p) {
  if (!fs.existsSync(p)) return { present: false, rows: [], unreadable: 0 };
  const rows = [];
  let unreadable = 0;
  for (const line of fs.readFileSync(p, 'utf8').split(/\r?\n/)) {
    if (!line.trim()) continue;
    try { rows.push(JSON.parse(line)); } catch { unreadable += 1; rows.push({ unreadable: line.slice(0, 200) }); }
  }
  return { present: true, rows, unreadable };
}

function stateFor(ctx) {
  const s = readState(ctx.statePath);
  const mine = s.rows.filter((r) => !r.unreadable && r.task === ctx.task);
  const archives = mine.filter((r) => r.verb === 'archive');
  const seals = mine.filter((r) => r.verb === 'seal');
  return {
    present: s.present, rows: s.rows.length, unreadable: s.unreadable,
    mine: mine.length, archives, seals,
    other_task_rows: s.rows.filter((r) => !r.unreadable && r.task !== ctx.task).length,
    digests: new Set(archives.map((r) => r.ordinal_digest)),
    payloadHashes: new Set(archives.map((r) => r.sha256)),
  };
}

function appendState(ctx, row) {
  fs.mkdirSync(path.dirname(ctx.statePath), { recursive: true });
  fs.appendFileSync(ctx.statePath, JSON.stringify({ at: new Date().toISOString(), task: ctx.task, scope: ctx.scope, ...row }) + '\n');
}

function octal(value, width) {
  const s = Math.max(0, Math.floor(value)).toString(8);
  return s.length >= width ? s.slice(-width) : s.padStart(width, '0');
}

function stringField(value, width) {
  const b = Buffer.from(String(value), 'utf8');
  if (b.length > width) die('a member name is longer than the tar header allows', { name: value, length: b.length, limit: width });
  const out = Buffer.alloc(width);
  b.copy(out);
  return out;
}

function header(name, size, mode) {
  const h = Buffer.alloc(BLOCK);
  stringField(name, 100).copy(h, 0);
  Buffer.from(octal(mode, 7) + '\0', 'utf8').copy(h, 100);
  Buffer.from(octal(0, 7) + '\0', 'utf8').copy(h, 108);
  Buffer.from(octal(0, 7) + '\0', 'utf8').copy(h, 116);
  Buffer.from(octal(size, 11) + '\0', 'utf8').copy(h, 124);
  Buffer.from(octal(0, 11) + '\0', 'utf8').copy(h, 136);
  h.write('        ', 148, 8, 'utf8');
  h.write('0', 156, 1, 'utf8');
  h.write('ustar\0', 257, 6, 'utf8');
  h.write('00', 263, 2, 'utf8');
  let sum = 0;
  for (const b of h) sum += b;
  Buffer.from(octal(sum, 6) + '\0 ', 'utf8').copy(h, 148);
  return h;
}

function pad(size) { const rem = size % BLOCK; return rem === 0 ? Buffer.alloc(0) : Buffer.alloc(BLOCK - rem); }

function readTar(bytes) {
  const members = [];
  let at = 0;
  while (at + BLOCK <= bytes.length) {
    const h = bytes.subarray(at, at + BLOCK);
    if (h.every((b) => b === 0)) break;
    const name = h.subarray(0, 100).toString('utf8').replace(/\0.*$/, '');
    const size = parseInt(h.subarray(124, 136).toString('utf8').replace(/\0.*$/, '').trim() || '0', 8);
    const stored = parseInt(h.subarray(148, 156).toString('utf8').replace(/\0.*$/, '').trim() || '0', 8);
    const copy = Buffer.from(h);
    copy.write('        ', 148, 8, 'utf8');
    let sum = 0;
    for (const b of copy) sum += b;
    if (sum !== stored) die('a tar header does not check out', { name, stored, computed: sum });
    const start = at + BLOCK;
    const body = bytes.subarray(start, start + size);
    if (body.length !== size) die('a member is shorter than its header says', { name, want: size, got: body.length });
    members.push({ name, size, sha256: sha(body) });
    at = start + size + pad(size).length;
  }
  return members;
}

function ordinalDigest(members) {
  return sha(Buffer.from(members.map((m, i) => `${i}\0${m.name}\0${m.size}\0${m.sha256}`).join('\n'), 'utf8'));
}

function checkEvidence(entry, what) {
  if (!entry || typeof entry.path !== 'string' || typeof entry.bytes !== 'number' || typeof entry.sha256 !== 'string'
    || typeof entry.ok_field !== 'string') {
    return { what, ok: false, why: 'the entry does not pin a path, bytes, sha256 and ok_field', entry };
  }
  if (!fs.existsSync(entry.path)) return { what, ok: false, why: 'absent', path: entry.path };
  const bytes = fs.readFileSync(entry.path);
  if (bytes.length !== entry.bytes) return { what, ok: false, why: 'bytes differ', path: entry.path, want: entry.bytes, got: bytes.length };
  if (sha(bytes) !== entry.sha256) return { what, ok: false, why: 'sha256 differs', path: entry.path };
  let rec;
  try { rec = JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, '')); }
  catch (e) { return { what, ok: false, why: 'not JSON', path: entry.path, detail: String(e.message) }; }
  const value = rec[entry.ok_field];
  if (value !== true) {
    return { what, ok: false, why: 'its ' + entry.ok_field + ' is not true', path: entry.path,
      got: value === undefined ? null : value, field_present: Object.prototype.hasOwnProperty.call(rec, entry.ok_field) };
  }
  return { what, ok: true, path: entry.path, bytes: bytes.length, sha256: entry.sha256, ok_field: entry.ok_field };
}

// The mandatory roles, read from the frozen record rather than from the plan. A plan that names six roles would be
// self-consistent; comparing against this list is what makes the seventh missing role a refusal.
function frozenRoles() {
  const b = fs.readFileSync(PINNED.mandatory_roles.path);
  if (b.length !== PINNED.mandatory_roles.bytes || sha(b) !== PINNED.mandatory_roles.sha256) {
    die('the mandatory consumer roles record is not the bytes the dispatch froze', { path: PINNED.mandatory_roles.path,
      want: PINNED.mandatory_roles.sha256, got: sha(b) });
  }
  const rec = JSON.parse(b.toString('utf8'));
  if (!Array.isArray(rec.roles) || rec.roles.length === 0) die('the mandatory roles record names no roles');
  return { roles: rec.roles.slice(), record: { path: PINNED.mandatory_roles.path, bytes: b.length, sha256: sha(b) },
    requires_all: rec.each_role_required === true };
}

// A plan's role coverage: every frozen role present exactly once, and every entry carrying the pin of a record that
// exists. The seal re-checks the pins; this only checks that the plan is shaped like a complete set.
function checkRoleCoverage(pinned, frozen) {
  const problems = [];
  if (!pinned || !Array.isArray(pinned.roles)) {
    return [{ obligation: 'mandatory_roles', why: 'the plan pins no mandatory consumer roles at all' }];
  }
  const named = new Map();
  for (const e of pinned.roles) {
    if (!e || typeof e.role !== 'string') { problems.push({ obligation: 'mandatory_roles', why: 'an entry names no role' }); continue; }
    if (named.has(e.role)) problems.push({ obligation: 'mandatory_roles', why: 'a role is pinned twice', role: e.role });
    named.set(e.role, e);
  }
  for (const role of frozen.roles) {
    if (!named.has(role)) problems.push({ obligation: 'mandatory_roles', why: 'a mandatory role is not pinned', role });
  }
  for (const role of named.keys()) {
    if (!frozen.roles.includes(role)) problems.push({ obligation: 'mandatory_roles', why: 'the plan pins a role the contract does not name', role });
  }
  return problems;
}

// ---------------------------------------------------------------- verbs
const [, , verb, contextPath, ...rest] = process.argv;

if (verb === 'state') {
  arity(verb, rest, 0);
  const ctx = loadContext(contextPath);
  const s = stateFor(ctx);
  const out = {
    task: ctx.task, verb, scope: ctx.scope,
    context: { path: ctx.__path, bytes: ctx.__bytes, sha256: ctx.__sha256, id: ctx.__id },
    identity: { task: ctx.task, root: ctx.authorized_report_root, state: ctx.statePath, terminal: ctx.destDir },
    present: s.present, rows: s.rows, unreadable: s.unreadable, rows_of_this_task: s.mine,
    rows_of_other_tasks: s.other_task_rows,
    archives: s.archives.length, seals: s.seals.length,
    terminal: s.seals.length === 1,
    ok: s.unreadable === 0 && s.archives.length <= 1 && s.seals.length <= 1,
    ok_means: 'the state file parses, and it holds at most one archive and at most one seal for this task',
  };
  process.stdout.write(JSON.stringify(out, null, 2) + '\n');
  process.exit(out.ok ? 0 : 1);
}

if (verb === 'plan') {
  arity(verb, rest, 2);
  const ctx = loadContext(contextPath);
  const [specPath, planOut] = rest;
  requireInside(ctx.authorized_report_root, planOut, 'plan');
  if (fs.existsSync(planOut)) die('CreateNew refused: the output already exists', { path: planOut });
  const spec = readJson(specPath);
  if (spec.task !== ctx.task) die('the spec is for another task than the context names', { want: ctx.task, got: spec.task === undefined ? null : spec.task });
  if (!Array.isArray(spec.members) || spec.members.length === 0) die('the spec lists no members');

  // The role coverage is checked at plan time as well as at seal time. Refusing a plan that could never be sealed is the
  // cheap half of the gate: it fails before a 20 MB payload is written rather than after.
  const frozen = frozenRoles();
  const roleProblems = checkRoleCoverage(spec.mandatory_roles, frozen);
  if (roleProblems.length) die('the spec does not pin a complete set of mandatory consumer roles', { problems: roleProblems });

  const members = [];
  for (const entry of spec.members) {
    if (!fs.existsSync(entry.from)) die('a planned member is absent', { from: entry.from, path: entry.path });
    const bytes = fs.readFileSync(entry.from);
    if (entry.bytes !== undefined && entry.bytes !== bytes.length) {
      die('a planned member is not the size the spec states', { from: entry.from, want: entry.bytes, got: bytes.length });
    }
    const h = sha(bytes);
    if (entry.sha256 !== undefined && entry.sha256 !== h) {
      die('a planned member is not the bytes the spec states', { from: entry.from, want: entry.sha256, got: h });
    }
    members.push({ ordinal: members.length, path: entry.path, role: entry.role, from: entry.from, bytes: bytes.length,
      sha256: h, mode: entry.mode === undefined ? 420 : entry.mode });
  }
  members.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  members.forEach((m, i) => { m.ordinal = i; });
  for (let i = 1; i < members.length; i++) {
    if (members[i].path === members[i - 1].path) {
      die('two members share one path in the payload', { path: members[i].path, from: [members[i - 1].from, members[i].from] });
    }
  }
  const roles = {};
  for (const m of members) roles[m.role] = (roles[m.role] || 0) + 1;
  const plan = {
    task: spec.task,
    what: 'the ordered copy-plan: every member of the payload, its role, its size and its hash, in the order the payload will carry it. '
      + 'This plan does not name the payload, and the payload carries this plan.',
    members, roles,
    total_bytes: members.reduce((n, m) => n + m.bytes, 0),
    member_count: members.length,
    ordinal_digest: ordinalDigest(members.map((m) => ({ name: m.path, size: m.bytes, sha256: m.sha256 }))),
    ordinal_digest_means: 'sha256 over the lines "<ordinal>\\0<path>\\0<size>\\0<sha256>", in the payload order',
    does_not_name_the_payload: true,
    required_set: spec.required_set || null,
    // The qualification gate. One entry per frozen role, each naming the record its consumer wrote and the field in that
    // record which says it passed. The seal reads all of them; a role that is missing, or whose record has drifted, or
    // whose field says anything but true, is a refusal rather than a gap.
    mandatory_roles: { contract: frozen.record, requires_all: frozen.requires_all, roles: spec.mandatory_roles.roles },
    evidence: Array.isArray(spec.evidence) ? spec.evidence : [],
    context: { path: ctx.__path, sha256: ctx.__sha256, id: ctx.__id, scope: ctx.scope },
  };
  const out = writeNew(planOut, JSON.stringify(plan, null, 2) + '\n');
  process.stdout.write(JSON.stringify({ verb, plan: out, member_count: plan.member_count, roles,
    total_bytes: plan.total_bytes, ordinal_digest: plan.ordinal_digest, evidence_pinned: plan.evidence.length,
    required_set_pinned: plan.required_set !== null, mandatory_roles_pinned: plan.mandatory_roles.roles.length,
    mandatory_roles_required: frozen.roles.length }, null, 2) + '\n');
  process.exit(0);
}

if (verb === 'archive') {
  arity(verb, rest, 2);
  const ctx = loadContext(contextPath);
  const [planPath, dest] = rest;
  requireInside(ctx.destDir, dest, 'payload');
  const plan = readJson(planPath);
  if (plan.task !== ctx.task) die('the plan is for another task than the context names', { want: ctx.task, got: plan.task });
  if (plan.context && plan.context.id && plan.context.id !== ctx.__id) {
    die('the plan was frozen under another task identity', { plan_context: plan.context.id, this_context: ctx.__id });
  }
  if (fs.existsSync(dest)) die('CreateNew refused: the output already exists', { path: dest });
  const before = stateFor(ctx);
  if (before.unreadable) die('the state file cannot be read, so no step may be taken on it', { path: ctx.statePath, unreadable: before.unreadable });
  if (before.seals.length) {
    die('this task is sealed, so nothing may be archived after it', { state: ctx.statePath,
      seals: before.seals.length, sealed_payloads: before.seals.map((r) => r.payload_sha256) });
  }
  // CARDINALITY. The taskbook fixes one successful production archive. 145 refused only a payload whose ordinal digest it
  // had already seen, which lets a second, different plan over different content into the same state before the seal. The
  // count is the thing the contract names, so the count is what is checked. The digest check below stays for the message.
  if (before.archives.length) {
    die('this task has already been archived once, and the contract fixes one archive per task identity',
      { state: ctx.statePath, archives: before.archives.length,
        previous: before.archives.map((r) => ({ dest: r.dest, ordinal_digest: r.ordinal_digest, sha256: r.sha256 })),
        attempted: { dest, ordinal_digest: plan.ordinal_digest },
        why: 'a second, different payload is a second publication attempt, not a repeat of the first' });
  }
  if (before.digests.has(plan.ordinal_digest)) {
    die('this payload has already been archived, under another path or name',
      { state: ctx.statePath, ordinal_digest: plan.ordinal_digest, previous: before.archives.map((r) => r.dest) });
  }
  const parts = [];
  for (const m of plan.members) {
    const bytes = fs.readFileSync(m.from);
    if (bytes.length !== m.bytes) die('a member changed size after the plan was frozen', { member: m.path, want: m.bytes, got: bytes.length });
    if (sha(bytes) !== m.sha256) die('a member changed after the plan was frozen', { member: m.path });
    parts.push(header(m.path, bytes.length, m.mode));
    parts.push(bytes);
    parts.push(pad(bytes.length));
  }
  parts.push(ZERO, ZERO);
  const payload = Buffer.concat(parts);
  const expectedLength = plan.members.reduce((n, m) => n + BLOCK + m.bytes + (m.bytes % BLOCK === 0 ? 0 : BLOCK - (m.bytes % BLOCK)), 0)
    + 2 * BLOCK;
  if (payload.length !== expectedLength) die('the payload is not the length its members add up to', { built: payload.length, expected: expectedLength });
  const out = writeNew(dest, payload);
  const back = readTar(fs.readFileSync(dest));
  if (back.length !== plan.members.length) die('the payload does not carry every planned member', { planned: plan.members.length, read: back.length });
  const readDigest = ordinalDigest(back.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 })));
  if (readDigest !== plan.ordinal_digest) die('the payload read back is not the plan', { want: plan.ordinal_digest, got: readDigest });
  appendState(ctx, { verb: 'archive', dest, ordinal_digest: readDigest, members: back.length, bytes: out.bytes, sha256: out.sha256,
    plan: { path: planPath, bytes: fs.statSync(planPath).size, sha256: sha(fs.readFileSync(planPath)) } });
  process.stdout.write(JSON.stringify({ verb, payload: out, members: back.length, ordinal_digest: readDigest,
    state: ctx.statePath, context_id: ctx.__id, archives_after: 1 }, null, 2) + '\n');
  process.exit(0);
}

if (verb === 'seal') {
  arity(verb, rest, 3);
  const ctx = loadContext(contextPath);
  const [payloadPath, receiptPath, planPath] = rest;
  requireInside(ctx.destDir, receiptPath, 'receipt');
  requireInside(ctx.destDir, payloadPath, 'payload');
  if (!fs.existsSync(payloadPath)) die('absent: ' + payloadPath);
  if (fs.existsSync(receiptPath)) die('CreateNew refused: the output already exists', { path: receiptPath });
  const before = stateFor(ctx);
  if (before.unreadable) die('the state file cannot be read, so it is not safe to seal', { path: ctx.statePath, unreadable: before.unreadable });
  if (before.seals.length) {
    die('this task is already sealed; a second seal is refused whatever it is called and whatever it carries',
      { state: ctx.statePath, seals: before.seals.length, sealed_payloads: before.seals.map((r) => r.payload_sha256) });
  }

  const bytes = fs.readFileSync(payloadPath);
  const members = readTar(bytes);
  if (members.length === 0) die('the payload carries no member', { payload: payloadPath });
  if (!bytes.subarray(-1024).equals(Buffer.concat([ZERO, ZERO]))) {
    die('the payload is not terminated, so it is not complete', { payload: payloadPath, bytes: bytes.length });
  }
  const thisDigest = ordinalDigest(members.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 })));
  const thisHash = sha(bytes);

  const archived = before.archives.find((r) => r.ordinal_digest === thisDigest && r.sha256 === thisHash);
  if (!archived) {
    die('this payload was never archived in this task, so there is nothing to seal',
      { state: ctx.statePath, state_present: before.present, archive_rows: before.archives.length,
        rows_in_this_task: before.mine, rows_of_other_tasks: before.other_task_rows,
        payload_digest: thisDigest, payload_sha256: thisHash, archived_digests: before.archives.map((r) => r.ordinal_digest) });
  }

  const plan = readJson(planPath);
  const planBytes = fs.readFileSync(planPath);
  const planSha = sha(planBytes);
  if (!archived.plan || archived.plan.sha256 !== planSha) {
    die('the plan handed to the seal is not the plan the archive recorded', { plan: planPath,
      archived_plan: archived.plan ? archived.plan.sha256 : null, got: planSha });
  }
  if (plan.ordinal_digest !== thisDigest) die('the plan ordinal digest is not this payload', { plan: plan.ordinal_digest, payload: thisDigest });
  if (plan.member_count !== members.length) die('the plan member count is not this payload', { plan: plan.member_count, payload: members.length });

  // THE QUALIFICATION GATE. Every mandatory role has to be present, pinned to the frozen contract, and backed by a record
  // that exists, has not changed, and says true in the field the plan named. The roles come from the frozen record, so a
  // plan that dropped one is refused here even though it is internally consistent.
  const frozen = frozenRoles();
  if (plan.mandatory_roles && plan.mandatory_roles.contract
    && (plan.mandatory_roles.contract.sha256 !== frozen.record.sha256)) {
    die('the plan pins a different mandatory roles contract than the one the dispatch froze',
      { plan: plan.mandatory_roles.contract.sha256, frozen: frozen.record.sha256 });
  }
  const coverage = checkRoleCoverage(plan.mandatory_roles, frozen);
  if (coverage.length) die('the plan does not pin a complete set of mandatory consumer roles', { problems: coverage });
  const roleResults = [];
  for (const entry of plan.mandatory_roles.roles) {
    const checked = checkEvidence({ path: entry.path, bytes: entry.bytes, sha256: entry.sha256, ok_field: entry.ok_field },
      'mandatory role ' + entry.role);
    roleResults.push({ role: entry.role, ...checked });
  }
  const badRoles = roleResults.filter((r) => !r.ok);
  if (badRoles.length) die('the seal cannot consume its mandatory consumer roles', { failed: badRoles.length,
    of: roleResults.length, detail: badRoles });

  const evidence = [];
  if (!plan.required_set) die('the plan pins no required set, so the seal would be a receipt for a payload nothing required anything of');
  evidence.push(checkEvidence(plan.required_set, 'required set'));
  for (const e of plan.evidence) evidence.push(checkEvidence(e, e.what || 'consumer result'));
  const bad = evidence.filter((e) => !e.ok);
  if (bad.length) die('the seal cannot consume its evidence', { failed: bad.length, of: evidence.length, detail: bad });

  const receipt = {
    task: ctx.task,
    what: 'the seal: the payload identity, and the identities of what it carries. This file is outside the payload, so the payload can '
      + 'be checked against it without the payload having to describe itself.',
    scope: ctx.scope,
    identity: { task: ctx.task, root: ctx.authorized_report_root, state: ctx.statePath, terminal: ctx.destDir },
    context: { path: ctx.__path, sha256: ctx.__sha256, id: ctx.__id },
    payload: { path: payloadPath, bytes: bytes.length, sha256: thisHash },
    member_count: members.length,
    ordinal_digest: thisDigest,
    archived: { at: archived.at, dest: archived.dest, bytes: archived.bytes, sha256: archived.sha256 },
    plan: { path: planPath, bytes: planBytes.length, sha256: planSha },
    mandatory_roles: { contract: frozen.record, required: frozen.roles, consumed: roleResults.map((r) => ({
      role: r.role, path: r.path, bytes: r.bytes, sha256: r.sha256, ok_field: r.ok_field })) },
    evidence_consumed: evidence.map((e) => ({ what: e.what, path: e.path, bytes: e.bytes, sha256: e.sha256, ok_field: e.ok_field })),
    members: members.map((m, i) => ({ ordinal: i, name: m.name, size: m.size, sha256: m.sha256 })),
    sealed_once: true,
    the_payload_does_not_carry_this_file: true,
  };
  const out = writeNew(receiptPath, JSON.stringify(receipt, null, 2) + '\n');
  appendState(ctx, { verb: 'seal', terminal: true, receipt: receiptPath, payload: payloadPath, payload_sha256: thisHash,
    ordinal_digest: thisDigest, members: members.length, evidence_consumed: evidence.length,
    mandatory_roles_consumed: roleResults.length });
  process.stdout.write(JSON.stringify({ verb, receipt: out, payload: receipt.payload, member_count: members.length,
    ordinal_digest: thisDigest, evidence_consumed: evidence.length, mandatory_roles_consumed: roleResults.length,
    state: ctx.statePath, terminal: true }, null, 2) + '\n');
  process.exit(0);
}

if (verb === 'verify') {
  arity(verb, rest, 3);
  const ctx = loadContext(contextPath);
  const [payloadPath, receiptPath, planPath] = rest;
  for (const p of [payloadPath, receiptPath, planPath]) if (!fs.existsSync(p)) die('absent: ' + p);
  const bytes = fs.readFileSync(payloadPath);
  const receipt = readJson(receiptPath);
  const plan = readJson(planPath);
  const members = readTar(bytes);
  const payloadDigest = ordinalDigest(members.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 })));
  const planDigest = ordinalDigest(plan.members.map((m) => ({ name: m.path, size: m.bytes, sha256: m.sha256 })));
  const planText = fs.readFileSync(planPath, 'utf8');

  const inPayload = new Map(members.map((m) => [m.name, m]));
  const inPlan = new Map(plan.members.map((m) => [m.path, m]));
  const onlyInPayload = [...inPayload.keys()].filter((k) => !inPlan.has(k)).sort();
  const onlyInPlan = [...inPlan.keys()].filter((k) => !inPayload.has(k)).sort();
  const sizeMismatches = [];
  const byteMismatches = [];
  for (const m of members) {
    const want = inPlan.get(m.name);
    if (!want) continue;
    if (m.size !== want.bytes) sizeMismatches.push({ member: m.name, want: want.bytes, got: m.size });
    else if (m.sha256 !== want.sha256) byteMismatches.push({ member: m.name });
  }
  const state = stateFor(ctx);
  const thisHash = sha(bytes);
  const archived = state.archives.find((r) => r.ordinal_digest === payloadDigest && r.sha256 === thisHash);
  const sealed = state.seals.find((r) => r.ordinal_digest === payloadDigest && r.payload_sha256 === thisHash);

  const norm = (p) => String(p === null || p === undefined ? '' : p).replace(/\\/g, '/');
  const payloadAbs = norm(path.resolve(payloadPath));
  const payloadBase = path.basename(payloadPath);
  const planNamesPayload = plan.members.some((m) => norm(m.from) === payloadAbs || norm(m.path) === payloadAbs)
    || norm(plan.payload_path || '') === payloadAbs
    || norm(planText).includes('"' + payloadAbs + '"')
    || planText.includes(thisHash);
  const membersWithTheSameBasename = plan.members
    .filter((m) => path.basename(norm(m.path)) === payloadBase || path.basename(norm(m.from)) === payloadBase)
    .map((m) => m.path);

  // The role coverage is re-derived here from the receipt and the frozen contract, not taken from the receipt's own claim
  // about how many roles it consumed.
  let frozen = null;
  try { frozen = frozenRoles(); } catch { frozen = null; }
  const receiptRoles = (receipt.mandatory_roles && Array.isArray(receipt.mandatory_roles.consumed))
    ? receipt.mandatory_roles.consumed : [];
  const receiptRoleNames = receiptRoles.map((r) => r.role).sort();
  const frozenRoleNames = frozen ? frozen.roles.slice().sort() : [];
  const rolesMatchFrozen = frozen !== null && receiptRoleNames.length === frozenRoleNames.length
    && receiptRoleNames.every((n, i) => n === frozenRoleNames[i]);

  const obligations = {
    receipt_names_this_task: { value: receipt.task === ctx.task, want: ctx.task, got: receipt.task === undefined ? null : receipt.task },
    receipt_names_this_scope: { value: receipt.scope === ctx.scope, want: ctx.scope, got: receipt.scope === undefined ? null : receipt.scope },
    receipt_names_this_identity: { value: !!(receipt.context && receipt.context.id === ctx.__id),
      want: ctx.__id, got: receipt.context && receipt.context.id ? receipt.context.id : null },
    receipt_payload_bytes: { value: !!(receipt.payload && receipt.payload.bytes === bytes.length),
      want: bytes.length, got: receipt.payload ? receipt.payload.bytes : null },
    receipt_payload_sha256: { value: !!(receipt.payload && receipt.payload.sha256 === thisHash),
      want: thisHash, got: receipt.payload ? receipt.payload.sha256 : null },
    receipt_member_count: { value: receipt.member_count === members.length, want: members.length,
      got: receipt.member_count === undefined ? null : receipt.member_count },
    receipt_ordinal_digest: { value: receipt.ordinal_digest === payloadDigest, want: payloadDigest,
      got: receipt.ordinal_digest === undefined ? null : receipt.ordinal_digest },
    plan_is_the_archived_plan: { value: !!(archived && archived.plan && archived.plan.sha256 === sha(fs.readFileSync(planPath))),
      want: archived && archived.plan ? archived.plan.sha256 : null, got: sha(fs.readFileSync(planPath)) },
    payload_is_the_archived_payload: { value: !!archived, want: 'an archive row with this digest and hash',
      got: archived ? { dest: archived.dest, sha256: archived.sha256 } : null },
    payload_is_the_sealed_payload: { value: !!sealed, want: 'a seal row with this digest and hash',
      got: sealed ? { receipt: sealed.receipt, sha256: sealed.payload_sha256 } : null },
    plan_digest_is_the_payload: { value: planDigest === payloadDigest, want: payloadDigest, got: planDigest },
    only_in_payload_is_empty: { value: onlyInPayload.length === 0, got: onlyInPayload.slice(0, 8) },
    only_in_plan_is_empty: { value: onlyInPlan.length === 0, got: onlyInPlan.slice(0, 8) },
    sizes_match: { value: sizeMismatches.length === 0, got: sizeMismatches.slice(0, 8) },
    bytes_match: { value: byteMismatches.length === 0, got: byteMismatches.slice(0, 8) },
    plan_does_not_name_payload: { value: !planNamesPayload, payload: payloadAbs,
      members_sharing_the_payload_basename: membersWithTheSameBasename,
      how: 'whole paths and whole member fields, plus the payload hash; never a basename' },
    state_has_one_archive: { value: state.archives.length === 1, got: state.archives.length,
      why: 'the contract fixes one archive per task identity' },
    state_has_one_seal: { value: state.seals.length === 1, got: state.seals.length },
    // The qualification gate, re-checked here against the receipt: the roles the seal consumed are exactly the frozen
    // roles, and the receipt names the same contract the dispatch froze.
    receipt_consumed_every_mandatory_role: { value: rolesMatchFrozen, want: frozenRoleNames, got: receiptRoleNames,
      why: 'the roles are compared against the frozen record, not against the receipt own count' },
    receipt_names_the_frozen_role_contract: { value: !!(frozen && receipt.mandatory_roles && receipt.mandatory_roles.contract
      && receipt.mandatory_roles.contract.sha256 === frozen.record.sha256),
      want: frozen ? frozen.record.sha256 : null,
      got: receipt.mandatory_roles && receipt.mandatory_roles.contract ? receipt.mandatory_roles.contract.sha256 : null },
    evidence_records_are_unchanged: { value: (receipt.evidence_consumed || []).every((e) => fs.existsSync(e.path)
      && sha(fs.readFileSync(e.path)) === e.sha256), got: (receipt.evidence_consumed || []).length },
    mandatory_role_records_are_unchanged: { value: receiptRoles.every((r) => fs.existsSync(r.path)
      && sha(fs.readFileSync(r.path)) === r.sha256), got: receiptRoles.length },
  };

  const failed = Object.entries(obligations).filter(([, v]) => v.value !== true).map(([k]) => k);
  const out = {
    task: ctx.task, verb, scope: ctx.scope,
    context: { path: ctx.__path, bytes: ctx.__bytes, sha256: ctx.__sha256, id: ctx.__id },
    identity: { task: ctx.task, root: ctx.authorized_report_root, state: ctx.statePath, terminal: ctx.destDir },
    payload: { path: payloadPath, bytes: bytes.length, sha256: thisHash },
    receipt: { path: receiptPath, bytes: fs.statSync(receiptPath).size, sha256: sha(fs.readFileSync(receiptPath)) },
    plan: { path: planPath, bytes: fs.statSync(planPath).size, sha256: sha(fs.readFileSync(planPath)) },
    member_count: members.length, ordinal_digest: payloadDigest,
    obligations, failed_obligations: failed,
    ok: failed.length === 0,
    ok_means: 'every named obligation is true. They are computed from the payload bytes, the receipt, the plan, the state file and the '
      + 'frozen role contract, each read here; none is copied from the receipt claim about itself.',
  };
  // The verify record goes beside the state file it is a statement about, which is the context's own report root -- not a
  // constant. The first version of this line was `R + '/verify-146.json'`, a production path written into by TEST_ONLY
  // fixture runs, and the fixture run that found it had already put a file there. A record belongs to the root whose
  // transaction it describes; deriving the path from the context is what makes that true for every scope.
  const outPath = process.env.GTOS_VERIFY_OUT || (ctx.authorized_report_root + '/verify-' + tagFor(ctx.task) + '.json');
  if (fs.existsSync(outPath)) die('CreateNew refused: the output already exists', { path: outPath });
  writeNew(outPath, JSON.stringify(out, null, 2) + '\n');
  process.stdout.write(JSON.stringify({ verb, ok: out.ok, failed_obligations: failed, member_count: members.length,
    ordinal_digest: payloadDigest, out: outPath }, null, 2) + '\n');
  process.exit(out.ok ? 0 : 1);
}

die('unknown verb', { verb, known: ['state', 'plan', 'archive', 'seal', 'verify'] });
