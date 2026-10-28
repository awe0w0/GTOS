// GTOS-DSH-146: what a trace proves, and what it does not.
//
// A trace is a list of operations. It is not a list of consumptions, and the difference is the whole subject of this file.
// The classifier below sorts every required member into one of four tiers, and only the last one counts:
//
//   metadata_only  the path was stat'd, tested for existence, listed, or opened. Nothing was read. A consumer that walked
//                  the package and never opened a file would produce a trace full of these, and it consumed nothing.
//   absent         the path never appears at all.
//   hash_only      the bytes were read and hashed, and nothing parsed them. This is the tier that makes 145's reading
//                  incomplete: a consumer that compares a record's sha256 to a pinned value has preserved the bytes without
//                  using anything the record says. It is real work, but it is not a semantic read.
//   semantic_read  the bytes were read and a parse was attributed to them -- either a whole-file parse, or a parse of a line
//                  of that file. This is the only tier that means the consumer used the record's content.
//
// The attribution is what makes `semantic_read` mean something. It is not "a parse happened somewhere in the process": the
// parse input's own hash has to be the file's, or the parse input has to be a substring of the file's bytes. A consumer that
// parsed some other file's content cannot claim this one.
//
// Six things are refused outright, because a trace that shows any of them is not evidence of anything:
//
//   * a required member whose recorded hash is not the required hash -- the trace and the package disagree about the bytes;
//   * a read attributed to a descriptor with no known origin (`fd:7`), because the path is unknown and could be anything;
//   * a read of a path outside the package that is not in the declared external set -- an unjustified external read, which is
//     how a consumer would pass by consulting something the package does not carry;
//   * a fabricated row: a path that does not exist, or a hash that does not match the file on disk;
//   * a truncated trace: no `trace-closed` row, so rows may be missing and absence proves nothing;
//   * a trace with no `trace-opened` row, which is not a trace of this preload at all.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const norm = (p) => String(p === null || p === undefined ? '' : p).replace(/\\/g, '/').toLowerCase();
const inside = (root, p) => {
  const a = norm(root);
  const b = norm(p);
  return b === a || b.startsWith(a.endsWith('/') ? a : a + '/');
};

export const METADATA_OPS = Object.freeze(['statSync', 'existsSync', 'readdirSync', 'openSync', 'lstatSync', 'accessSync']);
export const READ_OPS = Object.freeze(['readFileSync', 'promises.readFile', 'readSync', 'createReadStream']);

// Read a trace file into rows. A row that is not JSON is kept as a `malformed` row rather than dropped: a trace with garbage
// in it is a finding, and silently skipping the garbage is how a truncated write becomes invisible.
export function readTrace(tracePath) {
  if (!fs.existsSync(tracePath)) return { present: false, rows: [], malformed: 0 };
  const rows = [];
  let malformed = 0;
  for (const line of fs.readFileSync(tracePath, 'utf8').split(/\r?\n/)) {
    if (!line.trim()) continue;
    try { rows.push(JSON.parse(line)); } catch { malformed += 1; rows.push({ op: 'malformed', raw: line.slice(0, 200) }); }
  }
  return { present: true, rows, malformed };
}

// Does `needle` occur inside `haystack`? Used for the line-parse case. Buffer.indexOf, not string search, so that a parse
// input is matched against bytes and not against a decoding of them.
function containsBytes(haystack, needle) {
  if (!needle || needle.length === 0 || needle.length > haystack.length) return false;
  return haystack.indexOf(needle) !== -1;
}

