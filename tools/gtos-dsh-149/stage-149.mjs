// GTOS-DSH-149: the package tree, built before the archive so that T, C, D, E and the seal can all read the same bytes.
//
// The payload is not a directory of aggregates. It is the originals: for every stage the packet contracts for, the spec the
// stage was given, the control the runner froze, the helper that ran, the attempt reservation, the terminal, the guard's
// bridge record, the guard's own inner report and the raw channel the child wrote. Those bytes already exist in the 147
// report root and its two native roots; this file copies them into one tree, rehashing each copy, so the package is the
// originals and not a summary of them.
//
// WHAT CHANGED FROM 148. 148's tree carried eight material buckets and no bucket for the artifacts the ten prerequisite roles
// are filed against, so seven of those ten roles could only be decided by reading paths outside the package -- and because
// 148 answered T with its own required-set reader instead of the ten-role consumer, nobody saw it. This tree carries a ninth
// bucket for exactly those artifacts, and an origins record that says, for every material member, where its bytes were read
// from. That record is what lets a consumer be handed a path a role record names and open the member that holds those bytes.
//
// The tree is written under `R/package-149`. It is a staging tree, not the payload: the payload is the tar the engine writes
// from a plan over this tree, and the plan's hashes are taken from these files. The judgements are made about this tree and
// then collected into it, and the tree is measured after both are true of it.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };

const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const R147 = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const R140 = 'F:/GTOS-Chromium/artifacts/native-guarded-frame-recovery-repeatability-and-portable-consumption-20261027-a';
const WSL = '//wsl$/Ubuntu-20.04';
const TOOLS = R + '/worktree-149/tools/gtos-dsh-149';
const R148 = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const TOOLS147 = R147 + '/worktree-147/tools/gtos-dsh-147';
const TOOLS146 = R148 + '/worktree-148/tools/gtos-dsh-146';
const OUT = R + '/package-149';

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
  const routes = readJson(R + '/execution-identities-149.json').routes;
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
    task: 'GTOS-DSH-149',
    what: 'the identity every later spec and every chain judgement pins: the roots, the tools, the routes, and the source '
      + 'task whose executed originals this package carries',
    source_task: 'GTOS-DSH-147',
    source_task_means: 'the chain originals in this package are the 147 executions, preserved. They are not re-run here and '
      + 'are not counted as 148 executions; 148 executes the consumers, the publication and the judgements over them',
    roots: { report_root: R, package: OUT, work_root: '/var/tmp/gtos-dsh-149-e759-role-semantics',
      source_root: '/var/tmp/gtos-dsh-149-e759-role-semantics-view',
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
  const p = OUT + '/control/contract-149.json';
  fs.writeFileSync(p, JSON.stringify(contract, null, 2) + '\n', { flag: 'wx' });
  written.push(measure(p));
  return { written: written.length, missing, contract: measure(p), tool_pins_agree: contract.tool_pins_agree_with_the_147_contract,
    helper_drafts: Object.keys(contract.helper_drafts), helper_draft_disagreements: contract.helper_draft_disagreements };
};

