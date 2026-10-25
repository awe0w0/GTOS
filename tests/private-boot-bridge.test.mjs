import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const bytes = p => fs.readFileSync(p), sha = b => crypto.createHash('sha256').update(b).digest('hex');
const pin = p => ({ path: p, bytes: bytes(p).length, sha256: sha(bytes(p)) });
for (const p of [control.module, control.request_module]) {
  if (bytes(p.path).length !== p.bytes || sha(bytes(p.path)) !== p.sha256) throw Error('test subject pin');
}
const b = await import(pathToFileURL(control.module.path).href);
const api = await import(pathToFileURL(control.request_module.path).href);
let assertions = 0;
const check = (v, m) => { assertions++; if (!v) throw Error('assertion: ' + m); };
const reject = (f, m) => { let e; try { f(); } catch (x) { e = x; } check(!!e, m); };
const rejectAsync = async (f, m) => { let e; try { await f(); } catch (x) { e = x; } check(!!e, m); };
const copy = x => structuredClone(x);
fs.mkdirSync(control.fixture_root, { recursive: false });
const write = (name, data) => {
  const p = path.join(control.fixture_root, name); fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, typeof data === 'string' || Buffer.isBuffer(data) ? data : JSON.stringify(data), { flag: 'wx' }); return p;
};
const L = '/var/tmp/test-boot-work', S = '/var/tmp/test-boot-source';
const context = { work_root: L, source_root: S, toolchain_root: S + '/runtime', native_choices: {} };
const tools = { python_realpath: '/usr/bin/python3.8', loader: '/usr/lib/x86_64-linux-gnu/ld-2.31.so', toolchain_root: context.toolchain_root };
const native_EXEC = Object.fromEntries(['native_metadata', 'native_runtime_unit_test', 'runtime_stage', 'emulator_probe', 'vm_e99_trial'].map(k => [k, b.bootExecSetFor(k, tools, api, context)]));
const grant = { authorized: true, max_launches: 2, inner_seconds: 12, outer_seconds: 90,
  full_desktop: false, services: false, host_devices: false, network: false, disks: false, source_repair: false };
const authorization = { native_EXEC, target_execution_grant: grant };
for (const [stage, kind, category] of [['meta-01', 'metadata', 'native_metadata'], ['qemuver-01', 'version', 'emulator_probe'],
  ['qemuhelp-01', 'help', 'emulator_probe'], ['e99-01', 'e99', 'vm_e99_trial']]) {
  context.native_choices[stage] = { ...(kind === 'metadata' ? b.metadataRequest({ ...context, tools }, stage) : api.bootRequest(context, stage, kind)), category };
}
const plan = (stage, kind, auth = authorization, suppliedArgv) => b.bootPlan({ ...context, tools }, stage, kind,
  { api, tools, authorization: auth, suppliedArgv });
const meta = plan('meta-01', 'metadata'), probe = plan('qemuver-01', 'version'), vm = plan('e99-01', 'e99');
check(meta.argv.length === 5 && meta.cmd0 === tools.python_realpath, 'metadata is its own five-token request');
check(meta.exec_set.length === 2, 'metadata gets two EXEC files');
check(probe.argv.length === 5 && plan('qemuhelp-01', 'help').argv.at(-1) === '-help', 'query requests');
check(vm.argv.length === 38 && vm.exec_set.length === 4 && vm.cmd0_is_the_bundled_loader, 'VM exact request and honest cmd0');
check(vm.argv.includes('-nic') && vm.argv[vm.argv.indexOf('-nic') + 1] === 'none', 'required no-NIC option accepted');
reject(() => plan('absent-01', 'e99'), 'unregistered stage');
reject(() => plan('meta-01', 'version'), 'wrong kind');
reject(() => plan('e99-01', 'e99', { ...authorization, native_EXEC: { ...native_EXEC, vm_e99_trial: [...vm.exec_set, '/bin/sh'] } }), 'widened EXEC');
reject(() => plan('e99-01', 'e99', { ...authorization, target_execution_grant: { ...grant, network: true } }), 'widened grant');
reject(() => plan('e99-01', 'e99', { ...authorization, target_execution_grant: null }), 'missing grant');
reject(() => plan('e99-01', 'e99', authorization, [...vm.argv, '-drive', 'file=x']), 'extra argv');
reject(() => plan('e99-01', 'e99', authorization, [...vm.argv].reverse()), 'argv order');
const transportControl = { task: 'GTOS-DSH-107', distribution: 'Ubuntu-20.04', tools, guard_linux: '/mnt/f/guard.py', work_root: L, source_root: S };
const wsl = b.bootWslArgv(transportControl, vm, { HOME: L + '/home' });
check(wsl.filter(x => x === '--exec').length === 5, 'one WSL exec plus four file grants');
check(wsl.slice(wsl.indexOf(transportControl.guard_linux) + 1, wsl.indexOf('--')).filter(x => x === '--exec').length === 4, 'guard-only grant count');
check(JSON.stringify(wsl.slice(wsl.indexOf('--') + 1)) === JSON.stringify(vm.argv), 'real cmd0 immediately after guard separator');
reject(() => b.bootWslArgv(transportControl, { ...vm, exec_set: [...vm.exec_set, vm.exec_set[0]] }, {}), 'duplicate EXEC');
reject(() => b.bootWslArgv({ ...transportControl, guard_linux: '/other/guard.py' }, vm, {}), 'wrong guard mapping');

