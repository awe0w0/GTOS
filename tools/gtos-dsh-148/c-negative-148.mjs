// GTOS-DSH-148 C negatives: does the chain judge refuse each specific damage for the specific reason, or does it just
// happen to say no?
//
// A judge that answers "not ok" to everything is as useless as one that answers "ok" to everything, and the two are told
// apart the same way: by damaging one thing at a time and reading back which fact went red. So every case here names the
// fact it expects to fail, and a case passes only if that fact is the one that failed. The first case is the undamaged copy,
// which must stay green: without it, a judge that refused everything would pass every other case.
//
// The copy is a TEST_ONLY tree under the report root. It is a copy and not the package, because the package is what gets
// archived and a negatives run must not be able to touch it.
//
// Two of the package's eight material buckets are left out of the copy: the runtime supply (94 MB, and it is the bytes an
// emulator would need, which this judge never opens) and the source view (480 files the judge does not read either). That
// is a fact this run can check rather than assume: case 0 is the undamaged copy and it must come back green, so a bucket
// the judge actually needed would fail the undamaged case and be reported, not silently skipped.
const SKIP = [/^material\/runtime(\/|$)/, /^material\/sources(\/|$)/];
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { contractOf, derive, judgeAll, LAYERS, layerPath, reportPath } from './chain-148.mjs';

const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const SOURCE = R + '/package-148';
const RUN = 'cneg-' + process.pid;
const ROOT = R + '/test-only/' + RUN;

const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const writeJson = (p, j) => fs.writeFileSync(p, JSON.stringify(j, null, 2) + '\n');
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };

const judge = (pkg) => judgeAll(pkg, contractOf(pkg), derive(pkg));

// The facts that failed anywhere in the tree, in the order the judge reports them, with the stage that first failed on each.
// A derivation problem about one named stage is named by that stage, because "the registry never reserved this directory"
// is a different refusal from "a record names a stage the registry does not" and the two must not be merged into one string.
const failures = (result) => {
  const out = [];
  for (const s of result.stages) {
    for (const p of s.problems) out.push({ stage: s.stage, name: p.name, detail: p.detail });
  }
  for (const p of result.problems) {
    out.push({ stage: p.stage || null, name: p.stage ? 'derivation-stage:' + p.stage : 'derivation:' + p.why, detail: p });
  }
  return out;
};

const cases = [];
const add = (name, why, damage, expect) => cases.push({ name, why, damage, expect });

// ---- 0. the control: an undamaged copy is green.
add('undamaged-copy-stays-green',
  'the copy the other cases are made from must pass, or every refusal below would prove nothing',
  () => {},
  { ok: true, fails: [] });

// ---- 1. a requirement omitted from BOTH lists.
//
// The dangerous omission is the one that leaves no trace: drop a registry row and its stage directory together and the two
// lists still agree with each other. What catches it is that the record which fixes that stage's count still counts it.
add('requirement-omitted-from-both-lists',
  'a stage is deleted from the registry and from the package together, so the two lists still match; the record that fixes '
  + 'the count must notice',
  (pkg) => {
    const reg = pkg + '/control/stage-registry-147.jsonl';
    const lines = fs.readFileSync(reg, 'utf8').trim().split('\n');
    const kept = lines.filter((l) => !JSON.parse(l).stage.includes('hcase147-12'));
    fs.writeFileSync(reg, kept.join('\n') + '\n');
    fs.rmSync(pkg + '/chain/hcase147-12', { recursive: true, force: true });
  },
  { ok: false, fails: [{ name: 'derivation-stage:hcase147-12', stage: 'hcase147-12' }],
    anyOf: ['a record names a stage the registry does not'] });

// ---- 2. a stage in the package that no contract reserved.
add('stage-in-the-package-that-no-contract-reserved',
  'a chain directory appears that the registry never reserved',
  (pkg) => {
    fs.cpSync(pkg + '/chain/lk147-01', pkg + '/chain/lk147-09', { recursive: true });
    for (const n of fs.readdirSync(pkg + '/chain/lk147-09')) {
      fs.renameSync(pkg + '/chain/lk147-09/' + n, pkg + '/chain/lk147-09/' + n.replace('lk147-01', 'lk147-09'));
    }
  },
  { ok: false, fails: [{ name: 'derivation-stage:lk147-09', stage: 'lk147-09' }],
    anyOf: ['the package carries a stage the registry never reserved'] });

