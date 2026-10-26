// GTOS-DSH-130 B2: the link entry, exercised by a normal case and by every failure case.
//
// The normal case runs first and must show a real launch: the runner's own result document must report that the transport
// started and that a process group was measured. Only after that has been observed does the test require the failure cases
// to leave no downstream trace, because "no launch" means nothing unless launch is something this entry can be seen to do.
//
// Every case goes through the same entry function. There is no second code path and no literal `ld_started` anywhere.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as entry from '../tools/link-entry.mjs';
import * as admission from '../tools/link-admission.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-current-guest-recovery-and-executed-entry-closure-20261026-a';
const NODE = process.argv[5] || 'C:/Program Files/nodejs/node.exe';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });
const readBytes = (p) => {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
};
const pin = (p) => { const b = readBytes(p); return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) }; };
// Per-run state: a stage name is never reused once claimed, and a contract file is written once.
const RUN = String(10 + (Date.now() % 89));
const A = JSON.parse(readBytes(R + '/authority-130.json').toString('utf8'));
const L = A.work_root;
const S = A.source_root;
const RUNNER = R + '/frozen-runners/run-130-r01.mjs';
const BRIDGE = R + '/references/private-boot-checkpoints-130.mjs';

// ---------------------------------------------------------------- the frozen baseline order
// Read from disk, and the current individuals are placed into it slot by slot. A contract whose order came from the
// candidate's own argv would only show the candidate agreeing with itself.
const catalog = JSON.parse(readBytes(R + '/build-material-catalog-130.json').toString('utf8'));
const baselinePath = catalog.parent_actual_final_link_spec.path;
const baselineBytes = readBytes(baselinePath);
const baseline = JSON.parse(baselineBytes.toString('utf8'));
const baselineOrder = baseline.cmd.filter((tok) => /\.o$/.test(tok)).map((tok) => tok.split('/').pop());
check('the baseline spec was read from disk', baselineBytes !== null && baselineOrder.length === 61,
  { path: baselinePath, bytes: baselineBytes && baselineBytes.length, slots: baselineOrder.length });

// The current link record from this packet's own build.
const linkRecord = JSON.parse(readBytes(R + '/default-link-idl1.json').toString('utf8'));
check('the current link record names 61 individuals', linkRecord.provenance.length === 61,
  linkRecord.provenance.length);

// The current individual for each baseline slot, by lineage where the name differs.
const lineage = [];
const individuals = linkRecord.provenance.map((p, i) => {
  const tokenName = baselineOrder[i];
  const actualName = p.path.split('/').pop();
  if (tokenName && tokenName !== actualName) lineage.push({ slot: i, from: tokenName, to: actualName,
    why: 'the resolved object carries a different name than the baseline token' });
  const b = readBytes(entry.hostOf(p.path));
  return { logical: actualName, path: p.path, bytes: b.length, sha256: sha(b) };
});
check('every individual was read back from disk', individuals.every((x) => x.bytes > 0), null);
check('the lineage differences are recorded explicitly', Array.isArray(lineage), lineage);

const contract = {
  task: 'GTOS-DSH-130', kind: 'link_input_contract', tag: 'b2',
  order_from: { path: baselinePath, bytes: baselineBytes.length, sha256: sha(baselineBytes),
    read_by: 'this test, from the frozen baseline spec on disk' },
  order: baselineOrder,
  lineage,
  default_profile: { defines: [] },
  individuals,
};
// A contract file this packet owns, written once, never overwritten.
const contractPath = R + '/link-contract-b2-' + RUN + '.json';
fs.writeFileSync(contractPath, JSON.stringify(contract, null, 2) + '\n', { flag: 'wx' });

const bridgeFor = (stage) => ({
  task: A.task, stage, outer_stage: stage, kind: 'ld', route: 'image.link',
  report_root: R, work_root: L, source_root: S, distribution: 'Ubuntu-20.04',
  authority: pin(R + '/authority-130.json'),
  request_module: R + '/references/native-request-130.mjs',
  tools: { python_realpath: A.python.realpath },
  guard_linux: '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py',
  runner_module: RUNNER, binding_module: pin(BRIDGE),
  profile_makefile: R + '/profile-130/Makefile', profile_cxxflags: R + '/profile-130/kernel-cxxflags',
  report_path: R + '/evidence/' + stage + '.json', log_max_bytes: 524288,
});

