// GTOS-DSH-145 G: re-consume the 144 guest records with the judge this packet changed, without claiming any new execution.
//
// The 144 matrix is 28 guarded trials over 20 scenes, and its raw channels are still on disk. The taskbook is explicit that these are
// re-read rather than re-run: the launches happened, the guard cleaned up after each one, and restarting 28 VMs to accommodate a change of
// checker would spend executions to produce the same bytes. So every case here opens the original channel files read-only, calls the same
// judge the driver calls, and records `source_task: GTOS-DSH-144` and `counted_as_145_execution: false` beside every row.
//
// Two things are established, and they are different:
//
//   G  that the 28 trials still pass the updated judge -- a statement about the judge and about those recorded runs, not a statement that
//      this packet ran anything.
//   R  that the obligations the judge gained in this packet are exercised by those runs: the addressable range is the same at all six
//      points, the dump shape and tail padding hold, every enumerated member is a bounded integer, and the fast enumeration agrees with a
//      bit-by-bit walk. A judge that gained obligations nothing real exercises has not been tested against reality.
//
// Two judges own the matrix, and each trial is routed to the one that owns its family: the resource scenes carry FRAMEREC/OBJECT/RECOVERY
// diagnostics and are judged by `judgeResourceSemantics`; every other scene is judged by `functionalVerdict`, which reads its marker, its
// permits, its forbids list and its also_required lines. Calling only the resource judge throws on the other scenes; calling only the
// functional one would never exercise the frame obligations. Which judge ran is recorded per row.
import fs from 'node:fs';
import crypto from 'node:crypto';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const REF = R + '/worktree-145/tools/gtos-dsh-144/references/resource-semantics-144.mjs';
const ADM = R + '/worktree-145/tools/gtos-dsh-144/references/scene-admission-144.mjs';
const { judgeResourceSemantics, parseFrameSets, frameSetDiff } = await import('file:///' + REF.replace(/\\/g, '/'));
const { functionalVerdict, scene } = await import('file:///' + ADM.replace(/\\/g, '/'));
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

const RESOURCE_SCENES = ['runtime-objects-smp1', 'runtime-recovery-smp1', 'runtime-frame-recovery-smp1',
  'runtime-objects-smp2', 'runtime-objects-smp3', 'runtime-recovery-smp2', 'runtime-recovery-smp3',
  'runtime-frame-recovery-smp2', 'runtime-frame-recovery-smp3'];

const matrix = JSON.parse(fs.readFileSync(R144 + '/g-matrix-gm144-01-144.json', 'utf8'));
const lines = (text) => String(text).split('\n').map((l) => l.replace(/\r$/, ''));

