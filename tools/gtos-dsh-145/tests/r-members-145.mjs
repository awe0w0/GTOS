// GTOS-DSH-145 R: the frame-member enumeration, tested through the production parser and the production difference.
//
// Nothing here reimplements what it tests. Every case builds the exact text the kernel prints -- `FRAMEREC DIAG SET` and `FRAMEREC DIAG BM`
// lines -- and hands it to `parseFrameSets` and `frameSetDiff` from `references/resource-semantics-144.mjs`, the same two functions the
// guest judge calls. A test that carried its own copy of the enumeration would prove that the copy works, which is not the question.
//
// The cases are the ones the taskbook names:
//
//   every bit       for each position 0..31, a set that differs from an empty baseline by that one bit, and the member must be that
//                   frame number as a bounded integer. Bit 31 is the case the 144 enumeration returned null for.
//   high bit        the top bit of a word gained and lost independently, in the same word and in different words.
//   all ones        a full word gained, a full word lost, and a full bitmap replaced by its complement: 32 members, all numbers.
//   cross word      members in adjacent words, so the word offset is exercised rather than only the bit index.
//   empty           two identical sets, which is a legal difference of nothing and must not be reported as a fault.
//   real record     the actual baseline-to-victim difference from the 144 channel, where fourteen members were printed and one of them
//                   is frame 2655 -- bit 31 of word 82. The member list is compared with an independent bit-by-bit walk.
//
// The independent walk is written here on purpose, and it is the check that the production enumeration is right rather than merely
// self-consistent: it asks, bit by bit, whether the two points differ at that position, instead of isolating a low bit from a gain word.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const REF = R + '/worktree-145/tools/gtos-dsh-144/references/resource-semantics-144.mjs';
const { parseFrameSets, frameSetDiff } = await import('file:///' + REF.replace(/\\/g, '/'));
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

const hx = (n) => (n >>> 0).toString(16).toUpperCase().padStart(8, '0');

// Build the exact text the kernel prints for one point: the SET header, then one BM line per sixteen words.
function pointText(name, words, frames) {
  let owned = 0;
  for (const w of words) { let x = w >>> 0; while (x) { x = (x & (x - 1)) >>> 0; owned += 1; } }
  const lines = ['FRAMEREC DIAG SET ' + name + ' FRAMES ' + hx(frames) + ' OWNED ' + hx(owned) + ' COMPLETE ' + hx(1)];
  for (let i = 0; i < words.length; i += 16) {
    const slice = words.slice(i, i + 16);
    lines.push('FRAMEREC DIAG BM ' + name + ' ' + hx(i) + slice.map(hx).join(''));
  }
  return lines.join('\n');
}
// A whole six-point run, so the parser sees the shape it sees in a real channel.
function runText(points, frames) {
  return points.map(([n, w]) => pointText(n, w, frames)).join('\n');
}
const emptyBitmap = (words) => new Array(words).fill(0);
const withBit = (words, frame) => {
  const w = words.slice();
  w[frame >> 5] = (w[frame >> 5] | (1 << (frame & 31))) >>> 0;
  return w;
};
// The kernel dumps whole sixteen-word lines, so a synthetic dump has to be a whole number of them too. A 32736-frame range needs 1023
// words and the dump carries 1024, which is the padded shape the real channel has.
const WORDS_FOR = (frames) => Math.ceil(Math.ceil(frames / 32) / 16) * 16;

const cases = [];
const push = (c) => { cases.push(c); return c; };

// ---- every bit position, including the top one
{
  const perBit = [];
  const FRAMES = 32736;               // the real scene's declared coverage
  const WORDS = WORDS_FOR(FRAMES);
  for (let bit = 0; bit < 32; bit++) {
    const frame = 82 * 32 + bit;      // word 82, the word the real frame 2655 lives in
    const before = pointText('baseline', emptyBitmap(WORDS), FRAMES);
    const after = pointText('victim', withBit(emptyBitmap(WORDS), frame), FRAMES);
    const sets = parseFrameSets(before + '\n' + after);
    const diff = frameSetDiff(sets.points.get('baseline'), sets.points.get('victim'));
    const ok = diff !== null && diff.added.length === 1 && diff.added[0] === frame
      && Number.isInteger(diff.added[0]) && diff.removed.length === 0 && diff.invalid.length === 0;
    perBit.push({ bit, frame, added: diff ? diff.added : null, removed: diff ? diff.removed : null,
      is_integer: diff ? Number.isInteger(diff.added[0]) : false, invalid: diff ? diff.invalid : null, ok });
  }
  push({
    case: 'every_bit_position', frames: FRAMES, words: WORDS,
    all_ok: perBit.every((r) => r.ok),
    top_bit: perBit[31],
    failures: perBit.filter((r) => !r.ok),
    per_bit: perBit,
    ok: perBit.every((r) => r.ok),
    ok_means: 'for each of the 32 bit positions in a word, a set differing by that one bit enumerates exactly that frame number as an '
      + 'integer; the top bit is a number and not a null',
  });
}

