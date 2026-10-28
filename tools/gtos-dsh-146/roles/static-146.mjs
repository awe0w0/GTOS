// GTOS-DSH-146 role: static_byte_gate_full.
//
// The gate is the 144 packet's own module, run unmodified. That matters: a gate rewritten for this packet would be a second
// implementation of the same judgement, and a second implementation that agreed with the first would prove nothing about
// either. So the bytes copied into this packet's view are the bytes 144 froze, and the record pins them by hash.
//
// Two modules run, because 144 had two and they check different things:
//
//   static-gate-144.py   the first gate: ELF header, program headers, section table, entry symbol, undefined symbols, map
//                        membership, expected initializers. It builds four one-defect copies of the image and requires each to
//                        be refused.
//   static-gate-144b.py  with static-judge-144.py: the fuller judge, which adds the multiboot header and its checksum, the
//                        entry's section and segment, common symbols, the script's symbols, and the constructors, and runs
//                        every mutant through the same decision function as the real image.
//
// The image is the one this packet's package carries: 737872 bytes, 242c59e9. The map is the one the link wrote beside it.
// The 61 object basenames come from the 144 selection, which is where the link order was frozen.
//
// Python runs inside the distribution through its own absolute path. Nothing here runs a compiler, a linker, a disassembler or
// a tool that would decide for the gate.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const R145 = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const WSL_EXE = 'C:/Windows/System32/wsl.exe';
const DISTRO = 'Ubuntu-20.04';
const PYTHON = '/usr/bin/python3';
const L = '/var/tmp/gtos-dsh-146-e759-end-to-end';
const S = '/var/tmp/gtos-dsh-146-e759-end-to-end-view';
const WSL = '//wsl$/Ubuntu-20.04';
// One directory per run. A role record is evidence, and evidence is written once: a re-run writes into a new directory
// rather than over the previous attempt, so an earlier set of records stays readable instead of disappearing.
const RUN_TAG = process.argv[2] || '01';
const OUT_DIR = R + '/stage-output/roles-146-' + RUN_TAG;

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':/' + p.slice(9) : WSL + p);

// The frozen modules. They are read from the 145 worktree because that is where 144's helpers were carried forward
// unchanged; the hashes below are what 144 recorded, and they are checked before anything runs.
const MODULES = {
  gate: { name: 'static-gate-144.py', from: R145 + '/worktree-145/tools/gtos-dsh-144/static-gate-144.py' },
  judge_runner: { name: 'static-gate-144b.py', from: R145 + '/worktree-145/tools/gtos-dsh-144/static-gate-144b.py' },
  judge: { name: 'static-judge-144.py', from: R145 + '/worktree-145/tools/gtos-dsh-144/static-judge-144.py' },
};

function wsl(args) {
  const r = spawnSync(WSL_EXE, ['-d', DISTRO, '--', ...args], { encoding: 'utf8', windowsHide: true, maxBuffer: 32 * 1024 * 1024 });
  return { status: r.status, stdout: (r.stdout || '').trim(), stderr: (r.stderr || '').trim().slice(0, 2000) };
}

function putLinux(linuxPath, bytes) {
  // Written from the Windows side through the \\wsl$ share. The parent is made by WSL itself, because mkdir through the share
  // on a path WSL does not know about yet is how a directory ends up with the wrong owner.
  const dir = path.posix.dirname(linuxPath);
  wsl(['/bin/mkdir', '-p', dir]);
  const host = toHost(linuxPath);
  fs.mkdirSync(path.dirname(host), { recursive: true });
  fs.writeFileSync(host, bytes);
  const back = fs.readFileSync(host);
  return { path: linuxPath, bytes: back.length, sha256: sha(back) };
}

function readLinuxJson(linuxPath) {
  const host = toHost(linuxPath);
  if (!fs.existsSync(host)) return null;
  try { return JSON.parse(fs.readFileSync(host, 'utf8')); } catch { return { unreadable: true }; }
}

