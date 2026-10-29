// GTOS-DSH-146: the IO tracer, extended from 145's so that a read can be told apart from a hash.
//
// This is a preload, not a wrapper the consumers know about. It is loaded with `--require` before a consumer's first line
// runs, replaces the `fs` entry points that touch a file, and appends one JSON line per call. The consumer is not modified,
// does not import this module, and cannot tell it is being watched -- which is what makes the trace evidence rather than a
// claim.
//
// 145's tracer recorded reads and their hashes, and that was not enough for the question 146 has to answer. "The consumer
// read these bytes" and "the consumer used what these bytes say" are different statements: a consumer that opens a record,
// hashes it and compares the hash to a pinned value has preserved the bytes without consuming the semantics, and 145's trace
// could not tell that apart from a consumer that parsed the record and acted on a field in it.
//
// So this tracer also wraps `JSON.parse`. Every parse is recorded with the hash and length of the string it was handed, and
// the classifier below attributes a parse to a file when the parse input is that file (the ordinary case: the consumer reads
// the whole file and parses it) or a line of it (the `.jsonl` case). A read with no attributable parse is a hash, not a read
// of meaning -- and the classifier says so rather than counting it.
//
// Two other things this records that 145's did not:
//
//   * `trace-closed`, written from an exit hook. A trace without it is truncated, and a truncated trace cannot be evidence
//     that a consumer did not read something: the rows that would have shown it may be the ones that were lost.
//   * the descriptor's origin. `readFileSync(fd)` is recorded against the path the descriptor was opened from, and a
//     descriptor whose origin is unknown is recorded as `fd:<n>` and refuses to be counted, rather than being guessed at.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const out = process.env.GTOS_IO_TRACE;
const consumer = process.env.GTOS_IO_CONSUMER || 'unnamed';
const role = process.env.GTOS_IO_ROLE || null;

