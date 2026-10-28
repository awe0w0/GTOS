// GTOS-DSH-141 driver.
//
// Every build, link and unit launch this packet performs goes through one path: the runner reserves a named stage and freezes
// a helper, the helper hands the frozen bridge control to the production bridge, the bridge admits its own request, and the
// guard launches it in a child of its own with the two channels it writes itself. Nothing here runs a compiler, a linker or an
// ELF directly, and nothing here reads a result the guard did not write.
//
// Phases:
//   contract                       freeze the host unit contract this packet's specs pin
//   view    <stage> <wave>         materialise the source view the builds compile against
//   build   <stage> <wave> <tag>   compile and link the runtime host unit
//   runcase <stage> <tag> <case>   one case, one process, through the guard
//   gates   <stage> <tag>          re-read the unit's own records and report what they say

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const A = JSON.parse(fs.readFileSync(R + '/authority-141.json', 'utf8'));
const S = A.source_root;
const L = A.work_root;
const NODE = 'C:/Program Files/nodejs/node.exe';
const RUNNER = R + '/frozen-runners/run-141-r01.mjs';
const BRIDGE = R + '/references/private-boot-checkpoints-141.mjs';
const WORKTREE = R + '/worktree-141';
const ORDER_LOG = R + '/producer-order-141.jsonl';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
function toLinux(p) {
  if (/^[A-Za-z]:[\\/]/.test(p)) return '/mnt/' + p[0].toLowerCase() + '/' + p.slice(3).replace(/\\/g, '/');
  return p;
}
function toHost(l) {
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(l);
  if (m) return m[1].toUpperCase() + ':/' + m[2];
  return l.startsWith('/') ? '//wsl$/Ubuntu-20.04' + l : l;
}
const exists = (p) => fs.existsSync(p);
function put(hostPath, bytes) {
  if (exists(hostPath)) throw new Error('REFUSED exists: ' + hostPath);
  fs.mkdirSync(path.dirname(hostPath), { recursive: true });
  fs.writeFileSync(hostPath, bytes, { flag: 'wx' });
  const landed = fs.readFileSync(hostPath);
  const want = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes, 'utf8');
  if (!landed.equals(want)) throw new Error('not identical: ' + hostPath);
  return { path: hostPath, bytes: landed.length, sha256: sha(landed) };
}

// ---------------------------------------------------------------- the contract
const CXXFLAGS = (() => {
  const makefile = fs.readFileSync(R + '/profile-141/Makefile', 'utf8');
  const base = makefile.split('\n').find((l) => l.startsWith('BASE_CXXFLAGS :='))
    .replace('BASE_CXXFLAGS :=', '').trim().replace('$(OPTIMIZATION)', '-O2').split(/\s+/).filter(Boolean);
  const integer = fs.readFileSync(R + '/profile-141/kernel-cxxflags', 'utf8').trim().split(/\s+/).filter(Boolean);
  return [...base, ...integer];
})();
const DELTA = fs.readFileSync(R + '/profile-141/host-test-delta-runtime.txt', 'utf8')
  .split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));
const SEAM = [...CXXFLAGS, ...DELTA.map((m) => '-D' + m)];

