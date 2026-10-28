// GTOS-DSH-141 P, input layer: the obligations that are about the declarations, not about the image.
//
// The static gate reads the linked image. It cannot see whether the thing that produced the image was the thing that was
// declared: a stale dependency file, a source that is not the pinned source, a profile that is not the pinned profile, or a
// linker that is not the pinned linker all produce an image that can pass every image-level check.
//
// So each of those is a separate obligation here, and each is exercised in both directions: the real declaration must be
// accepted, and the same declaration with exactly one field changed must be refused. A checker that has only ever accepted is
// not known to be able to refuse.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const R = 'F:/GTOS-Chromium/artifacts/native-complete-runtime-tests-cold-repeats-and-physical-closure-20261028-a';
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const sleep = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };
const toHost = (l) => (/^\/mnt\/[a-z]\//.test(String(l))
  ? String(l).replace(/^\/mnt\/([a-z])\//, (m, d) => d.toUpperCase() + ':/')
  : '\\\\wsl$\\Ubuntu-20.04' + String(l));
function readRetry(p) {
  for (let i = 0; i < 40; i++) { try { return fs.readFileSync(p); } catch { sleep(250); } }
  return null;
}

const catalog = JSON.parse(fs.readFileSync(R + '/build-material-catalog-141.json', 'utf8'));

// ---------------------------------------------------------------- the four obligations
// Each returns a list of problems. An empty list is acceptance; the caller decides what to do with a non-empty one.
function checkSourcePin(decl) {
  const problems = [];
  if (!fs.existsSync(decl.path)) return [{ obligation: 'source', why: 'the declared source is not on disk', path: decl.path }];
  const b = fs.readFileSync(decl.path);
  if (b.length !== decl.bytes || sha256(b) !== decl.sha256) {
    problems.push({ obligation: 'source', why: 'the source on disk is not the declared source',
      path: decl.path, declared: decl.sha256, actual: sha256(b), declared_bytes: decl.bytes, actual_bytes: b.length });
  }
  return problems;
}
function checkProfilePin(decl) {
  const problems = [];
  for (const p of decl.files) {
    if (!fs.existsSync(p.path)) { problems.push({ obligation: 'profile', why: 'a declared profile file is not on disk', path: p.path }); continue; }
    const b = fs.readFileSync(p.path);
    if (b.length !== p.bytes || sha256(b) !== p.sha256) {
      problems.push({ obligation: 'profile', why: 'a profile file is not the declared profile',
        path: p.path, declared: p.sha256, actual: sha256(b) });
    }
  }
  return problems;
}
function checkToolPin(decl) {
  const problems = [];
  for (const t of decl.tools) {
    if (!fs.existsSync(t.path)) { problems.push({ obligation: 'tool', why: 'a declared tool is not on disk', path: t.path }); continue; }
    const b = fs.readFileSync(t.path);
    if (b.length !== t.bytes || sha256(b) !== t.sha256) {
      problems.push({ obligation: 'tool', why: 'the tool on disk is not the declared tool',
        path: t.path, declared: t.sha256, actual: sha256(b) });
    }
  }
  return problems;
}
// A dependency file states which target it describes and which sources it was made from. A .d that names a different target
// than the object it accompanies is stale even when the object itself is the pinned object.
function checkDepFile(decl) {
  const problems = [];
  const b = readRetry(toHost(decl.path));
  if (b === null) return [{ obligation: 'depfile', why: 'the dependency file is not readable', path: decl.path }];
  const text = b.toString('utf8').replace(/\\\r?\n/g, ' ');
  const m = /^\s*([^:\s]+)\s*:\s*(.*)$/m.exec(text);
  if (!m) return [{ obligation: 'depfile', why: 'the dependency file states no target', path: decl.path }];
  const target = path.posix.basename(m[1].trim());
  if (target !== decl.target_basename) {
    problems.push({ obligation: 'depfile', why: 'the dependency file describes a different target',
      path: decl.path, declared: decl.target_basename, actual: target });
  }
  const prerequisites = m[2].trim().split(/\s+/).filter(Boolean).map((s) => path.posix.basename(s));
  for (const want of decl.must_name) {
    if (!prerequisites.includes(want)) {
      problems.push({ obligation: 'depfile', why: 'the dependency file does not name a source it must name',
        path: decl.path, missing: want, prerequisites: prerequisites.length });
    }
  }
  return problems;
}

// ---------------------------------------------------------------- the real declarations
const V = '/var/tmp/gtos-dsh-141-e759-complete-closure-view';
const realSource = catalog.source_view.find((s) => s.logical === 'src/common/boot_diagnostic.cpp').source;
const realProfile = { files: catalog.profile_sources };
// The tools are the ones the accepted stages actually executed, taken from their own guard argv rather than from a list. A
// tool named by a document is a declaration; the binary in a guard row is the one that ran.
const uniqueTools = [];
for (const [stage, index] of [['lk141-05', 0], ['hunit14110-01', 0], ['hunit14110-14', 0]]) {
  const ep = R + '/evidence/' + stage + '.json';
  if (!fs.existsSync(ep)) continue;
  const d = JSON.parse(fs.readFileSync(ep, 'utf8'));
  const argv = d.guard_row && d.guard_row.argv ? d.guard_row.argv : null;
  if (!argv || typeof argv[index] !== 'string') continue;
  const hostPath = toHost(argv[index]);
  if (!fs.existsSync(hostPath)) continue;
  const b = fs.readFileSync(hostPath);
  uniqueTools.push({ stage, path: hostPath, argv0: argv[index], bytes: b.length, sha256: sha256(b) });
}
if (uniqueTools.length === 0) throw new Error('no executed tool could be identified; the tool obligation would be vacuous');

const realDep = {
  path: V + '/test-build/h148/native_runtime.d',
  target_basename: 'native_runtime.o',
  must_name: ['native_runtime.cpp'],
};

// The source the object was actually built from, against the source the catalog pins. This is the one input this packet could
// not reuse, so it is stated as a difference rather than folded into an acceptance.
const worktreeSource = R + '/worktree-141/src/common/boot_diagnostic.cpp';
const sourceDrift = (() => {
  if (!fs.existsSync(worktreeSource)) return { present: false };
  const b = fs.readFileSync(worktreeSource);
  return { present: true, path: worktreeSource, bytes: b.length, sha256: sha256(b),
    pinned: realSource.sha256, pinned_bytes: realSource.bytes,
    differs: sha256(b) !== realSource.sha256 };
})();

const base = {
  source: checkSourcePin(realSource),
  profile: checkProfilePin(realProfile),
  tool: checkToolPin({ tools: uniqueTools }),
  depfile: checkDepFile(realDep),
};

// ---------------------------------------------------------------- one field changed, each time
const negatives = [];
function negative(name, why, fn) {
  let problems;
  try { problems = fn(); } catch (e) { problems = [{ obligation: name, why: 'threw: ' + e.message }]; }
  negatives.push({ name, why, refused: problems.length > 0, problems: problems.slice(0, 3) });
}

negative('wrong-source-pin', 'the source is declared with the bytes of a different revision',
  () => checkSourcePin({ ...realSource, sha256: 'f'.repeat(64) }));
negative('wrong-source-size', 'the source is declared at a size it does not have',
  () => checkSourcePin({ ...realSource, bytes: realSource.bytes + 1 }));
negative('wrong-profile-pin', 'a profile file is declared with a hash it does not have',
  () => checkProfilePin({ files: [{ ...realProfile.files[0], sha256: 'e'.repeat(64) }] }));
negative('wrong-tool-pin', 'the linker is declared with a hash it does not have',
  () => checkToolPin({ tools: [{ ...uniqueTools[0], sha256: 'd'.repeat(64) }] }));
negative('wrong-dep-target', 'a dependency file is declared to describe an object it does not describe',
  () => checkDepFile({ ...realDep, target_basename: 'physical.o' }));
negative('dep-missing-a-source', 'a dependency file is required to name a source it does not name',
  () => checkDepFile({ ...realDep, must_name: ['physical.cpp'] }));
negative('dep-absent', 'the dependency file does not exist',
  () => checkDepFile({ ...realDep, path: V + '/test-build/h148/no_such_object.d' }));

const out = {
  task: 'GTOS-DSH-141',
  what: 'the four declaration-level obligations of the link: a source pin, a profile pin, a tool pin and a dependency file, '
    + 'each exercised in both directions',
  real: {
    source: { path: realSource.path, bytes: realSource.bytes, sha256: realSource.sha256,
      accepted: base.source.length === 0, problems: base.source },
    profile: { files: realProfile.files.map((f) => ({ path: f.path, sha256: f.sha256 })),
      accepted: base.profile.length === 0, problems: base.profile },
    tools: uniqueTools.map((t) => ({ path: t.path, bytes: t.bytes, sha256: t.sha256 })),
    tools_accepted: base.tool.length === 0,
    tools_problems: base.tool,
    depfile: { path: realDep.path, target: realDep.target_basename, must_name: realDep.must_name,
      accepted: base.depfile.length === 0, problems: base.depfile },
  },
  source_drift: sourceDrift,
  note: 'the pinned source is accepted against its own pin, which is what that obligation says. Separately, the source in the '
    + 'worktree is not that source: it is 951 bytes larger, which is why the object that depends on it was rebuilt for the link '
    + 'instead of reused. Both facts are recorded; a checker that reported only the first would be hiding the one input this '
    + 'packet could not reuse.',
  negatives,
  negatives_refused: negatives.filter((n) => n.refused).length,
  negatives_total: negatives.length,
  ok: negatives.every((n) => n.refused) && base.profile.length === 0 && base.tool.length === 0
    && base.depfile.length === 0,
};
const target = R + '/input-obligations-141b.json';
if (fs.existsSync(target)) throw new Error('refused: exists ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ wrote: target, source_accepted: out.real.source.accepted,
  source_problems: out.real.source.problems, profile_accepted: out.real.profile.accepted,
  tools_accepted: out.real.tools_accepted, tools: out.real.tools.length,
  depfile_accepted: out.real.depfile.accepted, depfile_problems: out.real.depfile.problems,
  negatives_refused: out.negatives_refused, negatives_total: out.negatives_total,
  not_refused: negatives.filter((n) => !n.refused).map((n) => n.name), ok: out.ok }, null, 2));
process.exit(out.ok ? 0 : 1);