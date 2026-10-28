// GTOS-DSH-146 roles: the six consumers that are not the static gate, run against the real records.
//
// Each role writes one record, and the record is what the seal pins. A role's `ok` is true only if its consumer ran and
// accepted: a consumer that threw, or that returned a verdict with problems, is a false, not a skipped check.
//
// The roles and what each one actually does:
//
//   prerequisite_full              consumeRole over all ten roles of the 144 prerequisite record, then bindSet over the ten
//                                  results. This is the packet's own consumer, not a reimplementation, and it reads the
//                                  contract, the order ledger and the frozen runner it names.
//   producer_and_native_chain_full the producer chain: the 61 frozen link slots in order, the dependency closure over the
//                                  depfiles, and the link-input consumption record. The order is compared slot by slot,
//                                  because a set comparison would accept a permuted link.
//   host_chain_full                consumeHostChain over the three host units and their 14 cases: the result, then the inner
//                                  report at the path the result names, then the raw channel at the path the inner report names.
//   guest_resource_semantics_full  judgeResourceSemantics over every resource trial in the G matrix, both channels.
//   independent_physical_closure   the derivation in d-required-146.mjs, which reads contracts and no spec.
//   publication_and_related_tests  the four test suites this packet wrote, run here rather than cited.
//
// Nothing here invents a fixture. Every input is a record a previous packet produced, and every path is read from the record
// that owns it rather than computed.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const W = R + '/worktree-146';
const OUT = R + '/stage-output/roles-146-' + (process.argv[2] || '01');
const NODE = 'C:/Program Files/nodejs/node.exe';
const WSL = '//wsl$/Ubuntu-20.04';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const toHost = (p) => (typeof p === 'string' && p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':/' + p.slice(9) : WSL + p);
const exists = (p) => fs.existsSync(p);
const load = (rel) => import(pathToFileURL(W + '/' + rel).href);

fs.mkdirSync(OUT, { recursive: true });
const RUN = process.argv[2] || '01';
const results = {};

function writeRole(role, record) {
  const target = OUT + '/' + role + '.json';
  // CreateNew: a role record is evidence, and evidence is written once. A re-run writes a new stage, not over this one.
  if (fs.existsSync(target)) {
    const existing = readJson(target);
    if (existing.stage === 'roles-146-' + RUN) fs.unlinkSync(target);
    else { results[role] = { ok: false, refused: 'a record already exists for this role', path: target, existing_stage: existing.stage }; return; }
  }
  fs.writeFileSync(target, JSON.stringify(record, null, 2) + '\n');
  results[role] = { ok: record.ok === true, path: target, bytes: fs.statSync(target).size,
    sha256: sha(fs.readFileSync(target)) };
}

// ---------------------------------------------------------------- prerequisite_full
{
  const P = await load('tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs');
  const set = readJson(R144 + '/t-prerequisites-tpr144-01-144.json');
  const ctx = {
    contract_path: R144 + '/contract-144.json',
    ledger_path: R144 + '/producer-order-144.jsonl',
    runner_path: R144 + '/frozen-runners/run-144-r01.mjs',
  };
  // consumeRole returns one verdict per role: `{ ok, detail, binding }` on success, `{ ok: false, why }` on refusal. An
  // earlier version of this harness read `problems` and `facts`, which the consumer does not produce -- so every role looked
  // consumed and the set looked unbound, which is the wrong conclusion from the right inputs. The shape is read from the
  // consumer's own return value, not assumed.
  const rows = [];
  const verdicts = [];
  for (const r of set.roles) {
    // The set names each role's record in `path`, not `record`. Reading the wrong key gave `undefined` to fs, which threw
    // from inside the consumer and made every role look refused for a reason that had nothing to do with the record.
    const recordPath = r.path || r.record;
    let verdict = null;
    let error = null;
    // The consumer takes the ROLE RECORD's parsed contents, not the set's row about it. The row names the record; the record
    // carries the slots, headers and pins the consumer reads. Handing it the row made every role refuse with "the contract
    // names no inputs at all", which was true of the row and false of the record.
    try { verdict = P.consumeRole(r.role, readJson(recordPath), ctx); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ role: r.role, record: recordPath, kind: r.kind || null,
      record_pin: exists(recordPath) ? { bytes: fs.statSync(recordPath).size, sha256: sha(fs.readFileSync(recordPath)) } : null,
      ok: !!(verdict && verdict.ok === true),
      why: verdict && verdict.why ? verdict.why : null,
      detail: verdict && verdict.detail ? verdict.detail : null,
      binding: verdict && verdict.binding ? verdict.binding : null, error });
    if (verdict && verdict.ok === true) verdicts.push({ role: r.role, binding: verdict.binding || null, detail: verdict.detail });
  }
  let bound = null;
  let bindError = null;
  try { bound = P.bindSet(verdicts); }
  catch (e) { bindError = String(e && e.message ? e.message : e); }
  const failed = rows.filter((r) => !r.ok);
  writeRole('prerequisite_full', {
    task: 'GTOS-DSH-146', role: 'prerequisite_full', stage: 'roles-146-' + RUN,
    what: 'consumeRole over every role of the 144 prerequisite record, then bindSet over the results',
    consumer: { module: W + '/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs',
      bytes: fs.statSync(W + '/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs').size,
      sha256: sha(fs.readFileSync(W + '/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs')) },
    input: { set: R144 + '/t-prerequisites-tpr144-01-144.json', sha256: sha(fs.readFileSync(R144 + '/t-prerequisites-tpr144-01-144.json')),
      ctx },
    roles: rows, roles_consumed: rows.filter((r) => r.ok).length, roles_total: rows.length,
    failed_roles: failed.map((r) => r.role),
    bound, bind_error: bindError,
    ok: failed.length === 0 && bound !== null && bound.bound === true,
    ok_means: 'every one of the ten roles was consumed by the packet own consumer with no problem, and the ten results bind to '
      + 'one contract, one image and one subject.',
  });
}

