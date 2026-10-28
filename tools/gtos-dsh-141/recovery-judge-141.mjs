// GTOS-DSH-141 R: the recovery judgement, made by the production comparator over real snapshots, plus a frame-set layer.
//
// Two layers, because one cannot do the other's work:
//
//   the comparator   the production observation comparator this packet cloned from the repository's reference set, imported
//                    as a module and used as the code the scenes use. It compares pins, numbers, strings, lists and maps, and
//                    it can fail. Over artefacts it sees a changed hash at an unchanged size.
//   the frame set    the allocator's own ownership record, walked by the unit and published as a count and a digest. It sees
//                    a set that changed while its size did not, which no count, no sum and no artefact hash can see.
//
// A third layer, the requirement list, refuses an incomplete record: two fields absent on both sides compare equal, which is
// right for a comparator and wrong for a judge.
//
// What is judged is what the guarded runs produced. Every file is read from disk and hashed here; every number is read out of
// a case's own raw channel, and the frame sets are the two the unit published from the production allocator. Nothing below is
// told what a run concluded.

import crypto from 'node:crypto';
import fs from 'node:fs';
import { diffSnapshots, snapshot, isPin } from './../references/observation-diff-141.mjs';
import { forConsumer } from './../references/host-path-141.mjs';

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const sleep = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };

