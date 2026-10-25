import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL, fileURLToPath } from 'node:url';

const need = (ok, text) => { if (!ok) throw Error('BOOT BRIDGE: ' + text); };
const same = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => v === b[i]);
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const hostKey = p => path.resolve(p).replaceAll('\\', '/').toLowerCase();
const within = (p, root) => hostKey(p).startsWith(hostKey(root).replace(/\/$/, '') + '/');

export function checkedPin(p) {
  need(p && typeof p.path === 'string' && Number.isSafeInteger(p.bytes) && p.bytes >= 0
    && /^[0-9a-f]{64}$/.test(p.sha256), 'component pin schema');
  const b = fs.readFileSync(p.path);
  need(b.length === p.bytes && sha(b) === p.sha256, 'component pin changed: ' + p.path);
  return b;
}
const jsonPin = p => JSON.parse(checkedPin(p).toString('utf8'));
const equalPin = (actual, expected) => actual && expected && actual.bytes === expected.bytes
  && actual.sha256 === expected.sha256;

export function bootExecSetFor(kind, tools, api, context) {
  return api.bootExecSet(context, kind, tools);
}

export function metadataRequest(context, stage) {
  need(/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage), 'Python stage');
  const argv = [context.tools.python_realpath, '-B', '-S', context.work_root + '/frozen-helpers/' + stage + '.py',
    context.work_root + '/inputs/' + stage + '.json'];
  return { stage, kind: 'metadata', argv, outputs: [], cwd: context.work_root, limit_seconds: 60,
    metadata_output: context.work_root + '/evidence/' + stage + '.json', env: {}, cmd0: argv[0] };
}

export function bootPlan(context, stage, kind, options) {
  const { api, tools, authorization } = options;
  const choice = context.native_choices?.[stage];
  need(choice && choice.stage === stage && choice.kind === kind, 'registered stage and kind');
  const pythonKind = ['metadata', 'runtimeunit', 'runtimestage'].includes(kind);
  const workRoot = kind === 'runtimestage' ? context.source_root : context.work_root;
  const request = pythonKind ? { ...metadataRequest({ ...context, work_root: workRoot }, stage), kind, work_root: workRoot }
    : api.bootRequest(context, stage, kind, options.suppliedArgv);
  need(same(request.argv, choice.argv), 'exact frozen argv order');
  if (options.suppliedArgv !== undefined) need(same(options.suppliedArgv, request.argv), 'caller argv');
  const category = kind === 'metadata' ? 'native_metadata' : kind === 'runtimeunit' ? 'native_runtime_unit_test'
    : kind === 'runtimestage' ? 'runtime_stage' : kind === 'e99' ? 'vm_e99_trial' : 'emulator_probe';
  need(choice.category === category, 'frozen execution category');
  const execSet = bootExecSetFor(category, tools, api, context);
  need(same(execSet, authorization.native_EXEC?.[category]), 'exact EXEC file grants');
  if (kind === 'e99') {
    const g = authorization.target_execution_grant;
    need(g?.authorized === true && g.max_launches === 2 && g.inner_seconds === 12 && g.outer_seconds === 90,
      'bounded VM grant');
    need(g.full_desktop === false && g.services === false && g.host_devices === false
      && g.network === false && g.disks === false && g.source_repair === false, 'no widened VM grant');
    need(same(request.outputs, choice.outputs), 'exact log outputs');
  }
  return { ...request, category, operation: choice.operation, mode: choice.mode,
    exec_set: execSet, argv_tokens: request.argv.length,
    cmd0_is_the_bundled_loader: !pythonKind && request.argv[0] !== request.emulator };
}

export function validatePlanFiles(control, context, plan) {
  const host = context.nativeHostPath;
  const inputs = ['metadata', 'runtimeunit', 'runtimestage'].includes(plan.kind) ? [plan.argv[3], plan.argv[4]]
    : plan.kind === 'e99' ? [plan.image] : [];
  for (const input of inputs) {
    const p = host(input);
    need(fs.existsSync(p) && fs.lstatSync(p).isFile() && !fs.lstatSync(p).isSymbolicLink(), 'ordinary input: ' + input);
  }
  for (const output of [...plan.outputs, ...(plan.metadata_output ? [plan.metadata_output] : [])]) {
    need(!fs.existsSync(host(output)), 'fresh output: ' + output);
    const parent = host(path.posix.dirname(output));
    need(fs.existsSync(parent) && fs.lstatSync(parent).isDirectory() && !fs.lstatSync(parent).isSymbolicLink(),
      'real output parent: ' + output);
  }
  if (plan.kind === 'e99') {
    const b = fs.readFileSync(host(plan.image));
    need(b.length === control.source_image.bytes && sha(b) === control.source_image.sha256, 'staged image identity');
  }
}

