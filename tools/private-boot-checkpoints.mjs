// GTOS-DSH-110 production bridge.
//
// Every native request is built here, from the normaliser's file descriptions, so no stage hand-assembles a control
// and no stage invents its own path conversion. The bridge never takes a free-form command line: each kind has its own
// builder, and the built argv must equal whatever the caller claimed.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const need = (ok, text) => { if (!ok) throw Error('BOOT BRIDGE: ' + text); };
const same = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length
  && a.every((v, i) => v === b[i]);
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// The single native stage identity gate. The runner calls this after the reservation and before the freeze; the bridge
// re-checks the same names before it builds the transport, so a mismatched stage can never reach the guard.
export function assertNativeStageIdentity(spec) {
  const c = spec?.control?.bridge;
  need(c && c.stage === spec.stage && c.outer_stage === spec.stage, 'outer/native stage identity');
  return true;
}

// A control may name the authority as a path or as a pin. A pin is verified before it is read, so the authority
// bytes the bridge uses are the bytes the caller declared.
function readPinned(value, label) {
  if (typeof value === 'string') {
    const bytes = fs.readFileSync(value);
    return { path: value, bytes: bytes.length, sha256: sha(bytes), text: bytes.toString('utf8') };
  }
  need(value && typeof value.path === 'string' && Number.isSafeInteger(value.bytes)
    && /^[0-9a-f]{64}$/.test(String(value.sha256)), 'pin schema for ' + label);
  const bytes = fs.readFileSync(value.path);
  need(bytes.length === value.bytes && sha(bytes) === value.sha256, 'pin changed for ' + label + ': ' + value.path);
  return { path: value.path, bytes: bytes.length, sha256: sha(bytes), text: bytes.toString('utf8') };
}

export async function loadBridgeContext(control) {
  const N = await import(pathToFileURL(control.request_module).href);
  const authorityRead = readPinned(control.authority, 'authority');
  const authority = JSON.parse(authorityRead.text);
  need(control.task === authority.task, 'current task');
  for (const key of ['report_root', 'work_root', 'source_root']) {
    need(control[key] === authority[key], 'authority ' + key);
  }
  const roles = N.parseToolRoles(authority);
  // The loader is resolved through tools[], and the authority's system_loader alias must agree with it.
  const loader = N.requireRole(roles, 'system-loader');
  need(loader.linux === authority.system_loader.path && loader.sha256 === authority.system_loader.sha256,
    'the system_loader alias must agree with the tools[] loader role');
  need(control.distribution === 'Ubuntu-20.04', 'exact distribution');
  need(control.tools.python_realpath === authority.python.realpath, 'system python identity');
  for (const role of roles.values()) {
    const b = fs.readFileSync(role.host);
    need(b.length === role.bytes && sha(b) === role.sha256, 'tool pin changed: ' + role.linux);
  }
  need(control.report_path.startsWith(control.report_root), 'fresh report under R');
  need(!fs.existsSync(control.report_path), 'fresh report under R');
  return { N, authority, roles, loader, authority_pin: { path: authorityRead.path, bytes: authorityRead.bytes, sha256: authorityRead.sha256 } };
}

// A Python stage runs its own frozen helper and its own control. It never borrows a boot request.
export function pythonRequest(control, stage, workRoot, limit) {
  need(/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage), 'Python stage');
  // The guard confines reads to the root it is given, so the frozen helper and its control must live in that same
  // root. A stage that writes into S therefore keeps both in S; every other Python stage keeps both in L.
  const helperRoot = workRoot;
  const argv = [control.tools.python_realpath, '-B', '-S',
    helperRoot + '/frozen-helpers/' + stage + '.py', helperRoot + '/inputs/' + stage + '.json'];
  return { stage, kind: 'python', argv, outputs: [], cwd: helperRoot, work_root: workRoot,
    helper_root: helperRoot, limit_seconds: limit,
    report: workRoot + '/evidence/' + stage + '.json',
    env: {}, cmd0: argv[0], python_entry: true };
}

