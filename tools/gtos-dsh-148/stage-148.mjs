// GTOS-DSH-148: the package tree, built before the archive so that C, H and the seal can all read the same bytes.
//
// The payload is not a directory of aggregates. It is the originals: for every stage the packet contracts for, the spec the
// stage was given, the control the runner froze, the helper that ran, the attempt reservation, the terminal, the guard's
// bridge record, the guard's own inner report and the raw channel the child wrote. Those bytes already exist in the 147
// report root and its two native roots; this file copies them into one tree, rehashing each copy, so the package is the
// originals and not a summary of them. The 147 review's fourth finding was that the 911-member payload carried no
// `.spec.json` and no attempt at all; this tree is built so that every one of them is a member.
//
// The tree is written under `R/package-148`. It is a staging tree, not the payload: the payload is the tar the engine
// writes from a plan over this tree, and the plan's hashes are taken from these files. C is run against this tree before
// the archive and against the extracted payload after it, through the same judge.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };

const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const R147 = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const WSL = '//wsl$/Ubuntu-20.04';
const TOOLS = R + '/worktree-148/tools/gtos-dsh-148';
const TOOLS147 = R + '/worktree-148/tools/gtos-dsh-147';
const TOOLS146 = R + '/worktree-148/tools/gtos-dsh-146';
const OUT = R + '/package-148';

const STAGE_GRAMMAR = /^[a-z][a-z0-9]*-[0-9]{2}$/;

const measure = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
const measureLinux = (linux) => {
  const host = WSL + linux;
  if (!exists(host)) return { linux, host, present: false };
  const b = fs.readFileSync(host);
  return { linux, host, present: true, bytes: b.length, sha256: sha(b) };
};
const copyTo = (from, to) => {
  fs.mkdirSync(path.dirname(to), { recursive: true });
  if (exists(to)) throw new Error('CreateNew refused, the package member already exists: ' + to);
  const b = fs.readFileSync(from);
  fs.writeFileSync(to, b, { flag: 'wx' });
  const back = fs.readFileSync(to);
  if (sha(back) !== sha(b)) throw new Error('the copy is not the original: ' + to);
  return { path: to, bytes: back.length, sha256: sha(back), from };
};

