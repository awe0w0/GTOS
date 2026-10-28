#!/usr/bin/env node
// GTOS-DSH-145 F: the publisher engine, bound to one frozen task identity instead of to whatever the caller passes.
//
// The 144 engine had four verbs and took the ledger path as an argument. That made every guarantee about a packet's finality conditional
// on the caller: a caller who passed a different ledger got a different transaction, a caller who passed an empty ledger got a seal over
// a payload that had never been archived, and a caller who passed the same ledger under a second output name got a second payload. The
// eleven negatives passed because they were written against the same ledger the successful run had used, so they tested the output name
// and not the task.
//
// This engine derives the transaction from the task identity instead. The identity is a pair -- the task name and the authorized report
// root -- and both the state file and the terminal directory are computed from that pair:
//
//   state      <root>/publication-state-<tag>.jsonl
//   terminal   <root>/archive-<tag>
//
// where <tag> is the digits at the end of the task name, or a slug of the whole name when it does not end in digits. Nothing the caller
// passes can move either one, because neither is an argument. A context file is still read, and it is checked against the derived values
// rather than believed: a context that names a different state path, a different destination, a different task or a different root is a
// refusal, and so is a copy of the frozen production context at another path.
//
//   plan    <context> <spec.json> <plan.json>
//   archive <context> <plan.json> <payload.tar>
//   seal    <context> <payload.tar> <receipt.json> <plan.json>
//   verify  <context> <payload.tar> <receipt.json> <plan.json>
//   state   <context>
//
// There is no argv position in which a ledger, a transaction directory or a second root can be named: the extra-argument check is exact
// arity, and an extra argument is a refusal rather than something silently ignored.
//
// The verbs keep the properties the 144 engine had, because they were right:
//   * CreateNew on every output. A second archive at the same path, or a second seal under any name, refuses.
//   * The plan does not name the payload it is carried in.
//   * verify parses the payload from its own bytes and compares in both directions, with ordinal digests on both sides.
//
// And they add the properties 144 was missing:
//   * seal requires a successful archive of THIS payload in THIS task. An absent state file, a zero-byte state file, a state file with
//     only other rows, and a state file whose archive row names another payload are all "nothing to seal". The 144 condition was
//     `before.archives.length && !before.digests.has(...)`, whose first conjunct skipped the whole check when the state carried no
//     archive row at all -- which is exactly the state a pre-archive seal runs in.
//   * seal consumes the required set and the consumer results the plan pinned, each by bytes and hash, each required to say true in its
//     own named field. A missing source, a changed source and a source that says false are three different refusals.
//   * every refusal is decided before anything is written, so a refused attempt cannot leave a partial artifact behind.
//   * refusals are recorded in their own audit file, never in the state file, so the state file changes only when a transaction step
//     actually succeeds and "the state did not change" is a byte comparison rather than a claim.
//   * verify publishes an explicit obligation schema. `ok` is the conjunction of named obligations and nothing else; a missing source is
//     a failed obligation rather than an absent one, so no caller can OR a false into a green.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const TASK = 'GTOS-DSH-145';
const TEST_ONLY_PREFIX = R + '/test-only/';

// The frozen authority. These are bytes, not paths: a caller can point at any file, and the file has to be this file.
const PINNED = {
  authority: { path: R + '/authority-145.json', bytes: 13048,
    sha256: '223ea3f8bc51121cbf5ba1f9e04b6ef148ec800ce38fcbdd9ce43e2aef705936' },
  context: { path: R + '/publication-context-145.json', bytes: 1153,
    sha256: '27029782be4846e819d09aa2a477e25180485daa0f8e9750907d60c2ab6088a7' },
};
const PRODUCTION = {
  task: TASK,
  root: R,
  state: R + '/publication-state-145.jsonl',
  destination: R + '/archive-145',
  refusal_audit: R + '/publication-refusals-145.jsonl',
};

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const BLOCK = 512;
const ZERO = Buffer.alloc(BLOCK);
const same = (a, b) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
const inside = (root, p) => {
  const r = path.resolve(root).toLowerCase();
  const q = path.resolve(p).toLowerCase();
  return q === r || q.startsWith(r.endsWith(path.sep) ? r : r + path.sep);
};

// The tag that names this task's state file and terminal directory. It comes from the task name, which is part of the identity, and from
// nothing else, so no caller-supplied path can produce a second one.
function tagFor(task) {
  const m = /-(\d+)$/.exec(task);
  if (m) return m[1];
  return task.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
}
const stateNameFor = (task) => 'publication-state-' + tagFor(task) + '.jsonl';
const destNameFor = (task) => 'archive-' + tagFor(task);