const CONTRACT = R + '/contract-cpp-141.json';
function contractPin() {
  if (!exists(CONTRACT)) throw new Error('the contract is not frozen yet: run the contract phase first');
  return pin(CONTRACT);
}
function orderEvent(event, detail) {
  let next = 1;
  if (exists(ORDER_LOG)) {
    const lines = fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean);
    if (lines.length) next = JSON.parse(lines[lines.length - 1]).seq + 1;
  }
  const rec = { seq: next, event, at: new Date().toISOString(), ...detail };
  fs.appendFileSync(ORDER_LOG, JSON.stringify(rec) + '\n', { encoding: 'utf8' });
  return rec;
}
function freezeContractInLedger(note) {
  const p = contractPin();
  const lines = exists(ORDER_LOG)
    ? fs.readFileSync(ORDER_LOG, 'utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l)) : [];
  if (lines.some((l) => l.event === 'contract-frozen' && l.contract && l.contract.sha256 === p.sha256)) return;
  orderEvent('contract-frozen', { contract: p, per_contract: true, epoch: note || null,
    what: 'the frozen flag profile, the declared host-test delta and the link layout every host unit spec pins' });
}

// ---------------------------------------------------------------- the bridge control and the spec
function bridgeFor(stage, kind, route) {
  return {
    task: A.task, stage, outer_stage: stage, kind, route,
    report_root: R, work_root: L, source_root: S, distribution: 'Ubuntu-20.04',
    authority: pin(R + '/authority-141.json'),
    request_module: R + '/references/native-request-141.mjs',
    tools: { python_realpath: A.python.realpath },
    guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
    runner_module: RUNNER,
    binding_module: pin(BRIDGE),
    profile_makefile: R + '/profile-141/Makefile',
    profile_cxxflags: R + '/profile-141/kernel-cxxflags',
    report_path: R + '/evidence/' + stage + '.json',
    log_max_bytes: 524288,
  };
}
function writeSpec(stage, route, mode, kind, options, limit) {
  const control = {
    task: A.task, route, mode,
    bridge_module_path: BRIDGE,
    bridge: bridgeFor(stage, kind, route),
    stage_options: options || {},
    purpose: kind + ' for ' + A.task,
    report_root: R,
    report_path: R + '/results/' + stage + '.json',
  };
  const spec = { task: A.task, route, stage, root: R, runnerPath: RUNNER,
    helper: 'native-141-01.mjs', control,
    expected_outputs: [control.report_path], limit, contract_pin: contractPin() };
  const p = R + '/stage-output/' + stage + '.spec.json';
  if (exists(p)) throw new Error('spec exists: ' + p);
  fs.writeFileSync(p, JSON.stringify(spec, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  orderEvent('spec-written', { stage, kind, spec: p, contract_sha256: spec.contract_pin.sha256 });
  return p;
}
function run(specPath) {
  const stage = path.basename(specPath).replace('.spec.json', '');
  orderEvent('run-started', { stage });
  const r = spawnSync(NODE, [RUNNER, specPath], { windowsHide: true, timeout: 1800000, maxBuffer: 64 * 1024 * 1024 });
  const outText = (r.stdout || '').toString();
  const line = (outText.split('\n').filter((l) => l.startsWith('stage='))[0] || '').trim();
  let detail = '';
  for (const p of [R + '/results/' + stage + '.json', R + '/evidence/' + stage + '.json']) {
    if (exists(p)) {
      try {
        const d = JSON.parse(fs.readFileSync(p, 'utf8'));
        const t = d.stderr_tail || (d.transport && d.transport.stderr_tail) || '';
        if (t) { detail = String(t).replace(/\n/g, ' | ').slice(0, 400); break; }
      } catch { }
    }
  }
  orderEvent('run-completed', { stage, status: r.status, ok: /ok=true/.test(line) });
  const errText = (r.stderr || '').toString();
  if (!detail && errText) detail = errText.replace(/\n/g, ' | ').slice(-700);
  return { stage, line, detail, status: r.status, stdout: outText.slice(-2000), stderr: errText.slice(-2000) };
}

const phase = process.argv[2];
const out = [];

// ================================================================ contract
if (phase === 'contract') {
  const materials = {
    task: A.task,
    what: 'the host unit contract: the frozen production flag profile, the declared host-test delta, the link layout, and the '
      + 'objects the unit links. A spec pins this file, so a build cannot be described as having run under a contract written '
      + 'afterwards.',
    cxxflags: CXXFLAGS,
    host_test_delta_macros: DELTA,
    unit_cxxflags: SEAM,
    link_layout: {
      script: 'tests/runtime_host.ld',
      load_address: '0x00100000',
      why: 'the unit must load where the modelled kernel image is, because the paging object refuses an unsafe context unless '
        + 'its own bytes are identity mapped. Linking at the modelled address makes that true of the host process too, which '
        + 'is the only way the production check can pass without being weakened.',
      modelled_kernel_window: ['0x00100000', '0x00500000'],
    },
    unit_sources_rebuilt: [
      'src/process/native_runtime.cpp', 'src/gdt.cpp', 'src/process/native_fp.cpp', 'src/multitasking.cpp',
      'src/process/native_realtime.cpp', 'src/memory/physical.cpp', 'src/memory/process_address_space.cpp',
      'src/memory/paging.cpp', 'src/process/native_surface.cpp', 'src/process/native_windows.cpp',
      'tests/runtime_test.cpp', 'tests/runtime_boundary.cpp', 'tests/gdt_host_seam.cpp',
    ],
    unit_assembly: ['tests/runtime_start.s'],
    unit_catalog_objects: ['native_fp.asm', 'elf32', 'resources'],
    test_cases: 11,
  };
  const written = put(CONTRACT, JSON.stringify(materials, null, 2) + '\n');
  freezeContractInLedger('contract-cpp-141 written once, then every spec pins its bytes');
  console.log(JSON.stringify({ wrote: written, contract: contractPin() }, null, 2));
  process.exit(0);
}

// ================================================================ view
if (phase === 'view') {
  const stage = process.argv[3];
  const wave = process.argv[4] || 'wave-01';
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-141.json', 'utf8'));
  const byLogical = new Map();
  for (const v of C.source_view) {
    byLogical.set(v.logical, { logical: v.logical, source: toLinux(v.source.path),
      bytes: v.source.bytes, sha256: v.source.sha256 });
  }
  // The files this packet actually changed, or added. They come from the packet's own worktree, never from a previous
  // packet, and a missing one is a stop rather than a fall back to the catalog.
  const overrides = [
    'include/common/boot_diagnostic.h', 'src/common/boot_diagnostic.cpp', 'src/kernel.cpp',
    'include/hardwarecommunication/cpu_work_pool.h', 'src/hardwarecommunication/cpu_work_pool.cpp',
    'src/hardwarecommunication/cpu_work_queue.cpp',
    'src/process/native_runtime.cpp', 'include/process/native_runtime.h',
    'src/memory/physical.cpp', 'include/memory/physical.h', 'src/memory/paging.cpp', 'include/memory/paging.h',
    'src/memory/process_address_space.cpp', 'src/gdt.cpp', 'include/gdt.h',
    'tests/safe_tss_test.cpp', 'tests/safe_tss_start.s', 'tests/safe_tss.ld',
    'tests/gdt_host_seam.cpp', 'tests/gdt_host_seam.h',
    'tests/runtime_test.cpp', 'tests/runtime_start.s', 'tests/runtime_boundary.cpp', 'tests/runtime_host.ld',
    'src/process/native_fp.cpp', 'src/multitasking.cpp', 'src/process/native_realtime.cpp',
    'tests/boot_diagnostic.test.cpp',
  ];
  for (const logical of overrides) {
    const p = WORKTREE + '/' + logical;
    if (!exists(p)) throw new Error('VIEW: a declared file is not in the worktree: ' + p);
    const b = fs.readFileSync(p);
    byLogical.set(logical, { logical, source: toLinux(p), bytes: b.length, sha256: sha(b) });
  }
  freezeContractInLedger();
  const files = [...byLogical.values()].sort((a, b) => (a.logical < b.logical ? -1 : 1));
  const control = { task: A.task, source_root: S, report_root: S,
    report_path: S + '/evidence/' + stage + '.json',
    request: { wave, files }, note: 'copies the frozen catalog plus this packet own files into a wave this stage owns' };
  put(toHost(S + '/frozen-helpers/' + stage + '.py'), fs.readFileSync(R + '/staging-141-01.py'));
  put(toHost(S + '/inputs/' + stage + '.json'), Buffer.from(JSON.stringify(control, null, 2) + '\n', 'utf8'));
  const p = writeSpec(stage, 'source.view', 'source-view', 'sourceview', { limit: 600 }, 700);
  out.push(run(p));
  console.log(JSON.stringify({ stage, wave, files: files.length, out }, null, 2));
  process.exit(out.every((r) => r.status === 0) ? 0 : 1);
}

// ================================================================ build
if (phase === 'build') {
  const stageRoot = process.argv[3];      // stage tag prefix, e.g. hunit14101
  const wave = process.argv[4] || 'wave-01';
  const tag = process.argv[5] || 'h141';
  // The stage grammar is one hyphen and a two digit tail, so every stage of one build shares an alnum prefix and is told
  // apart by its number. A name with a second hyphen is not a name this packet may claim.
  const rootAlnum = stageRoot.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*$/.test(rootAlnum)) throw new Error('bad stage root: ' + stageRoot);
  let seq = 0;
  const stageName = () => rootAlnum + '-' + String(++seq).padStart(2, '0');
  const srcRoot = S + '/' + wave;
  const objRoot = S + '/test-build/' + tag;
  fs.mkdirSync(toHost(objRoot), { recursive: true });
  const testElf = objRoot + '/runtime_test.elf';

  // The unit's own translation units are compiled from the staged view, so the bytes that were committed are the bytes that
  // were built. The three catalog objects are the unchanged dependencies.
  const C = JSON.parse(fs.readFileSync(R + '/build-material-catalog-141.json', 'utf8'));
  const rebuilt = [
    ['src/process/native_runtime.cpp', 'native_runtime'], ['src/gdt.cpp', 'gdt'],
    ['src/process/native_fp.cpp', 'native_fp'], ['src/multitasking.cpp', 'multitasking'],
    ['src/process/native_realtime.cpp', 'native_realtime'], ['src/memory/physical.cpp', 'physical'],
    ['src/memory/process_address_space.cpp', 'process_address_space'], ['src/memory/paging.cpp', 'paging'],
    ['src/process/native_surface.cpp', 'native_surface'], ['src/process/native_windows.cpp', 'native_windows'],
  ];
  const deltaPin = pin(R + '/profile-141/host-test-delta-runtime.txt');
  let index = 0;
  const specs = [];
  const objs = [];
  for (const [logical, name] of rebuilt) {
    const obj = objRoot + '/' + name + '.o';
    specs.push(writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: srcRoot + '/' + logical, objectFile: obj, cxxflags: SEAM,
        host_test_delta_path: deltaPin.path, sourceRoot: srcRoot, objectRoot: objRoot }, 700));
    objs.push(obj);
  }
  for (const name of ['tests/runtime_test.cpp', 'tests/runtime_boundary.cpp', 'tests/gdt_host_seam.cpp']) {
    const obj = objRoot + '/' + path.basename(name, '.cpp') + '.o';
    specs.push(writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: srcRoot + '/' + name, objectFile: obj, cxxflags: SEAM,
        host_test_delta_path: deltaPin.path, sourceRoot: srcRoot, objectRoot: objRoot }, 480));
    objs.push(obj);
  }
  const asmObj = objRoot + '/runtime_start.o';
  specs.push(writeSpec(stageName(), 'assembly', 'assembly', 'as',
    { sourceFile: srcRoot + '/tests/runtime_start.s', objectFile: asmObj, sourceRoot: srcRoot, objectRoot: objRoot }, 240));
  const deps = [];
  for (const slug of ['native_fp.asm', 'elf32', 'resources']) {
    const item = C.objects.find((o) => o.slug === slug);
    if (!item) throw new Error('catalog object missing: ' + slug);
    deps.push(toLinux(item.object.path));
  }
  const individuals = [asmObj, ...objs, ...deps];
  specs.push(writeSpec(stageName(), 'image.link', 'image-link', 'ld',
    { individuals, mapFile: objRoot + '/test.map', elfFile: testElf,
      linkerScript: srcRoot + '/tests/runtime_host.ld', objectRoot: objRoot }, 420));

  freezeContractInLedger();
  for (const s of specs) out.push(run(s));
  const ok = out.every((r) => r.status === 0);
  console.log(JSON.stringify({ testElf, elf: exists(toHost(testElf)) ? pin(toHost(testElf)) : null,
    map: exists(toHost(objRoot + '/test.map')) ? pin(toHost(objRoot + '/test.map')) : null,
    stages: out.map((r) => ({ stage: r.stage, status: r.status, line: r.line, detail: r.detail })), ok }, null, 2));
  process.exit(ok ? 0 : 1);
}

