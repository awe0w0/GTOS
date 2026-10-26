// GTOS-DSH-127 runner.
//
// No budgets. This runner carries no cap table, no attempt ceiling and no `assertCap`. The route table is used only to
// identify an action and to decide which executor owns it, exactly as AGENTS.goals-and-acceptance.md requires.
//
// What it does keep, because those are evidence rules and not quotas:
//   - the reservation is written complete:false before the freeze and before any launch
//   - the frozen helper bytes are written once and never edited in place
//   - the terminal result is written exclusively before the attempt is marked complete
//   - module, runner and operation subjects are pinned before and after the call
//   - a failed or incomplete attempt stays on disk and is never rewritten

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';

export const TASK = 'GTOS-DSH-127';
export const REPORT_ROOT = 'F:/GTOS-Chromium/artifacts/native-isolated-fixtures-current-evidence-and-runtime-delivery-20261026-a';

// Executor ownership. This is identity and permission routing, not a quota.
export const GUARD_ROUTES = Object.freeze([
  'source.view', 'native.metadata', 'runtime.stage', 'test.python', 'test.cpp.build',
  'test.cpp.run', 'assembly', 'object.compile', 'image.link', 'qemu.probe',
  'vm.checkpoint',
]);

export const PINNED = {
  node: { path: 'C:/Program Files/nodejs/node.exe', bytes: 86969160,
    sha256: '780f44f2c53c108bae261ada21a525b4bfe733c020ac85e41bfe94479090ac9b' },
};

const CHILD_PROCESS_FORBIDDEN = new Set(['export', 'archive', 'seal']);
const ALLOWED_IMPORTS = new Set(['node:fs', 'node:path', 'node:crypto', 'node:child_process', 'node:url']);

export function sha256(buffer) { return crypto.createHash('sha256').update(buffer).digest('hex'); }
export function hashFile(p) { const b = fs.readFileSync(p); return { bytes: b.length, sha256: sha256(b) }; }
export function pinOf(p) { return { path: p, ...hashFile(p) }; }
export function readJson(p) { return JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '')); }

export function writeNew(p, value) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  const text = typeof value === 'string' ? value : JSON.stringify(value, null, 2) + '\n';
  if (fs.existsSync(p)) {
    const existing = fs.readFileSync(p);
    if (existing.equals(Buffer.from(text, 'utf8'))) {
      return { path: p, bytes: existing.length, sha256: sha256(existing), unchanged: true };
    }
    throw new Error(`REFUSED: ${p} exists with different bytes`);
  }
  fs.writeFileSync(p, text, { encoding: 'utf8', flag: 'wx' });
  const h = hashFile(p);
  return { path: p, bytes: h.bytes, sha256: h.sha256, unchanged: false };
}

// The declared test-only subtrees. A fixture may live here and nowhere else.
const FIXTURE_PARENTS = [
  path.join(REPORT_ROOT, 'test-only'),
  '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-127-e759-isolated-replay/test-only',
];

// Validate a fixture root. A fixture is a place where destructive work is allowed, so the check is that it is inside a
// declared test-only subtree and not merely that it is absolute.
// The authority work root, as recorded in this packet's authority.
const AUTH_WORK_ROOT = (() => {
  try {
    const a = JSON.parse(fs.readFileSync(path.join(REPORT_ROOT, 'authority-127.json'), 'utf8'));
    return a.work_root;
  } catch { return null; }
})();

// A fixture work root on the Linux side is exactly <authority work_root>/test-only/<one segment>.
export function isFixtureWorkRoot(candidate, authorityWorkRoot) {
  if (typeof candidate !== 'string' || typeof authorityWorkRoot !== 'string') return false;
  const prefix = authorityWorkRoot.replace(/\/+$/, '') + '/test-only/';
  if (!candidate.startsWith(prefix)) return false;
  const rest = candidate.slice(prefix.length);
  if (rest === '' || rest.includes('/')) return false;
  return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(rest);
}