// ---- 3. a missing internal chain file.
add('missing-internal-chain-file',
  'the guard\'s own inner report is deleted from one stage',
  (pkg) => fs.rmSync(layerPath(pkg, 'pbl147-03', 'inner')),
  { ok: false, fails: [{ name: 'layer:inner-present', stage: 'pbl147-03' }] });

// ---- 4. a missing layer that only one route has.
add('missing-checkpoint-report-record',
  'the report record that carries a checkpoint\'s two guest channels is deleted',
  (pkg) => fs.rmSync(reportPath(pkg, 'gm147-07')),
  { ok: false, fails: [{ name: 'checkpoint:the-two-channels-are-in-the-package', stage: 'gm147-07' }] });

// ---- 5. a wrong subject.
add('wrong-subject-identity',
  'the subject pins a tool identity that is not the bytes on disk',
  (pkg) => {
    const p = layerPath(pkg, 'pbl147-05', 'bridge');
    const j = readJson(p);
    j.subject.tool_identity.sha256 = 'f'.repeat(64);
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'tool:the-subject-tool-identity-is-the-pinned-bytes', stage: 'pbl147-05' }] });

// ---- 6. a wrong order: the tool is not argv[0].
add('wrong-order-tool-is-not-argv0',
  'the arguments of one stage are rotated, so argv[0] is no longer the tool the route pins',
  (pkg) => {
    const p = layerPath(pkg, 'pbl147-06', 'inner');
    const j = readJson(p);
    j.argv = [j.argv[1], j.argv[0], ...j.argv.slice(2)];
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'tool:argv0-is-the-pinned-tool-for-this-route', stage: 'pbl147-06' }] });

// ---- 7. a wrong hash, in the raw channel.
add('wrong-hash-raw-channel',
  'the raw channel no longer hashes to what the guard recorded',
  (pkg) => {
    const p = layerPath(pkg, 'pbl147-07', 'raw');
    fs.writeFileSync(p, fs.readFileSync(p).toString('utf8') + '\n');
  },
  { ok: false, fails: [{ name: 'raw:bytes-match-the-guard-record', stage: 'pbl147-07' },
    { name: 'raw:hash-matches-the-guard-record', stage: 'pbl147-07' }] });

// ---- 8. a wrong hash, in the frozen helper.
add('wrong-hash-frozen-helper',
  'the helper that ran is not the draft its spec names',
  (pkg) => {
    const p = layerPath(pkg, 'pbl147-08', 'helper');
    fs.writeFileSync(p, fs.readFileSync(p).toString('utf8') + '\n// one line more\n');
  },
  { ok: false, fails: [{ name: 'helper:hash-is-the-pinned-hash', stage: 'pbl147-08' }] });

// ---- 9. a wrong exit layer: the wrapper claims an exit it cannot have had.
//
// The wrapper exits zero when it managed to run the transport; the transport's own number lives in the bridge. A terminal
// that reports a non-zero wrapper exit is claiming the transport was never judged.
add('wrong-exit-layer-wrapper-claims-a-failure',
  'the wrapper\'s exit is rewritten to a non-zero number, so the record says the transport never ran when the guard row says '
  + 'it did',
  (pkg) => {
    const p = layerPath(pkg, 'gm147-09', 'terminal');
    const j = readJson(p);
    j.transport_exit_code = 3;
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'exit:the-wrapper-ran-and-said-so', stage: 'gm147-09' }] });

// ---- 10. a wrong exit layer: the guard row no longer matches the child.
add('wrong-exit-layer-guard-row-disagrees-with-the-child',
  'the guard\'s exit code is rewritten so it is not the child\'s return code',
  (pkg) => {
    const p = layerPath(pkg, 'gm147-10', 'bridge');
    const j = readJson(p);
    j.guard_row.exit_code = 0;
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'exit:the-guard-number-is-the-child-number', stage: 'gm147-10' }] });

// ---- 11. a checkpoint whose deadline kill is not declared.
add('checkpoint-deadline-kill-not-declared',
  'the verdict stops declaring that the kill at the deadline was the expected outcome, so -9 would read as a crash',
  (pkg) => {
    const p = layerPath(pkg, 'gm147-11', 'bridge');
    const j = readJson(p);
    j.verdict.deadline_kill_expected = false;
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'exit:the-deadline-kill-is-the-expected-outcome', stage: 'gm147-11' }] });

