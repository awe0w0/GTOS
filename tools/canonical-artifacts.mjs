// GTOS-DSH-109 canonical artifact writer.
//
// The 108 packet lost two commit JSON files because a later step wrote over paths the Git transaction had already
// produced. This module makes that shape of loss impossible for a controlled writer: a canonical path is created once,
// a second creation is refused with the original bytes untouched, and a reconstruction must use a new name and declare
// its provenance.
//
// This is a constraint on a controlled writer. It is not an operating system boundary and it cannot stop an arbitrary
// Node program or a shell from writing the same path. It is stated that way on purpose.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export const KIND = Object.freeze({
  ORIGINAL: 'original',
  RECONSTRUCTED: 'reconstructed',
});

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// Canonical destinations live under one directory per consumer, so two independent commits cannot collide.
export function canonicalRoot(reportRoot, consumer) {
  if (typeof reportRoot !== 'string' || !path.isAbsolute(reportRoot)) {
    throw new Error('CANONICAL: report root must be absolute');
  }
  if (typeof consumer !== 'string' || !/^[a-z][a-z0-9-]*$/.test(consumer)) {
    throw new Error(`CANONICAL: consumer '${consumer}' must be lower-case letters, digits and dashes`);
  }
  return path.join(reportRoot, 'canonical', consumer);
}

export function canonicalPath(reportRoot, consumer, name) {
  if (typeof name !== 'string' || !/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(name)) {
    throw new Error(`CANONICAL: name '${name}' must be a plain file name`);
  }
  return path.join(canonicalRoot(reportRoot, consumer), name);
}

// The single creation entry point. Returns the pin of what is now on disk.
export function createCanonical(reportRoot, consumer, name, bytes) {
  const target = canonicalPath(reportRoot, consumer, name);
  const buffer = Buffer.isBuffer(bytes) ? bytes : Buffer.from(String(bytes), 'utf8');
  fs.mkdirSync(path.dirname(target), { recursive: true });
  let handle;
  try {
    // 'wx' fails if the file exists, which is the whole point: no silent replacement is possible.
    handle = fs.openSync(target, 'wx');
  } catch (error) {
    if (error && error.code === 'EEXIST') {
      const existing = fs.readFileSync(target);
      throw new Error(`CANONICAL: '${target}' already exists (${existing.length} B ${sha256(existing)}); `
        + 'a canonical artifact is created exactly once. Use a new name with kind=reconstructed, or a verification-after path.');
    }
    throw error;
  }
  try {
    fs.writeSync(handle, buffer, 0, buffer.length, 0);
  } finally {
    fs.closeSync(handle);
  }
  const landed = fs.readFileSync(target);
  if (!landed.equals(buffer)) {
    throw new Error(`CANONICAL: '${target}' did not land byte-identically`);
  }
  return { path: target, consumer, name, kind: KIND.ORIGINAL,
    bytes: landed.length, sha256: sha256(landed), created: true };
}

// A reconstruction must not reuse the canonical name. It records what it was rebuilt from and why.
export function createReconstructed(reportRoot, consumer, name, bytes, provenance) {
  if (!provenance || typeof provenance.reason !== 'string' || provenance.reason.length === 0) {
    throw new Error('CANONICAL: a reconstruction must state a reason');
  }
  if (!Array.isArray(provenance.sources) || provenance.sources.length === 0) {
    throw new Error('CANONICAL: a reconstruction must name its sources');
  }
  for (const source of provenance.sources) {
    if (typeof source.path !== 'string' || !Number.isSafeInteger(source.bytes)
      || !/^[0-9a-f]{64}$/.test(source.sha256)) {
      throw new Error('CANONICAL: every reconstruction source needs path, bytes and sha256');
    }
  }
  const pin = createCanonical(reportRoot, consumer, name, bytes);
  return { ...pin, kind: KIND.RECONSTRUCTED,
    provenance: { reason: provenance.reason, sources: provenance.sources,
      original_preserved: false,
      note: 'this is a reconstruction, not the original transaction product' } };
}

// A post-hoc verification is always a new file. It never touches the artifact it verifies.
export function verificationAfterPath(reportRoot, consumer, stage) {
  if (typeof stage !== 'string' || !/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) {
    throw new Error(`CANONICAL: stage '${stage}' does not match the stage grammar`);
  }
  return canonicalPath(reportRoot, consumer, `verification-after-${stage}.json`);
}

// The collision rule between a report destination and a canonical destination.
export function assertReportSeparateFromCanonical(reportPath, reportRoot) {
  if (typeof reportPath !== 'string' || !path.isAbsolute(reportPath)) {
    throw new Error('CANONICAL: the report path must be absolute');
  }
  const canonicalDir = path.resolve(reportRoot, 'canonical') + path.sep;
  const resolved = path.resolve(reportPath);
  if (resolved.toLowerCase().startsWith(canonicalDir.toLowerCase())) {
    throw new Error(`CANONICAL: report '${resolved}' targets the canonical area; `
      + 'reports and canonical artifacts never share a destination');
  }
  return { report: resolved, separate: true };
}

// A partial write must never look like a success. Either every member lands or the caller is told which did not.
export function writeSet(reportRoot, consumer, members) {
  const written = [];
  for (const member of members) {
    try {
      written.push(createCanonical(reportRoot, consumer, member.name, member.bytes));
    } catch (error) {
      return { ok: false, written, failed: member.name,
        error: String(error && error.message ? error.message : error),
        note: 'a partial set is a failure; the members already written keep their bytes' };
    }
  }
  return { ok: true, written, failed: null, error: null };
}
