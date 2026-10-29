// GTOS-DSH-149 H: the seven mandatory consumer roles, executed against the package.
//
// WHAT THIS FILE IS FOR. The 147 packet's seven roles were decided by a harness that imported a module and checked that the
// module's needs were present, then wrote `ok: true`. The 147 review's finding was that this is not a qualification: a role
// that was never executed cannot say anything about the payload, and "the module exists and the files it wants exist" is a
// statement about the inventory, not about the bytes. So every role here calls its consumer's real business entry, hands it
// package bytes, reads the answer the consumer produced, and requires that answer to be true -- and then damages one
// business input and requires the same entry to answer false for the reason the damage names.
//
// WHAT IS KEPT SEPARATE. The inventory question ("is every member present and is it the bytes the plan pinned") is answered
// by the closure audit and by the required set, not here. This file answers the business question. If an entry cannot be
// called, the role is `blocked` and says why; a hash comparison is never promoted into a business verdict.
//
// THE TRACE WRAPS THE CALL, NOT THE IMPORT. `withTrace` installs a counter around the filesystem functions for the duration
// of the entry call, so what it measures is the reads the business decision made. Importing a module is not a read of the
// package and is not counted; a consumer that read nothing while returning true is visible as `reads.total === 0`.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const R147 = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const R148 = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const R140 = 'F:/GTOS-Chromium/artifacts/native-guarded-frame-recovery-repeatability-and-portable-consumption-20261027-a';
const PKG = (process.env.GTOS_H_ROLES_PKG || (R + '/package-149')).replace(/\\/g, '/');
const OUT = (process.env.GTOS_H_ROLES_OUT || (R + '/roles-149')).replace(/\\/g, '/');
// THE CODE ROOT AND THE ENGINE ROOT ARE DECLARED, NOT ASSUMED.
//
// This harness loads two kinds of module: its own siblings (prereq-149.mjs, the F harnesses) and the packaged consumers that
// live under the engine directory. In the working tree those are a directory and its engine/ subdirectory. In the extracted
// payload they are one directory, because that is how the package carries them. So both roots are named here, each with a
// default that is correct for the working tree and each overridable, and every run records which it used. A run that cannot
// find a module at the root it was given refuses; it does not try the other root, because trying the other root is how a
// harness ends up deciding from the tree the payload exists to replace.
const CODE = (process.env.GTOS_H_ROLES_CODE || (R + '/worktree-149/tools/gtos-dsh-149')).replace(/\\/g, '/');
const ENGINE = (process.env.GTOS_H_ROLES_ENGINE || (CODE + '/engine')).replace(/\\/g, '/');
const TOOLS = CODE;
const TOOLS147 = (process.env.GTOS_H_ROLES_TOOLS147 || (R147 + '/worktree-147/tools/gtos-dsh-147')).replace(/\\/g, '/');
const TOOLS146 = (process.env.GTOS_H_ROLES_TOOLS146 || (R148 + '/worktree-148/tools/gtos-dsh-146')).replace(/\\/g, '/');
// The Linux side of the package, for the one consumer that runs inside the guard.
const PKG_LINUX = '/mnt/f' + PKG.replace(/^[A-Za-z]:/, '');
const LINUX_WORK = '/var/tmp/gtos-dsh-149-e759-role-semantics';
const LINUX_WORK_HOST = '//wsl$/Ubuntu-20.04' + LINUX_WORK;
const GUARD_LINUX = '/mnt/f/GTOS-Chromium/artifacts/native-cxx-full-output-frontier-20261022-a/guard-041-capture.py';
// The engine refuses a TEST_ONLY context that does not live under its TEST_ONLY prefix, and it is right to: a fixture that
// could be created anywhere could be created on top of the production root. So the fixture transactions this file takes are
// taken there, and everything they write stays there.
//
// The directory name is FIXED, not per-run. The payload a fixture transaction archives contains the fixture's own paths, so a
// per-run directory makes the archived bytes -- and therefore the digests the negatives quote -- different on every run over
// the same package. A record whose bytes change when nothing changed cannot be pinned by a seal. Before a run starts, the
// previous fixture tree is moved aside to the first free `h-roles-attempt-NN` name: superseded evidence is preserved, never
// overwritten, and the fixed path is free again.
const TESTONLY = (process.env.GTOS_H_ROLES_TESTONLY || (R + '/test-only/h-roles')).replace(/\\/g, '/');
const rotateTestOnly = () => {
  if (!exists(TESTONLY)) return null;
  let n = 1;
  while (exists(TESTONLY + '-attempt-' + String(n).padStart(2, '0'))) n += 1;
  const to = TESTONLY + '-attempt-' + String(n).padStart(2, '0');
  fs.renameSync(TESTONLY, to);
  return to;
};
// THE OUTPUT ROOT IS FIXED AND ROTATED, FOR THE SAME REASON THE FIXTURE ROOT IS.
//
// A role record is a judgement about a tree, and the records are collected INTO that tree afterwards. So a run must start
// from an empty output root: if it did not, `collect` would find a previous run's record under the same name and -- because
// collect refuses a name whose bytes differ -- would have to choose between refusing the new record and overwriting the old
// one. Neither is acceptable, so the previous tree is moved aside to the first free `roles-149-attempt-NN` name before the
// run starts. Superseded evidence is preserved and the fixed path is free again.
const rotateOut = () => {
  if (!exists(OUT)) return null;
  let n = 1;
  while (exists(OUT + '-attempt-' + String(n).padStart(2, '0'))) n += 1;
  const to = OUT + '-attempt-' + String(n).padStart(2, '0');
  fs.renameSync(OUT, to);
  return to;
};
process.env.GTOS_FIXTURE_RUN = 'h-roles';
// The fixture base is the harness's own TEST_ONLY tree, which is the directory it rotates. Declaring it here is what makes
// the run relocatable: a re-run from an extracted payload points `TESTONLY` inside its own run root, and the fixtures follow
// it there instead of being written into the live report root.
process.env.GTOS_FIXTURE_ROOT = TESTONLY.replace(/\/h-roles$/, '');

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
// Fixture directories carry the process id so a re-run never collides with an earlier one. The id is not part of what a
// negative decided, so it is folded out of anything that is written down, and two runs over the same package produce the
// same record bytes.
const escapeRe = (x) => x.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const scrub = (s) => String(s)
  .replace(new RegExp(escapeRe(process.env.GTOS_FIXTURE_ROOT || (R + '/test-only')) + '/[^"\\s,]*', 'g'), 'test-only/<run>/...')
  .replace(/h-roles-\d+/g, 'h-roles-<run>');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
// A read of a file under the WSL work root. Those reads cross the 9p share and occasionally fail with ETIMEDOUT or EIO while
// the file is perfectly readable a moment later; a report the guard has already written is not absent because one open timed
// out. So the read is retried a few times and a persistent failure is reported as an error rather than silently as "no file".
const readJsonFromWorkRoot = (p, tries = 5) => {
  let last;
  for (let i = 0; i < tries; i += 1) {
    try { return readJson(p); } catch (e) { last = e; Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 200); }
  }
  throw last;
};
const existsFromWorkRoot = (p, tries = 5) => {
  for (let i = 0; i < tries; i += 1) {
    try { fs.statSync(p); return true; } catch (e) {
      if (e && (e.code === 'ENOENT' || e.code === 'ENOTDIR')) return false;
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 200);
    }
  }
  return false;
};
const mod = async (p) => import(pathToFileURL(p).href);
const rel = (p) => String(p).replace(/\\/g, '/').replace(PKG + '/', '');
const writeNew = (p, text) => {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, text, { flag: 'wx' });
  const b = fs.readFileSync(p);
  return { path: p, bytes: b.length, sha256: sha(b) };
};

// ---------------------------------------------------------------- the trace
//
// Only the functions a business decision actually uses to open a file are wrapped, and only while the call runs. The wrapper
// is installed across the await points of the entry, not just up to its first one: every entry here is async, so a
// synchronous wrapper would record nothing and would report a trace of zero reads as if it had measured something.
//
// Reads are classified into four places, because "outside the package" on its own would be a misleading number. A consumer
// program and the fixture it was handed are not the payload; the payload is the thing the consumer has to be able to decide
// from. The fact that matters is that nothing was read from a live artifact tree.
const TRACED = ['readFileSync', 'readdirSync', 'existsSync', 'statSync', 'lstatSync', 'openSync', 'readlinkSync', 'opendirSync'];
const normalPath = (p) => String(p).replace(/\\/g, '/').replace(/^([A-Za-z]):/, (m, d) => d.toLowerCase() + ':');
const withTrace = async (fn, roots, forbiddenRoots, exemptRoots = []) => {
  const original = {};
  const reads = [];
  for (const name of TRACED) {
    original[name] = fs[name];
    fs[name] = function traced(...args) {
      if (typeof args[0] === 'string') reads.push({ fn: name, path: normalPath(args[0]) });
      return original[name].apply(fs, args);
    };
  }
  let value;
  let error = null;
  try { value = await fn(); } catch (e) { error = e; } finally {
    for (const name of TRACED) fs[name] = original[name];
  }
  const under = (p, r) => p === r || p.startsWith(r + '/');
  const where = (p) => {
    for (const [name, root] of roots) if (under(p, normalPath(root))) return name;
    return 'elsewhere';
  };
  const buckets = { in_package: 0, in_consumer: 0, in_packet_records: 0, in_work_root: 0, in_own_output: 0,
    elsewhere: 0 };
  const outside = [];
  const forbidden = [];
  // EVERY DISTINCT PATH, WITH THE DECLARED SOURCE IT BELONGS TO.
  //
  // A count per bucket is not an allowlist: `in_packet_records: 3` says three reads happened somewhere under the report root,
  // and the 148 review is right that this is exactly where a live-root read hides -- the bucket is correct and the read is
  // still a fallback. So each distinct path is kept with the source it was classified to, and the record carries the whole
  // list. `forbidden` is decided per path here rather than from a count, and a path under an exempt root -- the harness's own
  // output directory, declared by the caller that created it -- is classified as the harness's own output and is not
  // forbidden, which is the only exemption this file grants.
  const distinct = new Map();
  for (const r of reads) {
    let w = where(r.path);
    const exempt = exemptRoots.some((x) => under(r.path, normalPath(x)));
    if (exempt) w = 'in_own_output';
    buckets[w] += 1;
    if (w === 'elsewhere') outside.push(r.path);
    const isForbidden = !exempt && forbiddenRoots.some((f) => under(r.path, normalPath(f)));
    if (isForbidden) forbidden.push(r.path);
    const seen = distinct.get(r.path);
    if (seen) { seen.calls += 1; }
    else distinct.set(r.path, { path: r.path, bucket: w, forbidden: isForbidden, calls: 1 });
  }
  const paths = [...distinct.values()].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  return { value, error, trace: {
    total: reads.length,
    ...buckets,
    outside_paths: [...new Set(outside)].slice(0, 16),
    roots: roots.map(([name, root]) => ({ name, root })),
    // Roots that must not be read at all. When this harness runs in place the 147 origin root is forbidden, so a role that
    // reached back into the tree this packet preserved would be visible. When the post-seal receipt re-runs it from the
    // extracted payload, the report root is forbidden too: from there nothing may be read from the live root, and the
    // `in_packet_records` bucket -- correct in an in-place run -- would otherwise hide exactly that read.
    forbidden: forbiddenRoots.map(normalPath),
    exempt: exemptRoots.map(normalPath),
    forbidden_reads: forbidden.length,
    forbidden_paths: [...new Set(forbidden)].slice(0, 12),
    // The stability fact. `elsewhere` and `forbidden` are the two ways a live fallback could hide, and both have to be empty
    // -- not `total - in_package`, which would count the consumer's own source file as a fallback.
    no_live_fallback: buckets.elsewhere === 0 && forbidden.length === 0,
    // The allowlist itself: every distinct path, the source it was classified to, and whether it was forbidden.
    distinct_paths: paths.length,
    paths,
    // A trace that measured nothing cannot support a claim about where the reads went, and an `elsewhere` of zero over an empty
    // trace is the shape a broken tracer would report. So the record says whether the measurement happened at all.
    measured: reads.length > 0,
    kind: 'installed around the entry call across its await points: every fs call the business decision made while it ran',
  } };
};

