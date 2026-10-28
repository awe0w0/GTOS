// The numeric judge.
//
// What changed from 146
// ---------------------
// The 146 judge built its checklist out of the log it was given: every family was behind `if (nums.has(...))`, so a channel
// that dropped a family had fewer things to satisfy, and a channel that dropped every family still had the terminal marker
// to satisfy. This judge does the opposite. The obligations come from `scene-contract-147.mjs`, which does not read any
// channel; the channel is then walked against them. A label the contract names and the channel does not carry is a failure,
// not a skipped check, and the failure names the obligation.
//
// What a verdict means
// --------------------
// `ok: true` means: the scene exists in the contract, both channel sides carry every declared observation with the declared
// values and relations, no forbidden line appears, and the last line of both sides is the declared terminal marker. It does
// not mean the image is correct -- that is what the guarded build and the static gate are for -- and it does not mean the
// run was admitted, which is the admission gate's job.
//
// The two sides are judged separately and both must pass. A run that wrote its family to E9 but not to serial produced two
// different logs from one `printf` stream, which means one of them was assembled rather than emitted.

import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import {
  SCENES, CONSTANTS, STACK_STRIDE, stackTop, stackLow,
  workerSeed, workerCount, workerValue,
  COLD_REPEAT_SCENES, REPEATED_SCENES, EXPECTED_SCENE_COUNT, EXPECTED_TRIAL_COUNT, trialsFor,
  UnknownScene, scene as sceneOf, sceneNames, hasScene,
  parseChannel, channelView, CONTRACT_SUMMARY, CONTRACT_VERSION,
} from './scene-contract-147.mjs';

export { CONSTANTS, EXPECTED_SCENE_COUNT, EXPECTED_TRIAL_COUNT, COLD_REPEAT_SCENES, REPEATED_SCENES, trialsFor };

// ---------------------------------------------------------------- one channel against one scene
const judgeChannel = (scene, side, view) => {
  const failures = [];
  const notes = [];
  const obligationResults = [];

  const fail = (obligation, detail) => {
    failures.push({ side, obligation, detail });
    obligationResults.push({ obligation, ok: false, detail });
  };
  const pass = (obligation, detail) => obligationResults.push({ obligation, ok: true, detail });

  if (view.lines.length === 0) {
    return { ok: false, failures: [{ side, obligation: 'channel-non-empty', detail: 'the channel is empty' }],
      obligations: 0, passed: 0 };
  }

  for (const ob of scene.obligations) {
    if (ob.kind === 'number') {
      const all = view.nums(ob.label);
      const want = typeof ob.derived === 'function' ? ob.derived(view) : ob.value;
      if (all.length === 0) {
        fail(`number:${ob.label}`, `the channel does not carry '${ob.label}'`);
        continue;
      }
      if (want === null) { pass(`number:${ob.label}`, `present = ${all[0]}`); continue; }
      // Every occurrence must carry the declared value. A label printed twice with two different numbers is a log that
      // contradicts itself, and taking the first (or the last) would be choosing which of the two to believe.
      const wrong = all.filter((v) => v !== want);
      if (wrong.length) {
        fail(`number:${ob.label}`, `'${ob.label}' is ${wrong.map((v) => v + ' (0x' + v.toString(16) + ')').join(', ')} in `
          + `${wrong.length} of ${all.length} occurrence(s), the contract requires ${want} (0x${want.toString(16)})${ob.why ? ' -- ' + ob.why : ''}`);
        continue;
      }
      pass(`number:${ob.label}`, `${all.length} occurrence(s) = ${want}`);
      continue;
    }
    if (ob.kind === 'line') {
      if (view.lineIndex(ob.text) < 0) { fail(`line:${ob.text}`, `the channel does not carry the line '${ob.text}'`); continue; }
      pass(`line:${ob.text}`, `at ${view.lineIndex(ob.text)}`);
      continue;
    }
    if (ob.kind === 'family') {
      const count = view.lines.filter((l) => l === ob.prefix || l.startsWith(ob.prefix + ' ')).length;
      if (count < ob.min) { fail(`family:${ob.prefix}`, `the channel carries ${count} lines of '${ob.prefix}', the contract requires at least ${ob.min}`); continue; }
      pass(`family:${ob.prefix}`, `${count} lines`);
      continue;
    }
    if (ob.kind === 'relation') {
      let r;
      try { r = ob.fn(view); } catch (e) { r = { ok: false, why: 'relation threw: ' + e.message }; }
      if (!r || !r.ok) { fail(`relation:${ob.id}`, (r && r.why) || 'the relation does not hold'); continue; }
      pass(`relation:${ob.id}`, r.why || 'holds');
      continue;
    }
    fail(`unknown-obligation-kind:${ob.kind}`, 'the contract declared an obligation this judge cannot check');
  }

  // ---------------------------------------------------------------- forbidden lines
  // A forbidden entry is a prefix: `PANIC` refuses `PANIC E99 ...`, and `BOOT B04` refuses every line of that stage. This is
  // checked before the terminal marker so that a run which printed the right marker and then, or also, printed a failure
  // line is a failed run. The scenes declare their own failure lines for exactly this reason.
  for (const bad of scene.forbidden) {
    const hit = view.lines.find((l) => l === bad || l.startsWith(bad + ' ') || l.startsWith(bad + ':'));
    if (hit) fail(`forbidden:${bad}`, `the channel carries '${hit}'`);
  }

  // ---------------------------------------------------------------- the terminal marker is last
  const last = view.lines[view.lines.length - 1];
  if (last !== scene.terminal) {
    fail('terminal', `the last line is '${last}', the contract requires '${scene.terminal}'`);
  } else {
    pass('terminal', 'last line');
  }

  // ---------------------------------------------------------------- the scene's machine shape
  const detected = view.num('CPU DETECTED');
  if (detected !== null && detected !== scene.vcpus) {
    fail('vcpus', `the scene declares ${scene.vcpus} vCPU(s) and the channel reports CPU DETECTED ${detected}`);
  }

  return { ok: failures.length === 0, failures, obligations: obligationResults.length,
    passed: obligationResults.filter((o) => o.ok).length };
};

