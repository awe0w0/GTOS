// Resource-semantics judge for the three resource scenes of packet 147.
//
// This module answers one question about one finished guest run: did the machine leave its frames, its object slots and its
// identifier table in exactly the state the scene claims? It is deliberately not a marker checker. The scene's terminal marker
// is necessary and is checked, but a run that printed the marker after a failure line, or that printed the right numbers from
// a different scene, is refused here.
//
// Three rules shape it.
//
//   1. It reads the channel the guest itself wrote, and it reads all of it. Counts and rolling digests only locate a
//      disagreement; the exact conclusions are drawn from the member sets the log enumerates (which fill indices exist, which
//      identifiers were retired, which refusal categories each retired identifier hit) and from the arithmetic relations
//      between the numbers the guest printed (a claimed frame count plus the frames left free must equal the frames that were
//      free after the sibling, and so on). A digest that happens to match while the members differ is refused by the members.
//
//   2. Both channels are judged, and they must agree. The guest writes the same log to E9 and to COM1; a run where one
//      channel is truncated, empty or different from the other is refused, because the two are supposed to be one log written
//      twice.
//
//   3. Every obligation is two-sided. For each "this must be present" there is a matching "this must be absent": the scene's
//      own failure lines, the other scene's diagnostic family, and the events the scene is not allowed to reach. Accepting on
//      the presence of a good line alone is how a log with a failure line above it passes.
//
// The judge is pure: text in, verdict out, no filesystem, no process, no clock. The same function judges a real trial, a
// relocated copy of a real trial's log, and every mutated negative, so a negative cannot be refused by a rule the positive was
// never held to.

const HEX = /^[0-9A-Fa-f]{1,16}$/;

// ------------------------------------------------------------------ parsing
// The guest's log is a sequence of lines of the form `FAMILY DIAG KEY` or `FAMILY DIAG KEY VALUE`, where the value is either a
// bare decimal or an upper-case hex number. `BOOTDIAG ...` lines are the scene's terminal markers and are kept apart from the
// keyed diagnostics, because they are the claim rather than the measurement.
export function parseChannel(text) {
  const lines = String(text === undefined || text === null ? '' : text).split('\n').map((l) => l.replace(/\r$/, ''));
  const keys = [];
  const values = new Map();
  const present = new Set();
  const markers = [];
  const others = [];
  let trailingBlank = 0;
  for (const raw of lines) {
    const line = raw.trim();
    if (line === '') { trailingBlank += 1; continue; }
    trailingBlank = 0;
    if (line.startsWith('BOOTDIAG ')) { markers.push(line); continue; }
    const m = /^(OBJECT DIAG|RECOVERY DIAG|FRAMEREC DIAG|WORKER DIAG|AP DIAG|PAGING DIAG|TABLE DIAG) (.+)$/.exec(line);
    if (!m) { others.push(line); continue; }
    const family = m[1];
    const rest = m[2];
    // The key is everything up to a value that is a number; a key may itself contain digits (CR0, SS0, B04), so the split is on
    // the last space only when the tail parses as a number. Every value the guest prints is at least four hex digits wide, which
    // is what separates `ID A 00000001` from `CREATED A`: the single-letter tail of the latter is a name, not a number, and the
    // digest `C16E54A5` must not be mistaken for one either.
    const at = rest.lastIndexOf(' ');
    let key = rest;
    let value = null;
    if (at > 0) {
      const tail = rest.slice(at + 1);
      if (HEX.test(tail) && tail.length >= 4) {
        key = rest.slice(0, at);
        value = tail;
      }
    }
    keys.push({ family, key, value, line });
    present.add(family + ' ' + key);
    if (value !== null) {
      if (!values.has(family + ' ' + key)) values.set(family + ' ' + key, []);
      values.get(family + ' ' + key).push(value);
    }
  }
  return { lines, keys, values, present, markers, others, text: String(text || '') };
}

const dec = (v) => (/^[0-9A-Fa-f]{1,16}$/.test(v) ? parseInt(v, 16) : null);

// ------------------------------------------------------------------ obligations
// One helper to state an obligation in the same shape everywhere: a claim, whether it holds, and the exact numbers or members
// the claim was made about, so a refusal names what disagreed rather than only that something did.
function makeObligations() {
  const out = [];
  const add = (id, claim, holds, detail) => {
    out.push({ id, claim, ok: holds === true, detail: detail === undefined ? null : detail });
    return holds === true;
  };
  return { out, add };
}

function has(p, family, key) { return p.present.has(family + ' ' + key); }
function one(p, family, key) {
  const v = p.values.get(family + ' ' + key);
  return v && v.length === 1 ? dec(v[0]) : null;
}
function all(p, family, key) {
  const v = p.values.get(family + ' ' + key);
  return v ? v.map(dec) : [];
}
// A value the scene prints more than once is only usable if every occurrence agrees. The baseline triple is printed twice in
// the recovery scenes, and a run whose two baseline readings disagreed would be a run whose starting state moved underneath it,
// which is not a state any conclusion can be drawn from.
function uniform(p, family, key) {
  const v = p.values.get(family + ' ' + key);
  if (!v || v.length === 0) return null;
  const first = v[0];
  return v.every((x) => x === first) ? dec(first) : null;
}
function count(p, family, key) {
  const v = p.values.get(family + ' ' + key);
  return v ? v.length : 0;
}

// ------------------------------------------------------------------ the frame set, reconstructed from its bits
// The frame scene does not ask a reader to believe a count. It dumps the allocator's own bitmap six times, and this reader
// reassembles those bits and derives every claim from them. What the guest prints is, from `src/kernel.cpp`:
//
//   FRAMEREC DIAG SET <point> FRAMES <8> OWNED <8> COMPLETE <8>
//   FRAMEREC DIAG BM <point> <offset:8><sixteen words:8 each>          (one line per sixteen words)
//   FRAMEREC DIAG DELTA <label> ADDED <8> REMOVED <8>
//   FRAMEREC DIAG DELTA ADDED FRAME <8> / DELTA REMOVED FRAME <8>       (locators, at most sixteen per side)
//
// Three things make the reassembly checkable rather than trusted, and all three are checked here. Each dump line carries the
// word offset it belongs at, so a missing line is a gap rather than a shift. The declared OWNED is compared with the number of
// bits actually set, so a truncated dump cannot agree with its own summary. And the covered range is compared with FRAMES, so
// a machine whose frames exceed the declared capacity is reported as incomplete instead of being silently truncated.
const FRAME_SET_LINE = /^FRAMEREC DIAG SET (\S+) FRAMES ([0-9A-F]{8}) OWNED ([0-9A-F]{8}) COMPLETE ([0-9A-F]{8})$/;
const FRAME_BM_LINE = /^FRAMEREC DIAG BM (\S+) ([0-9A-F]{8})((?:[0-9A-F]{8}){16})$/;
const FRAME_DELTA_LINE = /^FRAMEREC DIAG DELTA (\S+) ADDED ([0-9A-F]{8}) REMOVED ([0-9A-F]{8})$/;
const FRAME_DELTA_MEMBER = /^FRAMEREC DIAG DELTA (ADDED|REMOVED) FRAME ([0-9A-F]{8})$/;

export function parseFrameSets(text) {
  const points = new Map();
  const deltas = new Map();
  const locators = { added: [], removed: [] };
  const malformed = [];
  const point = (name) => {
    if (!points.has(name)) points.set(name, { name, declared: null, words: new Map(), dumpLines: 0, deltaLines: [] });
    return points.get(name);
  };
  for (const raw of String(text === undefined || text === null ? '' : text).split('\n')) {
    const line = raw.trim();
    if (line.startsWith('FRAMEREC DIAG SET ')) {
      const m = FRAME_SET_LINE.exec(line);
      if (!m) { malformed.push(line); continue; }
      const p = point(m[1]);
      if (p.declared !== null) { malformed.push(line); continue; }
      p.declared = { frames: parseInt(m[2], 16), owned: parseInt(m[3], 16), complete: parseInt(m[4], 16) };
      continue;
    }
    if (line.startsWith('FRAMEREC DIAG BM ')) {
      const m = FRAME_BM_LINE.exec(line);
      if (!m) { malformed.push(line); continue; }
      const p = point(m[1]);
      const offset = parseInt(m[2], 16);
      if (p.words.has(offset)) { malformed.push(line); continue; }
      const words = [];
      for (let i = 0; i < 16; i++) words.push(parseInt(m[3].slice(i * 8, i * 8 + 8), 16));
      p.words.set(offset, words);
      p.dumpLines += 1;
      continue;
    }
    if (line.startsWith('FRAMEREC DIAG DELTA ')) {
      const member = FRAME_DELTA_MEMBER.exec(line);
      if (member) {
        const index = parseInt(member[2], 16);
        if (member[1] === 'ADDED') locators.added.push(index); else locators.removed.push(index);
        continue;
      }
      const m = FRAME_DELTA_LINE.exec(line);
      if (!m) { malformed.push(line); continue; }
      const row = { label: m[1], added: parseInt(m[2], 16), removed: parseInt(m[3], 16) };
      deltas.set(row.label, row);
      continue;
    }
  }
  // Assemble each point into one dense word array, in offset order, and derive its numbers from the bits.
  for (const p of points.values()) {
    const offsets = [...p.words.keys()].sort((a, b) => a - b);
    const gaps = [];
    for (let i = 0; i < offsets.length; i++) {
      if (offsets[i] !== i * 16) { gaps.push({ expected: i * 16, found: offsets[i] }); }
    }
    const words = [];
    for (const o of offsets) for (const w of p.words.get(o)) words.push(w);
    let setBits = 0;
    for (const w of words) { let x = w; while (x) { x &= x - 1; setBits += 1; } }
    const frames = p.declared ? p.declared.frames : null;
    // Every bit at or above FRAMES must be clear: the dump covers whole words, so the tail of the last word is padding and a set
    // bit there would be a frame the allocator cannot address.
    const covered = p.declared && p.declared.complete === 1 ? p.declared.frames : null;
    let outside = 0;
    if (covered !== null) {
      for (let i = covered; i < words.length * 32; i++) if (words[i >> 5] & (1 << (i & 31))) outside += 1;
    }
    p.wordsDense = words;
    p.offsets = offsets;
    p.gaps = gaps;
    p.setBits = setBits;
    p.bitsOutsideCoveredRange = outside;
    p.bitCount = words.length * 32;
  }
  return { points, deltas, locators, malformed };
}