const raw = { requested_path: L + '/logs/child-output-e99-01.raw', bytes: 0, sha256: sha(Buffer.alloc(0)) };
const probes = Object.fromEntries(api.BOOT_PROBES.map(k => [k, true]));
const groups = { pgid: 99, measured: true, final_measured: true, descendants_left: [], killed_after: [] };
const child = { returncode: -9, timed_out: true, output_path: raw.requested_path, output_bytes: 0, output_file_bytes: 0,
  output_sha256: raw.sha256, group_measured: true, group_measured_final: true, group_pgid: 99, descendants_left: [], killed: [] };
const log = Buffer.from('BOOT B01 HANDOFF\nBOOT B02 MEMORY\n' + api.E99_MARKER + '\n');
const data = { request: vm, spec: { stage: vm.stage, cmd: vm.argv }, inner: { stage: vm.stage, argv: vm.argv,
  child, refused: false, landlock: { ok: true }, probe_checks: probes }, row: { kind: 'result', stage: vm.stage,
  argv: vm.argv, exit_code: -9, inner_timed_out: true, timed_out: false, unshare_rc: 1, ok: false, refused: false,
  landlock_ok: true, probe_checks: probes, group: groups }, raw, e9: log, serial: log };
const verdict = b.classifyNativeEvidence(vm, data, api);
check(verdict.ok && verdict.guard_tool_ok === false && verdict.target_execution_observed === true, 'expected deadline preserves failed guard row');
const transport = { started: true, exit_code: 1, timed_out: false, launch_error: null };
check(b.transportAccepted(vm, transport, verdict, true), 'E99 accepts guard rc1');
check(!b.transportAccepted(vm, { ...transport, exit_code: 0 }, verdict, true), 'E99 does not require rc0');
check(!b.transportAccepted(vm, transport, verdict, false), 'posthash change refuses');
check(!b.transportAccepted(vm, { ...transport, timed_out: true }, verdict, true), 'outer timeout refuses');
check(!b.transportAccepted(vm, transport, { ok: false }, true), 'failed diagnostic refuses');
for (const key of ['version', 'metadata']) {
  const p = key === 'version' ? probe : meta, d = copy(data);
  d.spec = { stage: p.stage, cmd: p.argv }; d.inner.stage = p.stage; d.inner.argv = p.argv;
  d.inner.child.returncode = 0; d.inner.child.timed_out = false;
  Object.assign(d.row, { stage: p.stage, argv: p.argv, exit_code: 0, inner_timed_out: false, unshare_rc: 0, ok: true });
  const v = b.classifyNativeEvidence(p, d, api);
  check(v.ok && v.target_execution_observed === false, key + ' succeeds without E99 log');
  check(b.transportAccepted(p, { ...transport, exit_code: 0 }, v, true), key + ' requires normal guard rc0');
  d.inner.probe_checks.negative_cat_denied = false;
  check(!b.classifyNativeEvidence(p, d, api).ok, key + ' rejects broken probe');
}
const invalid = copy(data); invalid.serial = Buffer.from('BOOT B01 HANDOFF\n');
check(!b.classifyNativeEvidence(vm, invalid, api).ok && b.classifyNativeEvidence(vm, invalid, api).target_execution_observed === null, 'missing diagnostic does not assert no guest execution');
const mapped = p => path.join(control.fixture_root, 'mapped', p.slice(1));
const mappedContext = { ...context, nativeHostPath: mapped };
for (const input of [meta.argv[3], meta.argv[4], vm.image]) write('mapped/' + input.slice(1), 'TEST_ONLY input');
for (const output of [...vm.outputs, meta.metadata_output]) fs.mkdirSync(path.dirname(mapped(output)), { recursive: true });
const sourcePin = pin(mapped(vm.image));
b.validatePlanFiles({ source_image: sourcePin }, mappedContext, vm); check(true, 'ordinary image and fresh log parents');
b.validatePlanFiles({}, mappedContext, meta); check(true, 'metadata input and output parent');
reject(() => b.validatePlanFiles({ source_image: { ...sourcePin, bytes: 0 } }, mappedContext, vm), 'changed staged image');
fs.writeFileSync(mapped(vm.e9), log, { flag: 'wx' });
reject(() => b.validatePlanFiles({ source_image: sourcePin }, mappedContext, vm), 'existing log output');
const missingParent = { ...vm, outputs: [L + '/not-created/log'] };
reject(() => b.validatePlanFiles({ source_image: sourcePin }, mappedContext, missingParent), 'missing output parent');
const plain = pin(write('plain.bin', 'unchanged'));
check(b.checkedPin(plain).toString() === 'unchanged', 'actual component pin');
reject(() => b.checkedPin({ ...plain, sha256: '0'.repeat(64) }), 'wrong component pin');
reject(() => b.checkedPin({ ...plain, bytes: -1 }), 'invalid pin schema');