// ---------------------------------------------------------------- one scene, both sides
export const judgeScene = (name, channels, opts = {}) => {
  const trial = opts.trial === undefined ? null : opts.trial;
  let scene;
  try {
    scene = sceneOf(name);
  } catch (e) {
    if (e instanceof UnknownScene) {
      return { ok: false, scene: name, trial, refused: 'unknown-scene',
        failures: [{ side: null, obligation: 'scene-exists', detail: `'${name}' is not a permitted scene; the contract declares ${sceneNames().length}` }],
        obligations: 0, passed: 0 };
    }
    throw e;
  }
  const sides = {};
  for (const side of ['serial', 'e9']) {
    const text = channels && channels[side];
    if (typeof text !== 'string' || text.length === 0) {
      sides[side] = { ok: false, failures: [{ side, obligation: 'channel-present', detail: `the ${side} channel is missing or empty` }], obligations: 0, passed: 0 };
      continue;
    }
    sides[side] = judgeChannel(scene, side, channelView(parseChannel(text)));
  }
  const failures = [...sides.serial.failures, ...sides.e9.failures];
  return {
    ok: sides.serial.ok && sides.e9.ok,
    scene: name, trial, vcpus: scene.vcpus, stop: scene.stop,
    obligations: scene.obligations.length,
    serial: { ok: sides.serial.ok, obligations: sides.serial.obligations, passed: sides.serial.passed },
    e9: { ok: sides.e9.ok, obligations: sides.e9.obligations, passed: sides.e9.passed },
    failures,
  };
};