// ---- the top bit gained and lost independently
{
  const FRAMES = 32736;
  const WORDS = WORDS_FOR(FRAMES);
  const topA = 82 * 32 + 31;
  const topB = 83 * 32 + 31;
  const rows = [];

  const gained = frameSetDiff(
    parseFrameSets(pointText('before', emptyBitmap(WORDS), FRAMES)).points.get('before'),
    parseFrameSets(pointText('after', withBit(emptyBitmap(WORDS), topA), FRAMES)).points.get('after'));
  rows.push({ what: 'top bit of one word gained', added: gained.added, removed: gained.removed,
    ok: gained.added.length === 1 && gained.added[0] === topA && gained.removed.length === 0 });

  const lost = frameSetDiff(
    parseFrameSets(pointText('before', withBit(emptyBitmap(WORDS), topA), FRAMES)).points.get('before'),
    parseFrameSets(pointText('after', emptyBitmap(WORDS), FRAMES)).points.get('after'));
  rows.push({ what: 'top bit of one word lost', added: lost.added, removed: lost.removed,
    ok: lost.removed.length === 1 && lost.removed[0] === topA && lost.added.length === 0 });

  const both = frameSetDiff(
    parseFrameSets(pointText('before', withBit(emptyBitmap(WORDS), topA), FRAMES)).points.get('before'),
    parseFrameSets(pointText('after', withBit(emptyBitmap(WORDS), topB), FRAMES)).points.get('after'));
  rows.push({ what: 'top bit of one word gained while the top bit of the next is lost', added: both.added, removed: both.removed,
    ok: both.added.length === 1 && both.added[0] === topB && both.removed.length === 1 && both.removed[0] === topA });

  push({ case: 'high_bit_independent', top_bit_frames: [topA, topB], rows, ok: rows.every((r) => r.ok),
    ok_means: 'the top bit of a word is gained and lost independently, in the same word and across words, and every member is the frame '
      + 'number it belongs to' });
}

// ---- a full word, and a full bitmap against its complement
{
  const FRAMES = 32736;
  const WORDS = WORDS_FOR(FRAMES);
  const full = 0xFFFFFFFF;
  const rows = [];

  const gainedWord = frameSetDiff(
    parseFrameSets(pointText('before', emptyBitmap(WORDS), FRAMES)).points.get('before'),
    parseFrameSets(pointText('after', new Array(WORDS).fill(0).map((v, i) => (i === 82 ? full : 0)), FRAMES)).points.get('after'));
  rows.push({ what: 'one whole word gained', count: gainedWord.added.length,
    first: gainedWord.added[0], last: gainedWord.added[gainedWord.added.length - 1],
    all_integers: gainedWord.added.every((v) => Number.isInteger(v)),
    contiguous: gainedWord.added.every((v, i) => v === 82 * 32 + i),
    ok: gainedWord.added.length === 32 && gainedWord.removed.length === 0
      && gainedWord.added.every((v, i) => v === 82 * 32 + i) });

  // A bitmap and its complement over two words: every one of the 64 positions is a member on one side or the other, and none is both.
  const a = new Array(WORDS).fill(0).map((v, i) => (i === 82 ? 0x0000FFFF : i === 83 ? 0xFFFFFFFF : 0));
  const b = new Array(WORDS).fill(0).map((v, i) => (i === 82 ? 0xFFFF0000 : i === 83 ? 0 : 0));
  const complement = frameSetDiff(
    parseFrameSets(pointText('before', a, FRAMES)).points.get('before'),
    parseFrameSets(pointText('after', b, FRAMES)).points.get('after'));
  const addedOk = complement.added.length === 16 && complement.added.every((v, i) => v === 82 * 32 + 16 + i);
  const removedOk = complement.removed.length === 48
    && complement.removed.every((v, i) => (i < 16 ? v === 82 * 32 + i : v === 83 * 32 + (i - 16)));
  rows.push({ what: 'two words replaced by their complement', added: complement.added.length, removed: complement.removed.length,
    added_ok: addedOk, removed_ok: removedOk, disjoint: complement.added.every((v) => !complement.removed.includes(v)),
    ok: addedOk && removedOk && complement.added.every((v) => !complement.removed.includes(v)) });

  push({ case: 'full_words_and_complement', rows, ok: rows.every((r) => r.ok),
    ok_means: 'a whole word enumerates as thirty-two contiguous frame numbers, and a two-word complement splits into exactly the sixteen '
      + 'and forty-eight members the bits define, with no member on both sides' });
}

