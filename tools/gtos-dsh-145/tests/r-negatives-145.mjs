// GTOS-DSH-145 R negatives: every mutation aimed at an obligation this packet added must be refused, and refused by that obligation.
//
// The positive case is the untouched real channel, read back from a copy so the judge is shown to depend on the bytes and not on the path.
// Each negative applies one mutation to those bytes, writes the result to a fresh test-only tree, and judges the copy. Two things are
// recorded per case: that the channel was refused at all, and which obligation refused it first and whether the aimed-at obligation is
// among the refusals. A mutation that is accepted is a hole; a mutation refused for the wrong reason is a test that proves nothing.
//
// The four mutations here are the ones added in this packet, and each targets an obligation added with it. The pre-145 mutations are left
// where they are and are not repeated: this record is about the new checks, and re-running the old ones would make the count look larger
// without testing anything new.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const REF = R + '/worktree-145/tools/gtos-dsh-144/references/resource-semantics-144.mjs';
const { judgeResourceSemantics, MUTATIONS } = await import('file:///' + REF.replace(/\\/g, '/'));
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

const RUN = process.argv[2] || '01';
const TREE = R + '/test-only/r-negatives-145-' + RUN;
if (fs.existsSync(TREE)) throw new Error('refused, exists: ' + TREE);
fs.mkdirSync(TREE, { recursive: true });

const SCENE = 'runtime-frame-recovery-smp1';
const trial = JSON.parse(fs.readFileSync(R144 + '/g-trial-gm144-07-144.json', 'utf8'));
const channelPath = trial.channels.e9.path;
const original = fs.readFileSync(channelPath, 'utf8');

const write = (name, text) => {
  const p = path.join(TREE, name);
  fs.writeFileSync(p, text, { encoding: 'utf8', flag: 'wx' });
  const bytes = fs.readFileSync(p);
  return { path: p, bytes: bytes.length, sha256: sha(bytes) };
};

// The positive: the untouched channel, copied and read back.
const pos = write('positive.e9.log', original);
const posVerdict = judgeResourceSemantics(SCENE, { e9: fs.readFileSync(pos.path, 'utf8'), serial: fs.readFileSync(pos.path, 'utf8') });

const cases = [];
const problems = [];
if (posVerdict.accepted !== true) {
  problems.push({ why: 'the untouched copy of a passing channel was refused', first_refusal: posVerdict.first_refusal });
}
cases.push({
  side: 'positive', mutation: null,
  e9: pos, serial: pos,
  accepted: posVerdict.accepted === true,
  obligations_checked: posVerdict.counts.checked,
  first_refusal: posVerdict.first_refusal,
});

const NEW_MUTATIONS = ['frameset-range-differs', 'frameset-point-dropped',
  'frame-new-slot-differs', 'frame-slot-vacated-differs', 'frame-id-reissued', 'frame-victim-cost-lies'];
for (const id of NEW_MUTATIONS) {
  const m = MUTATIONS.find((x) => x.id === id);
  if (!m) { problems.push({ why: 'the mutation named in this test is not in the judge', mutation: id }); continue; }
  const mutated = m.apply(original);
  if (mutated === original) {
    problems.push({ why: 'the mutation changed nothing, so it is not a negative', mutation: id });
    cases.push({ side: 'negative', mutation: id, targets: m.targets, changed_nothing: true, accepted: null });
    continue;
  }
  const dir = path.join(TREE, id);
  fs.mkdirSync(dir);
  const e9 = write(id + '/e9.log', mutated);
  const serial = write(id + '/serial.log', mutated);
  const verdict = judgeResourceSemantics(SCENE, { e9: fs.readFileSync(e9.path, 'utf8'), serial: fs.readFileSync(serial.path, 'utf8') });
  const refusalIds = verdict.problems.map((p) => p.obligation);
  const hit = refusalIds.includes(m.targets);
  cases.push({
    side: 'negative', mutation: id, targets: m.targets,
    e9, serial,
    accepted: verdict.accepted === true,
    refused_by: verdict.first_refusal,
    refusal_ids: refusalIds,
    refused_by_the_obligation_it_targets: hit,
  });
  if (verdict.accepted === true) problems.push({ why: 'a mutation was accepted', mutation: id, targets: m.targets });
  else if (!hit) problems.push({ why: 'a mutation was refused, but not by the obligation it was aimed at',
    mutation: id, aimed_at: m.targets, refused_by: verdict.first_refusal, refusal_ids: refusalIds });
}