export function assertFixtureRoot(candidate) {
  if (candidate === undefined || candidate === null) return null;
  if (typeof candidate !== 'string' || candidate.trim() === '') {
    throw new Error('FIXTURE GATE: a fixture root must be a non-empty string');
  }
  if (!path.isAbsolute(candidate)) throw new Error('FIXTURE GATE: fixture root is relative: ' + candidate);
  const normalised = path.resolve(candidate);
  const ok = FIXTURE_PARENTS.some((parent) => {
    const base = path.resolve(parent);
    return normalised.toLowerCase().startsWith((base + path.sep).toLowerCase());
  });
  if (!ok) {
    throw new Error('FIXTURE GATE: ' + normalised + ' is not inside a declared test-only subtree ('
      + FIXTURE_PARENTS.map((p) => path.resolve(p)).join(' or ') + ')');
  }
  return normalised;
}

export function assertOwnedPath(candidate, label, roots) {
  if (typeof candidate !== 'string' || candidate.trim() === '') throw new Error(`PATH GATE: ${label} is empty`);
  if (!path.isAbsolute(candidate)) throw new Error(`PATH GATE: ${label} is relative`);
  const normalised = path.resolve(candidate);
  const inside = (roots || [REPORT_ROOT]).some((root) => {
    const base = path.resolve(root);
    return normalised.toLowerCase().startsWith((base + path.sep).toLowerCase());
  });
  if (!inside) throw new Error(`PATH GATE: ${label} '${normalised}' is outside every owned root`);
  return normalised;
}

export function assertRunnerPin(runnerPath, fixtureRoot) {
  const full = path.resolve(runnerPath);
  // A fixture carries its own frozen copy and its own pin, so the allowed frozen roots are the packet's and the fixture's.
  const frozenRoots = [path.resolve(REPORT_ROOT, 'frozen-runners')];
  if (fixtureRoot) frozenRoots.push(path.resolve(fixtureRoot, 'frozen-runners'));
  const owner = frozenRoots.find((frozenRoot) =>
    full.toLowerCase().startsWith((frozenRoot + path.sep).toLowerCase()));
  if (!owner) {
    throw new Error(`RUNNER PIN: '${full}' is not under a frozen-runners directory (`
      + frozenRoots.join(' or ') + '); only a frozen copy may run');
  }
  const revision = path.basename(full, path.extname(full));
  // The pin lives beside the frozen root that owns the runner.
  const pinPath = path.join(path.dirname(owner), `runner-pin-127-${revision}.json`);
  if (!fs.existsSync(pinPath)) throw new Error(`RUNNER PIN: no pin at ${pinPath}`);
  const h = hashFile(full);
  const pin = readJson(pinPath);
  if (pin.sha256 !== h.sha256 || pin.bytes !== h.bytes) {
    throw new Error(`RUNNER PIN: the frozen runner changed: now ${h.bytes} B ${h.sha256}, pinned ${pin.bytes} B ${pin.sha256}`);
  }
  return { runner_path: full, bytes: h.bytes, sha256: h.sha256, revision, pin_path: pinPath,
    pin_sha256: hashFile(pinPath).sha256, frozen: true };
}

export function assertPosthash(runnerPath, expectedSha, expectedBytes) {
  const h = hashFile(runnerPath);
  if (h.sha256 !== expectedSha || h.bytes !== expectedBytes) {
    throw new Error(`RUNNER POSTHASH: the frozen runner changed during the stage: now ${h.bytes} B ${h.sha256}`);
  }
  return { unchanged: true, bytes: h.bytes, sha256: h.sha256 };
}

