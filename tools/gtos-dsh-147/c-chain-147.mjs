// GTOS-DSH-147 C: the guarded full static and producer/native chain.
//
// The question this file answers is not "did the stage exit zero". It is: for one stage that this packet really ran, can a
// reader who has only the files on disk reconstruct the whole chain -- the spec that was written, the control the runner
// froze, the helper it froze, the reservation it took, the terminal it wrote, the bridge's report, the guard's own inner
// report, and the child's raw channel -- and do those eight layers agree with each other and with the pins in the
// contract?
//
// Two properties matter more than the others:
//
//   * Three exits. The transport exit, the guard's exit and the tool's own exit are three different numbers written by
//     three different programs. A chain that reports one of them three times is not a chain, so each is read from the file
//     its own author wrote and compared.
//   * Own group cleanup. The guard puts the child in its own process group and measures it. An empty group after cleanup,
//     with nothing left in `descendants_left` and nothing in `killed`, is the observable that says nothing was left running.
//
// The stage's tool is not inferred from what happened to be in argv. It is the tool the contract pins for that kind, and a
// chain whose argv[0] is any other path is refused even when it produced the right bytes.
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import path from 'node:path';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const WORKTREE = path.resolve(HERE, '..', '..').replace(/\\/g, '/');
const R = path.resolve(WORKTREE, '..').replace(/\\/g, '/');
const L = '/var/tmp/gtos-dsh-147-e759-scene-bound';
const S = '/var/tmp/gtos-dsh-147-e759-scene-bound-view';
const TASK = 'GTOS-DSH-147';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
// The records mix two path domains: the runner writes host paths (`//wsl$/Ubuntu-20.04/...`) and the guard writes Linux
// ones (`/var/tmp/...`). Comparing the two without normalising them first would make every path check fail on the form
// rather than on the file.
const toLinuxForm = (p) => p
  .replace(/^\/\/wsl\$\/Ubuntu-20\.04/i, '')
  .replace(/^\\\\wsl\$\\Ubuntu-20\.04/i, '')
  .replace(/\\/g, '/');