// Filesystem evidence is TEST_ONLY. No guard, emulator or WSL is launched by this suite.
const evidencePrefix = 'mapped/' + L.slice(1) + '/logs/';
write(evidencePrefix + 'spec-e99-01.json', data.spec); write(evidencePrefix + 'inner-report-e99-01.json', data.inner);
write(evidencePrefix + 'child-output-e99-01.raw', Buffer.alloc(0)); write(evidencePrefix + 'runs-107.jsonl', JSON.stringify(data.row) + '\n');
fs.writeFileSync(mapped(vm.serial), log, { flag: 'wx' });
const evidenceControl = { ...transportControl, path_context: {} }, paths = { nativeHostPath: (_ctx, p) => mapped(p) };
check(b.readBootEvidence(evidenceControl, vm, api, paths).verdict.ok, 'reads complete actual fixture bytes');
fs.appendFileSync(mapped(vm.e9), Buffer.alloc(65537));
reject(() => b.readBootEvidence(evidenceControl, vm, api, paths), 'oversized log is rejected before truncation');
const e99Second = { ...vm, stage: 'e99-02', e9: vm.e9 + '.second', serial: vm.serial + '.second' };
check(!b.readBootEvidence(evidenceControl, e99Second, api, paths).verdict.ok, 'missing current evidence refuses');
fs.appendFileSync(mapped(L + '/logs/runs-107.jsonl'), JSON.stringify(data.row) + '\n');
reject(() => b.readBootEvidence(evidenceControl, e99Second.stage === 'x' ? e99Second : { ...vm, e9: vm.e9 + '.absent' }, api, paths), 'duplicate result row refuses');
fs.appendFileSync(mapped(L + '/logs/runs-107.jsonl'), 'not-json\n');
reject(() => b.readBootEvidence(evidenceControl, e99Second, api, paths), 'malformed ledger refuses');

// A sentinel module makes pre-import pin ordering observable without any native execution.
const sentinel = path.join(control.fixture_root, 'import-side-effect.txt');
const modulePin = pin(write('sentinel.mjs', `import fs from 'node:fs';fs.writeFileSync(${JSON.stringify(sentinel)},'imported',{flag:'wx'});`));
const runnerPin = pin(write('frozen-runners/test-runner.mjs', 'export function spawnPinned() { throw Error("TEST_ONLY no spawn"); }\nexport function assertRunnerPin() {}\n'));
const rolePin = pin(write('role.bin', 'TEST_ONLY role'));
const authority = { task: 'GTOS-DSH-107', report_root: control.fixture_root, work_root: L, source_root: S,
  context: { toolchain_root: context.toolchain_root }, boot_module: modulePin, path_module: rolePin, runner: runnerPin,
  wsl: rolePin, guard: rolePin, bridge_control_schema: { guard_linux: '/mnt/f/guard.py' },
  python: { realpath: tools.python_realpath }, system_loader: { path: tools.loader },
  native_EXEC, target_execution_grant: grant, runtime: { loader: rolePin, emulator: rolePin, firmware: [] }, image: sourcePin };