export function assertModuleGate(frozenPath, route) {
  const text = fs.readFileSync(frozenPath, 'utf8');
  const found = new Set();
  for (const m of text.matchAll(/from\s+'([^']+)'/g)) found.add(m[1]);
  for (const m of text.matchAll(/import\s+'([^']+)'/g)) found.add(m[1]);
  for (const m of text.matchAll(/require\(\s*'([^']+)'\s*\)/g)) found.add(m[1]);
  const forbidden = new Set(['worker_threads', 'node:http', 'node:https', 'node:net', 'node:vm']);
  if (CHILD_PROCESS_FORBIDDEN.has(route)) forbidden.add('child_process');
  const bad = [...found].filter((spec) => !ALLOWED_IMPORTS.has(spec));
  if (bad.length > 0) throw new Error(`MODULE GATE: '${frozenPath}' imports ${bad.join(', ')}, outside the allowlist`);
  const hits = [...forbidden].filter((token) => text.includes(token));
  if (/\beval\s*\(/.test(text)) hits.push('eval(');
  if (/\bnew\s+Function\b/.test(text)) hits.push('new Function');
  if (hits.length > 0) throw new Error(`MODULE GATE: '${frozenPath}' mentions ${hits.join(', ')}`);
  return { imports: [...found], child_process_allowed: !CHILD_PROCESS_FORBIDDEN.has(route) };
}

export function freezeHelper(draftRelative, stage, base) {
  const owner = base || REPORT_ROOT;
  const draftPath = path.join(owner, draftRelative);
  if (!fs.existsSync(draftPath)) throw new Error(`draft missing: ${draftPath}`);
  const frozenPath = path.join(owner, 'frozen-helpers', `${stage}.mjs`);
  if (fs.existsSync(frozenPath)) throw new Error(`FREEZE: ${frozenPath} already exists; written once`);
  const draftBytes = fs.readFileSync(draftPath);
  fs.mkdirSync(path.dirname(frozenPath), { recursive: true });
  fs.writeFileSync(frozenPath, draftBytes, { flag: 'wx' });
  const frozenBytes = fs.readFileSync(frozenPath);
  if (!draftBytes.equals(frozenBytes)) throw new Error(`FREEZE: not byte-identical: ${frozenPath}`);
  return { frozen_path: frozenPath, frozen_name: `${stage}.mjs`,
    frozen_bytes: frozenBytes.length, frozen_sha256: sha256(frozenBytes),
    draft_path: draftPath, draft_sha256: sha256(draftBytes),
    byte_identical: true, executes: 'the frozen copy, not the draft' };
}

export function spawnPinned(exe, argv, options) {
  const opts = options || {};
  // The timeout exists to reclaim a process, not to ration work.
  const timeout = opts.timeoutMs || 600000;
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(exe, argv, { shell: false, windowsHide: true, cwd: opts.cwd || REPORT_ROOT });
    } catch (error) {
      resolve({ started: false, exit_code: null, timed_out: false,
        launch_error: String(error && error.message ? error.message : error), stdout: '', stderr: '', argv });
      return;
    }
    let stdout = '', stderr = '', settled = false, timedOut = false;
    const finish = (record) => { if (!settled) { settled = true; resolve(record); } };
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', (chunk) => { stdout += chunk; });
    child.stderr.on('data', (chunk) => { stderr += chunk; });
    child.on('error', (error) => {
      finish({ started: false, exit_code: null, timed_out: false,
        launch_error: String(error && error.message ? error.message : error), stdout, stderr, argv });
    });
    child.on('close', (code) => {
      finish({ started: true, exit_code: timedOut ? null : code, timed_out: timedOut,
        launch_error: null, stdout, stderr, argv });
    });
    const timer = setTimeout(() => { timedOut = true; try { child.kill('SIGKILL'); } catch { } }, timeout);
    child.on('close', () => clearTimeout(timer));
  });
}

// The route table is read for identity and executor ownership only. It never gates work.
export function routeFor(routeId) {
  const table = readJson(path.join(REPORT_ROOT, 'execution-identities-127.json'));
  const route = table.routes[routeId];
  if (!route) throw new Error(`ROUTE: '${routeId}' is not a route in this packet`);
  return { id: routeId, mode: route.mode, executor: route.executor };
}

const STAGE_REGISTRY = 'stage-registry-127.jsonl';