// ---------------------------------------------------------------- the contract
//
// Frozen before it is used. The tool pins are measured now rather than copied from the 147 contract, because the point of
// a pin is that it is the bytes on this host today; the 147 values are compared against as a second opinion.
const buildContract = () => {
  const routes = readJson(R + '/execution-identities-148.json').routes;
  const tools147 = readJson(R147 + '/contract-147.json').tools;
  const python = measureLinux('/usr/bin/python3.8');
  const roles = [
    ['g++', '/usr/bin/x86_64-linux-gnu-g++-9'],
    ['as', '/usr/bin/x86_64-linux-gnu-as'],
    ['cc1plus', '/usr/lib/gcc/x86_64-linux-gnu/9/cc1plus'],
    ['ld.bfd', '/usr/bin/x86_64-linux-gnu-ld.bfd'],
  ].map(([role, linux]) => ({ role, ...measureLinux(linux) }));
  const guard = measure('F:/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py');
  const runner = measure(R147 + '/frozen-runners/run-147-r01.mjs');

  // The checkpoint transport does not run the host's loader. It runs the loader staged inside the runtime root the packet
  // built, with the emulator from that same root, and both are pinned as the bytes on disk now. The host loader is pinned
  // beside them so a reader can see they are two different files and that the transport uses the staged one.
  const runtimeRoot = '/var/tmp/gtos-dsh-147-e759-scene-bound-view/runtime/root';
  const loaderInRoot = measureLinux(runtimeRoot + '/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2');
  const emulatorInRoot = measureLinux(runtimeRoot + '/usr/bin/qemu-system-i386');
  const hostLoader = measureLinux('/usr/lib/x86_64-linux-gnu/ld-2.31.so');

  // The frozen helper is one program per draft, and the draft a spec names is what decides which program ran. The 117
  // stages name two drafts between them, and every stage naming the same draft must carry the same bytes -- so the identity
  // is measured by grouping, and a stage that disagrees with its own group is a stage whose helper nobody can check. This
  // is measured from the 147 originals here and re-read from the package by the judge, so the two readings are of different
  // files reached by different code, not of one file read twice.
  const helperDrafts = {};
  const draftDisagreements = [];
  for (const name of fs.readdirSync(R147 + '/frozen-helpers').sort()) {
    if (!/\.mjs$/.test(name)) continue;
    const stage = name.replace(/\.mjs$/, '');
    const specPath = R147 + '/stage-output/' + stage + '.spec.json';
    if (!exists(specPath)) continue;
    const spec = readJson(specPath);
    const b = fs.readFileSync(R147 + '/frozen-helpers/' + name);
    const identity = { bytes: b.length, sha256: sha(b) };
    const seen = helperDrafts[spec.helper];
    if (!seen) helperDrafts[spec.helper] = { ...identity, stages_naming_it: 1, from: R147 + '/frozen-helpers/' + name };
    else if (seen.sha256 !== identity.sha256) {
      draftDisagreements.push({ draft: spec.helper, stage, seen: seen.sha256, now: identity.sha256 });
      seen.stages_naming_it += 1;
    } else seen.stages_naming_it += 1;
  }

  const disagreements = [];
  const cmp = (name, now, before) => {
    if (!before) return;
    if (now.bytes !== before.bytes || now.sha256 !== before.sha256) {
      disagreements.push({ tool: name, now: { bytes: now.bytes, sha256: now.sha256 }, before: { bytes: before.bytes, sha256: before.sha256 } });
    }
  };
  cmp('python', python, tools147.python);
  cmp('guard', guard, tools147.guard);
  for (const r of roles) cmp(r.role, r, (tools147.roles || []).find((x) => x.role === r.role));
  return {
    task: 'GTOS-DSH-148',
    what: 'the identity every later spec and every chain judgement pins: the roots, the tools, the routes, and the source '
      + 'task whose executed originals this package carries',
    source_task: 'GTOS-DSH-147',
    source_task_means: 'the chain originals in this package are the 147 executions, preserved. They are not re-run here and '
      + 'are not counted as 148 executions; 148 executes the consumers, the publication and the judgements over them',
    roots: { report_root: R, package: OUT, work_root: '/var/tmp/gtos-dsh-148-e759-executed-consumers',
      source_root: '/var/tmp/gtos-dsh-148-e759-executed-consumers-view',
      origin_report_root: R147,
      origin_work_root: '/var/tmp/gtos-dsh-147-e759-scene-bound',
      origin_source_root: '/var/tmp/gtos-dsh-147-e759-scene-bound-view' },
    stage_grammar: STAGE_GRAMMAR.source,
    routes,
    helper_drafts: helperDrafts,
    tools: {
      node: measure(process.execPath),
      wsl: measure('C:/Windows/System32/wsl.exe'),
      guard,
      guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
      python: { linux: '/usr/bin/python3', realpath: '/usr/bin/python3.8', ...python },
      runtime_root: runtimeRoot,
      loader_in_runtime_root: loaderInRoot,
      emulator_in_runtime_root: emulatorInRoot,
      host_loader: hostLoader,
      roles,
    },
    runner,
    helper_draft_disagreements: draftDisagreements,
    tool_pins_agree_with_the_147_contract: disagreements.length === 0,
    tool_pin_disagreements: disagreements,
    note: 'a disagreement is recorded rather than corrected: a tool that changed under the packet is a fact a reader has to '
      + 'see, and the chain judge compares each stage against these pins',
  };
};

// ---------------------------------------------------------------- the chain
//
// The eight layers, from the places the 147 packet actually wrote them. The inner report and the raw channel are resolved
// from each stage's own bridge record rather than guessed: the compiler stages wrote under the source view, the checkpoints
// under the work root, and the host-unit runs under their own directories. A fixed guess finds 99 of 117.
const LAYERS = ['spec', 'control', 'helper', 'attempt', 'terminal', 'bridge', 'inner', 'raw'];