// THE REQUIRED SET THIS RUN DECIDES, READ FROM THE PACKAGE.
//
// The required set is a judgement about the payload, and the payload carries it: the newest record in judgements/ that held.
// Reading it from the package rather than from the report root is what lets the post-seal receipt re-run this harness on the
// extraction and get the same answer, and it is the difference between a role that decides the payload and a role that decides
// whatever the report root happens to hold. If the package carries none, that is a refusal, not a reason to look elsewhere.
const requiredSetInPackage = () => {
  const dir = PKG + '/judgements';
  if (!exists(dir)) throw new Error('the package carries no judgements directory, so it carries no required set: ' + dir);
  const names = fs.readdirSync(dir).filter((n) => /^required-set-149-\d+\.json$/.test(n)).sort();
  if (!names.length) throw new Error('the package carries no required-set-149 record under ' + dir);
  const held = names.map((n) => ({ n, j: readJson(dir + '/' + n) })).filter((x) => x.j && x.j.ok === true);
  if (!held.length) throw new Error('the package carries no required set that held: ' + names.join(', '));
  const rec = held[held.length - 1];
  return { name: rec.n, path: dir + '/' + rec.n, record: rec.j };
};

// THE STATIC GATE'S SUPPORT MODULES, MATERIALISED INTO THIS PACKET'S OWN FROZEN STAGE.
//
// The guard needs a Linux path and the engine root is a Windows path. The obvious move -- spell the engine file as
// /mnt/f/... and hand that to the guard -- executes a file inside the working tree, which is the tree the payload exists to
// replace; the 148 review found exactly that, together with a work-root path that was not the one its own authority named.
// So the three Python modules are copied out of the engine root into a named stage under this packet's frozen Linux work
// root, each copy is re-read and its hash compared with the bytes it was copied from, and the guard is given the staged path.
// `static-gate-147b.py` loads `static-judge-147.py` from its own directory, so the three travel together.
//
// The copy is made once per process and reused, because both the role and its negative run the same program and a second copy
// would be a second chance for the bytes to differ.
let staticStageCache = null;
const staticStage = () => {
  if (staticStageCache) return staticStageCache;
  const dir = LINUX_WORK + '/stage-static-149';
  const host = LINUX_WORK_HOST + '/stage-static-149';
  fs.mkdirSync(host, { recursive: true });
  const rows = [];
  for (const name of ['static-gate-147.py', 'static-gate-147b.py', 'static-judge-147.py']) {
    const from = ENGINE + '/' + name;
    if (!exists(from)) throw new Error('the engine root carries no ' + name + ', so the gate cannot be run: ' + from);
    const b = fs.readFileSync(from);
    const to = host + '/' + name;
    fs.writeFileSync(to, b);
    const back = fs.readFileSync(to);
    const h = sha(back);
    if (h !== sha(b) || back.length !== b.length) {
      throw new Error('the staged copy of ' + name + ' is not the bytes it was copied from');
    }
    rows.push({ name, from, staged: dir + '/' + name, bytes: b.length, sha256: h });
  }
  staticStageCache = { dir, host, rows };
  return staticStageCache;
};

// ---------------------------------------------------------------- the roles
//
// One entry per role. `run()` returns the consumer's own answer, and `subjectOf` names the bytes the answer is about so the
// seal can compare it against what the plan pinned. `negatives` each damage one business input and name the fact they
// expect to go red: a role that refuses everything would pass a negative suite that only asked for "false".
const casesRoot = OUT + '/cases';
const roleDir = (role) => OUT + '/' + role;

