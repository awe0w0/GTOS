// GTOS-DSH-148 H: the seven mandatory consumer roles, executed against the package.
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

const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const PKG = (process.env.GTOS_H_ROLES_PKG || (R + '/package-148')).replace(/\\/g, '/');
const OUT = (process.env.GTOS_H_ROLES_OUT || (R + '/h-roles-148')).replace(/\\/g, '/');
const TOOLS = R + '/worktree-148/tools/gtos-dsh-148';
const TOOLS147 = R + '/worktree-148/tools/gtos-dsh-147';
const TOOLS146 = R + '/worktree-148/tools/gtos-dsh-146';
// The Linux side of the package, for the one consumer that runs inside the guard.
const PKG_LINUX = '/mnt/f' + PKG.replace(/^[A-Za-z]:/, '');
const LINUX_WORK = '/var/tmp/gtos-dsh-148-e759-consumers';
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
const TESTONLY = R + '/test-only/h-roles';
const rotateTestOnly = () => {
  if (!exists(TESTONLY)) return null;
  let n = 1;
  while (exists(R + '/test-only/h-roles-attempt-' + String(n).padStart(2, '0'))) n += 1;
  const to = R + '/test-only/h-roles-attempt-' + String(n).padStart(2, '0');
  fs.renameSync(TESTONLY, to);
  return to;
};
process.env.GTOS_FIXTURE_RUN = 'h-roles';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
// Fixture directories carry the process id so a re-run never collides with an earlier one. The id is not part of what a
// negative decided, so it is folded out of anything that is written down, and two runs over the same package produce the same
// record bytes.
const scrub = (s) => String(s)
  .replace(new RegExp(R.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '/test-only/[^"\\s,]*', 'g'), 'test-only/<run>/...')
  .replace(/h-roles-\d+/g, 'h-roles-<run>');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
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
const withTrace = async (fn, roots) => {
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
  const buckets = { in_package: 0, in_consumer: 0, in_packet_records: 0, in_work_root: 0, elsewhere: 0 };
  const outside = [];
  for (const r of reads) {
    const w = where(r.path);
    buckets[w] += 1;
    if (w === 'elsewhere') outside.push(r.path);
  }
  return { value, error, trace: {
    total: reads.length,
    ...buckets,
    outside_paths: [...new Set(outside)].slice(0, 16),
    roots: roots.map(([name, root]) => ({ name, root })),
    // The stability fact. `elsewhere` is the only bucket that could hide a live fallback, and it is the one that has to be
    // empty -- not `total - in_package`, which would count the consumer's own source file as a fallback.
    no_live_fallback: buckets.elsewhere === 0,
    kind: 'installed around the entry call across its await points: every fs call the business decision made while it ran',
  } };
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
  // The business question: does the closure's required set resolve, member by member, out of this package? The entry is the
  // derivation itself, which is a real program with a real answer, and its closure block is what the seal consumes.
  roles.push({
    role: 'prerequisite_full',
    decides: 'every physical member the closure derivation requires is present in the package and is the bytes it was '
      + 'required as, so the required set is a closure and not a wish',
    consumer: TOOLS + '/spec-148.mjs',
    entry: 'buildRequiredSet',
    subject_of: () => PKG + '/control/required-set-rs147-01-147.json',
    async run() {
      const m = await mod(TOOLS + '/spec-148.mjs');
      return m.buildRequiredSet({ root: PKG });
    },
    verdict: (r) => ({
      ok: r.ok === true,
      decided: r.required_physical_set.total,
      met: r.closure.members,
      detail: { required_not_read: r.closure.required_not_read, by_role: r.required_physical_set.by_role },
    }),
    obligations: (r) => [
      { name: 'every-required-member-resolves', value: r.closure.required_not_read === 0 },
      { name: 'the-set-is-not-empty', value: r.required_physical_set.total > 0 },
      { name: 'the-closure-was-derived-before-the-spec', value: r.closure.independent_of_spec === true },
      { name: 'the-closure-does-not-read-the-payload', value: r.closure.independent_of_payload === true },
    ],
    negatives: [
      { name: 'a-package-that-cannot-produce-the-required-members',
        why: 'the derivation is pointed at a root that carries the control records and none of the material, so the members it '
          + 'requires cannot be resolved',
        damage: () => {
          const dir = casesRoot + '/prerequisite_full/missing-material';
          fs.mkdirSync(dir + '/control', { recursive: true });
          for (const n of fs.readdirSync(PKG + '/control')) {
            fs.copyFileSync(PKG + '/control/' + n, dir + '/control/' + n);
          }
          return dir;
        },
        run: async (dir) => (await mod(TOOLS + '/spec-148.mjs')).buildRequiredSet({ root: dir }),
        verdict: (r) => r.ok === true,
        expect: 'required_not_read is greater than zero, so the set is not a closure',
        hit: (r) => r.closure.required_not_read > 0 && r.ok === false,
      },
      { name: 'one-required-member-with-the-wrong-bytes',
        why: 'one required member is replaced by a file of the same name and different bytes, which a presence check cannot '
          + 'see and a hash taken at resolution time must',
        damage: () => {
          const dir = casesRoot + '/prerequisite_full/drifted-member';
          fs.mkdirSync(dir + '/control', { recursive: true });
          for (const n of fs.readdirSync(PKG + '/control')) fs.copyFileSync(PKG + '/control/' + n, dir + '/control/' + n);
          const origin = readJson(PKG + '/control/required-set-rs147-01-147.json');
          const first = origin.required_physical_set.members.find((m) => m.role === 'producer_object');
          const src = PKG + '/material/producers/' + path.posix.basename(first.name);
          fs.mkdirSync(dir + '/material/producers', { recursive: true });
          const b = fs.readFileSync(src);
          b[b.length - 1] = b[b.length - 1] ^ 0x01;
          fs.writeFileSync(dir + '/material/producers/' + path.posix.basename(first.name), b);
          return { dir, member: first.name, bytes_before: b.length };
        },
        run: async (d) => (await mod(TOOLS + '/spec-148.mjs')).buildRequiredSet({ root: d.dir }),
        verdict: (r) => r.ok === true,
        expect: 'the drifted member is the only one the root can produce, so the rest of the set is unread',
        hit: (r) => r.closure.required_not_read > 0 && r.ok === false,
      },
    ],
  });

  // ---------------------------------------------------------------- 2. producer_and_native_chain_full
  roles.push({
    role: 'producer_and_native_chain_full',
    decides: 'every layer of the guarded chain for each stage the packet ran is present and agrees with its own records, '
      + 'with the three exit layers equal, and the preserved failures are still failures',
    consumer: TOOLS + '/chain-148.mjs',
    entry: 'judgeAll',
    subject_of: () => PKG + '/control/c-chain-147ca.json',
    async run() {
      const m = await mod(TOOLS + '/chain-148.mjs');
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
        run: async (dir) => (async () => { const m = await mod(TOOLS + '/chain-148.mjs'); return m.judgeAll(dir, m.contractOf(dir), m.derive(dir)); })(),
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
        run: async (dir) => (async () => { const m = await mod(TOOLS + '/chain-148.mjs'); return m.judgeAll(dir, m.contractOf(dir), m.derive(dir)); })(),
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
    consumer: TOOLS147 + '/host-consume-147.mjs',
    entry: 'consumeHostChain',
    subject_of: () => PKG + '/control/h-consumption-147e.json',
    async run() {
      const m = await mod(TOOLS147 + '/host-consume-147.mjs');
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
        task: 'GTOS-DSH-148',
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
        run: async (dir, base) => (await mod(TOOLS147 + '/host-consume-147.mjs'))
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
        run: async (d, base) => (await mod(TOOLS147 + '/host-consume-147.mjs')).consumeHostChain({
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
  // The business question: is the packaged image the image the contract describes, and does the same gate refuse four
  // single-defect copies of it? This is the one consumer that runs inside the guard, so its reads are not visible to a
  // Windows-side trace; what is checked instead is the control it was handed (every path in it is in the package) and the
  // report it wrote (the hashes it names are the package's bytes).
  roles.push({
    role: 'static_byte_gate_full',
    decides: 'the image bytes and the map text are judged by the same Python gate the guard runs as its child, and four '
      + 'single-defect copies of the image are refused by that same gate',
    consumer: TOOLS147 + '/static-gate-147.py',
    entry: 'gate (as the guard child, argv[1] = control json)',
    subject_of: () => PKG + '/material/image/p147-default.elf',
    async run() {
      const selection = readJson(PKG + '/control/p-selection-147-a.json');
      const catalog = readJson(R + '/build-material-catalog-148.json');
      const workDir = LINUX_WORK + '/tmp/gate-ps148-h';
      const reportPath = workDir + '/gate-report.json';
      fs.mkdirSync(LINUX_WORK_HOST + '/tmp/gate-ps148-h', { recursive: true });
      const control = {
        task: 'GTOS-DSH-148',
        stage: 'ps148-h01',
        image: PKG_LINUX + '/material/image/p147-default.elf',
        map: PKG_LINUX + '/material/image/p147-default.map',
        negatives_root: workDir,
        report_path: reportPath,
        expected: {
          load_address: 1048576,
          entry_symbol: 'loader',
          object_order: selection.slots.map((s) => s.basename),
          initializers: catalog.baseline_expected_initializers,
        },
        note: 'the packaged image and map, judged by the gate the guard runs; the four negatives are built beside it',
      };
      const controlHost = LINUX_WORK_HOST + '/inputs-ps148-h01.json';
      fs.writeFileSync(controlHost, JSON.stringify(control, null, 2) + '\n');
      for (const stale of [LINUX_WORK_HOST + '/tmp/gate-ps148-h/gate-report.json']) {
        if (exists(stale)) fs.unlinkSync(stale);
      }
      const argv = ['--distribution', 'Ubuntu-20.04', '--exec', '/usr/bin/python3.8', '-B', '-S', GUARD_LINUX,
        '--stage', 'ps148-h01', '--purpose', 'python for GTOS-DSH-148 static byte gate',
        '--work-root', LINUX_WORK, '--cwd', LINUX_WORK, '--log', LINUX_WORK + '/logs/runs-148.jsonl', '--limit', '900',
        '--exec', '/usr/bin/python3.8', '--exec', '/usr/lib/x86_64-linux-gnu/ld-2.31.so', '--',
        '/usr/bin/python3.8', '-B', '-S', '/mnt/f' + TOOLS147.replace(/^[A-Za-z]:/, '') + '/static-gate-147.py',
        LINUX_WORK + '/inputs-ps148-h01.json'];
      const run = spawnSync('C:/Windows/System32/wsl.exe', argv, { encoding: 'utf8', windowsHide: true,
        maxBuffer: 64 * 1024 * 1024 });
      const innerPath = LINUX_WORK_HOST + '/logs/inner-report-ps148-h01.json';
      const inner = exists(innerPath) ? readJson(innerPath) : null;
      const report = exists(LINUX_WORK_HOST + '/tmp/gate-ps148-h/gate-report.json')
        ? readJson(LINUX_WORK_HOST + '/tmp/gate-ps148-h/gate-report.json') : null;
      return {
        status: run.status, transport_exit: run.status,
        stdout: (run.stdout || '').slice(-1200), stderr: (run.stderr || '').slice(-1200),
        inner: inner ? { child_returncode: inner.child && inner.child.returncode,
          output_bytes: inner.child && inner.child.output_bytes,
          output_sha256: inner.child && inner.child.output_sha256,
          output_path: inner.child && inner.child.output_path, argv: inner.argv } : null,
        control, report,
        // The negative damages this control rather than building another one, so the two runs differ in exactly one field.
        __control: control,
      };
    },
    verdict: (r) => ({
      ok: !!(r.report && r.report.ok === true && r.inner && r.inner.child_returncode === 0 && r.transport_exit === 0),
      decided: r.report ? (r.report.negatives || []).length + 1 : 0,
      met: r.report && r.report.ok === true ? (r.report.negatives || []).filter((n) => n.refused).length + 1 : 0,
      detail: { accepted: r.report && r.report.accepted, negatives_ok: r.report && r.report.negatives_ok,
        negatives: r.report && (r.report.negatives || []).map((n) => ({ name: n.name, refused: n.refused })),
        image_sha256: r.report && r.report.image_sha256, exit_layers: { transport: r.transport_exit,
          child: r.inner && r.inner.child_returncode } },
    }),
    obligations: (r) => [
      { name: 'the-gate-accepted-the-image', value: !!(r.report && r.report.accepted === true) },
      { name: 'the-gate-refused-every-negative', value: !!(r.report && r.report.negatives_ok === true) },
      { name: 'the-guard-child-exited-zero', value: !!(r.inner && r.inner.child_returncode === 0) },
      { name: 'the-transport-exited-zero', value: r.transport_exit === 0 },
      { name: 'the-gate-read-the-package-image', value: !!(r.report
        && r.report.image === PKG_LINUX + '/material/image/p147-default.elf'
        && r.report.bytes === fs.statSync(PKG + '/material/image/p147-default.elf').size) },
    ],
    negatives: [
      { name: 'a-map-that-is-not-the-image-map',
        why: 'the gate is handed a map whose bytes differ from the packaged map, so the object order it reads cannot be the '
          + 'image\'s',
        damage: () => {
          const dir = casesRoot + '/static_byte_gate_full/wrong-map';
          fs.mkdirSync(dir, { recursive: true });
          const b = fs.readFileSync(PKG + '/material/image/p147-default.map');
          const cut = b.subarray(0, Math.floor(b.length / 2));
          fs.writeFileSync(dir + '/wrong.map', cut);
          return dir;
        },
        run: async (dir, base) => {
          const workDir = LINUX_WORK + '/tmp/gate-ps148-neg';
          fs.mkdirSync(LINUX_WORK_HOST + '/tmp/gate-ps148-neg', { recursive: true });
          for (const stale of ['gate-report.json']) {
            if (exists(LINUX_WORK_HOST + '/tmp/gate-ps148-neg/' + stale)) fs.unlinkSync(LINUX_WORK_HOST + '/tmp/gate-ps148-neg/' + stale);
          }
          // The map is staged where the guard can read it, inside the package's own report root.
          const stagedMap = R + '/test-only/gate-map-ps148-h01.map';
          fs.mkdirSync(path.dirname(stagedMap), { recursive: true });
          if (exists(stagedMap)) fs.unlinkSync(stagedMap);
          fs.copyFileSync(dir + '/wrong.map', stagedMap);
          const control = { ...base, stage: 'ps148-h02',
            map: '/mnt/f' + stagedMap.replace(/^[A-Za-z]:/, ''),
            negatives_root: workDir, report_path: workDir + '/gate-report.json' };
          const controlHost = LINUX_WORK_HOST + '/inputs-ps148-h02.json';
          fs.writeFileSync(controlHost, JSON.stringify(control, null, 2) + '\n');
          const argv = ['--distribution', 'Ubuntu-20.04', '--exec', '/usr/bin/python3.8', '-B', '-S', GUARD_LINUX,
            '--stage', 'ps148-h02', '--purpose', 'python for GTOS-DSH-148 static byte gate negative',
            '--work-root', LINUX_WORK, '--cwd', LINUX_WORK, '--log', LINUX_WORK + '/logs/runs-148.jsonl',
            '--limit', '900', '--exec', '/usr/bin/python3.8', '--exec', '/usr/lib/x86_64-linux-gnu/ld-2.31.so', '--',
            '/usr/bin/python3.8', '-B', '-S', '/mnt/f' + TOOLS147.replace(/^[A-Za-z]:/, '') + '/static-gate-147.py',
            LINUX_WORK + '/inputs-ps148-h02.json'];
          const run = spawnSync('C:/Windows/System32/wsl.exe', argv, { encoding: 'utf8', windowsHide: true,
            maxBuffer: 64 * 1024 * 1024 });
          const report = exists(LINUX_WORK_HOST + '/tmp/gate-ps148-neg/gate-report.json')
            ? readJson(LINUX_WORK_HOST + '/tmp/gate-ps148-neg/gate-report.json') : null;
          return { status: run.status, report, stderr: (run.stderr || '').slice(-600) };
        },
        verdict: (r) => !!(r.report && r.report.ok === true),
        expect: 'the gate refuses the image when the map it is given is not that image\'s map',
        hit: (r) => !!(r.report && r.report.ok === false && r.report.accepted === false),
      },
    ],
  });

  // ---------------------------------------------------------------- 5. guest_resource_semantics_full
  roles.push({
    role: 'guest_resource_semantics_full',
    decides: 'the twenty guest scenes and twenty-eight trials are judged against the contract by the numeric judge, reading '
      + 'the real e9 and serial channels the trials printed',
    consumer: TOOLS147 + '/n-numeric-147.mjs',
    entry: 'judgeMatrix',
    subject_of: () => PKG + '/control/g-matrix-gm147-01-147.json',
    async run() {
      const m = await mod(TOOLS147 + '/n-numeric-147.mjs');
      const matrix = readJson(PKG + '/control/g-matrix-gm147-01-147.json');
      const guestDir = PKG + '/material/guest';
      const trials = matrix.trials.map((t) => ({
        ...t,
        e9_text: fs.readFileSync(guestDir + '/' + t.stage + '.e9.log', 'utf8'),
        serial_text: fs.readFileSync(guestDir + '/' + t.stage + '.serial.log', 'utf8'),
      }));
      const answer = m.judgeMatrix({ ...matrix, trials }, {});
      answer.__matrix = matrix;
      answer.__trials = trials;
      return answer;
    },
    verdict: (r) => ({
      ok: r.ok === true,
      decided: r.judged,
      met: r.passed,
      detail: { scenes: r.scenes, trials: r.trials, problems: r.problems.slice(0, 4) },
    }),
    obligations: (r) => [
      { name: 'every-trial-was-judged', value: r.judged === 28 },
      { name: 'every-trial-passed', value: r.passed === 28 && r.judged === 28 },
      { name: 'every-declared-scene-was-judged', value: r.scenes === 20 },
      { name: 'no-problem-was-found', value: r.problems.length === 0 },
    ],
    negatives: [
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
          const m = await mod(TOOLS147 + '/n-numeric-147.mjs');
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
          const m = await mod(TOOLS147 + '/n-numeric-147.mjs');
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
    consumer: TOOLS147 + '/h-package-147.mjs',
    entry: 'makeResolver',
    subject_of: () => R + '/required-set-148-02.json',
    async run() {
      const m = await mod(TOOLS147 + '/h-package-147.mjs');
      const resolve = m.makeResolver(PKG);
      const rs = readJson(R + '/required-set-148-02.json');
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
        damage: () => ({ dir: null, name: 'tools/gtos-dsh-148/h-roles-148.mjs' }),
        run: async (d) => {
          const m = await mod(TOOLS147 + '/h-package-147.mjs');
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
          const rs = readJson(R + '/required-set-148-02.json');
          const target = rs.required_physical_set.members.find((x) => x.role === 'image');
          fs.mkdirSync(dir + '/' + path.posix.dirname(target.in_package), { recursive: true });
          const b = fs.readFileSync(PKG + '/' + target.in_package);
          b[b.length - 1] = b[b.length - 1] ^ 0x01;
          fs.writeFileSync(dir + '/' + target.in_package, b);
          return { dir, target };
        },
        run: async (d) => {
          const m = await mod(TOOLS147 + '/h-package-147.mjs');
          const resolve = m.makeResolver(d.dir);
          const rs = readJson(R + '/required-set-148-02.json');
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
    consumer: TOOLS + '/publish-148.mjs',
    entry: 'verify (state|plan|archive|seal|verify, through drive-148.mjs)',
    subject_of: () => TOOLS + '/publish-148.mjs',
    // This is the one role whose business input is not package bytes: the engine is asked to take a transaction over a fixture
    // identity, because rehearsing a production transaction on the real root is what the packet forbids. Its subject is the
    // engine program, so the bucket that has to carry its reads is the consumer tree.
    subject_bucket: 'in_consumer',
    async run() {
      // The transaction builder is F's, not a second one written here. F already proved, over 32 cases, that this shape is a
      // transaction the engine accepts and that each of its parts is what the seal reads; re-deriving it would mean this role
      // tested a different transaction from the one the negatives test.
      const f = await mod(TOOLS + '/f-negative-148.mjs');
      const num = String(9148 + (Number(process.env.GTOS_H_ROLES_FIXTURE_OFFSET || 0)));
      const tx = f.buildTransaction(num);
      const drive = await mod(TOOLS + '/drive-148.mjs');
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
          const f = await mod(TOOLS + '/f-negative-148.mjs');
          const drive = await mod(TOOLS + '/drive-148.mjs');
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
          const f = await mod(TOOLS + '/f-negative-148.mjs');
          const drive = await mod(TOOLS + '/drive-148.mjs');
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
    ['in_consumer', TOOLS147],
    ['in_consumer', TOOLS146],
    ['in_packet_records', R],
    // The designated WSL work root this packet created for its own guarded work. The role that runs its consumer inside the
    // guard has to read the report and the inner record the guard wrote, and they are here rather than in the package because
    // they are this run's own output, not the payload.
    ['in_work_root', LINUX_WORK_HOST],
  ];
  const positive = await withTrace(() => spec.run(), roots);

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
  const noLiveFallback = reads.total === null ? null : reads.outside_package === 0;
  const record = {
    task: 'GTOS-DSH-148',
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
    consumer: { path: rel(spec.consumer), bytes: consumerBytes.length, sha256: consumerSha, entry: spec.entry },
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
      no_live_fallback: positive.trace.no_live_fallback },
    negatives,
    negatives_passed: negatives.filter((n) => n.ok).length,
    negatives_run: negatives.length,
    ok: v.ok === true && obligations.every((o) => o.value === true) && negatives.every((n) => n.ok)
      && positive.trace.no_live_fallback === true && positive.trace.total > 0,
    ok_means: 'the consumer\'s own business entry was called with package bytes and answered true, every obligation it '
      + 'decided is true, every negative was refused for the reason the damage names, the trace around the call saw real '
      + 'reads, and none of them came from a live artifact tree',
  };
  fs.writeFileSync(dir + '/' + spec.role + '.json', JSON.stringify(record, null, 2) + '\n');
  const back = fs.readFileSync(dir + '/' + spec.role + '.json');
  return { record, index: { role: spec.role, path: dir + '/' + spec.role + '.json', bytes: back.length,
    sha256: sha(back), ok_field: 'ok', subject, consumer_version: consumerSha, entry: spec.entry,
    obligations_count: obligations.length } };
};

const main = async () => {
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
        negatives_passed: record.negatives_passed, subject_bucket: record.subject_bucket, reads: record.reads });
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
    task: 'GTOS-DSH-148',
    kind: 'h_roles_summary',
    what: 'the seven mandatory consumer roles, each executed through its own business entry against the package, with one '
      + 'business negative per role and a trace around the call',
    package_root: PKG,
    out_root: OUT,
    // Whether a previous fixture tree was moved aside before this run. The path it was moved to is not recorded, because it
    // carries the run's ordinal and would make this file differ between two runs over the same package.
    previous_fixture_tree_rotated: rotated !== null,
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
        value: records.every((r) => r.reads && r.reads.total > 0 && r.reads.elsewhere === 0),
        per_role: records.map((r) => ({ role: r.role, total: r.reads && r.reads.total,
          in_package: r.reads && r.reads.in_package, elsewhere: r.reads && r.reads.elsewhere })),
        means: 'a role that quietly read the 147 root or the live staged runtime would show a non-zero elsewhere count',
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
  fs.writeFileSync(OUT + '/index.json', JSON.stringify({ task: 'GTOS-DSH-148', kind: 'h_roles_index',
    roles: index, ok: green === roles.length }, null, 2) + '\n');
  fs.writeFileSync(OUT + '/summary.json', JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ summary: OUT + '/summary.json', ok: summary.ok, green, executed: summary.executed,
    negatives: summary.negatives, blocked }, null, 2));
  process.exit(summary.ok ? 0 : 1);
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) main().catch((e) => { console.error(JSON.stringify({ fatal: String(e.stack || e.message).slice(0, 2000) })); process.exit(2); });