function die(message, extra) {
  const row = { refused: true, why: message, ...(extra || {}) };
  // The refusal audit is the only thing a refusal writes, and it is a separate file from the state. It is best-effort: a refusal must not
  // turn into a crash because the audit could not be appended, and the refusal itself is already on stderr.
  try {
    const audit = extra && extra.__audit;
    if (audit) {
      const clean = { ...row };
      delete clean.__audit;
      fs.mkdirSync(path.dirname(audit), { recursive: true });
      fs.appendFileSync(audit, JSON.stringify({ at: new Date().toISOString(), verb: process.argv[3] || null, ...clean }) + '\n',
        { encoding: 'utf8' });
    }
  } catch { /* the refusal stands without its audit line */ }
  process.stderr.write(JSON.stringify(row) + '\n');
  process.exit(1);
}

function readJson(p) {
  if (!fs.existsSync(p)) die('absent: ' + p);
  try { return JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '')); }
  catch (e) { die('not JSON: ' + p, { detail: String(e.message) }); }
}

// CreateNew: exclusive create, and the bytes are read back so that a short or reordered write is not a success.
function writeNew(p, bytes) {
  const buf = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes, 'utf8');
  fs.mkdirSync(path.dirname(p), { recursive: true });
  let fd;
  try { fd = fs.openSync(p, 'wx'); }
  catch (e) {
    if (e.code === 'EEXIST') die('CreateNew refused: the output already exists', { path: p });
    throw e;
  }
  try { fs.writeSync(fd, buf, 0, buf.length, 0); } finally { fs.closeSync(fd); }
  const landed = fs.readFileSync(p);
  if (!landed.equals(buf)) die('the written bytes are not the bytes handed over', { path: p });
  return { path: p, bytes: landed.length, sha256: sha(landed) };
}

// ---------------------------------------------------------------- the context is a checked declaration
// A context names a task, a root, a state file and a destination. The task and the root are the identity; the state and the destination
// are derived from them and the context has to agree. Two contexts that differ in the state path or the destination are not two
// transactions -- one of them is wrong, and it is refused.
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
    // The production context is this exact file, and its contents are these exact values. A copy of it at another path is a refusal,
    // not a second transaction, because the identity is the bytes and the path the authority names.
    if (!same(p, PINNED.context.path)) die('this is not the production context the authority names', { got: p, want: PINNED.context.path });
    if (got !== PINNED.context.sha256) die('the production context is not the bytes the authority froze', { want: PINNED.context.sha256, got });
    if (ctx.task !== PRODUCTION.task) die('the production context names another task', { want: PRODUCTION.task, got: ctx.task });
    if (!same(ctx.authorized_report_root, PRODUCTION.root)) {
      die('the production context does not name the authorized report root', { want: PRODUCTION.root, got: ctx.authorized_report_root });
    }
    // The authority record is read too, and its bytes are checked, so that a caller cannot hand over a context that agrees with itself
    // while the authority it claims to derive from has been swapped.
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
    // A TEST_ONLY context is a separate task with its own state, and it may not be the production task wearing a fixture's name. That is
    // the escape route the taskbook names: rename the task, call it a fixture, and the sealed state no longer applies.
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

  // The state file and the terminal directory are computed from the task and the root. The context declares them so that a reader can
  // see them without running this engine, and the declaration is checked rather than used: a context that names another state file or
  // another destination is refused here, before it can start a second transaction under the same task.
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
  // The identity is the pair the taskbook fixes: the task and the authorized root. Everything else is derived from it.
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

// ---------------------------------------------------------------- the state file
// The state file is the transaction. It is written only by this engine, only after a step actually succeeded, and only appended to. Its
// path is derived from the task and the root, so it is never the caller's file.
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
  fs.appendFileSync(ctx.statePath, JSON.stringify({ at: new Date().toISOString(), task: ctx.task, context_id: ctx.__id,
    scope: ctx.scope, ...row }) + '\n', { encoding: 'utf8' });
}

