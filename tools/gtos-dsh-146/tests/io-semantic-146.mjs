// GTOS-DSH-146 IO test: run real consumers under the real preload, then break the trace in seven ways.
//
// The classifier is only worth what it refuses. So this test does not build traces by hand and then classify them -- it runs
// small consumers under `node --require io-trace-146.cjs`, so every trace it classifies is a trace the preload actually
// produced, and every fixture consumer is a real process. The negatives are then made by changing the real trace, which is
// the situation a real forged or truncated trace would present.
//
// The seven negatives, and the tier or refusal each is aimed at:
//
//   1. a consumer that stats every required file and reads none     -> metadata_only, refused
//   2. a consumer that reads and hashes every file, parses nothing   -> hash_only, refused
//   3. a required member the consumer never touches                  -> absent, refused
//   4. a trace row with a hash that is not the file's                -> io.no_fabricated_rows
//   5. a trace row for a path that is not there                      -> io.no_fabricated_rows
//   6. a trace with its trace-closed row removed                     -> trace.complete
//   7. a read through a descriptor whose origin is unknown            -> io.fd_is_attributed
//   8. a read of a path outside the package, undeclared               -> io.no_unjustified_external_read
//
// Plus two positives that have to pass, because a classifier that refuses everything is not a classifier:
//
//   9. a consumer that reads and parses every required JSON record  -> all semantic_read
//  10. a consumer that reads a .jsonl file and parses one line       -> that line's file is semantic_read, by attribution to
//      the bytes and not by assumption.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { classify, readTrace, compareClaim } from '../io-semantic-146.mjs';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const PRELOAD = R + '/worktree-146/tools/gtos-dsh-146/io-trace-146.cjs';
const NODE = 'C:/Program Files/nodejs/node.exe';
const PARENT = R + '/test-only/io-semantic-146';
fs.mkdirSync(PARENT, { recursive: true });
let runNumber = 1;
while (fs.existsSync(PARENT + '/run-' + String(runNumber).padStart(2, '0'))) runNumber += 1;
const ROOT = PARENT + '/run-' + String(runNumber).padStart(2, '0');
fs.mkdirSync(ROOT, { recursive: true });

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const normPath = (p) => String(p === null || p === undefined ? '' : p).replace(/\\/g, '/').toLowerCase();
const cases = [];
const rec = (name, expected, observed, extra) => cases.push({ name, expected, observed, ok: expected === observed, ...(extra || {}) });

// ---------------------------------------------------------------- the package under test
// A small package with the three shapes the classifier has to handle: whole-file JSON records, a .jsonl file, and a file
// that is not JSON at all.
const PKG = ROOT + '/package';
fs.mkdirSync(PKG, { recursive: true });
const records = {};
for (const name of ['alpha', 'beta', 'gamma']) {
  const p = PKG + '/' + name + '.json';
  fs.writeFileSync(p, JSON.stringify({ name, value: name.length * 7, ok: true, note: 'record ' + name }, null, 2) + '\n');
  const b = fs.readFileSync(p);
  records[name] = { path: p, bytes: b.length, sha256: sha(b), role: 'record' };
}
const jsonl = PKG + '/ledger.jsonl';
fs.writeFileSync(jsonl, ['{"n":1,"ok":true}', '{"n":2,"ok":true}', '{"n":3,"ok":true}'].join('\n') + '\n');
const jsonlBytes = fs.readFileSync(jsonl);
const jsonlEntry = { path: jsonl, bytes: jsonlBytes.length, sha256: sha(jsonlBytes), role: 'ledger' };
const blob = PKG + '/blob.bin';
fs.writeFileSync(blob, Buffer.from('this file is not JSON at all, and nothing can parse it\n', 'utf8'));
const blobBytes = fs.readFileSync(blob);
const blobEntry = { path: blob, bytes: blobBytes.length, sha256: sha(blobBytes), role: 'blob' };