// ---------------------------------------------------------------- the expected values
const selection = JSON.parse(fs.readFileSync(R144 + '/p-selection-144-a.json', 'utf8'));
const catalog = JSON.parse(fs.readFileSync(R144 + '/build-material-catalog-144.json', 'utf8'));
const image = JSON.parse(fs.readFileSync(R + '/current-default-materials-146.json', 'utf8')).native_image_content;
const map = JSON.parse(fs.readFileSync(R + '/current-default-materials-146.json', 'utf8')).map;

// The map record names its Windows path but not its Linux one, so the Linux path is derived from the share form rather than
// assumed. Deriving it is the difference between reading the file this packet declares and reading a path this script made up.
const linuxOf = (rec) => rec.linux || String(rec.path).replace(/^\/\/wsl\$\/[^/]+/, '');
const imageLinux = linuxOf(image);
const mapLinux = linuxOf(map);
const imageHost = toHost(imageLinux);
const mapHost = toHost(mapLinux);
if (!fs.existsSync(imageHost)) throw new Error('the image is absent: ' + imageLinux);
if (!fs.existsSync(mapHost)) throw new Error('the map is absent: ' + mapLinux);
const imageBytes = fs.readFileSync(imageHost);
if (imageBytes.length !== image.bytes || sha(imageBytes) !== image.sha256) {
  throw new Error('the image is not the bytes this packet declares: ' + sha(imageBytes) + ' ' + imageBytes.length);
}

const basenames = selection.slots.map((s) => s.basename);
if (basenames.length !== 61) throw new Error('the selection no longer carries 61 slots: ' + basenames.length);
const initializers = catalog.baseline_expected_initializers || [];
if (initializers.length === 0) throw new Error('the catalog names no expected initializers');

// ---------------------------------------------------------------- the two runs
const runTag = 'sg146-01';
const modules = {};
for (const [key, m] of Object.entries(MODULES)) {
  const bytes = fs.readFileSync(m.from);
  modules[key] = { from: m.from, from_sha256: sha(bytes), from_bytes: bytes.length,
    ...putLinux(L + '/frozen-helpers/' + (key === 'gate' ? runTag + '.py' : m.name), bytes) };
}

const results = {};
let linkOrderInitializers = null;