// ---------------------------------------------------------------- the matrix
//
// A matrix is the set of trials, and the set is part of the claim: twenty scenes, the six named cold repeats run twice, so
// twenty-eight trials. A matrix that covers nineteen scenes is not a smaller success, it is a different claim, and the
// pre-147 judge accepted it because it iterated the trials it was handed.
export const judgeMatrix = (matrix, opts = {}) => {
  const problems = [];
  const trials = Array.isArray(matrix && matrix.trials) ? matrix.trials : [];
  if (!trials.length) problems.push({ id: 'matrix-non-empty', detail: 'the matrix carries no trials' });

  const seen = new Map();
  const verdicts = [];
  for (const t of trials) {
    const key = `${t.scene}-t${t.trial}`;
    if (seen.has(key)) problems.push({ id: 'trial-unique', detail: `${key} appears more than once` });
    seen.set(key, t);
    if (!hasScene(t.scene)) { problems.push({ id: 'trial-scene-permitted', detail: `${key} names a scene the contract does not declare` }); continue; }
    const want = trialsFor(t.scene);
    if (t.trial < 1 || t.trial > want) problems.push({ id: 'trial-index', detail: `${key} is outside 1..${want}` });
    if (typeof t.serial_text !== 'string' || typeof t.e9_text !== 'string') {
      problems.push({ id: 'trial-channels', detail: `${key} does not carry both channel texts` });
      continue;
    }
    const v = judgeScene(t.scene, { serial: t.serial_text, e9: t.e9_text }, { trial: t.trial });
    verdicts.push(v);
    if (!v.ok) problems.push({ id: 'trial-judged', detail: `${key}: ${v.failures.slice(0, 3).map((f) => f.obligation + ' -- ' + f.detail).join(' | ')}` });
  }

  // every declared scene must be present, with exactly its declared trial count
  for (const s of SCENES) {
    const want = trialsFor(s.name);
    for (let i = 1; i <= want; i += 1) {
      if (!seen.has(`${s.name}-t${i}`)) problems.push({ id: 'trial-present', detail: `${s.name}-t${i} is missing from the matrix` });
    }
  }
  if (SCENES.length !== EXPECTED_SCENE_COUNT) problems.push({ id: 'scene-count', detail: `the contract declares ${SCENES.length} scenes, the taskbook fixes ${EXPECTED_SCENE_COUNT}` });
  if (trials.length !== EXPECTED_TRIAL_COUNT) problems.push({ id: 'trial-count', detail: `the matrix carries ${trials.length} trials, the taskbook fixes ${EXPECTED_TRIAL_COUNT}` });

  // The cold repeats. The taskbook names six scenes that must be started twice, and each of those six must appear as a
  // second trial. The comparison is one-directional on purpose: a scene the taskbook names and the matrix ran once is the
  // defect this check exists for, while a scene the packet chose to start twice as well is additional evidence rather than
  // a violation. The extras are recorded by name so a reader sees exactly what was run beyond the requirement, and the
  // count of the taskbook's own six is checked separately rather than being inferred from the total.
  const repeats = trials.filter((t) => t.trial === 2).map((t) => t.scene).sort();
  const wantRepeats = [...COLD_REPEAT_SCENES].sort();
  const missingRepeats = wantRepeats.filter((n) => !repeats.includes(n));
  if (missingRepeats.length) {
    problems.push({ id: 'cold-repeats', detail: `the taskbook names ${wantRepeats.join(',')} as repeats; the matrix repeats ${repeats.join(',')}, missing ${missingRepeats.join(',')}` });
  }
  if (wantRepeats.length !== 6) problems.push({ id: 'cold-repeat-count', detail: `the contract names ${wantRepeats.length} repeats, the taskbook fixes 6` });
  const extraRepeats = repeats.filter((n) => !wantRepeats.includes(n));

  return {
    ok: problems.length === 0,
    contract_version: CONTRACT_VERSION,
    scenes: SCENES.length, trials: trials.length, expected_trials: EXPECTED_TRIAL_COUNT,
    judged: verdicts.length,
    passed: verdicts.filter((v) => v.ok).length,
    repeats: { required: wantRepeats, observed: repeats, extra: extraRepeats, missing: missingRepeats },
    problems,
    verdicts,
  };
};

// ---------------------------------------------------------------- the negatives
//
// Each entry is a defect the judge must refuse. They are the defects 146 shipped: the family that went missing and took its
// own obligations with it, the marker-only log, the wrong scene's log, the wrong refusal reason, the one-sided channel, the
// omitted trial, and the two the taskbook names as missing witnesses -- a self-consistent VALUE/EXPECTED pair and an ESP0
// that is nonzero and aligned but belongs to no stack.
const dropFamily = (text, prefix) => text.split(/\r?\n/).filter((l) => !(l === prefix || l.startsWith(prefix + ' '))).join('\n');