const REQUIRED = [records.alpha, records.beta, records.gamma];

// ---------------------------------------------------------------- the fixture consumers
function consumer(name, body) {
  const p = ROOT + '/consumers/' + name + '.cjs';
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, "'use strict';\n" + body + '\n');
  return p;
}

// Runs a consumer under the preload and returns the trace path plus the classifier's verdict.
function runConsumer(name, scriptPath, required, opts) {
  const o = opts || {};
  const trace = ROOT + '/traces/' + name + '.jsonl';
  fs.mkdirSync(path.dirname(trace), { recursive: true });
  const r = spawnSync(NODE, ['--require', PRELOAD, scriptPath], {
    encoding: 'utf8', windowsHide: true, maxBuffer: 16 * 1024 * 1024,
    env: { ...process.env, GTOS_IO_TRACE: trace, GTOS_IO_CONSUMER: name },
  });
  // The preload and the consumer's own script are read by node's module loader, so they appear in every trace as reads
  // outside the package. They are the instrument, not the package, so they are declared as external here -- and that
  // declaration is what the "undeclared external read" negative below is measured against.
  const verdict = classify(trace, { required, packageRoot: o.packageRoot === undefined ? PKG : o.packageRoot,
    external: [PRELOAD, scriptPath, ...(o.external || [])] });
  return { trace, status: r.status, stdout: (r.stdout || '').trim(), stderr: (r.stderr || '').slice(0, 300), verdict };
}

// 9. the positive: every record read and parsed.
{
  const script = consumer('full', `
const fs = require('node:fs');
const out = [];
for (const p of ${JSON.stringify(REQUIRED.map((r) => r.path))}) {
  out.push(JSON.parse(fs.readFileSync(p, 'utf8')));
}
process.stdout.write(JSON.stringify({ consumed: out.length, names: out.map((r) => r.name) }));
`);
  const { verdict, status, stdout } = runConsumer('full', script, REQUIRED);
  rec('positive-consumer-exits-zero', 0, status, { stderr: undefined });
  rec('positive-every-record-is-a-semantic-read', 3, verdict.by_tier.semantic_read, { by_tier: verdict.by_tier });
  rec('positive-verdict-is-ok', true, verdict.ok, { refusals: verdict.refusals });
  const claim = JSON.parse(stdout || '{}');
  const claimProblems = compareClaim(claim, verdict);
  rec('positive-consumer-claim-matches-the-trace', 0, claimProblems.length, { problems: claimProblems });
}

// 10. a .jsonl line parse: the file is semantic_read because a parse input is a line of its bytes.
{
  const script = consumer('lineparse', `
const fs = require('node:fs');
const text = fs.readFileSync(${JSON.stringify(jsonl)}, 'utf8');
const lines = text.split('\\n').filter((l) => l.trim());
const rows = lines.map((l) => JSON.parse(l));
process.stdout.write(JSON.stringify({ rows: rows.length }));
`);
  const { verdict } = runConsumer('lineparse', script, [jsonlEntry]);
  rec('a-line-parse-attributes-to-the-file', 'semantic_read', verdict.members[0] && verdict.members[0].tier,
    { parses: verdict.members[0] && verdict.members[0].parses_attributed, trace_parses: verdict.trace.parses });
  rec('a-line-parse-verdict-is-ok', true, verdict.ok, { refusals: verdict.refusals });
}

// 1. stat-only: metadata is not a read.
{
  const script = consumer('statonly', `
const fs = require('node:fs');
for (const p of ${JSON.stringify(REQUIRED.map((r) => r.path))}) { fs.statSync(p); fs.existsSync(p); }
process.stdout.write('done');
`);
  const { verdict } = runConsumer('statonly', script, REQUIRED);
  rec('stat-only-is-metadata-only', 3, verdict.by_tier.metadata_only, { by_tier: verdict.by_tier });
  rec('stat-only-is-refused', false, verdict.ok, { refusals: verdict.refusals.map((r) => r.obligation) });
  rec('stat-only-consumed-nothing-semantically', 0, verdict.semantic);
}