// The index of the lowest set bit of a 32-bit word, as an integer in 0..31, or -1 when no bit is set.
//
// `Math.log2(word & -word)` is the obvious way to write this and it is wrong for bit 31. A word is parsed as a positive number up to
// 0xFFFFFFFF, and `&` converts both operands to int32, so for 0x80000000 the expression is -2147483648 & -2147483648, which is negative;
// Math.log2 of a negative number is NaN. That is not a rounding worry: the real baseline-to-victim difference has fourteen members and one
// of them is frame 2655, which is bit 31 of word 82, and it came out as null. A member set containing null is not a set of frames, and a
// count that happens to be right cannot make it one. clz32 is defined on the unsigned 32-bit value, so the index is exact for every bit
// including the top one.
function lowBitIndex(word) {
  const x = word >>> 0;
  if (x === 0) return -1;
  return 31 - Math.clz32(x);
}
// A member is a frame number the allocator can address. Anything else -- a NaN, a fraction, a negative, a value outside the bitmap -- is
// not a member and is reported rather than pushed into the set.
function isFrameMember(value, bitCount) {
  return Number.isInteger(value) && value >= 0 && value < bitCount;
}

// The bit-level difference between two assembled points. The guest prints its own ADDED/REMOVED counts; these are recomputed from
// the words so the printed pair is a claim that can be wrong rather than the only account of what moved.
//
// The result carries `invalid`, which is empty for every input this reader can produce. It exists so that the enumeration is checkable at
// the point it is made: if a member were ever not a bounded integer, the difference would say so instead of returning a set with a hole in
// it, and the obligation that consumes the set refuses.
export function frameSetDiff(before, after) {
  if (!before || !after) return null;
  const n = Math.max(before.wordsDense.length, after.wordsDense.length);
  const bitCount = Math.max(before.bitCount, after.bitCount, n * 32);
  const added = [];
  const removed = [];
  const invalid = [];
  let addedWords = 0;
  let removedWords = 0;
  for (let w = 0; w < n; w++) {
    const a = (before.wordsDense[w] === undefined ? 0 : before.wordsDense[w]) >>> 0;
    const b = (after.wordsDense[w] === undefined ? 0 : after.wordsDense[w]) >>> 0;
    let gain = (b & ~a) >>> 0;
    let lost = (a & ~b) >>> 0;
    if (gain) addedWords += 1;
    if (lost) removedWords += 1;
    while (gain) {
      const frame = w * 32 + lowBitIndex(gain & -gain);
      if (isFrameMember(frame, bitCount)) { if (!added.includes(frame)) added.push(frame); } else invalid.push({ side: 'added', frame });
      gain = (gain & (gain - 1)) >>> 0;
    }
    while (lost) {
      const frame = w * 32 + lowBitIndex(lost & -lost);
      if (isFrameMember(frame, bitCount)) { if (!removed.includes(frame)) removed.push(frame); } else invalid.push({ side: 'removed', frame });
      lost = (lost & (lost - 1)) >>> 0;
    }
  }
  added.sort((x, y) => x - y);
  removed.sort((x, y) => x - y);
  return { added, removed, addedWords, removedWords, invalid,
    bitCount,
    every_member_is_a_bounded_integer: invalid.length === 0
      && added.every((v) => isFrameMember(v, bitCount)) && removed.every((v) => isFrameMember(v, bitCount)),
    no_duplicate_members: new Set(added).size === added.length && new Set(removed).size === removed.length,
    identical: added.length === 0 && removed.length === 0 };
}

