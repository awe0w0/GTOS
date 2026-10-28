// GTOS-DSH-146 R test: the numeric judge on every real trial, then one-number mutations.
//
// The judge is worth only what its obligations catch, so this test has three halves:
//
//   * every real non-resource trial (17 scenes, 24 trials) must pass, and the three scenes the judge used to leave to markers
//     must be judged too -- 20 scenes and 28 trials in all. An obligation that is wrong about the guest shows up here as a
//     failure on real data instead of as a claim in a report;
//   * 47 mutations, each changing exactly one printed number, each naming the obligation that must notice. A mutation whose
//     target still passes means that obligation is decorative;
//   * seven negatives a marker judge cannot see: a channel whose markers all survive but one number is wrong, a channel with
//     the numbers stripped, a channel whose two output paths disagree, a ticket that contradicts its own expectation, a
//     refusal that printed a boot number, a refusal that stopped without saying why, and a booted scene that must not be
//     handed the refusal family.
//
// Trials are iterated, not scenes: five scenes ran twice and one ran twice with different CPU counts, and a judge that only
// saw the first trial of each would miss half the evidence.

import crypto from 'node:crypto';
import fs from 'node:fs';
import { judgeNonResource, parseNumbers, applyMutation, MUTATIONS, NUMERIC_SCENES, MARKER_ONLY_SCENES } from '../r-numeric-146.mjs';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const WSL = '//wsl$/Ubuntu-20.04';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const toHost = (p) => (typeof p === 'string' && p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':/' + p.slice(9) : WSL + p);
const read = (p) => fs.readFileSync(toHost(p), 'utf8');

const matrix = JSON.parse(fs.readFileSync(R144 + '/g-matrix-gm144-01-144.json', 'utf8'));
const cases = [];
const rec = (name, expected, observed, extra) => cases.push({ name, expected, observed, ok: expected === observed, ...(extra || {}) });

// ---------------------------------------------------------------- the real trials
const trials = [];
const markerOnly = [];
for (const trial of matrix.trials) {
  const isNumeric = NUMERIC_SCENES.includes(trial.scene);
  const isMarkerOnly = MARKER_ONLY_SCENES.includes(trial.scene);
  if (!isNumeric && !isMarkerOnly) continue; // the resource scenes belong to the bitmap module
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const verdict = judgeNonResource(trial.scene, serialText, e9Text);
  // The three scenes the judge used to leave to markers are now judged too. Two of them never print a boot number -- one
  // panics at B02, one has its option rejected -- so they are judged as refusals: that they stopped, that they said why,
  // and that nothing from the full path followed. The third, `tables-smp1`, does print the boot numbers and is judged on
  // them. Either way the trial is judged, and `obligations_family` says which way.
  const families = [...new Set(verdict.obligations.map((o) => o.obligation.split('.')[0]))];
  const entry = {
    scene: trial.scene, trial: trial.trial,
    family: isMarkerOnly ? 'marker_only' : 'numeric',
    obligations_family: families,
    ok: verdict.ok,
    obligation_count: verdict.obligation_count,
    failed: verdict.failed,
    numbers_read: verdict.numbers_read,
    repeated_labels: verdict.repeated_labels,
    channels_compared: verdict.channels_compared,
    serial: { path: trial.serial.path, bytes: fs.statSync(toHost(trial.serial.path)).size, sha256: sha(Buffer.from(serialText)) },
    e9: { path: trial.e9.path, bytes: fs.statSync(toHost(trial.e9.path)).size, sha256: sha(Buffer.from(e9Text)) },
  };
  if (isMarkerOnly) {
    markerOnly.push(entry);
    rec('marker-only-trial-is-judged:' + trial.scene + '#' + trial.trial, true, verdict.ok,
      { failed: verdict.failed, family: families });
  } else {
    trials.push(entry);
    rec('real-trial-passes:' + trial.scene + '#' + trial.trial, true, verdict.ok, { failed: verdict.failed });
  }
}

const totalObligations = trials.reduce((n, t) => n + t.obligation_count, 0);
const minObligations = Math.min(...trials.map((t) => t.obligation_count));
rec('every-numeric-scene-and-trial-was-judged', NUMERIC_SCENES.length, new Set(trials.map((t) => t.scene)).size);
rec('every-marker-only-scene-is-declared', MARKER_ONLY_SCENES.length, new Set(markerOnly.map((t) => t.scene)).size);
rec('every-marker-only-trial-was-judged', markerOnly.length, markerOnly.filter((t) => t.ok === true).length,
  { scenes: [...new Set(markerOnly.map((t) => t.scene))] });