const buildChain = () => {
  const stages = fs.readdirSync(R147).map((n) => /^attempt-(.+)-147\.json$/.exec(n)).filter(Boolean).map((m) => m[1]).sort();
  const rows = [];
  const problems = [];
  for (const stage of stages) {
    if (!STAGE_GRAMMAR.test(stage)) { problems.push({ stage, why: 'the stage name is not of the grammar' }); continue; }
    const dir = OUT + '/chain/' + stage;
    fs.mkdirSync(dir, { recursive: true });
    const layers = {};
    const want = {
      spec: R147 + '/stage-output/' + stage + '.spec.json',
      control: R147 + '/stage-output/' + stage + '.control.json',
      helper: R147 + '/frozen-helpers/' + stage + '.mjs',
      attempt: R147 + '/attempt-' + stage + '-147.json',
      terminal: R147 + '/result-' + stage + '-147.json',
      bridge: R147 + '/evidence/' + stage + '.json',
    };
    for (const [layer, from] of Object.entries(want)) {
      if (!exists(from)) { problems.push({ stage, layer, why: 'absent at its 147 location', from }); continue; }
      const to = dir + '/' + (layer === 'helper' ? stage + '.mjs' : stage + '.' + layer + '.json');
      layers[layer] = copyTo(from, to);
    }
    // The inner report and the raw channel, resolved from the bridge record's own paths.
    if (layers.bridge) {
      const ev = readJson(layers.bridge.path);
      for (const [layer, field, suffix] of [['inner', 'inner_report', '.inner.json'], ['raw', 'raw', '.raw']]) {
        const ref = ev[field];
        if (!ref || typeof ref.path !== 'string') { problems.push({ stage, layer, why: 'the bridge record names no ' + field }); continue; }
        if (!exists(ref.path)) { problems.push({ stage, layer, why: 'absent at the path the bridge record names', from: ref.path }); continue; }
        layers[layer] = copyTo(ref.path, dir + '/' + stage + suffix);
      }
    }
    // The report record: the only place a checkpoint's two guest channels are described.
    if (exists(R147 + '/results/' + stage + '.json')) {
      layers.report = copyTo(R147 + '/results/' + stage + '.json', dir + '/' + stage + '.report.json');
    }
    rows.push({ stage, route: layers.spec ? readJson(layers.spec.path).route : null,
      kind: layers.bridge ? readJson(layers.bridge.path).kind : null,
      layers: Object.keys(layers), layer_count: LAYERS.filter((l) => layers[l]).length,
      bytes: Object.values(layers).reduce((n, m) => n + m.bytes, 0) });
  }
  const complete = rows.filter((r) => r.layer_count === LAYERS.length);
  return { stages: rows.length, complete: complete.length,
    incomplete: rows.filter((r) => r.layer_count !== LAYERS.length).map((r) => ({ stage: r.stage, has: r.layers })),
    rows, problems, bytes: rows.reduce((n, r) => n + r.bytes, 0) };
};

// ---------------------------------------------------------------- the records, the engine, the material
const CONTROL_FILES = [
  // The reservation ledger: the primary source the stage derivation reads. It is a control record because it is the packet's
  // own statement of what it reserved, written before the work, and it has to travel with the package for the derivation to
  // be checkable by a reader.
  'stage-registry-147.jsonl', 'producer-order-147.jsonl',
  // The records that fix counts: the trials, the cases, the slots, the units, and the 147 C set carried as evidence.
  'g-matrix-gm147-01-147.json', 'g-trial-sc147-01-147.json', 'g-trial-sc147-02-147.json',
  'h-contract-147e.json', 'p-selection-147-a.json', 'p-build-147a.json', 'c-stage-set-147.json',
  // The judgements' own records, carried so the package can be read without the report root.
  'assembly-source-contract-147.json', 'scene-identity-p147-default-147.json', 'required-set-rs147-01-147.json',
  'h-consumption-147e.json', 'c-chain-147ca.json', 'd-closure-dc147-02-147.json',
  'n-numeric-nj147-02-147.json', 'r-negatives-rn147-01-147.json', 'h-negatives-147a.json',
];
const buildControl = () => {
  const written = [];
  const missing = [];
  for (const name of CONTROL_FILES) {
    const from = R147 + '/' + name;
    if (!exists(from)) { missing.push(name); continue; }
    written.push(copyTo(from, OUT + '/control/' + name));
  }
  // The unit build records and the role records, by pattern rather than by name: they are one per unit and one per role.
  for (const name of fs.readdirSync(R147)) {
    if (/^h-unit-build-.*-147\.json$/.test(name) || /^role-.*-147b?\.json$/.test(name)) {
      written.push(copyTo(R147 + '/' + name, OUT + '/control/' + name));
    }
  }
  const contract = buildContract();
  const p = OUT + '/control/contract-148.json';
  fs.writeFileSync(p, JSON.stringify(contract, null, 2) + '\n', { flag: 'wx' });
  written.push(measure(p));
  return { written: written.length, missing, contract: measure(p), tool_pins_agree: contract.tool_pins_agree_with_the_147_contract,
    helper_drafts: Object.keys(contract.helper_drafts), helper_draft_disagreements: contract.helper_draft_disagreements };
};

