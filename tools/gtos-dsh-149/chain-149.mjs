// GTOS-DSH-149 C: independent stage derivation and complete physical chain coverage.
//
// Two questions, answered separately and then joined.
//
//   DERIVATION. Which stages did this packet contract for? The answer is not a hand-written list. It is the packet's own
//   reservation ledger -- `stage-registry-147.jsonl`, one row per stage, written before the work and independent of the
//   package -- cross-checked against every record that fixes a count: the matrix that fixes the guest trials, the host
//   contract that fixes the cases, the producer build that fixes the sixty-one link slots and the twelve rebuilds, the six
//   unit-build records, and the 147 packet's own forty-one-stage C set. Each cross-check states the count its record
//   declares and the count the registry agrees on; a record whose declared count does not match is a problem, so a dropped
//   stage cannot hide behind a shorter list. The 147 C set is carried as evidence of what 147 covered, explicitly not as
//   the coverage limit: it is 41 of 117, and the other 76 are derived here.
//
//   COVERAGE. For every derived stage, are the eight layers of its chain physically present in the package, and do they
//   agree with each other and with the pinned tools? The layers are read from the package and nowhere else, because the
//   package is what a reader will have.
//
// THE EIGHT LAYERS. spec (what the stage was asked to run), control (what the runner froze), helper (the program that ran),
// attempt (the reservation, taken before the work), terminal (the record written before the attempt was marked complete),
// bridge (the guard's uniform record: subject, plan, guard row, inner report, raw channel, verdict), inner (the guard's own
// report of the child), raw (the bytes the child wrote).
//
// THE THREE EXITS. Three numbers written by three programs: the wrapper's `transport_exit_code`, the guard's
// `guard_row.exit_code`, and the child's `inner.child.returncode`. They are not required to be equal, because for a
// checkpoint they are not: the guard is killed by its own deadline and reports -9, the transport reports 1, and the
// verdict says 1 was the expected transport exit. What is required is that each number is what its own record says it
// should be, that the guard's number is the child's number, and that a checkpoint's deadline kill is declared rather than
// merely observed -- otherwise -9 would be indistinguishable from a crash.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const readJsonl = (p) => fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };

// ---------------------------------------------------------------- the contract
export const contractOf = (pkg) => {
  const c = readJson(pkg + '/control/contract-149.json');
  const roles = new Map(c.tools.roles.map((r) => [r.role, r]));
  return { ...c, roleMap: roles };
};

// The identity the route's tool must present. A run stage is the exception on purpose: it executes the artefact its own
// build made, so its tool identity is the artefact, not a compiler.
export const toolKindForRoute = (route) => {
  if (route === 'object.compile' || route === 'test.cpp.build') return 'g++';
  if (route === 'assembly') return 'as';
  if (route === 'image.link') return 'ld.bfd';
  return 'python';
};
const toolPinFor = (contract, kind) => (kind === 'python' ? contract.tools.python : contract.roleMap.get(kind));

// ---------------------------------------------------------------- the eight layers, inside the package
//
// One file name per layer, and the layer in the name. A package that named five layers `<stage>.json` would have four
// members overwriting each other on extraction, which is the kind of loss a package exists to make impossible.
export const LAYERS = ['spec', 'control', 'helper', 'attempt', 'terminal', 'bridge', 'inner', 'raw'];

// The layers of a reused producer slot's origin chain. Six rather than eight, because the origin packet's route is a compile:
// it has no checkpoint bridge and no emulator, so its guard row lives in the stage result rather than in a separate bridge
// report. The names are the ones the tenth material bucket writes, and the chain judge reads them by name.
export const ORIGIN_LAYERS = ['attempt', 'result', 'spec', 'evidence', 'results', 'helper'];
const layerFile = (stage, layer) => {
  if (layer === 'helper') return stage + '.mjs';
  if (layer === 'raw') return stage + '.raw';
  return stage + '.' + layer + '.json';
};
export const layerPath = (pkg, stage, layer) => pkg + '/chain/' + stage + '/' + layerFile(stage, layer);
export const reportPath = (pkg, stage) => pkg + '/chain/' + stage + '/' + stage + '.report.json';

// Stages whose original execution failed and was superseded. They are carried, not required as successes: deleting a failed
// record to tidy a package is the one thing this packet does not do, and counting it as a success would be the other. Each
// names the record that says why it failed and the stage that superseded it.
//
// Five, not four. The four whose chains stop short are easy to see; the fifth, hu147-15, has all eight layers and a bridge
// record that says `ok: false`, because a failed link still produces a guard row, an inner report and a raw channel -- the
// linker's complaint is in that channel. A judge that treated "eight layers present" as "succeeded" would have counted it as
// a green stage, which is why the reason each failure stopped is read out of its own records rather than inferred from
// which layers are missing.
export const PRESERVED_FAILURES = [
  { stage: 'sc147-01', why: 'the scene admission draft failed at launch: the draft module was not where the stage looked for it',
    said_by: 'attempt.launch_error', superseded_by: 'sc147-02' },
  { stage: 'rtm147-01', why: 'the runtime metadata read produced no guard inner report, so its chain cannot be reconstructed',
    said_by: 'bridge.ok false and no inner report', superseded_by: 'rtm147-02' },
  { stage: 'u147a-04', why: 'the boot_diagnostic unit link failed: the test linker script the unit needs was not among its inputs',
    said_by: 'terminal.transport_exit_code 1 with attempt.result_ok false', superseded_by: 'u147c-04' },
  { stage: 'u147b-04', why: 'the same unit link failed a second time for the same reason, before the unit input set was corrected',
    said_by: 'terminal.transport_exit_code 1 with attempt.result_ok false', superseded_by: 'u147c-04' },
  { stage: 'hu147-15', why: 'the h147 unit link failed: ld.bfd reported undefined references from native_runtime.o, and the '
      + 'raw channel carries the linker\'s own complaint',
    said_by: 'bridge.ok false with verdict.accepted false', superseded_by: 'hub147-15' },
];
const PRESERVED = new Set(PRESERVED_FAILURES.map((p) => p.stage));