rec('the-refusal-family-is-used-only-where-no-boot-number-was-printed', true,
  markerOnly.every((t) => (t.numbers_read === 0) === t.obligations_family.includes('refused-boot')),
  { rows: markerOnly.map((t) => ({ scene: t.scene, trial: t.trial, numbers: t.numbers_read, family: t.obligations_family })) });
rec('at-least-one-marker-only-trial-is-a-refusal', true,
  markerOnly.filter((t) => t.obligations_family.includes('refused-boot')).length >= 2,
  { refusals: markerOnly.filter((t) => t.obligations_family.includes('refused-boot')).length });
rec('the-judge-checks-a-substantial-number-of-relations', true, totalObligations >= 150, { obligations: totalObligations, trials: trials.length });
rec('every-trial-checks-more-than-a-marker', true, minObligations >= 8, { min: minObligations });
rec('the-two-channels-were-compared-where-both-exist', true,
  trials.filter((t) => t.channels_compared).length === trials.length,
  { compared: trials.filter((t) => t.channels_compared).length, of: trials.length });

// ---------------------------------------------------------------- the mutations
const byKey = new Map(matrix.trials.map((t) => [t.scene + '#' + t.trial, t]));
const mutations = [];
for (const m of MUTATIONS) {
  const trial = byKey.get(m.scene + '#' + m.trial);
  if (!trial) { rec('mutation-scene-exists:' + m.name, true, false, { scene: m.scene }); continue; }
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const before = judgeNonResource(m.scene, serialText, e9Text);
  const applied = applyMutation(serialText, m.label, m.value);
  if (!applied.applied) {
    mutations.push({ name: m.name, scene: m.scene, trial: m.trial, label: m.label, applied: false, targets: m.targets, caught: null });
    rec('mutation-was-applied:' + m.name, true, false, { label: m.label, why: 'the label is not in this channel, so the mutation tested nothing' });
    continue;
  }
  const after = judgeNonResource(m.scene, applied.text, e9Text);
  const newlyFailed = after.failed.filter((f) => !before.failed.includes(f));
  const caught = newlyFailed.includes(m.targets);
  mutations.push({ name: m.name, scene: m.scene, trial: m.trial, label: m.label, wrote: applied.wrote, applied: true,
    targets: m.targets, caught, newly_failed: newlyFailed.slice(0, 8), newly_failed_count: newlyFailed.length });
  rec('mutation-caught:' + m.name, true, caught, { targets: m.targets, newly_failed: newlyFailed.slice(0, 6) });
  // A mutation that broke everything would also "catch" its target, so the blast radius is recorded: one changed number
  // should not fail the whole scene.
  rec('mutation-is-local:' + m.name, true, newlyFailed.length <= 6, { newly_failed: newlyFailed.length, scene_obligations: after.obligation_count });
}