// ---- 12. an EMPTY raw channel where the guard recorded bytes.
//
// A compiler stage's raw channel is legitimately empty -- the object goes to a file, not to stdout -- so emptying one would
// prove nothing. A checkpoint's channel is not: the emulator writes its banner there, and the guard records how many bytes.
add('empty-raw-where-the-guard-recorded-bytes',
  'the raw file of a checkpoint is emptied but the guard\'s record still says it wrote bytes; this is a different fact from '
  + 'an absent raw',
  (pkg) => fs.writeFileSync(layerPath(pkg, 'gm147-12', 'raw'), ''),
  { ok: false, fails: [{ name: 'raw:bytes-match-the-guard-record', stage: 'gm147-12' }],
    mustNotFail: ['layer:raw-present'] });

// ---- 13. an ABSENT raw channel.
add('absent-raw-channel',
  'the raw file is deleted; this must be refused as an absent layer, not as an empty one',
  (pkg) => fs.rmSync(layerPath(pkg, 'gm147-13', 'raw')),
  { ok: false, fails: [{ name: 'layer:raw-present', stage: 'gm147-13' }],
    mustNotFail: ['raw:bytes-match-the-guard-record'] });

// ---- 14. an EMPTY raw channel that the guard also declares empty: empty is not by itself a failure.
//
// This is the pair to case 12. The same empty file is a fault there and not a fault here, and the only difference is whether
// the guard's own record agrees with it. A judge that refused empty channels outright would pass case 12 and fail this one.
add('empty-raw-declared-empty-is-accepted',
  'the raw file is emptied and the guard\'s record is corrected to say it wrote nothing, so the two agree; the judge must '
  + 'not refuse an empty channel merely for being empty',
  (pkg) => {
    fs.writeFileSync(layerPath(pkg, 'gm147-14', 'raw'), '');
    const p = layerPath(pkg, 'gm147-14', 'inner');
    const j = readJson(p);
    j.child.output_file_bytes = 0;
    j.child.output_sha256 = crypto.createHash('sha256').update(Buffer.alloc(0)).digest('hex');
    writeJson(p, j);
  },
  { ok: true, fails: [], mustNotFail: ['layer:raw-present', 'raw:empty-is-declared-empty', 'raw:bytes-match-the-guard-record',
    'raw:hash-matches-the-guard-record'] });

// ---- 15. a preserved failure that is quietly deleted.
add('preserved-failure-deleted',
  'a failed original is removed from the package and from the registry, as if it had never run',
  (pkg) => {
    const reg = pkg + '/control/stage-registry-147.jsonl';
    const lines = fs.readFileSync(reg, 'utf8').trim().split('\n');
    fs.writeFileSync(reg, lines.filter((l) => !JSON.parse(l).stage.includes('hu147-15')).join('\n') + '\n');
    fs.rmSync(pkg + '/chain/hu147-15', { recursive: true, force: true });
  },
  { ok: false, fails: [{ name: 'derivation-stage:hu147-15', stage: 'hu147-15' }],
    anyOf: ['a preserved failure original is not in the package'] });
// ---- 16. a preserved failure relabelled as a success.
add('preserved-failure-relabelled-as-a-success',
  'a failed link\'s own bridge record is rewritten to say it was accepted',
  (pkg) => {
    const p = layerPath(pkg, 'hu147-15', 'bridge');
    const j = readJson(p);
    j.ok = true;
    j.verdict.accepted = true;
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'preserved:no-record-in-the-package-calls-it-green', stage: 'hu147-15' }] });

// ---- 17. the spec's route disagrees with the registry.
add('spec-route-disagrees-with-the-registry',
  'one stage\'s spec claims a different route than the registry reserved for it',
  (pkg) => {
    const p = layerPath(pkg, 'pbl147-12', 'spec');
    const j = readJson(p);
    j.route = 'assembly';
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'spec:route-is-the-registry-route', stage: 'pbl147-12' }] });

// ---- 18. a producer slot whose origin is unnamed.
add('reused-producer-object-without-an-origin',
  'a reused slot stops naming where it was preserved from, so nobody can tell what the link actually consumed',
  (pkg) => {
    const p = pkg + '/control/p-build-147a.json';
    const j = readJson(p);
    delete j.reused[0].from;
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'derivation:a reused producer object names no origin', stage: null }],
    anyOf: ['a reused producer object names no origin'] });