const rows = [];
for (let i = 1; i <= 28; i++) {
  const n = String(i).padStart(2, '0');
  const trialPath = R144 + '/g-trial-gm144-' + n + '-144.json';
  if (!fs.existsSync(trialPath)) continue;
  const trial = JSON.parse(fs.readFileSync(trialPath, 'utf8'));

  // Each channel is opened read-only and hashed against the hash the 144 trial record already carries, so a channel that changed since
  // 144 is visible here instead of being judged as though it had not.
  const text = {};
  const identities = {};
  for (const side of ['e9', 'serial']) {
    const c = trial.channels && trial.channels[side] ? trial.channels[side] : null;
    if (!c || !c.path || !fs.existsSync(c.path)) continue;
    const bytes = fs.readFileSync(c.path);
    text[side] = bytes.toString('utf8');
    identities[side] = { path: c.path, linux_path: c.linux_path || null, bytes: bytes.length, sha256: sha(bytes),
      matches_the_144_record: sha(bytes) === c.sha256 && bytes.length === c.bytes };
  }

  const isResource = RESOURCE_SCENES.includes(trial.scene);
  // The resource judge reads the channel as text; the functional verdict reads it as a list of lines. Each gets the form it takes, from
  // the same bytes, so neither is judging a different file.
  const verdict = isResource
    ? judgeResourceSemantics(trial.scene, text)
    : functionalVerdict(scene(trial.scene), { e9: text.e9 === undefined ? null : lines(text.e9),
      serial: text.serial === undefined ? null : lines(text.serial) });
  const obligations = Array.isArray(verdict && verdict.obligations) ? verdict.obligations : [];
  const problems = Array.isArray(verdict && verdict.problems) ? verdict.problems : [];
  const framesetObligations = obligations.filter((o) => String(o.id || '').startsWith('frameset.'));
  rows.push({
    trial: trial.trial, scene: trial.scene,
    source_task: trial.task || 'GTOS-DSH-144',
    counted_as_145_execution: false,
    judge: isResource ? 'resource-semantics' : 'scene-admission-functional',
    channels_read: Object.keys(text),
    channel_identities: identities,
    channel_hashes_match_the_144_record: Object.keys(identities).length === 2
      && Object.values(identities).every((c) => c.matches_the_144_record),
    obligations_checked: obligations.length,
    frameset_obligations_checked: framesetObligations.length,
    problems: problems.length,
    problem_ids: problems.map((p) => p.obligation || p.id || null),
    accepted: verdict ? verdict.accepted === true : false,
    first_refusal: verdict && verdict.first_refusal ? (verdict.first_refusal.obligation || verdict.first_refusal.id || verdict.first_refusal.why || null) : null,
  });
}

// The frame obligations, read from a real frame channel so the list is what the judge actually emits rather than what this file hopes it
// emits.
const frameTrial = JSON.parse(fs.readFileSync(R144 + '/g-trial-gm144-07-144.json', 'utf8'));
const frameChannelPath = frameTrial.channels.e9.path;
const frameText = fs.readFileSync(frameChannelPath, 'utf8');
const frameVerdict = judgeResourceSemantics('runtime-frame-recovery-smp1', { e9: frameText, serial: frameText });
const framesetDetail = (frameVerdict.obligations || []).filter((o) => String(o.id || '').startsWith('frameset.'))
  .map((o) => ({ id: o.id, ok: o.ok, claim: o.claim, detail: o.detail }));
// The obligations this packet added, named here so the record states which ones it is checking rather than checking whatever happens to be
// there. Four are about the frame bits; six are about the slot and identifier numbers the scene prints.
const ADDED = ['frameset.same_addressable_range', 'frameset.members_are_bounded_integers',
  'frameset.bit_walk_agrees_with_enumeration', 'frameset.top_bit_members_are_numbers',
  'frame.live_slots_distinct', 'frame.victim_slot_vacated_is_the_same_slot', 'frame.new_victim_slot_is_the_vacated_one',
  'frame.identifier_numbers_distinct', 'frame.new_victim_id_is_a_new_generation', 'frame.victim_cost_matches_the_object_cost'];
const addedDetail = ADDED.map((id) => {
  const found = (frameVerdict.obligations || []).find((o) => o.id === id) || null;
  return { id, present: found !== null, ok: found ? found.ok === true : null, claim: found ? found.claim : null,
    detail: found ? found.detail : null };
});

// The member enumeration on the real channel, with an independent bit-by-bit walk beside it, so a reader can check the claim rather than
// take the boolean. The walk asks of every bit position whether the two points differ there, instead of isolating a low bit of a gain word.
const sets = parseFrameSets(frameText);
const b = sets.points.get('baseline');
const v = sets.points.get('victim');
const fast = frameSetDiff(b, v);
const walkAdded = [];
const words = Math.max(b.wordsDense.length, v.wordsDense.length);
for (let i = 0; i < words; i++) {
  const x = (b.wordsDense[i] === undefined ? 0 : b.wordsDense[i]) >>> 0;
  const y = (v.wordsDense[i] === undefined ? 0 : v.wordsDense[i]) >>> 0;
  for (let bit = 0; bit < 32; bit++) if (!((x >>> bit) & 1) && ((y >>> bit) & 1)) walkAdded.push(i * 32 + bit);
}

