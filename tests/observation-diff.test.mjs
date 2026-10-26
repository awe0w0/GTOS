// GTOS-DSH-128 J0a: the comparator must be able to fail.
//
// The 127 comparator reported "diff = 0" and that number could not have been anything else for a primitive field: it
// compared a changed count through a pin comparison, where both sides read `.bytes` as undefined. This test proves the
// opposite property for this module: for every kind of change, a real one must be reported, with the right location.
//
// Every change below is made to a real file or a real value in a TEST_ONLY root. None of it is a decoration nobody reads.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as od from '../tools/observation-diff.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-trusted-observation-link-admission-runtime-recovery-20261026-a';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const T = R + '/test-only/j0a';
if (fs.existsSync(T)) throw new Error('the fixture already exists; this test uses a fresh namespace: ' + T);
fs.mkdirSync(T, { recursive: true });

// ---------------------------------------------------------------- the fixture files
// Two files of the same size, so a one-byte change cannot be detected by length alone.
fs.writeFileSync(T + '/same-size-a.bin', Buffer.from('AAAAAAAA'));
fs.writeFileSync(T + '/same-size-b.bin', Buffer.from('BBBBBBBB'));
fs.writeFileSync(T + '/short.bin', Buffer.from('AB'));

const FILES = [
  { key: 'a', path: T + '/same-size-a.bin' },
  { key: 'b', path: T + '/same-size-b.bin' },
  { key: 'short', path: T + '/short.bin' },
  // A key whose file does not exist: absence is a state and must be comparable.
  { key: 'absent', path: T + '/never-created.bin' },
];
const VALUES = { attempts: 3, results: 3, label: 'before', flag: true, nothing: null };

// ---------------------------------------------------------------- the positive: no change, no difference
const before = od.snapshot({ files: FILES, values: VALUES });
const unchanged = od.snapshot({ files: FILES, values: VALUES });
check('an unchanged snapshot produces no difference', od.diffSnapshots(before, unchanged).length === 0,
  od.diffSnapshots(before, unchanged));
check('the snapshot actually pinned real files',
  before.files.a !== null && before.files.b !== null && before.files.short !== null,
  { a: before.files.a && before.files.a.bytes, b: before.files.b && before.files.b.bytes });
check('the snapshot recorded absence as absence', before.files.absent === null, before.files.absent);

// ---------------------------------------------------------------- every kind of real change is reported
function expectOne(name, mutate, at, kind) {
  const after = mutate();
  const d = od.diffSnapshots(before, after);
  const hit = d.some((x) => x.at === at && (kind === undefined || x.kind === kind));
  check(name, d.length > 0 && hit, { at, kind, found: d });
  return d;
}

// 1. A one-byte change in a file of unchanged size.
expectOne('a one-byte change in a same-size file is reported', () => {
  const b = Buffer.from(fs.readFileSync(T + '/same-size-a.bin'));
  b[0] = 0x42;
  fs.writeFileSync(T + '/same-size-a.bin', b);
  return od.snapshot({ files: FILES, values: VALUES });
}, 'files.a.sha256', 'string');
check('the one-byte change kept the file size the same, so only the hash could catch it',
  fs.statSync(T + '/same-size-a.bin').size === 8, fs.statSync(T + '/same-size-a.bin').size);

// 2. A different size.
fs.writeFileSync(T + '/same-size-b.bin', Buffer.from('BBBBBBBBBB'));
expectOne('a change of size is reported', () => od.snapshot({ files: FILES, values: VALUES }),
  'files.b.bytes', 'number');

// 3. A file that appears where nothing was.
fs.writeFileSync(T + '/never-created.bin', Buffer.from('now here'));
expectOne('a file that appears where there was absence is reported',
  () => od.snapshot({ files: FILES, values: VALUES }), 'files.absent', undefined);
check('absence to presence is a difference of kind, not of value',
  od.diffSnapshots(before, od.snapshot({ files: FILES, values: VALUES }))
    .some((x) => x.at === 'files.absent' && x.kind === 'type'), null);

// 4. A file that disappears.
fs.rmSync(T + '/never-created.bin');
fs.rmSync(T + '/short.bin');
expectOne('a file that disappears is reported', () => od.snapshot({ files: FILES, values: VALUES }),
  'files.short', undefined);

// 5. A count that changes. This is the exact case 127 could not see.
{
  const after = od.snapshot({ files: FILES, values: { ...VALUES, attempts: 4 } });
  const d = od.diffSnapshots(before, after);
  check('a changed count is reported', d.some((x) => x.at === 'values.attempts' && x.kind === 'number'),
    d);
  check('the changed count names both values', d.some((x) => x.at === 'values.attempts'
    && x.before === 3 && x.after === 4), d.filter((x) => x.at === 'values.attempts'));
  // And the 127 shape is demonstrably not what this module does: the old comparison would have matched these.
  const oldStyle = (a, b) => (a === null && b === null) || (a !== null && b !== null
    && a.bytes === b.bytes && a.sha256 === b.sha256);
  check('the 127 pin comparison would have called these two counts equal, and this module does not',
    oldStyle(3, 4) === true && od.diffValues(3, 4, 'v').length === 1,
    { old_style_said_equal: oldStyle(3, 4), new_module_differences: od.diffValues(3, 4, 'v').length });
}

