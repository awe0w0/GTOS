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
    'tests/boot_diagnostic.test.cpp', 'tests/boot_diagnostic_start.s',
    'tests/safe_tss_start.s', 'tests/tr_tss_test.cpp', 'tests/tr_tss_start.s', 'tests/tr_tss.ld',
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

// ================================================================ other host units
// The two units the packet's host gate also requires, built the same way: the production body, the unit own test, its own
// entry and the repository's own link script. They do not touch paging, so they keep the conventional layout.
if (phase === 'unit') {
  const stageRoot = process.argv[3];
  const wave = process.argv[4] || 'wave-06';
  const tag = process.argv[5] || 'u141';
  const helperLogical = process.argv[6];
  const testLogical = process.argv[7];
  const startLogical = process.argv[8];
  const ldLogical = process.argv[9] || 'tests/safe_tss.ld';
  // A unit that reads machine state cannot run on a host without the declared seam. The flag set and the delta file are the
  // same ones the runtime unit declares, so a unit either builds under the declared profile or does not build at all.
  const wantSeam = process.argv[10] === 'seam';
  const cxxFlagsForUnit = wantSeam ? SEAM : CXXFLAGS;
  const deltaForUnit = wantSeam ? { host_test_delta_path: R + '/profile-141/host-test-delta-runtime.txt' } : {};
  const rootAlnum = stageRoot.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*$/.test(rootAlnum)) throw new Error('bad stage root: ' + stageRoot);
  let seq = 0;
  const stageName = () => rootAlnum + '-' + String(++seq).padStart(2, '0');
  const srcRoot = S + '/' + wave;
  const objRoot = S + '/test-build/' + tag;
  fs.mkdirSync(toHost(objRoot), { recursive: true });
  const base = path.basename(testLogical, '.cpp');
  const helperList = helperLogical.split(',');
  const helperObjs = helperList.map((h) => objRoot + '/' + path.basename(h, '.cpp') + '.o');
  const testObj = objRoot + '/' + base + '.o';
  const startObj = objRoot + '/' + path.basename(startLogical, '.s') + '.o';
  const elfFile = objRoot + '/' + base + '.elf';
  freezeContractInLedger();
  const specs = [
    ...helperList.map((h, i) => writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: srcRoot + '/' + h, objectFile: helperObjs[i], cxxflags: cxxFlagsForUnit,
        ...deltaForUnit, sourceRoot: srcRoot, objectRoot: objRoot }, 480)),
    writeSpec(stageName(), 'test.cpp.build', 'cpp-unit-build', 'cxx',
      { sourceFile: srcRoot + '/' + testLogical, objectFile: testObj, cxxflags: cxxFlagsForUnit,
        ...deltaForUnit, sourceRoot: srcRoot, objectRoot: objRoot }, 480),
    writeSpec(stageName(), 'assembly', 'assembly', 'as',
      { sourceFile: srcRoot + '/' + startLogical, objectFile: startObj,
        sourceRoot: srcRoot, objectRoot: objRoot }, 240),
    writeSpec(stageName(), 'image.link', 'image-link', 'ld',
      { individuals: [startObj, ...helperObjs, testObj], mapFile: objRoot + '/test.map',
        elfFile, linkerScript: srcRoot + '/' + ldLogical, objectRoot: objRoot }, 300),
  ];
  for (const s of specs) out.push(run(s));
  const ok = out.every((r) => r.status === 0);
  console.log(JSON.stringify({ elfFile, elf: exists(toHost(elfFile)) ? pin(toHost(elfFile)) : null,
    stages: out.map((r) => ({ stage: r.stage, status: r.status, line: r.line, detail: r.detail })), ok }, null, 2));
  process.exit(ok ? 0 : 1);
}