// The obligations this packet added, and whether each has at least one mutation aimed at it. An obligation with no negative is a check
// nothing has shown can fail.
const ADDED = ['frameset.same_addressable_range', 'frameset.members_are_bounded_integers',
  'frameset.bit_walk_agrees_with_enumeration', 'frameset.top_bit_members_are_numbers',
  'frame.live_slots_distinct', 'frame.victim_slot_vacated_is_the_same_slot', 'frame.new_victim_slot_is_the_vacated_one',
  'frame.identifier_numbers_distinct', 'frame.new_victim_id_is_a_new_generation', 'frame.victim_cost_matches_the_object_cost'];
const coverage = ADDED.map((id) => ({
  obligation: id,
  aimed_at_by: NEW_MUTATIONS.filter((mid) => (MUTATIONS.find((x) => x.id === mid) || {}).targets === id),
  refused_at_least_once: cases.some((c) => c.side === 'negative' && c.refused_by_the_obligation_it_targets === true
    && c.targets === id),
}));
// `members_are_bounded_integers`, `bit_walk_agrees_with_enumeration` and `top_bit_members_are_numbers` cannot be falsified by editing a
// channel: the parser only ever produces integers, so no bytes make it produce a null. That is recorded as what it is rather than papered
// over with a mutation that would trip a different obligation.
const NOT_CHANNEL_FALSIFIABLE = {
  'frameset.members_are_bounded_integers': 'the parser cannot produce a non-integer member from any bytes, so this obligation is tested '
    + 'against the production functions directly (r-members-145) rather than by mutating a channel',
  'frameset.bit_walk_agrees_with_enumeration': 'the two enumerations are both computed from the same parsed words, so no channel makes them '
    + 'disagree; the agreement is established in r-members-145 over 32 bit positions and the real record',
  'frameset.top_bit_members_are_numbers': 'the top-bit members are a subset of the enumeration, and the null this obligation guards against '
    + 'is a property of the code rather than of the channel; r-members-145 exercises bit 31 explicitly',
};

const out = {
  task: 'GTOS-DSH-145',
  what: 'the negative side of the obligations this packet added to the frame judge',
  judge: { path: REF, bytes: fs.statSync(REF).size, sha256: sha(fs.readFileSync(REF)) },
  scene: SCENE,
  source_channel: { path: channelPath, bytes: fs.statSync(channelPath).size, sha256: sha(fs.readFileSync(channelPath)),
    source_task: 'GTOS-DSH-144', counted_as_145_execution: false,
    note: 'the channel is the real 144 record, opened read-only and copied into this task test-only tree; no run was made' },
  tree: TREE,
  cases,
  coverage,
  not_channel_falsifiable: NOT_CHANNEL_FALSIFIABLE,
  problems,
  counts: { cases: cases.length, positives: cases.filter((c) => c.side === 'positive').length,
    negatives: cases.filter((c) => c.side === 'negative').length,
    negatives_refused: cases.filter((c) => c.side === 'negative' && c.accepted === false).length,
    negatives_refused_for_the_right_reason:
      cases.filter((c) => c.side === 'negative' && c.refused_by_the_obligation_it_targets === true).length },
  ok: problems.length === 0 && posVerdict.accepted === true
    && cases.filter((c) => c.side === 'negative').every((c) => c.accepted === false && c.refused_by_the_obligation_it_targets === true),
  ok_means: 'the untouched channel is accepted, every mutation aimed at a new obligation is refused, and each is refused by the '
    + 'obligation it was aimed at rather than by something else on the way',
};
const target = R + '/r-negatives-145-' + RUN + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: out.ok, counts: out.counts, cases: cases.map((c) => ({ mutation: c.mutation, accepted: c.accepted,
  targets: c.targets, refused_by: c.refused_by ? (c.refused_by.obligation || c.refused_by.why) : null,
  ids: c.refusal_ids || null })), problems }, null, 2));
process.exit(out.ok ? 0 : 1);