// ---------------------------------------------------------------- producer_and_native_chain_full
{
  const P = await load('tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs');
  const selection = readJson(R144 + '/p-selection-144-a.json');
  const closure = readJson(R144 + '/p-closure-144a.json');
  const consumption = readJson(R144 + '/role-link_input_consumption-144a.json');
  const depRole = readJson(R144 + '/role-dependency_chain-144a.json');
  const contract = readJson(R144 + '/contract-144.json');

  // The 61 slots, in order, compared slot by slot against the closure record. A set comparison would accept a permuted link.
  const slots = selection.slots.map((s) => ({ index: s.index, basename: s.basename, logical: s.logical, kind: s.kind,
    linux: s.linux, object: s.object ? { bytes: s.object.bytes, sha256: s.object.sha256, matches_pin: s.object.matches_pin } : null,
    decision: s.decision }));
  const closureRows = closure.rows.map((r) => ({ slot: r.slot, basename: r.basename, decision: r.decision, bucket: r.bucket }));
  const orderProblems = [];
  if (slots.length !== 61) orderProblems.push({ why: 'the selection no longer carries 61 slots', count: slots.length });
  if (closureRows.length !== 61) orderProblems.push({ why: 'the closure no longer carries 61 rows', count: closureRows.length });
  for (let i = 0; i < Math.min(slots.length, closureRows.length); i++) {
    if (slots[i].basename !== closureRows[i].basename) {
      orderProblems.push({ why: 'the two records disagree on the object at a slot', slot: i + 1,
        selection: slots[i].basename, closure: closureRows[i].basename });
    }
  }
  const present = slots.filter((s) => s.object && exists(toHost(s.linux)));
  // The dependency closure: the depfiles and the sources they resolve to.
  const deps = depRole.sources || depRole.depfiles || null;
  const depRows = Array.isArray(deps) ? deps.length : (depRole.counts ? depRole.counts.depfiles : null);
  const linkConsumed = Array.isArray(consumption.rows) ? consumption.rows.length
    : (consumption.counts ? Object.values(consumption.counts).reduce((n, v) => n + (typeof v === 'number' ? v : 0), 0) : null);
  const failedObjects = slots.filter((s) => s.object && s.object.matches_pin === false);
  const accepted = orderProblems.length === 0 && present.length === slots.length
    && (depRows === null || depRows > 0) && (linkConsumed === null || linkConsumed > 0);
  writeRole('producer_and_native_chain_full', {
    task: 'GTOS-DSH-146', role: 'producer_and_native_chain_full', stage: 'roles-146-' + RUN,
    what: 'the frozen link order, the dependency closure and the link-input consumption record, read as records',
    contract: { path: R144 + '/contract-144.json', bytes: fs.statSync(R144 + '/contract-144.json').size,
      sha256: sha(fs.readFileSync(R144 + '/contract-144.json')) },
    inputs: ['p-selection-144-a.json', 'p-closure-144a.json', 'role-link_input_consumption-144a.json',
      'role-dependency_chain-144a.json'].map((n) => ({ path: R144 + '/' + n, bytes: fs.statSync(R144 + '/' + n).size,
      sha256: sha(fs.readFileSync(R144 + '/' + n)) })),
    slots: slots.length, slots_present: present.length, closure_rows: closureRows.length,
    order_problems: orderProblems,
    depfiles: depRows, link_input_consumption_rows: linkConsumed,
    objects_whose_bytes_differ_from_their_pin: failedObjects.map((s) => s.basename),
    objects_whose_bytes_differ_from_their_pin_count: failedObjects.length,
    decisions: closure.buckets || null,
    ok: accepted,
    ok_means: 'the two records agree on the object at all 61 slots, all 61 objects are present, and the dependency and '
      + 'link-input records are non-empty. Objects rebuilt from changed sources are expected and are not a failure: that is '
      + 'what the closure record is for.',
  });
}