export const MUTATIONS = [
  {
    id: 'unknown-scene', why: 'a scene the contract does not declare must refuse, not fall back to whatever the caller built',
    apply: ({ channels }) => ({ name: 'runtime-init-smp9', channels }),
  },
  {
    id: 'whole-family-missing', why: 'deleting the whole numeric family must fail as missing evidence, not shrink the checklist',
    apply: ({ channels, sceneName }) => {
      const prefix = { 'runtime-init-smp1': 'RUNTIME DIAG', 'runtime-objects-smp1': 'OBJECT DIAG',
        'runtime-recovery-smp1': 'RECOVERY DIAG', 'runtime-frame-recovery-smp1': 'FRAMEREC DIAG',
        'workers-smp2': 'WORKER DIAG', 'workers-smp3': 'WORKER DIAG', 'paging-smp1': 'PAGING DIAG',
        'ap-fresh-smp2': 'AP DIAG' }[sceneName];
      return { name: sceneName, channels: { serial: dropFamily(channels.serial, prefix), e9: dropFamily(channels.e9, prefix) } };
    },
  },
  {
    id: 'marker-only', why: 'a channel that is only the terminal marker carries no observation at all',
    apply: ({ channels, sceneName }) => {
      const marker = sceneOf(sceneName).terminal;
      return { name: sceneName, channels: { serial: marker + '\n', e9: marker + '\n' } };
    },
  },
  {
    id: 'wrong-scene-log', why: "another scene's channel is evidence for that scene, not this one",
    apply: ({ channels, sceneName, other }) => ({ name: sceneName, channels: { serial: other.serial, e9: other.e9 } }),
  },
  {
    id: 'wrong-refusal-reason', why: 'the E99 precedence and the parser rejection are different reasons and are not interchangeable',
    apply: ({ channels }) => ({
      name: 'e99-priority',
      channels: {
        serial: channels.serial.replace(/^PANIC E99 .*$/m, 'BOOTDIAG REJECT invalid-option'),
        e9: channels.e9.replace(/^PANIC E99 .*$/m, 'BOOTDIAG REJECT invalid-option'),
      },
    }),
  },
  {
    id: 'serial-only', why: 'one side of a two-sided channel means the log was assembled, not emitted',
    apply: ({ channels, sceneName }) => ({ name: sceneName, channels: { serial: channels.serial, e9: '' } }),
  },
  {
    id: 'e9-only', why: 'the same defect on the other side',
    apply: ({ channels, sceneName }) => ({ name: sceneName, channels: { serial: '', e9: channels.e9 } }),
  },
  {
    id: 'truncated', why: 'a log cut before its terminal line did not finish the scene',
    apply: ({ channels, sceneName }) => {
      const cut = (t) => t.split(/\r?\n/).slice(0, Math.max(4, Math.floor(t.split(/\r?\n/).length * 0.6))).join('\n');
      return { name: sceneName, channels: { serial: cut(channels.serial), e9: cut(channels.e9) } };
    },
  },
  {
    id: 'value-expected-fraud', why: 'a guest that prints VALUE and EXPECTED from the same number passes a self-comparison; the '
      + 'judge must derive the value from the fixed input instead',
    apply: ({ channels, sceneName }) => {
      if (!/^workers-smp/.test(sceneName)) return null;
      const bump = (t) => t.split(/\r?\n/).map((l) => {
        const m = l.match(/^WORKER DIAG VALUE ([0-9A-F]{8})$/);
        return m ? 'WORKER DIAG VALUE ' + ((parseInt(m[1], 16) + 1) & 0xffffffff).toString(16).toUpperCase().padStart(8, '0') : l;
      }).join('\n');
      // VALUE moved and EXPECTED moved with it: the guest is still self-consistent, so only an independent derivation
      // can see the defect.
      const both = (t) => bump(t).split(/\r?\n/).map((l) => {
        const m = l.match(/^WORKER DIAG EXPECTED ([0-9A-F]{8})$/);
        return m ? 'WORKER DIAG EXPECTED ' + ((parseInt(m[1], 16) + 1) & 0xffffffff).toString(16).toUpperCase().padStart(8, '0') : l;
      }).join('\n');
      return { name: sceneName, channels: { serial: both(channels.serial), e9: both(channels.e9) } };
    },
  },
  {
    id: 'esp0-aligned-but-not-a-stack', why: 'the 146 obligation was "nonzero and 16-byte aligned", which any number satisfies; the '
      + 'guest agreeing with itself about which slot owns it must not save it',
    apply: ({ channels }) => {
      const rewrite = (t) => t.split(/\r?\n/).map((l) => {
        if (/^RUNTIME DIAG TSS ESP0 [0-9A-F]{8}$/.test(l)) return 'RUNTIME DIAG TSS ESP0 0D000100';
        if (/^RUNTIME DIAG ESP0 OWNER TOP [0-9A-F]{8}$/.test(l)) return 'RUNTIME DIAG ESP0 OWNER TOP 0D000100';
        if (/^RUNTIME DIAG ESP0 OWNER LOW [0-9A-F]{8}$/.test(l)) return 'RUNTIME DIAG ESP0 OWNER LOW 0D000000';
        return l;
      }).join('\n');
      return { name: 'runtime-init-smp1', channels: { serial: rewrite(channels.serial), e9: rewrite(channels.e9) } };
    },
  },
  {
    id: 'survivor-data-moved', why: 'a neighbour whose bytes changed after the recovery was disturbed, whatever the count says',
    apply: ({ channels }) => {
      let first = true;
      const rewrite = (t) => t.split(/\r?\n/).map((l) => {
        const m = l.match(/^RECOVERY DIAG SURVIVOR DATA ([0-9A-F]{8})$/);
        if (m && first) { first = false; return 'RECOVERY DIAG SURVIVOR DATA DEADBEEF'; }
        return l;
      }).join('\n');
      return { name: 'runtime-recovery-smp1', channels: { serial: rewrite(channels.serial), e9: rewrite(channels.e9) } };
    },
  },
  {
    id: 'sibling-data-moved', why: 'the live neighbour is the witness for the frame scene; moving its bytes must fail',
    apply: ({ channels }) => {
      const rewrite = (t) => t.split(/\r?\n/).map((l) => /^FRAMEREC DIAG SIBLING DATA [0-9A-F]{8}$/.test(l)
        ? 'FRAMEREC DIAG SIBLING DATA CAFEBABE' : l).join('\n');
      return { name: 'runtime-frame-recovery-smp1', channels: { serial: rewrite(channels.serial), e9: rewrite(channels.e9) } };
    },
  },
  {
    id: 'family-swapped', why: 'the capacity refusal and the allocator refusal are different refusals and neither log may be read as the other',
    apply: ({ channels, other }) => ({ name: 'runtime-frame-recovery-smp1', channels: { serial: other.serial, e9: other.e9 } }),
  },
  {
    id: 'vcpu-count-wrong', why: 'two cores and three cores are different machines, and a run that detected two cannot stand in for three',
    apply: ({ channels, sceneName }) => {
      const rewrite = (t) => t.split(/\r?\n/).map((l) => {
        if (/^CPU DETECTED [0-9A-F]{8}$/.test(l)) return 'CPU DETECTED 00000002';
        if (/ DIAG DETECTED [0-9A-F]{8}$/.test(l)) return l.replace(/[0-9A-F]{8}$/, '00000002');
        return l;
      }).join('\n');
      return { name: sceneName, channels: { serial: rewrite(channels.serial), e9: rewrite(channels.e9) } };
    },
  },
  {
    id: 'duplicate-label', why: 'a label that appears twice is ambiguous; taking either value would let a log carry two answers',
    apply: ({ channels, sceneName }) => {
      const dup = (t) => {
        const lines = t.split(/\r?\n/);
        const i = lines.findIndex((l) => /^CPU DETECTED [0-9A-F]{8}$/.test(l));
        if (i < 0) return t;
        lines.splice(i + 1, 0, 'CPU DETECTED 00000009');
        return lines.join('\n');
      };
      return { name: sceneName, channels: { serial: dup(channels.serial), e9: dup(channels.e9) } };
    },
  },
];