// ---------------------------------------------------------------- ustar
function octal(value, width) {
  const s = value.toString(8);
  if (s.length > width - 1) die('a header field does not fit', { value, width });
  return s.padStart(width - 1, '0') + '\0';
}
function stringField(value, width) {
  const b = Buffer.alloc(width);
  const s = Buffer.from(value, 'utf8');
  if (s.length > width) die('a header field does not fit', { value, width });
  s.copy(b, 0);
  return b;
}
function header(name, size, mode) {
  const h = Buffer.alloc(BLOCK);
  let base = name;
  let prefix = '';
  if (Buffer.byteLength(name, 'utf8') > 100) {
    const cut = name.lastIndexOf('/', name.length - 100);
    if (cut <= 0) die('a member path is too long for a ustar header', { name });
    prefix = name.slice(0, cut);
    base = name.slice(cut + 1);
    if (Buffer.byteLength(prefix, 'utf8') > 155) die('a member prefix is too long for a ustar header', { name });
  }
  stringField(base, 100).copy(h, 0);
  octal(mode, 8).split('').forEach((c, i) => { h[100 + i] = c.charCodeAt(0); });
  octal(0, 8).split('').forEach((c, i) => { h[108 + i] = c.charCodeAt(0); });   // uid
  octal(0, 8).split('').forEach((c, i) => { h[116 + i] = c.charCodeAt(0); });   // gid
  octal(size, 12).split('').forEach((c, i) => { h[124 + i] = c.charCodeAt(0); });
  octal(0, 12).split('').forEach((c, i) => { h[136 + i] = c.charCodeAt(0); });  // mtime, a constant
  h[156] = '0'.charCodeAt(0);                                                   // regular file
  stringField('ustar\0', 6).copy(h, 257);
  stringField('00', 2).copy(h, 263);
  if (prefix) stringField(prefix, 155).copy(h, 345);
  h.fill(' '.charCodeAt(0), 148, 156);
  let sum = 0;
  for (const byte of h) sum += byte;
  const chk = sum.toString(8).padStart(6, '0') + '\0 ';
  for (let i = 0; i < 8; i++) h[148 + i] = chk.charCodeAt(i);
  return h;
}
// A member whose size is already a multiple of the block size needs no padding block. Emitting one anyway makes the reader stop early,
// because a zero block is the end of the archive.
function pad(size) { const rem = size % BLOCK; return rem === 0 ? Buffer.alloc(0) : Buffer.alloc(BLOCK - rem); }

function readTar(bytes) {
  const members = [];
  let at = 0;
  let stoppedAt = -1;
  while (at + BLOCK <= bytes.length) {
    const h = bytes.subarray(at, at + BLOCK);
    if (h.equals(ZERO)) { stoppedAt = at; break; }
    const str = (from, len) => {
      const s = h.subarray(from, from + len);
      const z = s.indexOf(0);
      return s.subarray(0, z < 0 ? s.length : z).toString('utf8');
    };
    const name = str(0, 100);
    const prefix = str(345, 155);
    const size = parseInt(str(124, 12).trim() || '0', 8);
    const mode = parseInt(str(100, 8).trim() || '0', 8);
    const mtime = parseInt(str(136, 12).trim() || '0', 8);
    const chkField = h.subarray(148, 156).toString('latin1');
    const declared = parseInt(chkField.replace(/[^0-7]/g, '') || '0', 8);
    const copy = Buffer.from(h);
    copy.fill(' '.charCodeAt(0), 148, 156);
    let actual = 0;
    for (const byte of copy) actual += byte;
    if (declared !== actual) die('a member header does not checksum', { name: name || '(unnamed)', declared, actual });
    if (!Number.isFinite(size) || size < 0) die('a member header has no usable size', { name });
    const full = prefix ? prefix + '/' + name : name;
    const body = bytes.subarray(at + BLOCK, at + BLOCK + size);
    if (body.length !== size) die('a member is truncated', { name: full, declared: size, present: body.length });
    members.push({ name: full, size, mode, mtime, sha256: sha(body) });
    at += BLOCK + Math.ceil(size / BLOCK) * BLOCK;
  }
  if (stoppedAt >= 0 && !bytes.subarray(stoppedAt).every((b) => b === 0)) {
    die('a zero block appears before the end of the payload', { at: stoppedAt, bytes: bytes.length, members_before: members.length });
  }
  return members;
}

function ordinalDigest(members) {
  return sha(Buffer.from(members.map((m, i) => `${i}\0${m.name}\0${m.size}\0${m.sha256}`).join('\n'), 'utf8'));
}

