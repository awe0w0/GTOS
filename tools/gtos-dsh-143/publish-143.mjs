#!/usr/bin/env node
// GTOS-DSH-143 D: the publisher engine. One real CLI, four verbs, and no verb that can quietly overwrite something.
//
//   plan    <spec.json>                     freeze an ordered copy-plan from the files on disk
//   archive <plan.json> <payload.tar>       write the payload, CreateNew, from the plan's own bytes
//   seal    <payload.tar> <receipt.json>    the single successful seal, CreateNew, over the payload's bytes
//   verify  <payload.tar> <receipt.json> <plan.json>
//                                           re-read the payload and compare it with the plan and the receipt
//
// The shape is ustar, written by hand rather than by a tar on PATH, because the header fields are the only place the archive's
// determinism can be checked: mtime, uid, gid and the mode are constants here, so two runs over the same members produce the same
// bytes and a changed byte is a changed member rather than a changed timestamp.
//
// Three properties are enforced rather than described:
//   * CreateNew. Every output is opened with the exclusive flag. A second archive at the same path, or a second seal of the same
//     payload, refuses. The refusal is the point: "sealed once" is a fact about the filesystem, not a sentence in a report.
//   * The manifest does not refer to itself. The manifest lists the members that are in the payload and never the payload. The
//     payload's own identity lives in the receipt, which is written outside the payload.
//   * The verification is independent of the writing. verify opens the payload as bytes, parses the headers itself, and compares
//     each member's bytes and hash with the plan, in both directions: a member in the payload that the plan does not name, and a
//     member the plan names that is not in the payload, are different failures and are reported as such.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const BLOCK = 512;
const ZERO = Buffer.alloc(BLOCK);

function die(message, extra) {
  process.stderr.write(JSON.stringify({ refused: true, why: message, ...(extra || {}) }) + '\n');
  process.exit(1);
}

function readJson(p) {
  if (!fs.existsSync(p)) die('absent: ' + p);
  try { return JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '')); }
  catch (e) { die('not JSON: ' + p, { detail: String(e.message) }); }
}

// CreateNew: exclusive create, and the bytes are read back so that a short or reordered write is not a success.
function writeNew(p, bytes) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  const buf = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes, 'utf8');
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

// ---------------------------------------------------------------- the transaction ledger
// A refusal that rests only on the file name is not a transaction. `CreateNew` stops the same name twice, and nothing else: the same
// payload sealed under a second receipt name, or archived again at a second path, would both succeed and both be wrong, because this
// packet has one terminal payload and one seal. The ledger is the record that makes the second attempt a refusal, and it is appended
// to rather than rewritten, so a refused attempt leaves the evidence of itself behind.
function ledgerPathFor(explicit) {
  if (!explicit) die('the ledger path is required: a transaction with no ledger is a file name check');
  return explicit;
}
function readLedger(p) {
  if (!fs.existsSync(p)) return [];
  const rows = [];
  for (const line of fs.readFileSync(p, 'utf8').split(/\r?\n/)) {
    if (!line.trim()) continue;
    try { rows.push(JSON.parse(line)); } catch { rows.push({ unreadable: line.slice(0, 200) }); }
  }
  return rows;
}
function appendLedger(p, row) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.appendFileSync(p, JSON.stringify(row) + '\n', { encoding: 'utf8' });
}
// What the ledger already says about this packet. A row that cannot be read is treated as a seal, not as an absence: an unreadable
// ledger must not be the way past the refusal.
function ledgerState(p) {
  const rows = readLedger(p);
  const unreadable = rows.filter((r) => r.unreadable).length;
  const archives = rows.filter((r) => r.verb === 'archive');
  const seals = rows.filter((r) => r.verb === 'seal');
  return { rows: rows.length, unreadable, archives, seals,
    digests: new Set(archives.map((r) => r.ordinal_digest)),
    payloadHashes: new Set(seals.map((r) => r.payload_sha256)) };
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
  // A ustar name field is 100 bytes. A longer path is split at a slash into prefix and name rather than truncated, because a
  // truncated name is a member nobody can find again.
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
  // The checksum is computed with the field itself read as spaces.
  h.fill(' '.charCodeAt(0), 148, 156);
  let sum = 0;
  for (const byte of h) sum += byte;
  const chk = sum.toString(8).padStart(6, '0') + '\0 ';
  for (let i = 0; i < 8; i++) h[148 + i] = chk.charCodeAt(i);
  return h;
}
// A member whose size is already a multiple of the block size needs no padding block at all. Returning a zero block for it was a real
// defect: the reader treats a zero block as the end of the archive, so a payload whose 197th member happened to be exactly 5120 bytes
// read back as 196 members and the archive verb refused it. The refusal was correct and the writer was not.
function pad(size) { const rem = size % BLOCK; return rem === 0 ? Buffer.alloc(0) : Buffer.alloc(BLOCK - rem); }