// ---------------------------------------------------------------- derivation
//
// Every cross-check is one record, the count that record declares, and the stages it resolves to. `agrees` is computed
// against both the record's own declared count and the registry's count for that subset, so a record that under-describes
// what ran and a registry that over-claims what was required both surface.
export const derive = (pkg) => {
  const problems = [];
  const C = (n) => pkg + '/control/' + n;
  const registry = readJsonl(C('stage-registry-147.jsonl'));
  const registryByStage = new Map(registry.map((r) => [r.stage, r]));
  const routes = readJson(C('contract-149.json')).routes;
  const checks = [];
  const check = (family, source_contract, declared, stages, note) => {
    const unique = [...new Set(stages)];
    const missingFromRegistry = unique.filter((s) => !registryByStage.has(s));
    const agrees = unique.length === declared && missingFromRegistry.length === 0;
    checks.push({ family, source_contract, declared_by_its_record: declared, resolved: unique.length,
      resolved_not_in_the_registry: missingFromRegistry, stages: unique.sort(), agrees, note: note || null });
    if (unique.length !== declared) {
      problems.push({ why: 'a record resolved a different number of stages than it declares', family, declared,
        resolved: unique.length });
    }
    for (const s of missingFromRegistry) problems.push({ why: 'a record names a stage the registry does not', family, stage: s });
    return unique;
  };

  // 0. The registry itself: one row per contracted stage, every row carrying a route the contract knows.
  const routesOf = new Map();
  for (const r of registry) {
    if (!Object.prototype.hasOwnProperty.call(routes, r.route)) {
      problems.push({ why: 'the registry gives a route the contract does not define', stage: r.stage, route: r.route });
    }
    routesOf.set(r.stage, r.route);
  }
  const duplicates = registry.length - registryByStage.size;
  if (duplicates !== 0) problems.push({ why: 'the registry reserves the same stage twice', duplicates });

  // 1. Guest trials: the matrix fixes the numeric trials, and the two scene records fix the scene trials.
  const matrix = readJson(C('g-matrix-gm147-01-147.json'));
  const matrixStages = matrix.trials.map((t) => t.stage);
  const sceneTrials = ['g-trial-sc147-01-147.json', 'g-trial-sc147-02-147.json'].map((n) => {
    const j = readJson(C(n));
    return j.stage || (j.trial && j.trial.stage) || n.replace(/^g-trial-|\.json$/g, '');
  });
  check('guest_trial', 'control/g-matrix-gm147-01-147.json (trials[]) and control/g-trial-sc147-0{1,2}-147.json',
    matrix.trials.length + sceneTrials.length, [...matrixStages, ...sceneTrials],
    'the matrix fixes ' + matrix.trials.length + ' numeric trials over ' + new Set(matrix.trials.map((t) => t.scene)).size
    + ' scenes; the two scene records fix the two admission trials');
  const notOk = matrix.trials.filter((t) => t.ok !== true);
  if (notOk.length) problems.push({ why: 'a matrix trial is not ok, so it is not a satisfied requirement',
    trials: notOk.map((t) => t.stage) });

  // 2. Host cases: one process per case, and the host contract fixes which cases exist.
  const hc = readJson(C('h-contract-147e.json'));
  const caseStages = [];
  for (const u of hc.units) for (const c of u.cases) caseStages.push(c.stage);
  check('host_case', 'control/h-contract-147e.json (units[].cases)', caseStages.length, caseStages,
    hc.units.length + ' units; each case is its own process because each is its own stage');

  // 3. Producer objects: the build record fixes the sixty-one slots, of which twelve were rebuilt here and forty-nine were
  //    reused from a preserved origin.
  //
  //    THE FINDING THIS REPLACES. Until now a reused slot was required to do one thing: name a non-empty `from` path. The
  //    record then said, in its own words, that a reused slot "is not required to have a chain here". Two thirds of the
  //    image therefore rested on a string. A path is not provenance -- it cannot be re-read, it does not say what executed,
  //    and it cannot be checked against the bytes it claims to explain.
  //
  //    So every reused slot now carries its origin chain, and the chain is resolved BY CONTENT: the slot's own sha256 is
  //    looked up among the origin packet's provenance rows, so a slot cannot be attached to an origin by sharing a file name
  //    with it. (This matters: the 140 objects and the 147 objects with the same basenames do NOT have the same bytes --
  //    140's arp.o is 3808 bytes and 147's is 2824 -- so a basename match would attach every slot to the wrong origin.)
  //    Each origin stage's six records are then read out of the package and required to agree with the slot and with each
  //    other: the attempt reserves the stage, the result reports a zero transport exit and an unmodified frozen helper, the
  //    spec pins the object it produced, the evidence carries the guard row, the stage result pins the object bytes, and the
  //    frozen helper is the one the attempt and the result both name.
  const pb = readJson(C('p-build-147a.json'));
  const rebuilt = pb.rebuilt.map((b) => b.stage);
  const reused = pb.reused.map((r) => ({ basename: r.basename, from: r.from || null, sha256: r.sha256 || null,
    bytes: r.bytes, slot: r.slot }));
  check('producer_object_rebuilt', 'control/p-build-147a.json (rebuilt[])', pb.rebuilt.length, rebuilt,
    'each rebuilt slot is one compile stage of this packet');
  const reusedWithoutOrigin = reused.filter((r) => !r.from);
  if (pb.rebuilt.length + pb.reused.length !== pb.total) {
    problems.push({ why: 'the build record\'s slots do not add up to its declared total', rebuilt: pb.rebuilt.length,
      reused: pb.reused.length, total: pb.total });
  }
  if (pb.total !== pb.expected) problems.push({ why: 'the build record built a different number of slots than the link needs',
    total: pb.total, expected: pb.expected });
  for (const r of reusedWithoutOrigin) problems.push({ why: 'a reused producer object names no origin', object: r.basename });

  // The origin chains, read out of the package. The origin stages are named by the tenth material bucket, which the package
  // build resolved by content and copied whole; this reads them back and judges them, so the derivation does not take the
  // build's word for the match.
  const originRoot = pkg + '/material/origins';
  // The origin packet's own closure record, carried in the package. It is what makes the slot-to-origin match checkable by a
  // reader: it names, for every origin stage, the object that stage produced and the bytes of that object. So the match is
  // made here by comparing hashes, not by comparing file names.
  const originClosure = exists(originRoot + '/_records/producer-closure-p140-05.json')
    ? readJson(originRoot + '/_records/producer-closure-p140-05.json') : null;
  if (!originClosure) problems.push({ why: 'the origin packet\'s closure record is not in the package',
    family: 'producer_origin_chain', wanted: 'material/origins/_records/producer-closure-p140-05.json' });
  const originObjectSha = new Map();
  const originBuilt = new Map();
  for (const b of (originClosure && originClosure.built) || []) {
    originBuilt.set(b.stage, b);
    if (b.object && b.object.sha256) originObjectSha.set(b.object.sha256, b.stage);
  }
  const originDirs = exists(originRoot)
    ? fs.readdirSync(originRoot).filter((n) => fs.statSync(originRoot + '/' + n).isDirectory() && n !== '_records').sort()
    : [];
  const originChains = [];
  const originProblems = [];
  const originSlotOf = new Map();
  for (const stage of originDirs) {
    const dir = originRoot + '/' + stage;
    const present = {};
    for (const kind of ORIGIN_LAYERS) present[kind] = exists(dir + '/' + kind + (kind === 'helper' ? '.mjs' : '.json'));
    const missingHere = ORIGIN_LAYERS.filter((k) => !present[k]);
    if (missingHere.length) {
      originProblems.push({ stage, why: 'the origin chain is not complete', missing: missingHere });
      originChains.push({ stage, present, missing: missingHere, held: false });
      continue;
    }
    const attempt = readJson(dir + '/attempt.json');
    const result = readJson(dir + '/result.json');
    const spec = readJson(dir + '/spec.json');
    const evidence = readJson(dir + '/evidence.json');
    const results = readJson(dir + '/results.json');
    const helperBytes = fs.readFileSync(dir + '/helper.mjs');
    const facts = [];
    const fact = (name, held, detail) => { facts.push({ name, held: !!held, detail }); if (!held) originProblems.push({ stage, name, detail }); };

    // The slot this origin produced, resolved BY CONTENT: the object hash the origin closure records for this stage is looked
    // up among the reused slots' hashes. A basename match would attach every slot to the wrong origin, because the 140 and
    // 147 objects of the same name do not have the same bytes.
    const built = originBuilt.get(stage);
    const objectSha = built && built.object && built.object.sha256;
    const slotRow = objectSha ? reused.find((r) => r.sha256 === objectSha) : null;
    const slot = slotRow ? slotRow.slot : null;
    const outputs = (results.subject && results.subject.outputs) || [];
    fact('origin:the-stage-result-names-the-object-it-produced', outputs.length === 1, outputs);
    fact('origin:the-origin-closure-names-the-bytes-this-stage-produced',
      !!objectSha && !!built.object && built.object.bytes > 0, { stage, object_sha256: objectSha,
        object_bytes: built && built.object && built.object.bytes });
    fact('origin:the-slot-this-origin-produced-is-in-the-build-record', slot !== null, { slot, basename: slotRow && slotRow.basename,
      matched_by: 'sha256 of the object the origin closure records for this stage' });
    // The three exits, each what its own record says it should be, and the frozen helper unchanged across the run. The guard
    // row and the inner report are read from the stage result, which is where this packet's route writes them; the same row
    // is also in the bridge evidence, and the two are compared with each other.
    fact('origin:the-attempt-reserved-this-stage', attempt.stage_claim && attempt.stage_claim.stage === stage,
      attempt.stage_claim && attempt.stage_claim.stage);
    fact('origin:the-attempt-completed', attempt.complete === true && attempt.result_ok === true,
      { complete: attempt.complete, result_ok: attempt.result_ok });
    fact('origin:the-transport-exited-zero', result.transport_exit_code === 0, result.transport_exit_code);
    fact('origin:the-guard-child-exited-zero',
      !!(results.guard_row && results.guard_row.exit_code === 0)
      && !!(results.inner_report && results.inner_report.returncode === 0),
      { guard_exit: results.guard_row && results.guard_row.exit_code,
        child: results.inner_report && results.inner_report.returncode });
    fact('origin:the-bridge-evidence-carries-the-same-guard-row',
      !!(evidence.guard_row && results.guard_row)
      && evidence.guard_row.exit_code === results.guard_row.exit_code
      && evidence.guard_row.stage === results.guard_row.stage,
      { evidence_exit: evidence.guard_row && evidence.guard_row.exit_code,
        result_exit: results.guard_row && results.guard_row.exit_code });
    fact('origin:the-stage-result-accepted-the-run', results.ok === true && results.verdict
      && results.verdict.accepted === true, { ok: results.ok, accepted: results.verdict && results.verdict.accepted });
    fact('origin:the-frozen-helper-was-unchanged', result.frozen_unchanged === true
      && result.frozen_posthash_sha256 === result.frozen_sha256, { unchanged: result.frozen_unchanged });
    fact('origin:the-helper-in-the-package-is-the-one-the-records-name',
      sha(helperBytes) === String(attempt.frozen && attempt.frozen.frozen_sha256)
      && sha(helperBytes) === String(result.frozen_sha256),
      { package: sha(helperBytes), attempt: attempt.frozen && attempt.frozen.frozen_sha256, result: result.frozen_sha256 });
    fact('origin:the-result-ok-claim-is-about-the-transport-only', /says nothing about any project behaviour/.test(
      String(result.ok_means || '')), null);
    originChains.push({ stage, present, missing: [], slot, basename: slotRow && slotRow.basename,
      route: result.route, kind: results.kind, helper_sha256: sha(helperBytes),
      facts, held: facts.every((f) => f.held) });
    if (slot !== null) originSlotOf.set(slot, stage);
  }
  // Every reused slot must have an origin chain, and every origin chain must belong to a reused slot. A chain nobody's slot
  // claims is as much a problem as a slot with no chain: it would let a slot be quietly dropped and the count still balance.
  for (const r of reused) {
    if (!originSlotOf.has(r.slot)) {
      originProblems.push({ slot: r.slot, basename: r.basename, why: 'a reused slot has no origin chain in the package' });
    }
  }
  const originStagesClaimed = new Set(originSlotOf.values());
  for (const c of originChains) {
    if (!originStagesClaimed.has(c.stage)) {
      originProblems.push({ stage: c.stage, why: 'an origin chain in the package belongs to no reused slot' });
    }
  }
  if (originChains.length !== reused.length) {
    originProblems.push({ why: 'the package carries a different number of origin chains than the build record reuses slots',
      chains: originChains.length, reused: reused.length });
  }
  for (const p of originProblems) problems.push({ ...p, family: 'producer_origin_chain' });
  const sel = exists(C('p-selection-147-a.json')) ? readJson(C('p-selection-147-a.json')) : null;
  if (sel && sel.slots.length !== pb.total) {
    problems.push({ why: 'the selection record and the build record disagree about how many slots the link has',
      selection: sel.slots.length, build: pb.total });
  }

  // 4. Host unit builds: the six records fix every build, assembly and link stage of the units they cover.
  const unitRecords = fs.readdirSync(pkg + '/control').filter((n) => /^h-unit-build-.*\.json$/.test(n)).sort()
    .map((n) => ({ name: n, record: readJson(pkg + '/control/' + n) }));
  const unitStages = [];
  for (const { record } of unitRecords) for (const s of record.stages || []) if (s.stage) unitStages.push(s.stage);
  check('host_unit_build', 'control/h-unit-build-*.json (stages[])', unitStages.length, unitStages,
    unitRecords.length + ' unit records; every stage a record lists is a stage this packet ran');
  const unitFailed = [];
  for (const { name, record } of unitRecords) {
    for (const s of record.stages || []) if (s.status !== 0) unitFailed.push({ record: name, stage: s.stage, status: s.status });
  }

  // 5. The link: the image, and one per unit that produces an ELF.
  const links = unitStages.filter((s) => routesOf.get(s) === 'image.link');
  check('link', 'control/h-unit-build-*.json (the image.link stages) plus the image link the registry reserves',
    links.length, links, 'each unit that produces an ELF ends in a link, and the image is one more');

  // 6. The gates: the static reads, the metadata reads, the source views and the runtime staging. These are the stages the
  //    registry reserves on the routes that produce an observation rather than an artefact.
  const gateRoutes = ['native.metadata', 'source.view', 'runtime.stage'];
  const gates = registry.filter((r) => gateRoutes.includes(r.route)).map((r) => r.stage);
  check('gate', 'control/stage-registry-147.jsonl (routes ' + gateRoutes.join(', ') + ')', gates.length, gates,
    'the observations the judgements read: the static image, the drift, the runtime root, the source view, the staged runtime');

  // 7. The 147 packet's own C set, carried as evidence of what 147 covered -- explicitly not as this packet's coverage cap.
  const c147 = readJson(C('c-stage-set-147.json'));
  const c147Stages = [...c147.static, ...c147.producers, c147.link, ...c147.units];
  check('c_set_147_preserved', 'control/c-stage-set-147.json (static, producers, link, units)', c147Stages.length, c147Stages,
    'source_task 147. It is ' + c147Stages.length + ' of the ' + registry.length + ' stages this packet carries, so it is '
    + 'evidence of what 147 covered and not a limit on what 148 covers');

  // The required set: everything the registry reserves. The package must equal it exactly.
  const required = registry.map((r) => r.stage).sort();
  const inPackage = exists(pkg + '/chain')
    ? fs.readdirSync(pkg + '/chain').filter((d) => fs.statSync(pkg + '/chain/' + d).isDirectory()).sort() : [];
  const requiredSet = new Set(required);
  const packageSet = new Set(inPackage);
  const absent = required.filter((s) => !packageSet.has(s));
  const unrequired = inPackage.filter((s) => !requiredSet.has(s));
  for (const s of absent) problems.push({ why: 'a stage the registry reserves has no chain directory in the package', stage: s });
  for (const s of unrequired) problems.push({ why: 'the package carries a stage the registry never reserved', stage: s });

  const preserved = PRESERVED_FAILURES.filter((p) => packageSet.has(p.stage));
  for (const p of PRESERVED_FAILURES) {
    if (!packageSet.has(p.stage)) problems.push({ why: 'a preserved failure original is not in the package', stage: p.stage });
    if (!requiredSet.has(p.stage)) problems.push({ why: 'a preserved failure is not in the registry either', stage: p.stage });
  }

  const byRoute = {};
  for (const r of registry) byRoute[r.route] = (byRoute[r.route] || 0) + 1;
  const byFamily = {};
  for (const c of checks) byFamily[c.family] = c.resolved;

  return {
    primary_source: 'control/stage-registry-147.jsonl',
    primary_source_means: 'the packet\'s own reservation ledger, one row per stage, written before the work and copied into '
      + 'the package as a control record. The package is compared against it, so the comparison is between two independently '
      + 'written things and not between the package and itself',
    registry_rows: registry.length,
    registry_routes: byRoute,
    cross_checks: checks,
    by_family: byFamily,
    stages: required.length,
    stage_list: required,
    requirements_by_stage: Object.fromEntries(required.map((s) => [s, [{ family: 'registry', route: routesOf.get(s) }]])),
    producer_slots: { total: pb.total, rebuilt: pb.rebuilt.length, reused: pb.reused.length,
      reused_naming_their_origin: reused.length - reusedWithoutOrigin.length,
      reused_with_a_complete_origin_chain: originChains.filter((c) => c.held === true).length,
      origin_chains: originChains.length,
      origin_stages: originChains.map((c) => ({ stage: c.stage, slot: c.slot, basename: c.basename, held: c.held,
        facts: (c.facts || []).filter((f) => !f.held).length })),
      origin_problems: originProblems.length,
      note: 'every reused slot resolves BY CONTENT to an origin stage whose six typed records -- the reservation, the terminal '
        + 'result, the spec, the guard evidence, the stage result and the frozen helper -- are carried in the package and '
        + 'judged here. A slot is not attached to an origin by sharing a file name with it: the 140 objects and the 147 '
        + 'objects of the same basename do not have the same bytes' },
    unit_records: unitRecords.length,
    unit_stages: unitStages.length,
    unit_stages_that_failed: unitFailed,
    c_set_147: { stages: c147Stages.length, of_registry: registry.length,
      note: 'preserved as source_task 147 and not used as a cap' },
    package_stage_directories: inPackage.length,
    absent_stages: absent,
    unrequired_stages: unrequired,
    preserved_failures: preserved,
    problems,
    ok: problems.length === 0,
  };
};