export function bootWslArgv(control, plan, env) {
  need(Array.isArray(plan.exec_set) && plan.exec_set.length > 0
    && new Set(plan.exec_set).size === plan.exec_set.length, 'distinct EXEC set');
  need(typeof control.guard_linux === 'string' && control.guard_linux.startsWith('/mnt/f/'), 'guard path');
  const switches = ['--stage', plan.stage, '--purpose', plan.kind + ' for ' + control.task,
    '--work-root', plan.work_root || control.work_root, '--cwd', plan.cwd, '--log', (plan.work_root || control.work_root) + '/logs/runs-' + control.task.slice(-3) + '.jsonl',
    '--limit', String(plan.limit_seconds)];
  if (plan.kind !== 'runtimestage') switches.push('--read-only', control.source_root);
  for (const file of plan.exec_set) switches.push('--exec', file);
  for (const [key, value] of Object.entries(env)) switches.push('--env', key + '=' + value);
  return ['--distribution', control.distribution, '--exec', control.tools.python_realpath, '-B', '-S', control.guard_linux,
    ...switches, '--', ...plan.argv];
}

export async function loadBridgeContext(control) {
  const authorization = jsonPin(control.authorization), authority = jsonPin(authorization.authority);
  need(control.task === authorization.task && control.task === authority.task, 'current task');
  for (const key of ['report_root', 'work_root', 'source_root']) need(control[key] === authority[key], 'authority ' + key);
  need(control.toolchain_root === authority.context.toolchain_root, 'authority toolchain');
  for (const [key, expected] of [['request_module', authority.boot_module], ['path_module', authority.path_module],
    ['runner', authority.runner], ['wsl', authority.wsl], ['guard', authority.guard]]) {
    need(equalPin(control[key], expected), 'authority component ' + key);
    checkedPin(control[key]);
  }
  need(within(control.runner.path, control.report_root + '/frozen-runners'), 'actual frozen runner path');
  need(control.guard_linux === authority.bridge_control_schema.guard_linux
    && control.distribution === 'Ubuntu-20.04', 'exact guard and distribution');
  need(control.tools.python_realpath === authority.python.realpath && control.tools.loader === authority.system_loader.path,
    'system tool identities');
  need(within(control.report_path, control.report_root) && !fs.existsSync(control.report_path), 'fresh report under R');
  need(JSON.stringify(authorization.native_EXEC) === JSON.stringify(authority.native_EXEC)
    && JSON.stringify(authorization.target_execution_grant) === JSON.stringify(authority.target_execution_grant), 'authority grants');
  const runtimePins = [authority.runtime.loader, authority.runtime.emulator, ...authority.runtime.firmware];
  for (const p of runtimePins) checkedPin(p);
  if (['version', 'help', 'e99'].includes(control.kind)) {
    const staged = runtimePins.map(p => {
      need(p.linux_path.startsWith(authority.source_root + '/runtime/'), 'runtime execution path under S');
      return { path: '//wsl$/Ubuntu-20.04' + p.linux_path, bytes: p.bytes, sha256: p.sha256 };
    });
    for (const p of staged) checkedPin(p);
    runtimePins.push(...staged);
  }
  const api = await import(pathToFileURL(control.request_module.path).href);
  const paths = await import(pathToFileURL(control.path_module.path).href);
  const runner = await import(pathToFileURL(control.runner.path).href);
  need(typeof runner.spawnPinned === 'function', 'actual runner spawn');
  runner.assertRunnerPin(control.runner.path);
  const context = { work_root: authority.work_root, source_root: authority.source_root,
    toolchain_root: authority.context.toolchain_root, native_choices: authority.native_choices,
    tools: control.tools, nativeHostPath: p => paths.nativeHostPath(control.path_context, p) };
  need(control.path_context.report_root === authority.report_root && control.path_context.work_root === authority.work_root
    && control.path_context.source_root === authority.source_root, 'authority mapping context');
  need(equalPin(control.source_image, authority.image), 'authority source image');
  return { authorization, authority, api, paths, runner, context,
    components: [control.authorization, authorization.authority, control.request_module, control.path_module,
      control.runner, control.wsl, control.guard, ...runtimePins] };
}