// The production flags are read from the frozen profile and the caller must match them token for token. A count
// check alone would accept a different flag set of the same length, so the comparison is exact.
export function productionCxxFlags(control) {
  const makefile = fs.readFileSync(control.profile_makefile, 'utf8');
  const line = makefile.split('\n').find((l) => l.startsWith('BASE_CXXFLAGS :='));
  need(line, 'the frozen Makefile declares BASE_CXXFLAGS');
  const base = line.replace('BASE_CXXFLAGS :=', '').trim()
    .replace('$(OPTIMIZATION)', '-O2').split(/\s+/).filter(Boolean);
  const integer = fs.readFileSync(control.profile_cxxflags, 'utf8').trim().split(/\s+/).filter(Boolean);
  return [...base, ...integer];
}

export function cxxRequest(control, roles, options) {
  const { sourceFile, objectFile, cxxflags, sourceRoot, objectRoot } = options;
  // The object may live in a subdirectory of the object root, so its parent is created here rather than assumed.
  const objectDir = '//wsl$/Ubuntu-20.04' + path.posix.dirname(objectFile);
  fs.mkdirSync(objectDir, { recursive: true });
  const frozen = productionCxxFlags(control);
  need(Array.isArray(cxxflags), 'frozen production cxx flags');
  need(same(cxxflags, frozen), 'the supplied cxx flags equal the frozen production profile: '
    + cxxflags.length + ' vs ' + frozen.length);
  need(cxxflags[0] === '-O2' && cxxflags[1] === '-m32' && cxxflags[2] === '-std=c++11',
    'production flag head');
  need(!cxxflags.some((f) => f === '-D' || f.startsWith('-D') && !f.startsWith('-DLFS')),
    'no added -D flag');
  const argv = [roles.get('g++').linux, ...cxxflags, '-c', sourceFile, '-o', objectFile];
  return { stage: options.stage, kind: 'cxx', argv, outputs: [objectFile], cwd: sourceRoot,
    work_root: control.source_root, object_root: objectRoot,
    limit_seconds: options.limit || 420,
    env: { TMPDIR: control.source_root + '/tmp' }, cmd0: argv[0],
    source_file: sourceFile, object_file: objectFile };
}

export function asRequest(control, roles, options) {
  const { sourceFile, objectFile, sourceRoot, objectRoot } = options;
  const argv = [roles.get('as').linux, '--32', sourceFile, '-o', objectFile];
  return { stage: options.stage, kind: 'as', argv, outputs: [objectFile], cwd: sourceRoot,
    work_root: control.source_root, object_root: objectRoot,
    limit_seconds: options.limit || 240,
    env: { TMPDIR: control.source_root + '/tmp' }, cmd0: argv[0],
    source_file: sourceFile, object_file: objectFile };
}

export function ldRequest(control, roles, options) {
  const { individuals, mapFile, elfFile, linkerScript, objectRoot } = options;
  need(Array.isArray(individuals) && individuals.length > 0, 'individual sequence');
  need(new Set(individuals).size === individuals.length, 'distinct individuals');
  const argv = [roles.get('ld.bfd').linux, '-melf_i386', '-T', linkerScript,
    '-Map', mapFile, '-o', elfFile, ...individuals];
  return { stage: options.stage, kind: 'ld', argv, outputs: [mapFile, elfFile], cwd: objectRoot,
    work_root: control.source_root, object_root: objectRoot,
    limit_seconds: options.limit || 300,
    env: { TMPDIR: control.source_root + '/tmp' }, cmd0: argv[0],
    individuals: individuals.length };
}

export function hostUnitRequest(control, options) {
  const { testElfLinux, workRoot } = options;
  need(typeof testElfLinux === 'string' && testElfLinux.startsWith('/'), 'absolute test ELF');
  return { stage: options.stage, kind: 'hostunit', argv: [testElfLinux], outputs: [],
    cwd: workRoot, work_root: workRoot, limit_seconds: options.limit || 180, env: {},
    cmd0: testElfLinux, test_elf: testElfLinux };
}

// The emulator request. cmd0 is the bundled loader from the new ext4 runtime, never the 540 byte wrapper.
// The runtime's own library directories, in the order the loader should search them.
function runtimeLibraryPath(rt) {
  return [rt + '/root/usr/lib/x86_64-linux-gnu', rt + '/root/lib/x86_64-linux-gnu',
    rt + '/root/usr/lib', rt + '/root/lib'].join(':');
}