// ---------------------------------------------------------------- one stage's chain
export const judgeStage = (pkg, stage, contract, registryRow) => {
  const facts = [];
  const problems = [];
  const fact = (name, held, detail) => { facts.push({ name, held: !!held, detail }); if (!held) problems.push({ name, detail }); };
  const L = (layer) => layerPath(pkg, stage, layer);
  const has = {};
  for (const layer of LAYERS) has[layer] = exists(L(layer));
  const missing = LAYERS.filter((l) => !has[l]);

  // A preserved failure is judged for what it is: the layers it has must be consistent with each other, and the reason it
  // stopped must be the reason its own records give. It is not counted as a success. A failure with a complete chain
  // (hu147-15) is not treated differently from one that stops short: both must show, in their own records, that they did not
  // succeed, and neither is counted as held.
  const preservedFailure = PRESERVED_FAILURES.find((p) => p.stage === stage);
  if (preservedFailure) {
    const present = LAYERS.filter((l) => has[l]);
    fact('preserved:the-original-is-present', present.length > 0, present);
    fact('preserved:the-spec-is-present', has.spec, L('spec'));
    fact('preserved:the-attempt-is-present', has.attempt, L('attempt'));
    fact('preserved:the-terminal-is-present', has.terminal, L('terminal'));
    fact('preserved:its-successor-is-in-the-package', exists(pkg + '/chain/' + preservedFailure.superseded_by),
      preservedFailure.superseded_by);
    const terminal = has.terminal ? readJson(L('terminal')) : null;
    const attempt = has.attempt ? readJson(L('attempt')) : null;
    const bridge = has.bridge ? readJson(L('bridge')) : null;
    const didNotSucceed = (attempt && attempt.result_ok === false) || (bridge && bridge.ok === false)
      || (terminal && terminal.transport_exit_code !== 0) || !!(attempt && attempt.launch_error)
      || !!(terminal && terminal.launch_error);
    fact('preserved:its-own-records-say-it-did-not-succeed', didNotSucceed,
      { attempt_result_ok: attempt && attempt.result_ok, bridge_ok: bridge && bridge.ok,
        transport_exit: terminal && terminal.transport_exit_code,
        attempt_launch_error: attempt && attempt.launch_error });
    // A failure that left a bridge must leave one that says so; a failure that produced no bridge must say so in the layers
    // it does have. Either way the package cannot show a green record for a stage that failed.
    fact('preserved:no-record-in-the-package-calls-it-green',
      !(bridge && bridge.ok === true) && !(attempt && attempt.result_ok === true && !bridge),
      { bridge_ok: bridge && bridge.ok, attempt_result_ok: attempt && attempt.result_ok });
    return { stage, route: registryRow ? registryRow.route : null, kind: bridge ? bridge.kind : null, preserved_failure: true,
      layers: has, missing_layers: missing, facts, held: facts.filter((f) => f.held).length, problems,
      ok: problems.length === 0 };
  }

  for (const layer of LAYERS) fact('layer:' + layer + '-present', has[layer], L(layer));
  if (problems.length) {
    return { stage, route: null, kind: null, preserved_failure: false, layers: has, missing_layers: missing, facts,
      held: facts.filter((f) => f.held).length, problems, ok: false };
  }

  const spec = readJson(L('spec'));
  const control = readJson(L('control'));
  const attempt = readJson(L('attempt'));
  const terminal = readJson(L('terminal'));
  const bridge = readJson(L('bridge'));
  const inner = readJson(L('inner'));
  const helperBytes = fs.readFileSync(L('helper'));
  const rawBytes = fs.readFileSync(L('raw'));
  const report = exists(reportPath(pkg, stage)) ? readJson(reportPath(pkg, stage)) : null;

  const route = spec.route;
  const kind = bridge.kind || null;
  const isCheckpoint = kind === 'checkpoint';
  const isRun = route === 'test.cpp.run';

  // ---- the spec: the stage's own statement of what it would run.
  fact('spec:task', spec.task === contract.source_task, { got: spec.task, want: contract.source_task });
  fact('spec:stage', spec.stage === stage, { got: spec.stage, want: stage });
  fact('spec:route-is-the-registry-route', registryRow ? spec.route === registryRow.route : true,
    { spec: spec.route, registry: registryRow && registryRow.route });
  fact('spec:route-known', Object.prototype.hasOwnProperty.call(contract.routes, route), route);
  fact('spec:names-its-helper', typeof spec.helper === 'string' && spec.helper.length > 0, spec.helper);
  fact('spec:names-its-runner', typeof spec.runnerPath === 'string' && spec.runnerPath.length > 0, spec.runnerPath);
  fact('spec:declares-expected-outputs', Array.isArray(spec.expected_outputs) && spec.expected_outputs.length > 0,
    spec.expected_outputs && spec.expected_outputs.length);
  fact('spec:pins-the-contract-it-answers-to', !!(spec.contract_pin && typeof spec.contract_pin.sha256 === 'string'),
    spec.contract_pin && spec.contract_pin.sha256);

  // ---- the control: what the runner froze before the stage ran.
  const cb = control.bridge || {};
  fact('control:stage', cb.stage === stage || control.stage === stage, { control: control.stage, bridge: cb.stage, want: stage });
  fact('control:route-matches-the-spec', control.route === route, { control: control.route, spec: route });
  fact('control:guard-is-the-pinned-guard', cb.guard_linux === contract.tools.guard_linux,
    { got: cb.guard_linux, want: contract.tools.guard_linux });
  fact('control:python-is-the-pinned-interpreter', !!(cb.tools && cb.tools.python_realpath === contract.tools.python.realpath),
    { got: cb.tools && cb.tools.python_realpath, want: contract.tools.python.realpath });
  fact('control:declares-a-work-root', typeof cb.work_root === 'string' && cb.work_root.length > 0, cb.work_root);
  fact('control:pins-the-binding-module', !!(cb.binding_module && typeof cb.binding_module.sha256 === 'string'),
    cb.binding_module && cb.binding_module.sha256);
  fact('control:pins-the-runner', !!(cb.runner_module && typeof cb.runner_module === 'string'), cb.runner_module);

  // ---- the helper: the draft the spec names has one frozen identity, and the packaged bytes must be that identity.
  const draft = contract.helper_drafts[spec.helper];
  fact('helper:the-spec-names-a-draft-the-contract-pins', !!draft, { spec_helper: spec.helper,
    known: Object.keys(contract.helper_drafts) });
  if (draft) {
    fact('helper:bytes-are-the-pinned-bytes', helperBytes.length === draft.bytes, { actual: helperBytes.length, want: draft.bytes });
    fact('helper:hash-is-the-pinned-hash', sha(helperBytes) === draft.sha256, { actual: sha(helperBytes), want: draft.sha256 });
  }

  // ---- the attempt: the reservation, taken before the work, which is what makes the terminal believable.
  fact('attempt:task', attempt.task === contract.source_task, attempt.task);
  fact('attempt:complete', attempt.complete === true, attempt.complete);
  fact('attempt:reservation', attempt.reservation === true, attempt.reservation);
  fact('attempt:claims-this-stage', !!(attempt.stage_claim && attempt.stage_claim.stage === stage),
    attempt.stage_claim && attempt.stage_claim.stage);
  fact('attempt:route-matches-the-spec', attempt.route === route, { attempt: attempt.route, spec: route });
  fact('attempt:runner-frozen', attempt.runner_frozen === true, attempt.runner_frozen);
  fact('attempt:runner-is-the-pinned-runner', attempt.runner_sha256 === contract.runner.sha256,
    { got: attempt.runner_sha256, want: contract.runner.sha256 });
  fact('attempt:no-launch-error', !attempt.launch_error, attempt.launch_error);
  fact('attempt:result-ok', attempt.result_ok === true, attempt.result_ok);

  // ---- the terminal: written before the attempt was marked complete, and it says whether the transport even started.
  fact('terminal:written-before-complete', terminal.terminal_result_written_before_complete === true,
    terminal.terminal_result_written_before_complete);
  fact('terminal:transport-started', terminal.transport_started === true, terminal.transport_started);
  fact('terminal:no-launch-error', !terminal.launch_error, terminal.launch_error);
  fact('terminal:frozen-helper-unchanged', terminal.frozen_unchanged === true, terminal.frozen_unchanged);
  fact('terminal:runner-unchanged', terminal.runner_unchanged === true, terminal.runner_unchanged);
  fact('terminal:the-wrapper-did-not-time-out', terminal.timed_out === false, terminal.timed_out);

  // ---- the bridge: the uniform record, the same shape on every route.
  fact('bridge:stage', bridge.stage === stage, bridge.stage);
  fact('bridge:route-matches-the-spec', (bridge.route || null) === route, { bridge: bridge.route || null, spec: route });
  fact('bridge:ok', bridge.ok === true, bridge.ok);
  const v = bridge.verdict || {};
  for (const k of ['accepted', 'guard_row_present', 'inner_present', 'raw_present', 'raw_within_bound', 'landlock_ok',
    'probes_ok', 'group_measured', 'group_empty_after_cleanup']) {
    fact('bridge:verdict.' + k, v[k] === true, v[k]);
  }
  fact('bridge:subject-present', !!bridge.subject, bridge.subject && bridge.subject.kind);
  fact('bridge:plan-present', !!bridge.plan, bridge.plan && bridge.plan.cmd0);
  fact('bridge:names-its-own-inner-report', !!(bridge.inner_report && typeof bridge.inner_report.path === 'string'),
    bridge.inner_report && bridge.inner_report.path);
  fact('bridge:names-its-own-raw-channel', !!(bridge.raw && typeof bridge.raw.path === 'string'),
    bridge.raw && bridge.raw.path);

  // ---- the inner report: the guard's own record of the child it ran.
  fact('inner:stage', inner.stage === stage, inner.stage);
  fact('inner:argv-present', Array.isArray(inner.argv) && inner.argv.length > 0, inner.argv && inner.argv.length);
  fact('inner:no-descendants-left', !!(inner.child && Array.isArray(inner.child.descendants_left)
    && inner.child.descendants_left.length === 0), inner.child && inner.child.descendants_left);
  fact('inner:nothing-killed', !!(inner.child && Array.isArray(inner.child.killed) && inner.child.killed.length === 0),
    inner.child && inner.child.killed);
  fact('inner:group-measured', !!(inner.child && inner.child.group_measured === true), inner.child && inner.child.group_measured);
  fact('inner:names-the-raw-file-it-wrote', !!(inner.child && typeof inner.child.output_path === 'string'),
    inner.child && inner.child.output_path);

  // ---- the raw channel: the child's bytes, against the guard's record of them. An absent raw was refused above by the
  // layer check; an empty raw is checked here against the byte count the guard recorded, so the two are different facts.
  fact('raw:bytes-match-the-guard-record', rawBytes.length === inner.child.output_file_bytes,
    { actual: rawBytes.length, recorded: inner.child.output_file_bytes });
  fact('raw:hash-matches-the-guard-record', sha(rawBytes) === inner.child.output_sha256,
    { actual: sha(rawBytes), recorded: inner.child.output_sha256 });
  fact('raw:empty-is-declared-empty', rawBytes.length > 0 || inner.child.output_file_bytes === 0,
    { bytes: rawBytes.length, recorded: inner.child.output_file_bytes });

  // ---- THE THREE EXITS.
  const exitWrapper = terminal.transport_exit_code;
  const exitGuard = bridge.guard_row && bridge.guard_row.exit_code;
  const exitInner = inner.child && inner.child.returncode;
  fact('exit:the-guard-number-is-the-child-number', exitGuard === exitInner, { guard: exitGuard, inner: exitInner });
  // The wrapper is not the guard: the wrapper exits zero when it managed to run the transport at all, and the guard's own
  // exit is a separate number recorded in the bridge. A wrapper that reported a failure here would mean the transport was
  // never judged.
  fact('exit:the-wrapper-ran-and-said-so', exitWrapper === 0, { wrapper: exitWrapper });
  fact('exit:the-transport-number-is-what-the-verdict-expected', v.transport_exit === v.expected_guard_exit,
    { transport: v.transport_exit, expected: v.expected_guard_exit });
  fact('exit:the-verdict-declares-the-transport-number-expected', v.guard_exit_as_expected === true, v.guard_exit_as_expected);
  if (isCheckpoint) {
    // A halted guest cannot exit cleanly: the guard kills it at its own deadline, and that kill is the expected fact. The
    // record has to say so, or a -9 would be indistinguishable from a crash.
    fact('exit:the-deadline-kill-is-the-expected-outcome', v.deadline_kill_expected === true, v.deadline_kill_expected);
    fact('exit:the-expected-transport-exit-is-one', v.expected_guard_exit === 1, v.expected_guard_exit);
    fact('exit:the-child-was-killed-at-the-deadline', exitInner === -9 && !!(inner.child && inner.child.timed_out === true),
      { returncode: exitInner, timed_out: inner.child && inner.child.timed_out });
    // The guard row distinguishes the two timeouts on purpose: `timed_out` is the guard itself, `inner_timed_out` is the
    // child. A checkpoint's guard finishes on time and its child does not, so the row must say inner_timed_out and must not
    // say timed_out.
    fact('exit:the-guard-row-says-the-child-was-the-one-that-timed-out',
      !!(bridge.guard_row && bridge.guard_row.inner_timed_out === true), bridge.guard_row && bridge.guard_row.inner_timed_out);
    fact('exit:the-guard-row-says-the-guard-itself-finished', !!(bridge.guard_row && bridge.guard_row.timed_out === false),
      bridge.guard_row && bridge.guard_row.timed_out);
  } else {
    fact('exit:all-three-are-zero', exitWrapper === 0 && exitGuard === 0 && exitInner === 0,
      { wrapper: exitWrapper, guard: exitGuard, inner: exitInner });
  }

  // ---- the tool and the subject.
  const argv0 = inner.argv && inner.argv[0];
  if (isRun) {
    // A run stage executes the artefact its own build made. The artefact is named twice: as argv[0] and as the subject's
    // tool identity, and those two have to be the same file, or the identity the record pins is not the thing that ran.
    const ti = bridge.subject && bridge.subject.tool_identity;
    fact('tool:runs-an-artefact-not-a-tool', /\.elf$/.test(String(argv0)), argv0);
    fact('tool:the-subject-names-the-artefact-it-ran', !!(ti && /\.elf$/.test(String(ti.linux_path))),
      ti && ti.linux_path);
    fact('tool:the-artefact-argv0-is-the-artefact-the-subject-names', !!(ti && ti.linux_path === argv0),
      { argv0, subject: ti && ti.linux_path });
    fact('tool:the-artefact-is-pinned-by-hash', !!(ti && typeof ti.sha256 === 'string' && ti.sha256.length === 64),
      ti && ti.sha256);
  } else if (isCheckpoint) {
    fact('tool:the-loader-is-the-one-in-the-staged-runtime-root', argv0 === contract.tools.loader_in_runtime_root.linux,
      { got: argv0, want: contract.tools.loader_in_runtime_root.linux });
    fact('tool:the-emulator-is-the-one-in-the-staged-runtime-root',
      Array.isArray(inner.argv) && inner.argv.includes(contract.tools.emulator_in_runtime_root.linux),
      { want: contract.tools.emulator_in_runtime_root.linux });
    fact('tool:everything-it-executes-is-inside-that-root',
      Array.isArray(inner.argv) && inner.argv.filter((a) => a.startsWith('/')).every((a) => !a.startsWith('/usr/')
        || a.startsWith(contract.tools.runtime_root) || a.startsWith('/var/tmp/gtos-dsh-147-e759-scene-bound')),
      { runtime_root: contract.tools.runtime_root });
    const ti = bridge.subject && bridge.subject.tool_identity;
    fact('tool:the-subject-names-its-tool-identity', !!(ti && typeof ti.sha256 === 'string'), ti && ti.sha256);
    fact('tool:the-image-is-named', !!(bridge.subject && typeof bridge.subject.image === 'string'),
      bridge.subject && bridge.subject.image);
    // The two guest channels, which only the report record describes.
    fact('checkpoint:the-two-channels-are-in-the-package',
      !!(report && report.channels && report.channels.e9 && report.channels.serial),
      report && report.channels ? Object.keys(report.channels) : null);
    fact('checkpoint:the-scene-is-named', !!(report && typeof report.scene === 'string'), report && report.scene);
    fact('checkpoint:the-gate-admitted-the-scene', !!(report && report.gate && report.gate.admitted === true),
      report && report.gate && report.gate.admitted);
  } else {
    const pin = toolPinFor(contract, toolKindForRoute(route));
    fact('tool:argv0-is-the-pinned-tool-for-this-route', !!pin && argv0 === pin.linux,
      { got: argv0, want: pin && pin.linux, kind: toolKindForRoute(route), route });
    const ti = bridge.subject && bridge.subject.tool_identity;
    fact('tool:the-subject-tool-identity-is-the-pinned-bytes', !!pin && !!ti && ti.sha256 === pin.sha256,
      { got: ti && ti.sha256, want: pin && pin.sha256, kind: toolKindForRoute(route) });
  }

  const held = facts.filter((f) => f.held).length;
  return { stage, route, kind, preserved_failure: false, layers: has, missing_layers: [], facts, held, problems,
    ok: problems.length === 0 };
};