export function readBootEvidence(control, plan, api, paths) {
  const host = p => paths.nativeHostPath(control.path_context, p);
  const readBounded = (p, limit) => {
    const file = host(p);
    if (!fs.existsSync(file)) return null;
    need(fs.statSync(file).size <= limit, 'oversized evidence: ' + p);
    const b = fs.readFileSync(file);
    need(b.length <= limit, 'oversized evidence after read: ' + p);
    return b;
  };
  const json = p => { const b = readBounded(p, 1024 * 1024); return b === null ? null : JSON.parse(b); };
  const root = (plan.work_root || control.work_root) + '/logs/', spec = json(root + 'spec-' + plan.stage + '.json'),
    inner = json(root + 'inner-report-' + plan.stage + '.json');
  const rawPath = root + 'child-output-' + plan.stage + '.raw', rawBytes = readBounded(rawPath, 4 * 1024 * 1024);
  const raw = rawBytes === null ? null : { requested_path: rawPath, bytes: rawBytes.length, sha256: sha(rawBytes) };
  const ledger = readBounded(root + 'runs-' + control.task.slice(-3) + '.jsonl', 8 * 1024 * 1024);
  const rows = ledger === null ? [] : ledger.toString('utf8').split(/\r?\n/).filter(Boolean).map(JSON.parse)
    .filter(r => r.kind === 'result' && r.stage === plan.stage);
  need(rows.length <= 1, 'one result row for current stage');
  const row = rows[0] || null;
  const data = { request: plan, spec, inner, row, raw };
  if (plan.kind === 'e99') {
    data.e9 = readBounded(plan.e9, 65536); data.serial = readBounded(plan.serial, 65536);
  }
  const verdict = classifyNativeEvidence(plan, data, api);
  return { spec, inner, row, raw, verdict,
    e9_bytes: data.e9?.length ?? null, serial_bytes: data.serial?.length ?? null };
}

export function classifyNativeEvidence(plan, data, api) {
  if (plan.kind === 'e99') return api.inspectBootEvidence(data);
  const { spec, inner, row, raw } = data, c = inner?.child, checks = [];
  const check = (name, ok) => checks.push({ name, ok: !!ok });
  check('current exact request', spec?.stage === plan.stage && inner?.stage === plan.stage && row?.stage === plan.stage
    && same(spec?.cmd, plan.argv) && same(inner?.argv, plan.argv) && same(row?.argv, plan.argv));
  check('real zero exit without timeout', c && c.returncode === 0 && c.timed_out === false
    && row?.exit_code === 0 && row.ok === true && row.unshare_rc === 0 && row.timed_out === false
    && row.inner_timed_out === false && inner.refused !== true && row.refused === false);
  check('guard and all eight probes', inner?.landlock?.ok === true && row?.landlock_ok === true
    && api.BOOT_PROBES.every(p => inner?.probe_checks?.[p] === true && row?.probe_checks?.[p] === true));
  check('real raw identity', raw && raw.requested_path === c?.output_path && raw.bytes === c.output_bytes
    && raw.bytes === c.output_file_bytes && raw.sha256 === c.output_sha256);
  check('measured empty process groups', c?.group_measured === true && c.group_measured_final === true
    && Number.isInteger(c.group_pgid) && c.group_pgid > 0 && row?.group?.pgid === c.group_pgid
    && row.group.measured === true && row.group.final_measured === true
    && same(c.descendants_left, []) && same(c.killed, []) && same(row.group.descendants_left, []) && same(row.group.killed_after, []));
  return { ok: checks.every(c => c.ok), checks, failed: checks.filter(c => !c.ok).map(c => c.name),
    guard_tool_ok: row?.ok ?? null, target_execution_observed: false,
    early_boot_diagnostic_observed: false, constructors_execution_inferred_from_validated_path: null,
    expected_deadline_kill: false, full_boot_pass: false, init_ready: 'unknown', services_runtime_ready: 'unknown' };
}

export function transportAccepted(plan, transport, verdict, unchanged) {
  const expected = plan.kind === 'e99' ? 1 : 0;
  return transport.started === true && transport.exit_code === expected && transport.timed_out === false
    && transport.launch_error === null && verdict.ok === true && unchanged === true;
}

export function assertNativeStageIdentity(spec) {
  const c = spec?.control?.bridge;
  need(c && c.stage === spec.stage && c.outer_stage === spec.stage, 'outer/native stage identity');
  return true;
}