// ---- 19. a matrix trial that is not ok.
add('guest-trial-that-is-not-ok',
  'a trial the matrix carries is marked not ok, so it cannot be counted as a satisfied requirement',
  (pkg) => {
    const p = pkg + '/control/g-matrix-gm147-01-147.json';
    const j = readJson(p);
    j.trials[4].ok = false;
    writeJson(p, j);
  },
  { ok: false, fails: [{ name: 'derivation:a matrix trial is not ok, so it is not a satisfied requirement', stage: null }],
    anyOf: ['a matrix trial is not ok'] });

// ---- the run
const results = [];
const main = () => {
  fs.mkdirSync(ROOT, { recursive: true });
  for (const [i, c] of cases.entries()) {
    const dir = ROOT + '/case-' + String(i).padStart(2, '0');
    fs.cpSync(SOURCE, dir, { recursive: true,
      filter: (src) => !SKIP.some((re) => re.test(path.relative(SOURCE, src).split(path.sep).join('/'))) });
    try { c.damage(dir); } catch (e) { results.push({ case: c.name, why: c.why, error: 'the damage itself failed: ' + e.message,
      ok: false }); continue; }
    let result;
    try { result = judge(dir); } catch (e) {
      results.push({ case: c.name, why: c.why, error: 'the judge threw: ' + e.message, ok: false });
      continue;
    }
    const fails = failures(result);
    const names = new Set(fails.map((f) => f.name));
    const expected = c.expect.fails.map((f) => f.name);
    const missing = expected.filter((n) => !names.has(n));
    const anyOfSatisfied = !c.expect.anyOf || c.expect.anyOf.some((needle) =>
      fails.some((f) => f.name.includes(needle) || JSON.stringify(f.detail).includes(needle)));
    const wronglyFailed = (c.expect.mustNotFail || []).filter((n) => names.has(n));
    const okVerdict = result.ok === c.expect.ok;
    // A refusal that names a fact is a precise refusal only if the fact it names is one of the facts the damage touched.
    const ok = okVerdict && missing.length === 0 && anyOfSatisfied && wronglyFailed.length === 0
      && (c.expect.ok ? fails.length === 0 : fails.length > 0);
    results.push({
      case: c.name, why: c.why,
      expected_verdict: c.expect.ok ? 'green' : 'refused',
      verdict: result.ok ? 'green' : 'refused',
      expected_facts: expected, expected_any_of: c.expect.anyOf || null,
      must_not_fail: c.expect.mustNotFail || null,
      failed_facts: fails.slice(0, 8),
      failed_fact_count: fails.length,
      stages_refused: [...new Set(fails.map((f) => f.stage).filter(Boolean))],
      missing_expected_facts: missing,
      facts_that_should_not_have_failed: wronglyFailed,
      ok,
    });
  }
  const record = {
    task: 'GTOS-DSH-148', kind: 'chain_negatives',
    what: 'one damage at a time against a copy of the package, each case naming the fact it expects to go red. The first '
      + 'case is the undamaged copy, so a judge that refused everything would fail this suite rather than pass it',
    source_package: SOURCE, work_root: ROOT,
    cases: results.length,
    passed: results.filter((r) => r.ok).length,
    failed: results.filter((r) => !r.ok).map((r) => r.case),
    results,
    ok: results.every((r) => r.ok),
    ok_means: 'every case produced the verdict it expected and the exact fact it expected to fail, no case failed a fact it '
      + 'said must not fail, and the undamaged copy stayed green',
  };
  // The record is written to the first free name of its series, so a re-run adds a record instead of replacing the one that
  // said what the previous run found.
  let out = null;
  for (let i = 1; i < 100 && !out; i += 1) {
    const p = R + '/c-negatives-148-' + String(i).padStart(2, '0') + '.json';
    if (!exists(p)) out = p;
  }
  if (!out) throw new Error('no free record name');
  fs.writeFileSync(out, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ cases: record.cases, passed: record.passed, failed: record.failed,
    detail: results.filter((r) => !r.ok).map((r) => ({ case: r.case, expected: r.expected_facts, got: r.failed_facts.map((f) => f.name),
      verdict: r.verdict, error: r.error })), record: out }, null, 2));
  process.exit(record.ok ? 0 : 1);
};
main();