// 2. hash-all: the bytes were preserved, the meaning was not used.
{
  const script = consumer('hashonly', `
const fs = require('node:fs');
const crypto = require('node:crypto');
const hashes = [];
for (const p of ${JSON.stringify(REQUIRED.map((r) => r.path))}) {
  hashes.push(crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'));
}
process.stdout.write(JSON.stringify({ hashes }));
`);
  const { verdict } = runConsumer('hashonly', script, REQUIRED);
  rec('hash-all-is-hash-only', 3, verdict.by_tier.hash_only, { by_tier: verdict.by_tier });
  rec('hash-all-is-refused', false, verdict.ok);
  rec('hash-all-consumed-nothing-semantically', 0, verdict.semantic);
}

// 3. a member nobody touched.
{
  const script = consumer('partial', `
const fs = require('node:fs');
JSON.parse(fs.readFileSync(${JSON.stringify(records.alpha.path)}, 'utf8'));
process.stdout.write('ok');
`);
  const { verdict } = runConsumer('partial', script, REQUIRED);
  rec('an-untouched-member-is-absent', 2, verdict.by_tier.absent, { by_tier: verdict.by_tier });
  rec('an-untouched-member-is-refused', false, verdict.ok);
}

// 4 and 5. fabricated rows, made by editing a real trace.
{
  const script = consumer('full2', `
const fs = require('node:fs');
for (const p of ${JSON.stringify(REQUIRED.map((r) => r.path))}) JSON.parse(fs.readFileSync(p, 'utf8'));
process.stdout.write('ok');
`);
  const { trace, verdict } = runConsumer('full2', script, REQUIRED);
  rec('the-base-trace-for-the-fabrication-cases-is-ok', true, verdict.ok);

  // 4. one read row's hash changed. The row is found by the path it names, not by its position: an earlier version of this
  // case indexed a fixed row number and silently changed nothing, so it was asserting that a trace with no forgery in it was
  // fine -- which is the failure mode of a test that passes for the wrong reason.
  const rows = readTrace(trace).rows;
  const victim = normPath(records.alpha.path);
  let forgedOne = false;
  const forged = rows.map((r) => {
    if (forgedOne || r.op !== 'readFileSync' || normPath(r.path) !== victim) return r;
    forgedOne = true;
    return { ...r, sha256: 'f'.repeat(64) };
  });
  rec('the-forgery-changed-a-row', true, forgedOne);
  const forgedTrace = ROOT + '/traces/forged-hash.jsonl';
  fs.writeFileSync(forgedTrace, forged.map((r) => JSON.stringify(r)).join('\n') + '\n');
  const v4 = classify(forgedTrace, { required: REQUIRED, packageRoot: PKG, external: [PRELOAD, script] });
  rec('a-forged-hash-is-refused', true, v4.refusals.some((r) => r.obligation === 'io.no_fabricated_rows'),
    { refusals: v4.refusals.map((r) => r.obligation) });
  rec('a-forged-hash-leaves-that-member-not-semantic', false, v4.ok);

  // 5. a read row for a path that is not there.
  const invented = [...rows, { consumer: 'full2', op: 'readFileSync', via: 'path', path: PKG + '/does-not-exist.json',
    bytes: 12, sha256: 'a'.repeat(64) }];
  const inventedTrace = ROOT + '/traces/forged-path.jsonl';
  fs.writeFileSync(inventedTrace, invented.map((r) => JSON.stringify(r)).join('\n') + '\n');
  const v5 = classify(inventedTrace, { required: REQUIRED, packageRoot: PKG, external: [PRELOAD, script] });
  rec('a-read-of-a-path-that-is-not-there-is-refused', true,
    v5.refusals.some((r) => r.obligation === 'io.no_fabricated_rows'), { refusals: v5.refusals.map((r) => r.obligation) });

  // 6. the trace-closed row removed.
  const truncated = rows.filter((r) => r.op !== 'trace-closed');
  const truncatedTrace = ROOT + '/traces/truncated.jsonl';
  fs.writeFileSync(truncatedTrace, truncated.map((r) => JSON.stringify(r)).join('\n') + '\n');
  const v6 = classify(truncatedTrace, { required: REQUIRED, packageRoot: PKG, external: [PRELOAD, script] });
  rec('a-truncated-trace-is-refused', true, v6.refusals.some((r) => r.obligation === 'trace.complete'),
    { refusals: v6.refusals.map((r) => r.obligation) });
  rec('a-truncated-trace-is-not-ok', false, v6.ok);
}