export function requireLiveReservation(control, plan) {
  need(control.stage === plan.stage && control.outer_stage === plan.stage, 'bridge stage binding');
  const file = path.join(control.report_root, 'attempt-' + plan.stage + '-' + control.task.slice(-3) + '.json');
  const bytes = fs.readFileSync(file), a = JSON.parse(bytes);
  need(a.task === control.task && a.attempt === plan.stage && a.complete === false && a.executor === 'guard'
    && a.category === plan.category && a.operation_id === plan.operation, 'live matching guard reservation');
  need(a.runner_sha256 === control.runner.sha256 && a.runner_bytes === control.runner.bytes, 'reservation runner identity');
  need(!fs.existsSync(path.join(control.report_root, 'result-' + plan.stage + '-' + control.task.slice(-3) + '.json')), 'no terminal result before native launch');
  return { path: file, bytes: bytes.length, sha256: sha(bytes) };
}

export function requireFreshNativeNamespace(control, plan, paths) {
  const root = (plan.work_root || control.work_root) + '/logs/';
  for (const name of ['spec-' + plan.stage + '.json', 'inner-report-' + plan.stage + '.json', 'child-output-' + plan.stage + '.raw'])
    need(!fs.existsSync(paths.nativeHostPath(control.path_context, root + name)), 'fresh native evidence name: ' + name);
  const ledger = paths.nativeHostPath(control.path_context, root + 'runs-' + control.task.slice(-3) + '.jsonl');
  if (fs.existsSync(ledger)) {
    need(fs.statSync(ledger).size <= 8 * 1024 * 1024, 'bounded existing ledger');
    const rows = fs.readFileSync(ledger, 'utf8').split(/\r?\n/).filter(Boolean).map(JSON.parse);
    need(!rows.some(r => r.stage === plan.stage), 'native stage never appeared in ledger');
  }
}

export async function runBootStage(control, stage, kind, options = {}) {
  const loaded = await loadBridgeContext(control);
  const { authorization, authority, api, paths, runner, context, components } = loaded;
  const plan = bootPlan(context, stage, kind, { api, tools: { ...control.tools, toolchain_root: control.toolchain_root },
    authorization, suppliedArgv: options.argv });
  components.push(requireLiveReservation(control, plan));
  requireFreshNativeNamespace(control, plan, paths);
  validatePlanFiles(control, context, plan);
  const activeRoot = plan.work_root || control.work_root;
  const env = { HOME: activeRoot + '/home', TMPDIR: activeRoot + '/tmp', TMP: activeRoot + '/tmp',
    TEMP: activeRoot + '/tmp', XDG_CACHE_HOME: activeRoot + '/cache', LANG: 'C.UTF-8',
    PYTHONDONTWRITEBYTECODE: '1', ...(plan.env || {}) };
  const wslArgv = bootWslArgv(control, plan, env);
  const transport = await runner.spawnPinned(control.wsl.path, wslArgv, { cwd: control.report_root, timeoutMs: 90000 });
  const evidence = readBootEvidence(control, plan, api, paths);
  let unchanged = true;
  try { for (const p of components) checkedPin(p); } catch { unchanged = false; }
  return { task: control.task, stage, kind, category: plan.category, plan, transport, evidence,
    guard_row: evidence.row, guard_tool_ok: evidence.row?.ok ?? null,
    expected_deadline_kill: evidence.verdict.expected_deadline_kill,
    early_boot_diagnostic_observed: evidence.verdict.early_boot_diagnostic_observed,
    target_execution_observed: evidence.verdict.target_execution_observed,
    constructors_execution_inferred_from_validated_path: evidence.verdict.constructors_execution_inferred_from_validated_path,
    component_posthash_unchanged: unchanged, full_boot_pass: false, init_ready: 'unknown', services_runtime_ready: 'unknown',
    ok: transportAccepted(plan, transport, evidence.verdict, unchanged) };
}

const main = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (main) {
  const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  runBootStage(control, control.stage, control.kind, { argv: control.args }).then(report => {
    fs.mkdirSync(path.dirname(control.report_path), { recursive: true });
    fs.writeFileSync(control.report_path, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
    console.log(JSON.stringify({ ok: report.ok, stage: report.stage, guard_tool_ok: report.guard_tool_ok,
      diagnostic: report.early_boot_diagnostic_observed })); process.exitCode = report.ok ? 0 : 1;
  }).catch(error => { console.error(String(error.stack || error)); process.exitCode = 1; });
}