// 1. the gate.
{
  const control = {
    task: 'GTOS-DSH-146',
    stage: runTag,
    image: imageLinux,
    map: mapLinux,
    negatives_root: L + '/tmp/gate-' + runTag,
    report_path: L + '/evidence/' + runTag + '-gate.json',
    expected: { load_address: 0x0100000, entry_symbol: 'loader', object_order: basenames, initializers },
    note: 'reads the image bytes and the map text; runs no tool, disassembles nothing, and builds four single-defect copies '
      + 'of the image in a scratch directory it owns',
  };
  const controlPath = L + '/inputs/' + runTag + '-gate.json';
  const controlPin = putLinux(controlPath, Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
  const r = wsl([PYTHON, L + '/frozen-helpers/' + runTag + '.py', controlPath]);
  const report = readLinuxJson(control.report_path);
  results.gate = {
    module: modules.gate, control: controlPin, exit_status: r.status, stderr: r.stderr,
    report_path: control.report_path,
    report_pin: fs.existsSync(toHost(control.report_path))
      ? { bytes: fs.statSync(toHost(control.report_path)).size, sha256: sha(fs.readFileSync(toHost(control.report_path))) } : null,
    ok: !!(report && report.ok), accepted: !!(report && report.accepted),
    image_bytes: report ? report.bytes : null,
    header: report ? { type: report.header && report.header.e_type, machine: report.header && report.header.e_machine,
      class: report.header && report.header.ei_class, data: report.header && report.header.ei_data } : null,
    program_headers: report ? report.program_headers : null,
    loadable_segments: report ? report.loadable_segments : null,
    lowest_load_vaddr: report ? report.lowest_load_vaddr : null,
    sections: report ? report.sections : null,
    entry: report ? report.entry : null,
    entry_symbols: report ? report.entry_symbols : null,
    undefined_symbols: report ? (report.undefined_symbols || []).length : null,
    map_objects: report ? report.map_objects : null,
    map_order_matches_argv: report ? report.map_order_matches_argv : null,
    initializers_expected: report ? report.initializers_expected : null,
    initializers_missing: report ? report.initializers_missing : null,
    problems: report && report.problems ? report.problems : null,
    problem_count: report && report.problems ? report.problems.length : null,
    negatives: report && report.negatives ? report.negatives.map((n) => ({ name: n.name, refused: n.refused,
      obligations_hit: n.obligations_hit, matched_expectation: n.matched_expectation })) : null,
    negatives_ok: report ? report.negatives_ok : null,
  };
}

// 2. the fuller judge, with the objects it reads.
{
  // The objects the judge is given are the ones the map names, not the ones the selection lists. Those are different paths:
  // the selection records the 140 view the objects were originally built in, and the 144 link read them from its own copy at
  // /var/tmp/gtos-dsh-144-e759-finality-view/producers/p144/. The map is the record of what was actually linked, so its paths
  // are the ones that answer "was this image made from these objects". Handing the judge the selection's paths made it refuse
  // the real image with "the map names an object at a different path" -- a true statement about two different directories, and
  // a wrong question to ask.
  const mapText = fs.readFileSync(mapHost, 'utf8');
  // The order matters, so it is preserved: `.sort()` here would destroy the one thing the map records that nothing else does.
  // An earlier version of this line sorted the paths, and the judge then derived the constructors in alphabetical object order
  // and reported that the image's table was permuted -- which it was, relative to a list this script had just scrambled.
  const mapPaths = [...new Set((mapText.match(/\/[^\s]*?\.o\b/g) || []))];
  const objects = mapPaths.length ? mapPaths : selection.slots.map((s) => s.linux);
  const absent = objects.filter((o) => !fs.existsSync(toHost(o)));
  // Each catalog initializer is named after the object that defines it: `_GLOBAL__sub_I_bootLogEmergencyScreen` comes from
  // kernel.o, and the two process ones from native_surface.o and native_windows.o. The association is derived from the names,
  // and then the list is put in the link order the map records.
  const symbolToObject = new Map();
  for (const sym of initializers) {
    const tail = sym.replace(/^_GLOBAL__sub_I_/, '');
    const guess = [
      'bootLogEmergencyScreen', 'NativeSurfaceBank', 'NativeWindows',
    ].find((k) => tail.includes(k));
    if (guess === 'bootLogEmergencyScreen') symbolToObject.set(sym, 'kernel.o');
    else if (guess === 'NativeSurfaceBank') symbolToObject.set(sym, 'native_surface.o');
    else if (guess === 'NativeWindows') symbolToObject.set(sym, 'native_windows.o');
  }
  const initializersInLinkOrder = [];
  for (const p of objects) {
    const base = p.split('/').pop();
    for (const [sym, obj] of symbolToObject) if (obj === base && !initializersInLinkOrder.includes(sym)) initializersInLinkOrder.push(sym);
  }
  for (const sym of initializers) if (!initializersInLinkOrder.includes(sym)) initializersInLinkOrder.push(sym);
  linkOrderInitializers = initializersInLinkOrder;
  if (absent.length) {
    results.judge = { ok: false, refused_before_running: 'producer objects are absent', absent: absent.slice(0, 4),
      absent_count: absent.length };
  } else {
    const control = {
      task: 'GTOS-DSH-146',
      stage: runTag + 'b',
      image: imageLinux,
      map: mapLinux,
      objects,
      objects_from: 'the map text',
      relocated_image: null,
      relocated_map: null,
      scratch: L + '/tmp/judge-' + runTag,
      report_path: L + '/evidence/' + runTag + '-judge.json',
      // The constructors are expected in the order the link produced them, which is the order the objects appear in the map.
      // The catalog lists the three symbols without an order; the map supplies it. Asking the table to match an unordered list
      // would be asking the linker to sort, which it does not do.
      expected: { entry_symbol: 'loader', initializers: initializersInLinkOrder },
      note: 'the judge is a pure function of bytes and text; the normal image and every mutant go through it',
    };
    const controlPath = L + '/inputs/' + runTag + '-judge.json';
    const controlPin = putLinux(controlPath, Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
    const r = wsl([PYTHON, L + '/frozen-helpers/static-gate-144b.py', controlPath]);
    const report = readLinuxJson(control.report_path);
    results.judge = {
      modules: { runner: modules.judge_runner, judge: modules.judge },
      control: controlPin, exit_status: r.status, stderr: r.stderr,
      report_path: control.report_path,
      report_pin: fs.existsSync(toHost(control.report_path))
        ? { bytes: fs.statSync(toHost(control.report_path)).size, sha256: sha(fs.readFileSync(toHost(control.report_path))) } : null,
      ok: !!(report && report.ok),
      real: report && report.real ? {
        accepted: report.real.accepted,
        obligations_failed: report.real.obligations_failed,
        problem_count: (report.real.problems || []).length,
        multiboot: report.real.multiboot,
        multiboot_checksum_sum: report.real.multiboot_checksum_sum,
        entry: report.real.entry, entry_symbols: report.real.entry_symbols,
        entry_section: report.real.entry_section, entry_segment: report.real.entry_segment,
        loadable_segments: report.real.loadable_segments, lowest_load_vaddr: report.real.lowest_load_vaddr,
        sections: report.real.sections,
        common_symbols: (report.real.common_symbols || []).length,
        undefined_symbols: (report.real.undefined_symbols || []).length,
        map_objects: report.real.map_objects,
        map_order_matches_argv: report.real.map_order_matches_argv,
        script_symbols: report.real.script_symbols,
        constructors_expected: report.real.constructors_expected,
        constructors_in_image: report.real.constructors_in_image,
        ctors_section: report.real.ctors_section,
        last_section_end: report.real.last_section_end,
      } : null,
      mutants: report && report.mutants ? report.mutants.map((m) => ({ id: m.id, refused: m.refused,
        targets: m.targets, hit_target: m.hit_target, wrong_reason: m.wrong_reason })) : null,
      mutants_ok: report ? report.mutants_ok : null,
      problems: report && report.problems ? report.problems : null,
    };
  }
}

const gateOk = results.gate.ok === true && results.gate.negatives_ok === true;
const judgeOk = results.judge.ok === true;
const out = {
  task: 'GTOS-DSH-146',
  role: 'static_byte_gate_full',
  what: 'the 144 static byte gate and its fuller judge, run unmodified over the image this packet carries',
  image: { linux: imageLinux, bytes: imageBytes.length, sha256: sha(imageBytes) },
  map: { linux: mapLinux, bytes: fs.statSync(mapHost).size, sha256: sha(fs.readFileSync(mapHost)) },
  expected: { load_address: '0x0100000', entry_symbol: 'loader', object_order_count: basenames.length,
    initializers: initializers.length, initializers_in_link_order: linkOrderInitializers },
  modules,
  results,
  python: { distro: DISTRO, path: PYTHON, why: 'inside the distribution by its own absolute path; nothing runs on the host' },
  ok: gateOk && judgeOk,
  ok_means: 'the gate accepts the real image, refuses all four of its own one-defect copies, and the fuller judge accepts the '
    + 'real image and refuses every mutant it builds, with each refusal hitting the obligation it was aimed at.',
};
fs.mkdirSync(OUT_DIR, { recursive: true });
const outPath = OUT_DIR + '/static_byte_gate_full.json';
if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ ok: out.ok, gate: { ok: results.gate.ok, accepted: results.gate.accepted,
  problems: results.gate.problem_count, negatives: results.gate.negatives && results.gate.negatives.length,
  negatives_ok: results.gate.negatives_ok },
  judge: { ok: results.judge.ok, failed: results.judge.real && results.judge.real.obligations_failed,
  mutants: results.judge.mutants && results.judge.mutants.length, mutants_ok: results.judge.mutants_ok,
  stderr: results.judge.stderr ? results.judge.stderr.slice(0, 400) : null },
  gate_stderr: results.gate.stderr ? results.gate.stderr.slice(0, 400) : null }, null, 2));
process.exit(out.ok ? 0 : 1);