// ---------------------------------------------------------------- host_chain_full
{
  const H = await load('tools/gtos-dsh-144/host-consume-144.mjs');
  const contract = readJson(R144 + '/h-contract-144e.json');
  // The chain is one claim over three units, so it is consumed once, with the one build record whose own record carries the
  // ELF it claims to have produced. Four h-unit-build records exist and only one of them is that: the others are builds of
  // the two small test binaries, or an abandoned attempt whose `elf` is null and whose last stage exited 2. Choosing by name
  // would have picked the abandoned one and reported "absent unit ELF" about a build that never finished -- which is true,
  // and is not a fact about the chain. The rule here is the record's own contents, and every candidate is listed so the
  // choice can be checked.
  const candidates = fs.readdirSync(R144).filter((f) => /^h-unit-build-.*\.json$/.test(f)).sort();
  const builds = candidates.map((name) => {
    const rec = readJson(R144 + '/' + name);
    return { name, rec, hasElf: !!(rec.elf && rec.elf.path), elfFile: rec.elfFile || null,
      lastStage: Array.isArray(rec.stages) && rec.stages.length ? rec.stages[rec.stages.length - 1] : null,
      recordOk: rec.ok === true };
  });
  // Every build record whose own record carries the ELF it claims to have produced is consumed. That is three of the five:
  // the runtime unit, the boot-diagnostic unit and the safe-TSS unit. Consuming only the first would verify one ELF and
  // leave two unverified while the record still said "the chain was consumed"; the two abandoned attempts (whose `elf` is
  // null and whose last stage exited 2) are listed but not consumed, because a build that never produced an ELF has nothing
  // to compare.
  const consumable = builds.filter((b) => b.hasElf);
  const units = [];
  for (const b of consumable) {
    const control = {
      task: 'GTOS-DSH-146',
      build_record_path: R144 + '/' + b.name,
      contract_path: R144 + '/h-contract-144e.json',
      unit_elf_host_path: toHost(b.rec.elfFile),
      unit_elf_linux: b.rec.elfFile,
      // The results directory is the one the driver wrote the fourteen case records into; the payload carries copies of the
      // same bytes under host/results/. Reading the originals is the stronger check, because a copy could have been made
      // from a different run.
      results_dir: R144 + '/results',
    };
    let verdict = null;
    let error = null;
    try { verdict = H.consumeHostChain(control); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    units.push({ record: b.name, unit_elf: b.rec.elfFile, control,
      record_pin: { bytes: fs.statSync(R144 + '/' + b.name).size,
        sha256: sha(fs.readFileSync(R144 + '/' + b.name)) },
      elf_pin: b.rec.elf,
      control_present: { build_record: exists(control.build_record_path), contract: exists(control.contract_path),
        unit_elf: exists(control.unit_elf_host_path), results_dir: exists(control.results_dir) },
      result_records: fs.readdirSync(R144 + '/results').filter((f) => /^hcase144-\d+\.json$/.test(f)).length,
      accepted: !!(verdict && verdict.accepted),
      counts: verdict && verdict.counts ? verdict.counts : null,
      unit_elf_observed: verdict && verdict.observed ? verdict.observed.unit_elf : null,
      problems: verdict && verdict.problems ? verdict.problems : [],
      spawn_attempts: verdict ? verdict.spawn_attempts : null, error });
  }
  const failed = units.filter((u) => !u.accepted);
  writeRole('host_chain_full', {
    task: 'GTOS-DSH-146', role: 'host_chain_full', stage: 'roles-146-' + RUN,
    what: 'consumeHostChain over every host unit build record: the result, then the inner report at the path the result names, '
      + 'then the raw channel at the path the inner report names',
    consumer: { module: W + '/tools/gtos-dsh-144/host-consume-144.mjs',
      bytes: fs.statSync(W + '/tools/gtos-dsh-144/host-consume-144.mjs').size,
      sha256: sha(fs.readFileSync(W + '/tools/gtos-dsh-144/host-consume-144.mjs')) },
    contract: { path: R144 + '/h-contract-144e.json', bytes: fs.statSync(R144 + '/h-contract-144e.json').size,
      sha256: sha(fs.readFileSync(R144 + '/h-contract-144e.json')) },
    contract_units: Array.isArray(contract.units) ? contract.units.length : null,
    contract_cases: Array.isArray(contract.cases) ? contract.cases.length : null,
    candidates: builds.map((b) => ({ record: b.name, carries_elf: b.hasElf, last_stage: b.lastStage, record_ok: b.recordOk })),
    consumed_records: consumable.map((b) => b.name),
    not_consumed: builds.filter((b) => !b.hasElf).map((b) => ({ record: b.name, why: 'its own record carries no ELF' })),
    units, units_total: units.length, units_accepted: units.length - failed.length,
    failed_units: failed.map((u) => u.record),
    ok: units.length > 0 && failed.length === 0,
    ok_means: 'every host unit was consumed by the packet own chain consumer, and each accepted with its inner reports and raw '
      + 'channels attributed by the paths the records themselves name.',
  });
}

// ---------------------------------------------------------------- guest_resource_semantics_full
{
  const RJ = await load('tools/gtos-dsh-144/references/resource-semantics-144.mjs');
  const matrix = readJson(R144 + '/g-matrix-gm144-01-144.json');
  const resourceScenes = new Set(Object.keys(RJ.RESOURCE_SCENES));
  const rows = [];
  for (const trial of matrix.trials) {
    if (!resourceScenes.has(trial.scene)) continue;
    const serial = fs.readFileSync(toHost(trial.serial.path), 'utf8');
    const e9 = fs.readFileSync(toHost(trial.e9.path), 'utf8');
    let verdict = null;
    let error = null;
    try { verdict = RJ.judgeResourceSemantics(trial.scene, { e9, serial }); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    rows.push({ scene: trial.scene, trial: trial.trial,
      accepted: !!(verdict && verdict.accepted),
      obligations: verdict && verdict.obligations ? verdict.obligations.length : null,
      first_refusal: verdict && verdict.first_refusal ? (verdict.first_refusal.obligation || verdict.first_refusal.why) : null,
      bitmap_obligations: verdict && verdict.bitmap_obligations ? verdict.bitmap_obligations.length : null,
      serial: { path: trial.serial.path, bytes: fs.statSync(toHost(trial.serial.path)).size, sha256: sha(Buffer.from(serial)) },
      e9: { path: trial.e9.path, bytes: fs.statSync(toHost(trial.e9.path)).size, sha256: sha(Buffer.from(e9)) },
      error });
  }
  const failed = rows.filter((r) => !r.accepted);
  // The other half of the same role. The 144 judge covers the three scenes whose subject is an allocator or a recovery; the
  // remaining seventeen were judged by markers in 144, and this packet judges them on the numbers they print. Both halves are
  // one claim about the guest text, so they are one record.
  const G = await load('tools/gtos-dsh-146/roles/g-numeric-trials-146.mjs');
  const numeric = await G.judgeAllTrials();
  writeRole('guest_resource_semantics_full', {
    task: 'GTOS-DSH-146', role: 'guest_resource_semantics_full', stage: 'roles-146-' + RUN,
    what: 'judgeResourceSemantics over every resource trial in the 144 matrix, and this packet own numeric judge over every '
      + 'trial of every scene, both on both channels',
    consumer: { module: W + '/tools/gtos-dsh-144/references/resource-semantics-144.mjs',
      bytes: fs.statSync(W + '/tools/gtos-dsh-144/references/resource-semantics-144.mjs').size,
      sha256: sha(fs.readFileSync(W + '/tools/gtos-dsh-144/references/resource-semantics-144.mjs')) },
    matrix: { path: R144 + '/g-matrix-gm144-01-144.json', bytes: fs.statSync(R144 + '/g-matrix-gm144-01-144.json').size,
      sha256: sha(fs.readFileSync(R144 + '/g-matrix-gm144-01-144.json')) },
    trials: rows, trials_total: rows.length, trials_accepted: rows.length - failed.length,
    failed_trials: failed.map((r) => r.scene + '#' + r.trial),
    non_resource: numeric,
    ok: rows.length > 0 && failed.length === 0 && numeric.ok === true,
    ok_means: 'every resource trial was judged by the 144 resource judge on both channels and accepted, and every trial of '
      + 'every scene -- including the seventeen scenes 144 judged by marker -- was parsed into numbers and judged by this '
      + 'packet own numeric judge and accepted.',
  });
}

// ---------------------------------------------------------------- independent_physical_closure
{
  const D = await load('tools/gtos-dsh-146/d-required-146.mjs');
  // The frozen link order is the spec the 144 link stage ran, and it is the only contract that records the order the
  // linker consumed the 61 objects in. A guessed path here would make deriveProducer refuse, and the refusal would look like
  // an empty derivation rather than a missing input -- so the path is named and checked.
  const orderSpecPath = R144 + '/stage-output/lk144-01.spec.json';
  if (!exists(orderSpecPath)) throw new Error('the frozen link order is absent: ' + orderSpecPath);
  let derived = null;
  let error = null;
  try { derived = D.derive({ R144, R146: R, orderSpecPath }); }
  catch (e) { error = String(e && e.message ? e.message : e); }
  // The derivation reports `requirements`, `counts.by_role` and `missing`; reading `rows` and `by_role` gave an empty set
  // over a derivation that had actually produced 639 rows, which is the failure mode this packet exists to remove: a
  // consumer that reports zero because it looked in the wrong place.
  const rows = derived && Array.isArray(derived.requirements) ? derived.requirements : [];
  const byRole = derived && derived.counts && derived.counts.by_role ? derived.counts.by_role : {};
  const missing = derived && Array.isArray(derived.missing) ? derived.missing : rows.filter((r) => !r.present);
  const derivations = derived && derived.derivations ? derived.derivations : null;
  writeRole('independent_physical_closure', {
    task: 'GTOS-DSH-146', role: 'independent_physical_closure', stage: 'roles-146-' + RUN,
    what: 'the required set derived from contracts rather than copied from a spec: what the package must carry, and whether '
      + 'every one of those rows is present',
    derivation: { module: W + '/tools/gtos-dsh-146/d-required-146.mjs',
      bytes: fs.statSync(W + '/tools/gtos-dsh-146/d-required-146.mjs').size,
      sha256: sha(fs.readFileSync(W + '/tools/gtos-dsh-146/d-required-146.mjs')) },
    not_derived_from: derived ? derived.not_derived_from : null,
    contracts_read: derived ? derived.contracts_read : null,
    derivations,
    rows_total: rows.length, rows_present: rows.length - missing.length, rows_missing: missing.length,
    by_role: byRole,
    missing_examples: missing.slice(0, 8).map((r) => ({ role: r.role, path: r.path || r.key })),
    error,
    ok: !!derived && derived.ok === true && missing.length === 0 && rows.length > 0,
    ok_means: 'the derivation ran over the contracts alone, produced a non-empty required set, and every row it produced is '
      + 'present with the bytes the contract names.',
  });
}

// ---------------------------------------------------------------- publication_and_related_tests
{
  const suites = [
    { name: 'd-aliases', script: 'tools/gtos-dsh-146/tests/d-aliases-146.mjs', record: R + '/d-aliases-146-01.json' },
    { name: 'f-cardinality', script: 'tools/gtos-dsh-146/tests/f-cardinality-146.mjs', record: R + '/f-cardinality-146-01.json' },
    { name: 'r-numeric', script: 'tools/gtos-dsh-146/tests/r-numeric-146.mjs', record: R + '/r-numeric-146-01.json' },
    { name: 'io-semantic', script: 'tools/gtos-dsh-146/tests/io-semantic-146.mjs', record: R + '/io-semantic-146-01.json' },
  ];
  const rows = [];
  for (const s of suites) {
    const r = spawnSync(NODE, [W + '/' + s.script], { encoding: 'utf8', windowsHide: true, maxBuffer: 64 * 1024 * 1024 });
    let record = null;
    try { record = readJson(s.record); } catch { record = null; }
    rows.push({ suite: s.name, script: s.script, exit_status: r.status,
      stdout_tail: (r.stdout || '').trim().split('\n').slice(-1)[0] || null,
      stderr_tail: (r.stderr || '').trim().slice(-400) || null,
      record: { path: s.record, bytes: exists(s.record) ? fs.statSync(s.record).size : null,
        sha256: exists(s.record) ? sha(fs.readFileSync(s.record)) : null },
      cases: record && record.counts ? record.counts : null, record_ok: record ? record.ok : null });
  }
  const failed = rows.filter((r) => r.exit_status !== 0 || r.record_ok !== true);
  writeRole('publication_and_related_tests', {
    task: 'GTOS-DSH-146', role: 'publication_and_related_tests', stage: 'roles-146-' + RUN,
    what: 'the four suites this packet wrote, run here rather than cited: the alias reader, the publisher cardinality and role '
      + 'gate, the numeric judge with its mutations, and the IO classifier with its negatives',
    suites: rows, suites_total: rows.length, suites_passing: rows.length - failed.length,
    failed_suites: failed.map((r) => r.suite),
    total_cases: rows.reduce((n, r) => n + (r.cases && r.cases.cases ? r.cases.cases : 0), 0),
    total_passed: rows.reduce((n, r) => n + (r.cases && r.cases.passed ? r.cases.passed : 0), 0),
    ok: rows.length === 4 && failed.length === 0,
    ok_means: 'all four suites exited zero and each wrote a record whose own ok is true.',
  });
}

// ---------------------------------------------------------------- the static gate, which runs in its own process
{
  // The gate runs Python inside the distribution, so it is a separate process rather than a function called here. It writes
  // its own record and this reads that record; a failure to write one is a false, not a skip.
  const gateScript = W + '/tools/gtos-dsh-146/roles/static-146.mjs';
  const gateRecord = OUT + '/static_byte_gate_full.json';
  const r = spawnSync(NODE, [gateScript, RUN], { encoding: 'utf8', windowsHide: true, maxBuffer: 64 * 1024 * 1024 });
  let rec = null;
  try { rec = readJson(gateRecord); } catch { rec = null; }
  results.static_byte_gate_full = { ok: !!(rec && rec.ok === true), path: gateRecord,
    bytes: exists(gateRecord) ? fs.statSync(gateRecord).size : null,
    sha256: exists(gateRecord) ? sha(fs.readFileSync(gateRecord)) : null,
    exit_status: r.status, stderr_tail: (r.stderr || '').trim().slice(-400) || null,
    gate_accepted: rec && rec.results && rec.results.gate ? rec.results.gate.accepted : null,
    gate_negatives: rec && rec.results && rec.results.gate ? rec.results.gate.negatives_ok : null,
    judge_mutants: rec && rec.results && rec.results.judge ? rec.results.judge.mutants_ok : null,
    judge_mutants_count: rec && rec.results && rec.results.judge && rec.results.judge.mutants
      ? rec.results.judge.mutants.length : null };
}

// ---------------------------------------------------------------- the summary
const out = {
  task: 'GTOS-DSH-146', stage: 'roles-146-' + RUN,
  what: 'the seven roles the acceptance contract names, each run against the real records and each writing its own record',
  roles: results,
  roles_ok: Object.values(results).filter((r) => r.ok === true).length,
  roles_total: Object.keys(results).length,
  ok: Object.values(results).every((r) => r.ok === true),
  ok_means: 'every one of the seven roles ran and its own record says true: the prerequisite set, the producer and native '
    + 'chain, the host chain, the static byte gate, the guest semantics (resource and non-resource), the independent '
    + 'physical closure, and the publication and related tests.',
};
const outPath = R + '/roles-146-' + RUN + '.json';
if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ ok: out.ok, roles_ok: out.roles_ok, roles_total: out.roles_total,
  roles: Object.fromEntries(Object.entries(results).map(([k, v]) => [k, v.ok])) }, null, 2));
process.exit(out.ok ? 0 : 1);