const out = {
  task: 'GTOS-DSH-145',
  what: 'the 144 guest matrix re-consumed with the judge this packet changed',
  source_task: 'GTOS-DSH-144',
  counted_as_145_execution: false,
  execution_claim: 'none: this record reads channels that 144 wrote. It does not launch a VM, boot the kernel or run a scene.',
  judges: {
    resource: { path: REF, bytes: fs.statSync(REF).size, sha256: sha(fs.readFileSync(REF)) },
    functional: { path: ADM, bytes: fs.statSync(ADM).size, sha256: sha(fs.readFileSync(ADM)) },
  },
  matrix: { path: R144 + '/g-matrix-gm144-01-144.json', bytes: fs.statSync(R144 + '/g-matrix-gm144-01-144.json').size,
    scenes: Array.isArray(matrix.scenes) ? matrix.scenes.length : null, trials: Array.isArray(matrix.trials) ? matrix.trials.length : null },
  how_the_channels_were_read: 'each channel file is opened read-only and its bytes are hashed against the hash the 144 trial record already '
    + 'carries, so a channel that changed since 144 would be visible here instead of being judged as if it had not',
  rows,
  counts: {
    trials_judged: rows.length,
    resource_trials: rows.filter((r) => r.judge === 'resource-semantics').length,
    functional_trials: rows.filter((r) => r.judge === 'scene-admission-functional').length,
    trials_with_frame_obligations: rows.filter((r) => r.frameset_obligations_checked > 0).length,
    trials_accepted: rows.filter((r) => r.accepted === true).length,
    trials_refused: rows.filter((r) => r.accepted !== true).length,
    frameset_obligations_on_the_frame_scene: framesetDetail.length,
  },
  every_channel_matches_its_144_record: rows.every((r) => r.channel_hashes_match_the_144_record),
  frameset_obligations: framesetDetail,
  added_obligations: addedDetail,
  counts_added: { added: ADDED.length, present: addedDetail.filter((o) => o.present).length,
    true_on_the_real_channel: addedDetail.filter((o) => o.ok === true).length },
  real_member_enumeration: {
    scene: 'runtime-frame-recovery-smp1', trial: frameTrial.trial, channel: frameChannelPath,
    frames_declared: b.declared ? b.declared.frames : null,
    added: fast.added, removed: fast.removed,
    added_count: fast.added.length, removed_count: fast.removed.length,
    contains_frame_2655: fast.added.includes(2655),
    any_member_not_an_integer: fast.added.concat(fast.removed).some((x) => !Number.isInteger(x)),
    agrees_with_the_bit_walk: JSON.stringify(fast.added) === JSON.stringify(walkAdded),
    bit_walk_added_count: walkAdded.length,
  },
  ok: rows.every((r) => r.accepted === true) && rows.every((r) => r.channel_hashes_match_the_144_record)
    && rows.every((r) => r.channels_read.length === 2)
    && ADDED.every((id) => addedDetail.some((o) => o.id === id && o.present === true && o.ok === true))
    && fast.added.length === 14 && fast.added.includes(2655) && !fast.added.concat(fast.removed).some((x) => !Number.isInteger(x)),
  ok_means: 'every one of the 28 recorded 144 trials is accepted by the updated judges, every channel still hashes to what the 144 record '
    + 'says, and the ten obligations this packet added -- four on the frame bits and six on the slot and identifier numbers -- are present '
    + 'and true on the real frame-recovery channel, whose baseline-to-victim difference is fourteen bounded integers including frame 2655',
};
const RUN = process.argv[2] || '01';
const target = R + '/g-reconsume-145-' + RUN + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: out.ok, counts: out.counts, real: out.real_member_enumeration,
  refused: rows.filter((r) => r.accepted !== true).map((r) => ({ trial: r.trial, scene: r.scene, judge: r.judge,
    first: r.first_refusal, ids: r.problem_ids })) }, null, 2));
process.exit(out.ok ? 0 : 1);