// The matrix negatives: the set itself is part of the claim.
export const MATRIX_MUTATIONS = [
  { id: 'omitted-trial', why: 'nineteen scenes is not a smaller success, it is a different claim',
    apply: (matrix) => ({ ...matrix, trials: matrix.trials.filter((t) => !(t.scene === 'workers-smp3' && t.trial === 2)) }) },
  { id: 'omitted-scene', why: 'dropping every trial of one scene must fail even though every remaining trial passes',
    apply: (matrix) => ({ ...matrix, trials: matrix.trials.filter((t) => t.scene !== 'paging-smp1') }) },
  { id: 'extra-trial', why: 'a third trial of a scene that declares two is not the declared matrix',
    apply: (matrix) => ({ ...matrix, trials: [...matrix.trials, { ...matrix.trials[0], trial: 3 }] }) },
  { id: 'cold-repeat-moved', why: 'the repeats must be the scenes the taskbook names, not whichever two ran twice',
    apply: (matrix) => ({ ...matrix, trials: matrix.trials.map((t) => (t.scene === 'workers-smp3' && t.trial === 2
      ? { ...t, scene: 'tables-smp1', trial: 2 } : t)) }) },
  { id: 'one-failing-trial', why: 'a single failing trial fails the matrix',
    apply: (matrix) => ({ ...matrix, trials: matrix.trials.map((t, i) => (i === 3 ? { ...t, serial_text: t.serial_text.replace(/\n[^\n]*\n?$/, '\n') } : t)) }) },
];

export const judgeMatrixMutations = (matrix, channelsFor) => {
  const out = [];
  for (const m of MATRIX_MUTATIONS) {
    const mutated = m.apply(matrix);
    const v = judgeMatrix(mutated, {});
    out.push({ id: m.id, refused: !v.ok, why: m.why, problems: v.problems.slice(0, 3).map((p) => p.id + ' -- ' + p.detail) });
  }
  return out;
};