// 6. A hash string that changes.
{
  const after = od.snapshot({ files: FILES, values: { ...VALUES, digest: 'a'.repeat(64) } });
  const d = od.diffSnapshots(od.snapshot({ files: FILES, values: { ...VALUES, digest: 'a'.repeat(64) } }), after);
  check('an identical hash string is not a difference', d.length === 0, d);
  const changed = od.diffSnapshots(od.snapshot({ files: FILES, values: { ...VALUES, digest: 'a'.repeat(64) } }),
    od.snapshot({ files: FILES, values: { ...VALUES, digest: 'b'.repeat(64) } }));
  check('a changed hash string is a difference', changed.some((x) => x.at === 'values.digest' && x.kind === 'string'),
    changed);
}

// 7. A null that becomes a value, and a value that becomes null. Neither is the same as absence.
{
  const d = od.diffSnapshots(before, od.snapshot({ files: FILES, values: { ...VALUES, nothing: 0 } }));
  check('null to zero is a difference of type', d.some((x) => x.at === 'values.nothing' && x.kind === 'type'), d);
  const d2 = od.diffSnapshots(od.snapshot({ files: FILES, values: { ...VALUES, nothing: 0 } }),
    od.snapshot({ files: FILES, values: { ...VALUES, nothing: null } }));
  check('zero to null is a difference of type', d2.some((x) => x.at === 'values.nothing' && x.kind === 'type'), d2);
}

// 8. A registry-style entry: one JSON object added to a list of rows.
{
  const rowsBefore = { rows: [{ stage: 'a-01', seq: 1 }, { stage: 'a-02', seq: 2 }] };
  const rowsAfter = { rows: [{ stage: 'a-01', seq: 1 }, { stage: 'a-02', seq: 2 }, { stage: 'a-03', seq: 3 }] };
  const d = od.diffValues(rowsBefore, rowsAfter, 'reg');
  check('a row added to a list is reported', d.some((x) => x.at === 'reg.rows.length' && x.before === 2 && x.after === 3),
    d);
  // A changed field inside a row.
  const rowsEdited = { rows: [{ stage: 'a-01', seq: 1 }, { stage: 'a-02', seq: 99 }] };
  const d2 = od.diffValues(rowsBefore, rowsEdited, 'reg');
  check('a changed field inside a row is reported',
    d2.some((x) => x.at === 'reg.rows[1].seq' && x.before === 2 && x.after === 99), d2);
  // A removed row.
  const rowsShort = { rows: [{ stage: 'a-01', seq: 1 }] };
  const d3 = od.diffValues(rowsBefore, rowsShort, 'reg');
  check('a row removed from a list is reported', d3.some((x) => x.at === 'reg.rows.length'), d3);
}

// 9. A map key that appears or disappears is not the same as a null value.
{
  const d = od.diffValues({ a: 1 }, { a: 1, b: null }, 'm');
  check('a new key whose value is null is reported as added, not as unchanged',
    d.some((x) => x.at === 'm.b' && x.kind === 'added'), d);
  const d2 = od.diffValues({ a: 1, b: null }, { a: 1 }, 'm');
  check('a removed key whose value was null is reported as removed',
    d2.some((x) => x.at === 'm.b' && x.kind === 'removed'), d2);
}

// ---------------------------------------------------------------- a real pollution positive
// The whole point: take a real record set, change one byte of one real file, and require the snapshot comparison to catch
// it. This is the check that would have caught the 127 instrument.
{
  const clean = od.snapshot({ files: FILES, values: VALUES });
  const victim = T + '/same-size-a.bin';
  const original = fs.readFileSync(victim);
  const polluted = Buffer.from(original);
  polluted[polluted.length - 1] = polluted[polluted.length - 1] ^ 0xff;
  fs.writeFileSync(victim, polluted);
  const afterPollution = od.snapshot({ files: FILES, values: VALUES });
  const d = od.diffSnapshots(clean, afterPollution);
  check('a real one-byte pollution of a real file is detected', d.length > 0
    && d.some((x) => x.at === 'files.a.sha256'), d);
  check('the pollution did not change the file size, so size alone would have missed it',
    fs.statSync(victim).size === original.length, { size: fs.statSync(victim).size, was: original.length });
  // Restore, and require the comparison to return to no difference.
  fs.writeFileSync(victim, original);
  check('restoring the byte returns the comparison to no difference',
    od.diffSnapshots(clean, od.snapshot({ files: FILES, values: VALUES })).length === 0, null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-128', kind: 'observation_diff_test',
  subject: 'references/observation-diff-128.mjs',
  fixture: T,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
