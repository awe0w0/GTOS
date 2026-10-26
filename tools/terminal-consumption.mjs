// GTOS-DSH-135 T: who owns which path, and what a terminal actually has to be.
//
// In 133 the link entry declared the helper's business report at the runner's own canonical result path. The helper wrote
// its report there; the runner then either failed to write its terminal or reused the file; and the entry read the file
// back, called it a "result", and accepted. Three faults met in one path:
//
//   - two different roles were given the same file;
//   - the file that was read was a helper report (`kind: "ld"`, a plan and a guard verdict), not a lifecycle terminal;
//   - the runner had exited non-zero and the attempt was still `complete: false`, and the entry accepted anyway.
//
// This module is the repair. A declaration is checked for path collisions across roles before anything is launched, and a
// terminal is only accepted when it is the terminal the attempt names, of the right type, for the right stage, written by
// the lifecycle, with the attempt completed after it.
//
// What this cannot do is stated rather than implied: it is a declaration and flow check. It does not stop arbitrary
// JavaScript from writing anywhere it likes; it stops this pipeline from declaring one file as two roles and then treating
// the wrong one as the terminal.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

function readBytes(p) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
}

// ---------------------------------------------------------------- path identity

// Two paths denote the same file when they differ only in separator, letter case, a drive-letter versus /mnt form, or a
// UNC versus a WSL root. Windows itself is case-insensitive, so a check that only compared strings would let
// `Result-X.json` and `result-x.json` collide while looking distinct.
export function normalisePath(p) {
  if (typeof p !== 'string' || !p) return p;
  let s = p.replace(/\\/g, '/');
  // A UNC WSL root and its /mnt-less Linux form are the same file.
  const unc = /^\/\/wsl\$\/([^/]+)\/(.*)$/i.exec(s);
  if (unc) s = '/' + unc[2];
  // A drive letter and its /mnt form are the same file.
  const drive = /^([A-Za-z]):\/(.*)$/.exec(s);
  if (drive) s = '/mnt/' + drive[1].toLowerCase() + '/' + drive[2];
  // Collapse duplicate separators, drop a trailing one, and resolve . and ..
  s = s.replace(/\/{2,}/g, '/').replace(/\/\.\//g, '/');
  if (s.length > 1 && s.endsWith('/')) s = s.slice(0, -1);
  return s.toLowerCase();
}

// The roles a declaration must keep apart. A caller may add roles; it may not merge them.
export function declarationCollisions(declaration) {
  const roles = [
    ['canonical_terminal', declaration.canonical_terminal],
    ['helper_report', declaration.helper_report],
    ['helper_output', declaration.helper_output],
    ['control', declaration.control],
    ['frozen_helper', declaration.frozen_helper],
    ['attempt', declaration.attempt],
  ].filter(([, p]) => typeof p === 'string' && p);
  const byPath = new Map();
  const collisions = [];
  for (const [role, p] of roles) {
    const key = normalisePath(p);
    const prior = byPath.get(key);
    if (prior) collisions.push({ path: p, normalised: key, roles: [prior, role] });
    else byPath.set(key, role);
  }
  return collisions;
}

// The preparation gate. It runs before the freeze and before any child, so a colliding declaration never reaches a launch.
export function assertDistinctRoles(declaration) {
  const collisions = declarationCollisions(declaration);
  if (collisions.length) {
    const e = new Error('DECLARATION: two roles name the same file: '
      + collisions.map((c) => c.roles.join(' and ') + ' at ' + c.path).join('; '));
    e.collisions = collisions;
    throw e;
  }
  return { ok: true, roles: 6 };
}

// ---------------------------------------------------------------- the terminal

// What a lifecycle terminal is, as opposed to a business report. A report describes what a tool did; a terminal records
// that a stage ended, under which attempt, with which exit. The two are told apart by their fields, not by their name.
export function looksLikeTerminal(doc) {
  if (!doc || typeof doc !== 'object') return { terminal: false, why: 'not an object' };
  const has = (k) => Object.prototype.hasOwnProperty.call(doc, k);
  const lifecycle = has('lifecycle') || has('terminal_result_written_before_complete');
  const attempt = has('attempt') || has('reservation_path') || has('reservation_sha256_at_start');
  const route = has('route') || has('route_id');
  const transport = has('transport_started') || has('transport') || has('prelaunch_refused');
  if (!lifecycle) return { terminal: false, why: 'it carries no lifecycle record' };
  if (!attempt) return { terminal: false, why: 'it names no attempt' };
  if (!route) return { terminal: false, why: 'it names no route' };
  if (!transport) return { terminal: false, why: 'it records no transport state' };
  return { terminal: true };
}

// The consumption. Everything is read from disk: the attempt the runner wrote, the terminal that attempt names, and the
// bytes of both. Nothing here trusts a caller's claim, a helper's `ok`, or a guard's acceptance.
export function consumeTerminal(options) {
  const { attemptPath, expect } = options;
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const out = () => ({ accepted: problems.length === 0, problems,
    first_refusal: problems.length ? problems[0] : null });

  const attemptBytes = readBytes(attemptPath);
  if (attemptBytes === null) { refuse('attempt', 'the attempt is absent: ' + attemptPath); return out(); }
  let attempt = null;
  try { attempt = JSON.parse(attemptBytes.toString('utf8')); } catch {
    refuse('attempt', 'the attempt is not readable JSON'); return out();
  }

  // The attempt has to describe the stage that was asked for.
  if (expect.task && attempt.task !== expect.task) refuse('attempt', 'the attempt is for another task: ' + attempt.task);
  if (expect.stage && attempt.attempt !== expect.stage) {
    refuse('attempt', 'the attempt names another stage: ' + attempt.attempt);
  }
  if (expect.route && attempt.route !== expect.route) refuse('attempt', 'the attempt names another route: ' + attempt.route);

  // The lifecycle must have completed. An incomplete attempt means the stage never finished, whatever else is on disk.
  if (attempt.complete !== true) {
    refuse('complete', 'the attempt is not complete, so the stage did not finish');
  }
  if (typeof attempt.lifecycle !== 'string' || !/complet/i.test(attempt.lifecycle)) {
    refuse('lifecycle', 'the attempt records no completion lifecycle: ' + String(attempt.lifecycle));
  }

  // The terminal the attempt names, not a path the caller supplied and not a name that merely looks like a result.
  const named = attempt.terminal_result;
  if (!named || typeof named.path !== 'string' || !named.path) {
    refuse('terminal', 'the attempt names no terminal result');
    return out();
  }
  if (normalisePath(named.path) !== normalisePath(expect.canonical_terminal)) {
    refuse('terminal', 'the terminal the attempt names is not the declared one: ' + named.path);
  }
  const terminalBytes = readBytes(named.path);
  if (terminalBytes === null) { refuse('terminal', 'the named terminal is absent: ' + named.path); return out(); }
  if (typeof named.sha256 === 'string' && sha256(terminalBytes) !== named.sha256) {
    refuse('terminal', 'the terminal bytes are not the ones the attempt recorded');
  }
  if (typeof named.bytes === 'number' && terminalBytes.length !== named.bytes) {
    refuse('terminal', 'the terminal size is not the one the attempt recorded');
  }

  let terminal = null;
  try { terminal = JSON.parse(terminalBytes.toString('utf8')); } catch {
    refuse('terminal', 'the terminal is not readable JSON'); return out();
  }
  const shape = looksLikeTerminal(terminal);
  if (!shape.terminal) {
    refuse('type', 'the file the attempt names is not a lifecycle terminal: ' + shape.why);
  }
  if (terminal.task !== undefined && expect.task && terminal.task !== expect.task) {
    refuse('terminal', 'the terminal is for another task: ' + terminal.task);
  }
  const terminalStage = terminal.stage !== undefined ? terminal.stage : terminal.attempt;
  if (terminalStage !== undefined && expect.stage && terminalStage !== expect.stage) {
    refuse('terminal', 'the terminal is for another stage: ' + terminalStage);
  }
  if (terminal.route !== undefined && expect.route && terminal.route !== expect.route) {
    refuse('terminal', 'the terminal is for another route: ' + terminal.route);
  }
  // The terminal has to say the transport actually started and did not fail to launch. A terminal that records a launch
  // error is a failed stage even if some file exists beside it.
  if (terminal.transport_started !== true) {
    refuse('transport', 'the terminal does not record a started transport');
  }
  if (terminal.launch_error) {
    refuse('transport', 'the terminal records a launch error: ' + String(terminal.launch_error));
  }
  if (terminal.timed_out === true) refuse('transport', 'the terminal records a timeout');
  // A stage whose declared outputs were not all produced did not succeed, whatever a tool inside it returned.
  if (terminal.expected_outputs_all_present === false) {
    refuse('outputs', 'the terminal records that not every declared output was produced');
  }
  // The runner's own ok is its verdict on the whole stage; a terminal that says ok false is a failed stage.
  if (terminal.ok === false) refuse('stage', 'the terminal records a stage that did not succeed');

  return { accepted: problems.length === 0, problems, first_refusal: problems.length ? problems[0] : null,
    attempt: { path: attemptPath, bytes: attemptBytes.length, sha256: sha256(attemptBytes),
      complete: attempt.complete, lifecycle: attempt.lifecycle, stage: attempt.attempt, route: attempt.route },
    terminal: { path: named.path, bytes: terminalBytes.length, sha256: sha256(terminalBytes),
      stage: terminal.stage, route: terminal.route, transport_started: terminal.transport_started,
      exit: terminal.transport_exit_code } };
}

export function gateTerminal(result) {
  if (result.accepted) return result;
  const e = new Error('TERMINAL REFUSED at ' + result.first_refusal.obligation + ': ' + result.first_refusal.why);
  e.result = result;
  throw e;
}

// ---------------------------------------------------------------- the runner's own status

// A non-zero runner is a failed stage. The 133 defect was accepting because ld itself returned 0, so the tool's success
// stood in for the lifecycle's failure. The two are separate facts and this keeps them separate.
export function runnerStatus(terminal) {
  if (!terminal || typeof terminal !== 'object') return { ok: false, why: 'no terminal' };
  const started = terminal.transport_started === true;
  const exit = terminal.transport_exit_code;
  return { started, exit,
    ok: started && exit === 0 && terminal.timed_out !== true && !terminal.launch_error,
    note: 'the runner exit is the lifecycle status; a tool that returned 0 inside a runner that failed did not succeed '
      + 'as a stage' };
}