// The scenes and the diagnostic family each one writes. A log that carries another family's diagnostics is refused, which is
// what stops the two 140-era refusals from being read as each other.
export const RESOURCE_SCENES = {
  'runtime-objects-smp1': { family: 'OBJECT DIAG', marker: 'BOOTDIAG RUNTIME OBJECTS RECLAIMED', kind: 'objects' },
  'runtime-recovery-smp1': { family: 'RECOVERY DIAG', marker: 'BOOTDIAG RUNTIME RECOVERED CAPACITY', kind: 'capacity' },
  'runtime-frame-recovery-smp1': { family: 'FRAMEREC DIAG', marker: 'BOOTDIAG RUNTIME RECOVERED FRAMES', kind: 'frame' },
};
export const RESOURCE_REFUSALS = {
  'runtime-objects-smp2': { family: 'OBJECT DIAG', marker: 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU' },
  'runtime-objects-smp3': { family: 'OBJECT DIAG', marker: 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU' },
  'runtime-recovery-smp2': { family: 'RECOVERY DIAG', marker: 'BOOTDIAG RUNTIME RECOVERY REFUSED MULTI CPU' },
  'runtime-recovery-smp3': { family: 'RECOVERY DIAG', marker: 'BOOTDIAG RUNTIME RECOVERY REFUSED MULTI CPU' },
  'runtime-frame-recovery-smp2': { family: 'FRAMEREC DIAG', marker: 'BOOTDIAG RUNTIME FRAME RECOVERY REFUSED MULTI CPU' },
  'runtime-frame-recovery-smp3': { family: 'FRAMEREC DIAG', marker: 'BOOTDIAG RUNTIME FRAME RECOVERY REFUSED MULTI CPU' },
};

const FAILURE_WORDS = [
  'FAILED', 'WRONG', 'SHARED', 'RETAINED', 'INCOMPLETE', 'LIVE', 'DIRTIED', 'ACCEPTED', 'READABLE', 'EXITABLE',
  'REUSED', 'STILL LIVE', 'SERVED', 'DRIFTED', 'NOT ASKED', 'MOVED', 'MADE', 'DAMAGED', 'RESIDUE', 'OCCUPIED',
  'ENABLED', 'DISAGREES', 'UNREADABLE', 'STARVED ACCEPTED', 'SHORT', 'FULL', 'REFUSAL LEFT AN ID',
];

// A line is a failure line when its key ends in a word that only a failing path prints. The list is explicit rather than
// clever: a judge that guessed from the word "FAILED" alone would miss `FRAMEREC DIAG REFUSAL LEFT AN ID`.
function failureLines(p, family) {
  const out = [];
  for (const k of p.keys) {
    if (k.family !== family) continue;
    const head = k.key;
    const isFailure = FAILURE_WORDS.some((w) => head === w || head.endsWith(' ' + w));
    if (!isFailure) continue;
    // The two-sided trap: a key that *states* a safe outcome can contain a failure word as its own value-carrying name, and the
    // safe forms below are the ones the scenes actually print.
    if (/^(OLD ID|OLD IDS|OLD A|OLD B|OLD A REAPED|OLD B REAPED)/.test(head)) continue;
    if (head === 'PRESSURE HELD' || head === 'WALK AGREES' || head === 'SPARE SLOT' || head === 'SIBLING LIVE'
      || head === 'RECOVERED' || head === 'VICTIM TOMBSTONED' || head === 'RECREATED' || head === 'NEW LIVE'
      || head === 'NEW DATA MATCHES' || head === 'NEW ID FRESH' || head === 'SURVIVORS LIVE'
      || head === 'REFUSAL CLEAN' || head === 'REFUSAL IDENTLESS' || head === 'FRAME SET UNMOVED'
      || head === 'NO OBJECT MADE' || head === 'SIBLING INTACT' || head === 'ALLOCATOR REFUSED'
      || head === 'FILLED TO CAPACITY' || head === 'LIVE DISTINCT' || head === 'OVERFLOW REFUSED'
      || head === 'BASELINE RESTORED' || head === 'ACCOUNTED' || head === 'TABLE EMPTY'
      || head === 'RESOURCES RESTORED' || head === 'ADDRESS SPACES DIFFER' || head === 'DATA DIFFERS'
      || head === 'DATA A MATCHES' || head === 'DATA B MATCHES' || head === 'RECOVERED DATA MATCHES'
      || head === 'OLD IDS TOMBSTONED' || head === 'OLD ID UNREADABLE' || head === 'OLD ID UNEXITABLE'
      || head === 'RECLAIMED' || head === 'INTERRUPTS CLEAR' || head === 'NOT DISPATCHED'
      || head === 'PRESSURE REVERSED' || head === 'RETIRED ALL' || head === 'STARVED REFUSED'
      || head === 'RECOVERED ID FRESH' || head === 'STACKS PREPARED' || head === 'ACTIVATED'
      || head === 'RECOVERED LIVE' || head === 'RECOVERED DATA MATCHES'
      // The frame scene states three successes whose names contain a failure word: the slot was reused, the retired object's
      // physical frame came back, and the object that took the slot is live. `SLOT NOT REUSED` stays a failure because these
      // are exact names, not suffixes -- which is the reason the allow-list is written out rather than matched by pattern.
      || head === 'SLOT REUSED' || head === 'RETIRED FRAME REUSED' || head === 'NEW VICTIM LIVE'
      || head === 'CREATED A' || head === 'CREATED B' || head === 'EXIT REQUESTED'
      || head === 'VICTIM EXIT REQUESTED' || head === 'VICTIM REAPED' || head === 'SIBLING CREATED'
      || head === 'SPARE SLOTS' || head === 'SIBLING ID') continue;
    out.push(k.line);
  }
  return out;
}

// ------------------------------------------------------------------ the judge
// `scene` is the declared scene name; `channels` is `{ e9, serial }` as text. The verdict names every obligation it checked, so
// a reader can see what a pass actually covered and a negative can say which obligation it was refused by.
export function judgeResourceSemantics(scene, channels) {
  const spec = RESOURCE_SCENES[scene];
  const refusal = RESOURCE_REFUSALS[scene];
  if (!spec && !refusal) throw new Error('the judge has no obligations for scene: ' + scene);
  const e9 = parseChannel(channels && channels.e9);
  const serial = parseChannel(channels && channels.serial);
  const { out, add } = makeObligations();
  const facts = { scene, family: (spec || refusal).family, kind: spec ? spec.kind : 'refusal' };

  // ---------------------------------------------------------------- the channel pair
  add('channel.e9.non_empty', 'the e9 channel carries at least one line', e9.keys.length + e9.markers.length > 0,
    { lines: e9.keys.length + e9.markers.length });
  add('channel.serial.non_empty', 'the serial channel carries at least one line',
    serial.keys.length + serial.markers.length > 0, { lines: serial.keys.length + serial.markers.length });
  // The two channels are one log written twice. Comparing them as whole texts is the strongest form of that claim and it is the
  // one the guest's own writer supports: the same bytes are emitted to both ports.
  const sameText = e9.text.replace(/\r/g, '') === serial.text.replace(/\r/g, '');
  add('channel.pair.identical', 'the two channels carry the same log', sameText,
    { e9_lines: e9.keys.length, serial_lines: serial.keys.length });

  const p = e9;
  const fam = (spec || refusal).family;

  // ---------------------------------------------------------------- the terminal marker, exactly once
  const marker = (spec || refusal).marker;
  const markerCount = p.markers.filter((m) => m === marker).length;
  add('marker.present', 'the scene terminal marker is present exactly once', markerCount === 1,
    { marker, found: markerCount });
  const strayMarkers = p.markers.filter((m) => m !== marker);
  add('marker.only_the_scene_own', 'no other scene terminal marker appears', strayMarkers.length === 0,
    { stray: strayMarkers.slice(0, 4) });

  // ---------------------------------------------------------------- the diagnostic family
  const foreign = p.keys.filter((k) => k.family !== fam && /OBJECT DIAG|RECOVERY DIAG|FRAMEREC DIAG/.test(k.family));
  add('family.no_other_diagnostic', 'the log carries no other resource family', foreign.length === 0,
    { foreign: [...new Set(foreign.map((k) => k.family))] });

  const failures = failureLines(p, fam);
  add('family.no_failure_line', 'the log carries no line that only a failing path prints', failures.length === 0,
    { failures: failures.slice(0, 6) });

  // Every line must be accounted for. A line the parser does not recognise is a line nothing above judged, so a guest that printed
  // a warning in a shape this reader did not anticipate would be judged only on the lines it did recognise. The allow-list is the
  // boot preamble the kernel prints before any scene runs, written out exactly, and anything else is refused.
  const BOOT_LINE = [
    /^BOOT B[0-9]{2} [A-Z0-9 ]+$/, /^BOOT SERIAL COM1 [0-9]+ [0-9][A-Z][0-9]$/, /^GTOS 0\.3 PROTECTED DESKTOP BOOT$/,
    /^MEMORY FREE FRAMES [0-9A-F]{8}$/,
    /^PHYSICAL SELFTEST PASS$/, /^HEAP SELFTEST PASS$/, /^CPU VENDOR .+ SOURCE .+$/, /^CPU DETECTED [0-9A-F]{8}$/,
    /^CPU ONLINE [0-9A-F]{8}$/, /^SCHEDULER SELFTEST PASS$/, /^AP startup\/self-test complete$/,
  ];
  const unexplained = p.others.filter((l) => !BOOT_LINE.some((re) => re.test(l)));
  add('channel.every_line_accounted_for', 'every line is either a scene diagnostic, a terminal marker, or a known boot line',
    unexplained.length === 0, { unexplained: unexplained.slice(0, 6), count: unexplained.length });

  // ---------------------------------------------------------------- a multi-core refusal
  // A refused scene must stop before the resource work. The refusal is the whole content: a log that went on to fill slots and
  // then claimed a refusal would be a run that ignored its own decision.
  if (refusal) {
    const detected = one(p, fam, 'DETECTED');
    add('refusal.detected', 'the machine reported the processor count it saw', detected !== null, { detected });
    add('refusal.detected_over_one', 'the count it saw is more than one, which is what it refused', detected !== null && detected > 1,
      { detected });
    const refusedLine = p.keys.some((k) => k.key === 'REFUSED MULTI CPU');
    add('refusal.stated', 'the refusal is stated', refusedLine === true, null);
    const workLines = p.keys.filter((k) => /^(FILLED|CREATED A|SIBLING CREATED|CAPACITY|CLAIMED FRAMES)/.test(k.key));
    add('refusal.no_resource_work', 'no slot was filled and no frame was claimed', workLines.length === 0,
      { lines: workLines.map((k) => k.line).slice(0, 4) });
    facts.processors = detected;
    return finish(out, facts);
  }

  // ---------------------------------------------------------------- the prepared baseline
  // Every resource scene begins from the same prepared state and must end there. The baseline is read from the log, not
  // compared with a constant: what is judged is that the end equals the beginning of this very run.
  const baselineAllocated = uniform(p, fam, 'BASELINE ALLOCATED');
  const baselineFree = uniform(p, fam, 'BASELINE FREE');
  add('baseline.allocated_measured', 'the run measured its allocated frame count before the work',
    baselineAllocated !== null, { baselineAllocated });
  add('baseline.free_measured', 'the run measured its free frame count before the work',
    baselineFree !== null, { baselineFree });
  add('baseline.readings_agree', 'every baseline reading the run printed agrees with the others',
    (p.values.get(fam + ' BASELINE ALLOCATED') || []).length >= 1
      && uniform(p, fam, 'BASELINE ALLOCATED') !== null && uniform(p, fam, 'BASELINE FREE') !== null,
    { allocated: p.values.get(fam + ' BASELINE ALLOCATED') || [], free: p.values.get(fam + ' BASELINE FREE') || [] });
  facts.baseline = { allocated: baselineAllocated, free: baselineFree };

  // The frames the machine holds must add up: the prepared baseline, plus what the work allocated, minus what it released, is
  // what it ends with. That identity is checked at the end, against the numbers the log itself printed.

  if (spec.kind === 'objects') {
    // -------------------------------------------------------------- objects: two live objects with their own memory
    const detected = one(p, fam, 'DETECTED');
    add('objects.detected_one', 'the runtime saw exactly one processor', detected === 1, { detected });
    const cr3 = one(p, fam, 'CR3');
    add('objects.cr3_present', 'the sealed directory address was printed', cr3 !== null && cr3 > 0, { cr3 });
    add('objects.created_a', 'object A was created', has(p, fam, 'CREATED A'), null);
    add('objects.created_b', 'object B was created', has(p, fam, 'CREATED B'), null);
    const idA = one(p, fam, 'ID A');
    const idB = one(p, fam, 'ID B');
    add('objects.ids_nonzero', 'both identifiers are non-zero', idA !== null && idB !== null && idA > 0 && idB > 0,
      { idA, idB });
    add('objects.ids_distinct', 'the two identifiers differ', idA !== null && idB !== null && idA !== idB, { idA, idB });
    add('objects.data_a', 'object A reads back its own bytes', has(p, fam, 'DATA A MATCHES'), null);
    add('objects.data_b', 'object B reads back its own bytes', has(p, fam, 'DATA B MATCHES'), null);
    add('objects.data_private', 'the two objects hold different bytes, so neither is reading the bytes of the other', has(p, fam, 'DATA DIFFERS'),
      null);
    const dirA = one(p, fam, 'DIR A');
    const dirB = one(p, fam, 'DIR B');
    add('objects.directories_measured', 'both objects reported a directory', dirA !== null && dirB !== null && dirA > 0 && dirB > 0,
      { dirA, dirB });
    add('objects.directories_distinct', 'the two objects have different directories', dirA !== null && dirB !== null && dirA !== dirB,
      { dirA, dirB });
    add('objects.spaces_differ', 'the address spaces are stated to differ', has(p, fam, 'ADDRESS SPACES DIFFER'), null);

    // The counts the log prints are the member set of this scene: two objects created, none exited, none reaped at that point.
    const createdCount = one(p, fam, 'CREATED COUNT');
    const exitedCount = one(p, fam, 'EXITED COUNT');
    const reapedCount = one(p, fam, 'REAPED COUNT');
    add('objects.counted_created_two', 'two objects exist at the count point', createdCount === 2, { createdCount });
    add('objects.counted_exited_zero', 'no object had exited at the count point', exitedCount === 0, { exitedCount });
    add('objects.counted_reaped_zero', 'no object had been reaped at the count point', reapedCount === 0, { reapedCount });
    add('objects.interrupts_clear', 'the object path ran with interrupts clear', has(p, fam, 'INTERRUPTS CLEAR'), null);
    add('objects.not_dispatched', 'no object was dispatched', has(p, fam, 'NOT DISPATCHED'), null);

    // Release and the tombstone: both old identifiers must be dead in both directions. The member set is the two identifiers
    // that were just created, and the log names each of them, so a run that retired only one is refused here.
    add('objects.exit_requested', 'the exit was requested', has(p, fam, 'EXIT REQUESTED'), null);
    const reaped = one(p, fam, 'REAPED');
    add('objects.reaped_two', 'exactly the two live objects were reaped', reaped === 2, { reaped });
    add('objects.reclaimed', 'the reclamation is stated', has(p, fam, 'RECLAIMED'), null);
    const oldA = one(p, fam, 'OLD A REAPED');
    const oldB = one(p, fam, 'OLD B REAPED');
    const liveA = one(p, fam, 'OLD A LIVE');
    const liveB = one(p, fam, 'OLD B LIVE');
    add('objects.old_ids_members', 'both retired identifiers are named in the member set',
      oldA !== null && oldB !== null, { oldA, oldB });
    add('objects.old_ids_reaped', 'each retired identifier is marked reaped', oldA === 1 && oldB === 1, { oldA, oldB });
    // The scene prints a liveness reading for the first retired identifier only, so that is the reading the obligation is
    // made over: the second identifier is covered by its reaped reading and by the tombstone line.
    add('objects.old_ids_dead', 'the retired identifier the run re-read is marked not live', liveA === 0, { liveA, liveB });
    add('objects.old_ids_tombstoned', 'the tombstone is stated', has(p, fam, 'OLD IDS TOMBSTONED'), null);
    add('objects.old_id_unreadable', 'a retired identifier cannot read memory', has(p, fam, 'OLD ID UNREADABLE'), null);
    add('objects.old_id_unexitable', 'a retired identifier cannot exit an object', has(p, fam, 'OLD ID UNEXITABLE'), null);

    // The end equals the beginning. Both numbers, not one of them: a run that restored the count but not the frames would pass a
    // single-number check.
    const afterAllocated = one(p, fam, 'AFTER ALLOCATED');
    const afterFree = one(p, fam, 'AFTER FREE');
    add('objects.after_measured', 'the end state was measured', afterAllocated !== null && afterFree !== null,
      { afterAllocated, afterFree });
    add('objects.after_equals_baseline', 'the end state equals the prepared baseline on both numbers',
      afterAllocated !== null && afterFree !== null && afterAllocated === baselineAllocated && afterFree === baselineFree,
      { baseline: { allocated: baselineAllocated, free: baselineFree }, after: { allocated: afterAllocated, free: afterFree } });
    add('objects.restored_stated', 'the restoration is stated', has(p, fam, 'RESOURCES RESTORED'), null);
    const finalCreated = one(p, fam, 'FINAL CREATED');
    const finalExited = one(p, fam, 'FINAL EXITED');
    const finalReaped = one(p, fam, 'FINAL REAPED');
    add('objects.final_totals', 'the totals the run printed agree: two created, two exited, two reaped',
      finalCreated === 2 && finalExited === 2 && finalReaped === 2, { finalCreated, finalExited, finalReaped });
    facts.members = { identifiers: [idA, idB], directories: [dirA, dirB], retired: [oldA, oldB] };
    facts.end = { allocated: afterAllocated, free: afterFree };
    return finish(out, facts);
  }

  if (spec.kind === 'capacity') {
    // -------------------------------------------------------------- capacity: the slot table fills, the refusal is FreeSlot's
    const detected = one(p, fam, 'DETECTED');
    add('capacity.detected_one', 'the runtime saw exactly one processor', detected === 1, { detected });
    const capacity = one(p, fam, 'CAPACITY');
    add('capacity.declared', 'the runtime stated the capacity of its own table', capacity !== null && capacity > 0, { capacity });
    // The member set of a fill is the sequence of indices it printed. It must be exactly 1..capacity: no gap, no repeat, and no
    // index beyond the declared capacity. A count alone would accept 1,1,1,1.
    const filled = all(p, fam, 'FILLED');
    const expected = capacity === null ? [] : Array.from({ length: capacity }, (_, i) => i + 1);
    const filledExact = capacity !== null && filled.length === capacity
      && expected.every((v, i) => filled[i] === v);
    add('capacity.fill_members', 'the fill printed exactly the indices 1..capacity in order', filledExact,
      { filled, expected });
    add('capacity.filled_stated', 'the fill is stated to have reached capacity', has(p, fam, 'FILLED TO CAPACITY'), null);
    const fullCreated = one(p, fam, 'FULL CREATED');
    const fullReaped = one(p, fam, 'FULL REAPED');
    add('capacity.full_created', 'the number created equals the declared capacity',
      capacity !== null && fullCreated === capacity, { capacity, fullCreated });
    add('capacity.full_reaped_none', 'nothing had been reaped while the table was full', fullReaped === 0, { fullReaped });
    add('capacity.live_distinct', 'every live object is distinct', has(p, fam, 'LIVE DISTINCT'), null);

    // The refusal itself: refused, identless, and clean. The refused call must not have left an identifier behind, and it must
    // not have made an object, which is what the created count still being the capacity says.
    add('capacity.overflow_refused', 'the call beyond capacity was refused', has(p, fam, 'OVERFLOW REFUSED'), null);
    const overflowId = one(p, fam, 'OVERFLOW ID');
    add('capacity.overflow_identless', 'the refused call left the identifier of the caller clear', overflowId === 0, { overflowId });
    const refusedCreated = one(p, fam, 'REFUSED CREATED');
    add('capacity.refusal_made_no_object', 'the refused call created no object',
      refusedCreated !== null && capacity !== null && refusedCreated === capacity, { refusedCreated, capacity });
    add('capacity.refusal_clean', 'the refusal changed nothing', has(p, fam, 'REFUSAL CLEAN'), null);

    // The frame accounting across the fill: capacity objects are held, and the difference between the refused state and the
    // baseline is the cost of exactly those objects, so the per-object cost is derived from this run rather than assumed.
    const refusedAllocated = one(p, fam, 'REFUSED ALLOCATED');
    const refusedFree = one(p, fam, 'REFUSED FREE');
    add('capacity.refused_measured', 'the state after the refusal was measured',
      refusedAllocated !== null && refusedFree !== null, { refusedAllocated, refusedFree });
    const heldByCapacity = refusedAllocated !== null && baselineAllocated !== null ? refusedAllocated - baselineAllocated : null;
    const costEach = heldByCapacity !== null && capacity ? heldByCapacity / capacity : null;
    add('capacity.held_frames_positive', 'holding the table costs frames', heldByCapacity !== null && heldByCapacity > 0,
      { heldByCapacity });
    add('capacity.cost_per_object_integral', 'the frames held are a whole multiple of the capacity',
      costEach !== null && Number.isInteger(costEach) && costEach > 0, { costEach });
    add('capacity.free_mirrors_allocated', 'the free count fell by exactly the frames the allocated count rose by',
      refusedFree !== null && baselineFree !== null && refusedAllocated !== null && baselineAllocated !== null
        && (baselineFree - refusedFree) === (refusedAllocated - baselineAllocated),
      { baselineFree, refusedFree, baselineAllocated, refusedAllocated });

    // Release, tombstone and the actual reuse of the freed slot. The new object must be live, must hold its own bytes, must
    // carry an identifier that was not one of the retired ones, and the survivors must still be live and countable.
    add('capacity.exit_requested', 'the exit of the victim was requested', has(p, fam, 'EXIT REQUESTED'), null);
    const reapedOne = one(p, fam, 'REAPED');
    add('capacity.one_reaped', 'exactly one object was reaped', reapedOne === 1, { reapedOne });
    add('capacity.victim_tombstoned', 'the identifier of the victim is tombstoned', has(p, fam, 'VICTIM TOMBSTONED'), null);
    add('capacity.recreated', 'a new object was created into the freed slot', has(p, fam, 'RECREATED'), null);
    const newId = one(p, fam, 'NEW ID');
    add('capacity.new_id_fresh', 'the reused slot issued a fresh identifier', has(p, fam, 'NEW ID FRESH'), null);
    add('capacity.new_id_member', 'the new identifier is not one the fill printed',
      newId !== null && newId > 0 && !filled.includes(newId), { newId, filled });
    add('capacity.new_data', 'the new object holds its own bytes', has(p, fam, 'NEW DATA MATCHES'), null);
    add('capacity.new_live', 'the new object is live', has(p, fam, 'NEW LIVE'), null);
    const survivors = one(p, fam, 'SURVIVORS');
    add('capacity.survivors_members', 'the survivors are the fill minus the victim, and the count says so',
      survivors !== null && capacity !== null && survivors === capacity - 1, { survivors, capacity });
    add('capacity.survivors_live', 'every survivor is still live', has(p, fam, 'SURVIVORS LIVE'), null);

    // The totals: the fill, minus the victim, plus the new object, is the created total; one exit, one reap.
    const finalCreated = one(p, fam, 'FINAL CREATED');
    const finalExited = one(p, fam, 'FINAL EXITED');
    const finalReaped = one(p, fam, 'FINAL REAPED');
    add('capacity.final_created', 'the created total is the fill plus the one recreated object',
      finalCreated !== null && capacity !== null && finalCreated === capacity + 1, { finalCreated, capacity });
    add('capacity.final_exited', 'exactly one object exited', finalExited === 1, { finalExited });
    add('capacity.final_reaped', 'exactly one object was reaped in total', finalReaped === 1, { finalReaped });
    add('capacity.accounted', 'the totals are accounted for', has(p, fam, 'ACCOUNTED'), null);
    add('capacity.interrupts_clear', 'the path ran with interrupts clear', has(p, fam, 'INTERRUPTS CLEAR'), null);

    // Retirement of the whole table, and the return to the prepared baseline on all three numbers including the digest.
    const retired = one(p, fam, 'RETIRED');
    add('capacity.retired_members',
      'the retired set is the created set minus the victim, which is the survivors plus the one recreated object',
      retired !== null && finalCreated !== null && survivors !== null && retired === finalCreated - 1
        && retired === survivors + 1, { retired, finalCreated, survivors });
    add('capacity.retired_stated', 'the retirement is stated to be complete', has(p, fam, 'RETIRED ALL'), null);
    add('capacity.baseline_restored_stated', 'the baseline restoration is stated', has(p, fam, 'BASELINE RESTORED'), null);
    add('capacity.table_empty', 'the slot table is empty at the end', has(p, fam, 'TABLE EMPTY'), null);
    const baseDigest = uniform(p, fam, 'BASELINE DIGEST');
    const restoredAllocated = one(p, fam, 'RESTORED ALLOCATED');
    const restoredFree = one(p, fam, 'RESTORED FREE');
    const restoredDigest = one(p, fam, 'RESTORED DIGEST');
    add('capacity.digest_measured', 'the run measured the frame digest, and every baseline reading of it agrees',
      baseDigest !== null && restoredDigest !== null,
      { baseDigest, restoredDigest, readings: p.values.get(fam + ' BASELINE DIGEST') || [] });
    add('capacity.restored_equals_baseline',
      'the end state equals the prepared baseline on the allocated count, the free count and the digest',
      restoredAllocated !== null && restoredFree !== null && restoredDigest !== null && baselineAllocated !== null
        && baselineFree !== null && baseDigest !== null && restoredAllocated === baselineAllocated
        && restoredFree === baselineFree && restoredDigest === baseDigest,
      { baseline: { allocated: baselineAllocated, free: baselineFree, digest: baseDigest },
        restored: { allocated: restoredAllocated, free: restoredFree, digest: restoredDigest } });
    facts.members = { capacity, filled, newId, survivors, retired };
    facts.end = { allocated: restoredAllocated, free: restoredFree, digest: restoredDigest };
    return finish(out, facts);
  }

  // -------------------------------------------------------------- frames: a live sibling, real pressure, exact reversal
  const detected = one(p, fam, 'DETECTED');
  add('frame.detected_one', 'the runtime saw exactly one processor', detected === 1, { detected });
  const capacity = one(p, fam, 'SPARE SLOTS');
  add('frame.spare_slots_measured', 'the spare slot count was measured', capacity !== null, { spareSlots: capacity });
  // The scene requires a spare slot and a live sibling: the failure has to come from the frame allocator, not from a full table.
  add('frame.spare_slot_exists', 'at least one slot was spare when the pressure was applied',
    capacity !== null && capacity > 0, { spareSlots: capacity });
  add('frame.sibling_created', 'the sibling was created', has(p, fam, 'SIBLING CREATED'), null);
  const siblingId = one(p, fam, 'SIBLING ID');
  add('frame.sibling_identified', 'the sibling has an identifier', siblingId !== null && siblingId > 0, { siblingId });
  add('frame.sibling_live', 'the sibling is live', has(p, fam, 'SIBLING LIVE'), null);
  add('frame.sibling_intact', 'the sibling survived the pressure intact', has(p, fam, 'SIBLING INTACT'), null);
  const cost = one(p, fam, 'OBJECT COST');
  const victimCost = one(p, fam, 'VICTIM COST');
  add('frame.cost_measured', 'the cost of one object in frames was measured', cost !== null && cost > 0, { cost });

  // The frame arithmetic across the sibling: the allocated count rose by exactly the object cost, and the free count fell by the
  // same amount. This is the "own natural pressure" claim: the frames are the production allocator's, not a stand-in.
  const afterSiblingAllocated = one(p, fam, 'AFTER SIBLING ALLOCATED');
  const afterSiblingDigest = one(p, fam, 'AFTER SIBLING DIGEST');
  add('frame.after_sibling_measured', 'the state after the sibling was measured',
    afterSiblingAllocated !== null && afterSiblingDigest !== null, { afterSiblingAllocated, afterSiblingDigest });
  add('frame.sibling_cost_exact', 'the allocated count rose by exactly the object cost',
    afterSiblingAllocated !== null && baselineAllocated !== null && cost !== null
      && (afterSiblingAllocated - baselineAllocated) === cost,
    { baselineAllocated, afterSiblingAllocated, cost });

  // The pressure: every remaining free frame but a few is held, so the next Create cannot succeed. The held set plus the frames
  // left free must equal the free set the sibling left behind, which is what makes the claim about the whole set rather than a
  // count of what was taken.
  const claimed = one(p, fam, 'CLAIMED FRAMES');
  const blocks = one(p, fam, 'CLAIM BLOCKS');
  const pressureFree = one(p, fam, 'PRESSURE FREE');
  add('frame.pressure_held', 'the pressure is stated to be held', has(p, fam, 'PRESSURE HELD'), null);
  add('frame.claimed_measured', 'the number of frames claimed was measured', claimed !== null && claimed > 0, { claimed });
  add('frame.blocks_measured', 'the number of claim blocks was measured', blocks !== null && blocks > 0, { blocks });
  add('frame.pressure_free_small', 'few frames were left free under the pressure',
    pressureFree !== null && pressureFree > 0 && pressureFree < (cost === null ? 8 : cost * 2), { pressureFree, cost });
  // The state the victim left is the anchor for both the pressure and its withdrawal, because the victim is live when the pressure
  // is applied and stays live through it. The earlier version of this judge compared both against the state after the sibling,
  // which is one object out of date: the pressure then appeared to be one object short and the withdrawal one object over. The
  // scene prints all three numbers, so the comparison is made against what the scene printed.
  const afterVictimAllocated = one(p, fam, 'AFTER VICTIM ALLOCATED');
  const afterVictimFree = one(p, fam, 'AFTER VICTIM FREE');
  const afterVictimDigest = one(p, fam, 'AFTER VICTIM DIGEST');
  add('frame.after_victim_measured', 'the state after the second object was measured',
    afterVictimAllocated !== null && afterVictimFree !== null && afterVictimDigest !== null,
    { afterVictimAllocated, afterVictimFree, afterVictimDigest });
  add('frame.claim_plus_free_covers_the_set',
    'the frames claimed plus the frames left free equals the free set the second object left behind',
    claimed !== null && pressureFree !== null && afterVictimFree !== null
      && (claimed + pressureFree) === afterVictimFree,
    { claimed, pressureFree, expected: afterVictimFree });

  // The failure and its exact footprint: the frame set did not move, no object was made, the created count did not rise, the
  // failure count rose by exactly one, and the caller's identifier was left clear.
  const beforeAllocated = one(p, fam, 'BEFORE ALLOCATED');
  const beforeFree = one(p, fam, 'BEFORE FREE');
  const beforeDigest = one(p, fam, 'BEFORE DIGEST');
  const beforeFailures = one(p, fam, 'BEFORE FAILURES');
  const beforeCreated = one(p, fam, 'BEFORE CREATED');
  const afterAllocated = one(p, fam, 'AFTER ALLOCATED');
  const afterFree = one(p, fam, 'AFTER FREE');
  const afterDigest = one(p, fam, 'AFTER DIGEST');
  const afterFailures = one(p, fam, 'AFTER FAILURES');
  const afterCreated = one(p, fam, 'AFTER CREATED');
  add('frame.starved_refused', 'the starved create was refused', has(p, fam, 'STARVED REFUSED'), null);
  const starvedId = one(p, fam, 'STARVED ID');
  add('frame.starved_identless', 'the refused create left the identifier of the caller clear', starvedId === 0, { starvedId });
  add('frame.refusal_identless', 'the refusal is stated to have carried no identifier',
    has(p, fam, 'REFUSAL IDENTLESS'), null);
  const added = one(p, fam, 'REFUSALS ADDED');
  add('frame.one_refusal_added', 'exactly one refusal was added', added === 1, { added });
  add('frame.allocator_refused', 'the refusal came from the allocator', has(p, fam, 'ALLOCATOR REFUSED'), null);
  add('frame.no_object_made', 'no object was made by the refused call', has(p, fam, 'NO OBJECT MADE'), null);
  add('frame.frame_set_unmoved', 'the frame set did not move across the refusal', has(p, fam, 'FRAME SET UNMOVED'), null);
  add('frame.before_equals_after',
    'the state before and after the refusal is identical on the allocated count, the free count and the digest',
    beforeAllocated !== null && afterAllocated !== null && beforeFree !== null && afterFree !== null
      && beforeDigest !== null && afterDigest !== null && beforeAllocated === afterAllocated && beforeFree === afterFree
      && beforeDigest === afterDigest,
    { before: { allocated: beforeAllocated, free: beforeFree, digest: beforeDigest },
      after: { allocated: afterAllocated, free: afterFree, digest: afterDigest } });
  add('frame.failures_rose_by_one',
    'the failure count rose by exactly one and the created count did not move',
    beforeFailures !== null && afterFailures !== null && afterFailures - beforeFailures === 1
      && beforeCreated !== null && afterCreated !== null && beforeCreated === afterCreated,
    { beforeFailures, afterFailures, beforeCreated, afterCreated });

  // The withdrawal is exact: every claimed frame returned, and the state after the withdrawal is the state the sibling left,
  // on the free count and the digest. A run that returned the wrong number of frames would fail the free count.
  const returned = one(p, fam, 'RETURNED FRAMES');
  const withdrawnFree = one(p, fam, 'WITHDRAWN FREE');
  const withdrawnDigest = one(p, fam, 'WITHDRAWN DIGEST');
  add('frame.pressure_reversed', 'the pressure is stated to be reversed', has(p, fam, 'PRESSURE REVERSED'), null);
  add('frame.all_frames_returned', 'every claimed frame was returned', returned !== null && claimed !== null && returned === claimed,
    { returned, claimed });
  add('frame.withdrawal_exact',
    'the free count and the digest after the withdrawal equal the state the second object left',
    withdrawnFree !== null && withdrawnDigest !== null && afterVictimFree !== null && afterVictimDigest !== null
      && withdrawnFree === afterVictimFree && withdrawnDigest === afterVictimDigest,
    { withdrawnFree, expectedFree: afterVictimFree, withdrawnDigest, afterVictimDigest });

  // Recovery in the same instance, then the victim's release and the reuse of its slot by a live new object.
  add('frame.recovered', 'the same instance recovered', has(p, fam, 'RECOVERED'), null);
  const recoveredId = one(p, fam, 'RECOVERED ID');
  add('frame.recovered_id', 'the recovered object has an identifier', recoveredId !== null && recoveredId > 0, { recoveredId });
  add('frame.recovered_fresh', 'the identifier of the recovered object is fresh', has(p, fam, 'RECOVERED ID FRESH'), null);
  add('frame.recovered_data', 'the recovered object holds its own bytes', has(p, fam, 'RECOVERED DATA MATCHES'), null);
  add('frame.recovered_live', 'the recovered object is live', has(p, fam, 'RECOVERED LIVE'), null);
  add('frame.victim_exit', 'the exit of the victim was requested', has(p, fam, 'VICTIM EXIT REQUESTED'), null);
  const victimReaped = one(p, fam, 'VICTIM REAPED');
  add('frame.victim_reaped_one', 'exactly one victim was reaped', victimReaped === 1, { victimReaped });
  add('frame.victim_tombstoned', 'the identifier of the victim is tombstoned', has(p, fam, 'VICTIM TOMBSTONED'), null);
  // Both sides of the old identifier: it may not read, exit or reach its old memory, and an identifier that was never issued is
  // refused by the same three paths. That is the reverse control: a run that refused everything would fail the sibling's reads
  // above, and a run that refused nothing would fail these.
  add('frame.old_id_read_refused', 'the retired identifier cannot read', has(p, fam, 'OLD ID READ REFUSED'), null);
  add('frame.old_id_exit_refused', 'the retired identifier cannot exit', has(p, fam, 'OLD ID EXIT REFUSED'), null);
  add('frame.old_id_memory_refused', 'the retired identifier cannot reach its old memory',
    has(p, fam, 'OLD ID MEMORY REFUSED'), null);
  add('frame.unknown_id_refused', 'an identifier that was never issued is refused', has(p, fam, 'UNKNOWN ID REFUSED'), null);

  // The reuse, judged from the addresses rather than from the statements. Three directories are printed: the retired victim's, the
  // sibling's, the recovered object's, and the one the new object took. The obligation is that no two objects that are alive at the
  // same moment share a directory. The retired object is not alive, so its directory is allowed to come back -- the reap freed
  // those frames and the allocator may hand them straight back, and on one CPU it does. Requiring a fresh address would be requiring
  // something the runtime does not promise; what it does promise is that a live object's page table is its own.
  const victimDirectory = one(p, fam, 'VICTIM DIRECTORY');
  const siblingDirectory = one(p, fam, 'SIBLING DIRECTORY');
  const recoveredDirectory = one(p, fam, 'RECOVERED DIRECTORY');
  const newVictimDirectory = one(p, fam, 'NEW VICTIM DIRECTORY');
  add('frame.directories_measured', 'every live object in the reuse block printed its directory',
    victimDirectory !== null && siblingDirectory !== null && recoveredDirectory !== null && newVictimDirectory !== null,
    { victimDirectory, siblingDirectory, recoveredDirectory, newVictimDirectory });
  add('frame.live_directories_distinct',
    'the object that took the retired slot lives in a directory neither live neighbour is using',
    newVictimDirectory !== null && siblingDirectory !== null && recoveredDirectory !== null
      && newVictimDirectory !== 0 && newVictimDirectory !== siblingDirectory
      && newVictimDirectory !== recoveredDirectory,
    { newVictimDirectory, siblingDirectory, recoveredDirectory });
  add('frame.retired_frame_recorded',
    'whether the retired object physical frame came back is recorded rather than assumed either way',
    victimDirectory !== null && newVictimDirectory !== null,
    { victimDirectory, newVictimDirectory, reused: victimDirectory === newVictimDirectory });
  add('frame.slot_reused', 'the new object took the slot the retired object vacated', has(p, fam, 'SLOT REUSED'), null);
  add('frame.new_generation', 'the new object has an identifier of its own', has(p, fam, 'NEW GENERATION'), null);
  add('frame.reused_slot_old_id_refused',
    'with a live object in the retired slot, the retired identifier is still refused on all three paths',
    has(p, fam, 'REUSED SLOT OLD READ REFUSED') && has(p, fam, 'REUSED SLOT OLD EXIT REFUSED')
      && has(p, fam, 'REUSED SLOT OLD MEMORY REFUSED'), null);
  add('frame.reused_slot_read_wrote_nothing',
    'the refused read under the retired identifier wrote nothing into the caller buffer',
    has(p, fam, 'REUSED SLOT READ UNTOUCHED'), null);
  add('frame.new_victim_data', 'the object in the reused slot holds its own bytes', has(p, fam, 'NEW VICTIM DATA MATCHES'), null);
  add('frame.neighbours_held', 'both live neighbours are unchanged by the reuse',
    has(p, fam, 'SIBLING HELD') && has(p, fam, 'RECOVERED HELD'), null);

  // -------------------------------------------------------------- the slot and identifier numbers themselves
  // Everything above reads a statement: `SLOT REUSED` says the slot came back, `NEW GENERATION` says the identifier is new. The scene also
  // prints the numbers those statements are about, and until now this judge did not read them. A statement is a caption; the number is the
  // claim. These obligations consume the numbers the run already prints, so a run that said "reused" while reusing a different slot, or that
  // called an identifier new while reissuing the old one, would be refused here instead of being believed.
  const siblingSlot = one(p, fam, 'SIBLING SLOT');
  const victimSlot = one(p, fam, 'VICTIM SLOT');
  const victimSlotAfter = one(p, fam, 'VICTIM SLOT AFTER');
  const newVictimSlot = one(p, fam, 'NEW VICTIM SLOT');
  add('frame.slot_numbers_measured', 'every slot number the reuse block is about was printed',
    siblingSlot !== null && victimSlot !== null && victimSlotAfter !== null && newVictimSlot !== null,
    { siblingSlot, victimSlot, victimSlotAfter, newVictimSlot });
  // Two objects alive at the same moment cannot occupy one slot. The sibling and the victim are both live when the victim is created, so
  // their slot numbers have to differ; this is the numeric form of "the victim was given a slot of its own".
  add('frame.live_slots_distinct', 'the two live objects hold different slots',
    siblingSlot !== null && victimSlot !== null && siblingSlot !== victimSlot, { siblingSlot, victimSlot });
  // The vacated slot is the same slot, read before and after the reap. `VICTIM SLOT VACATED` states it; the two numbers are what make the
  // statement checkable, and they are the numbers the reuse below depends on.
  add('frame.victim_slot_vacated_is_the_same_slot',
    'the slot the victim occupied before the reap is the slot it is reported to have vacated after it',
    victimSlot !== null && victimSlotAfter !== null && victimSlot === victimSlotAfter
      && has(p, fam, 'VICTIM SLOT VACATED'),
    { victimSlot, victimSlotAfter, vacated_stated: has(p, fam, 'VICTIM SLOT VACATED') });
  // The reuse claim, in numbers: the slot the new object took is the slot the victim vacated, and it is not the sibling's. The statement
  // `SLOT REUSED` alone would be satisfied by any new object anywhere.
  add('frame.new_victim_slot_is_the_vacated_one',
    'the new object took exactly the slot the victim vacated, and not the slot the live sibling holds',
    newVictimSlot !== null && victimSlotAfter !== null && siblingSlot !== null
      && newVictimSlot === victimSlotAfter && newVictimSlot !== siblingSlot,
    { newVictimSlot, victimSlotAfter, siblingSlot });
  const newVictimId = one(p, fam, 'NEW VICTIM ID');
  const victimId = one(p, fam, 'VICTIM ID');
  add('frame.identifier_numbers_measured', 'every identifier number the reuse block is about was printed',
    siblingId !== null && victimId !== null && recoveredId !== null && newVictimId !== null,
    { siblingId, victimId, recoveredId, newVictimId });
  // Four objects, four numbers, no two the same. This is the identifier form of the directory obligation above: a reused slot must not
  // resurrect an old identifier, and the object in the slot has to be a different object, which it can only be by number.
  const idNumbers = [siblingId, victimId, recoveredId, newVictimId];
  add('frame.identifier_numbers_distinct',
    'the four objects the scene creates carry four distinct identifiers',
    idNumbers.every((v) => v !== null && v > 0) && new Set(idNumbers).size === 4, { ids: idNumbers });
  add('frame.new_victim_id_is_a_new_generation',
    'the object in the reused slot has an identifier of its own rather than the retired one',
    newVictimId !== null && victimId !== null && newVictimId !== victimId && newVictimId > victimId,
    { victimId, newVictimId, new_generation_stated: has(p, fam, 'NEW GENERATION') });
  // The cost the scene prints for the victim is the cost it printed for one object, and it is also what the allocated count rose by. Read
  // as numbers, the three agree; read as statements they are three unrelated lines.
  add('frame.victim_cost_matches_the_object_cost',
    'the cost printed for the victim equals the object cost, and the allocated count rose by that cost when the victim was made',
    cost !== null && victimCost !== null && victimCost === cost && afterVictimAllocated !== null && afterSiblingAllocated !== null
      && (afterVictimAllocated - afterSiblingAllocated) === victimCost,
    { cost, victimCost, afterSiblingAllocated, afterVictimAllocated });

  // The end equals the prepared baseline on all three numbers, and the table is empty and accounted for.
  const finalReaped = one(p, fam, 'FINAL REAPED');
  const finalAllocated = one(p, fam, 'FINAL ALLOCATED');
  const finalFree = one(p, fam, 'FINAL FREE');
  const finalDigest = one(p, fam, 'FINAL DIGEST');
  const finalCreated = one(p, fam, 'FINAL CREATED');
  const finalExited = one(p, fam, 'FINAL EXITED');
  const baseDigest = uniform(p, fam, 'BASELINE DIGEST');
  add('frame.final_measured', 'the final state was measured on all three numbers',
    finalAllocated !== null && finalFree !== null && finalDigest !== null, { finalAllocated, finalFree, finalDigest });
  add('frame.final_equals_baseline',
    'the final state equals the prepared baseline on the allocated count, the free count and the digest',
    finalAllocated !== null && finalFree !== null && finalDigest !== null && baselineAllocated !== null && baselineFree !== null
      && baseDigest !== null && finalAllocated === baselineAllocated && finalFree === baselineFree
      && finalDigest === baseDigest,
    { baseline: { allocated: baselineAllocated, free: baselineFree, digest: baseDigest },
      final: { allocated: finalAllocated, free: finalFree, digest: finalDigest } });
  add('frame.baseline_restored', 'the baseline restoration is stated', has(p, fam, 'BASELINE RESTORED'), null);
  add('frame.accounted', 'the totals are accounted for', has(p, fam, 'ACCOUNTED'), null);
  add('frame.table_empty', 'the slot table is empty at the end', has(p, fam, 'TABLE EMPTY'), null);
  add('frame.interrupts_clear', 'the path ran with interrupts clear', has(p, fam, 'INTERRUPTS CLEAR'), null);

  // -------------------------------------------------------------- the frame set, read from its bits
  // Everything above is arithmetic on numbers the guest printed. This block is the stronger claim the scene makes possible: the
  // allocator's own bitmap is dumped six times and the same conclusions are re-derived from the bits. A count can agree while the
  // set differs -- two frames swapped for two others leaves OWNED identical -- so the bits are what settle it.
  const sets = parseFrameSets(p.text);
  const POINTS = ['baseline', 'victim', 'before', 'after', 'withdrawn', 'final'];
  add('frameset.six_points_dumped', 'the run dumped its frame set at all six points',
    POINTS.every((n) => sets.points.has(n) && sets.points.get(n).declared !== null),
    { present: [...sets.points.keys()] });
  add('frameset.no_malformed_line', 'every frame-set line has the exact shape the kernel prints',
    sets.malformed.length === 0, { malformed: sets.malformed.slice(0, 4) });
  // Each dump is checked against its own header, line by line, so a dump that stopped early or repeated a line is refused here
  // rather than being reassembled into a plausible-looking set.
  const dumpProblems = [];
  for (const name of POINTS) {
    const pt = sets.points.get(name);
    if (!pt || pt.declared === null) { dumpProblems.push({ point: name, why: 'not dumped' }); continue; }
    if (pt.gaps.length > 0) dumpProblems.push({ point: name, why: 'word lines are not contiguous', gaps: pt.gaps.slice(0, 4) });
    const covered = pt.declared.complete === 1 ? pt.declared.frames : null;
    if (covered === null) { dumpProblems.push({ point: name, why: 'the dump declares itself incomplete' }); continue; }
    const words = Math.ceil(covered / 32);
    if (pt.dumpLines !== Math.ceil(words / 16)) {
      dumpProblems.push({ point: name, why: 'the number of dump lines does not cover the range', lines: pt.dumpLines, words });
    }
    if (pt.setBits !== pt.declared.owned) {
      dumpProblems.push({ point: name, why: 'the bits set disagree with the declared owned count', bits: pt.setBits, declared: pt.declared.owned });
    }
    if (pt.bitsOutsideCoveredRange !== 0) {
      dumpProblems.push({ point: name, why: 'a frame outside the addressable range is set', outside: pt.bitsOutsideCoveredRange });
    }
  }
  add('frameset.dumps_internally_consistent',
    'every dump covers its declared range with contiguous lines and its set bits equal its declared owned count',
    dumpProblems.length === 0, { problems: dumpProblems.slice(0, 6) });

  // The deltas the guest printed are recomputed from the words. The printed pair is then a claim that can be wrong, instead of
  // being the only account of what moved.
  // The labels are the exact strings the kernel prints, taken from the real channel rather than guessed: it writes `before-after`,
  // with a hyphen. A label written here as `before->after` matched nothing, and the obligation that every pair has a printed delta
  // then refused a real run for a reason that was this reader's mistake.
  const DIFFS = [['before', 'after', 'before-after'], ['victim', 'withdrawn', 'victim-withdrawn'],
    ['baseline', 'final', 'baseline-final']];
  const diffRows = [];
  for (const [from, to, label] of DIFFS) {
    const a = sets.points.get(from);
    const b = sets.points.get(to);
    const printed = sets.deltas.get(label);
    const computed = frameSetDiff(a, b);
    diffRows.push({ label, printed: printed || null,
      computed: computed ? { added: computed.added.length, removed: computed.removed.length, identical: computed.identical } : null });
  }
  add('frameset.deltas_printed_for_every_pair', 'the run printed a delta for each of the three pairs it must compare',
    diffRows.every((r) => r.printed !== null), { rows: diffRows.map((r) => r.label) });
  add('frameset.deltas_recompute_from_bits',
    'the added and removed counts the run printed equal the counts recomputed from the two bitmaps',
    diffRows.every((r) => r.printed !== null && r.computed !== null
      && r.printed.added === r.computed.added && r.printed.removed === r.computed.removed),
    { rows: diffRows });

  // The three set-level claims, each stated as bit identity or as an exact membership relation rather than as a count.
  const baselineSet = sets.points.get('baseline');
  const victimSet = sets.points.get('victim');
  const beforeSet = sets.points.get('before');
  const afterSet = sets.points.get('after');
  const withdrawnSet = sets.points.get('withdrawn');
  const finalSet = sets.points.get('final');
  const refusalDiff = frameSetDiff(beforeSet, afterSet);
  add('frameset.refusal_moved_no_bit',
    'the frame set across the starved create is bit-identical, so the refused call claimed and released nothing',
    refusalDiff !== null && refusalDiff.identical === true,
    { diff: refusalDiff ? { added: refusalDiff.added.slice(0, 8), removed: refusalDiff.removed.slice(0, 8),
      addedCount: refusalDiff.added.length, removedCount: refusalDiff.removed.length } : null });
  const withdrawDiff = frameSetDiff(victimSet, withdrawnSet);
  add('frameset.withdrawal_restored_the_bits',
    'the frame set after the withdrawal is bit-identical to the set the second object left, not merely equal in count',
    withdrawDiff !== null && withdrawDiff.identical === true,
    { diff: withdrawDiff ? { addedCount: withdrawDiff.added.length, removedCount: withdrawDiff.removed.length } : null });
  const finalDiff = frameSetDiff(baselineSet, finalSet);
  add('frameset.final_equals_baseline_bit_for_bit',
    'the final frame set is bit-identical to the prepared baseline, so every frame the run took was given back to the same place',
    finalDiff !== null && finalDiff.identical === true,
    { diff: finalDiff ? { addedCount: finalDiff.added.length, removedCount: finalDiff.removed.length } : null });

  // The sibling's frames are still held at the end and the object cost is the difference the bits show, which is the "own natural
  // pressure" claim read from the set rather than from a subtraction of two printed counts.
  // The baseline-to-victim difference is two objects, not one: the sibling is created first and the victim second, and the baseline
  // dump predates both. The first version of this obligation compared it with the single object cost and refused a real run for a
  // factor of two, which was this reader's error and not the run's. What the scene prints makes the intended check possible: the
  // object cost, the allocated count at the baseline, and the allocated count after the victim are all in the log, so the bits are
  // held to the printed arithmetic rather than to a constant.
  const baselineToVictim = frameSetDiff(baselineSet, victimSet);
  const costFromBits = baselineToVictim === null ? null : baselineToVictim.added.length - baselineToVictim.removed.length;
  const printedRise = afterVictimAllocated !== null && baselineAllocated !== null
    ? afterVictimAllocated - baselineAllocated : null;
  add('frameset.object_cost_from_bits',
    'the frames the baseline-to-victim difference adds, counted from the bits, equal the rise the run printed in its allocated '
      + 'count, and that rise is exactly two object costs',
    costFromBits !== null && cost !== null && printedRise !== null && baselineToVictim.removed.length === 0
      && costFromBits === printedRise && printedRise === 2 * cost,
    { costFromBits, printedRise, printedCost: cost, expected: cost === null ? null : 2 * cost,
      removed: baselineToVictim ? baselineToVictim.removed.length : null });

  // The locator lines are the first sixteen members of each side. They must be members of the recomputed difference and they must
  // be a prefix of it in ascending frame order, which is the order the kernel walks its words.
  const locatorsAgree = (side, list) => {
    if (list.length === 0) return true;
    const ascending = list.every((v, i) => i === 0 || v > list[i - 1]);
    const subset = list.every((v) => side.includes(v));
    return ascending && subset;
  };
  add('frameset.locators_are_members',
    'the frame numbers the run printed as locators are ascending members of the difference recomputed from the bits',
    locatorsAgree(refusalDiff ? refusalDiff.added : [], sets.locators.added)
      && locatorsAgree(refusalDiff ? refusalDiff.removed : [], sets.locators.removed),
    { printedAdded: sets.locators.added.length, printedRemoved: sets.locators.removed.length,
      computedAdded: refusalDiff ? refusalDiff.added.length : null, computedRemoved: refusalDiff ? refusalDiff.removed.length : null });

  // The counts the guest printed beside the sets must agree with the bits too, or the two accounts of the same run disagree.
  add('frameset.owned_counts_agree_with_bits',
    'the owned count printed at each point equals the number of bits set in that point bitmap',
    POINTS.every((n) => { const pt = sets.points.get(n); return pt && pt.declared && pt.setBits === pt.declared.owned; }),
    { rows: POINTS.map((n) => { const pt = sets.points.get(n);
      return { point: n, bits: pt ? pt.setBits : null, declared: pt && pt.declared ? pt.declared.owned : null }; }) });

  // -------------------------------------------------------------- the range, which every point must share
  // A point's header declares how many frames the allocator can address at that moment. If two points disagree, the two dumps are not
  // dumps of the same allocator and comparing their bits is comparing two different address spaces: a frame number means one thing in one
  // and something else in the other, and a difference computed across them is arithmetic on unrelated numbers. So the range is required
  // to be the same at all six points, and it is required to be the range the scene is built around.
  const declaredRanges = POINTS.map((n) => { const pt = sets.points.get(n); return pt && pt.declared ? pt.declared.frames : null; });
  const rangeSet = [...new Set(declaredRanges)];
  add('frameset.same_addressable_range',
    'all six points declare the same number of addressable frames, so their bitmaps describe one allocator rather than two',
    rangeSet.length === 1 && rangeSet[0] !== null,
    { ranges: POINTS.map((n, i) => ({ point: n, frames: declaredRanges[i] })) });

  // A dump-shape obligation was drafted here and is deliberately not added. It would have required the offsets to be 0, 16, 32, ... with
  // sixteen words per line and every padding bit above the declared range clear -- and every one of those is already enforced:
  // `frameset.dumps_internally_consistent` refuses a point whose lines are not contiguous, whose line count does not cover the range, whose
  // set bits disagree with its declared owned count, or which has a bit above the addressable range set. That was checked by mutating a real
  // channel rather than by reading the code: a padding bit set in the last line is refused by `frameset.dumps_internally_consistent` for the
  // owned-count mismatch, and a relabelled offset is refused there for the resulting hole. A second obligation that can only fail when the
  // first one already has adds a name and no coverage, so the shape claim stays where it was.

  // -------------------------------------------------------------- the members are frames, and they are exact
  // Every difference this judge computes is required to be a set of bounded integers with no duplicates. This is the obligation that the
  // 147 reader could not state, because its enumeration returned null for the top bit and a set containing null is not a set of frames.
  const allDiffs = [
    ['before-after', refusalDiff], ['victim-withdrawn', withdrawDiff], ['baseline-final', finalDiff],
    ['baseline-victim', baselineToVictim],
  ].filter(([, d]) => d !== null);
  add('frameset.members_are_bounded_integers',
    'every frame the differences enumerate is an integer inside the addressable range, with no null, no fraction and no duplicate',
    allDiffs.length === 4 && allDiffs.every(([, d]) => d.every_member_is_a_bounded_integer && d.no_duplicate_members && d.invalid.length === 0),
    { diffs: allDiffs.map(([label, d]) => ({ label, added: d.added.length, removed: d.removed.length,
      invalid: d.invalid.slice(0, 4), bounded: d.every_member_is_a_bounded_integer, unique: d.no_duplicate_members })) });

  // A second enumeration, written differently on purpose: it walks every bit of every word in order and asks whether the two points differ
  // there, rather than isolating the low bit of a gain word. If the two disagree, the fast enumeration is wrong and the member list cannot
  // be trusted; agreeing means the members are the set the bits define, not the set one loop happened to produce.
  const bruteForceDiff = (beforePt, afterPt) => {
    if (!beforePt || !afterPt) return null;
    const words = Math.max(beforePt.wordsDense.length, afterPt.wordsDense.length);
    const a = []; const b = [];
    for (let w = 0; w < words; w++) {
      const x = (beforePt.wordsDense[w] === undefined ? 0 : beforePt.wordsDense[w]) >>> 0;
      const y = (afterPt.wordsDense[w] === undefined ? 0 : afterPt.wordsDense[w]) >>> 0;
      for (let bit = 0; bit < 32; bit++) {
        const inA = (x >>> bit) & 1;
        const inB = (y >>> bit) & 1;
        if (!inA && inB) a.push(w * 32 + bit);
        if (inA && !inB) b.push(w * 32 + bit);
      }
    }
    return { added: a, removed: b };
  };
  const bruteChecks = [['before-after', beforeSet, afterSet], ['victim-withdrawn', victimSet, withdrawnSet],
    ['baseline-final', baselineSet, finalSet], ['baseline-victim', baselineSet, victimSet]].map(([label, x, y]) => {
    const fast = frameSetDiff(x, y);
    const slow = bruteForceDiff(x, y);
    const agree = fast !== null && slow !== null
      && JSON.stringify(fast.added) === JSON.stringify(slow.added)
      && JSON.stringify(fast.removed) === JSON.stringify(slow.removed);
    return { label, agree, fastAdded: fast ? fast.added.length : null, slowAdded: slow ? slow.added.length : null };
  });
  add('frameset.bit_walk_agrees_with_enumeration',
    'a bit-by-bit walk of the same two bitmaps produces exactly the member lists the fast enumeration produces',
    bruteChecks.every((c) => c.agree === true), { checks: bruteChecks });

  // The high bit is the case the 147 enumeration got wrong, so the real record is required to carry a member at or above bit 31 of some
  // word when the bits say there is one. This is not a demand that a particular frame appear: it is the statement that if the highest bit
  // of any word moved, the member list contains that frame as a number, which is exactly what a null cannot be.
  const highBitMembers = allDiffs.flatMap(([label, d]) => [...d.added.map((f) => ({ label, side: 'added', f })),
    ...d.removed.map((f) => ({ label, side: 'removed', f }))]).filter((m) => (m.f & 31) === 31);
  add('frameset.top_bit_members_are_numbers',
    'every member that sits at the top bit of its word is present in the difference as a number, not as a null or a NaN',
    highBitMembers.every((m) => Number.isInteger(m.f) && m.f >= 0),
    { top_bit_members: highBitMembers.map((m) => ({ label: m.label, side: m.side, frame: m.f })) });

  facts.frameSets = {
    points: POINTS.map((n) => { const pt = sets.points.get(n);
      return { point: n, frames: pt && pt.declared ? pt.declared.frames : null,
        owned: pt && pt.declared ? pt.declared.owned : null, bits: pt ? pt.setBits : null,
        lines: pt ? pt.dumpLines : null }; }),
    diffs: diffRows,
    object_cost_from_bits: costFromBits,
  };

  const finalReapedReadings = all(p, fam, 'FINAL REAPED');
  const reapedMonotone = finalReapedReadings.every((v, i) => i === 0 || v >= finalReapedReadings[i - 1]);
  // Four objects: the sibling that is live before the pressure, the victim, the object the same instance recovers with, and the
  // object that takes the retired victim's slot. The count is written down rather than derived, because a run that created one
  // object and retired it would satisfy "created equals exited" while proving nothing about reuse. The earlier version of this
  // obligation said two, which was true of the scene before the slot-reuse block was added.
  add('frame.totals', 'the totals the run printed agree: four created, four exited, and the reaped count rises to four',
    finalCreated === 4 && finalExited === 4 && finalReapedReadings.length >= 1 && reapedMonotone
      && finalReapedReadings[finalReapedReadings.length - 1] === 4,
    { finalCreated, finalExited, finalReapedReadings });
  facts.members = { siblingId, recoveredId, spareSlots: capacity, claimed, returned, cost };
  facts.end = { allocated: finalAllocated, free: finalFree, digest: finalDigest };
  return finish(out, facts);
}

function finish(out, facts) {
  const problems = out.filter((o) => !o.ok).map((o) => ({ obligation: o.id, why: o.claim, detail: o.detail }));
  return {
    scene: facts.scene,
    kind: facts.kind,
    accepted: problems.length === 0,
    obligations: out,
    counts: { checked: out.length, held: out.length - problems.length, refused: problems.length },
    first_refusal: problems[0] || null,
    problems,
    facts,
    note: 'this judge reads the channel the guest wrote: the member sets it enumerates and the arithmetic relations between the '
      + 'numbers it printed. A matching digest does not override a differing member set, and a present marker does not '
      + 'override a failure line.',
  };
}

// ------------------------------------------------------------------ the channel mutations
// One list, used by the negatives. Each entry names the obligation it is supposed to trip, so a negative that is refused for
// the wrong reason is visible rather than counted as a pass.
// A mutation may name the scenes it applies to. The four bitmap mutations do, because only the frame scene dumps a bitmap: applied
// to the objects scene they would change nothing, and a mutation that changes nothing is not a negative. Naming the scope here rather
// than filtering in the caller keeps the statement of what a mutation is good for beside the mutation itself, and it makes an
// out-of-scope application a recorded skip instead of a silent one.
export const MUTATIONS = [
  { id: 'drop-marker', targets: 'marker.present', apply: (t) => t.split('\n').filter((l) => !l.startsWith('BOOTDIAG ')).join('\n') },
  { id: 'duplicate-marker', targets: 'marker.present',
    apply: (t) => { const i = t.split('\n'); const m = i.find((l) => l.startsWith('BOOTDIAG ')); return i.concat([m]).join('\n'); } },
  { id: 'empty-channel', targets: 'channel.e9.non_empty', apply: () => '' },
  { id: 'truncate-half', targets: 'marker.present', apply: (t) => t.slice(0, Math.floor(t.length / 2)) },
  { id: 'other-channel-differs', targets: 'channel.pair.identical', apply: (t) => t + '\n' },
  { id: 'failure-line-added', targets: 'family.no_failure_line',
    apply: (t) => t.replace(/\nBOOTDIAG /, '\n__FAMILY__ DIAG DATA SHARED\nBOOTDIAG ') },
  { id: 'foreign-family', targets: 'family.no_other_diagnostic',
    apply: (t) => '__OTHER__ DIAG DETECTED 00000001\n' + t },
  // The end-state free count is nudged by one frame while every other line is left alone. The key is chosen in the order the
  // three scenes name their end state, and the first key that is actually present is the one nudged: a single alternation over
  // the three names matched the frame scene's mid-run `AFTER FREE` first, which tripped a different (also true) obligation and
  // left the end-state comparison untouched. The mutation names the real family rather than a placeholder, because it is
  // applied to a real channel and a placeholder pattern would match nothing there.
  { id: 'baseline-drift', targets: 'end==baseline',
    apply: (t) => {
      for (const key of ['FINAL FREE', 'RESTORED FREE', 'AFTER FREE']) {
        const re = new RegExp('(^(?:OBJECT|RECOVERY|FRAMEREC) DIAG ' + key + ' )([0-9A-F]{8})', 'm');
        const out = t.replace(re, (m, a, v) => a + bump(v));
        if (out !== t) return out;
      }
      return t;
    } },
  { id: 'second-trial-dropped', targets: 'marker.present', apply: (t) => t.split('\n').slice(0, 8).join('\n') },
  // The bitmap mutations. Each one changes a single fact about the dumped frame set and leaves every count the guest printed alone,
  // which is the case a judge reading only the printed numbers cannot see: a bit moved is invisible to OWNED, and a delta that lies
  // about a difference the words do not show is invisible unless the words are read.
  { id: 'frameset-bit-flip', scenes: ['runtime-frame-recovery-smp1'], targets: 'frameset.dumps_internally_consistent',
    apply: (t) => {
      // One bit is set in a baseline word that was zero, so the declared owned count no longer matches the bits.
      const lines = t.split('\n');
      for (let i = 0; i < lines.length; i++) {
        const m = /^(FRAMEREC DIAG BM baseline [0-9A-F]{8})([0-9A-F]{128})$/.exec(lines[i].trim());
        if (!m) continue;
        const words = m[2].match(/.{8}/g);
        if (words[0] !== '00000000') continue;
        words[0] = '00000001';
        lines[i] = m[1] + words.join('');
        return lines.join('\n');
      }
      return t;
    } },
  { id: 'frameset-delta-lies', scenes: ['runtime-frame-recovery-smp1'], targets: 'frameset.deltas_recompute_from_bits',
    apply: (t) => t.replace(/^(FRAMEREC DIAG DELTA before-after ADDED )([0-9A-F]{8})/m,
      (m, a, v) => a + bump(v)) },
  { id: 'frameset-final-bit-moved', scenes: ['runtime-frame-recovery-smp1'], targets: 'frameset.final_equals_baseline_bit_for_bit',
    apply: (t) => {
      // A frame is added to the final set and the owned count is bumped with it, so the dump stays internally consistent and only
      // the comparison with the baseline can catch it.
      const lines = t.split('\n');
      let touched = false;
      for (let i = 0; i < lines.length; i++) {
        const m = /^(FRAMEREC DIAG BM final [0-9A-F]{8})([0-9A-F]{128})$/.exec(lines[i].trim());
        if (!m || touched) continue;
        const words = m[2].match(/.{8}/g);
        const at = words.findIndex((w) => w === '00000000');
        if (at < 0) continue;
        words[at] = '00000001';
        lines[i] = m[1] + words.join('');
        touched = true;
      }
      if (!touched) return t;
      return lines.join('\n').replace(/^(FRAMEREC DIAG SET final FRAMES [0-9A-F]{8} OWNED )([0-9A-F]{8})/m,
        (m, a, v) => a + bump(v));
    } },
  // The obligations this packet added, and the mutations that are supposed to refuse through them.
  //
  // The three below each change one property of the dump that the older obligations were blind to: the range a point declares, the shape of
  // its lines and the padding above its range, and whether all six points are there to be differenced at all. A judge that kept the
  // pre-145 set would accept every one of these channels, which is what makes them negatives rather than noise.
  { id: 'frameset-range-differs', scenes: ['runtime-frame-recovery-smp1'], targets: 'frameset.same_addressable_range',
    // One point declares a range the others do not. The words are untouched, so the only way to see this is to compare the six headers: a
    // reader that trusted each point on its own would compare bitmaps of two different address spaces and report a clean difference.
    apply: (t) => t.replace(/^(FRAMEREC DIAG SET victim FRAMES )([0-9A-F]{8})/m,
      (m, a, v) => (v === '00007FF0' ? a + '00007FE8' : a + '00007FF0')) },
  { id: 'frameset-point-dropped', scenes: ['runtime-frame-recovery-smp1'], targets: 'frameset.members_are_bounded_integers',
    // Every line of one point is removed, so one of the four differences cannot be formed at all. The boundedness of a member cannot be
    // expressed by a channel -- the parser only ever produces integers -- so what a channel can show is that the obligation is not vacuous
    // when a difference is missing. The boundedness itself is tested against the production functions directly.
    apply: (t) => t.split('\n').filter((l) => !/^FRAMEREC DIAG (SET|BM) withdrawn /.test(l.trim())).join('\n') },
  // The slot and identifier numbers. Each mutation changes one number and leaves every statement alone, which is the case a judge reading
  // only the words cannot see: `SLOT REUSED` is still printed, `NEW GENERATION` is still printed, and only the numbers disagree.
  { id: 'frame-new-slot-differs', scenes: ['runtime-frame-recovery-smp1'], targets: 'frame.new_victim_slot_is_the_vacated_one',
    // The new object reports a slot the victim never held. The words still say the slot was reused.
    apply: (t) => t.replace(/^(FRAMEREC DIAG NEW VICTIM SLOT )([0-9A-F]{8})$/m,
      (m, a, v) => a + (v === '00000002' ? '00000003' : '00000002')) },
  { id: 'frame-slot-vacated-differs', scenes: ['runtime-frame-recovery-smp1'], targets: 'frame.victim_slot_vacated_is_the_same_slot',
    // The slot the victim is reported to have vacated is not the one it occupied, so `VICTIM SLOT VACATED` is a statement without a number
    // behind it.
    apply: (t) => t.replace(/^(FRAMEREC DIAG VICTIM SLOT AFTER )([0-9A-F]{8})$/m,
      (m, a, v) => a + (v === '00000002' ? '00000003' : '00000002')) },
  { id: 'frame-id-reissued', scenes: ['runtime-frame-recovery-smp1'], targets: 'frame.identifier_numbers_distinct',
    // The object in the reused slot is given the retired identifier, so two of the four objects share a number while `NEW GENERATION` is
    // still printed.
    apply: (t) => {
      const victim = /^FRAMEREC DIAG VICTIM ID ([0-9A-F]{8})$/m.exec(t);
      if (!victim) return t;
      return t.replace(/^(FRAMEREC DIAG NEW VICTIM ID )([0-9A-F]{8})$/m, (m, a) => a + victim[1]);
    } },
  { id: 'frame-victim-cost-lies', scenes: ['runtime-frame-recovery-smp1'], targets: 'frame.victim_cost_matches_the_object_cost',
    // The cost printed for the victim no longer equals the object cost, and no longer equals the rise in the allocated count.
    apply: (t) => t.replace(/^(FRAMEREC DIAG VICTIM COST )([0-9A-F]{8})$/m,
      (m, a, v) => a + (parseInt(v, 16) + 1).toString(16).toUpperCase().padStart(8, '0')) },
  // A line the reader has no rule for. Nothing above it is disturbed, so only an obligation that accounts for every line can refuse
  // it -- which is the point: an unreadable line must not be an unjudged line.
  { id: 'unreadable-line', targets: 'channel.every_line_accounted_for',
    apply: (t) => t.replace(/\nBOOTDIAG /, '\nWARNING FRAME TABLE MIGHT BE STALE\nBOOTDIAG ') },
];

function bump(hex) {
  const n = parseInt(hex, 16) + 1;
  return n.toString(16).toUpperCase().padStart(hex.length, '0');
}