const ENGINE_FILES = [
  [TOOLS + '/publish-149.mjs', 'engine/publish-149.mjs'],
  [TOOLS + '/drive-149.mjs', 'engine/drive-149.mjs'],
  [TOOLS + '/chain-149.mjs', 'engine/chain-149.mjs'],
  [TOOLS + '/f-negative-149.mjs', 'engine/f-negative-149.mjs'],
  [TOOLS + '/f-qualification-149.mjs', 'engine/f-qualification-149.mjs'],
  [TOOLS + '/c-negative-149.mjs', 'engine/c-negative-149.mjs'],
  [TOOLS + '/spec-149.mjs', 'engine/spec-149.mjs'],
  [TOOLS + '/stage-149.mjs', 'engine/stage-149.mjs'],
  // The H harness and the module it drives the closure role through. A payload whose roles were decided by a harness that
  // does not travel with it cannot be re-judged by a reader, and the harness is the only record of what the entries were
  // called with.
  [TOOLS + '/roles-149.mjs', 'engine/roles-149.mjs'],
  [TOOLS + '/prereq-149.mjs', 'engine/prereq-149.mjs'],
  [TOOLS147 + '/h-package-147.mjs', 'engine/h-package-147.mjs'],
  // The three consumers the T role calls, byte-identical to the ones 147 ran. They are the modules the ten-role entry is
  // made of, so a reader who wants to re-judge T from the payload has the entry and everything under it.
  [TOOLS147 + '/references/prerequisite-consumption-147.mjs', 'engine/references/prerequisite-consumption-147.mjs'],
  [TOOLS147 + '/references/resource-semantics-147.mjs', 'engine/references/resource-semantics-147.mjs'],
  [TOOLS147 + '/references/scene-admission-147.mjs', 'engine/references/scene-admission-147.mjs'],
  [TOOLS147 + '/c-chain-147.mjs', 'engine/c-chain-147.mjs'],
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
  // The two programs that judge the payload AFTER it is sealed. They travel for the same reason every other judge does: a
  // reader who has only the payload should be able to run the receipt that will be written about it, rather than take a
  // receipt's word for what the receipt did. Carrying them is also what makes the rehearsal meaningful -- the rehearsal runs
  // the same bytes the post-seal step will run, from the same tree, so the rehearsal cannot pass on a version of the receipt
  // that the seal will not use.
  [TOOLS + '/postseal-149.mjs', 'engine/postseal-149.mjs'],
  [TOOLS + '/rehearse-149.mjs', 'engine/rehearse-149.mjs'],
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
// The bytes the judgements read. Not a selection: the whole eight-bucket tree the 148 payload carried and its closure
// verified member for member -- the image and its map, the producer objects and their depfiles, the host ELFs, the host case
// logs and results, the guest channels and their inner reports, the profile, the source view, and the two hundred and six
// runtime files. A consumer handed a package has to be able to answer its question from the package, so the package carries
// every byte the consumers read. The source is the 148 payload's own staging tree, which the 148 seal verified at 2138 of
// 2138 members; copying from there means this package inherits exactly that set rather than a re-selection of it.
const MATERIAL_SOURCE = R148 + '/package-148/material';
const MATERIAL_BUCKETS = ['canonical', 'guest', 'host', 'image', 'producers', 'profile', 'sources', 'runtime'];
// Every bucket the tree is supposed to hold, including the one that has no source directory: the role artifacts are copied
// one entry at a time from the paths the roles name, not walked out of a tree. The list is separate from the walk list so
// that "all the buckets are present" is a claim about the whole set and not about the subset that happens to be walkable.
const MATERIAL_BUCKET_NAMES = [...MATERIAL_BUCKETS, 'role-artifacts', 'origins', 'guest-records', 'prior-contracts'];

// THE NINTH BUCKET, and the reason this packet carries one at all.
//
// The ten prerequisite roles are filed against artifacts that the 147 material set does not contain: the order spec the
// producer contract froze against, the checkpoint binding module the link input read names, the runtime manifest, the
// runtime verification record, and the four host tools the tooling subject pins. The 148 package answered 7 of the 10 roles
// with its own readers and never called the ten-role consumer, so nobody noticed that the consumer would have opened paths
// outside the package for every one of them. It is not enough to say those paths exist on this host: a package whose roles
// can only be decided by reading the machine that built it is not a package. So the bytes are carried.
//
// Each entry names the artifact the role pins, the member it is carried as, and where the bytes were read from. The reader
// then resolves every path a role record names to a member by the pin the record carries, and the consumer re-reads that
// member and compares the same pin again -- so a wrong copy fails rather than passes.
const ROLE_ARTIFACTS = [
  { role: 'source_profile_contract', what: 'the order spec the producer contract was frozen against',
    pin_path: R140 + '/stage-output/lkp14010-01.spec.json', member: 'material/role-artifacts/lkp14010-01.spec.json' },
  { role: 'link_input_consumption', what: 'the checkpoint binding module the link input read pins',
    pin_path: R147 + '/worktree-147/tools/gtos-dsh-147/references/private-boot-checkpoints-147.mjs',
    member: 'material/role-artifacts/private-boot-checkpoints-147.mjs' },
  { role: 'runtime_environment', what: 'the runtime materialisation manifest',
    pin_path: R147 + '/runtime-manifest-147.json', member: 'material/role-artifacts/runtime-manifest-147.json' },
  { role: 'runtime_environment', what: 'the runtime verification record the manifest is checked by',
    pin_path: '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-147-e759-scene-bound-view/evidence/rvf147-01.json',
    member: 'material/role-artifacts/rvf147-01.json' },
  { role: 'tooling_subject', what: 'the frozen runner', pin_path: R147 + '/frozen-runners/run-147-r01.mjs',
    member: 'material/role-artifacts/run-147-r01.mjs' },
  { role: 'tooling_subject', what: 'the capture guard',
    pin_path: 'F:/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
    member: 'material/role-artifacts/guard-041-capture.py' },
  { role: 'tooling_subject', what: 'the compiler', pin_path: '//wsl$/Ubuntu-20.04/usr/bin/x86_64-linux-gnu-g++-9',
    member: 'material/role-artifacts/x86_64-linux-gnu-g++-9' },
  { role: 'tooling_subject', what: 'the assembler', pin_path: '//wsl$/Ubuntu-20.04/usr/bin/x86_64-linux-gnu-as',
    member: 'material/role-artifacts/x86_64-linux-gnu-as' },
  { role: 'tooling_subject', what: 'the linker', pin_path: '//wsl$/Ubuntu-20.04/usr/bin/x86_64-linux-gnu-ld.bfd',
    member: 'material/role-artifacts/x86_64-linux-gnu-ld.bfd' },
];

// THE TENTH BUCKET: the origin chains of the forty-nine reused producer slots.
//
// THE FINDING THIS ANSWERS. The 147 build record says, for each of the sixty-one link slots, either that this packet rebuilt
// it or that it was reused. For the forty-nine reused slots it records one thing about the origin: a `from` path. The 148
// chain judge required that path to be non-empty and then said, in the record, that a reused slot "is not required to have a
// chain here". That is the whole of what the packet knew about where two thirds of its own image came from, and it is not
// provenance: a path is a string, and a string cannot be re-read.
//
// So each reused slot's origin chain is carried. The origin is the 140 packet's producer closure, which executed sixty-one
// producer stages and recorded, for each, an attempt reservation, a terminal result, a spec, a guard evidence record, a stage
// result and the frozen helper -- the same typed shape this packet uses for its own stages. The slots are matched to those
// stages BY CONTENT: the slot's own sha256 is looked up among the closure's provenance rows, so a slot cannot be attached to
// an origin by sharing a file name with it. Forty-nine of forty-nine match exactly, which is the fact the 148 record could
// not state.
const ORIGIN_TASK = 'GTOS-DSH-140';
const ORIGIN_KINDS = [
  { kind: 'attempt', rel: (s) => 'attempt-' + s + '-140.json' },
  { kind: 'result', rel: (s) => 'result-' + s + '-140.json' },
  { kind: 'spec', rel: (s) => 'stage-output/' + s + '.spec.json' },
  { kind: 'evidence', rel: (s) => 'evidence/' + s + '.json' },
  { kind: 'results', rel: (s) => 'results/' + s + '.json' },
  { kind: 'helper', rel: (s) => 'frozen-helpers/' + s + '.mjs' },
];
// The two records that name the closure itself, carried so a reader can re-derive the slot-to-stage match rather than
// believe this packet's list of it.
const ORIGIN_RECORDS = ['producer-closure-p140-05.json', 'producer-contract-p140-05.json'];
// The origin stage of each reused slot, resolved by content. Read from the 148 package's own copy of the build record and
// from the 140 closure, both of which are inputs; the answer is checked to be a bijection, so two slots cannot claim one
// origin and an origin cannot be claimed twice.
const resolveOriginStages = () => {
  const pb = JSON.parse(fs.readFileSync(R148 + '/package-148/control/p-build-147a.json', 'utf8').replace(/^\uFEFF/, ''));
  const closure = JSON.parse(fs.readFileSync(R140 + '/producer-closure-p140-05.json', 'utf8').replace(/^\uFEFF/, ''));
  const bySha = new Map(closure.provenance.map((p) => [p.sha256, p]));
  const byStage = new Map(closure.built.map((b) => [b.stage, b]));
  const rows = [];
  const problems = [];
  for (const r of pb.reused) {
    const p = bySha.get(r.sha256);
    if (!p) { problems.push({ slot: r.slot, basename: r.basename, why: 'no provenance row in the origin closure carries these bytes' }); continue; }
    const b = byStage.get(p.stage);
    if (!b) { problems.push({ slot: r.slot, stage: p.stage, why: 'the origin stage has no built row' }); continue; }
    rows.push({ slot: r.slot, basename: r.basename, bytes: r.bytes, sha256: r.sha256, from: r.from,
      origin_task: closure.task, origin_stage: p.stage, origin_token: p.token, origin_kind: b.kind,
      origin_logical: b.logical, origin_object: b.object, origin_depfile: b.depfile });
  }
  const stages = rows.map((r) => r.origin_stage);
  if (new Set(stages).size !== stages.length) problems.push({ why: 'two slots claim one origin stage', stages: stages.length,
    distinct: new Set(stages).size });
  return { rows, problems, declared: pb.reused.length, total: pb.total, rebuilt: pb.rebuilt.length };
};

// THE ELEVENTH AND TWELFTH BUCKETS: the per-trial records the matrix pins, and the prior assembly contract the 147 assembly
// contract is an amendment of.
//
// Both were found by deriving the required set from the real contracts instead of inheriting a table. The guest matrix pins,
// for each of its twenty-eight trials, a record file -- path, bytes, hash -- and the package did not carry those files, so a
// consumer reading the matrix could read every channel and still not resolve a third of what the matrix names. The 147
// assembly contract pins the 146 contract it amends, in the same shape. Neither is scaffolding: both are records a consumer
// re-reads. So both are carried, and the requirement derivation resolves them by their exact pins rather than by name.
const GUEST_TRIAL_RECORD = (stage) => R147 + '/g-trial-' + stage + '-147.json';
const PRIOR_CONTRACTS = [
  { what: 'the 146 assembly source contract the 147 contract amends',
    pin_path: 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a/assembly-source-contract-146.json',
    member: 'material/prior-contracts/assembly-source-contract-146.json' },
];

const buildMaterial = () => {
  if (!exists(MATERIAL_SOURCE)) throw new Error('the 148 material tree is absent at ' + MATERIAL_SOURCE);  const written = [];
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
  // The ninth bucket: the artifacts the ten roles are filed against.
  const roleArtifacts = [];
  for (const a of ROLE_ARTIFACTS) {
    if (!exists(a.pin_path)) { missing.push(a.member); roleArtifacts.push({ ...a, present: false }); continue; }
    const rec = copyTo(a.pin_path, OUT + '/' + a.member);
    written.push({ to: a.member.replace(/^material\//, ''), ...rec });
    byBucket['role-artifacts'] = (byBucket['role-artifacts'] || 0) + 1;
    roleArtifacts.push({ role: a.role, what: a.what, pin_path: a.pin_path, member: a.member, bytes: rec.bytes,
      sha256: rec.sha256, present: true });
  }
  return { written: written.length, by_bucket: byBucket, missing, role_artifacts: roleArtifacts,
    bytes: written.reduce((n, w) => n + w.bytes, 0) };
};

// The eleventh bucket: the twenty-eight per-trial records the guest matrix pins, each copied only if its bytes are the bytes
// the matrix pinned. A record that drifted is refused here rather than carried as if it were the one the matrix names.
const buildGuestRecords = () => {
  const matrix = JSON.parse(fs.readFileSync(OUT + '/control/g-matrix-gm147-01-147.json', 'utf8').replace(/^\uFEFF/, ''));
  const written = [];
  const missing = [];
  const rows = [];
  for (const t of matrix.trials) {
    const pin = t.record;
    const src = GUEST_TRIAL_RECORD(t.stage);
    const member = 'material/guest-records/' + t.stage + '.json';
    if (!exists(src)) { missing.push(member); rows.push({ stage: t.stage, member, present: false, why: 'absent at the source' }); continue; }
    const b = fs.readFileSync(src);
    if (sha(b) !== String(pin.sha256) || b.length !== pin.bytes) {
      missing.push(member);
      rows.push({ stage: t.stage, member, present: false, why: 'the source bytes are not the bytes the matrix pinned',
        pinned: { bytes: pin.bytes, sha256: pin.sha256 }, found: { bytes: b.length, sha256: sha(b) } });
      continue;
    }
    const rec = copyTo(src, OUT + '/' + member);
    written.push({ to: member.replace(/^material\//, ''), ...rec });
    rows.push({ stage: t.stage, member, bytes: rec.bytes, sha256: rec.sha256, present: true,
      matrix_pin_agrees: true });
  }
  return { written: written.length, missing, rows };
};

// The twelfth bucket: the prior contracts the packet's own contracts amend.
const buildPriorContracts = () => {
  const written = [];
  const missing = [];
  const rows = [];
  for (const a of PRIOR_CONTRACTS) {
    if (!exists(a.pin_path)) { missing.push(a.member); rows.push({ ...a, present: false }); continue; }
    const rec = copyTo(a.pin_path, OUT + '/' + a.member);
    written.push({ to: a.member.replace(/^material\//, ''), ...rec });
    rows.push({ what: a.what, pin_path: a.pin_path, member: a.member, bytes: rec.bytes, sha256: rec.sha256, present: true });
  }
  return { written: written.length, missing, rows };
};

// The tenth bucket, copied one member at a time like the ninth, because the origin records do not live in one tree either:
// they are the 140 packet's attempt, result, spec, evidence, stage result and frozen helper for each origin stage.
const buildOriginMaterial = (originRows) => {
  const written = [];
  const missing = [];
  const rows = [];
  for (const r of originRows) {
    const files = [];
    for (const k of ORIGIN_KINDS) {
      const src = R140 + '/' + k.rel(r.origin_stage);
      const member = 'material/origins/' + r.origin_stage + '/' + k.kind
        + (k.kind === 'helper' ? '.mjs' : '.json');
      if (!exists(src)) { missing.push(member); files.push({ kind: k.kind, member, present: false }); continue; }
      const rec = copyTo(src, OUT + '/' + member);
      written.push({ to: member.replace(/^material\//, ''), ...rec });
      files.push({ kind: k.kind, member, origin: src, bytes: rec.bytes, sha256: rec.sha256, present: true });
    }
    rows.push({ ...r, files });
  }
  const records = [];
  for (const name of ORIGIN_RECORDS) {
    const src = R140 + '/' + name;
    const member = 'material/origins/_records/' + name;
    if (!exists(src)) { missing.push(member); records.push({ name, member, present: false }); continue; }
    const rec = copyTo(src, OUT + '/' + member);
    written.push({ to: member.replace(/^material\//, ''), ...rec });
    records.push({ name, member, origin: src, bytes: rec.bytes, sha256: rec.sha256, present: true });
  }
  return { written: written.length, missing, rows, records,
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
import { PRESERVED_FAILURES } from './chain-149.mjs';

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
  add('f-negatives-149.json', 'judgement');
  add('f-qualification-149.json', 'judgement');
  // Every record of each judge's series, not a name written down here. A list of names has to be edited every time a judge is
  // re-run, and the one time it is not edited the package carries a superseded verdict while the newer one sits in the report
  // root -- which is exactly the failure this packet exists to close.
  const series = (pattern, role) => {
    for (const n of fs.readdirSync(R).filter((x) => pattern.test(x)).sort()) add(n, role);
  };
  series(/^c-chain-149-\d+\.json$/, 'judgement');
  series(/^c-negatives-149-\d+\.json$/, 'judgement');
  series(/^required-set-149-\d+\.json$/, 'judgement');
  series(/^stage-149-\d+\.json$/, 'judgement');
  // The H role records are NOT collected here, and the reason is a dependency rather than a preference.
  //
  // A role record is a judgement about this tree: its subject is a member of the tree and its trace names the paths it read.
  // So it cannot be written before the tree exists, and a build that copied the records from a previous run would be shipping
  // verdicts about a tree that no longer exists -- which is precisely the 148 failure the review names, where the payload
  // carried an older harness beside records that harness did not produce. They arrive through `collect`, after the roles have
  // been executed against this tree, and `collect` refuses any name whose bytes differ from what it holds.
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
  // The tenth bucket: the forty-nine origin chains, resolved by content and carried whole.
  const originResolution = resolveOriginStages();
  const originsMaterial = buildOriginMaterial(originResolution.rows);
  material.by_bucket['origins'] = originsMaterial.written;
  material.written += originsMaterial.written;
  material.bytes += originsMaterial.bytes;
  if (originsMaterial.missing.length) material.missing.push(...originsMaterial.missing);
  // The eleventh and twelfth buckets: the per-trial records the matrix pins, and the prior contracts the packet's own
  // contracts amend.
  const guestRecords = buildGuestRecords();
  material.by_bucket['guest-records'] = guestRecords.written;
  material.written += guestRecords.written;
  if (guestRecords.missing.length) material.missing.push(...guestRecords.missing);
  const priorContracts = buildPriorContracts();
  material.by_bucket['prior-contracts'] = priorContracts.written;
  material.written += priorContracts.written;
  if (priorContracts.missing.length) material.missing.push(...priorContracts.missing);
  // The origins record. Every material member is written beside the path it was read from, so a reader can answer "where did
  // this byte come from" from the package rather than from the report root, and so the alias map the ten roles need is
  // derivable from the package instead of from the machine. It is written after the copy and re-reads each member, so it
  // describes the bytes that are there rather than the bytes that were meant to be there.
  const origins = [];
  for (const b of MATERIAL_BUCKETS) {
    const walk = (d, rel) => {
      for (const n of fs.readdirSync(d).sort()) {
        const full = d + '/' + n;
        const r = rel ? rel + '/' + n : n;
        if (fs.statSync(full).isDirectory()) { walk(full, r); continue; }
        const here = OUT + '/material/' + b + '/' + r;
        origins.push({ member: 'material/' + b + '/' + r, origin: full, bytes: fs.statSync(here).size,
          sha256: sha(fs.readFileSync(here)) });
      }
    };
    if (exists(MATERIAL_SOURCE + '/' + b)) walk(MATERIAL_SOURCE + '/' + b, '');
  }
  for (const a of material.role_artifacts) {
    if (!a.present) continue;
    origins.push({ member: a.member, origin: a.pin_path, bytes: a.bytes, sha256: a.sha256, role: a.role });
  }
  // The tenth bucket's members, so the origins record covers every material member and not only the walked ones.
  for (const r of originsMaterial.rows) {
    for (const f of r.files) {
      if (!f.present) continue;
      origins.push({ member: f.member, origin: f.origin, bytes: f.bytes, sha256: f.sha256,
        origin_task: ORIGIN_TASK, origin_stage: r.origin_stage, slot: r.slot });
    }
  }
  for (const r of originsMaterial.records) {
    if (!r.present) continue;
    origins.push({ member: r.member, origin: r.origin, bytes: r.bytes, sha256: r.sha256, origin_task: ORIGIN_TASK });
  }
  for (const r of guestRecords.rows) {
    if (!r.present) continue;
    origins.push({ member: r.member, origin: GUEST_TRIAL_RECORD(r.stage), bytes: r.bytes, sha256: r.sha256,
      role: 'guest_trial_record', stage: r.stage });
  }
  for (const r of priorContracts.rows) {
    if (!r.present) continue;
    origins.push({ member: r.member, origin: r.pin_path, bytes: r.bytes, sha256: r.sha256, role: 'prior_contract' });
  }
  const originsPath = OUT + '/control/material-origins-149.json';
  fs.writeFileSync(originsPath, JSON.stringify({
    task: 'GTOS-DSH-149', kind: 'material_origins',
    what: 'every member of the material set with the absolute path its bytes were read from and the bytes that are there now',
    members: origins.length,
    buckets: [...MATERIAL_BUCKETS, 'role-artifacts', 'origins', 'guest-records', 'prior-contracts'],
    note: 'the origin paths are the ones the producing packet used; they are recorded so a consumer can be handed a mapping '
      + 'from a path a record names to the member that holds those bytes, resolved by the pin the record carries',
    rows: origins,
  }, null, 2) + '\n', { flag: 'wx' });
  const incomplete = chain.incomplete.map((i) => i.stage);
  const incompleteThatIsNotAPreservedFailure = incomplete.filter((s) => !PRESERVED.has(s));
  // A preserved failure's absent layers are the fact that is being preserved, so they are carried in the record and are not
  // counted as problems with the build. Everything else is.
  const chainProblemsThatAreNotPreserved = chain.problems.filter((p) => !PRESERVED.has(p.stage));
  const chainProblemsThatArePreserved = chain.problems.filter((p) => PRESERVED.has(p.stage));
  const preservedThatIsComplete = chain.rows.filter((r) => PRESERVED.has(r.stage) && r.layer_count === LAYERS.length)
    .map((r) => r.stage);
  const record = {
    task: 'GTOS-DSH-149', kind: 'package_tree',
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
    material_origins: { path: originsPath, members: origins.length },
    origin_chains: { declared_reused: originResolution.declared, resolved: originResolution.rows.length,
      problems: originResolution.problems, written: originsMaterial.written,
      missing: originsMaterial.missing, records: originsMaterial.records,
      stages: originsMaterial.rows.map((r) => ({ slot: r.slot, basename: r.basename, origin_stage: r.origin_stage,
        origin_kind: r.origin_kind, files: r.files.length })) },
    ok: control.missing.length === 0 && engine.missing.length === 0 && material.missing.length === 0
      && chainProblemsThatAreNotPreserved.length === 0 && incompleteThatIsNotAPreservedFailure.length === 0
      && [...PRESERVED].every((s) => chain.rows.some((r) => r.stage === s))
      && control.helper_drafts.length > 0 && control.helper_draft_disagreements.length === 0
      && originResolution.problems.length === 0 && originsMaterial.missing.length === 0
      && originResolution.rows.length === originResolution.declared,
    ok_means: 'every control record, every engine module and every material file was found where the 147 execution left it, '
      + 'every stage the contracts require has all eight layers, the only incomplete stage directories are the four named '
      + 'preserved failures, each of which is present with its own original layers, every stage naming the same helper '
      + 'draft carries the same helper bytes, and every one of the reused producer slots resolves by content to an origin '
      + 'stage whose whole typed chain was copied into the package',
  };
  const p = nextRecord('stage-149');
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
  const materialSourceOf = (b) => MATERIAL_SOURCE + '/' + b;
  for (const bucket of MATERIAL_BUCKETS) {
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
  // The ninth bucket, re-measured the same way: every role artifact re-read at its origin and compared with the member.
  const roleRows = [];
  for (const a of ROLE_ARTIFACTS) {
    const here = OUT + '/' + a.member;
    if (!exists(a.pin_path)) { materialMismatched.push({ bucket: 'role-artifacts', member: a.member, why: 'absent at the origin' }); continue; }
    if (!exists(here)) { materialMismatched.push({ bucket: 'role-artifacts', member: a.member, why: 'absent in the package' }); continue; }
    const o = sha(fs.readFileSync(a.pin_path));
    const h = sha(fs.readFileSync(here));
    if (o !== h) materialMismatched.push({ bucket: 'role-artifacts', member: a.member, why: 'differs from the origin' });
    roleRows.push({ role: a.role, member: a.member, bytes: fs.statSync(here).size, sha256: h, origin: a.pin_path, matches: o === h });
  }
  materialRows.push({ bucket: 'role-artifacts', members: roleRows.length });
  // The tenth bucket, re-measured the same way: every origin-chain member re-read at the 140 record it was copied from and
  // compared with the member, and every reused slot required to still resolve to the same origin stage by content.
  const originRowsVerified = [];
  const originResolutionVerified = resolveOriginStages();
  if (originResolutionVerified.problems.length) {
    materialMismatched.push({ bucket: 'origins', why: 'the slots no longer resolve to origin stages',
      problems: originResolutionVerified.problems });
  }
  for (const r of originResolutionVerified.rows) {
    const files = [];
    for (const k of ORIGIN_KINDS) {
      const src = R140 + '/' + k.rel(r.origin_stage);
      const member = 'material/origins/' + r.origin_stage + '/' + k.kind + (k.kind === 'helper' ? '.mjs' : '.json');
      const here = OUT + '/' + member;
      if (!exists(src)) { materialMismatched.push({ bucket: 'origins', member, why: 'absent at the origin' }); continue; }
      if (!exists(here)) { materialMismatched.push({ bucket: 'origins', member, why: 'absent in the package' }); continue; }
      const o = sha(fs.readFileSync(src));
      const h = sha(fs.readFileSync(here));
      if (o !== h) materialMismatched.push({ bucket: 'origins', member, why: 'differs from the origin' });
      files.push({ kind: k.kind, member, bytes: fs.statSync(here).size, sha256: h, origin: src, matches: o === h });
    }
    originRowsVerified.push({ slot: r.slot, basename: r.basename, origin_stage: r.origin_stage,
      origin_kind: r.origin_kind, files });
  }
  const originRecordRows = [];
  for (const name of ORIGIN_RECORDS) {
    const src = R140 + '/' + name;
    const member = 'material/origins/_records/' + name;
    const here = OUT + '/' + member;
    if (!exists(src)) { materialMismatched.push({ bucket: 'origins', member, why: 'absent at the origin' }); continue; }
    if (!exists(here)) { materialMismatched.push({ bucket: 'origins', member, why: 'absent in the package' }); continue; }
    const o = sha(fs.readFileSync(src));
    const h = sha(fs.readFileSync(here));
    if (o !== h) materialMismatched.push({ bucket: 'origins', member, why: 'differs from the origin' });
    originRecordRows.push({ name, member, bytes: fs.statSync(here).size, sha256: h, matches: o === h });
  }
  materialRows.push({ bucket: 'origins', members: originRowsVerified.reduce((n, r) => n + r.files.length, 0)
    + originRecordRows.length });
  // The eleventh and twelfth buckets, re-measured the same way: every per-trial record re-read at the 147 record the matrix
  // pins, and every prior contract re-read at its origin.
  const guestRecordRows = [];
  const matrixForVerify = JSON.parse(fs.readFileSync(OUT + '/control/g-matrix-gm147-01-147.json', 'utf8').replace(/^\uFEFF/, ''));
  for (const t of matrixForVerify.trials) {
    const src = GUEST_TRIAL_RECORD(t.stage);
    const member = 'material/guest-records/' + t.stage + '.json';
    const here = OUT + '/' + member;
    if (!exists(src)) { materialMismatched.push({ bucket: 'guest-records', member, why: 'absent at the origin' }); continue; }
    if (!exists(here)) { materialMismatched.push({ bucket: 'guest-records', member, why: 'absent in the package' }); continue; }
    const o = sha(fs.readFileSync(src));
    const h = sha(fs.readFileSync(here));
    if (o !== h) materialMismatched.push({ bucket: 'guest-records', member, why: 'differs from the origin' });
    if (h !== String(t.record.sha256)) materialMismatched.push({ bucket: 'guest-records', member,
      why: 'the packaged record is not the record the matrix pins' });
    guestRecordRows.push({ stage: t.stage, member, bytes: fs.statSync(here).size, sha256: h,
      matrix_pin_sha256: t.record.sha256, matches: o === h && h === String(t.record.sha256) });
  }
  materialRows.push({ bucket: 'guest-records', members: guestRecordRows.length });
  const priorRows = [];
  for (const a of PRIOR_CONTRACTS) {
    const here = OUT + '/' + a.member;
    if (!exists(a.pin_path)) { materialMismatched.push({ bucket: 'prior-contracts', member: a.member, why: 'absent at the origin' }); continue; }
    if (!exists(here)) { materialMismatched.push({ bucket: 'prior-contracts', member: a.member, why: 'absent in the package' }); continue; }
    const o = sha(fs.readFileSync(a.pin_path));
    const h = sha(fs.readFileSync(here));
    if (o !== h) materialMismatched.push({ bucket: 'prior-contracts', member: a.member, why: 'differs from the origin' });
    priorRows.push({ member: a.member, bytes: fs.statSync(here).size, sha256: h, matches: o === h });
  }
  materialRows.push({ bucket: 'prior-contracts', members: priorRows.length });
  const members = fs.readdirSync(OUT, { recursive: true }).filter((x) => fs.statSync(OUT + '/' + x).isFile());
  const totals = members.reduce((n, m) => n + fs.statSync(OUT + '/' + m).size, 0);
  const record = {
    task: 'GTOS-DSH-149', kind: 'package_tree_verified',
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
    material: { source: MATERIAL_SOURCE, buckets: materialRows, mismatched: materialMismatched,
      role_artifacts: roleRows, origin_chains: originRowsVerified, origin_records: originRecordRows,
      guest_records: guestRecordRows, prior_contracts: priorRows },
    origin_chains: { declared_reused: originResolutionVerified.declared,
      resolved: originResolutionVerified.rows.length,
      slots_with_all_six_members: originRowsVerified.filter((r) => r.files.length === ORIGIN_KINDS.length
        && r.files.every((f) => f.matches)).length, records: originRecordRows },
    guest_records: { members: guestRecordRows.length,
      all_are_the_bytes_the_matrix_pins: guestRecordRows.every((r) => r.matches === true) },
    prior_contracts: { members: priorRows.length, all_match: priorRows.every((r) => r.matches === true) },
    preserved_failures_carried: rows.filter((r) => r.preserved_failure).map((r) => ({ stage: r.stage,
      layers_present: r.layer_count,
      why: (PRESERVED_FAILURES.find((p) => p.stage === r.stage) || {}).why,
      superseded_by: (PRESERVED_FAILURES.find((p) => p.stage === r.stage) || {}).superseded_by })),
    members_that_differ_from_their_original: mismatched,
    rows,
    ok: mismatched.length === 0 && incompleteNotPreserved.length === 0 && materialMismatched.length === 0
      && materialRows.map((r) => r.bucket).join() === MATERIAL_BUCKET_NAMES.join(),
    ok_means: 'every packaged chain layer is byte-identical to the original it was copied from, every material member is '
      + 'byte-identical to the member it was copied from and all eight buckets are present, and the only incomplete stage '
      + 'directories are preserved failures',
  };
  const p = nextRecord('stage-149');
  fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  return { record, recordPath: p };
};

// ---------------------------------------------------------------- collecting the judgements into the tree
//
// The judgements are made about the tree, so they are made after it exists and then moved into it. Nothing in the tree is
// rewritten to make room for them: `collect` writes only paths that are not there yet, and a judgement whose member already
// exists is never replaced. That is the property the 148 review found missing -- there, three meaningful corrections were
// committed after the seal, and the payload carried older copies of the two modules that produced its own role records. Here
// the record and the module that wrote it are both inside the tree, and the tree is measured after both are in it.
//
// COLLECT IS RE-RUNNABLE, AND THAT IS NOT A LOOSENING. Two of the judgements cannot be made until the tree carries the
// others: the seven consumer roles read the required set out of the tree, so the required set has to be in it before they
// run, and their own records can only be collected after they have. A `collect` that refused on a second run would force
// either a rebuild per judgement or a tree assembled by hand. So a member already present is not a refusal when its bytes
// are the bytes the record now has -- it is `already_present`, and the two cases are reported separately. A member whose
// bytes DIFFER is still refused, and nothing is ever overwritten: the older member stays exactly as it was and the
// difference is named. That is what makes a second run safe: it can only add, and it can only agree.
export const collect = () => {
  if (!exists(OUT)) throw new Error('there is no tree to collect into at ' + OUT);
  const added = [];
  const already = [];
  const refused = [];
  const put = (from, rel) => {
    const to = OUT + '/judgements/' + rel;
    if (!exists(from)) { refused.push({ member: 'judgements/' + rel, why: 'absent at the report root', from }); return; }
    const src = fs.readFileSync(from);
    if (exists(to)) {
      const had = fs.readFileSync(to);
      if (sha(had) === sha(src) && had.length === src.length) {
        already.push({ member: 'judgements/' + rel, bytes: had.length, sha256: sha(had) });
      } else {
        refused.push({ member: 'judgements/' + rel, why: 'the tree already carries a different record under this name',
          tree_bytes: had.length, tree_sha256: sha(had), record_bytes: src.length, record_sha256: sha(src) });
      }
      return;
    }
    added.push({ member: 'judgements/' + rel, ...copyTo(from, to) });
  };
  // The single records, by exact name. A record this packet did not write is not collected: the list is the packet's own
  // statement of what it produced, and a glob over the report root would collect whatever else happened to be lying there.
  for (const n of ['f-negatives-149.json', 'f-qualification-149.json']) put(R + '/' + n, n);
  // The series. Each is a family that grew one record per attempt, and all of them travel: a superseded record is part of
  // what happened, and a reader who sees only the last one cannot tell an attempt from a first try.
  const series = [/^c-chain-149-\d+\.json$/, /^c-negatives-149-\d+\.json$/, /^required-set-149-\d+\.json$/,
    /^stage-149-\d+\.json$/, /^t-prerequisites-149-\d+\.json$/, /^d-closure-149-\d+\.json$/,
    /^static-149-\d+\.json$/, /^guest-149-\d+\.json$/, /^host-149-\d+\.json$/];
  for (const n of fs.readdirSync(R).sort()) {
    if (!series.some((re) => re.test(n))) continue;
    put(R + '/' + n, n);
  }
  // The role records: the index, the summary and one record per role, each in its own directory. `cases/` is skipped for the
  // same reason the build skips it: what it holds is the damaged copies the negatives were refused on -- and, for two of them,
  // a whole copy of the tree that was damaged -- so it is test scaffolding, not evidence. Collecting it would put hundreds of
  // megabytes of deliberately damaged material into the payload under a name that says it is a judgement.
  if (exists(R + '/roles-149')) {
    const walk = (d, rel) => {
      for (const n of fs.readdirSync(d).sort()) {
        const full = d + '/' + n;
        const r = rel ? rel + '/' + n : n;
        if (r === 'cases' || r.startsWith('cases/')) continue;
        if (fs.statSync(full).isDirectory()) { walk(full, r); continue; }
        put(full, 'roles-149/' + r);
      }
    };
    walk(R + '/roles-149', '');
  }
  const p = nextRecord('stage-149');
  const record = {
    task: 'GTOS-DSH-149', kind: 'package_tree_judgements_collected',
    what: 'the judgement records this packet wrote, moved into the tree that is about to become the payload',
    at: OUT,
    added: added.length,
    bytes: added.reduce((n, a) => n + a.bytes, 0),
    already_present: already.length,
    already_present_members: already.map((a) => a.member),
    refused,
    members: added.map((a) => a.member),
    ok: refused.length === 0 && (added.length + already.length) > 0,
    ok_means: 'every judgement record this packet wrote is now a member of the tree, either added by this run or already '
      + 'present with the same bytes, and no member of the tree was replaced to make room for one',
  };
  fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  return { record, recordPath: p };
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
if (isMain) {
  const mode = process.argv[2] || 'build';
  if (mode === 'collect') {
    const { record, recordPath } = collect();
    console.log(JSON.stringify({ at: record.at, added: record.added, bytes: record.bytes, refused: record.refused,
      ok: record.ok, record: recordPath }, null, 2));
    process.exit(record.ok ? 0 : 1);
  }
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