// The reader parses headers itself rather than trusting a tar to have written them.
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
  // A zero block is the end of the archive only if everything after it is also zero. A payload with a zero block in the middle is a
  // payload whose writer emitted padding where there should have been none, and reading it as a short archive would report the symptom
  // ("fewer members than planned") instead of the cause.
  if (stoppedAt >= 0 && !bytes.subarray(stoppedAt).every((b) => b === 0)) {
    die('a zero block appears before the end of the payload', { at: stoppedAt, bytes: bytes.length, members_before: members.length });
  }
  return members;
}

// The ordinal digest is over the members in order: an archive with the right members in the wrong order is a different payload,
// and a set comparison alone would not notice.
function ordinalDigest(members) {
  return sha(Buffer.from(members.map((m, i) => `${i}\0${m.name}\0${m.size}\0${m.sha256}`).join('\n'), 'utf8'));
}

// ---------------------------------------------------------------- verbs
const [, , verb, ...rest] = process.argv;

if (verb === 'plan') {
  const spec = readJson(rest[0]);
  // The plan is for one packet. A spec for another packet would produce a plan, a payload and a receipt that agree with each other
  // and describe work this packet did not do, so the task identity is checked here rather than trusted.
  if (spec.task !== 'GTOS-DSH-143') {
    die('the spec is for another packet', { want: 'GTOS-DSH-143', got: spec.task === undefined ? null : spec.task });
  }
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
  // Two members at one path is not a payload; it is a payload with one of its files hidden behind another. The reader returns both
  // and a consumer takes the last, so the bytes that win are decided by ordinal rather than by intent. The spec that produced this
  // happened because two rules named the same record -- a records glob and an explicit entry -- and the plan carried it twice.
  for (let i = 1; i < members.length; i++) {
    if (members[i].path === members[i - 1].path) {
      die('two members share one path in the payload', { path: members[i].path, from: [members[i - 1].from, members[i].from] });
    }
  }
  const roles = {};
  for (const m of members) roles[m.role] = (roles[m.role] || 0) + 1;
  const plan = {
    task: spec.task,
    what: 'the ordered copy-plan: every member of the payload, its role, its size and its hash, in the order the payload will '
      + 'carry it. This plan does not name the payload, and the payload carries this plan.',
    members,
    roles,
    total_bytes: members.reduce((n, m) => n + m.bytes, 0),
    member_count: members.length,
    // The digest is over the same four fields on both sides: the plan stores a member as path/bytes, the payload reader
    // returns name/size, and the digest is computed from the fields rather than from either object.
    ordinal_digest: ordinalDigest(members.map((m) => ({ name: m.path, size: m.bytes, sha256: m.sha256 }))),
    ordinal_digest_means: 'sha256 over the lines "<ordinal>\\0<path>\\0<size>\\0<sha256>", in the payload order',
    does_not_name_the_payload: true,
    required_set: spec.required_set || null,
  };
  const out = writeNew(rest[1], JSON.stringify(plan, null, 2) + '\n');
  process.stdout.write(JSON.stringify({ verb, plan: out, member_count: plan.member_count, roles,
    total_bytes: plan.total_bytes, ordinal_digest: plan.ordinal_digest }, null, 2) + '\n');
  process.exit(0);
}