export function emulatorRequest(control, authority, options) {
  const { kind, image, logs, append } = options;
  const rt = control.source_root + '/runtime';
  const loader = rt + '/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2';
  const emulator = rt + '/root/usr/bin/qemu-system-i386';
  const base = [loader, '--library-path', runtimeLibraryPath(rt), emulator];

  // A version or help request asks the emulator a question and must not start a machine, so it carries no machine
  // options, no kernel and no append line. A checkpoint request starts the bounded diagnostic machine.
  const emulatorEnv = {
    LD_LIBRARY_PATH: runtimeLibraryPath(rt),
    QEMU_MODULE_DIR: rt + '/root/usr/lib/x86_64-linux-gnu/qemu',
  };
  if (kind === 'version') {
    return { stage: options.stage, kind, argv: [...base, '--version'], outputs: [],
      cwd: control.work_root, work_root: control.work_root, limit_seconds: options.limit || 60,
      env: emulatorEnv, cmd0: loader, emulator, image: null, logs: null, append: null,
      cmd0_is_the_bundled_loader: true, machine_started: false };
  }
  if (kind === 'help') {
    return { stage: options.stage, kind, argv: [...base, '--help'], outputs: [],
      cwd: control.work_root, work_root: control.work_root, limit_seconds: options.limit || 60,
      env: emulatorEnv, cmd0: loader, emulator, image: null, logs: null, append: null,
      cmd0_is_the_bundled_loader: true, machine_started: false };
  }
  need(typeof image === 'string' && image.startsWith('/'), 'a checkpoint needs the current image');
  need(typeof append === 'string' && append.length > 0, 'a checkpoint needs its append line');
  need(logs && logs.e9 && logs.serial, 'a checkpoint needs two log destinations');
  const machine = ['-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'qemu32',
    '-smp', '1', '-m', '128M',
    '-nodefaults', '-no-user-config', '-display', 'none', '-monitor', 'none',
    '-nic', 'none', '-vga', 'none',
    '-serial', 'file:' + logs.serial,
    '-chardev', 'file,id=e9,path=' + logs.e9,
    '-device', 'isa-debugcon,iobase=0xe9,chardev=e9',
    '-no-reboot', '-no-shutdown',
    '-L', rt + '/firmware'];
  const argv = [...base, ...machine, '-kernel', image, ...(options.extra || []), '-append', append];
  return { stage: options.stage, kind, argv, outputs: [logs.e9, logs.serial],
    cwd: control.work_root, work_root: control.work_root, limit_seconds: options.limit || 130,
    env: emulatorEnv, cmd0: loader, emulator, image, logs, append,
    cmd0_is_the_bundled_loader: true, machine_started: true };
}

export function validatePlanFiles(control, plan) {
  const host = (linux) => {
    if (typeof linux !== 'string' || !linux.startsWith('/')) throw new Error('BOOT BRIDGE: non-absolute input ' + linux);
    const mount = /^\/mnt\/([a-z])\/(.*)$/.exec(linux);
    if (mount) return mount[1].toUpperCase() + ':/' + mount[2];
    return '//wsl$/Ubuntu-20.04' + linux;
  };
  const inputs = [];
  if (plan.python_entry) inputs.push(plan.argv[3], plan.argv[4]);
  if (plan.kind === 'cxx' || plan.kind === 'as') inputs.push(plan.source_file);
  if (plan.kind === 'ld') {
    const t = plan.argv.indexOf('-T');
    inputs.push(plan.argv[t + 1]);
    for (const one of plan.argv.slice(plan.argv.indexOf('-o') + 2)) inputs.push(one);
  }
  if (plan.kind === 'hostunit') inputs.push(plan.test_elf);
  if (plan.image) inputs.push(plan.image);
  for (const input of inputs) {
    const p = host(input);
    need(fs.existsSync(p), 'input present: ' + input);
    const st = fs.lstatSync(p);
    need(st.isFile() && !st.isSymbolicLink(), 'ordinary input: ' + input);
  }
  for (const output of plan.outputs || []) {
    need(!fs.existsSync(host(output)), 'fresh output: ' + output);
    const parent = host(path.posix.dirname(output));
    need(fs.existsSync(parent) && fs.lstatSync(parent).isDirectory() && !fs.lstatSync(parent).isSymbolicLink(),
      'real output parent: ' + output);
  }
  if (plan.image && control.image_pin) {
    const b = fs.readFileSync(host(plan.image));
    need(b.length === control.image_pin.bytes && sha(b) === control.image_pin.sha256,
      'image identity before launch');
  }
}

