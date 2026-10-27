// GTOS-DSH-138 production bridge.
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

// The identity of the tool a plan will run: the executable's own name, its resolved path, and the hash of its bytes when
// this host can read them. A stage that says "I linked with ld.bfd" and a stage that says "something exited zero" are
// different claims, and only the first one is a tool identity. When the tool lives inside the distribution and this host
// cannot read it, the identity records that honestly rather than inventing a hash.
function toolIdentity(plan) {
  const cmd0 = plan.cmd0 || (plan.argv && plan.argv[0]) || null;
  if (typeof cmd0 !== 'string' || !cmd0) return null;
  const id = { name: path.posix.basename(cmd0), linux_path: cmd0, host_path: null,
    bytes: null, sha256: null, readable_from_this_host: false };
  const m = /^\/(.+)$/.exec(cmd0);
  if (m) id.host_path = '//wsl$/Ubuntu-20.04' + cmd0;
  try {
    if (id.host_path) {
      const bytes = fs.readFileSync(id.host_path);
      id.bytes = bytes.length;
      id.sha256 = sha(bytes);
      id.readable_from_this_host = true;
    }
  } catch { /* a tool this host cannot read stays null, which is a fact and not a failure */ }
  return id;
}

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

// A fixture work root is exactly <authority work_root>/test-only/<one segment>. Nothing deeper, nothing sideways.
export function isFixtureWorkRoot(candidate, authorityWorkRoot) {
  if (typeof candidate !== 'string' || typeof authorityWorkRoot !== 'string') return false;
  const prefix = authorityWorkRoot.replace(/\/+$/, '') + '/test-only/';
  if (!candidate.startsWith(prefix)) return false;
  const rest = candidate.slice(prefix.length);
  if (rest === '' || rest.includes('/')) return false;
  return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(rest);
}