if (verb === 'archive') {
  const planPath = rest[0];
  const dest = rest[1];
  const ledgerPath = ledgerPathFor(rest[2]);
  const plan = readJson(planPath);
  // A second archive of the same plan is refused whatever it is called and wherever it is put. The same bytes under a second name
  // are the same payload twice, and this packet publishes one.
  if (fs.existsSync(dest)) die('CreateNew refused: the output already exists', { path: dest });
  const before = ledgerState(ledgerPath);
  if (before.seals.length) {
    die('this packet is sealed, so nothing may be archived after it', { ledger: ledgerPath, seals: before.seals.length });
  }
  if (before.digests.has(plan.ordinal_digest)) {
    die('this payload has already been archived, under another path or name', { ledger: ledgerPath,
      ordinal_digest: plan.ordinal_digest, previous: before.archives.map((r) => r.dest) });
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
  // The length is arithmetic, so it is checked as arithmetic: one header and one body per member, padding only where a body is not a
  // whole number of blocks, and two zero blocks at the end.
  const expectedLength = plan.members.reduce((n, m) => n + BLOCK + m.bytes + (m.bytes % BLOCK === 0 ? 0 : BLOCK - (m.bytes % BLOCK)), 0)
    + 2 * BLOCK;
  if (payload.length !== expectedLength) die('the payload is not the length its members add up to', { built: payload.length, expected: expectedLength });
  // The payload is written and then read back with the reader, so a header the writer got wrong is caught by the writer's own
  // archive rather than by whoever opens it next.
  const out = writeNew(dest, payload);
  const back = readTar(fs.readFileSync(dest));
  if (back.length !== plan.members.length) die('the payload does not carry every planned member', { planned: plan.members.length, read: back.length });
  const readDigest = ordinalDigest(back.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 })));
  if (readDigest !== plan.ordinal_digest) die('the payload read back is not the plan', { want: plan.ordinal_digest, got: readDigest });
  appendLedger(ledgerPath, { at: new Date().toISOString(), verb: 'archive', task: plan.task, dest,
    ordinal_digest: readDigest, members: back.length, bytes: out.bytes, sha256: out.sha256 });
  process.stdout.write(JSON.stringify({ verb, payload: out, members: back.length, ordinal_digest: readDigest,
    ledger: ledgerPath }, null, 2) + '\n');
  process.exit(0);
}

if (verb === 'seal') {
  const payloadPath = rest[0];
  const receiptPath = rest[1];
  const ledgerPath = ledgerPathFor(rest[2]);
  if (!fs.existsSync(payloadPath)) die('absent: ' + payloadPath);
  // The name first, so that the same name twice is refused by CreateNew and everything else is refused by the ledger.
  if (fs.existsSync(receiptPath)) die('CreateNew refused: the output already exists', { path: receiptPath });
  const before = ledgerState(ledgerPath);
  if (before.seals.length) {
    die('this packet is already sealed; a second seal is refused whatever it is called and whatever it carries', {
      ledger: ledgerPath, seals: before.seals.length,
      sealed_payloads: before.seals.map((r) => r.payload_sha256),
      this_payload: sha(fs.readFileSync(payloadPath)) });
  }
  if (before.unreadable) die('the ledger cannot be read, so it is not safe to seal', { ledger: ledgerPath, unreadable: before.unreadable });
  const bytes = fs.readFileSync(payloadPath);
  const members = readTar(bytes);
  // A seal is a statement about a payload that is complete. A truncated payload parses as a shorter member list, and a seal over it
  // would be a receipt for a payload nobody finished writing, so the end-of-archive marker is required before anything is hashed.
  if (members.length === 0) die('the payload carries no member', { payload: payloadPath });
  if (!bytes.subarray(-1024).equals(Buffer.concat([ZERO, ZERO]))) {
    die('the payload is not terminated, so it is not complete', { payload: payloadPath, bytes: bytes.length });
  }
  // A payload that was never archived under this transaction is not this packet's payload. The check is on the content, so a copy of
  // the payload at another path is recognised rather than treated as a new one.
  const thisDigest = ordinalDigest(members.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 })));
  if (before.archives.length && !before.digests.has(thisDigest)) {
    die('this payload was never archived in this transaction, so there is nothing to seal', {
      ledger: ledgerPath, payload_digest: thisDigest, archived: [...before.digests] });
  }
  const receipt = {
    task: 'GTOS-DSH-143',
    what: 'the seal: the payload identity, and the identities of what it carries. This file is outside the payload, so the '
      + 'payload can be checked against it without the payload having to describe itself.',
    payload: { path: payloadPath, bytes: bytes.length, sha256: sha(bytes) },
    member_count: members.length,
    ordinal_digest: ordinalDigest(members.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 }))),
    members: members.map((m, i) => ({ ordinal: i, name: m.name, size: m.size, sha256: m.sha256 })),
    sealed_once: true,
    the_payload_does_not_carry_this_file: true,
  };
  const out = writeNew(receiptPath, JSON.stringify(receipt, null, 2) + '\n');
  // The ledger row is written after the receipt lands, and it is the row that makes this seal terminal: every later seal of this
  // packet, of this payload or of any other, is refused from here on.
  appendLedger(ledgerPath, { at: new Date().toISOString(), verb: 'seal', task: receipt.task, terminal: true,
    receipt: receiptPath, payload: payloadPath, payload_sha256: receipt.payload.sha256,
    ordinal_digest: receipt.ordinal_digest, members: receipt.member_count });
  process.stdout.write(JSON.stringify({ verb, receipt: out, payload: receipt.payload, member_count: receipt.member_count,
    ordinal_digest: receipt.ordinal_digest, ledger: ledgerPath, terminal: true }, null, 2) + '\n');
  process.exit(0);
}