// Every stage name ever claimed in this root, in the order it was claimed. Append only.
export function readStageRegistry(base) {
  const p = path.join(base || REPORT_ROOT, STAGE_REGISTRY);
  if (!fs.existsSync(p)) return { path: p, rows: [], names: new Set() };
  const rows = fs.readFileSync(p, 'utf8').split(/\r?\n/).filter(Boolean).map((line, i) => {
    let r = null;
    try { r = JSON.parse(line); } catch { throw new Error('REGISTRY: line ' + (i + 1) + ' is not JSON'); }
    if (typeof r.stage !== 'string' || !r.stage) throw new Error('REGISTRY: line ' + (i + 1) + ' has no stage');
    return r;
  });
  return { path: p, rows, names: new Set(rows.map((r) => r.stage)) };
}

// Claim a stage name. The claim is durable and precedes the freeze, so the name is spent the moment it is asked for.
function claimStage(base, stage, detail) {
  const registry = readStageRegistry(base);
  if (registry.names.has(stage)) {
    const first = registry.rows.find((r) => r.stage === stage);
    const e = new Error('STAGE REGISTRY: \'' + stage + '\' was already claimed at ' + first.at
      + ' (attempt ' + first.attempt + '); an executed stage name is never reused, and deleting its attempt file does '
      + 'not free it');
    e.registry_refusal = { stage, first_claim: first };
    throw e;
  }
  const row = { stage, attempt: detail.attempt, task: TASK, route: detail.route,
    parent_attempt: detail.parent_attempt ?? null,
    retry_of: detail.retry_of ?? null,
    failure_cause: detail.failure_cause ?? null,
    at: new Date().toISOString() };
  fs.appendFileSync(registry.path, JSON.stringify(row) + '\n', { encoding: 'utf8' });
  return row;
}