const stage = () => {
  const roles = [];

  // ---------------------------------------------------------------- 1. prerequisite_full
  //
  // THE FINDING THIS REPLACES. 148 answered this role with `spec.buildRequiredSet`, which walks a required-member list and
  // hashes what it finds. That is the D question -- is the required set a closure of the package -- and it is a real question,
  // but it is not this role. This role is the ten prerequisite roles of the build that produced the image: the source
  // contract, the producer chain, the dependency chain, the link input consumption, the typed terminal, the default link, the
  // static image, the related host tests, the tooling subject and the runtime environment, each decided from the artifact it
  // is a claim about, and then bound into one set. A role that clears a file inventory has decided nothing about whether
  // those ten are about the same build.
  //
  // So the entry is the original `admitPrerequisiteSet`, which calls `consumeRole` for each of the ten and then `bindSet`.
  // `buildRequiredSet` still runs, as the D sub-step, and is reported in the detail so the two answers are visible side by
  // side rather than one standing in for the other.
  roles.push({
    role: 'prerequisite_full',
    decides: 'the ten prerequisite roles of the build are each decided from the artifact they are a claim about, by the '
      + 'original consumer, on the package bytes, and the set binds to one contract, one image and one tool generation',
    consumer: TOOLS + '/prereq-149.mjs',
    entry: 'judge -> admitPrerequisiteSet -> consumeRole x10 -> bindSet',
    subject_of: () => PKG + '/control/role-source_profile_contract-147.json',
    async run() {
      const m = await mod(TOOLS + '/prereq-149.mjs');
      return m.judge({ pkg: PKG, engine: ENGINE });
    },
    verdict: (r) => ({
      ok: r.ok === true,
      decided: r.roles_required,
      met: r.roles_that_produced_a_fact,
      detail: {
        entry: r.entry,
        consumer_version: r.consumer_version,
        consume_role_version: r.underlying.consumeRole_version,
        roles_that_produced_a_fact: r.roles_that_produced_a_fact,
        binding: r.binding && { bound: r.binding.bound, contract: r.binding.contract, image: r.binding.image,
          subject: r.binding.subject, named: r.binding.named },
        aliases: r.aliases && { pins_named: r.aliases.pins_named, unresolved: r.aliases.unresolved,
          index_members: r.aliases.index.members },
        roles: (r.roles || []).map((x) => ({ role: x.role, kind: x.kind, fact_ok: x.fact_ok, why: x.why,
          pins_named: x.pins_named })),
        problems: r.problems,
      },
    }),
    obligations: (r) => [
      { name: 'all-ten-roles-were-opened', value: r.roles_that_produced_a_fact === 10 },
      { name: 'every-role-produced-a-true-fact', value: (r.roles || []).every((x) => x.fact_ok === true) },
      { name: 'the-set-binds-to-one-build', value: !!(r.binding && r.binding.bound === true) },
      { name: 'the-consumer-is-the-original-entry', value: /admitPrerequisiteSet/.test(String(r.entry))
        && r.roles_required === 10 },
      { name: 'nothing-was-read-outside-the-package', value: !!(r.aliases && r.aliases.unresolved === 0) },
    ],
    negatives: [
      // THE FIRST INTEGRATION NEGATIVE: the required set is green but the ten roles are not.
      //
      // This is the 148 defect reproduced. There, the role was answered with `spec.buildRequiredSet` -- a program with a real
      // answer, which can be true over a package whose ten roles were never decided -- and the ten roles were never asked.
      //
      // The damage has to be one the required set tolerates and the ten roles do not, or the negative would only show that two
      // programs disagree. `ROLE_SHAPE` in the frozen admission module says a `static_image` record has kind
      // `static_image_inspection`; the required set reads the record's pins and does not read its kind. So the record is left
      // byte-identical in every pin it states and only its kind is changed: the required set still resolves every pin it names,
      // and the ten roles refuse, because a record of the wrong kind is not a record for that role. A harness that answered
      // this role with the required set would pass a package whose role records say the wrong thing about themselves.
      { name: 'only-the-required-set-was-decided-but-the-ten-roles-were-not',
        why: 'the kind of one role record is changed, with every pin it states left intact. The required set reads pins, so it '
          + 'stays green; the ten-role consumer reads the kind, so it must refuse',
        damage: () => {
          const dir = casesRoot + '/prerequisite_full/d-answers-t-claims-nothing';
          fs.mkdirSync(dir, { recursive: true });
          for (const n of ['control', 'material', 'chain', 'engine', 'judgements']) {
            if (exists(PKG + '/' + n)) fs.cpSync(PKG + '/' + n, dir + '/' + n, { recursive: true });
          }
          const p = dir + '/control/role-static_image-147.json';
          const j = readJson(p);
          j.kind = 'something_this_packet_does_not_declare';
          fs.writeFileSync(p, JSON.stringify(j, null, 2) + '\n');
          return dir;
        },
        run: async (dir) => {
          const p = await mod(TOOLS + '/prereq-149.mjs');
          const s = await mod(TOOLS + '/spec-149.mjs');
          const roles = await p.judge({ pkg: dir, engine: ENGINE });
          const required = s.buildRequiredSet({ root: dir });
          return { roles, required };
        },
        verdict: (r) => r.roles.ok === true,
        expect: 'the ten-role answer is false while the required set is still true, which is exactly the substitution the '
          + 'role must not make',
        hit: (r) => r.roles.ok === false && r.required.ok === true,
        summarise: (r) => ({ ten_roles_ok: r.roles.ok, required_set_ok: r.required.ok,
          ten_roles_why: r.roles.why || (r.roles.problems || []).map((p) => p.why).slice(0, 4) }),
      },
      // The second: a role record whose artifact is not the bytes it pins. The consumer re-reads every pin, so a record that
      // describes a different file is refused rather than believed.
      { name: 'a-role-record-that-describes-artifacts-the-package-does-not-hold',
        why: 'one role record is edited so the image it pins is not the image the package carries. The consumer re-derives '
          + 'every pin from bytes it reads itself, so the record is refused',
        damage: () => {
          const dir = casesRoot + '/prerequisite_full/role-record-pins-other-bytes';
          fs.mkdirSync(dir, { recursive: true });
          for (const n of ['control', 'material', 'chain', 'engine', 'judgements']) {
            if (exists(PKG + '/' + n)) fs.cpSync(PKG + '/' + n, dir + '/' + n, { recursive: true });
          }
          const p = dir + '/control/role-static_image-147.json';
          const j = readJson(p);
          j.elf.sha256 = 'f'.repeat(64);
          fs.writeFileSync(p, JSON.stringify(j, null, 2) + '\n');
          return dir;
        },
        run: async (dir) => (await mod(TOOLS + '/prereq-149.mjs')).judge({ pkg: dir, engine: ENGINE }),
        verdict: (r) => r.ok === true,
        expect: 'the static_image role is refused because the image it names is not the image the record pins',
        hit: (r) => r.ok === false && ((r.unresolved_count || 0) > 0
          || (r.roles || []).some((x) => x.role === 'static_image' && x.fact_ok === false)),
        summarise: (r) => ({ ok: r.ok, why: r.why || null, unresolved: r.unresolved_count || 0,
          roles_failing: (r.roles || []).filter((x) => !x.fact_ok).map((x) => ({ role: x.role, why: x.why })) }),
      },
    ],
  });

  // ---------------------------------------------------------------- 2. producer_and_native_chain_full
  roles.push({
    role: 'producer_and_native_chain_full',
    decides: 'every layer of the guarded chain for each stage the packet ran is present and agrees with its own records, '
      + 'with the three exit layers equal, and the preserved failures are still failures',
    consumer: TOOLS + '/chain-149.mjs',
    entry: 'judgeAll',
    subject_of: () => PKG + '/control/c-chain-147ca.json',
    async run() {
      const m = await mod(TOOLS + '/chain-149.mjs');
      return m.judgeAll(PKG, m.contractOf(PKG), m.derive(PKG));
    },
    verdict: (r) => ({
      ok: r.ok === true,
      decided: r.facts_total,
      met: r.facts_held,
      detail: { stages_checked: r.stages_checked, stages_held: r.stages_held, preserved: r.stages_that_are_preserved_failures,
        problems: r.problems.length },
    }),
    obligations: (r) => [
      { name: 'every-stage-was-judged', value: r.stages_checked === 117 },
      { name: 'every-fact-held', value: r.facts_held === r.facts_total && r.facts_total > 0 },
      { name: 'the-preserved-failures-are-still-failures', value: r.stages_that_are_preserved_failures === 5 },
      { name: 'no-stage-was-left-with-a-problem', value: r.problems.length === 0 },
    ],
    negatives: [
      { name: 'a-chain-layer-that-is-absent',
        why: 'one stage loses its bridge report, so the layer the exits are read from is gone',
        damage: () => {
          const dir = casesRoot + '/producer_and_native_chain_full/missing-layer';
          fs.mkdirSync(dir, { recursive: true });
          for (const n of ['control', 'chain']) {
            fs.cpSync(PKG + '/' + n, dir + '/' + n, { recursive: true });
          }
          fs.unlinkSync(dir + '/chain/pbl147-03/pbl147-03.bridge.json');
          return dir;
        },
        run: async (dir) => (async () => { const m = await mod(TOOLS + '/chain-149.mjs'); return m.judgeAll(dir, m.contractOf(dir), m.derive(dir)); })(),
        verdict: (r) => r.ok === true,
        expect: 'the missing bridge report is named as the layer that is absent, on the stage it was taken from',
        hit: (r) => r.ok === false && r.problems.some((p) => p.stage === 'pbl147-03'
          && /bridge/i.test(String(p.first || p.name))),
        summarise: (r) => ({ ok: r.ok, stages_checked: r.stages_checked, stages_held: r.stages_held,
          problems: r.problems.map((p) => ({ stage: p.stage, first: p.first, why: p.why })) }),
      },
      { name: 'a-guard-exit-that-disagrees-with-its-child',
        why: 'one checkpoint\'s guard row is changed so the guard exit is no longer the child return code',
        damage: () => {
          const dir = casesRoot + '/producer_and_native_chain_full/disagreeing-exit';
          fs.mkdirSync(dir, { recursive: true });
          for (const n of ['control', 'chain']) fs.cpSync(PKG + '/' + n, dir + '/' + n, { recursive: true });
          const p = dir + '/chain/gm147-10/gm147-10.bridge.json';
          const j = readJson(p);
          j.guard_row.exit_code = 0;
          fs.writeFileSync(p, JSON.stringify(j, null, 2) + '\n');
          return dir;
        },
        run: async (dir) => (async () => { const m = await mod(TOOLS + '/chain-149.mjs'); return m.judgeAll(dir, m.contractOf(dir), m.derive(dir)); })(),
        verdict: (r) => r.ok === true,
        expect: 'the exit layers are compared with each other, so the changed guard number is a refusal on that stage',
        hit: (r) => r.ok === false && r.problems.some((p) => p.stage === 'gm147-10'),
        summarise: (r) => ({ ok: r.ok, stages_checked: r.stages_checked, stages_held: r.stages_held,
          problems: r.problems.map((p) => ({ stage: p.stage, first: p.first, why: p.why })) }),
      },
    ],
  });

  // ---------------------------------------------------------------- 3. host_chain_full
  //
  // The business question: does the host unit chain -- 21 build inputs, 3 units, 14 cases, each with an inner report, a raw
  // channel, an argv and three agreeing exit layers -- stand up when it is read out of the package? The records name the
  // paths the run wrote, which are paths on the live tree; the relocation map is what moves them, and the consumer refuses
  // the map if no read used it, so a run that quietly read the live tree cannot pass.
  roles.push({
    role: 'host_chain_full',
    decides: 'the production host consumer is called on the real unit chain in the package: every build input, unit ELF, '
      + 'case, inner report, raw channel, argv and exit layer is opened, re-hashed and compared with what its record claims',
    consumer: ENGINE + '/host-consume-147.mjs',
    entry: 'consumeHostChain',
    subject_of: () => PKG + '/control/h-consumption-147e.json',
    async run() {
      const m = await mod(ENGINE + '/host-consume-147.mjs');
      const named = new Set();
      const build = readJson(PKG + '/control/h-unit-build-h147b-147.json');
      const contract = readJson(PKG + '/control/h-contract-147e.json');
      for (const i of build.inputs) if (i.host_path) named.add(i.host_path);
      for (const u of contract.units) {
        if (u.elf && u.elf.path) named.add(u.elf.path);
        if (u.elf_linux) named.add(u.elf_linux);
      }
      for (const n of fs.readdirSync(PKG + '/material/host/results')) {
        const r = readJson(PKG + '/material/host/results/' + n);
        if (r.inner_report && r.inner_report.path) named.add(r.inner_report.path);
        if (r.raw && r.raw.path) named.add(r.raw.path);
      }
      // Every path the records name is mapped, and the map is required to be total. An earlier round of this role mapped only
      // the sources, the ELFs and the logs, and the consumer then opened eighteen files on the live 147 tree -- the producer
      // objects and the three profile files -- while still reporting accepted, because a relocation map that covers most of
      // the names is a map that lets the rest through. The package carries those files too, so the map covers them.
      const toPackage = (p) => {
        const q = String(p).replace(/\\/g, '/');
        const m1 = /scene-bound-view\/(wave-0[12]\/.*)$/.exec(q);
        if (m1) return PKG + '/material/sources/scene-bound-view/' + m1[1];
        const m2 = /\/test-build\/[^/]+\/([^/]+)$/.exec(q);
        if (m2) return PKG + '/material/host/elf/' + m2[1];
        const m3 = /\/logs\/([^/]+)$/.exec(q);
        if (m3) return PKG + '/material/host/logs/' + m3[1];
        // The producer objects the ELF was linked from, and the three profile files the build record pins.
        const m4 = /\/producers\/p147\/([^/]+\.o)$/.exec(q);
        if (m4) return PKG + '/material/producers/' + m4[1];
        const m5 = /\/profile-147\/([^/]+)$/.exec(q);
        if (m5) return PKG + '/material/profile/' + m5[1];
        return null;
      };
      const aliases = {};
      const unmapped = [];
      for (const p of named) {
        const to = toPackage(p);
        if (to && exists(to)) aliases[p] = to;
        else unmapped.push(p);
      }
      const control = {
        task: 'GTOS-DSH-149',
        build_record_path: PKG + '/control/h-unit-build-h147b-147.json',
        contract_path: PKG + '/control/h-contract-147e.json',
        results_dir: PKG + '/material/host/results',
        unit_elf_host_path: PKG + '/material/host/elf/runtime_test.elf',
        path_aliases: aliases,
      };
      const answer = m.consumeHostChain(control);
      answer.__relocation = { named: named.size, aliased: Object.keys(aliases).length, unmapped };
      // The control is kept on the answer so a negative can damage exactly one of its entries instead of rebuilding it: a
      // rebuilt control would be a second, slightly different input, and the negative would then be testing that input.
      answer.__control = control;
      return answer;
    },
    verdict: (r) => ({
      ok: r.accepted === true,
      decided: r.counts.cases + r.counts.build_inputs,
      met: r.problems.length === 0 ? r.counts.cases + r.counts.build_inputs : 0,
      detail: { counts: r.counts, aliases: r.path_aliases, unmapped: r.__relocation.unmapped,
        problems: r.problems.slice(0, 4) },
    }),
    obligations: (r) => [
      { name: 'the-host-chain-is-accepted', value: r.accepted === true },
      { name: 'every-case-was-read', value: r.counts.cases === 14 },
      { name: 'every-exit-layer-was-read', value: r.counts.exit_layers === 14 },
      { name: 'every-argv-was-read', value: r.counts.argvs === 14 },
      { name: 'the-relocation-map-was-used', value: r.path_aliases.every_supplied_alias_used === true },
      // The map has to be total, not merely used: a map that covers most of the names leaves the rest reading the live tree,
      // and the consumer cannot see that because those reads succeed.
      { name: 'every-path-the-records-name-was-relocated', value: r.__relocation.unmapped.length === 0
        && r.__relocation.aliased === r.__relocation.named },
    ],
    negatives: [
      { name: 'a-contract-that-expects-fewer-cases',
        why: 'one unit\'s case list is shortened in a copy of the contract, so the unit no longer carries every case the '
          + 'consumer requires',
        damage: () => {
          const dir = casesRoot + '/host_chain_full/short-contract';
          fs.mkdirSync(dir, { recursive: true });
          const c = readJson(PKG + '/control/h-contract-147e.json');
          const runtime = c.units.find((u) => u.role === 'runtime');
          runtime.cases = runtime.cases.slice(0, 11);
          fs.writeFileSync(dir + '/contract.json', JSON.stringify(c, null, 2) + '\n');
          return dir;
        },
        run: async (dir, base) => (await mod(ENGINE + '/host-consume-147.mjs'))
          .consumeHostChain({ ...base, contract_path: dir + '/contract.json' }),
        verdict: (r) => r.accepted === true,
        expect: 'the unit does not carry every case',
        hit: (r) => r.accepted === false && r.problems.some((p) => /every case/.test(String(p.why))),
      },
      { name: 'a-build-input-that-drifted',
        why: 'one build input is replaced by different bytes, so the ELF is no longer the ELF those sources built',
        damage: () => {
          const dir = casesRoot + '/host_chain_full/drifted-input';
          fs.mkdirSync(dir, { recursive: true });
          const b = readJson(PKG + '/control/h-unit-build-h147b-147.json');
          const input = b.inputs[0];
          const src = PKG + '/material/sources/scene-bound-view/' + /scene-bound-view\/(wave-0[12]\/.*)$/
            .exec(String(input.host_path).replace(/\\/g, '/'))[1];
          const bytes = fs.readFileSync(src);
          bytes[0] = bytes[0] ^ 0x01;
          const drifted = dir + '/drifted.cpp';
          fs.writeFileSync(drifted, bytes);
          return { dir, drifted, input };
        },
        run: async (d, base) => (await mod(ENGINE + '/host-consume-147.mjs')).consumeHostChain({
          ...base,
          path_aliases: { ...base.path_aliases, [d.input.host_path]: d.drifted },
        }),
        verdict: (r) => r.accepted === true,
        expect: 'the build input does not agree with the bytes the build record pinned',
        hit: (r) => r.accepted === false && r.problems.some((p) => /build input/.test(String(p.member || p.why))),
      },
    ],
  });

  // ---------------------------------------------------------------- 4. static_byte_gate_full
  //
  // THE FINDING THIS REPLACES. 148 answered this role with `static-gate-147.py` alone, which is the image-level gate: it reads
  // the header, the multiboot word, the segments, the sections, the map and the script symbols, and it refuses four
  // single-defect copies. That is a real answer to a real question, and it is not the whole of what 147 executed. 147 ran two
  // static programs, and the second one -- `static-gate-147b.py` -- is the one that calls `static-judge-147.judge()` for the
  // real image, for a relocated copy of it, and for eighteen one-defect mutants, with the expected constructor table derived
  // from the input objects rather than from the image. That is the obligation 148 dropped: obligation 10 of the judge, the
  // constructor table, cannot be decided from the image at all -- it is a fact about the objects the link consumed. An image
  // that merely contains three functions named _GLOBAL__sub_I_* passes a name-presence check; it cannot pass this one.
  //
  // So this role runs BOTH programs, each as the guard's child, and requires both reports. The second control carries the
  // sixty-one packaged producer objects, so `derive_constructors` resolves every .init_array entry through its relocation to
  // the symbol it names and the expected table is a fact about the inputs.
  roles.push({
    role: 'static_byte_gate_full',
    decides: 'the packaged image is accepted by the image-level gate AND by the judge-driven gate, which derives the expected '
      + 'constructor table from the sixty-one input objects, and both gates refuse every one-defect copy they are given',
    consumer: ENGINE + '/static-gate-147b.py',
  consumer_files: [ENGINE + '/static-gate-147.py', ENGINE + '/static-gate-147b.py',
    ENGINE + '/static-judge-147.py'],
    entry: 'gate(argv[1] = control json) and judge(image, map, expected, objects)',
    subject_of: () => PKG + '/material/image/p147-default.elf',
    async run() {
      const selection = readJson(PKG + '/control/p-selection-147-a.json');
      // THE MAP NAMES THE OBJECTS AT THE PRODUCER'S PATHS, AND THE PACKAGE HOLDS THEM AT ITS OWN.
      //
      // The 147 map is a text file in which every input object is named by the absolute path the link was given:
      // `/var/tmp/gtos-dsh-147-e759-scene-bound-view/producers/p147/arp.o`. The package carries the same bytes under
      // `material/producers/arp.o`, and it does not carry the producer's tree -- that tree is the origin this packet preserves
      // and must not read. The judge's obligation "the map names an object at a different path" compares the map's spelling
      // with the spelling of the objects it is handed, so handing it packaged paths makes it refuse a map that is in fact
      // correct, for a reason that is about this packet's staging and not about the image.
      //
      // So the map is relocated: each input path is rewritten to the path of a staged copy of the packaged object, and nothing
      // else in the file changes. The relocation is not taken on trust and is not built from the sixty-one basenames. The
      // closure record for this very link states, for each of its sixty-one producer objects, the exact path the producer
      // used and the hash of the bytes that were there. Each spelling is taken from that record, the packaged member is
      // selected by that hash, and the member's bytes are re-hashed after being staged. So the claim "this name now denotes
      // the same object" is decided by content, which is stronger than the string comparison the obligation makes.
      const closureRecord = readJson(PKG + '/control/d-closure-dc147-02-147.json');
      const closureByPath = new Map();
      for (const m of closureRecord.members || []) {
        if (m && m.role === 'producer_object' && typeof m.src_linux === 'string' && typeof m.sha256 === 'string') {
          closureByPath.set(m.src_linux, m);
        }
      }
      // The staged tree keeps the shape the producer used -- one directory holding the objects -- because two of the judge's
      // own mutants are built by finding a map line that names an object under a `producers/` directory. Staging the objects
      // directly into a flat directory would silently remove those two mutants from the suite, and a suite that quietly lost
      // two members would still report "every mutant refused".
      const stageRoot = LINUX_WORK + '/tmp/judge-ps149-h/inputs/producers';
      const stageRootHost = LINUX_WORK_HOST + '/tmp/judge-ps149-h/inputs/producers';
      fs.mkdirSync(stageRootHost, { recursive: true });
      const relocation = { spellings_from_the_closure_record: closureByPath.size, rewritten: 0,
        not_named_by_the_closure_record: [], bytes_differ: [], staged: 0, rows: [] };
      const stagedBySha = new Map();
      let mapText = fs.readFileSync(PKG + '/material/image/p147-default.map', 'utf8');
      for (const [spelling, m] of closureByPath) {
        const member = (fs.existsSync(PKG + '/material/producers/' + m.rel.split('/').pop()))
          ? PKG + '/material/producers/' + m.rel.split('/').pop() : null;
        if (!member) { relocation.not_named_by_the_closure_record.push({ spelling, why: 'the package carries no copy' }); continue; }
        const bytes = fs.readFileSync(member);
        if (sha(bytes) !== m.sha256) {
          relocation.bytes_differ.push({ spelling, member, packaged_sha256: sha(bytes), closure_sha256: m.sha256 });
          continue;
        }
        // One staged file per distinct content, named as the packaged member is named.
        const name = m.rel.split('/').pop();
        const stagedHost = stageRootHost + '/' + name;
        if (!stagedBySha.has(m.sha256)) {
          fs.copyFileSync(member, stagedHost);
          stagedBySha.set(m.sha256, { host: stagedHost, linux: stageRoot + '/' + name, bytes: bytes.length });
          relocation.staged += 1;
        }
        const staged = stagedBySha.get(m.sha256).linux;
        if (!mapText.includes(spelling)) {
          relocation.not_named_by_the_closure_record.push({ spelling, why: 'the map does not use this spelling' });
          continue;
        }
        mapText = mapText.split(spelling).join(staged);
        relocation.rewritten += 1;
        relocation.rows.push({ basename: name, from: spelling, to: staged, bytes: bytes.length, sha256: m.sha256 });
      }
      // The staged objects are handed to the judge in the order the map names them, so the expected constructor table is
      // derived in link order and the map order is compared against the order the image really has.
      const mapOrderAfter = (() => {
        const ls = mapText.split('\n');
        const s = ls.findIndex((l) => l.trim() === 'Linker script and memory map');
        const out = [];
        for (const line of ls.slice(s + 1)) {
          const t = line.trim();
          if (!t) continue;
          const tok = t.split(/\s+/).pop();
          if (tok.endsWith('.o') && !out.includes(tok)) out.push(tok);
        }
        return out;
      })();
      if (relocation.not_named_by_the_closure_record.length || relocation.bytes_differ.length) {
        throw new Error('the map cannot be relocated onto the packaged objects: '
          + JSON.stringify({ not_named: relocation.not_named_by_the_closure_record.slice(0, 4),
            bytes_differ: relocation.bytes_differ.slice(0, 4) }));
      }
      if (mapOrderAfter.length !== closureByPath.size) {
        throw new Error('the relocated map names ' + mapOrderAfter.length + ' objects where the closure record names '
          + closureByPath.size);
      }
      const relocatedMapPath = LINUX_WORK + '/tmp/judge-ps149-h/p147-default.relocated.map';
      fs.writeFileSync(LINUX_WORK_HOST + '/tmp/judge-ps149-h/p147-default.relocated.map', mapText);
      // THE RELOCATED COPY. The same image and the same map at different paths, judged by the same function with the same
      // objects. A green verdict must not be a statement about one path, and this is the caller that says so: the bytes are
      // identical -- the judge records `same_bytes` -- so a difference in the verdict could only be caused by the paths.
      const movedMapPath = LINUX_WORK + '/tmp/judge-ps149-h/relocated/p147-default.map';
      fs.mkdirSync(LINUX_WORK_HOST + '/tmp/judge-ps149-h/relocated', { recursive: true });
      fs.writeFileSync(LINUX_WORK_HOST + '/tmp/judge-ps149-h/relocated/p147-default.map', mapText);
      const movedImagePath = LINUX_WORK + '/tmp/judge-ps149-h/relocated/p147-default.elf';
      fs.copyFileSync(PKG + '/material/image/p147-default.elf', LINUX_WORK_HOST + '/tmp/judge-ps149-h/relocated/p147-default.elf');
      // The objects, in the order the map names them -- which is the order the link placed them in, and so the order the
      // image's constructor table is in. `derive_constructors` walks its argument in order, so this is what makes the derived
      // table and the image's table comparable at all.
      const stagedObjectsInMapOrder = mapOrderAfter.map((linuxPath) => {
        const name = linuxPath.split('/').pop();
        const row = relocation.rows.find((x) => x.basename === name);
        if (!row) throw new Error('the relocated map names an object this role did not stage: ' + linuxPath);
        return row.to;
      });
      const staticMods = staticStage();
      const runGate = (control, stem, program) => {
        const controlHost = LINUX_WORK_HOST + '/inputs-' + stem + '.json';
        fs.mkdirSync(path.dirname(controlHost), { recursive: true });
        fs.writeFileSync(controlHost, JSON.stringify(control, null, 2) + '\n');
        const argv = ['--distribution', 'Ubuntu-20.04', '--exec', '/usr/bin/python3.8', '-B', '-S', GUARD_LINUX,
          '--stage', stem, '--purpose', 'python for GTOS-DSH-149 static byte gate ' + program,
          '--work-root', LINUX_WORK, '--cwd', LINUX_WORK, '--log', LINUX_WORK + '/logs/runs-149.jsonl', '--limit', '900',
          '--exec', '/usr/bin/python3.8', '--exec', '/usr/lib/x86_64-linux-gnu/ld-2.31.so', '--',
          '/usr/bin/python3.8', '-B', '-S', staticMods.dir + '/' + program,
          LINUX_WORK + '/inputs-' + stem + '.json'];
        const run = spawnSync('C:/Windows/System32/wsl.exe', argv, { encoding: 'utf8', windowsHide: true,
          maxBuffer: 64 * 1024 * 1024 });
        const innerPath = LINUX_WORK_HOST + '/logs/inner-report-' + stem + '.json';
        const inner = existsFromWorkRoot(innerPath) ? readJsonFromWorkRoot(innerPath) : null;
        // The report the gate writes, read back through the host spelling of the same file. The control carries the Linux
        // path the gate must use; this is the Windows view of that one path, not a second path built by prefixing, which
        // would name a directory that does not exist and would silently read nothing.
        const reportHost = LINUX_WORK_HOST + control.report_path.slice(LINUX_WORK.length);
        const report = existsFromWorkRoot(reportHost) ? readJsonFromWorkRoot(reportHost) : null;
        return { transport_exit: run.status, stderr: (run.stderr || '').slice(-800),
          stdout: (run.stdout || '').slice(-800),
          child_returncode: inner && inner.child ? inner.child.returncode : null,
          output_bytes: inner && inner.child ? inner.child.output_bytes : null,
          output_sha256: inner && inner.child ? inner.child.output_sha256 : null,
          report, control };
      };
      // The first gate: the image-level one, with its four negatives built beside it.
      const workA = LINUX_WORK + '/tmp/gate-ps149-h';
      fs.mkdirSync(LINUX_WORK_HOST + '/tmp/gate-ps149-h', { recursive: true });
      for (const stale of ['gate-report.json']) {
        if (exists(LINUX_WORK_HOST + '/tmp/gate-ps149-h/' + stale)) fs.unlinkSync(LINUX_WORK_HOST + '/tmp/gate-ps149-h/' + stale);
      }
      const controlA = {
        task: 'GTOS-DSH-149',
        stage: 'ps149-h01',
        image: PKG_LINUX + '/material/image/p147-default.elf',
        map: relocatedMapPath,
        negatives_root: workA,
        report_path: workA + '/gate-report.json',
        expected: {
          load_address: 1048576,
          entry_symbol: 'loader',
          object_order: selection.slots.map((s) => s.basename),
        },
        note: 'the packaged image and map, judged by the image-level gate the guard runs; the four negatives are built beside it',
      };
      const gate = runGate(controlA, 'ps149-h01', 'static-gate-147.py');

      // The second gate: the judge-driven one. It reads the same image and map, loads the sixty-one objects, derives the
      // constructor table from them, judges the real image and a relocated copy, and then judges eighteen mutants.
      const workB = LINUX_WORK + '/tmp/judge-ps149-h';
      fs.mkdirSync(LINUX_WORK_HOST + '/tmp/judge-ps149-h/scratch', { recursive: true });
      for (const stale of ['judge-report.json']) {
        if (exists(LINUX_WORK_HOST + '/tmp/judge-ps149-h/' + stale)) fs.unlinkSync(LINUX_WORK_HOST + '/tmp/judge-ps149-h/' + stale);
      }
      const objects = stagedObjectsInMapOrder;
      const controlB = {
        task: 'GTOS-DSH-149',
        stage: 'ps149-h02',
        image: PKG_LINUX + '/material/image/p147-default.elf',
        map: relocatedMapPath,
        objects,
        expected: {
          load_address: 1048576,
          entry_symbol: 'loader',
          // No constructor table is stated: the judge derives it from the objects it is handed, and stating one here would be
          // exactly the substitution this role exists to refuse.
        },
        scratch: workB + '/scratch',
        // The third caller the judge offers: the same image and the same map at a second path. Without these two fields the
        // judge simply does not make the third call, and the role would be reporting on one path while claiming the answer
        // does not depend on it.
        relocated_image: movedImagePath,
        relocated_map: movedMapPath,
        report_path: workB + '/judge-report.json',
        note: 'the packaged image and the relocated map, judged by the judge-driven gate, which derives the expected '
          + 'constructor table from the sixty-one staged objects; the eighteen mutants are written to the scratch directory',
      };
      const judged = runGate(controlB, 'ps149-h02', 'static-gate-147b.py');
      // `__control` is what the negatives are given as their baseline: the two gate answers this role decided, so a negative
      // can ask what the same judge says about a damaged copy without re-deriving the baseline itself.
      const answer = { gate, judge: { ...judged, objects: objects.length }, relocation, map_order_after: mapOrderAfter };
      return { ...answer, __control: answer };
    },
    verdict: (r) => {
      const g = r.gate.report;
      const j = r.judge.report;
      const mutants = (j && j.mutants) || [];
      const negativesA = (g && g.negatives) || [];
      return {
        ok: !!(g && g.ok === true && r.gate.transport_exit === 0 && r.gate.child_returncode === 0
          && j && j.ok === true && r.judge.transport_exit === 0 && r.judge.child_returncode === 0
          && j.real && j.real.accepted === true && mutants.length === 18 && mutants.every((m) => m.ok === true)),
        decided: negativesA.length + mutants.length + 2,
        met: negativesA.filter((n) => n.refused).length + mutants.filter((m) => m.refused).length
          + ((g && g.accepted) ? 1 : 0) + ((j && j.real && j.real.accepted) ? 1 : 0),
        detail: {
          gate: g && { ok: g.ok, accepted: g.accepted, negatives: negativesA.map((n) => ({ name: n.name, refused: n.refused })),
            image_bytes: g.image && g.image.bytes, map_objects: g.map_objects },
          judge: j && { ok: j.ok, accepted: j.real && j.real.accepted, objects_loaded: j.objects,
            mutants: mutants.length, mutants_refused: mutants.filter((m) => m.refused).length,
            mutants_ok: j.mutants_ok, counts: j.counts,
            relocated: j.relocated && { accepted: j.relocated.accepted, same_bytes: j.relocated.same_bytes,
              failed: j.relocated.obligations_failed },
            obligations_failed: j.real && j.real.obligations_failed,
            constructors_expected: j.real && j.real.constructors_expected,
            constructors_from: (j.real && j.real.constructors_derived_from || []).map((d) => d.object),
            map_order_matches_the_staged_argv: j.real && j.real.map_order_matches_argv },
          exit_layers: { gate_transport: r.gate.transport_exit, gate_child: r.gate.child_returncode,
            judge_transport: r.judge.transport_exit, judge_child: r.judge.child_returncode },
        },
      };
    },
    obligations: (r) => {
      const g = r.gate.report;
      const j = r.judge.report;
      const mutants = (j && j.mutants) || [];
      return [
        { name: 'the-image-level-gate-accepted-the-image', value: !!(g && g.accepted === true) },
        { name: 'the-image-level-gate-refused-every-negative', value: !!(g && g.negatives_ok === true) },
        { name: 'the-judge-accepted-the-image', value: !!(j && j.real && j.real.accepted === true) },
        { name: 'the-judge-loaded-the-sixty-one-input-objects', value: !!(j && j.objects === 61) },
        { name: 'the-judge-derived-the-constructor-table-from-the-objects',
          // The table the judge derives is `constructors_expected`, and the objects each entry came from are
          // `constructors_derived_from`. Both are facts about the input objects: the judge never reads a table from its control.
          value: !!(j && j.real && Array.isArray(j.real.constructors_expected) && j.real.constructors_expected.length > 0
            && Array.isArray(j.real.constructors_derived_from) && j.real.constructors_derived_from.length > 0) },
        { name: 'the-judge-accepted-the-relocated-copy', value: !!(j && j.relocated && j.relocated.accepted === true) },
        { name: 'the-judge-refused-every-one-of-the-eighteen-mutants', value: mutants.length === 18 && !!(j && j.mutants_ok) },
        { name: 'both-guard-children-exited-zero', value: r.gate.child_returncode === 0 && r.judge.child_returncode === 0 },
        { name: 'both-transports-exited-zero', value: r.gate.transport_exit === 0 && r.judge.transport_exit === 0 },
      ];
    },
    negatives: [
      // THE THIRD INTEGRATION NEGATIVE: the first gate is green but the full judge is refused.
      //
      // This is the 148 substitution reproduced as a check. The image-level gate can accept an image whose constructor table is
      // wrong, because it never looks at the objects. The judge does. So the negative takes the judge's own
      // `ctor-order-swapped` mutant -- two constructor pointers exchanged -- and requires that the image-level gate accepts the
      // real image while the judge refuses that mutant on the constructor obligation. A harness that answered this role with
      // the first gate alone would report green over an image the judge refuses.
      { name: 'the-first-gate-is-green-but-the-full-judge-refuses',
        why: 'two constructor pointers in the image are exchanged; the image-level gate does not examine the constructor table, '
          + 'and the judge does, because the expected order is derived from the input objects',
        damage: () => {
          const dir = casesRoot + '/static_byte_gate_full/first-gate-green-judge-red';
          fs.mkdirSync(dir, { recursive: true });
          fs.copyFileSync(PKG + '/material/image/p147-default.elf', dir + '/image.elf');
          fs.copyFileSync(PKG + '/material/image/p147-default.map', dir + '/image.map');
          return dir;
        },
        run: async (dir, base) => {
          const swapped = ((base.judge.report && base.judge.report.mutants) || [])
            .find((m) => m.name === 'ctor-order-swapped');
          return { gate: base.gate.report, judge: base.judge.report, swapped, dir };
        },
        verdict: (r) => !!(r.judge && r.judge.ok === true),
        expect: 'the image-level gate accepted the image while the judge refused the swapped constructor order, so the two '
          + 'answers differ and only the judge is right about the table',
        hit: (r) => !!(r.gate && r.gate.accepted === true && r.swapped && r.swapped.refused === true
          && r.swapped.hit_target === true),
        summarise: (r) => ({ gate_accepted: r.gate && r.gate.accepted, swapped_refused: r.swapped && r.swapped.refused,
          swapped_targets: r.swapped && r.swapped.targets, swapped_failed: r.swapped && r.swapped.obligations_failed }),
      },
      { name: 'a-map-that-is-not-the-image-map',
        why: 'the judge-driven gate is handed a map whose bytes differ from the packaged map, so the object order it reads '
          + 'cannot be the image\'s',
        damage: () => {
          const dir = casesRoot + '/static_byte_gate_full/wrong-map';
          fs.mkdirSync(dir, { recursive: true });
          const b = fs.readFileSync(PKG + '/material/image/p147-default.map');
          fs.writeFileSync(dir + '/wrong.map', b.subarray(0, Math.floor(b.length / 2)));
          return dir;
        },
        run: async (dir, base) => {
          const control = { ...base.judge.control, stage: 'ps149-h03',
            map: '/mnt/f' + (dir + '/wrong.map').replace(/^[A-Za-z]:/, ''),
            scratch: LINUX_WORK + '/tmp/judge-ps149-neg/scratch',
            report_path: LINUX_WORK + '/tmp/judge-ps149-neg/judge-report.json' };
          fs.mkdirSync(LINUX_WORK_HOST + '/tmp/judge-ps149-neg/scratch', { recursive: true });
          const controlHost = LINUX_WORK_HOST + '/inputs-ps149-h03.json';
          fs.writeFileSync(controlHost, JSON.stringify(control, null, 2) + '\n');
          const argv = ['--distribution', 'Ubuntu-20.04', '--exec', '/usr/bin/python3.8', '-B', '-S', GUARD_LINUX,
            '--stage', 'ps149-h03', '--purpose', 'python for GTOS-DSH-149 static judge negative',
            '--work-root', LINUX_WORK, '--cwd', LINUX_WORK, '--log', LINUX_WORK + '/logs/runs-149.jsonl',
            '--limit', '900', '--exec', '/usr/bin/python3.8', '--exec', '/usr/lib/x86_64-linux-gnu/ld-2.31.so', '--',
            '/usr/bin/python3.8', '-B', '-S', staticStage().dir + '/static-gate-147b.py',
            LINUX_WORK + '/inputs-ps149-h03.json'];
          const run = spawnSync('C:/Windows/System32/wsl.exe', argv, { encoding: 'utf8', windowsHide: true,
            maxBuffer: 64 * 1024 * 1024 });
          const report = existsFromWorkRoot(LINUX_WORK_HOST + '/tmp/judge-ps149-neg/judge-report.json')
            ? readJsonFromWorkRoot(LINUX_WORK_HOST + '/tmp/judge-ps149-neg/judge-report.json') : null;
          return { status: run.status, report, stderr: (run.stderr || '').slice(-600) };
        },
        verdict: (r) => !!(r.report && r.report.ok === true),
        expect: 'the judge refuses the image when the map it is given is not that image\'s map',
        hit: (r) => !!(r.report && r.report.ok === false && r.report.real && r.report.real.accepted === false),
      },
    ],
  });

  // ---------------------------------------------------------------- 5. guest_resource_semantics_full
  //
  // THE FINDING THIS REPLACES. 148 answered this role with `n-numeric-147.mjs` alone -- the numeric judge, which reads the
  // matrix, each trial's e9 and serial channel, and decides whether the scene reached its declared stop with the values it
  // declared. That is N, and N is half the answer. The other half is `resource-semantics-147.mjs`, the R consumer, which reads
  // the same channels and decides a different question: what the guest did with the resources it was given -- whether the
  // frame set it dumped reassembles from its own bits, whether the delta it printed recomputes from those bits, whether a
  // refusal was the declared refusal, whether the end state is the baseline the same run began from.
  //
  // The two are not the same question, and the difference is measurable rather than asserted: of the nineteen single-change
  // mutations R declares, thirteen leave N green and are refused by R. A harness that answered this role with N would report
  // green over a channel whose frame set does not reassemble.
  //
  // So this role runs N over all twenty-eight trials, R over every trial whose scene R declares, and R's own mutation suite
  // over the three positive scenes -- the same three positives and thirty-nine negatives 147 executed, with the family word
  // substitution and the single-channel exception the mutations are written for.
  roles.push({
    role: 'guest_resource_semantics_full',
    decides: 'the twenty scenes and twenty-eight trials are judged by the numeric judge AND the resource-semantics consumer '
      + 'over the same packaged channels, and the resource consumer refuses every one of the thirty-nine single-change copies '
      + 'of those channels on the obligation the change was aimed at',
    consumer: ENGINE + '/n-numeric-147.mjs',
  consumer_files: [ENGINE + '/n-numeric-147.mjs', ENGINE + '/references/resource-semantics-147.mjs'],
    entry: 'judgeMatrix(matrix, {}) and judgeResourceSemantics(scene, {e9, serial}) and MUTATIONS',
    subject_of: () => PKG + '/control/g-matrix-gm147-01-147.json',
    async run() {
      const n = await mod(ENGINE + '/n-numeric-147.mjs');
      const rj = await mod(ENGINE + '/references/resource-semantics-147.mjs');
      const matrix = readJson(PKG + '/control/g-matrix-gm147-01-147.json');
      const guestDir = PKG + '/material/guest';
      const trials = matrix.trials.map((t) => ({
        ...t,
        e9_text: fs.readFileSync(guestDir + '/' + t.stage + '.e9.log', 'utf8'),
        serial_text: fs.readFileSync(guestDir + '/' + t.stage + '.serial.log', 'utf8'),
      }));
      const numeric = n.judgeMatrix({ ...matrix, trials }, {});

      // R over every trial whose scene R declares obligations for. A trial of a scene R does not declare is not skipped
      // silently: it is counted, so the coverage is visible.
      const declared = new Set([...Object.keys(rj.RESOURCE_SCENES), ...Object.keys(rj.RESOURCE_REFUSALS)]);
      const covered = [];
      for (const t of trials) {
        if (!declared.has(t.scene)) continue;
        const v = rj.judgeResourceSemantics(t.scene, { e9: t.e9_text, serial: t.serial_text });
        covered.push({ stage: t.stage, scene: t.scene, accepted: v.accepted === true, checked: v.counts.checked,
          first_refusal: v.first_refusal });
      }

      // R's own mutation suite, over the three positive scenes, with the substitutions the mutations are written for: the
      // family word each scene prints, and -- for the one mutation that is about the two channels differing -- the untouched
      // serial channel, because a mutation that changed both channels could not test whether the pair is compared at all.
      const WORD = { 'OBJECT DIAG': 'OBJECT', 'RECOVERY DIAG': 'RECOVERY', 'FRAMEREC DIAG': 'FRAMEREC' };
      const OTHER = { OBJECT: 'RECOVERY', RECOVERY: 'FRAMEREC', FRAMEREC: 'OBJECT' };
      const cases = [];
      const skipped = [];
      const problems = [];
      for (const sceneName of Object.keys(rj.RESOURCE_SCENES)) {
        const trial = trials.find((t) => t.scene === sceneName && t.ok === true);
        if (!trial) { problems.push({ why: 'no passing trial of this scene in the matrix', scene: sceneName }); continue; }
        const word = WORD[rj.RESOURCE_SCENES[sceneName].family];
        const positive = rj.judgeResourceSemantics(sceneName, { e9: trial.e9_text, serial: trial.serial_text });
        cases.push({ scene: sceneName, side: 'positive', mutation: null, accepted: positive.accepted === true,
          obligations_checked: positive.counts.checked, first_refusal: positive.first_refusal });
        if (positive.accepted !== true) {
          problems.push({ why: 'the untouched copy of a passing trial was refused', scene: sceneName,
            first_refusal: positive.first_refusal });
        }
        for (const m of rj.MUTATIONS) {
          if (Array.isArray(m.scenes) && !m.scenes.includes(sceneName)) {
            skipped.push({ scene: sceneName, mutation: m.id, why: 'the mutation declares the scenes it applies to and this is not one' });
            continue;
          }
          const mutated = m.apply(trial.e9_text).replace(/__FAMILY__/g, word).replace(/__OTHER__/g, OTHER[word]);
          if (mutated === trial.e9_text) {
            problems.push({ why: 'the mutation changed nothing, so it is not a negative', scene: sceneName, mutation: m.id });
            continue;
          }
          const serial = m.id === 'other-channel-differs' ? trial.serial_text : mutated;
          const v = rj.judgeResourceSemantics(sceneName, { e9: mutated, serial });
          const hit = v.problems.some((p) => (m.targets === 'end==baseline'
            ? /_equals_baseline$/.test(p.obligation) : p.obligation === m.targets));
          cases.push({ scene: sceneName, side: 'negative', mutation: m.id, targets: m.targets, accepted: v.accepted === true,
            right_reason: hit, refused_by: v.first_refusal });
          if (v.accepted === true) problems.push({ why: 'a mutation was accepted', scene: sceneName, mutation: m.id });
          else if (!hit) {
            problems.push({ why: 'a mutation was refused, but not by the obligation it was aimed at', scene: sceneName,
              mutation: m.id, aimed_at: m.targets, refused_by: v.first_refusal });
          }
        }
      }
      const positives = cases.filter((c) => c.side === 'positive');
      const negatives = cases.filter((c) => c.side === 'negative');
      return {
        numeric, covered, covered_total: trials.filter((t) => declared.has(t.scene)).length,
        declared_scenes: declared.size, positive_scenes: Object.keys(rj.RESOURCE_SCENES).length,
        refusal_scenes: Object.keys(rj.RESOURCE_REFUSALS).length,
        resource: {
          cases, skipped, problems,
          positives: positives.length, positives_accepted: positives.filter((c) => c.accepted).length,
          negatives: negatives.length, negatives_refused: negatives.filter((c) => !c.accepted).length,
          negatives_refused_for_the_aimed_obligation: negatives.filter((c) => c.right_reason).length,
          accepted_by_mistake: negatives.filter((c) => c.accepted).length,
        },
        consumer_versions: { numeric: sha(fs.readFileSync(ENGINE + '/n-numeric-147.mjs')),
          resource: sha(fs.readFileSync(ENGINE + '/references/resource-semantics-147.mjs')) },
      };
    },
    verdict: (r) => ({
      ok: r.numeric.ok === true && r.covered_total === 12 && r.covered.every((c) => c.accepted)
        && r.resource.positives === 3 && r.resource.positives_accepted === 3
        && r.resource.negatives === 39 && r.resource.negatives_refused === 39
        && r.resource.negatives_refused_for_the_aimed_obligation === 39 && r.resource.problems.length === 0,
      decided: r.numeric.judged + r.covered_total + r.resource.positives + r.resource.negatives,
      met: r.numeric.passed + r.covered.filter((c) => c.accepted).length + r.resource.positives_accepted
        + r.resource.negatives_refused,
      detail: {
        numeric: { judged: r.numeric.judged, passed: r.numeric.passed, scenes: r.numeric.scenes,
          problems: (r.numeric.problems || []).slice(0, 4) },
        resource_over_trials: { covered: r.covered_total, accepted: r.covered.filter((c) => c.accepted).length,
          declared_scenes: r.declared_scenes, positive_scenes: r.positive_scenes, refusal_scenes: r.refusal_scenes,
          failing: r.covered.filter((c) => !c.accepted).slice(0, 6) },
        resource_mutations: { positives: r.resource.positives, positives_accepted: r.resource.positives_accepted,
          negatives: r.resource.negatives, negatives_refused: r.resource.negatives_refused,
          right_reason: r.resource.negatives_refused_for_the_aimed_obligation,
          accepted_by_mistake: r.resource.accepted_by_mistake, skipped: r.resource.skipped.length,
          problems: r.resource.problems.slice(0, 6) },
        consumer_versions: r.consumer_versions,
      },
    }),
    obligations: (r) => [
      { name: 'every-trial-was-judged-by-the-numeric-judge', value: r.numeric.judged === 28 },
      { name: 'every-trial-passed-the-numeric-judge', value: r.numeric.passed === 28 && r.numeric.judged === 28 },
      { name: 'every-declared-scene-was-judged', value: r.numeric.scenes === 20 },
      { name: 'the-numeric-judge-found-no-problem', value: (r.numeric.problems || []).length === 0 },
      { name: 'every-trial-of-a-declared-resource-scene-was-judged-by-R', value: r.covered_total === 12 },
      { name: 'every-one-of-those-trials-was-accepted-by-R', value: r.covered.every((c) => c.accepted === true) },
      { name: 'R-accepted-the-untouched-copy-of-each-positive-scene',
        value: r.resource.positives === 3 && r.resource.positives_accepted === 3 },
      { name: 'R-refused-all-thirty-nine-single-change-copies',
        value: r.resource.negatives === 39 && r.resource.negatives_refused === 39 },
      { name: 'every-refusal-landed-on-the-aimed-obligation',
        value: r.resource.negatives_refused_for_the_aimed_obligation === 39 && r.resource.accepted_by_mistake === 0 },
      { name: 'the-resource-suite-found-no-problem', value: r.resource.problems.length === 0 },
      { name: 'the-two-consumers-are-the-original-modules',
        value: /^[0-9a-f]{64}$/.test(String(r.consumer_versions && r.consumer_versions.numeric))
          && /^[0-9a-f]{64}$/.test(String(r.consumer_versions && r.consumer_versions.resource)) },
    ],
    negatives: [
      // THE SECOND INTEGRATION NEGATIVE: N is green and R refuses.
      //
      // The numeric judge checks the stops and the values; the resource consumer reassembles the frame set from the bits the
      // guest dumped and recomputes the deltas from those bits. R's `frameset-delta-lies` mutation changes one declared delta
      // and leaves every count and marker the numeric judge reads alone. The negative requires exactly that split: N accepts
      // the channel and R refuses it on the delta obligation. A harness that answered this role with N alone would report
      // green over a frame set that does not recompute.
      { name: 'the-numeric-judge-is-green-but-the-resource-consumer-refuses',
        why: 'one declared frame-set delta is changed in a positive trial\'s channel while every marker and count the numeric '
          + 'judge reads is kept, so N still accepts the trial and R refuses it because the delta does not recompute from the '
          + 'bits',
        damage: () => {
          const dir = casesRoot + '/guest_resource_semantics_full/n-green-r-red';
          fs.mkdirSync(dir, { recursive: true });
          const matrix = readJson(PKG + '/control/g-matrix-gm147-01-147.json');
          const guestDir = PKG + '/material/guest';
          const trials = matrix.trials.map((t) => ({
            ...t,
            e9_text: fs.readFileSync(guestDir + '/' + t.stage + '.e9.log', 'utf8'),
            serial_text: fs.readFileSync(guestDir + '/' + t.stage + '.serial.log', 'utf8'),
          }));
          // The victim is a trial of the frame-recovery scene, because that is the scene that dumps a frame set.
          const victim = trials.find((t) => t.scene === 'runtime-frame-recovery-smp1' && t.ok === true);
          fs.writeFileSync(dir + '/matrix.json', JSON.stringify({ ...matrix, trials }, null, 2) + '\n');
          return { dir, stage: victim.stage, scene: victim.scene, mutation: 'frameset-delta-lies' };
        },
        run: async (d) => {
          const n = await mod(ENGINE + '/n-numeric-147.mjs');
          const rj = await mod(ENGINE + '/references/resource-semantics-147.mjs');
          const damaged = readJson(d.dir + '/matrix.json');
          const trial = damaged.trials.find((t) => t.stage === d.stage);
          const m = rj.MUTATIONS.find((x) => x.id === d.mutation);
          // The mutation is applied with the family word of the frame scene, exactly as the suite applies it.
          const mutated = m.apply(trial.e9_text).replace(/__FAMILY__/g, 'FRAMEREC')
            .replace(/__OTHER__/g, 'OBJECT');
          // N sees the channel the mutation produced, in both ports, so nothing about the pair is what refuses it.
          const asTrials = damaged.trials.map((t) => (t.stage === d.stage
            ? { ...t, e9_text: mutated, serial_text: mutated } : t));
          const numeric = n.judgeMatrix({ ...damaged, trials: asTrials }, {});
          const resource = rj.judgeResourceSemantics(trial.scene, { e9: mutated, serial: mutated });
          return { numeric, resource, stage: d.stage, scene: d.scene, mutation: d.mutation };
        },
        verdict: (r) => !!(r.numeric && r.numeric.ok === true && r.resource && r.resource.accepted === true),
        expect: 'the numeric judge still accepts the trial while the resource consumer refuses it on the frame-set delta '
          + 'obligation, which is the difference between the two questions',
        hit: (r) => !!(r.numeric && r.numeric.ok === true && r.resource && r.resource.accepted === false
          && r.resource.problems.some((p) => p.obligation === 'frameset.deltas_recompute_from_bits')),
        summarise: (r) => ({ stage: r.stage, scene: r.scene, mutation: r.mutation,
          numeric_ok: r.numeric && r.numeric.ok, resource_accepted: r.resource && r.resource.accepted,
          resource_refused_by: r.resource && r.resource.first_refusal && r.resource.first_refusal.obligation }),
      },
      { name: 'a-trial-whose-channel-lost-its-marker',
        why: 'one trial\'s e9 text has a witness line removed, so the scene it claims to be is not the scene the channel shows',
        damage: () => {
          const dir = casesRoot + '/guest_resource_semantics_full/stripped-channel';
          fs.mkdirSync(dir, { recursive: true });
          const matrix = readJson(PKG + '/control/g-matrix-gm147-01-147.json');
          const guestDir = PKG + '/material/guest';
          const trials = matrix.trials.map((t) => ({
            ...t,
            e9_text: fs.readFileSync(guestDir + '/' + t.stage + '.e9.log', 'utf8'),
            serial_text: fs.readFileSync(guestDir + '/' + t.stage + '.serial.log', 'utf8'),
          }));
          const victim = trials.find((t) => /^WORKER DIAG/m.test(t.e9_text)) || trials[4];
          victim.e9_text = victim.e9_text.split('\n').filter((l) => !/^WORKER DIAG/.test(l)).join('\n');
          fs.writeFileSync(dir + '/matrix.json', JSON.stringify({ ...matrix, trials }, null, 2) + '\n');
          return { dir, stage: victim.stage, scene: victim.scene };
        },
        run: async (d) => {
          const m = await mod(ENGINE + '/n-numeric-147.mjs');
          const damaged = readJson(d.dir + '/matrix.json');
          const answer = m.judgeMatrix(damaged, {});
          answer.__damaged_stage = d.stage;
          answer.__damaged_scene = d.scene;
          return answer;
        },
        verdict: (r) => r.ok === true,
        expect: 'the trial whose witness line was removed is refused, and it is the trial that was damaged',
        hit: (r) => r.ok === false && r.problems.some((p) => {
          const text = JSON.stringify(p);
          return text.includes(r.__damaged_stage) || (r.__damaged_scene && text.includes(r.__damaged_scene));
        }),
      },
      { name: 'a-matrix-that-drops-a-trial',
        why: 'one trial is removed from the matrix, so the set no longer covers every declared scene the way the contract requires',
        damage: () => {
          const dir = casesRoot + '/guest_resource_semantics_full/dropped-trial';
          fs.mkdirSync(dir, { recursive: true });
          const matrix = readJson(PKG + '/control/g-matrix-gm147-01-147.json');
          const guestDir = PKG + '/material/guest';
          const trials = matrix.trials.map((t) => ({
            ...t,
            e9_text: fs.readFileSync(guestDir + '/' + t.stage + '.e9.log', 'utf8'),
            serial_text: fs.readFileSync(guestDir + '/' + t.stage + '.serial.log', 'utf8'),
          }));
          const kept = trials.filter((t, i) => i !== 2);
          fs.writeFileSync(dir + '/matrix.json', JSON.stringify({ ...matrix, trials: kept }, null, 2) + '\n');
          return dir;
        },
        run: async (dir) => {
          const m = await mod(ENGINE + '/n-numeric-147.mjs');
          return m.judgeMatrix(readJson(dir + '/matrix.json'), {});
        },
        verdict: (r) => r.ok === true,
        expect: 'the trial count is no longer the expected one, so the matrix is refused as incomplete',
        hit: (r) => r.ok === false && r.judged !== 28,
      },
    ],
  });
  // ---------------------------------------------------------------- 6. independent_physical_closure
  //
  // The business question: can every name the packet uses be resolved out of this tree and nowhere else? The resolver is the
  // packaged one, it has exactly one root, and its answer to a name that leaves the root is a refusal. The positives are the
  // required set resolved through it; the negatives are the five ways out.
  roles.push({
    role: 'independent_physical_closure',
    decides: 'the packaged resolver resolves every required member inside the package and refuses every name that leaves it, '
      + 'so the closure is physical and has no second root to fall back to',
    consumer: ENGINE + '/h-package-147.mjs',
    entry: 'makeResolver',
    subject_of: () => requiredSetInPackage().path,
    async run() {
      const m = await mod(ENGINE + '/h-package-147.mjs');
      const resolve = m.makeResolver(PKG);
      const rs = requiredSetInPackage().record;
      const resolved = [];
      const failed = [];
      for (const member of rs.required_physical_set.members) {
        try {
          const abs = resolve(member.in_package);
          const b = fs.readFileSync(abs);
          if (sha(b) === member.sha256 && b.length === member.bytes) resolved.push(member.in_package);
          else failed.push({ member: member.in_package, why: 'the bytes are not the bytes it was required as' });
        } catch (e) {
          failed.push({ member: member.in_package, why: e.message });
        }
      }
      const escapes = [];
      for (const name of ['../../etc/passwd', '/etc/passwd', 'C:/Windows/win.ini', 'material/../../../x',
        'engine/not-in-this-package.mjs']) {
        try { resolve(name); escapes.push({ name, outcome: 'RESOLVED' }); }
        catch (e) { escapes.push({ name, outcome: e instanceof m.NotInPackage ? 'refused' : 'refused-otherwise: ' + e.message }); }
      }
      return { resolved, failed, escapes, required: rs.required_physical_set.total };
    },
    verdict: (r) => ({
      ok: r.failed.length === 0 && r.resolved.length === r.required && r.escapes.every((e) => e.outcome === 'refused'),
      decided: r.required,
      met: r.resolved.length,
      detail: { failed: r.failed.slice(0, 4), escapes: r.escapes },
    }),
    obligations: (r) => [
      { name: 'every-required-member-resolves-inside-the-package', value: r.resolved.length === r.required },
      { name: 'every-resolved-member-is-the-bytes-it-was-required-as', value: r.failed.length === 0 },
      { name: 'a-name-that-leaves-the-root-is-refused', value: r.escapes.filter((e) => e.name !== 'engine/not-in-this-package.mjs')
        .every((e) => e.outcome === 'refused') },
      { name: 'a-name-that-is-not-in-the-package-is-refused', value: r.escapes
        .filter((e) => e.name === 'engine/not-in-this-package.mjs').every((e) => e.outcome === 'refused') },
    ],
    negatives: [
      { name: 'a-name-that-exists-only-in-the-worktree',
        why: 'a name that is real in the worktree and absent from the package is offered to the resolver; a resolver with a '
          + 'second root would find it',
        damage: () => ({ dir: null, name: 'tools/gtos-dsh-149/roles-149.mjs' }),
        run: async (d) => {
          const m = await mod(ENGINE + '/h-package-147.mjs');
          const resolve = m.makeResolver(PKG);
          try { resolve(d.name); return { outcome: 'RESOLVED' }; }
          catch (e) { return { outcome: e instanceof m.NotInPackage ? 'refused' : 'refused-otherwise: ' + e.message }; }
        },
        verdict: (r) => r.outcome === 'RESOLVED',
        expect: 'the resolver refuses, because the file exists in the worktree and the resolver does not look there',
        hit: (r) => r.outcome === 'refused',
      },
      { name: 'a-member-whose-bytes-drifted',
        why: 'one required member is replaced in a copy of the tree, so resolution succeeds and the hash does not',
        damage: () => {
          const dir = casesRoot + '/independent_physical_closure/drifted-member';
          fs.mkdirSync(dir, { recursive: true });
          const rs = requiredSetInPackage().record;
          const target = rs.required_physical_set.members.find((x) => x.role === 'image');
          fs.mkdirSync(dir + '/' + path.posix.dirname(target.in_package), { recursive: true });
          const b = fs.readFileSync(PKG + '/' + target.in_package);
          b[b.length - 1] = b[b.length - 1] ^ 0x01;
          fs.writeFileSync(dir + '/' + target.in_package, b);
          return { dir, target };
        },
        run: async (d) => {
          const m = await mod(ENGINE + '/h-package-147.mjs');
          const resolve = m.makeResolver(d.dir);
          const rs = requiredSetInPackage().record;
          const abs = resolve(d.target.in_package);
          const b = fs.readFileSync(abs);
          return { matches: sha(b) === d.target.sha256 && b.length === d.target.bytes, role: d.target.role };
        },
        verdict: (r) => r.matches === true,
        expect: 'the drifted image no longer matches the hash it was required as',
        hit: (r) => r.matches === false && r.role === 'image',
      },
    ],
  });

  // ---------------------------------------------------------------- 7. publication_and_related_tests
  //
  // The business question: does the frozen engine, given a spec and a context, take one transaction and then verify it? The
  // engine is a program, so it is run as a program -- through the driver, which reads its output through a declared schema
  // and refuses a record that is missing a field rather than ORing the fields it happens to find.
  roles.push({
    role: 'publication_and_related_tests',
    decides: 'the publication engine takes one whole transaction -- state, plan, archive, seal, verify -- over a fixture '
      + 'identity and its own verification record says the transaction held',
    consumer: TOOLS + '/publish-149.mjs',
    entry: 'verify (state|plan|archive|seal|verify, through drive-149.mjs)',
    subject_of: () => TOOLS + '/publish-149.mjs',
    // This is the one role whose business input is not package bytes: the engine is asked to take a transaction over a fixture
    // identity, because rehearsing a production transaction on the real root is what the packet forbids. Its subject is the
    // engine program, so the bucket that has to carry its reads is the consumer tree.
    subject_bucket: 'in_consumer',
    async run() {
      // The transaction builder is F's, not a second one written here. F already proved, over 32 cases, that this shape is a
      // transaction the engine accepts and that each of its parts is what the seal reads; re-deriving it would mean this role
      // tested a different transaction from the one the negatives test.
      const f = await mod(TOOLS + '/f-negative-149.mjs');
      const num = String(9148 + (Number(process.env.GTOS_H_ROLES_FIXTURE_OFFSET || 0)));
      const tx = f.buildTransaction(num);
      const drive = await mod(TOOLS + '/drive-149.mjs');
      const answer = drive.phase({ tag: 'h-' + tx.fx.tag, contextPath: tx.fx.contextPath, specPath: tx.specPath,
        planPath: tx.planPath, payloadPath: tx.payloadPath, receiptPath: tx.receiptPath, outPath: tx.verifyPath });
      answer.__tx = { fx: tx.fx, planPath: tx.planPath, payloadPath: tx.payloadPath, receiptPath: tx.receiptPath,
        specPath: tx.specPath };
      return answer;
    },
    verdict: (r) => ({
      ok: r.ok === true && r.accepted === true,
      decided: r.verify ? r.verify.obligation_count : 0,
      met: r.verify && r.verify.ok === true ? r.verify.obligation_count : 0,
      detail: { failed_checks: r.failed_checks, verify_ok: r.verify && r.verify.ok,
        obligation_count: r.verify && r.verify.obligation_count,
        seal_roles_consumed: r.seal && r.seal.mandatory_roles_consumed },
    }),
    obligations: (r) => [
      { name: 'the-transaction-was-accepted', value: r.accepted === true },
      { name: 'the-verification-record-says-ok', value: !!(r.verify && r.verify.ok === true) },
      { name: 'every-obligation-was-counted', value: !!(r.verify && r.verify.obligation_count > 0
        && r.verify.obligation_count === (r.verify.obligation_names || []).length) },
      { name: 'the-seal-consumed-every-role', value: !!(r.seal && r.plan
        && r.seal.mandatory_roles_consumed === r.plan.mandatory_roles_required) },
    ],
    negatives: [
      { name: 'a-second-archive-for-the-same-task',
        why: 'a task is taken through state, plan and archive and then asked to archive a second time, without a seal in '
          + 'between, so the only rule that can refuse is the one archive per task identity',
        damage: () => null,
        run: async () => {
          const f = await mod(TOOLS + '/f-negative-149.mjs');
          const drive = await mod(TOOLS + '/drive-149.mjs');
          const num = String(9248 + (Number(process.env.GTOS_H_ROLES_FIXTURE_OFFSET || 0)));
          const tx = f.buildTransaction(num);
          const statuses = {};
          for (const [verb, args] of [
            ['state', [tx.fx.contextPath]],
            ['plan', [tx.fx.contextPath, tx.specPath, tx.planPath]],
            ['archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]],
          ]) statuses[verb] = drive.runEngine(verb, args).status;
          const second = drive.step({ verb: 'archive',
            args: [tx.fx.contextPath, tx.planPath, tx.fx.destDir + '/payload-2.tar'],
            expect: 'already been archived' });
          return { statuses, first_archive_bytes: fs.statSync(tx.payloadPath).size, second };
        },
        verdict: (r) => r.statuses.state === 0 && r.statuses.plan === 0 && r.statuses.archive === 0
          && r.second.refused === true && r.second.names_the_expected_obligation === true,
        expect: 'the engine refuses the second archive and says the task has already been archived once',
        hit: (r) => r.second.refused === true && r.second.names_the_expected_obligation === true
          && /already been archived/.test(String(r.second.refusal)),
      },
      { name: 'a-seal-over-a-payload-that-is-not-the-plan',
        why: 'a transaction is archived, then a different file is offered to the seal; the seal must refuse rather than '
          + 'accept whatever it is handed',
        damage: () => null,
        run: async () => {
          const f = await mod(TOOLS + '/f-negative-149.mjs');
          const drive = await mod(TOOLS + '/drive-149.mjs');
          const num = String(9348 + (Number(process.env.GTOS_H_ROLES_FIXTURE_OFFSET || 0)));
          const tx = f.buildTransaction(num);
          const state = drive.runEngine('state', [tx.fx.contextPath]);
          const plan = drive.runEngine('plan', [tx.fx.contextPath, tx.specPath, tx.planPath]);
          const archive = drive.runEngine('archive', [tx.fx.contextPath, tx.planPath, tx.payloadPath]);
          const wrong = tx.fx.destDir + '/wrong.tar';
          fs.writeFileSync(wrong, 'this is not the payload that was archived\n');
          const seal = drive.step({ verb: 'seal',
            args: [tx.fx.contextPath, wrong, tx.receiptPath, tx.planPath], expect: 'not the plan|not the payload|refus' });
          return { statuses: { state: state.status, plan: plan.status, archive: archive.status }, seal };
        },
        verdict: (r) => r.seal.refused === true && r.seal.names_the_expected_obligation === true,
        expect: 'the seal refuses a payload that is not the plan it was given',
        hit: (r) => r.seal.refused === true && r.seal.names_the_expected_obligation === true,
      },
    ],
  });

  return roles;
};

// ---------------------------------------------------------------- the run
const runRole = async (spec) => {
  const dir = roleDir(spec.role);
  fs.mkdirSync(dir, { recursive: true });
  const consumerBytes = fs.readFileSync(spec.consumer);
  const consumerSha = sha(consumerBytes);
  // A role can be decided by more than one program -- the static gate is two gates over one judge, the resource role is the
  // numeric judge and the resource consumer -- so every file the role executes is pinned, not only the one named as its entry.
  // The entry is the file whose `main` the role calls; the rest are loaded by it and are just as much part of the decision.
  const consumerFiles = (spec.consumer_files || [spec.consumer]).map((f) => {
    const b = fs.readFileSync(f);
    return { path: rel(f), bytes: b.length, sha256: sha(b) };
  });
  const subjectPath = spec.subject_of();
  const subjectBytes = fs.readFileSync(subjectPath);
  const subject = sha(subjectBytes);

  // The roots are named rather than one, so the trace can say where each read went. The package is the payload; the consumer
  // trees are the program doing the deciding; the packet root carries the packet's own records, and the fixture trees live
  // under it, so they are covered by it rather than listed separately. None of these is a live artifact tree, and the bucket
  // for anything that is none of them is the one that has to be empty.
  //
  // Every root here is a fixed path. An earlier version listed the per-run fixture directory, which put a process id into
  // every role record and made the same package produce different record bytes on each run; a record that changes when
  // nothing changed is not evidence of anything.
  const roots = [
    ['in_package', PKG],
    ['in_consumer', TOOLS],
    ['in_consumer', TOOLS146],
    ['in_packet_records', R],
    // The designated WSL work root this packet created for its own guarded work. The role that runs its consumer inside the
    // guard has to read the report and the inner record the guard wrote, and they are here rather than in the package because
    // they are this run's own output, not the payload.
    ['in_work_root', LINUX_WORK_HOST],
  ];
  // The 147 tree is NOT one of the roots, and that is the point of this list. It is the tree the payload is supposed to
  // replace, so it is forbidden below; if it were also a read root a read from it would be classified `in_consumer` and would
  // never be counted against the role. Every consumer this harness executes is the copy the package carries under `engine/`,
  // so a read that reaches the 147 worktree is a read that has fallen back to a live tree, and it is counted as one.
  // The roots that must not be read at all. The 147 origin tree is always forbidden -- this packet preserved it, and a role
  // that reached back into it would be deciding from the tree the payload is supposed to replace. `GTOS_H_ROLES_FORBID` adds
  // more, which is how the post-seal receipt forbids the live report root when it re-runs this harness from the extraction.
  const forbiddenRoots = [
    'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a',
    '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-147-e759-scene-bound-view',
    ...String(process.env.GTOS_H_ROLES_FORBID || '').split(';').map((s) => s.trim()).filter(Boolean),
  ];
  // The one exemption, and it is declared by the caller that created the directory: a post-seal receipt re-runs this harness
  // from an extraction outside the report root and writes its own records under a directory it just made. Reads of that
  // directory are reads of this run's own output, not of a live tree, and they are classified `in_own_output` rather than
  // being folded into a bucket that would also cover the report root.
  const exemptRoots = String(process.env.GTOS_H_ROLES_EXEMPT || '').split(';').map((s) => s.trim()).filter(Boolean);
  const positive = await withTrace(() => spec.run(), roots, forbiddenRoots, exemptRoots);

  const v = positive.value ? spec.verdict(positive.value) : { ok: false, decided: 0, met: 0, detail: { error: String(positive.error && positive.error.message) } };
  const obligations = positive.value ? spec.obligations(positive.value) : [{ name: 'the-entry-returned', value: false }];

  // The negatives: one business fault each, and each has to be refused for the reason it names.
  const negatives = [];
  for (const neg of spec.negatives) {
    let result;
    try {
      const damaged = neg.damage();
      const base = positive.value && positive.value.__control ? positive.value.__control : null;
      const answer = await neg.run(damaged, base || undefined);
      result = { name: neg.name, why: neg.why, expect: neg.expect, verdict: neg.verdict(answer), hit: neg.hit(answer),
        ok: neg.hit(answer) === true,
        // The summary is the part of the answer the verdict was read from; the excerpt is kept beside it so a reader can check
        // that the summary is not a convenient selection. Both are scrubbed of the per-run fixture directory: the damage is
        // reproduced by a re-run, but the path it happened at is not part of what was decided.
        summary: neg.summarise ? neg.summarise(answer) : null,
        observed: scrub(JSON.stringify(answer)).slice(0, 1200),
        observed_bytes: JSON.stringify(answer).length };
    } catch (e) {
      result = { name: neg.name, why: neg.why, expect: neg.expect, ok: false, error: String(e.message).slice(0, 600) };
    }
    negatives.push(result);
  }

  const reads = positive.trace;
  // THE IO ALLOWLIST: every path the business call opened, with the declared source it belongs to.
  //
  // The buckets above describe; this decides. Each distinct path is checked against the sources this run declared, and a path
  // that belongs to none of them, or that belongs to a forbidden root, makes the role fail -- there is no reading of
  // `elsewhere === 0` over a list nobody looked at. The paths themselves travel in the record, so a reader can check the
  // classification rather than the count.
  const io = {
    what: 'every distinct path the business entry opened while it ran, with the source it was classified to and whether that '
      + 'source was forbidden; a path in no declared source, or in a forbidden one, fails the role',
    declared_sources: reads.roots.map((r) => ({ bucket: r.name, root: r.root })),
    exempt_sources: reads.exempt,
    forbidden_sources: reads.forbidden,
    measured: reads.measured,
    distinct_paths: reads.distinct_paths,
    by_bucket: { in_package: reads.in_package, in_consumer: reads.in_consumer, in_packet_records: reads.in_packet_records,
      in_work_root: reads.in_work_root, in_own_output: reads.in_own_output, elsewhere: reads.elsewhere },
    unclassified: reads.paths.filter((p) => p.bucket === 'elsewhere').map((p) => p.path),
    forbidden: reads.paths.filter((p) => p.forbidden).map((p) => p.path),
    paths: reads.paths,
    ok: reads.measured === true && reads.elsewhere === 0 && reads.forbidden_reads === 0,
    ok_means: 'the trace measured at least one read, and every distinct path it saw belongs to a source this run declared and '
      + 'none of them is under a forbidden root',
  };
  const record = {
    task: 'GTOS-DSH-149',
    role: spec.role,
    decides: spec.decides,
    // Which bucket of the trace has to carry this role's reads: the package for the six whose business input is payload
    // bytes, the consumer tree for the one whose input is a fixture by design.
    subject_bucket: spec.subject_bucket || 'in_package',
    subject,
    subject_path: rel(subjectPath),
    consumer_version: consumerSha,
    entry: spec.entry,
    business_called: positive.value !== null && positive.error === null,
    consumer: { path: rel(spec.consumer), bytes: consumerBytes.length, sha256: consumerSha, entry: spec.entry,
      files: consumerFiles },
    input: [
      { path: rel(subjectPath), bytes: subjectBytes.length, sha256: subject, what: 'the record or artefact this role\'s '
        + 'answer is about' },
    ],
    output: { path: rel(dir + '/' + spec.role + '.json'), ok_field: 'ok' },
    business: { called: positive.value !== null && positive.error === null, verdict: v.ok,
      obligations_decided: obligations.length, obligations_met: obligations.filter((o) => o.value === true).length,
      detail: v.detail },
    obligations,
    reads: { total: positive.trace.total, in_package: positive.trace.in_package,
      in_consumer: positive.trace.in_consumer, in_packet_records: positive.trace.in_packet_records,
      in_work_root: positive.trace.in_work_root,
      elsewhere: positive.trace.elsewhere, outside_paths: positive.trace.outside_paths,
      kind: positive.trace.kind, roots: positive.trace.roots,
      forbidden: positive.trace.forbidden, forbidden_reads: positive.trace.forbidden_reads,
      forbidden_paths: positive.trace.forbidden_paths, exempt: positive.trace.exempt,
      in_own_output: positive.trace.in_own_output, measured: positive.trace.measured,
      no_live_fallback: positive.trace.no_live_fallback },
    io,
    negatives,
    negatives_passed: negatives.filter((n) => n.ok).length,
    negatives_run: negatives.length,
    ok: v.ok === true && obligations.every((o) => o.value === true) && negatives.every((n) => n.ok)
      && positive.trace.no_live_fallback === true && positive.trace.total > 0 && io.ok === true,
    ok_means: 'the consumer\'s own business entry was called with package bytes and answered true, every obligation it '
      + 'decided is true, every negative was refused for the reason the damage names, the trace around the call saw real '
      + 'reads, and every distinct path it saw belongs to a declared source that is not a live artifact tree',
  };
  fs.writeFileSync(dir + '/' + spec.role + '.json', JSON.stringify(record, null, 2) + '\n');
  const back = fs.readFileSync(dir + '/' + spec.role + '.json');
  return { record, index: { role: spec.role, path: dir + '/' + spec.role + '.json', bytes: back.length,
    sha256: sha(back), ok_field: 'ok', subject, consumer_version: consumerSha, entry: spec.entry,
    obligations_count: obligations.length } };
};

const main = async () => {
  const rotatedOut = rotateOut();
  const rotated = rotateTestOnly();
  fs.mkdirSync(OUT, { recursive: true });
  fs.mkdirSync(casesRoot, { recursive: true });
  const roles = stage();
  const index = [];
  const records = [];
  const blocked = [];
  for (const spec of roles) {
    try {
      const { record, index: row } = await runRole(spec);
      index.push(row);
      records.push({ role: record.role, ok: record.ok, business: record.business, negatives: record.negatives_run,
        negatives_passed: record.negatives_passed, subject_bucket: record.subject_bucket, reads: record.reads,
        io: { distinct_paths: record.io.distinct_paths, by_bucket: record.io.by_bucket, ok: record.io.ok,
          unclassified: record.io.unclassified.length, forbidden: record.io.forbidden.length } });
      if (!record.ok) blocked.push({ role: record.role, why: 'the role did not hold',
        obligations_not_met: record.obligations.filter((o) => o.value !== true).map((o) => o.name),
        negatives_failed: record.negatives.filter((n) => !n.ok).map((n) => n.name) });
    } catch (e) {
      blocked.push({ role: spec.role, why: 'the harness could not run this role: ' + String(e.message).slice(0, 400) });
      records.push({ role: spec.role, ok: false, error: String(e.message).slice(0, 400) });
    }
  }
  const green = index.filter((r) => readJson(r.path).ok === true).length;
  const summary = {
    task: 'GTOS-DSH-149',
    kind: 'h_roles_summary',
    what: 'the seven mandatory consumer roles, each executed through its own business entry against the package, with one '
      + 'business negative per role and a trace around the call',
    package_root: PKG,
    out_root: OUT,
    // Whether a previous fixture tree was moved aside before this run. The path it was moved to is not recorded, because it
    // carries the run's ordinal and would make this file differ between two runs over the same package.
    previous_fixture_tree_rotated: rotated !== null,
    previous_output_tree_rotated: rotatedOut !== null,
    roles_required: roles.length,
    executed: records.filter((r) => r.ok !== undefined).length,
    green,
    blocked,
    negatives: { cases: records.reduce((n, r) => n + (r.negatives || 0), 0),
      passed: records.reduce((n, r) => n + (r.negatives_passed || 0), 0) },
    // The three stability cases the taskbook asks for, each decided rather than described.
    stability: {
      // 1. No live fallback. Every role's trace saw at least one read, and none of the reads it saw came from a live artifact
      // tree. `elsewhere` is the only bucket that could hide one, so it is the number that has to be zero.
      no_live_fallback: {
        value: records.every((r) => r.reads && r.reads.total > 0 && r.reads.elsewhere === 0
          && r.io && r.io.ok === true && r.io.unclassified === 0 && r.io.forbidden === 0),
        per_role: records.map((r) => ({ role: r.role, total: r.reads && r.reads.total,
          in_package: r.reads && r.reads.in_package, elsewhere: r.reads && r.reads.elsewhere,
          forbidden_reads: r.reads && r.reads.forbidden_reads,
          distinct_paths: r.io && r.io.distinct_paths, unclassified: r.io && r.io.unclassified,
          io_ok: r.io && r.io.ok })),
        means: 'a role that quietly read the 147 origin tree or the live staged runtime would show a non-zero count in '
          + '`elsewhere` or in `forbidden_reads`; the second is the one the post-seal receipt turns on for the report root. '
          + 'The counts are backed by the per-path allowlist each role carries: every distinct path, the declared source it '
          + 'belongs to, and the ones that belong to none',
      },
      // 2. The trace wraps real execution. It has to be non-empty -- a trace of zero reads is a wrapper that measured nothing
      // -- and it has to have seen the bucket that carries this role's subject: the package for the six roles whose business
      // input is payload bytes, the consumer tree for the one whose input is a fixture by design.
      trace_wraps_real_execution: {
        value: records.every((r) => r.reads && r.reads.total > 0
          && r.reads[r.subject_bucket] > 0),
        per_role: records.map((r) => ({ role: r.role, bucket: r.subject_bucket,
          total: r.reads && r.reads.total, in_bucket: r.reads && r.reads[r.subject_bucket] })),
        reads_total: records.reduce((n, r) => n + ((r.reads && r.reads.total) || 0), 0),
        reads_in_package: records.reduce((n, r) => n + ((r.reads && r.reads.in_package) || 0), 0),
        means: 'the wrapper is installed across the entry\'s await points, not only up to its first one, and the reads it '
          + 'saw are the reads the decision made',
      },
      // 3. The inventory is not a substitute for the business verdict. A hash comparison can say a file is present; it
      // cannot say the consumer accepted it. So no role is green unless its own entry was called and decided obligations.
      inventory_is_not_a_substitute: {
        value: records.every((r) => r.business && r.business.called === true && r.business.obligations_decided > 0),
        per_role: records.map((r) => ({ role: r.role, called: r.business && r.business.called,
          decided: r.business && r.business.obligations_decided,
          met: r.business && r.business.obligations_met })),
        means: 'every green role carries the obligations its own consumer decided, and a role that could not be called is in '
          + 'blocked instead of being counted as a file comparison',
      },
    },
    records,
    ok: blocked.length === 0 && green === roles.length
      && records.every((r) => r.reads && r.reads.total > 0 && r.reads.elsewhere === 0),
    ok_means: 'all seven roles executed their real business entry against the package, answered true, decided their '
      + 'obligations and refused every business negative for the reason the damage names; the trace around each call saw real '
      + 'reads, all of them inside the package, the consumer tree or the packet records, and none from a live tree',
  };
  fs.writeFileSync(OUT + '/index.json', JSON.stringify({ task: 'GTOS-DSH-149', kind: 'h_roles_index',
    roles: index, ok: green === roles.length }, null, 2) + '\n');
  fs.writeFileSync(OUT + '/summary.json', JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ summary: OUT + '/summary.json', ok: summary.ok, green, executed: summary.executed,
    negatives: summary.negatives, blocked }, null, 2));
  process.exit(summary.ok ? 0 : 1);
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) main().catch((e) => { console.error(JSON.stringify({ fatal: String(e.stack || e.message).slice(0, 2000) })); process.exit(2); });