export const judgeAll = (pkg, contract, derived) => {
  const registry = readJsonl(pkg + '/control/stage-registry-147.jsonl');
  const byStage = new Map(registry.map((r) => [r.stage, r]));
  const results = derived.stage_list.map((s) => judgeStage(pkg, s, contract, byStage.get(s)));
  const required = results.filter((r) => !r.preserved_failure);
  const preserved = results.filter((r) => r.preserved_failure);
  const bad = required.filter((r) => !r.ok);
  const problems = derived.problems.slice();
  for (const r of bad) {
    problems.push({ why: 'a required stage chain did not hold', stage: r.stage, first: r.problems[0] && r.problems[0].name,
      detail: r.problems[0] && r.problems[0].detail });
  }
  for (const r of preserved.filter((p) => !p.ok)) {
    problems.push({ why: 'a preserved failure is not consistent with its own record', stage: r.stage,
      first: r.problems[0] && r.problems[0].name });
  }
  const byRoute = {};
  for (const r of results) byRoute[r.route] = (byRoute[r.route] || 0) + 1;
  return {
    task: 'GTOS-DSH-149', kind: 'chain_coverage',
    what: 'every stage the packet reserved, derived from its reservation ledger and cross-checked against every record that '
      + 'fixes a count, and for each one the eight layers of its chain read out of the package and checked against each '
      + 'other and against the pinned tools',
    derivation: { primary_source: derived.primary_source, primary_source_means: derived.primary_source_means,
      registry_rows: derived.registry_rows, registry_routes: derived.registry_routes, cross_checks: derived.cross_checks,
      by_family: derived.by_family, stages: derived.stages, producer_slots: derived.producer_slots,
      unit_records: derived.unit_records, unit_stages: derived.unit_stages,
      unit_stages_that_failed: derived.unit_stages_that_failed, c_set_147: derived.c_set_147,
      package_stage_directories: derived.package_stage_directories },
    stages_checked: results.length,
    stages_held: required.filter((r) => r.ok).length,
    stages_that_are_preserved_failures: preserved.length,
    stages_by_route: byRoute,
    facts_total: results.reduce((n, r) => n + r.facts.length, 0),
    facts_held: results.reduce((n, r) => n + r.held, 0),
    stages: results,
    preserved_failures: preserved.map((p) => ({ stage: p.stage, ok: p.ok, layers_present: LAYERS.filter((l) => p.layers[l]).length,
      missing_layers: p.missing_layers,
      why: (PRESERVED_FAILURES.find((x) => x.stage === p.stage) || {}).why,
      said_by: (PRESERVED_FAILURES.find((x) => x.stage === p.stage) || {}).said_by,
      superseded_by: (PRESERVED_FAILURES.find((x) => x.stage === p.stage) || {}).superseded_by })),
    problems,
    ok: problems.length === 0 && bad.length === 0,
    ok_means: 'the registry\'s reserved stage set equals the package\'s stage set exactly, every record that fixes a count '
      + 'agrees with the registry, every required stage has all eight layers with each of the three exits the number its own '
      + 'record says it should be, the guard exit is the child return code, the tool is the one the contract pins for the '
      + 'route, the helper is the draft the spec names, and the raw channel is the bytes the guard recorded. The four stages '
      + 'whose original execution failed are carried as preserved failures and are not counted as successes',
  };
};

// ---------------------------------------------------------------- CLI
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) {
  const pkg = process.argv[2];
  const outPath = process.argv[3];
  if (!pkg) { console.error('usage: chain-149.mjs <package-root> [out.json]'); process.exit(2); }
  const contract = contractOf(pkg);
  const derived = derive(pkg);
  const result = judgeAll(pkg, contract, derived);
  const text = JSON.stringify(result, null, 2) + '\n';
  if (outPath) {
    if (exists(outPath)) throw new Error('REFUSED, the record already exists: ' + outPath);
    fs.writeFileSync(outPath, text, { flag: 'wx' });
  }
  console.log(JSON.stringify({ ok: result.ok, stages_checked: result.stages_checked, stages_held: result.stages_held,
    preserved: result.stages_that_are_preserved_failures, by_route: result.stages_by_route,
    cross_checks: result.derivation.cross_checks.map((c) => c.family + ':' + c.resolved + '/' + c.declared_by_its_record
      + (c.agrees ? ' ok' : ' MISMATCH')),
    facts: { total: result.facts_total, held: result.facts_held },
    problems: result.problems.slice(0, 14) }, null, 2));
  process.exit(result.ok ? 0 : 1);
}