export async function loadBridgeContext(control) {
  const N = await import(pathToFileURL(control.request_module).href);
  const authorityRead = readPinned(control.authority, 'authority');
  const authority = JSON.parse(authorityRead.text);
  need(control.task === authority.task, 'current task');
  // The work root is the one path a fixture may vary, and only into a declared test-only subtree of the authority's own
  // work root. This is not the gate being relaxed: any other value is still refused, the fixture form is validated
  // segment by segment, and the report and source roots must still match exactly.
  for (const key of ['report_root', 'source_root']) {
    need(control[key] === authority[key], 'authority ' + key);
  }
  need(control.work_root === authority.work_root || isFixtureWorkRoot(control.work_root, authority.work_root),
    'authority work_root');
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
  // A host test may add exactly the declared seam delta and nothing else. The delta file is frozen and pinned,
  // so the exception is a documented boundary rather than an open door.
  let flagsOk = same(cxxflags, frozen);
  let profileVariant = 'production';
  if (!flagsOk && options.host_test_delta_path) {
    const delta = fs.readFileSync(options.host_test_delta_path, 'utf8').split('\n')
      .map((line) => line.trim()).filter((line) => line && !line.startsWith('#'));
    const extra = cxxflags.slice(frozen.length);
    flagsOk = cxxflags.length === frozen.length + delta.length
      && frozen.every((flag, index) => flag === cxxflags[index])
      && delta.every((macro, index) => extra[index] === ('-D' + macro));
    if (flagsOk) profileVariant = 'production+host-test-delta';
  }
  need(flagsOk, 'the supplied cxx flags equal the frozen production profile, or that profile plus the '
    + 'declared host-test delta: ' + cxxflags.length + ' vs ' + frozen.length);
  need(cxxflags[0] === '-O2' && cxxflags[1] === '-m32' && cxxflags[2] === '-std=c++11',
    'production flag head');
  // An added -D is admitted only when it is exactly a macro the frozen delta declares. That keeps the
  // original rule (no smuggled defines) while allowing the one documented seam.
  let declaredMacros = [];
  if (options.host_test_delta_path) {
    declaredMacros = fs.readFileSync(options.host_test_delta_path, 'utf8').split('\n')
      .map((line) => line.trim()).filter((line) => line && !line.startsWith('#'));
  }
  const addedDefines = cxxflags.filter((f) => f.startsWith('-D') && !f.startsWith('-DLFS'));
  need(addedDefines.every((f) => declaredMacros.includes(f.slice(2))),
    'every added -D flag is declared in the frozen host-test delta');
  const argv = [roles.get('g++').linux, ...cxxflags, '-c', sourceFile, '-o', objectFile];
  return { stage: options.stage, kind: 'cxx', argv, outputs: [objectFile], cwd: sourceRoot,
    work_root: control.source_root, object_root: objectRoot,
    limit_seconds: options.limit || 420,
    env: { TMPDIR: control.source_root + '/tmp' }, cmd0: argv[0],
    source_file: sourceFile, object_file: objectFile,
    profile_variant: profileVariant };
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
  // One optional selector argument, so a single ELF can serve as several independent process fixtures. A successful
  // Activate publishes a process-wide owner that has no destroy path, so two activations cannot share a process.
  // The selector is a decimal integer and nothing else.
  const selector = options.caseSelector;
  const argv = [testElfLinux];
  if (selector !== undefined && selector !== null) {
    need(Number.isInteger(selector) && selector > 0 && selector < 100, 'case selector');
    argv.push(String(selector));
  }
  return { stage: options.stage, kind: 'hostunit', argv, outputs: [],
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
  // The selected identity must be supplied and must match the image this request names. This is the binding the 113
  // chain lacked, and it is checked here, before the argv is built.
  need(options.identity && typeof options.identity === 'object', 'a checkpoint needs the selected identity');
  need(options.identity.kind === 'selected_image_identity', 'the supplied identity is not a selected identity');
  need(options.image_pin && typeof options.image_pin === 'object', 'a checkpoint needs its image pin');
  need(options.image_pin.sha256 === options.identity.image.sha256
    && options.image_pin.bytes === options.identity.image.bytes,
    'the image pin is not the selected image');
  need(image === options.identity.image.linux,
    'the request image path is not the selected Linux image path: ' + image);
  need(typeof append === 'string' && append.length > 0, 'a checkpoint needs its append line');
  need(logs && logs.e9 && logs.serial, 'a checkpoint needs two log destinations');
  const vcpus = options.vcpus === undefined ? 1 : options.vcpus;
  // Three vCPUs are allowed for the two-AP scenario only: a BSP and two real APs, each running compiled fixed jobs.
  need(vcpus === 1 || vcpus === 2 || vcpus === 3, 'vCPU count must be 1, 2 or 3, got ' + vcpus);
  const machine = ['-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'qemu32',
    '-smp', String(vcpus), '-m', '128M',
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
    env: emulatorEnv, cmd0: loader, emulator, image, logs, append, vcpus,
    identity: options.identity.identity,
    image_pin: { bytes: options.identity.image.bytes, sha256: options.identity.image.sha256 },
    map_pin: { bytes: options.identity.map.bytes, sha256: options.identity.map.sha256 },
    session: options.session === undefined ? null : options.session,
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
  // An observer of this call only. It is told when this request is about to hand work to the transport and when that
  // transport settled. It observes nothing outside this call: no process table, no other stage, no other session.
  const observer = typeof options.observer === 'function' ? options.observer : null;
  if (observer) observer({ event: 'launch', stage, kind, cmd0: plan.cmd0, argv_tokens: plan.argv.length,
    image: plan.image || null, work_root: plan.work_root });
  const transport = await runner.spawnPinned(authority.wsl.path, wslArgv,
    { timeoutMs: (plan.limit_seconds + 60) * 1000 });
  if (observer) observer({ event: 'settled', stage, kind, exit_code: transport.exit_code,
    timed_out: transport.timed_out === true, launch_error: transport.launch_error || null });

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
  // The inner report and the raw stream are this bridge's own products, so this bridge records where it put them and what
  // they hashed at the moment it read them. In 137 only the booleans `inner_report_present` and `raw_present` were
  // recorded, so a consumer that wanted the inner report had to rebuild the same name itself. That is the caller
  // assembling an identity it never observed. The path is recorded here, with the bytes and the hash, and the consumer
  // checks the pin rather than guessing a name.
  const innerLinux = plan.work_root + '/logs/inner-report-' + stage + '.json';
  const rawLinux = plan.work_root + '/logs/child-output-' + stage + '.raw';
  const innerPath = host(innerLinux);
  const rawPath = host(rawLinux);
  const innerBytes = fs.existsSync(innerPath) ? fs.readFileSync(innerPath) : null;
  const inner = innerBytes === null ? null : JSON.parse(innerBytes.toString('utf8'));
  const rawBytes = fs.existsSync(rawPath) ? fs.readFileSync(rawPath) : null;

  const verdict = classify(kind, plan, transport, guardRow, inner, rawBytes, control);
  const report = {
    task: control.task, stage, kind, route: control.route,
    // The subject: what this stage asked the tool to do. A consumer that only sees the transport cannot tell whether the
    // tool that ran is the tool this stage meant to run. These are the built request, not the caller's claim.
    subject: {
      kind, cmd0: plan.cmd0, cwd: plan.cwd, work_root: plan.work_root,
      argv: plan.argv, argv_tokens: plan.argv_tokens,
      outputs: plan.outputs, image: plan.image || null, emulator: plan.emulator || null,
      exec_set: plan.exec_set, limit_seconds: plan.limit_seconds,
      tool_identity: toolIdentity(plan),
    },
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
    guard_row: guardRow,
    // The two channels, each with the identity this bridge observed.
    inner_report: {
      linux_path: innerLinux, path: innerPath,
      present: inner !== null,
      bytes: innerBytes === null ? null : innerBytes.length,
      sha256: innerBytes === null ? null : sha(innerBytes),
      // The tool's own return code, taken from the inner report the guard wrote. This is the number 137 substituted the
      // helper's transport exit code for.
      returncode: inner && inner.child ? inner.child.returncode : null,
      timed_out: inner && inner.child ? inner.child.timed_out : null,
      group: inner && inner.child ? {
        pgid: inner.child.group_pgid, measured: inner.child.group_measured,
        measured_final: inner.child.group_measured_final,
        descendants_left: inner.child.descendants_left,
        killed: inner.child.killed,
        output_path: inner.child.output_path,
        output_bytes: inner.child.output_bytes,
        output_file_bytes: inner.child.output_file_bytes,
        output_sha256: inner.child.output_sha256,
      } : null,
      argv: inner ? inner.argv : null,
      cwd: inner ? inner.cwd : null,
      stage: inner ? inner.stage : null,
      work_root: inner ? inner.work_root : null,
    },
    raw: {
      linux_path: rawLinux, path: rawPath,
      present: rawBytes !== null,
      bytes: rawBytes ? rawBytes.length : 0,
      sha256: rawBytes ? sha(rawBytes) : null,
    },
    // The 137 names are kept because old readers exist and because they are what this bridge always meant.
    inner_report_present: inner !== null,
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
    // 137 accepted a non-emulator stage on the transport and the guard row alone, so a stage whose tool wrote no inner
    // report and no raw stream was still "ok". The inner report is the only place the tool's own return code appears, so
    // accepting without it means accepting on the wrapper's exit code, which is what the guard returns, not what the tool
    // returned. Both channels are now required, and the tool's own code is required to be the expected one.
    v.tool_returncode = inner && inner.child ? inner.child.returncode : null;
    v.tool_timed_out = inner && inner.child ? inner.child.timed_out === true : null;
    v.accepted = v.transport_started && v.guard_exit_as_expected && v.guard_row_present
      && v.landlock_ok === true && v.probes_ok === true && !v.timed_out
      && v.inner_present && v.raw_present && v.raw_within_bound
      && v.tool_returncode === expected && v.tool_timed_out === false;
  }
  v.note = emulator
    ? 'a halted guest cannot exit cleanly, so the tool row is expected to be red. Guest behaviour is judged from the logs.'
    : 'a non-emulator stage must exit zero with a clean guard verdict.';
  return v;
}