const authorityPin = pin(write('authority.json', authority));
const authPin = pin(write('authorization.json', { task: authority.task, authority: authorityPin, native_EXEC, target_execution_grant: grant }));
const loadControl = { ...transportControl, report_root: control.fixture_root, toolchain_root: context.toolchain_root,
  authorization: authPin, request_module: modulePin, path_module: rolePin, runner: runnerPin, wsl: rolePin, guard: rolePin,
  report_path: path.join(control.fixture_root, 'report.json'), path_context: { report_root: control.fixture_root, work_root: L, source_root: S }, source_image: sourcePin };
await rejectAsync(() => b.loadBridgeContext({ ...loadControl, authorization: { ...authPin, sha256: '0'.repeat(64) } }), 'authorization verified first');
check(!fs.existsSync(sentinel), 'bad authorization did not import');
await rejectAsync(() => b.loadBridgeContext({ ...loadControl, runner: { ...runnerPin, sha256: '0'.repeat(64) } }), 'runner pin before import');
check(!fs.existsSync(sentinel), 'bad runner did not import request module');
await rejectAsync(() => b.loadBridgeContext({ ...loadControl, path_module: { ...rolePin, bytes: 0 } }), 'path module pin before import');
check(!fs.existsSync(sentinel), 'bad path module did not import');
await rejectAsync(() => b.loadBridgeContext({ ...loadControl, task: 'GTOS-DSH-106' }), 'cross-task authority');
check(!fs.existsSync(sentinel), 'wrong task did not import');

check(b.assertNativeStageIdentity({ stage: 'meta-01', control: { bridge: { stage: 'meta-01', outer_stage: 'meta-01' } } }), 'one shared native stage');
reject(() => b.assertNativeStageIdentity({ stage: 'metastage-01', control: { bridge: { stage: 'meta-01', outer_stage: 'metastage-01' } } }), '107 outer/choice mismatch');
reject(() => b.assertNativeStageIdentity({ stage: 'meta-01', control: { bridge: { stage: 'meta-01' } } }), 'outer stage cannot be omitted');
const live = { task: 'GTOS-DSH-108', attempt: 'meta-03', complete: false, executor: 'guard', category: 'native_metadata', operation_id: 'qemu.metadata', runner_sha256: plain.sha256, runner_bytes: plain.bytes };
const liveControl = { task: live.task, stage: live.attempt, outer_stage: live.attempt, report_root: control.fixture_root, runner: plain };
const livePlan = { stage: live.attempt, category: live.category, operation: live.operation_id };
write('attempt-meta-03-108.json', live);
check(b.requireLiveReservation(liveControl, livePlan).sha256.length === 64, 'actual false reservation before launch');
reject(() => b.requireLiveReservation({ ...liveControl, outer_stage: 'other-01' }, livePlan), 'wrong outer binding');
reject(() => b.requireLiveReservation({ ...liveControl, runner: { ...plain, bytes: 0 } }, livePlan), 'wrong live runner');
reject(() => b.requireLiveReservation(liveControl, { ...livePlan, category: 'emulator_probe' }), 'wrong live category');
write('result-meta-03-108.json', { ok: true });
reject(() => b.requireLiveReservation(liveControl, livePlan), 'completed native stage cannot be launched again');
reject(() => b.requireFreshNativeNamespace(evidenceControl, vm, paths), '107 repeated name refused before guard');
for (const [stage, kind, category] of [['rtunit-01', 'runtimeunit', 'native_runtime_unit_test'], ['rtstage-01', 'runtimestage', 'runtime_stage']]) {
  const owner = kind === 'runtimestage' ? S : L;
  context.native_choices[stage] = { ...b.metadataRequest({ ...context, tools, work_root: owner }, stage), kind, category };
  const request = plan(stage, kind);
  check(request.argv.length === 5 && request.exec_set.length === 2 && request.work_root === owner, kind + ' exact Python request and writable owner');
}
const runtimeTransport = b.bootWslArgv(transportControl, plan('rtstage-01', 'runtimestage'), {});
check(runtimeTransport[runtimeTransport.indexOf('--work-root') + 1] === S && !runtimeTransport.includes('--read-only'), 'runtime staging writes only its S owner');
b.requireFreshNativeNamespace({ ...evidenceControl, task: 'GTOS-DSH-108' }, { ...meta, stage: 'meta-09' }, { nativeHostPath: (_ctx, p) => path.join(control.fixture_root, 'not-created', p.slice(1)) });
check(true, 'fresh namespace admits no old evidence');
check(assertions >= 64, 'regression count sanity');
const result = { ok: true, assertions, module: control.module, request_module: control.request_module,
  fixtures_are_test_only: true, WSL_started: false, guard_started: false, emulator_started: false, target_execution: false };
fs.writeFileSync(control.result_path, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify(result));