// ---------------------------------------------------------------- fixtures for the judge's own test
//
// A fixture is *not* evidence. It is a channel that carries exactly what the contract declares, built from a real captured
// channel by adding the witness lines the 147 kernel prints. It exists so that the judge can be tested for both answers --
// accept a conforming channel, refuse each defect -- before any 147 image exists to produce a real one. The real run then
// has to produce the same lines from the kernel, which is a separate claim and is checked by the run itself.
const hex8 = (n) => (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const fnv = (bytes) => {
  let d = 2166136261 >>> 0;
  for (const b of bytes) { d = (d ^ b) >>> 0; d = Math.imul(d, 16777619) >>> 0; }
  return d >>> 0;
};

const insertAfter = (text, anchor, extra) => {
  const lines = text.split(/\r?\n/);
  const out = [];
  let done = false;
  for (const l of lines) {
    out.push(l);
    if (!done && l === anchor) { out.push(...extra); done = true; }
  }
  if (!done) throw new Error('fixture anchor not found: ' + anchor);
  return out.join('\n');
};
const replaceLine = (text, re, fn) => text.split(/\r?\n/).map((l) => (re.test(l) ? fn(l) : l)).join('\n');

// What the 147 kernel adds, per scene, expressed from the contract's own constants. This is the only place a fixture invents
// a value, and every invented value is derived from a declared constant rather than from a captured log.
const WITNESS = {
  'runtime-init-smp1': (text) => insertAfter(text, 'RUNTIME DIAG TSS OBSERVED', [
    'RUNTIME DIAG STACK ARENA ' + hex8(CONSTANTS.kernel_stack_arena),
    'RUNTIME DIAG STACK PAGES ' + hex8(CONSTANTS.kernel_stack_pages),
    'RUNTIME DIAG STACK STRIDE ' + hex8(STACK_STRIDE),
    'RUNTIME DIAG STACK SLOTS ' + hex8(CONSTANTS.maximum_processes),
    'RUNTIME DIAG ESP0 OWNER 00000000',
    'RUNTIME DIAG ESP0 OWNER TOP ' + hex8(stackTop(0)),
    'RUNTIME DIAG ESP0 OWNER LOW ' + hex8(stackLow(0)),
    'RUNTIME DIAG ESP0 IN OWNER SPAN',
    'RUNTIME DIAG ESP0 OWNER IS ACTIVATED SLOT',
  ]),
  'runtime-objects-smp1': (text) => insertAfter(text, 'OBJECT DIAG DATA DIFFERS', [
    'OBJECT DIAG DATA A DIGEST 1A2B3C4D',
    'OBJECT DIAG DATA B DIGEST 5E6F7A8B',
  ]),
  'runtime-recovery-smp1': (text) => {
    let n = 0;
    let out = replaceLine(text, /^RECOVERY DIAG FILLED [0-9A-F]{8}$/, (l) => {
      const i = n; n += 1;
      const dir = 0x00a50000 + i * 0x1000;
      const data = fnv(Array.from({ length: 16 }, (_, k) => 0x10 * (i + 1) + k));
      return [l, 'RECOVERY DIAG FILLED ID ' + hex8(i + 1), 'RECOVERY DIAG FILLED SLOT ' + hex8(i),
        'RECOVERY DIAG FILLED DIR ' + hex8(dir), 'RECOVERY DIAG FILLED DATA ' + hex8(data),
        'RECOVERY DIAG FILLED DATA READ'].join('\n');
    });
    const survivors = [0, 2, 3];
    out = replaceLine(out, /^RECOVERY DIAG SURVIVORS [0-9A-F]{8}$/, (l) => {
      const blocks = survivors.map((i) => {
        const dir = 0x00a50000 + i * 0x1000;
        const data = fnv(Array.from({ length: 16 }, (_, j) => 0x10 * (i + 1) + j));
        return ['RECOVERY DIAG SURVIVOR ID ' + hex8(i + 1), 'RECOVERY DIAG SURVIVOR SLOT ' + hex8(i),
          'RECOVERY DIAG SURVIVOR DIR ' + hex8(dir), 'RECOVERY DIAG SURVIVOR DATA ' + hex8(data),
          'RECOVERY DIAG SURVIVOR READ'];
      });
      // The count line comes first, then one block per survivor, which is the order the kernel prints them in.
      return [l, ...blocks.flat()].join('\n');
    });
    out = insertAfter(out, 'RECOVERY DIAG SURVIVORS LIVE', ['RECOVERY DIAG SURVIVORS DISTINCT']);
    return out;
  },
  'runtime-frame-recovery-smp1': (text) => {
    const sibData = fnv(Array.from({ length: 16 }, (_, i) => 0x50 + i));
    const recData = fnv(Array.from({ length: 16 }, (_, i) => 0x60 + i));
    let out = insertAfter(text, 'FRAMEREC DIAG SIBLING SLOT 00000000',
      ['FRAMEREC DIAG SIBLING DATA AT CREATE ' + hex8(sibData)]);
    out = insertAfter(out, 'FRAMEREC DIAG RECOVERED DIRECTORY 00A68000', [
      'FRAMEREC DIAG SIBLING DATA ' + hex8(sibData),
      'FRAMEREC DIAG RECOVERED DATA ' + hex8(recData),
      'FRAMEREC DIAG NEIGHBOUR DATA DISTINCT',
    ]);
    return out;
  },
  'workers-smp2': (text) => workersWitness(text),
  'workers-smp3': (text) => workersWitness(text),
};

// The worker ticket block, rebuilt in the order the 147 kernel prints it. The 144 channel carries PASS/TICKET/SLOT/APIC/
// VALUE/EXPECTED; the 147 kernel adds WORKER, JOB, SEED and COUNT between PASS and TICKET. Those four are derived from the
// ticket's own pass, worker and slot, so the rebuild is a re-emission rather than an invention -- but it is still a fixture,
// and the real kernel has to print them itself.
const workersWitness = (text) => {
  const lines = text.split(/\r?\n/);
  const out = [];
  let cur = null;
  const flush = () => {
    if (!cur) return;
    const worker = cur.APIC - 1;
    out.push('WORKER DIAG PASS ' + hex8(cur.PASS));
    out.push('WORKER DIAG WORKER ' + hex8(worker));
    out.push('WORKER DIAG JOB ' + hex8(cur.SLOT));
    out.push('WORKER DIAG SEED ' + hex8(workerSeed(cur.PASS, worker, cur.SLOT)));
    out.push('WORKER DIAG COUNT ' + hex8(workerCount(cur.SLOT)));
    out.push('WORKER DIAG TICKET ' + hex8(cur.TICKET));
    out.push('WORKER DIAG SLOT ' + hex8(cur.SLOT));
    out.push('WORKER DIAG APIC ' + hex8(cur.APIC));
    out.push('WORKER DIAG VALUE ' + hex8(cur.VALUE));
    out.push('WORKER DIAG EXPECTED ' + hex8(cur.EXPECTED));
    cur = null;
  };
  for (const l of lines) {
    const m = l.match(/^WORKER DIAG (PASS|TICKET|SLOT|APIC|VALUE|EXPECTED) ([0-9A-F]{8})$/);
    if (!m) { flush(); out.push(l); continue; }
    if (m[1] === 'PASS') { flush(); cur = { PASS: parseInt(m[2], 16) }; continue; }
    if (!cur) { out.push(l); continue; }
    cur[m[1]] = parseInt(m[2], 16);
  }
  flush();
  return out.join('\n');
};

// The fixture builder: real captured channel in, contract-conforming channel out. A scene the 147 kernel did not change has
// no builder, and its captured channel is already the fixture.
export const buildFixture = (sceneName, channels) => {
  const w = WITNESS[sceneName] || ((t) => t);
  return { serial: w(channels.serial), e9: w(channels.e9) };
};
export const WITNESS_SCENES = Object.keys(WITNESS);

// ---------------------------------------------------------------- the judge's own test
//
// Two claims, both checked against the real judge:
//   1. a channel that carries the contract is accepted, with every obligation satisfied;
//   2. every defect in MUTATIONS and MATRIX_MUTATIONS is refused.
// The test is only as good as the fixtures, so it also asserts that each fixture satisfies strictly more obligations than
// the raw captured channel it was built from -- i.e. that the witness lines are what the acceptance depends on.
export const selftest = (corpus) => {
  const results = [];
  const fixtures = {};
  // The trial count is a property of the contract, and it is asserted here rather than derived at the point of use, so a
  // change to the repeat list shows up as a failed test instead of as a quietly smaller matrix.
  results.push({ id: 'contract:scene-count', ok: SCENES.length === EXPECTED_SCENE_COUNT,
    detail: `${SCENES.length} scenes declared, ${EXPECTED_SCENE_COUNT} fixed` });
  results.push({ id: 'contract:trial-count', ok: EXPECTED_TRIAL_COUNT === 28,
    detail: `${EXPECTED_TRIAL_COUNT} trials: ${SCENES.length} scenes, ${REPEATED_SCENES.length} of them repeated` });
  results.push({ id: 'contract:cold-repeats', ok: COLD_REPEAT_SCENES.length === 6, detail: `${COLD_REPEAT_SCENES.length} named repeats` });
  for (const s of SCENES) {
    if (!corpus[s.name]) continue;
    try { fixtures[s.name] = buildFixture(s.name, corpus[s.name]); } catch (e) { results.push({ id: 'fixture:' + s.name, ok: false, detail: e.message }); }
  }
  // 1. acceptance
  for (const [name, ch] of Object.entries(fixtures)) {
    const v = judgeScene(name, ch);
    results.push({ id: 'accept:' + name, ok: v.ok,
      detail: v.ok ? `${v.obligations} obligations, both sides` : v.failures.slice(0, 4).map((f) => f.obligation + ' -- ' + f.detail).join(' | ') });
  }
  // 2. the witness lines are load-bearing
  //    Only for the scenes the 147 kernel actually extended. For the others the captured channel is already the fixture, and
  //    the claim "the fixture proves something" would be false, so it is not made.
  for (const name of WITNESS_SCENES) {
    const ch = fixtures[name];
    if (!ch) continue;
    const raw = judgeScene(name, corpus[name]);
    results.push({ id: 'witness-is-load-bearing:' + name, ok: !raw.ok,
      detail: raw.ok ? 'the raw captured channel already passed; the fixture proves nothing' : `raw channel refused (${raw.failures.length} failures)` });
  }
  // 3. every defect is refused
  for (const r of mutationResults(fixtures)) results.push(r);
  return { ok: results.every((r) => r.ok), results };
};

// Every defect is refused.
//
// Each mutation names the scene it is a defect *of*. A mutation applied to a scene that cannot exhibit it is a no-op and would
// report a vacuous refusal, so the mapping is explicit and a missing entry is a failure rather than a fallback to whichever
// scene happened to come first.
//
// The base channels are a parameter because the same fifteen defects are aimed at two different corpora and both claims are
// worth making: the fixture corpus of the earlier packet, where the witness lines are what the acceptance depends on, and this
// packet's own real channels, where the numbers were produced by this image. A refusal over a fixture shows the judge can
// reject a doctored file; a refusal over the real channel shows it rejects a doctoring of the bytes this packet actually
// captured.
export const MUTATION_SCENE = {
  'unknown-scene': 'runtime-init-smp1',
  'whole-family-missing': 'runtime-recovery-smp1',
  'marker-only': 'workers-smp2',
  'wrong-scene-log': 'runtime-objects-smp1',
  'wrong-refusal-reason': 'e99-priority',
  'serial-only': 'paging-smp1',
  'e9-only': 'paging-smp1',
  'truncated': 'runtime-frame-recovery-smp1',
  'value-expected-fraud': 'workers-smp2',
  'esp0-aligned-but-not-a-stack': 'runtime-init-smp1',
  'survivor-data-moved': 'runtime-recovery-smp1',
  'sibling-data-moved': 'runtime-frame-recovery-smp1',
  'family-swapped': 'runtime-frame-recovery-smp1',
  'vcpu-count-wrong': 'workers-smp3',
  'duplicate-label': 'runtime-init-smp1',
};

export const mutationResults = (base) => {
  const results = [];
  for (const m of MUTATIONS) {
    const sceneName = MUTATION_SCENE[m.id];
    if (!sceneName) { results.push({ id: 'refuse:' + m.id, ok: false, detail: 'the mutation names no scene' }); continue; }
    const channels = base[sceneName];
    const otherName = Object.keys(base).find((n) => n !== sceneName);
    if (!channels) { results.push({ id: 'refuse:' + m.id, ok: false, detail: 'no channel for ' + sceneName }); continue; }
    const mutated = m.apply({ name: sceneName, sceneName, channels, other: base[otherName] });
    if (mutated === null) { results.push({ id: 'refuse:' + m.id, ok: false, detail: 'the mutation refused to apply to ' + sceneName }); continue; }
    const v = judgeScene(mutated.name, mutated.channels);
    results.push({ id: 'refuse:' + m.id, ok: !v.ok, scene: sceneName, targets: m.targets,
      detail: v.ok ? 'the judge accepted the defect' : (v.refused || v.failures.slice(0, 2).map((f) => f.obligation).join(', ')) });
  }
  return results;
};

// ---------------------------------------------------------------- CLI
const SELF_PATH = fileURLToPath(import.meta.url);
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH);
if (isMain) {
  const mode = process.argv[2];
  const arg = process.argv[3];
  const readCorpus = (matrixPath) => {
    const m = JSON.parse(fs.readFileSync(matrixPath, 'utf8'));
    const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
    const corpus = {};
    for (const t of m.trials) {
      if (corpus[t.scene]) continue;
      corpus[t.scene] = { serial: fs.readFileSync(toHost(t.serial.path), 'utf8'), e9: fs.readFileSync(toHost(t.e9.path), 'utf8') };
    }
    return corpus;
  };
  if (mode === 'summary') {
    console.log(JSON.stringify(CONTRACT_SUMMARY, null, 2));
    process.exit(0);
  }
  if (mode === 'selftest') {
    const r = selftest(readCorpus(arg));
    for (const x of r.results) console.log((x.ok ? 'ok   ' : 'FAIL ') + x.id + (x.ok ? '' : '  <- ' + x.detail));
    console.log('\n' + (r.ok ? 'selftest: every claim holds' : 'selftest: FAILED'));
    process.exit(r.ok ? 0 : 1);
  }
  if (mode === 'judge') {
    const m = JSON.parse(fs.readFileSync(arg, 'utf8'));
    const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
    const trials = m.trials.map((t) => ({ ...t, serial_text: fs.readFileSync(toHost(t.serial.path), 'utf8'),
      e9_text: fs.readFileSync(toHost(t.e9.path), 'utf8') }));
    const v = judgeMatrix({ ...m, trials });
    console.log(JSON.stringify({ ok: v.ok, scenes: v.scenes, trials: v.trials, judged: v.judged, passed: v.passed,
      problems: v.problems.slice(0, 40) }, null, 2));
    process.exit(v.ok ? 0 : 1);
  }
  console.error('usage: n-numeric-147.mjs summary | selftest <matrix.json> | judge <matrix.json>');
  process.exit(2);
}
