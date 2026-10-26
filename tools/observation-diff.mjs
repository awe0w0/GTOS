// GTOS-DSH-128 J0a: a snapshot comparator that can fail.
//
// The 127 comparator recursed to the leaf and then called a pin comparison, which reads `.bytes` and `.sha256`. A string
// or a number has neither, so two different strings both gave `undefined` and `undefined === undefined` reported them as
// equal. A count that changed from 15 to 16 was therefore invisible, and "diff = 0" said nothing about those fields.
//
// This module compares by what the value actually is:
//
//   a pin     an object that carries bytes and sha256, compared field by field
//   a number  compared as a number
//   a string  compared as a string
//   null      a state, compared as a state, distinct from a missing key
//   a list    compared by length and then element by element, so order matters
//   a map     compared by key set and then value by value
//
// Every difference names where it is and what it was. Nothing here decides whether a difference is acceptable; it only
// reports, and the caller must act.

import crypto from 'node:crypto';
import { readFileSync } from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// A pin is a plain object with a numeric bytes and a string sha256. That shape is what makes it a pin; nothing else is.
export function isPin(v) {
  return v !== null && typeof v === 'object' && !Array.isArray(v)
    && typeof v.bytes === 'number' && typeof v.sha256 === 'string'
    && typeof v.path === 'string';
}

function kindOf(v) {
  if (v === null) return 'null';
  if (v === undefined) return 'absent';
  if (Array.isArray(v)) return 'list';
  if (isPin(v)) return 'pin';
  if (typeof v === 'object') return 'map';
  return typeof v;
}

// Compare two values and return the differences. The trail names the path from the root.
export function diffValues(before, after, trail = 'root') {
  const out = [];
  const kb = kindOf(before);
  const ka = kindOf(after);

  // A change of type is a difference even when the values could be coerced.
  if (kb !== ka) {
    out.push({ at: trail, kind: 'type', before: kb, after: ka,
      before_value: describe(before), after_value: describe(after) });
    return out;
  }

  switch (ka) {
    case 'absent':
      return out; // both absent: no difference
    case 'null':
      return out; // both null: no difference
    case 'number':
    case 'string':
    case 'boolean':
    case 'bigint':
      // The 127 defect: these were handed to a pin comparison and always matched. Compared as themselves here.
      if (before !== after) out.push({ at: trail, kind: ka, before, after });
      return out;
    case 'pin':
      // Field by field, so a changed hash, a changed size or a changed path is each reported separately.
      if (before.bytes !== after.bytes) {
        out.push({ at: trail + '.bytes', kind: 'number', before: before.bytes, after: after.bytes });
      }
      if (before.sha256 !== after.sha256) {
        out.push({ at: trail + '.sha256', kind: 'string', before: before.sha256, after: after.sha256 });
      }
      if (before.path !== after.path) {
        out.push({ at: trail + '.path', kind: 'string', before: before.path, after: after.path });
      }
      // A pin may carry other fields; they are compared too rather than ignored.
      for (const k of new Set([...Object.keys(before), ...Object.keys(after)])) {
        if (['bytes', 'sha256', 'path'].includes(k)) continue;
        out.push(...diffValues(before[k], after[k], trail + '.' + k));
      }
      return out;
    case 'list':
      if (before.length !== after.length) {
        out.push({ at: trail + '.length', kind: 'number', before: before.length, after: after.length });
      }
      // Element by element up to the shorter length, so a reordering is visible as differing elements.
      for (let i = 0; i < Math.min(before.length, after.length); i++) {
        out.push(...diffValues(before[i], after[i], trail + '[' + i + ']'));
      }
      return out;
    case 'map': {
      const keys = new Set([...Object.keys(before), ...Object.keys(after)]);
      for (const k of keys) {
        const inB = Object.prototype.hasOwnProperty.call(before, k);
        const inA = Object.prototype.hasOwnProperty.call(after, k);
        // A key that appeared or disappeared is its own difference; it is not the same as a key whose value is null.
        if (inB !== inA) {
          out.push({ at: trail + '.' + k, kind: inB ? 'removed' : 'added',
            before: inB ? describe(before[k]) : null, after: inA ? describe(after[k]) : null });
          continue;
        }
        out.push(...diffValues(before[k], after[k], trail + '.' + k));
      }
      return out;
    }
    default:
      out.push({ at: trail, kind: 'unknown-type', before: kb, after: ka });
      return out;
  }
}

function describe(v) {
  if (v === null) return 'null';
  if (v === undefined) return 'absent';
  if (typeof v === 'string') return v.length > 80 ? v.slice(0, 80) + '...' : v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  if (Array.isArray(v)) return 'list[' + v.length + ']';
  if (isPin(v)) return 'pin(' + v.bytes + ',' + String(v.sha256).slice(0, 12) + ')';
  if (typeof v === 'object') return 'map{' + Object.keys(v).length + '}';
  return String(v);
}

// A snapshot of a set of files and of a set of plain values. The files are pinned; the values are carried as they are.
export function snapshot(options) {
  const { files = [], values = {}, host = (p) => p } = options;
  const out = { files: {}, values: {} };
  for (const entry of files) {
    const key = typeof entry === 'string' ? entry : entry.key;
    const path = typeof entry === 'string' ? entry : entry.path;
    let b = null;
    for (let i = 0; i < 30 && b === null; i++) { try { b = readFileSync(host(path)); } catch { /* retry */ } }
    // Absence is a state. It is recorded as null and compared as null, not skipped.
    out.files[key] = b === null ? null : { path, bytes: b.length, sha256: sha256(b) };
  }
  for (const [k, v] of Object.entries(values)) out.values[k] = v;
  return out;
}

// The comparison of two snapshots, in the same shape as the values.
export function diffSnapshots(before, after) {
  const out = [];
  const keys = new Set([...Object.keys(before.files || {}), ...Object.keys(after.files || {})]);
  for (const k of keys) {
    const inB = Object.prototype.hasOwnProperty.call(before.files || {}, k);
    const inA = Object.prototype.hasOwnProperty.call(after.files || {}, k);
    if (inB !== inA) {
      out.push({ at: 'files.' + k, kind: inB ? 'removed' : 'added' });
      continue;
    }
    out.push(...diffValues(before.files[k], after.files[k], 'files.' + k));
  }
  out.push(...diffValues(before.values || {}, after.values || {}, 'values'));
  return out;
}
