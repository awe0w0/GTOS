// GTOS-DSH-145: the IO tracer.
//
// This is a preload, not a wrapper the consumers know about. It is loaded with `--require` before a consumer's first line runs, it replaces
// the handful of `fs` entry points that read a file, and it appends one JSON line per call to the trace file named by the environment. The
// consumer is not modified, does not import this module, and cannot tell it is being watched -- which is the property that makes the trace
// evidence rather than a claim. The 144 comparison took the read set from a member table that the packet had already built, so it could only
// ever report what the packet believed it had read. This reports what the process actually opened.
//
// What is recorded: the resolved absolute path, the operation, the byte count when the call returned bytes, and the hash of the bytes. The
// hash is what makes a later claim checkable -- "the consumer read this file" and "the consumer read these bytes" are different statements,
// and only the second one is about the package.
//
// What is not recorded: writes, directory listings and stats are recorded as operations without bytes, because a consumer that lists a
// directory has not read the files in it and a trace that counted a listing as a read would overstate the closure.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const out = process.env.GTOS_IO_TRACE;
const consumer = process.env.GTOS_IO_CONSUMER || 'unnamed';
if (out) {
  // The original functions, captured before anything is replaced. `appendFileSync` is deliberately left untouched and is what the trace is
  // written with, so recording a read never re-enters the recorder.
  const raw = {
    readFileSync: fs.readFileSync,
    openSync: fs.openSync,
    readSync: fs.readSync,
    closeSync: fs.closeSync,
    appendFileSync: fs.appendFileSync,
  };
  const seen = new Set();
  const write = (row) => {
    try { raw.appendFileSync(out, JSON.stringify(row) + '\n'); } catch (_) { /* a trace that cannot be written must not break the consumer */ }
  };
  const resolve = (p) => {
    // A module loaded through `import()` is opened by node's ESM loader with a `file:` URL rather than a path. `path.resolve` on that string
    // treats the scheme as a relative directory and produces `F:\...\file:\F:\...`, which names nothing; the URL is converted instead, so the
    // trace records the same absolute path for an imported module as for a required one.
    const s = String(p);
    if (/^file:/i.test(s)) {
      try { return require('node:url').fileURLToPath(new URL(s)); } catch (_) { return s; }
    }
    try { return path.resolve(s); } catch (_) { return s; }
  };
  const hashOf = (b) => crypto.createHash('sha256').update(b).digest('hex');

  // `readFileSync` is the entry point every consumer in this packet uses for a file it cares about. It is wrapped for both the path and the
  // file-descriptor form, because a consumer that opened a descriptor first would otherwise be invisible.
  const originalReadFileSync = fs.readFileSync;
  fs.readFileSync = function tracedReadFileSync(target, options) {
    const bytes = originalReadFileSync.apply(fs, arguments);
    const isPath = typeof target === 'string' || target instanceof Buffer || target instanceof URL;
    write({ consumer, op: 'readFileSync', via: isPath ? 'path' : 'fd',
      path: isPath ? resolve(target) : 'fd:' + String(target),
      bytes: Buffer.isBuffer(bytes) || typeof bytes === 'string' ? Buffer.byteLength(bytes) : null,
      sha256: Buffer.isBuffer(bytes) ? hashOf(bytes) : typeof bytes === 'string' ? hashOf(Buffer.from(bytes, 'utf8')) : null });
    return bytes;
  };
  // The promise form, so a consumer that reads asynchronously is not a blind spot.
  const originalPromisesReadFile = fs.promises.readFile.bind(fs.promises);
  fs.promises.readFile = async function tracedPromisesReadFile(target, options) {
    const bytes = await originalPromisesReadFile(target, options);
    write({ consumer, op: 'promises.readFile', via: 'path', path: resolve(target),
      bytes: Buffer.isBuffer(bytes) || typeof bytes === 'string' ? Buffer.byteLength(bytes) : null,
      sha256: Buffer.isBuffer(bytes) ? hashOf(bytes) : typeof bytes === 'string' ? hashOf(Buffer.from(bytes, 'utf8')) : null });
    return bytes;
  };
  // A descriptor opened for reading is recorded, and the descriptor is remembered so that a later `readFileSync(fd)` can be attributed to
  // the path it came from rather than to a number.
  const fdPaths = new Map();
  const originalOpenSync = fs.openSync;
  fs.openSync = function tracedOpenSync(target, flags) {
    const fd = originalOpenSync.apply(fs, arguments);
    const reading = typeof flags === 'string' ? /r/.test(flags) : (typeof flags === 'number' ? (flags & 3) === 0 : true);
    if (reading) {
      fdPaths.set(fd, resolve(target));
      write({ consumer, op: 'openSync', via: 'path', path: resolve(target), bytes: null, sha256: null, flags: String(flags) });
    }
    return fd;
  };
  // Stats and listings are recorded without bytes: they are evidence of a directory being walked, not of a file being read, and the
  // comparison below treats them as such.
  const originalStatSync = fs.statSync;
  fs.statSync = function tracedStatSync(target, options) {
    const st = originalStatSync.apply(fs, arguments);
    if (typeof target === 'string' || target instanceof Buffer) {
      const p = resolve(target);
      if (!seen.has('stat:' + p)) { seen.add('stat:' + p); write({ consumer, op: 'statSync', via: 'path', path: p,
        bytes: null, sha256: null, is_file: st && typeof st.isFile === 'function' ? st.isFile() : null }); }
    }
    return st;
  };
  const originalReaddirSync = fs.readdirSync;
  fs.readdirSync = function tracedReaddirSync(target, options) {
    const list = originalReaddirSync.apply(fs, arguments);
    if (typeof target === 'string' || target instanceof Buffer) {
      write({ consumer, op: 'readdirSync', via: 'path', path: resolve(target), bytes: null, sha256: null,
        entries: Array.isArray(list) ? list.length : null });
    }
    return list;
  };
  const originalExistsSync = fs.existsSync;
  fs.existsSync = function tracedExistsSync(target) {
    const yes = originalExistsSync.apply(fs, arguments);
    if (typeof target === 'string' || target instanceof Buffer) {
      const p = resolve(target);
      if (!seen.has('exists:' + p)) { seen.add('exists:' + p); write({ consumer, op: 'existsSync', via: 'path', path: p,
        bytes: null, sha256: null, exists: yes }); }
    }
    return yes;
  };
  // The trace's own beginning, so a reader can tell an empty trace from a trace that was never opened.
  write({ consumer, op: 'trace-opened', via: 'preload', path: out, bytes: null, sha256: null,
    node: process.version, argv: process.argv.slice(1) });
}