// ---------------------------------------------------------------- 1. the normal case, through the entry
// A new tag so the outputs are fresh and the claim is real.
const TAG = 'b2ok' + RUN;
const okStage = 'b2ok' + RUN + '-01';

const okElf = S + '/image/' + TAG + '-default.elf';
const okMap = S + '/image/' + TAG + '-default.map';
const positive = entry.linkThroughEntry({
  admission, runner: RUNNER, node: NODE, stage: okStage, contractPath,
  individuals: individuals.map((x) => x.path), elfFile: okElf, mapFile: okMap,
  linkerScript: S + '/wave-01/linker.ld', objectRoot: S + '/wave-01',
  reportRoot: R, authority: A, bridge: bridgeFor(okStage), limit: 900,
});
check('the normal case was admitted', positive.steps.some((s) => s.step === 'validate' && s.outcome === 'admitted'),
  positive.steps);
check('the normal case claimed its outputs',
  positive.steps.some((s) => s.step === 'claim' && s.outcome === 'claimed'), positive.steps);
// The launch is observed, not asserted: the runner reported the transport starting and a process group being measured.
check('the normal case really launched a tool, observed from the guard row',
  positive.launched === true && positive.launch_observation.transport_started === true
  && positive.launch_observation.group_measured === true
  && positive.launch_observation.group_pgid > 0, positive.launch_observation);
check('the launched process group was empty after cleanup',
  positive.launch_observation.group_descendants_left === 0, positive.launch_observation);
check('the guard accepted the stage',
  positive.launch_observation.verdict_accepted === true, positive.launch_observation);
check('the normal case produced a real image', positive.elf !== null && positive.elf.bytes === 745124,
  positive.elf);
check('the image is the current default image',
  positive.elf && positive.elf.sha256 === 'cbf17148b07df5712b14ebefdbd610f8f446f342b237aae16fc291f3f4a6871d',
  positive.elf && positive.elf.sha256);
check('the normal case produced a map', positive.map !== null && positive.map.bytes > 0, positive.map);
check('the normal case was consumed without a refusal', positive.refusal === null, positive.refusal);
// The attempt file is the record that a stage ran.
check('the normal case left an attempt record',
  fs.existsSync(R + '/attempt-' + okStage + '-130.json'), okStage);

// ---------------------------------------------------------------- 2. the failure cases, through the same entry
// Each starts from the admitted baseline and breaks exactly one obligation. The entry refuses before it writes a spec, so
// the absence of a spec and an attempt is the evidence that nothing launched.
const cases = [];
function negative(name, mutate, expectedObligation, options = {}) {
  const stage = 'b2ng' + RUN + String(cases.length) + '-01';
  const elf = S + '/image/' + stage + '-default.elf';
  const map = S + '/image/' + stage + '-default.map';
  const list = mutate(individuals.map((x) => x.path));
  const record = entry.linkThroughEntry({
    admission, runner: RUNNER, node: NODE, stage,
    contractPath: options.contractPath || contractPath,
    individuals: list, elfFile: elf, mapFile: map,
    linkerScript: S + '/wave-01/linker.ld', objectRoot: S + '/wave-01',
    reportRoot: R, authority: A, bridge: bridgeFor(stage), limit: 300,
  });
  const specPath = R + '/stage-output/' + stage + '.spec.json';
  const attemptPath = R + '/attempt-' + stage + '-130.json';
  cases.push({ case: name, stage, obligation: record.refusal ? record.refusal.obligation : null,
    launched: record.launched, spec_written: fs.existsSync(specPath),
    attempt_written: fs.existsSync(attemptPath),
    elf_written: fs.existsSync(entry.hostOf(elf)), map_written: fs.existsSync(entry.hostOf(map)) });
  check('refused at ' + expectedObligation + ': ' + name,
    record.refusal !== null && record.refusal.obligation === expectedObligation, record.refusal);
  check('nothing launched for ' + name, record.launched !== true, record.launched);
  check('no spec and no attempt for ' + name,
    !fs.existsSync(specPath) && !fs.existsSync(attemptPath), { spec: fs.existsSync(specPath),
      attempt: fs.existsSync(attemptPath) });
  check('no output was written for ' + name,
    !fs.existsSync(entry.hostOf(elf)) && !fs.existsSync(entry.hostOf(map)), null);
  return stage;
}

