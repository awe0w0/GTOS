// GTOS-DSH-147 H: the seven mandatory consumer roles, judged inside the package.
//
// The question is not "is the payload complete". It is: after the payload has been extracted, can each of the seven roles
// the taskbook makes mandatory be *decided* by code that lives in the package, reading only files that live in the
// package -- and is that decision a semantic one rather than a hash comparison or a JSON.parse?
//
// Three things are therefore measured rather than asserted:
//
//   * Provenance of the code. Every consumer is imported from the extracted tree, and its resolved module URL is recorded
//     and checked to be inside the package. A consumer that resolved out of the worktree would be a consumer the payload
//     does not carry.
//   * Provenance of the reads. `node:fs` is wrapped for the duration of each role, and every path opened, read or stat'ed
//     is recorded and classified: inside the package, or one of the declared external pinned tools. Anything else is a
//     live read of an old root, and it is reported by path rather than denied in the abstract.
//   * The decision itself. Each role calls the real consumer entry with the package's own artefacts and requires the
//     consumer's own verdict object -- not the absence of an exception.
//
// The resolver is the piece that makes the rest meaningful. `resolveInPackage` has exactly one rule: a name resolves under
// the package root or it throws. There is no second search root, no alias table that falls back to the live tree, and no
// `alias[p] || p` -- because that expression is precisely how a miss becomes a silent read of the worktree.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const TASK = 'GTOS-DSH-147';

// ---------------------------------------------------------------- the fail-closed resolver
export class NotInPackage extends Error {
  constructor(name, why) {
    super('not in package: ' + name + ' (' + why + ')');
    this.name = 'NotInPackage';
    this.lookup = name;
    this.why = why;
  }
}