const ENGINE_FILES = [
  [TOOLS + '/publish-148.mjs', 'engine/publish-148.mjs'],
  [TOOLS + '/drive-148.mjs', 'engine/drive-148.mjs'],
  [TOOLS + '/chain-148.mjs', 'engine/chain-148.mjs'],
  [TOOLS + '/f-negative-148.mjs', 'engine/f-negative-148.mjs'],
  [TOOLS + '/f-qualification-148.mjs', 'engine/f-qualification-148.mjs'],
  [TOOLS + '/c-negative-148.mjs', 'engine/c-negative-148.mjs'],
  [TOOLS + '/spec-148.mjs', 'engine/spec-148.mjs'],
  [TOOLS + '/stage-148.mjs', 'engine/stage-148.mjs'],
  // The H harness and the module it drives the closure role through. A payload whose roles were decided by a harness that
  // does not travel with it cannot be re-judged by a reader, and the harness is the only record of what the entries were
  // called with.
  [TOOLS + '/h-roles-148.mjs', 'engine/h-roles-148.mjs'],
  [TOOLS147 + '/h-package-147.mjs', 'engine/h-package-147.mjs'],
  // The static gate and its judge. These are the bytes the guard actually executed as the child for the static role, so they
  // are carried as programs rather than as libraries: nothing here imports them.
  [TOOLS147 + '/static-gate-147.py', 'engine/static-gate-147.py'],
  [TOOLS147 + '/static-gate-147b.py', 'engine/static-gate-147b.py'],
  [TOOLS147 + '/static-judge-147.py', 'engine/static-judge-147.py'],
  [TOOLS147 + '/n-numeric-147.mjs', 'engine/n-numeric-147.mjs'],
  [TOOLS147 + '/host-consume-147.mjs', 'engine/host-consume-147.mjs'],
  [TOOLS147 + '/scene-contract-147.mjs', 'engine/scene-contract-147.mjs'],
  [TOOLS147 + '/run-147-r01.mjs', 'engine/runner/run-147-r01.mjs'],
  [TOOLS146 + '/io-trace-146.cjs', 'engine/io-trace-146.cjs'],
  [TOOLS146 + '/d-required-146.mjs', 'engine/d-required-146.mjs'],
];
const buildEngine = () => {
  const written = [];
  const missing = [];
  for (const [from, to] of ENGINE_FILES) {
    if (!exists(from)) { missing.push(from); continue; }
    written.push(copyTo(from, OUT + '/' + to));
  }
  return { written: written.length, missing };
};

// ---------------------------------------------------------------- the material
//
// The bytes the judgements read. Not a selection: the whole seven-bucket tree the 147 payload carried and its closure
// verified member for member -- the image and its map, the producer objects and their depfiles, the host ELFs, the host
// case logs and results, the guest channels and their inner reports, the profile, and the source view. A consumer that is
// handed a package has to be able to answer its question from the package, so the package carries every byte the consumers
// read. The source is the 147 payload's own extracted `material/` tree, which is the set the closure proved identical to
// the 785 members it was planned from; copying from there means the 148 package inherits exactly that set rather than a
// re-selection of it.
const MATERIAL_SOURCE = R147 + '/archive-147/harness-postseal-ps147-02/extracted/material';
const MATERIAL_BUCKETS = ['canonical', 'guest', 'host', 'image', 'producers', 'profile', 'sources'];
// The runtime supply is the eighth bucket, and it is not in the 147 payload: that packet required it of the machine it ran
// on and resolved the requirement against the live staged root, which is why its required set could say 689 required and
// 689 resolved while the payload carried 785 different members. This packet cannot do that and still be a package: if the
// required set names 206 runtime files, the payload has to carry 206 runtime files, or the closure would be reporting a
// requirement it did not satisfy. So the staged root the 147 execution used is carried as bytes.
const RUNTIME_SOURCE = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-147-e759-scene-bound-view/runtime';
const MATERIAL_BUCKETS_ALL = [...MATERIAL_BUCKETS, 'runtime'];