// 7. a descriptor whose origin is unknown. The real preload attributes a descriptor it opened, so the unattributed case is
// made by removing the open row from a real descriptor read -- which is exactly the shape of a trace from a preload that
// forgot to remember descriptors.
{
  const script = consumer('fdread', `
const fs = require('node:fs');
const fd = fs.openSync(${JSON.stringify(records.alpha.path)}, 'r');
const text = fs.readFileSync(fd, 'utf8');
fs.closeSync(fd);
JSON.parse(text);
process.stdout.write('ok');
`);
  const { trace, verdict } = runConsumer('fdread', script, [records.alpha]);
  rec('an-attributed-descriptor-read-is-accepted', 'semantic_read', verdict.members[0] && verdict.members[0].tier,
    { trace_rows: verdict.trace.rows });
  rec('an-attributed-descriptor-read-is-ok', true, verdict.ok, { refusals: verdict.refusals });

  // Now make the descriptor read look like one from a tracer that did not remember descriptors: the row keeps its bytes and
  // its hash, but names `fd:3` instead of the path. Dropping the open row alone would not do it, because the preload recorded
  // the attribution at read time -- which is the point of recording it there.
  const fdRows = readTrace(trace).rows.map((r) => (r.op === 'readFileSync' && r.via === 'fd'
    ? { ...r, path: 'fd:3', fd_attributed: false } : r));
  const noOpen = ROOT + '/traces/fd-unattributed.jsonl';
  fs.writeFileSync(noOpen, fdRows.map((r) => JSON.stringify(r)).join('\n') + '\n');
  const v7 = classify(noOpen, { required: [records.alpha], packageRoot: PKG, external: [PRELOAD, script] });
  rec('a-descriptor-with-no-known-origin-is-refused', true,
    v7.refusals.some((r) => r.obligation === 'io.fd_is_attributed'), { refusals: v7.refusals.map((r) => r.obligation) });
}

// 8. a read outside the package that the packet did not declare.
{
  const outside = ROOT + '/outside/secret.json';
  fs.mkdirSync(path.dirname(outside), { recursive: true });
  fs.writeFileSync(outside, JSON.stringify({ secret: 'not in the package' }, null, 2) + '\n');
  const script = consumer('external', `
const fs = require('node:fs');
for (const p of ${JSON.stringify(REQUIRED.map((r) => r.path))}) JSON.parse(fs.readFileSync(p, 'utf8'));
const extra = JSON.parse(fs.readFileSync(${JSON.stringify(outside)}, 'utf8'));
process.stdout.write(JSON.stringify(extra));
`);
  const { verdict } = runConsumer('external', script, REQUIRED);
  rec('an-undeclared-external-read-is-refused', true,
    verdict.refusals.some((r) => r.obligation === 'io.no_unjustified_external_read'),
    { refusals: verdict.refusals.map((r) => r.obligation) });
  // Three reads fall outside the package: the preload, the consumer's own script (both declared) and the secret (not).
  rec('the-external-read-is-recorded', 1, verdict.outside_reads.filter((r) => !r.allowed).length,
    { outside: verdict.outside_reads.map((r) => ({ path: path.basename(r.path), allowed: r.allowed })) });

  // And the same run with the path declared: the read is then not a refusal, and the member classification is unaffected.
  const declared = classify(verdict.trace.path, { required: REQUIRED, packageRoot: PKG, external: [PRELOAD, script, outside] });
  rec('a-declared-external-read-is-not-refused', true, declared.ok, { refusals: declared.refusals.map((r) => r.obligation) });
  // Two reads fall outside the package in this run: the consumer's own script and the secret. The preload itself is loaded by
  // node's internal require, not through the `fs` entry points this tracer replaces, so it does not appear -- and that is
  // worth knowing, because it means the harness is one read smaller than it looks.
  rec('a-declared-external-read-is-still-recorded', 2, declared.outside_reads.filter((r) => r.allowed).length);
}