// A pinned evidence entry: a file whose bytes and hash are known, and whose own boolean says whether the thing it records succeeded.
// `ok_field` is explicit per entry because guessing a field name is how the 144 wrapper read a field that did not exist and reported a
// false it could not see.
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
  // The spec is for this task. A spec for another task would produce a plan, a payload and a receipt that agree with each other and
  // describe work this packet did not do.
  if (spec.task !== ctx.task) die('the spec is for another task than the context names', { want: ctx.task, got: spec.task === undefined ? null : spec.task });
  if (!Array.isArray(spec.members) || spec.members.length === 0) die('the spec lists no members');
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
    // What the seal is required to consume besides the payload: the required set and the consumer results. Both are pinned by bytes and
    // hash and both are read at seal time, so a seal over a payload whose evidence changed after the plan is a refusal.
    required_set: spec.required_set || null,
    evidence: Array.isArray(spec.evidence) ? spec.evidence : [],
    context: { path: ctx.__path, sha256: ctx.__sha256, id: ctx.__id, scope: ctx.scope },
  };
  const out = writeNew(planOut, JSON.stringify(plan, null, 2) + '\n');
  process.stdout.write(JSON.stringify({ verb, plan: out, member_count: plan.member_count, roles,
    total_bytes: plan.total_bytes, ordinal_digest: plan.ordinal_digest, evidence_pinned: plan.evidence.length,
    required_set_pinned: plan.required_set !== null }, null, 2) + '\n');
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
    state: ctx.statePath, context_id: ctx.__id }, null, 2) + '\n');
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

  // The pre-archive condition, unguarded. A seal is a statement about a payload that was archived in this task. The 144 engine wrote
  // `before.archives.length && !before.digests.has(thisDigest)`, so a state file with no archive row skipped the check entirely and a
  // seal over a never-archived payload succeeded. Here the check is on membership alone: no archive row means the digest is not in the
  // set, and the digest not being in the set is the refusal.
  const archived = before.archives.find((r) => r.ordinal_digest === thisDigest && r.sha256 === thisHash);
  if (!archived) {
    die('this payload was never archived in this task, so there is nothing to seal',
      { state: ctx.statePath, state_present: before.present, archive_rows: before.archives.length,
        rows_in_this_task: before.mine, rows_of_other_tasks: before.other_task_rows,
        payload_digest: thisDigest, payload_sha256: thisHash, archived_digests: before.archives.map((r) => r.ordinal_digest) });
  }

  // The plan the archive recorded has to be the plan handed to the seal, and its members have to be the payload's members.
  const plan = readJson(planPath);
  const planBytes = fs.readFileSync(planPath);
  const planSha = sha(planBytes);
  if (!archived.plan || archived.plan.sha256 !== planSha) {
    die('the plan handed to the seal is not the plan the archive recorded', { plan: planPath,
      archived_plan: archived.plan ? archived.plan.sha256 : null, got: planSha });
  }
  if (plan.ordinal_digest !== thisDigest) die('the plan ordinal digest is not this payload', { plan: plan.ordinal_digest, payload: thisDigest });
  if (plan.member_count !== members.length) die('the plan member count is not this payload', { plan: plan.member_count, payload: members.length });

  // The evidence the seal consumes: the required set and the consumer results, each pinned by bytes and hash and each required to say
  // true in its own named field. A missing source, a changed source and a source that says false are three different refusals.
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
    evidence_consumed: evidence.map((e) => ({ what: e.what, path: e.path, bytes: e.bytes, sha256: e.sha256, ok_field: e.ok_field })),
    members: members.map((m, i) => ({ ordinal: i, name: m.name, size: m.size, sha256: m.sha256 })),
    sealed_once: true,
    the_payload_does_not_carry_this_file: true,
  };
  const out = writeNew(receiptPath, JSON.stringify(receipt, null, 2) + '\n');
  appendState(ctx, { verb: 'seal', terminal: true, receipt: receiptPath, payload: payloadPath, payload_sha256: thisHash,
    ordinal_digest: thisDigest, members: members.length, evidence_consumed: evidence.length });
  process.stdout.write(JSON.stringify({ verb, receipt: out, payload: receipt.payload, member_count: members.length,
    ordinal_digest: thisDigest, evidence_consumed: evidence.length, state: ctx.statePath, terminal: true }, null, 2) + '\n');
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

  // Whether the plan names the payload it is being verified against. The plan is frozen before the payload exists, so it cannot name it, and
  // this obligation is here to catch a plan that was rewritten afterwards to mention the thing it produced.
  //
  // The comparison is exact. An earlier version asked whether the plan's text contained the payload's basename, which is true of any plan that
  // carries a member called `inputs/04-...-payload.tar`: the basename is a substring of that member's name, and the obligation refused a
  // correct plan for a member whose name merely ends in the same eleven characters. That is the same defect as comparing two path sets by
  // suffix, and it is why this now compares whole paths and whole member fields.
  const norm = (p) => String(p === null || p === undefined ? '' : p).replace(/\\/g, '/');
  const payloadAbs = norm(path.resolve(payloadPath));
  const payloadBase = path.basename(payloadPath);
  const planNamesPayload = plan.members.some((m) => norm(m.from) === payloadAbs || norm(m.path) === payloadAbs)
    || norm(plan.payload_path || '') === payloadAbs
    || norm(planText).includes('"' + payloadAbs + '"')
    // The payload's own hash, which the plan cannot carry because it was frozen before the payload existed. A 64-digit hash is not a substring
    // of any other name, so this one is safe to test as a substring.
    || planText.includes(thisHash);
  // A member whose *basename* is the payload's basename, in a directory the payload is not in, is a different file and is not a naming of this
  // payload. It is listed so the distinction is visible rather than argued.
  const membersWithTheSameBasename = plan.members
    .filter((m) => path.basename(norm(m.path)) === payloadBase || path.basename(norm(m.from)) === payloadBase)
    .map((m) => m.path);

  // The obligations, named. `ok` below is the conjunction of exactly these and nothing else, so a reader can see which one failed instead
  // of a single boolean. Every one of them is computed from bytes read here; none is copied from the receipt.
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
    no_member_only_in_payload: { value: onlyInPayload.length === 0, count: onlyInPayload.length },
    no_member_only_in_plan: { value: onlyInPlan.length === 0, count: onlyInPlan.length },
    every_member_size_matches: { value: sizeMismatches.length === 0, count: sizeMismatches.length },
    every_member_bytes_match: { value: byteMismatches.length === 0, count: byteMismatches.length },
    ordinal_digest_payload_equals_plan: { value: payloadDigest === planDigest, payload: payloadDigest, plan: planDigest },
    receipt_ordinal_digest: { value: receipt.ordinal_digest === payloadDigest, want: payloadDigest,
      got: receipt.ordinal_digest === undefined ? null : receipt.ordinal_digest },
    plan_does_not_name_payload: { value: !planNamesPayload, payload: payloadAbs,
      members_sharing_the_payload_basename: membersWithTheSameBasename,
      how: 'compared as whole paths and whole member fields, not as a substring of the plan text' },
    state_records_this_archive: { value: !!archived, rows: state.archives.length },
    state_records_this_seal: { value: !!sealed, rows: state.seals.length },
    state_is_readable: { value: state.unreadable === 0, unreadable: state.unreadable },
  };
  const failed = Object.keys(obligations).filter((k) => obligations[k].value !== true);
  const out = {
    task: ctx.task, verb, scope: ctx.scope,
    context: { path: ctx.__path, bytes: ctx.__bytes, sha256: ctx.__sha256, id: ctx.__id },
    identity: { task: ctx.task, root: ctx.authorized_report_root, state: ctx.statePath, terminal: ctx.destDir },
    payload_path: payloadPath,
    payload: { bytes: bytes.length, sha256: thisHash },
    members_in_payload: members.length, members_in_plan: plan.members.length,
    matched: members.length - sizeMismatches.length - byteMismatches.length - onlyInPayload.length,
    only_in_payload: onlyInPayload, only_in_plan: onlyInPlan,
    size_mismatches: sizeMismatches.slice(0, 8), byte_mismatches: byteMismatches.slice(0, 8),
    ordinal_digest_payload: payloadDigest, ordinal_digest_plan: planDigest,
    schema: 'publish-145/verify/1',
    obligations,
    failed_obligations: failed,
    ok: failed.length === 0,
    ok_means: 'every named obligation in `obligations` is true. Each one is computed from the payload bytes, the receipt, the plan or the '
      + 'state file read in this run; none is copied from another record, and a source that is absent fails its obligation rather than '
      + 'being skipped',
  };
  process.stdout.write(JSON.stringify(out, null, 2) + '\n');
  process.exit(out.ok ? 0 : 1);
}

die('unknown verb', { verb, verbs: ['plan', 'archive', 'seal', 'verify', 'state'] });