// ================================================================ run any built unit
if (phase === 'rununit') {
  const stage = process.argv[3];
  const tag = process.argv[4] || 'u141';
  const elfBase = process.argv[5];
  const selectorArg = process.argv[6];
  const elfHost = S + '/test-build/' + tag + '/' + elfBase;
  if (!exists(toHost(elfHost))) throw new Error('no unit ELF at ' + toHost(elfHost));
  const workRoot = S + '/unit-run/' + stage;
  fs.mkdirSync(toHost(workRoot + '/logs'), { recursive: true });
  const options = { testElfLinux: elfHost, workRoot, limit: 240 };
  if (selectorArg !== undefined) options.caseSelector = Number(selectorArg);
  freezeContractInLedger();
  const spec = writeSpec(stage, 'test.cpp.run', 'host-unit-run', 'hostunit', options, 300);
  out.push(run(spec));
  const evidence = R + '/evidence/' + stage + '.json';
  let summary = null;
  let rawText = null;
  if (exists(evidence)) {
    const d = JSON.parse(fs.readFileSync(evidence, 'utf8'));
    summary = { ok: d.ok, verdict: d.verdict, guard_row: d.guard_row ? {
      exit_code: d.guard_row.exit_code, inner_timed_out: d.guard_row.inner_timed_out,
      timed_out: d.guard_row.timed_out, verdict: d.guard_row.verdict } : null,
      inner_report: d.inner_report, raw: d.raw };
    if (d.raw && d.raw.path) {
      for (let i = 0; i < 60 && rawText === null; i++) {
        try { rawText = fs.readFileSync(d.raw.path, 'utf8'); } catch {
          const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, 250);
        }
      }
    }
  }
  console.log(JSON.stringify({ stage, elf: pin(toHost(elfHost)), summary, raw_text: rawText }, null, 2));
  process.exit(0);
}

// ================================================================ the fixed61 default image
// The kernel image is not rebuilt here and nothing about it is assumed. Its 61 inputs are the ones the parent link actually
// consumed, taken from that link's own argv rather than from a list written down for this packet; every one of them is
// hashed against the catalog pin before the link runs, so a changed object stops the link instead of entering the image.
if (phase === 'fixedlink') {
  const stage = process.argv[3];
  const tag = process.argv[4] || 'fixed61';
  const rootAlnum = stage.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) throw new Error('bad stage name: ' + stage);
  const catalog = JSON.parse(fs.readFileSync(R + '/build-material-catalog-141.json', 'utf8'));
  const parentSpec = JSON.parse(fs.readFileSync(catalog.parent_actual_final_link_spec.path, 'utf8'));
  const parentArgv = parentSpec.cmd;
  const oAt = parentArgv.indexOf('-o');
  if (oAt < 0) throw new Error('the parent link argv has no output');
  const individualsLinux = parentArgv.slice(oAt + 2);
  // The pin for each input, keyed by the name the link used for it.
  const byName = new Map();
  for (const o of catalog.objects) byName.set(path.basename(o.object.path), o.object);
  byName.set(path.basename(catalog.loader.path), catalog.loader);
  const verified = [];
  const problems = [];
  // An input the catalog does not pin has to be produced here. The replacement is named on the command line and must be a real
  // object whose basename is the one the parent link used, so a substitution cannot change which slot it fills.
  const substituteLinux = process.argv[5] || null;
  const substitute = substituteLinux
    ? { linux: substituteLinux, host: toHost(substituteLinux),
        pin: exists(toHost(substituteLinux)) ? pin(toHost(substituteLinux)) : null }
    : null;
  if (substitute && !substitute.pin) throw new Error('the substitute object is not on disk: ' + substituteLinux);
  const resolved = individualsLinux.map((linux) => {
    const name = path.posix.basename(linux);
    // The order comes from the parent argv; the location comes from the catalog pin, which is the copy the guard can read
    // and the copy whose bytes were verified. The parent own working copy sits outside every declared read root.
    if (byName.has(name)) return { linux: toLinux(byName.get(name).path), name, source: 'catalog', parent_linux: linux };
    if (!substitute) return { linux, name, source: 'unpinned' };
    if (path.posix.basename(substituteLinux) !== name) return { linux, name, source: 'unpinned' };
    return { linux: substituteLinux, name, source: 'rebuilt', parent_linux: linux };
  });
  for (const entry of resolved) {
    const linux = entry.linux;
    const name = entry.name;
    const pinWant = byName.get(name);
    const hostPath = toHost(linux);
    if (!pinWant) {
      // The rebuilt substitute carries its own identity, which is recorded rather than compared with a pin that does not exist.
      if (entry.source === 'rebuilt') { verified.push({ input: name, bytes: substitute.pin.bytes, sha256: substitute.pin.sha256,
        source: 'rebuilt', parent_linux: entry.parent_linux, replaces: entry.parent_linux }); continue; }
      problems.push({ input: linux, why: 'no catalog pin for this input' }); continue;
    }
    if (!exists(hostPath)) { problems.push({ input: linux, why: 'the input is not on disk' }); continue; }
    const st = fs.lstatSync(hostPath);
    if (!st.isFile() || st.isSymbolicLink()) { problems.push({ input: linux, why: 'not an ordinary file' }); continue; }
    const b = fs.readFileSync(hostPath);
    const h = sha(b);
    if (h !== pinWant.sha256 || b.length !== pinWant.bytes) {
      problems.push({ input: linux, why: 'the bytes are not the pinned bytes', want: pinWant.sha256, got: h });
      continue;
    }
    verified.push({ input: name, bytes: b.length, sha256: h });
  }
  const linkerEntry = catalog.source_inputs.find((s) => s.logical === 'linker.ld');
  if (!linkerEntry) throw new Error('the catalog pins no linker script');
  const linkerHost = linkerEntry.source.path;
  const linkerBytes = fs.readFileSync(linkerHost);
  if (sha(linkerBytes) !== linkerEntry.source.sha256) problems.push({ input: linkerHost, why: 'the linker script is not pinned' });
  if (problems.length) {
    console.log(JSON.stringify({ stage, ok: false, problems, verified: verified.length }, null, 2));
    process.exit(1);
  }
  const imageDir = S + '/image';
  fs.mkdirSync(toHost(imageDir), { recursive: true });
  const elfFile = imageDir + '/' + tag + '.elf';
  const mapFile = imageDir + '/' + tag + '.map';
  freezeContractInLedger();
  const spec = writeSpec(stage, 'image.link', 'image-link', 'ld',
    { individuals: resolved.map((e) => e.linux), mapFile, elfFile,
      linkerScript: toLinux(linkerHost), objectRoot: imageDir }, 900);
  out.push(run(spec));
  const elfHost = toHost(elfFile);
  const mapHost = toHost(mapFile);
  const result = {
    stage, tag,
    inputs: { count: resolved.length, verified: verified.length, order: verified.map((v) => v.input),
      pins: verified,
      reused_from_catalog: verified.filter((v) => !v.source).length,
      rebuilt: verified.filter((v) => v.source === 'rebuilt').map((v) => ({ input: v.input, bytes: v.bytes,
        sha256: v.sha256, replaces: v.replaces })) },
    linker_script: { host: linkerHost, bytes: linkerBytes.length, sha256: sha(linkerBytes) },
    parent_link_argv_sha256: sha(Buffer.from(JSON.stringify(parentArgv))),
    image: exists(elfHost) ? pin(elfHost) : null,
    map: exists(mapHost) ? pin(mapHost) : null,
    baseline_image: catalog.baseline_image,
    reproduced_baseline: exists(elfHost) ? sha(fs.readFileSync(elfHost)) === catalog.baseline_image.sha256 : false,
    stages: out.map((r) => ({ stage: r.stage, status: r.status, line: r.line, detail: r.detail })),
    ok: out.every((r) => r.status === 0) && exists(elfHost),
  };
  console.log(JSON.stringify(result, null, 2));
  process.exit(result.ok ? 0 : 1);
}