// The file: URL case. A module imported through `import()` is opened by node with a file: URL, and a tracer that resolved it
// with path.resolve would record `F:\...\file:\F:\...`, which names nothing and would make a real read look absent.
{
  const script = consumer('urlread', `
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const url = pathToFileURL(${JSON.stringify(records.beta.path)});
const text = fs.readFileSync(url, 'utf8');
JSON.parse(text);
process.stdout.write('ok');
`);
  const { verdict } = runConsumer('urlread', script, [records.beta]);
  rec('a-file-url-read-is-attributed-to-the-path', 'semantic_read', verdict.members[0] && verdict.members[0].tier,
    { path_in_trace: verdict.members[0] && verdict.members[0].reads, refusals: verdict.refusals });
}

// A non-JSON file cannot be a semantic read, and the classifier has to say so rather than passing it because the bytes were
// read: nothing can parse it, so nothing consumed it.
{
  const script = consumer('blob', `
const fs = require('node:fs');
const b = fs.readFileSync(${JSON.stringify(blob)}, 'utf8');
process.stdout.write(String(b.length));
`);
  const { verdict } = runConsumer('blob', script, [blobEntry]);
  rec('a-non-json-file-read-without-a-parse-is-hash-only', 'hash_only', verdict.members[0] && verdict.members[0].tier,
    { by_tier: verdict.by_tier });
  rec('a-non-json-file-is-not-accepted-as-consumed', false, verdict.ok);
}

const out = {
  task: 'GTOS-DSH-146',
  stage: 'io-semantic-146-01',
  what: 'the IO classifier driven by real consumers under the real preload: metadata is not a read, a hash is not a semantic '
    + 'read, and seven ways of making a trace lie are refused',
  preload: { path: PRELOAD, bytes: fs.statSync(PRELOAD).size, sha256: sha(fs.readFileSync(PRELOAD)) },
  package: { root: PKG, required: REQUIRED.map((r) => ({ path: r.path, bytes: r.bytes, sha256: r.sha256 })),
    jsonl: { path: jsonlEntry.path, bytes: jsonlEntry.bytes, sha256: jsonlEntry.sha256 },
    blob: { path: blobEntry.path, bytes: blobEntry.bytes, sha256: blobEntry.sha256 } },
  cases,
  counts: { cases: cases.length, passed: cases.filter((c) => c.ok).length, failed: cases.filter((c) => !c.ok).length },
  ok: cases.every((c) => c.ok),
  ok_means: 'every positive consumer is classified semantic_read and every negative is either put in the right lower tier or '
    + 'refused by the obligation it was aimed at. The traces are produced by the preload, not written by this test.',
};
const outPath = R + '/io-semantic-146-01.json';
if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify(out.counts), out.ok ? 'OK' : 'FAILED');
for (const c of cases) if (!c.ok) console.log('  FAIL', c.name, 'want', JSON.stringify(c.expected), 'got', JSON.stringify(c.observed), JSON.stringify(c.refusals || c.problems || ''));