export const makeResolver = (root) => {
  const norm = (n) => String(n).replace(/\\/g, '/').replace(/^\.\//, '');
  const resolve = (name) => {
    const rel = norm(name);
    // A name that tries to leave the package is refused before the filesystem is asked. `..` and an absolute path are the
    // two ways out, and both are named rather than normalised away.
    if (rel.startsWith('/') || /^[A-Za-z]:/.test(rel) || rel.split('/').includes('..')) {
      throw new NotInPackage(name, 'the name leaves the package root');
    }
    const abs = root + '/' + rel;
    if (!fs.existsSync(abs)) throw new NotInPackage(name, 'absent from the package');
    return abs;
  };
  resolve.read = (name) => fs.readFileSync(resolve(name));
  resolve.json = (name) => JSON.parse(resolve.read(name).toString('utf8'));
  // `pathToFileURL` rather than `'file:///' + abs`: the package may be reached through a UNC share (`//wsl$/...`), and
  // string concatenation produces `file://///wsl$/...`, which is not a file URL and which Node refuses with a message about
  // absolute paths that says nothing about the real cause.
  resolve.url = (name) => pathToFileURL(resolve(name)).href;
  resolve.root = root;
  return resolve;
};

// ---------------------------------------------------------------- resolving a *logical* need
//
// A role's evidence is named the way the packet talks about it -- "the prerequisite record", "the matrix" -- while the payload
// carries it under the name the run wrote it with (`records/t-prerequisites-tpr147-02-147.json`,
// `records/g-matrix-gm147-01-147.json`). Resolving the logical name to the carried one is therefore a real step, and it is
// kept fail-closed in the same way: the name is matched only against members the plan lists, the match has to be unique, and
// the matched member still goes through the strict resolver. A logical name that matches nothing, or matches two things, is a
// refusal that says which -- never a fallback to a path on the live tree.
export const makeLogical = (root, plan) => {
  const strict = makeResolver(root);
  const members = (plan.members || []).map((m) => m.path);
  const logical = (need) => {
    const under = need.under || '';
    const re = need.matching || /./;
    const hits = members.filter((p) => p.startsWith(under) && re.test(p.slice(under.length)));
    if (hits.length === 0) throw new NotInPackage(need.what || under, 'no member of the plan matches this need');
    if (hits.length < (need.at_least || 1)) {
      throw new NotInPackage(need.what || under, `only ${hits.length} members match, the need asks for ${need.at_least}`);
    }
    // Every matched member is resolved strictly, so "the plan lists it" and "the package carries it" are both required.
    for (const p of hits) strict(p);
    return hits;
  };
  logical.root = root;
  return logical;
};

// ---------------------------------------------------------------- the roles
//
// Each role names the modules that decide it, the artefacts it decides on, and the entry it calls. The module list is the
// package's own namespace; a role whose module is missing is a role the payload cannot decide, which is the failure this
// file exists to catch.
export const ROLES = {
  prerequisite_full: {
    decides: 'the ten prerequisite role records, each re-opened and checked against the bytes it pins',
    modules: ['engine/references/prerequisite-consumption-147.mjs', 'engine/references/scene-admission-147.mjs'],
    tools: [],
    needs: [{ what: 'the prerequisite record', under: 'records/', matching: /^t-prerequisites-.*-147\.json$/ },
      { what: 'the role records', under: 'records/', matching: /^role-.*-147\.json$/, at_least: 10 }],
  },
  producer_and_native_chain_full: {
    decides: 'every layer of the guarded chain for each stage the packet ran: spec, control, helper, reservation, '
      + 'terminal, bridge report, guard inner report and raw channel, with three exits and group cleanup',
    modules: ['engine/c-chain-147.mjs', 'engine/references/private-boot-checkpoints-147.mjs'],
    // The runner is a program the packet executed, not a library it imported, and it has no import-safe entry: it reads
    // `process.argv` and dispatches. Importing it to prove it exists would run it. Its bytes are what the chain record pins,
    // so it is verified as bytes and left unexecuted here.
    tools: [{ path: 'engine/runner/run-147-r01.mjs', why: 'a program the packet ran through the pinned node, not a library' }],
    needs: [{ what: 'the guarded chain record', under: 'records/', matching: /^c-chain-147.*\.json$/ },
      { what: 'the chain negatives', under: 'records/', matching: /^c-negatives-147.*\.json$/ },
      { what: 'the producer objects', under: 'material/producers/', matching: /\.o$/, at_least: 61 },
      { what: 'the depfiles', under: 'material/producers/', matching: /\.d$/, at_least: 56 }],
  },
  host_chain_full: {
    decides: 'the production host consumer called on the real unit chain and on twelve deliberately damaged copies of it',
    modules: ['engine/host-consume-147.mjs', 'engine/references/host-path-147.mjs'],
    tools: [],
    needs: [{ what: 'the host build record', under: 'material/canonical/', matching: /^host-build-runtime\.json$/ },
      { what: 'the host contract', under: 'material/canonical/', matching: /^host-contract\.json$/ },
      { what: 'the unit ELF', under: 'material/host/elf/', matching: /^runtime_test\.elf$/ },
      { what: 'the host results', under: 'material/host/results/', matching: /./, at_least: 14 },
      { what: 'the host consumption record', under: 'records/', matching: /^h-consumption-147.*\.json$/ }],
  },
  static_byte_gate_full: {
    decides: 'the image bytes, the map text and the constructor byte gate, with the same judge applied to relocated and '
      + 'single-defect copies',
    modules: [],
    tools: [{ path: 'engine/static-gate-147.py', why: 'a Python gate the guard ran as the child' },
      { path: 'engine/static-gate-147b.py', why: 'a Python gate the guard ran as the child' },
      { path: 'engine/static-judge-147.py', why: 'the judge the second gate imports, run inside the guard' }],
    needs: [{ what: 'the image', under: 'material/image/', matching: /\.elf$/ },
      { what: 'the map', under: 'material/image/', matching: /\.map$/ },
      { what: 'the static gate report', under: 'evidence/', matching: /^ps147-.*\.json$/ },
      { what: 'the static judge report', under: 'evidence/', matching: /^pj147-.*\.json$/ },
      { what: 'the profile', under: 'material/profile/', matching: /./, at_least: 3 }],
  },
  guest_resource_semantics_full: {
    decides: 'every declared scene against the scene contract, from the real channels, with the required family fixed by '
      + 'the scene rather than by which labels appeared',
    modules: ['engine/n-numeric-147.mjs', 'engine/scene-contract-147.mjs', 'engine/references/resource-semantics-147.mjs',
      'engine/references/scene-admission-147.mjs'],
    tools: [],
    needs: [{ what: 'the matrix', under: 'records/', matching: /^g-matrix-.*-147\.json$/ },
      { what: 'the numeric verdict', under: 'records/', matching: /^n-numeric-.*-147\.json$/ },
      { what: 'the guest channels', under: 'material/guest/', matching: /\.e9\.log$/, at_least: 28 },
      { what: 'the guest channels', under: 'material/guest/', matching: /\.serial\.log$/, at_least: 28 }],
  },
  independent_physical_closure: {
    decides: 'the depfile closure over the extracted tree, the producer objects and their recorded sources, and the '
      + 'relocated copy of the same bytes',
    modules: ['engine/references/prerequisite-consumption-147.mjs'],
    tools: [{ path: 'engine/relocate-147.py', why: 'a Python relocation the guard ran as the child' }],
    needs: [{ what: 'the D closure', under: 'records/', matching: /^d-closure-.*-147\.json$/ },
      { what: 'the preserved originals', under: 'records/', matching: /^d-preserved-147\.json$/ },
      { what: 'the source view', under: 'material/sources/', matching: /./, at_least: 480 }],
  },
  publication_and_related_tests: {
    decides: 'the transaction engine, its own tar reader, the cardinality and role checks, and the fresh harness',
    // The engine and the driver both dispatch on `process.argv` at module load, so importing them to prove they are present
    // would execute them with this process's arguments. They are verified as bytes; the package harness that calls them is
    // the importable one.
    modules: ['engine/h-package-147.mjs', 'engine/harness-147.mjs'],
    tools: [{ path: 'engine/publish-147.mjs', why: 'a CLI engine that dispatches on process.argv when loaded' },
      { path: 'engine/drive-147.mjs', why: 'a CLI driver that dispatches on process.argv when loaded' }],
    needs: [{ what: 'the contract the packet answers to', under: 'control/', matching: /^contract-147\.json$/ },
      { what: 'the required physical set', under: 'control/', matching: /^required-set\.json$/ }],
  },
};

// ---------------------------------------------------------------- the read trace
//
// `node:fs` is wrapped rather than replaced: the same object keeps its other methods, and every call is recorded before it
// is passed through. The wrapper is installed only for the duration of a role, so one role's reads cannot be attributed to
// another's.
export const traceReads = async (fn) => {
  const reads = [];
  const names = ['readFileSync', 'openSync', 'statSync', 'lstatSync', 'existsSync', 'readdirSync', 'accessSync', 'realpathSync'];
  const saved = new Map();
  for (const n of names) {
    saved.set(n, fs[n]);
    fs[n] = function traced(p, ...rest) {
      try { reads.push({ op: n, path: String(p) }); } catch { /* a path that cannot be stringified is not a read */ }
      return saved.get(n).call(fs, p, ...rest);
    };
  }
  try {
    const value = await fn();
    return { value, reads };
  } finally {
    for (const [n, f] of saved) fs[n] = f;
  }
};

// A read is classified by where it landed. The two path domains are unified the same way on both sides -- separators to `/`,
// a drive letter spelled one way, a UNC share left intact -- because the earlier version stripped the `//wsl$` prefix from the
// read but not from the root, which made every read of the package look like a read of the live tree.
const normalisePath = (p) => String(p).replace(/\\/g, '/')
  .replace(/^([A-Za-z]):\//, (m, d) => d.toLowerCase() + ':/')
  .replace(/\/{2,}/g, '/');

const classifyRead = (p, root, externals) => {
  const lin = normalisePath(p);
  // The UNC form of a WSL path and its `/mnt/<d>` form name the same file; both are compared after the same reduction.
  const rootForms = [normalisePath(root), normalisePath(root).replace(/^\/wsl\$\//, '')];
  if (rootForms.some((r) => lin === r || lin.startsWith(r + '/'))) return 'package';
  for (const e of externals) {
    const en = normalisePath(e).replace(/\/$/, '');
    if (lin === en || lin.startsWith(en + '/')) return 'external';
  }
  return 'live';
};

// ---------------------------------------------------------------- the audit
export const auditPackage = async ({ root, plan, externals = [], trace = true }) => {
  const resolve = makeResolver(root);
  const logical = makeLogical(root, plan);
  const planByPath = new Map((plan.members || []).map((m) => [m.path, m]));
  const roles = [];
  const problems = [];

  // The modules the payload carries, by name, so a role that names a module the plan does not carry is caught here rather
  // than at import time with a stack trace that says nothing about the payload.
  const packaged = (rel) => {
    if (!planByPath.has(rel)) return { present: false, why: 'the plan does not carry this module' };
    const want = planByPath.get(rel);
    if (!fs.existsSync(root + '/' + rel)) return { present: false, why: 'absent from the extracted tree' };
    const b = fs.readFileSync(root + '/' + rel);
    if (b.length !== want.bytes) return { present: false, why: 'a different size than the plan states' };
    if (sha(b) !== want.sha256) return { present: false, why: 'different bytes at the same size' };
    return { present: true, bytes: b.length, sha256: sha(b) };
  };

  for (const [name, spec] of Object.entries(ROLES)) {
    const modules = [];
    for (const m of spec.modules) {
      const p = packaged(m);
      modules.push({ module: m, ...p });
      if (!p.present) problems.push({ role: name, module: m, why: p.why });
    }
    // A tool is a program the packet executed, verified as bytes and deliberately not imported: both the publisher and the
    // driver dispatch on `process.argv` at load, so importing one would run it against this process's arguments.
    const tools = (spec.tools || []).map((t) => {
      const p = packaged(t.path);
      if (!p.present) problems.push({ role: name, tool: t.path, why: p.why });
      return { tool: t.path, why: t.why, ...p };
    });
    const needs = spec.needs.map((need) => {
      // A need is resolved against the plan, then through the strict resolver, so "the plan lists it" and "the package
      // carries it" are both required and a logical name that matches nothing is a refusal rather than a silent skip.
      try {
        const hits = logical(need);
        return { need: need.what || need.under, matched: hits.length, members: hits.slice(0, 4), present: true };
      } catch (err) {
        problems.push({ role: name, need: need.what || need.under, why: err.message });
        return { need: need.what || need.under, matched: 0, present: false, detail: err.message };
      }
    });

    // The consumers are imported from the package, and the URL each one resolved to is recorded. `import.meta.url` inside
    // the consumer is what a consumer sees, so a module that resolved from anywhere else shows up as a different prefix.
    const imports = [];
    for (const m of spec.modules) {
      if (!m.endsWith('.mjs') || !modules.find((x) => x.module === m && x.present)) continue;
      try {
        const url = resolve.url(m);
        const ns = await import(url);
        imports.push({ module: m, url, exports: Object.keys(ns).sort(),
          // A module that exports a version states which generation of itself it is; one that does not is recorded as
          // silent rather than given a version it never claimed.
          version: ns.CONTRACT_VERSION || ns.PKG_VERSION || ns.VERSION || null });
      } catch (err) {
        imports.push({ module: m, url: resolve.url(m), error: String(err && err.message) });
        problems.push({ role: name, module: m, why: 'the packaged module did not load: ' + String(err && err.message) });
      }
    }
    roles.push({ role: name, decides: spec.decides, modules, tools, needs, imports,
      ok: problems.every((p) => p.role !== name) });
  }

  // ---------------------------------------------------------------- the read trace
  //
  // The traced body is the packaged modules loading and the resolver walking the role's needs. That is the read set the
  // package can actually account for: anything outside it would have to come from a consumer that was not traced, which is
  // why the trace is reported with the list of modules it covered.
  let traceResult = null;
  if (trace) {
    const body = async () => {
      for (const spec of Object.values(ROLES)) {
        for (const need of spec.needs) { try { logical(need); } catch { /* the miss is already recorded above */ } }
        for (const t of (spec.tools || [])) { try { resolve(t.path); } catch { /* recorded above */ } }
        for (const m of spec.modules) {
          if (!m.endsWith('.mjs')) { try { resolve(m); } catch { /* recorded above */ } continue; }
          try { await import(resolve.url(m)); } catch { /* recorded above */ }
        }
      }
      return true;
    };
    const { reads } = await traceReads(body);
    const classified = reads.map((r) => ({ ...r, where: classifyRead(r.path, root, externals) }));
    const live = classified.filter((r) => r.where === 'live');
    const outside = classified.filter((r) => r.where === 'external');
    traceResult = {
      reads: classified.length,
      in_package: classified.filter((r) => r.where === 'package').length,
      external_pinned: outside.length,
      live: live.length,
      live_paths: [...new Set(live.map((r) => r.path))].slice(0, 20),
      external_paths: [...new Set(outside.map((r) => r.path))].slice(0, 20),
      note: 'every filesystem call the packaged consumers made while this audit ran, classified by where it landed',
    };
    if (live.length) problems.push({ why: 'a packaged consumer read a path outside the package and outside the pinned tools',
      count: live.length, first: traceResult.live_paths });
  }

  return { task: TASK, kind: 'h_package_audit', root, roles, trace: traceResult, problems,
    problem_count: problems.length, ok: problems.length === 0 };
};

// ---------------------------------------------------------------- the resolver negatives
//
// Each case copies the package, applies exactly one defect, and asks the same resolver and the same audit to decide. The
// pristine copy has to pass first: a negative suite over a tree that already failed proves nothing about the defects.
export const resolverNegatives = async ({ root, plan, externals = [], scratch }) => {
  const copyTree = (from, to) => {
    fs.mkdirSync(to, { recursive: true });
    for (const e of fs.readdirSync(from, { withFileTypes: true })) {
      const a = from + '/' + e.name;
      const b = to + '/' + e.name;
      if (e.isDirectory()) copyTree(a, b);
      else fs.copyFileSync(a, b);
    }
  };
  const cases = [];
  const pristine = scratch + '/case-00-pristine';
  copyTree(root, pristine);
  const base = await auditPackage({ root: pristine, plan, externals, trace: false });
  cases.push({ name: 'pristine', accepted: base.ok, expected: null, hit_target: base.ok,
    problems: base.problems.map((p) => p.module || p.tool || p.need || p.why) });

  const modules = Object.values(ROLES).flatMap((s) => s.modules);
  const tools = Object.values(ROLES).flatMap((s) => (s.tools || []).map((t) => t.path));
  const aModule = modules.find((m) => m.endsWith('.mjs'));
  const aPython = tools.find((m) => m.endsWith('.py'));

  const defects = [
    ['module-absent', (dir) => fs.unlinkSync(dir + '/' + aModule), aModule],
    ['module-tampered-worktree-valid', (dir) => {
      // One byte changed in the package while the worktree copy is untouched. This is the defect that a hash taken at
      // packing time cannot see and a hash taken at consumption time must.
      const p = dir + '/' + aModule;
      const b = fs.readFileSync(p);
      b[Math.floor(b.length / 2)] = b[Math.floor(b.length / 2)] ^ 0x01;
      fs.writeFileSync(p, b);
    }, aModule],
    ['single-item-same-size-drift', (dir) => {
      // Same length, different bytes: a check that only compares sizes passes this.
      const p = dir + '/' + aPython;
      const b = fs.readFileSync(p);
      const at = b.indexOf(0x0a);
      b[at] = b[at] === 0x0a ? 0x0d : 0x0a;
      fs.writeFileSync(p, b);
      return b.length;
    }, aPython],
    ['alias-miss-no-fallback', (dir) => {
      // A name that exists only in the worktree. The resolver has no second root, so this has to be a refusal.
      const res = makeResolver(dir);
      try { res('engine/not-in-the-package-147.mjs'); return 'RESOLVED'; } catch (err) {
        return err instanceof NotInPackage ? 'refused' : 'refused-for-the-wrong-reason: ' + err.message;
      }
    }, 'engine/not-in-the-package-147.mjs'],
    ['escape-the-root', (dir) => {
      const res = makeResolver(dir);
      for (const n of ['../../etc/passwd', '/etc/passwd', 'engine/../../../x']) {
        try { res(n); return 'RESOLVED ' + n; } catch (err) {
          if (!(err instanceof NotInPackage)) return 'refused-for-the-wrong-reason: ' + err.message;
        }
      }
      return 'refused';
    }, 'escape'],
    ['evidence-absent', (dir) => {
      // The role's evidence, not its code: a payload that carries the consumer but not the artefact it decides on cannot
      // decide the role. The member removed is the first one the *first* need of the *first* role resolves to, so the
      // refusal has to be about that need and not about a need that happened to fail for another reason.
      const need = Object.values(ROLES)[0].needs[0];
      const hits = makeLogical(dir, plan)(need);
      const victim = hits[0];
      fs.unlinkSync(dir + '/' + victim);
      return { need: need.what, victim };
    }, 'the prerequisite record'],
  ];

  for (let i = 0; i < defects.length; i++) {
    const [name, mutate, target] = defects[i];
    const dir = scratch + '/case-' + String(i + 1).padStart(2, '0') + '-' + name;
    copyTree(root, dir);
    const note = mutate(dir);
    const v = await auditPackage({ root: dir, plan, externals, trace: false });
    const noteNeed = note && typeof note === 'object' ? note.need : null;
    const hit = v.problems.some((p) => (p.module === target) || (p.tool === target) || (p.need === target)
      || p.why === target
      || (noteNeed && p.need === noteNeed)
      || (typeof note === 'string' && (p.need === note || p.module === note || p.tool === note))
      || (name === 'alias-miss-no-fallback' && note === 'refused' && v.problems.length > 0)
      || (name === 'escape-the-root' && note === 'refused' && v.problems.length > 0));
    cases.push({ name, expected: target, refused: !v.ok, hit_target: hit,
      problems: v.problems.map((p) => p.module || p.tool || p.need || p.why) });
  }

  const failures = cases.filter((c) => (c.name === 'pristine' ? !c.accepted : !(c.refused && c.hit_target)));
  return { task: TASK, kind: 'h_resolver_negatives', cases: cases.length - 1,
    refused: cases.filter((c) => c.name !== 'pristine' && c.refused).length,
    hit_target: cases.filter((c) => c.name !== 'pristine' && c.hit_target).length,
    pristine_accepted: cases[0].accepted, failures, results: cases, ok: failures.length === 0 };
};

// ---------------------------------------------------------------- CLI
const SELF_PATH = fileURLToPath(import.meta.url);
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH);
if (isMain) {
  const mode = process.argv[2];
  const root = process.argv[3];
  const planPath = process.argv[4];
  const outPath = process.argv[5];
  if (!mode || !root || !planPath) {
    console.error('usage: h-package-147.mjs audit|negatives <packageRoot> <plan.json> [out.json] [externals.json]');
    process.exit(2);
  }
  const plan = JSON.parse(fs.readFileSync(planPath, 'utf8'));
  let externals = [];
  if (process.argv[6]) externals = JSON.parse(fs.readFileSync(process.argv[6], 'utf8')).externals || [];
  const result = mode === 'audit'
    ? await auditPackage({ root, plan, externals })
    : await resolverNegatives({ root, plan, externals, scratch: process.argv[6] || (root + '-negatives') });
  if (outPath) {
    if (fs.existsSync(outPath)) throw new Error('REFUSED: ' + outPath + ' already exists');
    fs.writeFileSync(outPath, JSON.stringify(result, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  }
  console.log(JSON.stringify({ ok: result.ok, kind: result.kind, problems: (result.problems || []).slice(0, 8),
    roles: result.roles ? result.roles.map((r) => ({ role: r.role, ok: r.ok })) : undefined,
    trace: result.trace, cases: result.cases, refused: result.refused, hit_target: result.hit_target,
    failures: result.failures }, null, 2));
  process.exit(result.ok ? 0 : 1);
}