export async function invokeTransportLifecycle(options) {
  const { stage, route, executor, root, runnerPin, declaredOutputs, expectedOutputs,
    frozen, launch, preLaunch, routeGate = null,
    parent_attempt = null, retry_of = null, failure_cause = null } = options;
  const base = root || REPORT_ROOT;
  const recordPath = path.join(base, `attempt-${stage}-127.json`);
  const resultPath = path.join(base, `result-${stage}-127.json`);

  // The durable claim is the first thing that happens. A name that has been claimed is refused as a name, before any
  // question about what else survived on disk. If this throws, no reservation, no freeze and no child has happened.
  const claim = claimStage(base, stage, { attempt: stage, route, parent_attempt, retry_of, failure_cause });

  if (fs.existsSync(recordPath)) {
    throw new Error(`stage '${stage}' is already reserved or executed; an executed stage name is never reused`);
  }
  if (fs.existsSync(resultPath)) {
    throw new Error(`RESULT GATE: a terminal result already exists at ${resultPath}; it is never overwritten`);
  }

  const reservation = {
    task: TASK, attempt: stage, complete: false, reservation: true,
    stage_claim: claim,
    parent_attempt, retry_of, failure_cause,
    executor, route,
    route_id: routeGate?.id ?? null,
    route_mode: routeGate?.mode ?? null,
    reserved_utc: new Date().toISOString(),
    runner_frozen: runnerPin.frozen, runner_path: runnerPin.runner_path,
    runner_revision: runnerPin.revision, runner_bytes: runnerPin.bytes, runner_sha256: runnerPin.sha256,
    runner_pin_path: runnerPin.pin_path, runner_pin_sha256: runnerPin.pin_sha256,
    declared_outputs: declaredOutputs || [],
    lifecycle: 'reservation_written_complete_false',
    note: 'written before the freeze and before any launch; complete only after a terminal result exists. '
      + 'There is no attempt budget in this packet.',
  };
  const reservationPin = writeNew(recordPath, reservation);
  const observation = { record_complete_at_start: false, result_absent_at_start: true,
    reservation_sha256: reservationPin.sha256 };

  let launchResult = null, launchError = null;
  const started = Date.now();
  try {
    if (typeof preLaunch === 'function') await preLaunch();
    launchResult = await launch();
  } catch (error) {
    launchError = String(error && error.message ? error.message : error);
  }
  const elapsed = Date.now() - started;

  const transportStarted = launchResult ? launchResult.transport_started === true : false;
  const transportExit = launchResult ? launchResult.transport_exit_code : null;
  const hostHelperStarted = launchResult ? launchResult.host_helper_started === true : false;
  const timedOut = launchResult ? launchResult.timed_out === true : false;
  if (launchResult && launchResult.launch_error) launchError = launchResult.launch_error;

  const expectedState = [];
  let allPresent = true;
  for (const one of expectedOutputs || []) {
    const present = fs.existsSync(one) && fs.statSync(one).isFile();
    if (!present) allPresent = false;
    expectedState.push({ path: one, present, bytes: present ? fs.statSync(one).size : null });
  }

  const prelaunchRefused = launchError !== null && !transportStarted;
  let helperPosthash = null, runnerPosthash = null, validationError = null;
  let subjectsUnchanged = routeGate === null ? null : false;
  try {
    if (frozen.frozen_path && frozen.frozen_sha256 && frozen.frozen_bytes !== null) {
      helperPosthash = hashFile(frozen.frozen_path);
    }
    runnerPosthash = hashFile(runnerPin.runner_path);
    if (routeGate !== null) {
      for (const subject of routeGate.subjects || []) {
        const now = hashFile(subject.path);
        if (now.bytes !== subject.bytes || now.sha256 !== subject.sha256) {
          throw new Error('SUBJECT: ' + subject.path + ' changed during the stage');
        }
      }
      subjectsUnchanged = true;
    }
  } catch (error) {
    validationError = String(error && error.message ? error.message : error);
  }
  const helperUnchanged = helperPosthash === null ? null
    : helperPosthash.sha256 === frozen.frozen_sha256 && helperPosthash.bytes === frozen.frozen_bytes;
  const runnerUnchanged = runnerPosthash === null ? null
    : runnerPosthash.sha256 === runnerPin.sha256 && runnerPosthash.bytes === runnerPin.bytes;
  const ok = launchError === null && validationError === null && !timedOut && transportStarted
    && hostHelperStarted && transportExit === 0 && allPresent && helperUnchanged === true
    && runnerUnchanged === true && (routeGate === null || subjectsUnchanged === true);

  const resultDoc = {
    task: TASK, attempt: stage, route, executor,
    route_id: routeGate?.id ?? null, route_mode: routeGate?.mode ?? null,
    route_subjects_unchanged: subjectsUnchanged,
    lifecycle: 'terminal_result_written_before_complete',
    terminal_result_written_before_complete: true,
    reservation_path: recordPath, reservation_sha256_at_start: reservationPin.sha256,
    prelaunch_refused: prelaunchRefused,
    transport_started: transportStarted, transport_exit_code: transportExit,
    host_helper_started: hostHelperStarted, timed_out: timedOut,
    launch_error: launchError,
    project_child_started: null,
    project_child_note: 'OS process classification only; guest execution is determined separately from emulator evidence',
    process_group_measured: false,
    process_group_note: 'this orchestrator owns no process group and does not claim one is empty',
    child_evidence_fabricated: false,
    frozen_name: frozen.frozen_name, frozen_sha256: frozen.frozen_sha256,
    frozen_bytes: frozen.frozen_bytes,
    frozen_posthash_sha256: helperPosthash ? helperPosthash.sha256 : null,
    frozen_unchanged: helperUnchanged,
    runner_posthash: runnerPosthash, runner_unchanged: runnerUnchanged,
    validation_error: validationError,
    expected_outputs_state: expectedState, expected_outputs_all_present: allPresent,
    stdout_tail: launchResult ? String(launchResult.stdout || '').slice(-8000) : '',
    stderr_tail: launchResult ? String(launchResult.stderr || '').slice(-8000) : '',
    stdout_bytes: launchResult ? Buffer.byteLength(String(launchResult.stdout || ''), 'utf8') : 0,
    stderr_bytes: launchResult ? Buffer.byteLength(String(launchResult.stderr || ''), 'utf8') : 0,
    host_launch_ms: elapsed, observation, ok,
    ok_means: 'the transport finished, the helper exited zero, every expected output exists and the frozen helper '
      + 'did not change. It says nothing about any project behaviour.',
  };
  const resultPin = writeNew(resultPath, resultDoc);

  const completed = { ...reservation, complete: true, lifecycle: 'completed_after_terminal_result',
    completed_utc: new Date().toISOString(),
    terminal_result: { path: resultPin.path, bytes: resultPin.bytes, sha256: resultPin.sha256 },
    result_ok: ok, launch_error: launchError, host_helper_started: hostHelperStarted,
    project_child_started: null,
    frozen: { frozen_path: frozen.frozen_path, frozen_name: frozen.frozen_name,
      frozen_bytes: frozen.frozen_bytes, frozen_sha256: frozen.frozen_sha256 } };

  if (!fs.existsSync(resultPath)) {
    throw new Error(`LIFECYCLE: refusing to complete an attempt whose terminal result does not exist: ${resultPath}`);
  }
  const current = readJson(recordPath);
  if (current.complete === true) {
    throw new Error(`LIFECYCLE: the attempt is already complete: ${recordPath}`);
  }
  const temp = `${recordPath}.tmp-complete`;
  if (fs.existsSync(temp)) throw new Error(`LIFECYCLE: a stale completion temp file exists: ${temp}`);
  fs.writeFileSync(temp, JSON.stringify(completed, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  fs.renameSync(temp, recordPath);
  const completionPin = hashFile(recordPath);

  return { stage, reservation: reservationPin, result: resultPin, completion: completionPin,
    ok, launch_error: launchError, transport_started: transportStarted,
    transport_exit_code: transportExit, host_helper_started: hostHelperStarted,
    timed_out: timedOut, project_child_started: null,
    expected_outputs_all_present: allPresent, observation, result_written_before_complete: true };
}

export async function invokeStage(spec) {
  const stage = spec.stage;
  // A retry declares which attempt it follows and why. The fields travel on the spec, which is the record the runner reads.
  const retryIdentity = {
    parent_attempt: spec.parent_attempt ?? null,
    retry_of: spec.retry_of ?? null,
    failure_cause: spec.failure_cause ?? null,
  };
  const route = routeFor(spec.route);
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) {
    throw new Error(`stage name must be <letter then letters/digits>-NN, got '${stage}'`);
  }
  // A fixture context owns every mutable path of the stage. Without one the packet's own root is the context, which is
  // what every business stage uses.
  const fixtureRoot = assertFixtureRoot(spec.fixture_root);
  const root = spec.root || REPORT_ROOT;
  const stateRoot = fixtureRoot || root;
  // A fixture owns its registry AND its guard ledger. The registry lives on the host side of the fixture and the guard
  // writes its ledger on the Linux side, so they cannot be the same path; they must be the same namespace. If the guard is
  // told to write somewhere else, one stage name can be claimed in one place and recorded in another, which is exactly how
  // a name gets used twice.
  if (fixtureRoot) {
    const declared = spec.control && spec.control.bridge ? spec.control.bridge.work_root : null;
    if (typeof declared !== 'string' || !declared.startsWith('/')) {
      throw new Error('FIXTURE GATE: a fixture stage must declare its bridge work_root as an absolute Linux path');
    }
    if (!isFixtureWorkRoot(declared, AUTH_WORK_ROOT)) {
      throw new Error('FIXTURE GATE: the guard ledger root ' + declared + ' is not a test-only subtree of the authority '
        + 'work root ' + AUTH_WORK_ROOT);
    }
    const fixtureSegment = path.basename(path.resolve(fixtureRoot));
    const linuxSegment = declared.split('/').filter(Boolean).pop();
    if (fixtureSegment !== linuxSegment) {
      throw new Error('FIXTURE GATE: the fixture registry namespace is ' + fixtureSegment + ' but the guard ledger would be '
        + 'written under ' + linuxSegment + '; a fixture owns both or neither');
    }
  }
  const executor = route.executor;
  if (spec.executor && spec.executor !== executor) throw new Error('EXECUTOR GATE: route/executor mismatch');
  const runnerPin = assertRunnerPin(spec.runnerPath || process.argv[1], fixtureRoot);
  const limit = spec.limit || 600;
  const expected = (spec.expected_outputs || []).slice();

  const frozen = { frozen_path: null, frozen_name: stage + '.mjs', frozen_bytes: null, frozen_sha256: null };
  let controlPath = null;
  let nodePin = null;
  const stdoutPath = path.join(stateRoot, 'stage-output', stage + '.out.txt');
  const stderrPath = path.join(stateRoot, 'stage-output', stage + '.err.txt');
  const preLaunch = async () => {
    if (spec.control?.bridge?.binding_module) {
      const binding = spec.control.bridge.binding_module;
      const now = pinOf(binding.path);
      if (now.bytes !== binding.bytes || now.sha256 !== binding.sha256) {
        throw new Error('NATIVE BINDING: module pin');
      }
      const mod = await import(new URL('file:///' + binding.path.replace(/\\/g, '/')).href);
      mod.assertNativeStageIdentity(spec);
    }
    nodePin = pinOf(PINNED.node.path);
    if (nodePin.bytes !== PINNED.node.bytes || nodePin.sha256 !== PINNED.node.sha256) {
      throw new Error('HOST TOOL PIN: node drifted');
    }
    Object.assign(frozen, freezeHelper(spec.helper, stage, stateRoot));
    assertModuleGate(frozen.frozen_path, spec.route);
    if (spec.control) {
      controlPath = path.join(stateRoot, 'stage-output', stage + '.control.json');
      assertOwnedPath(controlPath, 'control file for ' + stage,
        fixtureRoot ? [REPORT_ROOT, fixtureRoot] : [REPORT_ROOT, root]);
      if (fs.existsSync(controlPath)) throw new Error('CONTROL GATE: ' + controlPath + ' already exists');
      writeNew(controlPath, spec.control);
    }
    for (const p of [stdoutPath, stderrPath]) {
      if (fs.existsSync(p)) throw new Error('STAGE OUTPUT: ' + p + ' already exists');
    }
    fs.mkdirSync(path.dirname(stdoutPath), { recursive: true });
    fs.writeFileSync(stdoutPath, '', { flag: 'wx' });
    fs.writeFileSync(stderrPath, '', { flag: 'wx' });
  };
  const launch = async () => {
    const argv = controlPath ? [frozen.frozen_path, controlPath] : [frozen.frozen_path];
    const rec = await spawnPinned(nodePin.path, argv, { timeoutMs: limit * 1000 });
    fs.writeFileSync(stdoutPath, rec.stdout || '', { flag: 'w' });
    fs.writeFileSync(stderrPath, rec.stderr || '', { flag: 'w' });
    return { transport_started: rec.started, transport_exit_code: rec.exit_code,
      host_helper_started: rec.started, timed_out: rec.timed_out,
      launch_error: rec.launch_error, stdout: rec.stdout, stderr: rec.stderr };
  };
  const routeGate = { id: route.id, mode: route.mode, subjects: spec.route_subjects || [] };
  const outcome = await invokeTransportLifecycle({
    stage, route: route.id, executor, root: stateRoot, runnerPin, routeGate,
    declaredOutputs: expected, expectedOutputs: expected, frozen, launch, preLaunch,
    parent_attempt: retryIdentity.parent_attempt,
    retry_of: retryIdentity.retry_of,
    failure_cause: retryIdentity.failure_cause,
  });
  console.log('stage=' + stage + ' ok=' + outcome.ok + ' result=' + outcome.result.path);
  return outcome;
}

const isMain = process.argv[1]
  && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
if (isMain) {
  const spec = readJson(process.argv[2]);
  invokeStage(spec).catch((error) => {
    console.error(String(error && error.stack ? error.stack : error));
    process.exitCode = 1;
  });
}