// The classifier.
//
//   required     [{ path, sha256, bytes, role }]  -- the members that have to be consumed
//   packageRoot  the directory the package was unpacked into, or the root the paths are relative to
//   external     paths that may legitimately be read from outside the package (the runtime, for instance). Anything else
//                outside is an unjustified external read.
//   onDisk       a function (path) => Buffer | null, so a caller can resolve a traced path to bytes even when the package
//                lives somewhere else. Defaults to reading from disk.
export function classify(tracePath, opts) {
  const o = opts || {};
  const trace = readTrace(tracePath);
  const required = o.required || [];
  const packageRoot = o.packageRoot || null;
  const external = (o.external || []).map(norm);
  const onDisk = o.onDisk || ((p) => (fs.existsSync(p) ? fs.readFileSync(p) : null));

  const refusals = [];
  const opened = trace.rows.some((r) => r.op === 'trace-opened');
  const closed = trace.rows.some((r) => r.op === 'trace-closed');
  if (!trace.present) refusals.push({ obligation: 'trace.present', why: 'there is no trace file', path: tracePath });
  if (trace.present && !opened) refusals.push({ obligation: 'trace.opened', why: 'the trace has no trace-opened row, so it is not a trace of this preload' });
  if (trace.present && !closed) {
    refusals.push({ obligation: 'trace.complete', why: 'the trace has no trace-closed row, so it is truncated and absence in it proves nothing' });
  }
  if (trace.malformed) refusals.push({ obligation: 'trace.parses', why: 'the trace has rows that are not JSON', malformed: trace.malformed });

  // Every read row, with its attribution checked.
  const reads = [];
  const metadata = [];
  const parses = [];
  for (const r of trace.rows) {
    if (r.op === 'json.parse') { parses.push(r); continue; }
    if (METADATA_OPS.includes(r.op)) { metadata.push(r); continue; }
    if (!READ_OPS.includes(r.op)) continue;
    const attributed = r.fd_attributed !== false && !String(r.path || '').startsWith('fd:');
    if (!attributed) {
      refusals.push({ obligation: 'io.fd_is_attributed', why: 'a read was attributed to a descriptor with no known origin, so the path it names is unknown',
        path: r.path, op: r.op, consumer: r.consumer || null });
      continue;
    }
    reads.push(r);
  }

  // The external set. A read inside the package is expected; a read outside it has to be declared, or it is a consumer
  // consulting something the package does not carry -- which is how a closure claim passes without the package being enough.
  const outsideReads = [];
  if (packageRoot) {
    for (const r of reads) {
      if (inside(packageRoot, r.path)) continue;
      const allowed = external.some((e) => norm(r.path) === e || norm(r.path).startsWith(e.endsWith('/') ? e : e + '/'));
      outsideReads.push({ path: r.path, allowed, op: r.op, consumer: r.consumer || null });
      if (!allowed) {
        refusals.push({ obligation: 'io.no_unjustified_external_read', why: 'a read of a path outside the package that the packet did not declare',
          path: r.path, op: r.op });
      }
    }
  }

  // Fabrication. A row that names a path that is not there, or whose hash is not the hash of the file, cannot be evidence
  // that anything was read.
  const fabricated = [];
  const hashCache = new Map();
  const bytesOfPath = (p) => {
    if (hashCache.has(norm(p))) return hashCache.get(norm(p));
    let bytes = null;
    try { bytes = onDisk(p); } catch { bytes = null; }
    hashCache.set(norm(p), bytes);
    return bytes;
  };
  for (const r of reads) {
    const bytes = bytesOfPath(r.path);
    if (bytes === null) { fabricated.push({ path: r.path, why: 'the path is not there, so the row describes a read that could not have happened' }); continue; }
    if (r.sha256 && sha256(bytes) !== r.sha256) {
      fabricated.push({ path: r.path, why: 'the row hash is not the hash of the file', row: r.sha256, disk: sha256(bytes) });
    }
  }
  if (fabricated.length) refusals.push({ obligation: 'io.no_fabricated_rows', why: 'the trace contains rows that do not describe the files on disk',
    count: fabricated.length, examples: fabricated.slice(0, 4) });

  // The per-member classification.
  const members = required.map((req) => {
    const target = norm(req.path);
    const readRows = reads.filter((r) => norm(r.path) === target);
    const metaRows = metadata.filter((r) => norm(r.path) === target);
    const exactHash = readRows.filter((r) => req.sha256 === undefined || r.sha256 === req.sha256);
    const bytes = bytesOfPath(req.path);
    // A parse is attributed to this member when its input hash is the file's, or when the parse input is a substring of the
    // file's bytes. The second case is the `.jsonl` line, and it is checked against the bytes rather than assumed.
    const parsesForThis = parses.filter((p) => {
      if (req.sha256 !== undefined && p.sha256 === req.sha256) return true;
      if (!bytes) return false;
      const line = String(p.prefix || '');
      if (!line) return false;
      return containsBytes(bytes, Buffer.from(line, 'utf8'));
    });

    let tier;
    if (exactHash.length > 0 && parsesForThis.length > 0) tier = 'semantic_read';
    else if (exactHash.length > 0) tier = 'hash_only';
    else if (readRows.length > 0) tier = 'read_wrong_bytes';
    else if (metaRows.length > 0) tier = 'metadata_only';
    else tier = 'absent';

    return {
      path: req.path, role: req.role || null, required_sha256: req.sha256 === undefined ? null : req.sha256,
      tier,
      consumed_semantically: tier === 'semantic_read',
      reads: readRows.length, reads_with_the_required_hash: exactHash.length, metadata_operations: metaRows.length,
      parses_attributed: parsesForThis.length,
      metadata_ops: [...new Set(metaRows.map((r) => r.op))],
      read_hashes: [...new Set(readRows.map((r) => r.sha256).filter(Boolean))].slice(0, 3),
    };
  });

  const byTier = {};
  for (const m of members) byTier[m.tier] = (byTier[m.tier] || 0) + 1;
  const semantic = members.filter((m) => m.tier === 'semantic_read');
  const notSemantic = members.filter((m) => m.tier !== 'semantic_read');

  const ok = refusals.length === 0 && notSemantic.length === 0 && members.length === required.length && required.length > 0;

  return {
    trace: { path: tracePath, present: trace.present, rows: trace.rows.length, malformed: trace.malformed,
      opened, closed, parses: parses.length, reads: reads.length, metadata_operations: metadata.length },
    package_root: packageRoot,
    required: required.length,
    members,
    by_tier: byTier,
    semantic: semantic.length,
    not_semantic: notSemantic.length,
    not_semantic_members: notSemantic.slice(0, 12).map((m) => ({ path: m.path, tier: m.tier })),
    outside_reads: outsideReads,
    fabricated,
    refusals,
    ok,
    ok_means: 'every required member was read with the required hash AND a parse was attributed to it, the trace is complete, '
      + 'no read is attributed to an unknown descriptor, no read is fabricated, and no read falls outside the package without '
      + 'having been declared.',
  };
}

// A consumer's own claim, for comparison. A consumer that reports "I consumed N records" can be checked against the trace,
// and the two numbers disagreeing is a finding about the consumer rather than about the package.
export function compareClaim(claim, result) {
  const problems = [];
  if (claim === null || claim === undefined) return problems;
  if (typeof claim.consumed === 'number' && claim.consumed !== result.semantic) {
    problems.push({ obligation: 'claim.consumed_matches_the_trace', why: 'the consumer says it consumed a different number of records than the trace shows',
      claimed: claim.consumed, traced: result.semantic });
  }
  if (Array.isArray(claim.paths)) {
    const traced = new Set(result.members.filter((m) => m.tier === 'semantic_read').map((m) => norm(m.path)));
    const claimedOnly = claim.paths.map(norm).filter((p) => !traced.has(p));
    if (claimedOnly.length) {
      problems.push({ obligation: 'claim.paths_are_traced', why: 'the consumer names records the trace does not show as semantically read',
        count: claimedOnly.length, examples: claimedOnly.slice(0, 4) });
    }
  }
  return problems;
}