// ---------------------------------------------------------------- the negatives a marker judge cannot see
{
  // Every marker kept, one number wrong. This is the exact shape a marker-based judge accepts.
  const trial = byKey.get('runtime-init-smp1#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const zeroed = applyMutation(serialText, 'RUNTIME DIAG TSS DESC TYPE', 0);
  const verdict = judgeNonResource('runtime-init-smp1', zeroed.text, e9Text);
  const markersKept = /RUNTIME DIAG TSS OBSERVED/.test(zeroed.text) && /BOOTDIAG RUNTIME INITIALIZED/.test(zeroed.text)
    && /RUNTIME DIAG TSS OBLIGATIONS FAILED 00000000/.test(zeroed.text);
  rec('markers-survive-the-number-being-wrong', true, markersKept);
  rec('a-zeroed-number-is-refused-while-every-marker-stands', true, !verdict.ok && markersKept,
    { failed: verdict.failed.slice(0, 6), markersKept });
}
{
  // The numbers stripped: labels only. A marker judge passes this; this one cannot.
  const trial = byKey.get('paging-smp1#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const stripped = serialText.split(/\r?\n/).map((l) => l.replace(/\s+[0-9A-Fa-f]{8}\s*$/, '')).join('\n');
  const verdict = judgeNonResource('paging-smp1', stripped, e9Text);
  rec('a-channel-without-numbers-is-refused', true, !verdict.ok, { failed: verdict.failed.slice(0, 6),
    obligation_count: verdict.obligation_count });
}
{
  // The two output paths disagree on one number: the serial port says the checksum was A2BEC76E and E9 says zero. One run,
  // two states -- which is a finding no single-channel judge can see.
  const trial = byKey.get('ap-fresh-smp2#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const tamperedE9 = applyMutation(e9Text, 'AP DIAG CHECKSUM', 0x12345678);
  const verdict = judgeNonResource('ap-fresh-smp2', serialText, tamperedE9.text);
  rec('the-two-channels-disagreeing-is-refused', true, !verdict.ok,
    { failed: verdict.failed.slice(0, 6), applied: tamperedE9.applied });
}
{
  // A channel where a per-ticket VALUE was changed but its own EXPECTED was not: the guest's claim about that ticket no
  // longer holds, even though every aggregate counter still agrees.
  const trial = byKey.get('workers-smp2#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const lines = serialText.split('\n');
  let changed = false;
  for (let i = 0; i < lines.length; i++) {
    if (!changed && /^WORKER DIAG VALUE 00001024$/.test(lines[i].trim())) { lines[i] = 'WORKER DIAG VALUE 00001025'; changed = true; }
  }
  const verdict = judgeNonResource('workers-smp2', lines.join('\n'), e9Text);
  rec('a-ticket-value-that-does-not-match-its-expectation-is-refused', true, changed && !verdict.ok,
    { failed: verdict.failed.slice(0, 6), changed });
}

{
  // A refusal trial that printed a boot number did not refuse: it got past B02 and then said it had not. The refusal family
  // has to notice, otherwise "it stopped before the numbers" is a claim nothing checks.
  const trial = byKey.get('kernel-invalid#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const smuggled = serialText.split(/\r?\n/).map((l) => (l === 'BOOTDIAG REJECT invalid-option'
    ? 'CPU DETECTED 00000001\n' + l : l)).join('\n');
  const verdict = judgeNonResource('kernel-invalid', smuggled, e9Text);
  rec('a-refusal-that-printed-a-boot-number-is-refused', true, !verdict.ok,
    { failed: verdict.failed.slice(0, 6), numbers_read: verdict.numbers_read });
}
{
  // A refusal that stops silently: the panic line removed and nothing put in its place. The trial still printed no numbers,
  // so the only thing that can catch it is the obligation that the refusal says why.
  const trial = byKey.get('e99-priority#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const silent = serialText.split(/\r?\n/).filter((l) => !/^PANIC /.test(l)).join('\n');
  const verdict = judgeNonResource('e99-priority', silent, e9Text);
  rec('a-refusal-that-does-not-say-why-is-refused', true, !verdict.ok,
    { failed: verdict.failed.slice(0, 6), silent_lines: silent.split('\n').length });
}
{
  // The reverse error: a scene that DID print the boot numbers must not be handed the refusal family. If it were, the
  // obligations would be vacuous and the boot checks would never run.
  const trial = byKey.get('runtime-init-smp1#1');
  const serialText = read(trial.serial.path);
  const e9Text = read(trial.e9.path);
  const verdict = judgeNonResource('runtime-init-smp1', serialText, e9Text);
  const names = verdict.obligations.map((o) => o.obligation);
  rec('a-scene-that-printed-the-boot-numbers-gets-the-boot-family', true,
    names.includes('boot.at_least_one_cpu_detected') && !names.some((n) => n.startsWith('refused-boot.')),
    { families: [...new Set(names.map((n) => n.split('.')[0]))] });
}

const out = {
  task: 'GTOS-DSH-146',
  stage: 'r-numeric-146-01',
  what: 'the non-resource G/R scenes judged on relations between the numbers the guest printed, on every real trial of every '
    + 'scene -- including the three that used to be left to markers -- with 47 one-number mutations and seven negatives that '
    + 'keep every marker and still fail',
  matrix: { path: R144 + '/g-matrix-gm144-01-144.json', sha256: sha(fs.readFileSync(R144 + '/g-matrix-gm144-01-144.json')) },
  numeric_trials: trials,
  marker_only_trials: markerOnly,
  total_obligations: totalObligations,
  mutations,
  mutation_summary: { total: mutations.length, applied: mutations.filter((m) => m.applied).length,
    caught: mutations.filter((m) => m.caught === true).length,
    missed: mutations.filter((m) => m.applied && m.caught !== true).map((m) => m.name) },
  cases,
  counts: { cases: cases.length, passed: cases.filter((c) => c.ok).length, failed: cases.filter((c) => !c.ok).length },
  ok: cases.every((c) => c.ok),
  ok_means: 'every real trial of every scene satisfies every relation the judge checks, including the two scenes judged as '
    + 'refusals because they never reached the boot numbers; every one of the 47 one-number mutations is caught by the '
    + 'obligation it was aimed at without failing the whole scene; and all seven negatives are refused.',
};
const outPath = R + '/r-numeric-146-01.json';
if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify(out.counts), 'mutations', JSON.stringify(out.mutation_summary), out.ok ? 'OK' : 'FAILED');
for (const c of cases) if (!c.ok) console.log('  FAIL', c.name, JSON.stringify(c).slice(0, 400));