if (verb === 'verify') {
  const [payloadPath, receiptPath, planPath] = rest;
  const problems = [];
  for (const p of [payloadPath, receiptPath, planPath]) if (!fs.existsSync(p)) die('absent: ' + p);
  const bytes = fs.readFileSync(payloadPath);
  const receipt = readJson(receiptPath);
  const plan = readJson(planPath);
  const members = readTar(bytes);

  // The receipt has to describe this payload, not another one.
  if (receipt.task !== plan.task) problems.push({ why: 'the receipt names another packet than the plan does',
    receipt_task: receipt.task === undefined ? null : receipt.task, plan_task: plan.task === undefined ? null : plan.task });
  if (receipt.payload.bytes !== bytes.length) problems.push({ why: 'the receipt names a different payload size', want: receipt.payload.bytes, got: bytes.length });
  if (receipt.payload.sha256 !== sha(bytes)) problems.push({ why: 'the receipt names a different payload hash' });
  if (receipt.member_count !== members.length) problems.push({ why: 'the receipt names a different member count' });

  // Per-member, both directions.
  const inPayload = new Map(members.map((m) => [m.name, m]));
  const inPlan = new Map(plan.members.map((m) => [m.path, m]));
  const onlyInPayload = [...inPayload.keys()].filter((k) => !inPlan.has(k)).sort();
  const onlyInPlan = [...inPlan.keys()].filter((k) => !inPayload.has(k)).sort();
  if (onlyInPayload.length) problems.push({ why: 'the payload carries members the plan does not name', members: onlyInPayload.slice(0, 8), count: onlyInPayload.length });
  if (onlyInPlan.length) problems.push({ why: 'the plan names members the payload does not carry', members: onlyInPlan.slice(0, 8), count: onlyInPlan.length });
  let matched = 0;
  for (const m of members) {
    const want = inPlan.get(m.name);
    if (!want) continue;
    if (m.size !== want.bytes) problems.push({ why: 'a member is a different size than the plan states', member: m.name, want: want.bytes, got: m.size });
    else if (m.sha256 !== want.sha256) problems.push({ why: 'a member has different bytes at the same size', member: m.name });
    else matched += 1;
  }

  // The ordinal digest, computed twice: from the payload's own order and from the plan's order. Equal digests mean the same
  // members in the same order, which a set comparison does not.
  const payloadDigest = ordinalDigest(members.map((m) => ({ name: m.name, size: m.size, sha256: m.sha256 })));
  const planDigest = ordinalDigest(plan.members.map((m) => ({ name: m.path, size: m.bytes, sha256: m.sha256 })));
  if (payloadDigest !== planDigest) problems.push({ why: 'the payload order is not the plan order', payload: payloadDigest, plan: planDigest });
  if (receipt.ordinal_digest !== payloadDigest) problems.push({ why: 'the receipt ordinal digest is not the payload order' });

  // The plan must not name the payload it is carried in.
  const planText = fs.readFileSync(planPath, 'utf8');
  if (planText.includes(sha(bytes))) problems.push({ why: 'the plan names the payload hash, so the manifest refers to itself' });
  if (planText.includes(path.basename(payloadPath))) problems.push({ why: 'the plan names the payload file, so the manifest refers to itself' });

  const out = {
    task: 'GTOS-DSH-143', verb, payload_path: payloadPath,
    payload: { bytes: bytes.length, sha256: sha(bytes) },
    members_in_payload: members.length, members_in_plan: plan.members.length, matched,
    only_in_payload: onlyInPayload, only_in_plan: onlyInPlan,
    ordinal_digest_payload: payloadDigest, ordinal_digest_plan: planDigest,
    problems, ok: problems.length === 0 && matched === plan.members.length && matched === members.length,
    ok_means: 'the payload was parsed from its own bytes, every member was compared with the plan in both directions, and the '
      + 'ordinal digests computed from the payload order and from the plan order are the same',
  };
  process.stdout.write(JSON.stringify(out, null, 2) + '\n');
  process.exit(out.ok ? 0 : 1);
}

die('unknown verb', { verb, verbs: ['plan', 'archive', 'seal', 'verify'] });