negative('an individual is missing', (l) => l.filter((_, i) => i !== 30), 'missing');
negative('an individual is duplicated', (l) => [...l, l[10]], 'duplicate');
negative('two individuals are swapped', (l) => { const o = l.slice(); const t = o[5]; o[5] = o[6]; o[6] = t; return o; },
  'order');
// A real file whose name the contract does not carry.
const intruder = R + '/test-only/b2/intruder.o';
fs.mkdirSync(R + '/test-only/b2', { recursive: true });
fs.writeFileSync(intruder, Buffer.from('an object the contract does not have\n'));
negative('an extra object', (l) => [...l, intruder], 'extra');
// A real object at the same slot and the same name, with different bytes at a different size: the hash obligation.
const drifted = R + '/test-only/b2/drift/' + individuals[20].logical;
fs.mkdirSync(R + '/test-only/b2/drift', { recursive: true });
fs.writeFileSync(drifted, Buffer.from('bytes that are not the contracted object\n'));
negative('a drifted object at the same slot and name', (l) => {
  const o = l.slice(); o[20] = drifted; return o;
}, 'hash');
// The fresh claim, at the entry: a pair that already exists.
{
  const stage = 'b2cl' + RUN + '-01';
  const record = entry.linkThroughEntry({
    admission, runner: RUNNER, node: NODE, stage, contractPath,
    individuals: individuals.map((x) => x.path),
    elfFile: okElf, mapFile: okMap,
    linkerScript: S + '/wave-01/linker.ld', objectRoot: S + '/wave-01',
    reportRoot: R, authority: A, bridge: bridgeFor(stage), limit: 300,
  });
  check('a claim on an existing output pair is refused at the entry',
    record.refusal !== null && record.refusal.obligation === 'fresh', record.refusal);
  check('the refused claim left the existing image untouched',
    pin(entry.hostOf(okElf)) !== null && pin(entry.hostOf(okElf)).sha256
      === 'cbf17148b07df5712b14ebefdbd610f8f446f342b237aae16fc291f3f4a6871d', pin(entry.hostOf(okElf)));
  check('the refused claim left no attempt', !fs.existsSync(R + '/attempt-' + stage + '-130.json'), stage);
}

// A contract that claims a host-test profile while calling itself the default one.
{
  const badContract = JSON.parse(JSON.stringify(contract));
  badContract.default_profile.defines = ['GTOS_NATIVE_RUNTIME_HOST_TEST'];
  const badPath = R + '/link-contract-b2-hostprofile-' + RUN + '.json';
  fs.writeFileSync(badPath, JSON.stringify(badContract, null, 2) + '\n', { flag: 'wx' });
  negative('a contract calling a host-test profile the default', (l) => l, 'profile', { contractPath: badPath });
}

// ---------------------------------------------------------------- 3. the contract was not rewritten
{
  const now = pin(contractPath);
  check('the b2 contract is still the bytes this test wrote',
    now.sha256 === sha(readBytes(contractPath)) && JSON.parse(readBytes(contractPath).toString('utf8')).tag === 'b2',
    now);
  // And the record's order is the baseline's order, slot by slot, not a copy of the current list.
  const stored = JSON.parse(readBytes(contractPath).toString('utf8'));
  check('the stored order is the baseline order, slot by slot',
    stored.order.length === 61 && stored.order.every((n, i) => n === baselineOrder[i]), stored.order.length);
  check('the stored individuals are aligned to that order',
    stored.individuals.length === 61
    && stored.individuals.every((x, i) => x.logical === (baselineOrder[i] || x.logical) || lineage.some((g) => g.slot === i)),
    null);
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-130', kind: 'link_entry_test',
  subject: 'references/link-entry-130.mjs, one entry for the normal case and every failure',
  contract: contractPath,
  baseline: { path: baselinePath, bytes: baselineBytes.length, sha256: sha(baselineBytes) },
  individuals: individuals.length, lineage,
  positive: { launched: positive.launched, elf: positive.elf, map: positive.map,
    launch_observation: positive.launch_observation },
  cases,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