// ================================================================ one object, kernel profile
// The parent link consumed one object that no catalog pin covers: boot_diagnostic.o, compiled in that packet own source view.
// 141 has to produce it, so it is compiled here under the frozen kernel profile -- the same flags, no host-test delta -- and
// the result is compared with the parent object. If the bytes match, the rebuild reproduces what the parent linked; if they
// do not, the difference is a fact to report rather than something to paper over.
if (phase === 'kobj') {
  const stageRoot = process.argv[3];
  const wave = process.argv[4];
  const tag = process.argv[5];
  const logical = process.argv[6];
  const rootAlnum = stageRoot.replace(/[^a-z0-9]/g, '');
  if (!/^[a-z][a-z0-9]*$/.test(rootAlnum)) throw new Error('bad stage root: ' + stageRoot);
  const objRoot = S + '/test-build/' + tag;
  fs.mkdirSync(toHost(objRoot), { recursive: true });
  const objectFile = objRoot + '/' + path.basename(logical, '.cpp') + '.o';
  freezeContractInLedger();
  const spec = writeSpec(rootAlnum + '-01', 'test.cpp.build', 'cpp-unit-build', 'cxx',
    { sourceFile: S + '/' + wave + '/' + logical, objectFile, cxxflags: CXXFLAGS,
      sourceRoot: S + '/' + wave, objectRoot: objRoot }, 600);
  out.push(run(spec));
  const hostObj = toHost(objectFile);
  console.log(JSON.stringify({ stage: rootAlnum + '-01', objectFile,
    object: exists(hostObj) ? pin(hostObj) : null,
    stages: out.map((r) => ({ stage: r.stage, status: r.status, line: r.line, detail: r.detail })),
    ok: out.every((r) => r.status === 0) && exists(hostObj) }, null, 2));
  process.exit(out.every((r) => r.status === 0) && exists(hostObj) ? 0 : 1);
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