if (out) {
  // The originals, captured before anything is replaced. `appendFileSync` is left untouched and is what the trace is written
  // with, so recording a read never re-enters the recorder.
  const raw = {
    readFileSync: fs.readFileSync,
    appendFileSync: fs.appendFileSync,
    openSync: fs.openSync,
  };
  const seen = new Set();
  const write = (row) => {
    try { raw.appendFileSync(out, JSON.stringify(row) + '\n'); } catch { /* a trace that cannot be written must not break the consumer */ }
  };
  const resolve = (p) => {
    // A module loaded through `import()` is opened by node's ESM loader with a `file:` URL rather than a path. `path.resolve`
    // on that string treats the scheme as a relative directory and produces `F:\...\file:\F:\...`, which names nothing; the
    // URL is converted instead, so the trace records one absolute path for an imported module and for a required one.
    const s = String(p);
    if (/^file:/i.test(s)) {
      try { return require('node:url').fileURLToPath(new URL(s)); } catch { return s; }
    }
    try { return path.resolve(s); } catch { return s; }
  };
  const hashOf = (b) => crypto.createHash('sha256').update(b).digest('hex');
  const bytesOf = (b) => (Buffer.isBuffer(b) || typeof b === 'string' ? Buffer.byteLength(b) : null);
  const shaOfValue = (b) => (Buffer.isBuffer(b) ? hashOf(b) : typeof b === 'string' ? hashOf(Buffer.from(b, 'utf8')) : null);

  // `readFileSync` is the entry point every consumer in this packet uses for a file it cares about. Both the path form and
  // the descriptor form are wrapped, because a consumer that opened a descriptor first would otherwise be invisible.
  const fdPaths = new Map();
  const originalReadFileSync = fs.readFileSync;
  fs.readFileSync = function tracedReadFileSync(target, options) {
    const bytes = originalReadFileSync.apply(fs, arguments);
    const isPath = typeof target === 'string' || target instanceof Buffer || target instanceof URL;
    const viaFd = !isPath;
    const fdOrigin = viaFd ? (fdPaths.get(target) || null) : null;
    write({ consumer, role, op: 'readFileSync', via: viaFd ? 'fd' : 'path',
      path: isPath ? resolve(target) : (fdOrigin ? fdOrigin : 'fd:' + String(target)),
      fd_attributed: viaFd ? fdOrigin !== null : true,
      bytes: bytesOf(bytes), sha256: shaOfValue(bytes) });
    return bytes;
  };
  const originalPromisesReadFile = fs.promises.readFile.bind(fs.promises);
  fs.promises.readFile = async function tracedPromisesReadFile(target, options) {
    const bytes = await originalPromisesReadFile(target, options);
    write({ consumer, role, op: 'promises.readFile', via: 'path', path: resolve(target), fd_attributed: true,
      bytes: bytesOf(bytes), sha256: shaOfValue(bytes) });
    return bytes;
  };
  // A descriptor opened for reading is remembered, so a later `readFileSync(fd)` is attributed to the path it came from
  // rather than to a number. Opening is metadata: it says a file was made available, not that anything in it was used.
  const originalOpenSync = fs.openSync;
  fs.openSync = function tracedOpenSync(target, flags) {
    const fd = originalOpenSync.apply(fs, arguments);
    const reading = typeof flags === 'string' ? /r/.test(flags) : (typeof flags === 'number' ? (flags & 3) === 0 : true);
    if (reading) {
      fdPaths.set(fd, resolve(target));
      write({ consumer, role, op: 'openSync', via: 'path', path: resolve(target), bytes: null, sha256: null, flags: String(flags) });
    }
    return fd;
  };
  // Stats, existence checks and listings are recorded without bytes: they are evidence of a path being looked at, not of a
  // file being read, and the classifier treats them as such.
  const originalStatSync = fs.statSync;
  fs.statSync = function tracedStatSync(target, options) {
    const st = originalStatSync.apply(fs, arguments);
    if (typeof target === 'string' || target instanceof Buffer) {
      const p = resolve(target);
      if (!seen.has('stat:' + p)) {
        seen.add('stat:' + p);
        write({ consumer, role, op: 'statSync', via: 'path', path: p, bytes: null, sha256: null,
          is_file: st && typeof st.isFile === 'function' ? st.isFile() : null });
      }
    }
    return st;
  };
  const originalExistsSync = fs.existsSync;
  fs.existsSync = function tracedExistsSync(target) {
    const yes = originalExistsSync.apply(fs, arguments);
    if (typeof target === 'string' || target instanceof Buffer) {
      const p = resolve(target);
      if (!seen.has('exists:' + p)) {
        seen.add('exists:' + p);
        write({ consumer, role, op: 'existsSync', via: 'path', path: p, bytes: null, sha256: null, exists: yes });
      }
    }
    return yes;
  };
  const originalReaddirSync = fs.readdirSync;
  fs.readdirSync = function tracedReaddirSync(target, options) {
    const list = originalReaddirSync.apply(fs, arguments);
    if (typeof target === 'string' || target instanceof Buffer) {
      write({ consumer, role, op: 'readdirSync', via: 'path', path: resolve(target), bytes: null, sha256: null,
        entries: Array.isArray(list) ? list.length : null });
    }
    return list;
  };

  // THE NEW ONE. A parse is what turns bytes into meaning. Recorded with the hash and length of the string that was handed
  // over, so the classifier can attribute it to a file: an exact match is a whole-file parse, and a parse input that is a
  // line of a read file is a line parse.
  const originalParse = JSON.parse;
  JSON.parse = function tracedParse(text, reviver) {
    if (typeof text === 'string') {
      write({ consumer, role, op: 'json.parse', via: 'string', path: null, bytes: Buffer.byteLength(text),
        sha256: hashOf(Buffer.from(text, 'utf8')), prefix: text.slice(0, 48) });
    }
    return originalParse.call(JSON, text, reviver);
  };

  // The trace's own boundaries, so a reader can tell an empty trace from one that was never opened, and a complete trace from
  // a truncated one. `process.on('exit')` runs after the consumer's last line and before the process ends.
  write({ consumer, role, op: 'trace-opened', via: 'preload', path: out, bytes: null, sha256: null,
    node: process.version, argv: process.argv.slice(1) });
  process.on('exit', (code) => {
    write({ consumer, role, op: 'trace-closed', via: 'preload', path: out, bytes: null, sha256: null, exit_code: code });
  });
}