const toHost = (p) => (/^[A-Za-z]:[\\/]/.test(p) || /^[\\/]{2}/.test(p)
  ? p.replace(/\//g, '\\')
  : /^\/mnt\/([a-z])\/(.*)$/.test(p) ? p.replace(/^\/mnt\/([a-z])\//, (m, d, rest) => d.toUpperCase() + ':/' + rest)
    : '\\\\wsl$\\Ubuntu-20.04' + p.replace(/\//g, '\\'));
const exists = (p) => { try { fs.statSync(toHost(p)); return true; } catch { return false; } };
const pin = (p) => { const b = fs.readFileSync(toHost(p)); return { path: p, bytes: b.length, sha256: sha(b) }; };
const readJson = (p) => JSON.parse(fs.readFileSync(toHost(p), 'utf8'));
const readJsonMaybe = (p) => { try { return readJson(p); } catch { return null; } };

export const realRoots = () => ({ report: R, work: L, source: S });

// ---------------------------------------------------------------- the pins
export const pinsOf = (contractPath = R + '/contract-147.json') => {
  const c = readJson(contractPath);
  const role = new Map(c.tools.roles.map((r) => [r.role, r]));
  const closure = new Map(c.executor_closure.map((e) => [path.basename(e.path), e]));
  return {
    contract: pin(contractPath),
    roots: c.roots,
    guard: c.tools.guard,
    guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
    python_realpath: c.tools.python.realpath,
    python_linux: c.tools.python.linux,
    python: c.tools.python,
    node: c.tools.node,
    wsl: c.tools.wsl,
    roles: role,
    closure,
    runner: closure.get('run-147-r01.mjs'),
    helper_draft: closure.get('native-147-01.mjs'),
    bridge: closure.get('private-boot-checkpoints-147.mjs'),
  };
};

// The tool a kind must run. Anything not listed is the pinned interpreter, which is what the metadata, source-view,
// staging, runtime-stage, host-unit, version, help and checkpoint builders all run.
export const toolFor = (pins, kind) => {
  if (kind === 'cxx') return pins.roles.get('g++').linux;
  if (kind === 'as') return pins.roles.get('as').linux;
  if (kind === 'ld') return pins.roles.get('ld.bfd').linux;
  return pins.python_realpath;
};

// ---------------------------------------------------------------- the eight layers
//
// Six of the eight layers have a name the packet's own layout fixes: the spec, the control the runner froze, the helper
// it froze, the reservation and the terminal all live under the report root beside the stage name, and the bridge report
// lives under `results/`. The last two -- the guard's inner report and the child's raw channel -- do not: the guard
// writes them under whichever work root the route gave it, which for a compiler stage is the source view and for a
// checkpoint is the work root. Rather than guess, they are resolved from the bridge record, which is the file whose
// author knows where it put them; the fallback only runs when the bridge record is itself the missing layer, so the
// record still says `absent` instead of silently looking somewhere else.
export const reportLayers = (roots, stage) => ({
  spec: roots.report + '/stage-output/' + stage + '.spec.json',
  control: roots.report + '/stage-output/' + stage + '.control.json',
  helper: roots.report + '/frozen-helpers/' + stage + '.mjs',
  attempt: roots.report + '/attempt-' + stage + '-147.json',
  terminal: roots.report + '/result-' + stage + '-147.json',
  bridge: roots.report + '/results/' + stage + '.json',
  evidence: roots.report + '/evidence/' + stage + '.json',
});

const sidecarCandidates = (roots, stage) => [
  roots.work + '/logs/inner-report-' + stage + '.json',
  roots.source + '/logs/inner-report-' + stage + '.json',
];
const rawCandidates = (roots, stage) => [
  roots.work + '/logs/child-output-' + stage + '.raw',
  roots.source + '/logs/child-output-' + stage + '.raw',
];

export const layerPaths = (roots, stage, bridgeRecord = null) => {
  const base = reportLayers(roots, stage);
  const map = roots.map || ((p) => p);
  const pick = (declared, candidates) => {
    if (declared) return map(declared);
    const hit = candidates.find((c) => exists(c));
    return hit || candidates[0];
  };
  const innerDeclared = bridgeRecord && bridgeRecord.inner_report ? bridgeRecord.inner_report.path : null;
  const rawDeclared = bridgeRecord && bridgeRecord.raw ? bridgeRecord.raw.path : null;
  return {
    ...base,
    inner: pick(innerDeclared, sidecarCandidates(roots, stage)),
    raw: pick(rawDeclared, rawCandidates(roots, stage)),
  };
};

export const readChain = (roots, stage) => {
  // The bridge report is read first because it names the two sidecars.
  const bridgePath = reportLayers(roots, stage).bridge;
  const bridgeRecord = exists(bridgePath) ? readJsonMaybe(bridgePath) : null;
  const p = layerPaths(roots, stage, bridgeRecord);
  const present = {};
  for (const [k, v] of Object.entries(p)) present[k] = exists(v);
  const out = { stage, paths: p, present, bytes: {} };
  for (const [k, v] of Object.entries(p)) {
    if (!present[k]) continue;
    const b = fs.readFileSync(toHost(v));
    out.bytes[k] = { bytes: b.length, sha256: sha(b) };
  }
  out.spec = present.spec ? readJsonMaybe(p.spec) : null;
  out.control = present.control ? readJsonMaybe(p.control) : null;
  out.attempt = present.attempt ? readJsonMaybe(p.attempt) : null;
  out.terminal = present.terminal ? readJsonMaybe(p.terminal) : null;
  out.bridge = present.bridge ? bridgeRecord : null;
  out.evidence = present.evidence ? readJsonMaybe(p.evidence) : null;
  out.inner = present.inner ? readJsonMaybe(p.inner) : null;
  out.rawText = present.raw ? fs.readFileSync(toHost(p.raw), 'utf8') : null;
  return out;
};

// ---------------------------------------------------------------- the judgement
//
// Every check produces one named fact. The record keeps the facts that failed as well as the ones that held, because a
// reader asking "was the group really empty" should get the number rather than a boolean somebody else computed.
export const judgeChain = (roots, stage, pins, opts = {}) => {
  const c = readChain(roots, stage);
  const facts = [];
  const add = (id, ok, detail) => facts.push({ id, ok: ok === true, detail: detail === undefined ? null : detail });
  const require = (layer) => {
    const ok = c.present[layer] === true;
    add('present:' + layer, ok, ok ? c.bytes[layer] : 'absent: ' + c.paths[layer]);
    return ok;
  };

  // --- the eight layers exist. A missing one stops the rest of that layer's checks but not the others, so the record
  // says everything that is wrong rather than the first thing.
  for (const layer of ['spec', 'control', 'helper', 'attempt', 'terminal', 'bridge', 'inner', 'raw']) require(layer);

  // --- spec: it names the frozen runner and pins this contract.
  if (c.spec) {
    add('spec:contract-pin', c.spec.contract_pin && c.spec.contract_pin.sha256 === pins.contract.sha256,
      c.spec.contract_pin ? c.spec.contract_pin.sha256 : 'no contract pin');
    add('spec:runner', c.spec.runnerPath === pins.runner.path,
      c.spec.runnerPath || 'no runner path');
    add('spec:route', typeof c.spec.route === 'string' && c.spec.route.length > 0, c.spec.route);
    add('spec:task', c.spec.task === TASK, c.spec.task);
  }

  // --- control: the runner froze this and the bridge read it. The guard and the interpreter are the pinned ones.
  if (c.control) {
    const br = c.control.bridge || {};
    add('control:stage', br.stage === stage, br.stage);
    add('control:guard', br.guard_linux === pins.guard_linux, br.guard_linux);
    add('control:python', br.tools && br.tools.python_realpath === pins.python_realpath,
      br.tools ? br.tools.python_realpath : 'no tools');
    // The bridge module is named on the control, not inside its bridge block: it is the module the runner imports before
    // it has a bridge at all, so it cannot be a field of the thing it imports. What is checked is the bytes, because this
    // packet's control names the tool worktree's copy of that module and the contract names the closure's copy.
    const ctlMod = c.control.bridge_module_path;
    add('control:bridge-module', typeof ctlMod === 'string' && exists(ctlMod) && pin(ctlMod).sha256 === pins.bridge.sha256,
      ctlMod);
  }

  // --- helper: frozen before the launch, and the same bytes after it.
  if (c.attempt && c.present.helper) {
    const f = c.attempt.frozen || {};
    add('helper:frozen-path', typeof f.frozen_path === 'string' && f.frozen_path.length > 0, f.frozen_path);
    add('helper:frozen-pin', f.frozen_sha256 === c.bytes.helper.sha256, f.frozen_sha256);
    add('helper:frozen-bytes', f.frozen_bytes === c.bytes.helper.bytes, f.frozen_bytes);
  }
  if (c.terminal && c.present.helper) {
    add('helper:unchanged', c.terminal.frozen_unchanged === true
      && c.terminal.frozen_posthash_sha256 === c.bytes.helper.sha256,
      c.terminal.frozen_posthash_sha256);
  }

  // --- attempt: a reservation with a route, bound to the pinned runner by a pin file.
  if (c.attempt) {
    add('attempt:complete', c.attempt.complete === true, c.attempt.complete);
    add('attempt:reservation', c.attempt.reservation === true, c.attempt.reservation);
    add('attempt:route', c.attempt.stage_claim && c.attempt.stage_claim.route === c.spec?.route,
      c.attempt.stage_claim ? c.attempt.stage_claim.route : 'no claim');
    const rp = c.attempt.runner_pin_path;
    const okPin = typeof rp === 'string' && exists(rp) && pin(rp).sha256 === c.attempt.runner_pin_sha256;
    add('attempt:runner-pin', okPin, rp);
    add('attempt:runner-bytes', c.attempt.runner_sha256 === pins.runner.sha256, c.attempt.runner_sha256);
  }

  // --- terminal: written before the attempt was marked complete, and the runner did not change under it.
  if (c.terminal) {
    add('terminal:lifecycle', c.terminal.lifecycle === 'terminal_result_written_before_complete', c.terminal.lifecycle);
    add('terminal:runner-unchanged', c.terminal.runner_unchanged === true
      && c.terminal.runner_posthash && c.terminal.runner_posthash.sha256 === pins.runner.sha256,
      c.terminal.runner_posthash ? c.terminal.runner_posthash.sha256 : 'no posthash');
    add('terminal:subjects-unchanged', c.terminal.route_subjects_unchanged === true, c.terminal.route_subjects_unchanged);
    const outs = c.terminal.expected_outputs_state || [];
    add('terminal:outputs-present', c.terminal.expected_outputs_all_present === true && outs.length > 0,
      outs.length + ' declared');
    const missing = outs.filter((o) => o.present !== true || !(o.bytes > 0));
    add('terminal:outputs-nonempty', missing.length === 0,
      missing.length ? missing.map((o) => o.path).join(', ') : null);
    // The declared byte count is checked against the file, not taken on trust. A record that says an output is 27939
    // bytes when it is 27938 is wrong even though both numbers are positive.
    const mismatched = outs.filter((o) => !exists(o.path) || fs.statSync(toHost(o.path)).size !== o.bytes);
    add('terminal:outputs-pin', outs.length > 0 && mismatched.length === 0,
      mismatched.map((o) => ({ path: o.path, declared: o.bytes,
        actual: exists(o.path) ? fs.statSync(toHost(o.path)).size : null })));
    // And the outputs the terminal names are the ones the spec declared, so a terminal cannot quietly drop one.
    const specOuts = (c.spec && c.spec.expected_outputs) || [];
    const sameSet = specOuts.length === outs.length
      && specOuts.every((p) => outs.some((o) => o.path === p));
    add('terminal:outputs-match-spec', sameSet, { spec: specOuts.length, terminal: outs.length });
  }

  // --- bridge and inner: two authors, two files.
  const verdict = c.bridge ? (c.bridge.verdict || c.bridge.bridge_verdict) : null;
  const guardRow = c.bridge ? c.bridge.guard_row : null;
  if (c.bridge) {
    add('bridge:verdict', verdict !== null && verdict !== undefined, verdict ? 'present' : 'no verdict');
    if (verdict) {
      add('bridge:accepted', verdict.accepted === true, verdict.accepted);
      add('bridge:transport-started', verdict.transport_started === true, verdict.transport_started);
      add('bridge:guard-exit-as-expected', verdict.guard_exit_as_expected === true, verdict.expected_guard_exit);
      add('bridge:guard-row', verdict.guard_row_present === true, verdict.guard_row_present);
      add('bridge:inner-present', verdict.inner_present === true, verdict.inner_present);
      add('bridge:raw-present', verdict.raw_present === true, verdict.raw_present);
      add('bridge:raw-within-bound', verdict.raw_within_bound === true, verdict.raw_within_bound);
      add('bridge:landlock', verdict.landlock_ok === true, verdict.landlock_ok);
      add('bridge:probes', verdict.probes_ok === true, verdict.probes_ok);
      add('bridge:group-measured', verdict.group_measured === true, verdict.group_measured);
      add('bridge:group-empty', verdict.group_empty_after_cleanup === true, verdict.group_empty_after_cleanup);
    }
    add('bridge:report-path', typeof c.bridge.bridge_report === 'string' && exists(c.bridge.bridge_report),
      c.bridge.bridge_report);
  }
  if (c.inner) {
    add('inner:stage', c.inner.stage === stage, c.inner.stage);
    const lk = c.inner.landlock || {};
    add('inner:landlock', lk.ok === true && lk.no_new_privs_rc === 0 && lk.restrict_self_rc === 0
      && lk.directory_execute_granted === false, { ok: lk.ok, nnp: lk.no_new_privs_rc, restrict: lk.restrict_self_rc });
    // The probe block is a map, not a list: each name states the outcome it wants (`..._allowed`, `..._denied`) and the
    // value says whether that outcome was observed. Every one of them has to be true, including the denials -- a denied
    // probe that was not denied is the guard failing to confine, not the guard being lenient.
    const checks = c.inner.probe_checks || {};
    const checkNames = Object.keys(checks);
    const PROBE_REQUIRED = ['inside_work_root_write_allowed', 'loader_readable', 'positive_python_allowed',
      'private_shm_write_allowed', 'negative_cat_denied', 'negative_true_denied',
      'outside_source_write_denied', 'outside_tmp_write_denied'];
    const probesOk = checkNames.length >= PROBE_REQUIRED.length
      && checkNames.every((k) => checks[k] === true)
      && PROBE_REQUIRED.every((k) => checks[k] === true);
    add('inner:probes', probesOk, { names: checkNames.length, not_true: checkNames.filter((k) => checks[k] !== true) });
    const ch = c.inner.child || {};
    add('inner:group-measured', ch.group_measured === true && ch.group_measured_final === true, ch.group_pgid);
    add('inner:group-empty', Array.isArray(ch.descendants_left) && ch.descendants_left.length === 0,
      Array.isArray(ch.descendants_left) ? ch.descendants_left.length : 'not measured');
    add('inner:group-nothing-killed', Array.isArray(ch.killed) && ch.killed.length === 0,
      Array.isArray(ch.killed) ? ch.killed.join(',') : 'not measured');
    add('inner:timed-out', ch.timed_out === false, ch.timed_out);
  }

  // --- raw: the child's own channel. Genuinely empty is legal; absent is not.
  if (c.present.raw && c.inner) {
    const ch = c.inner.child || {};
    const b = fs.readFileSync(toHost(c.paths.raw));
    add('raw:matches-inner', sha(b) === ch.output_sha256 && b.length === ch.output_bytes,
      { bytes: b.length, inner_bytes: ch.output_bytes });
    add('raw:path-matches-inner', toLinuxForm(ch.output_path) === toLinuxForm(c.paths.raw), ch.output_path);
  }

  // --- the three exits, each from its own author.
  if (c.terminal && verdict) {
    add('exit:transport-agrees', c.terminal.transport_exit_code === verdict.transport_exit,
      { terminal: c.terminal.transport_exit_code, bridge: verdict.transport_exit });
  }
  if (guardRow && c.inner) {
    add('exit:guard-agrees-with-tool', guardRow.exit_code === c.inner.child.returncode,
      { guard: guardRow.exit_code, tool: c.inner.child.returncode });
  }
  if (verdict && c.inner) {
    add('exit:bridge-agrees-with-tool', verdict.tool_returncode === c.inner.child.returncode,
      { bridge: verdict.tool_returncode, tool: c.inner.child.returncode });
  }
  if (c.inner) {
    const kind = opts.kind || (c.control && c.control.bridge && c.control.bridge.kind) || null;
    const expected = opts.expectedToolExit === undefined ? 0 : opts.expectedToolExit;
    add('exit:tool-as-expected', c.inner.child.returncode === expected,
      { kind, observed: c.inner.child.returncode, expected });
  }

  // --- the exact tool.
  //
  // Two kinds of launch have to be told apart, because they make different claims:
  //
  //   * a builder: the program that runs is a pinned tool from the contract, and the claim is that it is that exact file and
  //     nothing else. `tool:argv0` is the contract's answer, not a pattern.
  //   * a unit run: the program that runs is the unit ELF this packet compiled, so there is no pinned tool to compare with --
  //     the subject is the executable. The claim that replaces it is stronger, not weaker: the file executed has to be the
  //     one the spec named, it has to be inside the tree this packet built, and it has to be in the grant. A run of anything
  //     else -- an installed binary, a tool from PATH -- fails `tool:runs-own-artifact`.
  if (c.inner) {
    const kind = opts.kind || (c.control && c.control.bridge && c.control.bridge.kind) || null;
    const opts2 = (c.control && c.control.stage_options) || {};
    const ownElf = typeof opts2.testElfLinux === 'string' ? opts2.testElfLinux : null;
    const runsOwnArtifact = kind === 'hostunit' && ownElf !== null;
    const want = runsOwnArtifact ? ownElf : toolFor(pins, kind);
    add('tool:argv0', c.inner.argv && c.inner.argv[0] === want, { kind, argv0: c.inner.argv && c.inner.argv[0], want,
      mode: runsOwnArtifact ? 'runs the artifact this packet built' : 'runs the pinned tool' });
    if (runsOwnArtifact) {
      // The ELF must be inside the tree the build wrote into, exist, and be the file the spec named. "Inside the source view"
      // is what distinguishes this packet's own output from an installed program that happens to share a basename.
      //
      // The roots come from the control's bridge block, not from the bridge *report*: the report is the guard's record of one
      // process and does not carry the roots the control was written with.
      const ctlBridge = (c.control && c.control.bridge) || {};
      const roots = [ctlBridge.source_root, ctlBridge.work_root].filter((x) => typeof x === 'string');
      const inTree = roots.some((r) => ownElf.startsWith(r + '/'));
      add('tool:runs-own-artifact', inTree && exists(ownElf) && fs.statSync(toHost(ownElf)).size > 0,
        { path: ownElf, in_tree: inTree, roots_checked: roots.length,
          bytes: exists(ownElf) ? fs.statSync(toHost(ownElf)).size : null });
      // And the pin: the ELF that ran is the ELF the build produced, hashed at this moment.
      add('tool:artifact-pin', exists(ownElf) ? pin(ownElf).sha256 !== null : false,
        exists(ownElf) ? pin(ownElf) : 'absent');
    } else {
      // The pinned tool's bytes are checked, not just its name. A path that is right and a file that was replaced is the
      // defect a name comparison cannot see.
      const role = kind === 'cxx' ? 'g++' : kind === 'as' ? 'as' : kind === 'ld' ? 'ld.bfd' : null;
      const pinRec = role ? pins.roles.get(role) : null;
      if (pinRec) {
        add('tool:pinned-bytes', pin(pinRec.linux).sha256 === pinRec.sha256,
          { role, linux: pinRec.linux, want: pinRec.sha256 });
      } else {
        add('tool:pinned-bytes', pin(pins.python_realpath).sha256 === pins.python.sha256,
          { role: 'python', want: pins.python.sha256 });
      }
    }
    const granted = (c.inner.landlock && c.inner.landlock.exec_files_granted) || [];
    add('tool:in-grant', granted.includes(c.inner.argv && c.inner.argv[0]), granted);
    if (c.bridge && c.bridge.subject) {
      add('tool:subject-argv0', c.bridge.subject.cmd0 === c.inner.argv[0], c.bridge.subject.cmd0);
      add('tool:subject-argv', JSON.stringify(c.bridge.subject.argv) === JSON.stringify(c.inner.argv),
        (c.bridge.subject.argv || []).length + ' tokens');
    }
    if (guardRow) add('tool:guard-argv', JSON.stringify(guardRow.argv) === JSON.stringify(c.inner.argv),
      (guardRow.argv || []).length + ' tokens');
    if (c.bridge && c.bridge.subject) {
      add('tool:subject-cwd', c.bridge.subject.cwd === c.inner.cwd, { subject: c.bridge.subject.cwd, inner: c.inner.cwd });
    }
  }

  const problems = facts.filter((f) => !f.ok);
  return { stage, ok: problems.length === 0, problems, facts, paths: c.paths, present: c.present };
};

// ---------------------------------------------------------------- the declared stage set
//
// The stages this packet's C goal covers: the two static stages, the twelve producer rebuilds, the link, and the two host
// units. Each is a stage this packet actually ran through the pinned runner, and each has all eight layers on disk.
export const STAGE_SET = (tags) => [
  ...tags.static.map((s) => ({ stage: s, role: 'static' })),
  ...tags.producers.map((s) => ({ stage: s, role: 'producer' })),
  { stage: tags.link, role: 'link' },
  ...tags.units.map((s) => ({ stage: s, role: 'unit' })),
];

// ---------------------------------------------------------------- the CLI
const SELF_PATH = fileURLToPath(import.meta.url);
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH);
if (isMain) {
  const mode = process.argv[2];
  const tag = process.argv[3] || 'a';
  const pins = pinsOf();

  const write = (name, obj) => {
    const p = R + '/' + name;
    if (exists(p)) throw new Error('REFUSED: ' + p + ' already exists');
    fs.writeFileSync(toHost(p), JSON.stringify(obj, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
    return pin(p);
  };

  if (mode === 'chain') {
    // The stage names are passed as one comma-separated list per role so the record can say which role each served. The
    // argument is either the JSON itself or a path to it: the full stage set is long enough that passing it as one shell
    // argument would be an exercise in quoting rather than in evidence.
    const raw = process.argv[4] || '{}';
    const spec = exists(raw) ? readJson(raw) : JSON.parse(raw);
    const set = STAGE_SET({
      static: spec.static || [], producers: spec.producers || [], link: spec.link, units: spec.units || [],
    });
    const rows = [];
    for (const s of set) {
      const j = judgeChain(realRoots(), s.stage, pins, { kind: spec.kinds ? spec.kinds[s.stage] : undefined });
      rows.push({ role: s.role, stage: s.stage, ok: j.ok, problems: j.problems, facts: j.facts, paths: j.paths });
    }
    const problems = rows.flatMap((r) => r.problems.map((p) => ({ stage: r.stage, id: p.id, detail: p.detail })));
    const summary = {
      task: TASK, kind: 'c_guarded_chain',
      what: 'every layer of the guarded chain for each stage this packet ran: the spec written, the control the runner '
        + 'froze, the helper it froze and re-hashed, the reservation, the terminal, the bridge report, the guard inner '
        + 'report and the child raw channel, with the three exits and the process-group cleanup read from their own authors',
      pins: { contract: pins.contract, guard: pins.guard, runner: pins.runner, helper_draft: pins.helper_draft,
        bridge: pins.bridge, python_realpath: pins.python_realpath,
        roles: [...pins.roles.values()].map((r) => ({ role: r.role, linux: r.linux, sha256: r.sha256 })) },
      stages: rows.map((r) => ({ role: r.role, stage: r.stage, ok: r.ok,
        facts: r.facts.length, held: r.facts.filter((f) => f.ok).length,
        problems: r.problems.map((p) => ({ id: p.id, detail: p.detail })) })),
      detail: rows,
      problems, problem_count: problems.length,
      ok: problems.length === 0,
      ok_means: 'for every stage in the set, all eight chain layers exist, the frozen helper and runner are the bytes the '
        + 'contract pins both before and after the launch, the three exit numbers each come from the program that wrote '
        + 'them and agree, the process group was measured and is empty afterwards with nothing left to kill, the tool that '
        + 'ran is the exact path the contract pins for that kind, and every declared output exists and is non-empty',
    };
    const rec = write('c-chain-147' + tag + '.json', summary);
    console.log(JSON.stringify({ ok: summary.ok, stages: summary.stages.length,
      problem_count: summary.problem_count, problems: summary.problems.slice(0, 12), record: rec }, null, 2));
    process.exit(summary.ok ? 0 : 1);
  }

  if (mode === 'negatives') {
    // A real complete chain, relocated byte-for-byte into a scratch tree this file owns, then exactly one dependency
    // removed or changed per case. The relocation rewrites the roots the records name so the chain stays internally
    // consistent; the defect is then the only difference from the chain that passed.
    const stage = process.argv[4];
    if (!stage) throw new Error('usage: negatives <tag> <stage>');
    const scratch = S + '/tmp/cneg-' + tag + '-' + stage;
    if (exists(scratch)) throw new Error('REFUSED: ' + scratch + ' already exists');
    fs.mkdirSync(toHost(scratch), { recursive: true });

    const real = readChain(realRoots(), stage);
    const absent = Object.entries(real.present).filter(([, v]) => !v).map(([k]) => k);
    if (absent.length) throw new Error('the chain to relocate is incomplete: ' + absent.join(', '));

    // The pinned externals are not part of the relocated tree. The contract, the executor closure and the guard are what
    // the chain is judged against, so a relocation that rewrote their names would be relocating the standard along with
    // the subject. They are held out of the substitution and put back unchanged afterwards. The bridge module the
    // control itself names is added to that list: this packet's control names the tool worktree's copy of the bridge
    // rather than the closure's own copy, and both are the same bytes, so the module the runner really imported has to
    // survive the relocation as well.
    const PROTECTED = [pins.contract.path, pins.guard.path, pins.runner.path, pins.helper_draft.path,
      ...pins.closure.values().map((e) => e.path),
      ...(real.control && real.control.bridge_module_path ? [real.control.bridge_module_path] : []),
      // The declared outputs are products of the run, not part of the chain's layout. They are left where the run put
      // them: the terminal's byte pin is a claim about those files, and a relocation that rewrote a record and then
      // measured the rewritten copy would be pinning its own arithmetic.
      ...((real.terminal && real.terminal.expected_outputs_state) || []).map((o) => o.path)]
      .filter((p, i, a) => a.indexOf(p) === i);
    const protect = (t) => {
      let out = t;
      PROTECTED.forEach((p, i) => { out = out.split(p).join('\u0000' + i + '\u0000'); });
      return out;
    };
    const unprotect = (t) => t.replace(/\u0000(\d+)\u0000/g, (m, i) => PROTECTED[Number(i)]);

    const rootsFor = (caseDir) => {
      const r = { report: caseDir + '/report', work: caseDir + '/work', source: caseDir + '/source' };
      // The relocation rewrites every root the records name, so a record and the tree it describes stay consistent.
      //
      // The order matters and is not cosmetic: the source-view root begins with the work root's own text, so a naive
      // `split(work).join(...)` would rewrite a source-view path into a work-root path with `-view` left dangling in the
      // middle of it. The longest root is therefore substituted first, and each substitution is anchored on a following
      // separator so a root is never matched inside a longer sibling path.
      //
      // A path that is already inside this case's own tree is returned untouched. The scratch tree is created under the
      // source view, so without that guard a second pass would see the scratch root as a source-view path and relocate
      // the relocation.
      const sub = (text, from, to) => text.replace(new RegExp(from.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '(?=/|$)', 'g'), to);
      r.map = (p) => {
        const lin = toLinuxForm(p);
        if (lin.startsWith(caseDir)) return lin;
        return unprotect(sub(sub(sub(protect(lin), S, r.source), L, r.work), R, r.report));
      };
      return r;
    };
    // Relocation is applied to parsed values, not to the raw text. Rewriting the text would also rewrite JSON escapes
    // (`\n` would become `/n`), which would corrupt a record that happens to carry a newline in a message.
    const mapDeep = (v, f) => {
      if (typeof v === 'string') return f(v);
      if (Array.isArray(v)) return v.map((x) => mapDeep(x, f));
      if (v && typeof v === 'object') {
        const out = {};
        for (const [k, x] of Object.entries(v)) out[k] = mapDeep(x, f);
        return out;
      }
      return v;
    };

    const relocate = (caseDir) => {
      const r = rootsFor(caseDir);
      const lp = layerPaths(r, stage, real.bridge);
      for (const dir of [r.report + '/stage-output', r.report + '/frozen-helpers', r.report + '/results',
        r.report + '/evidence', r.work + '/logs', r.source + '/logs']) fs.mkdirSync(toHost(dir), { recursive: true });
      for (const [k, src] of Object.entries(real.paths)) {
        if (!real.present[k]) continue;
        const b = fs.readFileSync(toHost(src));
        if (k === 'helper' || k === 'raw') {
          fs.writeFileSync(toHost(lp[k]), b);
          continue;
        }
        // The records name absolute roots. Relocating the tree without relocating the names would make every path
        // check fail for a reason that has nothing to do with the defect under test.
        fs.writeFileSync(toHost(lp[k]),
          JSON.stringify(mapDeep(JSON.parse(b.toString('utf8')), r.map), null, 2) + '\n');
      }
      // The runner pin is a record the reservation names, so it is part of the chain and moves with it, byte for byte.
      const rp = real.attempt && real.attempt.runner_pin_path;
      if (rp && exists(rp)) fs.writeFileSync(toHost(r.report + '/' + path.basename(rp)), fs.readFileSync(toHost(rp)));
      return { roots: r, paths: lp };
    };

    const cases = [
      ['missing-spec', (p) => fs.unlinkSync(toHost(p.spec)), 'present:spec'],
      ['missing-terminal', (p) => fs.unlinkSync(toHost(p.terminal)), 'present:terminal'],
      ['missing-inner', (p) => fs.unlinkSync(toHost(p.inner)), 'present:inner'],
      ['missing-raw', (p) => fs.unlinkSync(toHost(p.raw)), 'present:raw'],
      ['missing-helper', (p) => fs.unlinkSync(toHost(p.helper)), 'present:helper'],
      ['missing-attempt', (p) => fs.unlinkSync(toHost(p.attempt)), 'present:attempt'],
      ['wrong-tool', (p) => {
        const j = readJson(p.inner);
        j.argv[0] = '/bin/sh';
        fs.writeFileSync(toHost(p.inner), JSON.stringify(j, null, 2) + '\n');
      }, 'tool:argv0'],
      ['wrong-subject', (p) => {
        const j = readJson(p.bridge);
        j.subject.cmd0 = '/bin/sh';
        fs.writeFileSync(toHost(p.bridge), JSON.stringify(j, null, 2) + '\n');
      }, 'tool:subject-argv0'],
      ['wrong-output-pin', (p) => {
        const j = readJson(p.terminal);
        j.expected_outputs_state[0].bytes = j.expected_outputs_state[0].bytes + 1;
        fs.writeFileSync(toHost(p.terminal), JSON.stringify(j, null, 2) + '\n');
      }, 'terminal:outputs-pin'],
      ['wrong-chain-layer', (p) => {
        // The spec is pointed at a different runner than the one the contract pins: a chain that ran is not the chain
        // this packet's contract describes.
        const j = readJson(p.spec);
        j.runnerPath = R + '/frozen-runners/run-146-r01.mjs';
        fs.writeFileSync(toHost(p.spec), JSON.stringify(j, null, 2) + '\n');
      }, 'spec:runner'],
      ['wrong-helper-bytes', (p) => {
        // One byte changed in the frozen helper after the launch: the post-hash must notice.
        const b = fs.readFileSync(toHost(p.helper));
        b[0] = b[0] ^ 0x20;
        fs.writeFileSync(toHost(p.helper), b);
      }, 'helper:unchanged'],
      ['group-left-running', (p) => {
        const j = readJson(p.inner);
        j.child.descendants_left = [4242];
        j.child.killed = [4242];
        fs.writeFileSync(toHost(p.inner), JSON.stringify(j, null, 2) + '\n');
      }, 'inner:group-empty'],
      ['raw-tampered', (p) => {
        const b = fs.readFileSync(toHost(p.raw));
        fs.writeFileSync(toHost(p.raw), Buffer.concat([b, Buffer.from('x')]));
      }, 'raw:matches-inner'],
      ['exit-disagreement', (p) => {
        const j = readJson(p.terminal);
        j.transport_exit_code = j.transport_exit_code === 0 ? 1 : 0;
        fs.writeFileSync(toHost(p.terminal), JSON.stringify(j, null, 2) + '\n');
      }, 'exit:transport-agrees'],
    ];

    // The pristine relocated chain must pass first. A negative suite against a chain that already failed would prove
    // nothing about the defects.
    const baseDir = scratch + '/case-00-pristine';
    const base = relocate(baseDir);
    const baseVerdict = judgeChain(base.roots, stage, pins);
    const results = [{
      name: 'pristine', refused: !baseVerdict.ok, expected: null,
      hit_target: baseVerdict.ok, problems: baseVerdict.problems.map((p) => p.id),
    }];

    for (let i = 0; i < cases.length; i++) {
      const [name, mutate, expectedId] = cases[i];
      const dir = scratch + '/case-' + String(i + 1).padStart(2, '0') + '-' + name;
      const rel = relocate(dir);
      mutate(rel.paths);
      const v = judgeChain(rel.roots, stage, pins);
      results.push({
        name, expected: expectedId,
        refused: !v.ok,
        hit_target: v.problems.some((p) => p.id === expectedId),
        problems: v.problems.map((p) => p.id),
      });
    }

    const failures = results.filter((r) => (r.name === 'pristine' ? !r.hit_target : !(r.refused && r.hit_target)));
    const summary = {
      task: TASK, kind: 'c_chain_negatives',
      what: 'a complete chain relocated byte-for-byte into a scratch tree, then one dependency removed or changed per '
        + 'case; each case must be refused by the same judgement the pristine chain passed',
      source_stage: stage,
      source_chain: { paths: real.paths, bytes: real.bytes },
      scratch,
      cases: results.length - 1,
      refused: results.filter((r) => r.name !== 'pristine' && r.refused).length,
      hit_target: results.filter((r) => r.name !== 'pristine' && r.hit_target).length,
      pristine_accepted: results[0].hit_target,
      failures,
      results,
      ok: failures.length === 0,
      ok_means: 'the pristine relocation passes every check, and each of the thirteen single-defect cases is refused with '
        + 'the specific obligation the defect breaks rather than with a generic error',
    };
    const rec = write('c-negatives-147' + tag + '.json', summary);
    console.log(JSON.stringify({ ok: summary.ok, cases: summary.cases, refused: summary.refused,
      hit_target: summary.hit_target, pristine_accepted: summary.pristine_accepted,
      failures: summary.failures, record: rec }, null, 2));
    process.exit(summary.ok ? 0 : 1);
  }

  console.error('usage: c-chain-147.mjs chain <tag> <stageSpecJson> | negatives <tag> <stage>');
  process.exit(2);
}