export function bootWslArgv(control, plan, env, execSet) {
  need(Array.isArray(execSet) && execSet.length > 0, 'nonempty EXEC set');
  need(new Set(execSet).size === execSet.length, 'distinct EXEC set');
  need(control.guard_linux.startsWith('/mnt/f/'), 'guard path');
  const switches = ['--stage', plan.stage, '--purpose', plan.kind + ' for ' + control.task,
    '--work-root', plan.work_root, '--cwd', plan.cwd,
    '--log', plan.work_root + '/logs/runs-' + control.task.slice(-3) + '.jsonl',
    '--limit', String(plan.limit_seconds)];
  // Any stage that must read files outside its work root declares them here. The emulator reads the runtime in S
  // while its own work root is L, so S is granted read-only rather than widening the work root itself.
  if (plan.kind === 'python' && control.read_only_source) switches.push('--read-only', control.source_root);
  if (plan.machine_started === true || plan.kind === 'version' || plan.kind === 'help') {
    switches.push('--read-only', control.source_root);
  }
  for (const file of execSet) switches.push('--exec', file);
  for (const [key, value] of Object.entries(env || {})) switches.push('--env', key + '=' + value);
  return ['--distribution', control.distribution, '--exec', control.tools.python_realpath,
    '-B', '-S', control.guard_linux, ...switches, '--', ...plan.argv];
}

// ---------------------------------------------------------------- the one native entry point
export async function runBootStage(control, stage, kind, options = {}) {
  const { N, authority, roles, loader } = await loadBridgeContext(control);
  const ctx = { runtime_root: control.source_root + '/runtime', test_elf_linux: options.testElfLinux };

  const builders = {
    metadata: () => pythonRequest(control, stage, control.source_root, options.limit || 300),
    sourceview: () => pythonRequest(control, stage, control.source_root, options.limit || 420),
    staging: () => pythonRequest(control, stage, control.work_root, options.limit || 240),
    runtimestage: () => pythonRequest(control, stage, control.source_root, options.limit || 600),
    cxx: () => cxxRequest(control, roles, { ...options, stage }),
    as: () => asRequest(control, roles, { ...options, stage }),
    ld: () => ldRequest(control, roles, { ...options, stage }),
    hostunit: () => hostUnitRequest(control, { ...options, stage }),
    version: () => emulatorRequest(control, authority, { ...options, kind: 'version', stage }),
    help: () => emulatorRequest(control, authority, { ...options, kind: 'help', stage }),
    checkpoint: () => emulatorRequest(control, authority, { ...options, kind: 'checkpoint', stage }),
  };
  need(builders[kind], 'declared kind ' + kind);
  const plan = builders[kind]();

  const execKind = ['sourceview', 'metadata', 'staging', 'runtimestage'].includes(kind) ? 'python'
    : kind === 'hostunit' ? 'hostunit'
      : ['version', 'help', 'checkpoint'].includes(kind) ? 'qemu' : kind;
  plan.exec_set = N.execSetFor(execKind, authority, roles, ctx);
  plan.argv_tokens = plan.argv.length;

  if (options.argv !== undefined) need(same(options.argv, plan.argv), 'caller argv equals the built argv');
  validatePlanFiles(control, plan);
  const wslArgv = bootWslArgv(control, plan, plan.env, plan.exec_set);

  const runner = await import(pathToFileURL(control.runner_module).href);
  const transport = await runner.spawnPinned(authority.wsl.path, wslArgv,
    { timeoutMs: (plan.limit_seconds + 60) * 1000 });

  const host = (linux) => {
    const mount = /^\/mnt\/([a-z])\/(.*)$/.exec(linux);
    return mount ? mount[1].toUpperCase() + ':/' + mount[2] : '//wsl$/Ubuntu-20.04' + linux;
  };
  const ledger = host(plan.work_root + '/logs/runs-' + control.task.slice(-3) + '.jsonl');
  let guardRow = null;
  if (fs.existsSync(ledger)) {
    for (const line of fs.readFileSync(ledger, 'utf8').split('\n').filter(Boolean)) {
      let row; try { row = JSON.parse(line); } catch { continue; }
      if (row.stage === stage && row.kind === 'result') guardRow = row;
    }
  }
  const innerPath = host(plan.work_root + '/logs/inner-report-' + stage + '.json');
  const rawPath = host(plan.work_root + '/logs/child-output-' + stage + '.raw');
  const inner = fs.existsSync(innerPath) ? JSON.parse(fs.readFileSync(innerPath, 'utf8')) : null;
  const rawBytes = fs.existsSync(rawPath) ? fs.readFileSync(rawPath) : null;

  const verdict = classify(kind, plan, transport, guardRow, inner, rawBytes, control);
  const report = {
    task: control.task, stage, kind, route: control.route,
    plan: { argv: plan.argv, argv_tokens: plan.argv_tokens, cmd0: plan.cmd0,
      emulator: plan.emulator || null, image: plan.image || null, outputs: plan.outputs,
      exec_set: plan.exec_set, cwd: plan.cwd, work_root: plan.work_root,
      limit_seconds: plan.limit_seconds },
    wsl_argv: wslArgv,
    transport: { started: transport.started, exit_code: transport.exit_code,
      timed_out: transport.timed_out, launch_error: transport.launch_error,
      stdout_bytes: Buffer.byteLength(transport.stdout || '', 'utf8'),
      stderr_bytes: Buffer.byteLength(transport.stderr || '', 'utf8') },
    stdout_tail: String(transport.stdout || '').slice(-6000),
    stderr_tail: String(transport.stderr || '').slice(-6000),
    guard_row: guardRow, inner_report_present: inner !== null,
    raw_present: rawBytes !== null, raw_bytes: rawBytes ? rawBytes.length : 0,
    raw_sha256: rawBytes ? sha(rawBytes) : null,
    verdict, ok: verdict.accepted,
  };
  need(!fs.existsSync(control.report_path), 'fresh bridge report');
  fs.mkdirSync(path.dirname(control.report_path), { recursive: true });
  fs.writeFileSync(control.report_path, JSON.stringify(report, null, 2) + '\n',
    { encoding: 'utf8', flag: 'wx' });
  return report;
}