const buildMaterial = () => {
  if (!exists(MATERIAL_SOURCE)) throw new Error('the 147 extracted material tree is absent at ' + MATERIAL_SOURCE);
  const written = [];
  const missing = [];
  const byBucket = {};
  const walk = (dir, rel, sink) => {
    for (const n of fs.readdirSync(dir).sort()) {
      const full = dir + '/' + n;
      const r = rel ? rel + '/' + n : n;
      const st = fs.statSync(full);
      if (st.isDirectory()) { walk(full, r, sink); continue; }
      const rec = copyTo(full, OUT + '/material/' + sink + '/' + r);
      written.push({ to: sink + '/' + r, ...rec });
      byBucket[sink] = (byBucket[sink] || 0) + 1;
    }
  };
  for (const b of MATERIAL_BUCKETS) {
    if (!exists(MATERIAL_SOURCE + '/' + b)) { missing.push('material/' + b); continue; }
    walk(MATERIAL_SOURCE + '/' + b, '', b);
  }
  if (!exists(RUNTIME_SOURCE)) missing.push('material/runtime');
  else walk(RUNTIME_SOURCE, '', 'runtime');
  return { written: written.length, by_bucket: byBucket, missing,
    bytes: written.reduce((n, w) => n + w.bytes, 0) };
};

// ---------------------------------------------------------------- main
//
// The 117 stage directories this packet carries are not 117 successes. Four of them are the originals of executions that
// failed and were superseded -- the scene draft that could not find its module, the runtime metadata read that produced no
// inner report, and two unit links that failed for the same missing input before it was corrected. Those four are carried
// on purpose: a package that deleted them would be a package that cannot show what went wrong, and one that counted them as
// successes would be a package that lies. So the condition is not "every stage is complete"; it is "every stage the
// contracts require is complete, and the four known failures are present and are the ones named".
import { PRESERVED_FAILURES } from './chain-148.mjs';

const PRESERVED = new Set(PRESERVED_FAILURES.map((p) => p.stage));

// A record is written to the first name of its series that is not yet taken, so a re-run adds a record instead of
// overwriting the one that said what the previous run found.
const nextRecord = (series) => {
  for (let i = 1; i < 100; i += 1) {
    const p = R + '/' + series + '-' + String(i).padStart(2, '0') + '.json';
    if (!exists(p)) return p;
  }
  throw new Error('no free record name in the series ' + series);
};

// ---------------------------------------------------------------- the judgements
//
// The records this packet's own judges wrote. They travel in the payload for the same reason the 147 control records do: a
// reader who has only the payload has to be able to see what was decided about it and recompute it, without being handed a
// report root that nobody else can see. The newest held record of each series is carried, together with the records that
// failed -- the failed ones are not dropped, because a packet that carries only its successes cannot show what it found.
const JUDGEMENTS = () => {
  const out = [];
  const add = (name, role) => { if (exists(R + '/' + name)) out.push([R + '/' + name, 'judgements/' + name, role]); };
  add('f-negatives-148.json', 'judgement');
  add('f-qualification-148.json', 'judgement');
  // Every record of each judge's series, not a name written down here. A list of names has to be edited every time a judge is
  // re-run, and the one time it is not edited the package carries a superseded verdict while the newer one sits in the report
  // root -- which is exactly the failure this packet exists to close.
  const series = (pattern, role) => {
    for (const n of fs.readdirSync(R).filter((x) => pattern.test(x)).sort()) add(n, role);
  };
  series(/^c-chain-148-\d+\.json$/, 'judgement');
  series(/^c-negatives-148-\d+\.json$/, 'judgement');
  series(/^required-set-148-\d+\.json$/, 'judgement');
  series(/^stage-148-\d+\.json$/, 'judgement');
  // The H roles: every role record, the index that pins them and the summary that says whether the set held. The records
  // live one directory per role, so the walk has to go down; and `cases/` is skipped, because what it holds is the damaged
  // copies the negatives were refused on -- test scaffolding, not evidence, and not something the package should carry.
  if (exists(R + '/h-roles-148')) {
    const walk = (d, prefix) => {
      for (const n of fs.readdirSync(d).sort()) {
        const p = d + '/' + n;
        const relp = prefix ? prefix + '/' + n : n;
        if (relp.startsWith('cases/')) continue;
        if (fs.statSync(p).isDirectory()) { walk(p, relp); continue; }
        if (!n.endsWith('.json')) continue;
        add('h-roles-148/' + relp, relp === 'index.json' || relp === 'summary.json' ? 'role_index' : 'role_record');
      }
    };
    walk(R + '/h-roles-148', '');
  }
  return out;
};