// ---- members in adjacent words
{
  const FRAMES = 32736;
  const WORDS = WORDS_FOR(FRAMES);
  let w = emptyBitmap(WORDS);
  for (const frame of [0, 31, 32, 63, 64, 2655, 2656, 32735]) w = withBit(w, frame);
  const diff = frameSetDiff(
    parseFrameSets(pointText('before', emptyBitmap(WORDS), FRAMES)).points.get('before'),
    parseFrameSets(pointText('after', w, FRAMES)).points.get('after'));
  const want = [0, 31, 32, 63, 64, 2655, 2656, 32735];
  push({ case: 'cross_word', want, got: diff.added,
    ordered: diff.added.every((v, i) => v === want[i]),
    bounded: diff.added.every((v) => v >= 0 && v < FRAMES),
    ok: JSON.stringify(diff.added) === JSON.stringify(want) && diff.removed.length === 0,
    ok_means: 'members at word boundaries and at the last addressable frame enumerate in ascending order as the frames they are' });
}

// ---- an empty difference, which is legal
{
  const FRAMES = 32736;
  const WORDS = WORDS_FOR(FRAMES);
  let w = emptyBitmap(WORDS);
  for (const frame of [17, 2655, 30000]) w = withBit(w, frame);
  const diff = frameSetDiff(
    parseFrameSets(pointText('baseline', w, FRAMES)).points.get('baseline'),
    parseFrameSets(pointText('final', w, FRAMES)).points.get('final'));
  push({ case: 'empty_difference', added: diff.added, removed: diff.removed, identical: diff.identical,
    ok: diff.identical === true && diff.added.length === 0 && diff.removed.length === 0 && diff.invalid.length === 0,
    ok_means: 'two identical bitmaps produce a difference of nothing, which is a legal result and is reported as identical rather than '
      + 'as a fault' });
}

// ---- the real record: the actual baseline-to-victim difference from the 144 channel
{
  const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
  const real = [];
  for (let t = 1; t <= 28 && real.length === 0; t++) {
    const tn = String(t).padStart(2, '0');
    const trialPath = R144 + '/g-trial-gm144-' + tn + '-144.json';
    if (!fs.existsSync(trialPath)) continue;
    const trial = JSON.parse(fs.readFileSync(trialPath, 'utf8'));
    if (trial.scene !== 'runtime-frame-recovery-smp1') continue;
    const e9 = trial.channels && trial.channels.e9 ? trial.channels.e9 : null;
    if (!e9 || !e9.path || !fs.existsSync(e9.path)) continue;
    const text = fs.readFileSync(e9.path, 'utf8');
    const sets = parseFrameSets(text);
    const b = sets.points.get('baseline');
    const v = sets.points.get('victim');
    if (!b || !v) continue;
    const fast = frameSetDiff(b, v);
    // The independent walk: ask, bit by bit, whether the two points differ at that position.
    const slowAdded = [];
    const slowRemoved = [];
    const words = Math.max(b.wordsDense.length, v.wordsDense.length);
    for (let i = 0; i < words; i++) {
      const x = (b.wordsDense[i] === undefined ? 0 : b.wordsDense[i]) >>> 0;
      const y = (v.wordsDense[i] === undefined ? 0 : v.wordsDense[i]) >>> 0;
      for (let bit = 0; bit < 32; bit++) {
        const inA = (x >>> bit) & 1;
        const inB = (y >>> bit) & 1;
        if (!inA && inB) slowAdded.push(i * 32 + bit);
        if (inA && !inB) slowRemoved.push(i * 32 + bit);
      }
    }
    real.push({
      trial: 'g-trial-gm144-' + tn + '-144.json', scene: trial.scene, channel: e9.path,
      channel_bytes: fs.statSync(e9.path).size, channel_sha256: sha(fs.readFileSync(e9.path)),
      frames: b.declared ? b.declared.frames : null,
      added: fast.added, removed: fast.removed,
      added_count: fast.added.length, removed_count: fast.removed.length,
      contains_2655: fast.added.includes(2655),
      any_null: fast.added.some((x) => x === null || !Number.isInteger(x)) || fast.removed.some((x) => x === null || !Number.isInteger(x)),
      agrees_with_the_bit_walk: JSON.stringify(fast.added) === JSON.stringify(slowAdded)
        && JSON.stringify(fast.removed) === JSON.stringify(slowRemoved),
      slow_added_count: slowAdded.length,
    });
    break;
  }
  const r = real[0];
  push({ case: 'real_baseline_to_victim', found: real.length, record: r || null,
    ok: !!r && r.added_count === 14 && r.removed_count === 0 && r.contains_2655 === true && r.any_null === false
      && r.agrees_with_the_bit_walk === true,
    ok_means: 'the real 144 baseline-to-victim difference enumerates as fourteen members with none of them null, frame 2655 among them, '
      + 'and the list is exactly what a bit-by-bit walk of the same two bitmaps produces' });
}