function classify(kind, plan, transport, guardRow, inner, rawBytes, control) {
  const emulator = ['version', 'help', 'checkpoint'].includes(kind);
  const expected = emulator ? 1 : 0;
  const v = {
    transport_started: transport.started === true,
    transport_exit: transport.exit_code,
    expected_guard_exit: expected,
    guard_exit_as_expected: transport.exit_code === expected,
    guard_row_present: guardRow !== null,
    inner_present: inner !== null,
    raw_present: rawBytes !== null,
    raw_within_bound: rawBytes !== null && rawBytes.length <= (control.log_max_bytes || 524288),
    timed_out: transport.timed_out === true,
  };
  v.landlock_ok = guardRow ? guardRow.verdict?.landlock_ok === true : null;
  v.probes_ok = guardRow ? guardRow.verdict?.probes_ok === true : null;
  v.group_measured = guardRow ? guardRow.verdict?.group_identity_measured === true : null;
  v.group_empty_after_cleanup = guardRow ? guardRow.verdict?.group_empty_after_cleanup === true : null;

  if (emulator) {
    // A halted guest cannot exit cleanly, so the tool row is expected to be red. Guest behaviour is judged from logs.
    v.deadline_kill_expected = true;
    v.accepted = v.transport_started && v.guard_row_present && v.inner_present && v.raw_present
      && v.raw_within_bound && v.landlock_ok === true && v.probes_ok === true
      && v.group_measured === true && v.group_empty_after_cleanup === true;
  } else {
    v.accepted = v.transport_started && v.guard_exit_as_expected && v.guard_row_present
      && v.landlock_ok === true && v.probes_ok === true && !v.timed_out;
  }
  v.note = emulator
    ? 'a halted guest cannot exit cleanly, so the tool row is expected to be red. Guest behaviour is judged from the logs.'
    : 'a non-emulator stage must exit zero with a clean guard verdict.';
  return v;
}