const buildJudgements = () => {
  const written = [];
  const missing = [];
  for (const [from, to, role] of JUDGEMENTS()) {
    const rec = copyTo(from, OUT + '/' + to);
    written.push({ to, role, ...rec });
  }
  return { written: written.length, missing, roles: written.map((w) => w.role) };
};

const build = ({ at = OUT } = {}) => {
  if (at !== OUT) throw new Error('this build writes only to ' + OUT);
  if (exists(OUT)) throw new Error('CreateNew refused: the package tree already exists at ' + OUT);
  fs.mkdirSync(OUT, { recursive: true });
  const control = buildControl();
  const chain = buildChain();
  const engine = buildEngine();
  const judgements = buildJudgements();
  const material = buildMaterial();
  const incomplete = chain.incomplete.map((i) => i.stage);
  const incompleteThatIsNotAPreservedFailure = incomplete.filter((s) => !PRESERVED.has(s));
  // A preserved failure's absent layers are the fact that is being preserved, so they are carried in the record and are not
  // counted as problems with the build. Everything else is.
  const chainProblemsThatAreNotPreserved = chain.problems.filter((p) => !PRESERVED.has(p.stage));
  const chainProblemsThatArePreserved = chain.problems.filter((p) => PRESERVED.has(p.stage));
  const preservedThatIsComplete = chain.rows.filter((r) => PRESERVED.has(r.stage) && r.layer_count === LAYERS.length)
    .map((r) => r.stage);
  const record = {
    task: 'GTOS-DSH-148', kind: 'package_tree',
    what: 'the package tree the payload is planned from: the eight chain layers for every executed stage, the control '
      + 'records the derivation reads, the engine and the material the judgements read',
    at: OUT,
    control: { written: control.written, missing: control.missing, contract: control.contract,
      tool_pins_agree_with_the_147_contract: control.tool_pins_agree,
      helper_drafts: control.helper_drafts, helper_draft_disagreements: control.helper_draft_disagreements },
    chain: { stages: chain.stages, complete: chain.complete, incomplete: chain.incomplete,
      bytes: chain.bytes, problems: chainProblemsThatAreNotPreserved,
      problems_that_are_the_preserved_failures: chainProblemsThatArePreserved,
      preserved_failures_carried: [...PRESERVED].filter((s) => chain.rows.some((r) => r.stage === s)),
      incomplete_that_is_not_a_preserved_failure: incompleteThatIsNotAPreservedFailure,
      preserved_failure_that_is_surprisingly_complete: preservedThatIsComplete },
    engine,
    judgements,
    material,
    ok: control.missing.length === 0 && engine.missing.length === 0 && material.missing.length === 0
      && chainProblemsThatAreNotPreserved.length === 0 && incompleteThatIsNotAPreservedFailure.length === 0
      && [...PRESERVED].every((s) => chain.rows.some((r) => r.stage === s))
      && control.helper_drafts.length > 0 && control.helper_draft_disagreements.length === 0,
    ok_means: 'every control record, every engine module and every material file was found where the 147 execution left it, '
      + 'every stage the contracts require has all eight layers, the only incomplete stage directories are the four named '
      + 'preserved failures, each of which is present with its own original layers, and every stage naming the same helper '
      + 'draft carries the same helper bytes',
  };
  const p = nextRecord('stage-148');
  fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  return { record, recordPath: p };
};