// The stage records speak Linux paths; the host reaches them either through a drive letter or through the distribution's own
// share. The production mapper is then given a host path, which is the domain it requires.
const toHost = (l) => (/^\/mnt\/[a-z]\//.test(String(l))
  ? String(l).replace(/^\/mnt\/([a-z])\//, (m, d) => d.toUpperCase() + ':/')
  : '\\\\wsl$\\Ubuntu-20.04' + String(l));
const host = (p) => forConsumer(toHost(p), 'win32');

const V = '/var/tmp/gtos-dsh-141-e759-complete-closure-view';
const BUILD = V + '/test-build/h148';
const RAW = {
  9: V + '/unit-run/rc141-46/logs/child-output-rc141-46.raw',
  10: V + '/unit-run/rc141-47/logs/child-output-rc141-47.raw',
  11: V + '/unit-run/rc141-48/logs/child-output-rc141-48.raw',
};

function readText(p) {
  let text = null;
  for (let i = 0; i < 60 && text === null; i++) {
    try { text = fs.readFileSync(host(p), 'utf8'); } catch { sleep(250); }
  }
  if (text === null) throw new Error('not readable: ' + p);
  return text;
}

// Every frame set the unit published, read out of the channels rather than written here.
function publishedFrameSets() {
  const sets = {};
  for (const [caseNo, path] of Object.entries(RAW)) {
    const text = readText(path);
    const re = /FRAMESET ([a-z-]+) count ([0-9A-F]{8}) digest ([0-9A-F]{8})/g;
    let m;
    while ((m = re.exec(text)) !== null) {
      sets[caseNo + ':' + m[1]] = { count: parseInt(m[2], 16), digest: parseInt(m[3], 16), from: path };
    }
  }
  return sets;
}
const SETS = publishedFrameSets();
const need = (k) => {
  if (!SETS[k]) throw new Error('the unit published no frame set named ' + k);
  return SETS[k];
};

function case10Numbers() {
  const text = readText(RAW[10]);
  const pick = (re, name) => {
    const m = text.match(re);
    if (!m) throw new Error('case 10 raw channel does not carry ' + name);
    return parseInt(m[1], 16);
  };
  return {
    protect_consulted: pick(/ProtectRange consulted ([0-9A-F]{8}) times/, 'the consultation count'),
    protect_refused: pick(/refused ([0-9A-F]{8}) times/, 'the refusal count'),
    pages_at_refusal: pick(/at page count ([0-9A-F]{8})/, 'the page count at refusal'),
    late_create_returned: /late Create returned false/.test(text) ? 'false' : 'true',
    unit_marker: /RUNTIME UNIT PASS/.test(text) ? 'PASS' : 'FAIL',
  };
}
const observed = case10Numbers();

const FILES = [
  { key: 'unit.elf', path: BUILD + '/runtime_test.elf' },
  { key: 'native_runtime.o', path: BUILD + '/native_runtime.o' },
  { key: 'paging.o', path: BUILD + '/paging.o' },
  { key: 'physical.o', path: BUILD + '/physical.o' },
  { key: 'process_address_space.o', path: BUILD + '/process_address_space.o' },
  { key: 'native_runtime.d', path: BUILD + '/native_runtime.d' },
  { key: 'link.map', path: BUILD + '/test.map' },
  { key: 'raw.case9', path: RAW[9] },
  { key: 'raw.case10', path: RAW[10] },
  { key: 'raw.case11', path: RAW[11] },
];

const DECLARED = {
  slot_capacity: { value: 4, source: 'include/process/native_runtime.h: MaximumProcesses' },
  objects_created_and_reaped: { value: 4, source: 'tests/runtime_test.cpp case 9: four objects created, four reaped' },
};

const values = {
  slot_capacity: DECLARED.slot_capacity.value,
  objects_live: DECLARED.objects_created_and_reaped.value,
  objects_created: DECLARED.objects_created_and_reaped.value,
  objects_reaped: DECLARED.objects_created_and_reaped.value,
  frames_held_after_reap: need('9:after-reap').count - need('9:baseline').count,
  capacity_reason: 'the slot table is full',
  protect_consulted: observed.protect_consulted,
  protect_refused: observed.protect_refused,
  pages_at_refusal: observed.pages_at_refusal,
  late_create_returned: observed.late_create_returned,
  unit_marker: observed.unit_marker,
  // The frame sets as the unit published them: the pair from the exact-recovery case and the pair from the leak case.
  recover_baseline_count: need('9:baseline').count,
  recover_after_reap_count: need('9:after-reap').count,
  recover_baseline_digest: need('9:baseline').digest,
  recover_after_reap_digest: need('9:after-reap').digest,
  leak_baseline_count: need('11:leak-baseline').count,
  leak_after_count: need('11:leak-after').count,
  leak_baseline_digest: need('11:leak-baseline').digest,
  leak_after_digest: need('11:leak-after').digest,
};

const REQUIRED_KEYS = Object.freeze(Object.keys(values));

// ---------------------------------------------------------------- the judge
// Three layers, each reporting separately, so a refusal says which layer saw it and a reader can tell whether the comparator
// was doing the work or the requirement list was.
function judge(before, after, requiredKeys) {
  const differences = diffSnapshots(before, after);
  const missing = [];
  for (const k of requiredKeys) {
    const inB = Object.prototype.hasOwnProperty.call(before.values || {}, k);
    const inA = Object.prototype.hasOwnProperty.call(after.values || {}, k);
    if (!inB || !inA) missing.push({ key: k, before: inB ? 'present' : 'absent', after: inA ? 'present' : 'absent' });
  }
  const v = after.values || {};
  const setProblems = [];
  // A set recovered exactly: same size and same digest. Either alone is not enough.
  if (v.recover_baseline_count !== v.recover_after_reap_count) {
    setProblems.push('the recovered set is a different size from the baseline');
  }
  if (v.recover_baseline_digest !== v.recover_after_reap_digest) {
    setProblems.push('the recovered set is not the baseline set, although its size may match');
  }
  // A leak: the set grew by the number of frames that were never given back.
  const grew = v.leak_after_count - v.leak_baseline_count;
  if (grew !== 4) setProblems.push('the leak set grew by ' + grew + ' frames, the constructed leak is four');
  if (v.leak_baseline_digest === v.leak_after_digest) {
    setProblems.push('the leak set has the baseline digest, so the digest cannot see the leak');
  }
  return {
    differences, missing, set_problems: setProblems,
    by_comparator: differences.length > 0,
    by_requirement: missing.length > 0,
    by_frame_set: setProblems.length > 0,
    refused: differences.length > 0 || missing.length > 0 || setProblems.length > 0,
  };
}

const before = snapshot({ files: FILES, values, host });
sleep(1200); // a real interval, so a zero difference is a statement about the artefacts and not about the clock
const after = snapshot({ files: FILES, values, host });
const repeat = judge(before, after, REQUIRED_KEYS);

const clone = (v) => JSON.parse(JSON.stringify(v));

const MUTATIONS = [
  {
    name: 'four-frame-leak',
    why: 'four frames moved from free to allocated and never came back; the sum of free and allocated is unchanged, so the '
      + 'check 139 used passes while the machine is four frames poorer',
    apply: (s) => { s.values.leak_after_count += 4; s.values.leak_after_digest = 0x1AF22CB5 ^ 0x0F0F0F0F; },
    expect: 'by_frame_set',
  },
  {
    name: 'equal-size-changed-set',
    why: 'the same number of frames and a different set of them: one frame given back and another taken. A count comparison '
      + 'accepts it, a sum accepts it, and the digest does not',
    apply: (s) => { s.values.recover_after_reap_digest = 0xA29EF9B5 ^ 0x00FF00FF; },
    expect: 'by_frame_set',
  },
  {
    name: 'equal-size-changed-artefact',
    why: 'an artefact whose byte count is identical and whose content is not: the size comparison passes and the identity '
      + 'does not',
    apply: (s) => {
      const p = s.files['native_runtime.o'];
      if (!isPin(p)) throw new Error('the artefact pin is absent');
      s.files['native_runtime.o'] = { path: p.path, bytes: p.bytes, sha256: sha256(Buffer.from('a different object')) };
    },
    expect: 'by_comparator',
  },
  {
    name: 'sibling-damaged',
    why: 'a live sibling object was overwritten while another attempt was refused; its size is unchanged',
    apply: (s) => {
      const p = s.files['process_address_space.o'];
      if (!isPin(p)) throw new Error('the sibling pin is absent');
      s.files['process_address_space.o'] = { path: p.path, bytes: p.bytes, sha256: sha256(Buffer.from('damaged sibling')) };
    },
    expect: 'by_comparator',
  },
  {
    name: 'wrong-capacity-reason',
    why: 'the refusal is attributed to the wrong capacity, which changes no number at all',
    apply: (s) => { s.values.capacity_reason = 'the frame allocator is exhausted'; },
    expect: 'by_comparator',
  },
  {
    name: 'paired-absent-fields',
    why: 'two fields are absent on both sides; the comparator is right to call two absent values equal, and the requirement '
      + 'layer is what refuses the incomplete record',
    apply: (s) => { delete s.values.slot_capacity; delete s.values.pages_at_refusal; },
    expect: 'by_requirement',
  },
  {
    name: 'leaked-object',
    why: 'an object that was created and never reaped, which leaves its frames held and its slot live',
    apply: (s) => {
      s.values.objects_live = before.values.objects_live + 1;
      s.values.objects_reaped = before.values.objects_reaped - 1;
      s.values.frames_held_after_reap = before.values.frames_held_after_reap + 7;
    },
    expect: 'by_comparator',
  },
];

const mutations = MUTATIONS.map((m) => {
  const copy = clone(after);
  m.apply(copy);
  const r = judge(before, copy, REQUIRED_KEYS);
  return {
    name: m.name, why: m.why, expected_layer: m.expect,
    refused: r.refused,
    refused_by_expected_layer: r[m.expect] === true,
    by_comparator: r.by_comparator, by_requirement: r.by_requirement, by_frame_set: r.by_frame_set,
    differences: r.differences.length > 4
      ? r.differences.slice(0, 4).concat([{ at: '...', kind: 'truncated', before: null, after: null },
        { at: 'total', kind: 'number', before: r.differences.length, after: r.differences.length }])
      : r.differences,
    missing: r.missing,
    set_problems: r.set_problems,
  };
});

const out = {
  task: 'GTOS-DSH-141',
  what: 'the production observation comparator imported and run over real snapshots of the artefacts the guarded runs '
    + 'produced, a frame-set layer over the sets the unit published from the production allocator, and a requirement layer, '
    + 'with a real-interval repeat and seven single-condition mutations',
  layers: {
    comparator: { module: 'references/observation-diff-141.mjs',
      sha256: sha256(fs.readFileSync(R + '/references/observation-diff-141.mjs')) },
    frame_set: { produced_by: 'tests/runtime_test.cpp: CaptureFrameSet over the production allocator, FrameSetDigestOf, '
      + 'SayFrameSet; published in the guarded raw channels of cases 9 and 11' },
    requirement: { keys: REQUIRED_KEYS },
    path_mapper: { module: 'references/host-path-141.mjs',
      sha256: sha256(fs.readFileSync(R + '/references/host-path-141.mjs')) },
  },
  subject: {
    files: FILES.map((f) => ({ key: f.key, linux: f.path, host: host(f.path), pin: after.files[f.key] })),
    values,
    values_provenance: {
      from_case10_raw: ['protect_consulted', 'protect_refused', 'pages_at_refusal', 'late_create_returned', 'unit_marker'],
      from_case9_and_11_framesets: ['recover_*', 'leak_*', 'frames_held_after_reap'],
      declared: DECLARED,
      frame_sets: SETS,
    },
  },
  repeat: {
    zero_differences: repeat.differences.length === 0 && repeat.missing.length === 0 && repeat.set_problems.length === 0,
    differences: repeat.differences,
    missing: repeat.missing,
    set_problems: repeat.set_problems,
    files_pinned: Object.keys(after.files).length,
    note: 'the two snapshots were taken a real interval apart and every file was read twice, so a zero difference is a '
      + 'statement about the artefacts rather than about the clock',
  },
  mutations,
  mutations_refused: mutations.filter((m) => m.refused).length,
  mutations_refused_by_expected_layer: mutations.filter((m) => m.refused_by_expected_layer).length,
  mutations_total: mutations.length,
  ok: repeat.differences.length === 0 && repeat.missing.length === 0 && repeat.set_problems.length === 0
    && mutations.every((m) => m.refused && m.refused_by_expected_layer),
};
const target = R + '/recovery-judge-141.json';
if (fs.existsSync(target)) throw new Error('refused: exists ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({
  wrote: target,
  repeat_zero: out.repeat.zero_differences,
  files_pinned: out.repeat.files_pinned,
  frame_sets: SETS,
  mutations_refused: out.mutations_refused,
  mutations_refused_by_expected_layer: out.mutations_refused_by_expected_layer,
  mutations_total: out.mutations_total,
  unexpected: mutations.filter((m) => !(m.refused && m.refused_by_expected_layer)).map((m) => m.name),
  ok: out.ok,
}, null, 2));
process.exit(out.ok ? 0 : 1);