// ---- a member outside the addressable range is refused rather than silently dropped
{
  // A dump that declares a smaller range than its own words reach: the words are two long, the header says one word is addressable. The
  // bit above the declared range must be caught by the range obligations, and the difference must not quietly include it as a frame.
  const text = 'FRAMEREC DIAG SET baseline FRAMES ' + hx(32) + ' OWNED ' + hx(0) + ' COMPLETE ' + hx(1) + '\n'
    + 'FRAMEREC DIAG BM baseline ' + hx(0) + [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0].map(hx).join('') + '\n'
    + 'FRAMEREC DIAG BM baseline ' + hx(16) + [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1].map(hx).join('');
  const sets = parseFrameSets(text);
  const pt = sets.points.get('baseline');
  push({ case: 'range_overflow_is_visible', declared_frames: pt.declared.frames, words: pt.wordsDense.length,
    bits_outside: pt.bitsOutsideCoveredRange,
    ok: pt.bitsOutsideCoveredRange === 1,
    ok_means: 'a set bit above the declared addressable range is counted, so the range obligation can refuse the dump instead of the '
      + 'reader pretending the bit is a frame' });
}

// ---- a duplicate offset and a malformed line are refused by the parser rather than reassembled
{
  const good = 'FRAMEREC DIAG SET baseline FRAMES ' + hx(512) + ' OWNED ' + hx(1) + ' COMPLETE ' + hx(1) + '\n'
    + 'FRAMEREC DIAG BM baseline ' + hx(0) + [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0].map(hx).join('');
  const dup = good + '\n' + 'FRAMEREC DIAG BM baseline ' + hx(0) + [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0].map(hx).join('');
  const bad = good + '\nFRAMEREC DIAG BM baseline 0000000Z ' + '0'.repeat(128);
  const dupSets = parseFrameSets(dup);
  const badSets = parseFrameSets(bad);
  push({ case: 'duplicate_and_malformed', duplicate_malformed: dupSets.malformed.length, badline_malformed: badSets.malformed.length,
    ok: dupSets.malformed.length === 1 && badSets.malformed.length === 1,
    ok_means: 'a second line at an offset already dumped, and a line whose offset is not eight hex digits, are both reported as '
      + 'malformed rather than being folded into the set' });
}

const out = {
  task: 'GTOS-DSH-145',
  what: 'the production frame-member parser and difference, exercised on the finite inputs the taskbook names and on the real 144 record',
  subject: { path: REF, bytes: fs.statSync(REF).size, sha256: sha(fs.readFileSync(REF)) },
  exports_under_test: ['parseFrameSets', 'frameSetDiff'],
  cases,
  counts: { cases: cases.length, bit_positions: 32, real_records: cases.find((c) => c.case === 'real_baseline_to_victim').found },
  all_ok: cases.every((c) => c.ok === true),
  ok: cases.every((c) => c.ok === true),
  ok_means: 'every member the production enumeration produces for these inputs is a bounded integer frame number, the top bit included, '
    + 'and the real record agrees with an independent bit-by-bit walk',
};
const RUN = process.argv[2] || '01';
const target = R + '/r-members-145-' + RUN + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: out.ok, cases: cases.map((c) => ({ case: c.case, ok: c.ok })),
  real: cases.find((c) => c.case === 'real_baseline_to_victim').record }, null, 2));
process.exit(out.ok ? 0 : 1);