// ---------------------------------------------------------------- verifying the tree, not rebuilding it
//
// The first build's record computed its verdict with the wrong condition -- it required every stage directory to be
// complete, which the four preserved failures can never be -- so it said false over a tree that was right. The record is
// kept, because it is what the first build actually said, and this second pass answers the question the first one should
// have asked: does every member of the tree equal the original it was copied from, and is the only incompleteness the
// incompleteness that was named in advance?
export const verify = () => {
  if (!exists(OUT)) throw new Error('there is no tree to verify at ' + OUT);
  const stages = fs.readdirSync(OUT + '/chain').filter((d) => fs.statSync(OUT + '/chain/' + d).isDirectory()).sort();
  const rows = [];
  const mismatched = [];
  for (const stage of stages) {
    const dir = OUT + '/chain/' + stage;
    const present = fs.readdirSync(dir).sort();
    const layers = {};
    const want = {
      spec: R147 + '/stage-output/' + stage + '.spec.json',
      control: R147 + '/stage-output/' + stage + '.control.json',
      helper: R147 + '/frozen-helpers/' + stage + '.mjs',
      attempt: R147 + '/attempt-' + stage + '-147.json',
      terminal: R147 + '/result-' + stage + '-147.json',
      bridge: R147 + '/evidence/' + stage + '.json',
    };
    for (const [layer, from] of Object.entries(want)) {
      const name = layer === 'helper' ? stage + '.mjs' : stage + '.' + layer + '.json';
      if (!present.includes(name)) { layers[layer] = { present: false }; continue; }
      const here = fs.readFileSync(dir + '/' + name);
      const there = exists(from) ? fs.readFileSync(from) : null;
      const same = there !== null && sha(here) === sha(there);
      layers[layer] = { present: true, bytes: here.length, sha256: sha(here), equals_the_original: same, from };
      if (!same) mismatched.push({ stage, layer, from });
    }
    // The inner report and the raw channel: compared against the path the packaged bridge record names, which is the
    // originals' own statement of where they are.
    const bridgeHere = exists(dir + '/' + stage + '.bridge.json') ? readJson(dir + '/' + stage + '.bridge.json') : null;
    for (const [layer, field, name] of [['inner', 'inner_report', stage + '.inner.json'], ['raw', 'raw', stage + '.raw']]) {
      if (!present.includes(name)) { layers[layer] = { present: false }; continue; }
      const here = fs.readFileSync(dir + '/' + name);
      const ref = bridgeHere && bridgeHere[field] ? bridgeHere[field].path : null;
      const there = ref && exists(ref) ? fs.readFileSync(ref) : null;
      const same = there !== null && sha(here) === sha(there);
      layers[layer] = { present: true, bytes: here.length, sha256: sha(here), equals_the_original: same, from: ref };
      if (!same) mismatched.push({ stage, layer, from: ref });
    }
    rows.push({ stage, present, layer_count: LAYERS.filter((l) => layers[l] && layers[l].present).length,
      layers, preserved_failure: PRESERVED.has(stage) });
  }
  const incomplete = rows.filter((r) => r.layer_count !== LAYERS.length);
  const incompleteNotPreserved = incomplete.filter((r) => !r.preserved_failure).map((r) => r.stage);
  // A preserved failure is not required to be incomplete. One of the five -- the failed h147 link -- has all eight layers,
  // because a link that fails still writes a guard row, an inner report and a raw channel carrying the linker's complaint.
  // Whether a preserved failure succeeded is decided by its own records in the chain judge, not by counting its layers here,
  // so this list is reported as a fact and is not a failure condition.
  const completeButPreserved = rows.filter((r) => r.preserved_failure && r.layer_count === LAYERS.length).map((r) => r.stage);
  // The material, measured against the tree it was copied from. Copying rehashed each member, and this re-reads them, so a
  // member that changed after it was written would show up here rather than only at the next archive.
  const materialMismatched = [];
  const materialRows = [];
  const materialSourceOf = (b) => (b === 'runtime' ? RUNTIME_SOURCE : MATERIAL_SOURCE + '/' + b);
  for (const bucket of MATERIAL_BUCKETS_ALL) {
    const src = materialSourceOf(bucket);
    if (!exists(src)) { materialMismatched.push({ bucket, why: 'absent at the source' }); continue; }
    let n = 0;
    const walk = (d, rel) => {
      for (const name of fs.readdirSync(d).sort()) {
        const full = d + '/' + name;
        const r = rel ? rel + '/' + name : name;
        const st = fs.statSync(full);
        if (st.isDirectory()) { walk(full, r); continue; }
        n += 1;
        const here = OUT + '/material/' + bucket + '/' + r;
        if (!exists(here)) { materialMismatched.push({ bucket, member: r, why: 'absent in the package' }); continue; }
        if (sha(fs.readFileSync(here)) !== sha(fs.readFileSync(full))) {
          materialMismatched.push({ bucket, member: r, why: 'differs from the original' });
        }
      }
    };
    walk(src, '');
    materialRows.push({ bucket, members: n });
  }
  const members = fs.readdirSync(OUT, { recursive: true }).filter((x) => fs.statSync(OUT + '/' + x).isFile());
  const totals = members.reduce((n, m) => n + fs.statSync(OUT + '/' + m).size, 0);
  const record = {
    task: 'GTOS-DSH-148', kind: 'package_tree_verified',
    what: 'the package tree measured against the originals it was copied from: every member rehashed, every stage '
      + 'classified as required or as a carried failure',
    at: OUT,
    members: members.length,
    bytes: totals,
    stages: rows.length,
    stages_with_all_eight_layers: rows.filter((r) => r.layer_count === LAYERS.length).length,
    incomplete: incomplete.map((r) => ({ stage: r.stage, layers_present: r.layer_count, preserved_failure: r.preserved_failure })),
    incomplete_that_is_not_a_preserved_failure: incompleteNotPreserved,
    preserved_failures_with_a_complete_chain: completeButPreserved,
    material: { source: MATERIAL_SOURCE, buckets: materialRows, mismatched: materialMismatched },
    preserved_failures_carried: rows.filter((r) => r.preserved_failure).map((r) => ({ stage: r.stage,
      layers_present: r.layer_count,
      why: (PRESERVED_FAILURES.find((p) => p.stage === r.stage) || {}).why,
      superseded_by: (PRESERVED_FAILURES.find((p) => p.stage === r.stage) || {}).superseded_by })),
    members_that_differ_from_their_original: mismatched,
    rows,
    ok: mismatched.length === 0 && incompleteNotPreserved.length === 0 && materialMismatched.length === 0
      && materialRows.length === MATERIAL_BUCKETS_ALL.length,
    ok_means: 'every packaged chain layer is byte-identical to the original it was copied from, every material member is '
      + 'byte-identical to the member it was copied from and all eight buckets are present, and the only incomplete stage '
      + 'directories are preserved failures',
  };
  const p = nextRecord('stage-148');
  fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  return { record, recordPath: p };
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
if (isMain) {
  const mode = process.argv[2] || 'build';
  if (mode === 'verify') {
    const { record, recordPath } = verify();
    console.log(JSON.stringify({ at: record.at, members: record.members, bytes: record.bytes, stages: record.stages,
      all_eight: record.stages_with_all_eight_layers, incomplete: record.incomplete,
      differ: record.members_that_differ_from_their_original, ok: record.ok, record: recordPath }, null, 2));
    process.exit(record.ok ? 0 : 1);
  }
  const { record, recordPath } = build({});
  console.log(JSON.stringify({ at: record.at, control: record.control.written,
    chain: { stages: record.chain.stages, complete: record.chain.complete,
      incomplete: record.chain.incomplete.map((i) => i.stage),
      preserved: record.chain.preserved_failures_carried,
      incomplete_not_preserved: record.chain.incomplete_that_is_not_a_preserved_failure },
    engine: record.engine.written, material: record.material, ok: record.ok, record: recordPath,
    tool_pins_agree: record.control.tool_pins_agree_with_the_147_contract }, null, 2));
  process.exit(record.ok ? 0 : 1);
}