// ================================================================ runcase
if (phase === 'runcase') {
  const stage = process.argv[3];
  const tag = process.argv[4] || 'h141';
  const caseSelector = process.argv[5] === undefined ? null : Number(process.argv[5]);
  const objRoot = S + '/test-build/' + tag;
  const testElf = objRoot + '/runtime_test.elf';
  const elfHost = toHost(testElf);
  if (!exists(elfHost)) throw new Error('no unit ELF at ' + elfHost);
  const workRoot = S + '/unit-run/' + stage;
  // The guard writes its own spec, ledger, inner report and raw stream under <work-root>/logs, so that directory is the
  // caller's to create and never the guard's to invent.
  fs.mkdirSync(toHost(workRoot + '/logs'), { recursive: true });
  const options = { testElfLinux: testElf, workRoot, limit: 240 };
  if (caseSelector !== null) options.caseSelector = caseSelector;
  freezeContractInLedger();
  const p = writeSpec(stage, 'test.cpp.run', 'host-unit-run', 'hostunit', options, 300);
  out.push(run(p));
  const reportPath = R + '/results/' + stage + '.json';
  let summary = null;
  if (exists(reportPath)) {
    const rep = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
    summary = { ok: rep.ok, guard_row: rep.guard_row ? {
      exit_code: rep.guard_row.exit_code, inner_timed_out: rep.guard_row.inner_timed_out,
      timed_out: rep.guard_row.timed_out, verdict: rep.guard_row.verdict } : null,
      inner_report: rep.inner_report, raw: rep.raw, verdict: rep.verdict };
  }
  const rawHost = rep0Raw(summary, stage);
  const rawText = rawHost && exists(rawHost) ? fs.readFileSync(rawHost, 'utf8') : null;
  console.log(JSON.stringify({ stage, caseSelector, status: out[0].status, summary,
    raw_text: rawText, elf: pin(elfHost) }, null, 2));
  process.exit(0);
}
function rep0Raw(summary, stage) {
  const rep = R + '/results/' + stage + '.json';
  if (!exists(rep)) return null;
  try {
    const d = JSON.parse(fs.readFileSync(rep, 'utf8'));
    if (d.raw && d.raw.path) return toHost(d.raw.path);
    if (d.raw_path) return toHost(d.raw_path);
  } catch { }
  return null;
}

console.error('unknown phase: ' + phase);
process.exit(2);