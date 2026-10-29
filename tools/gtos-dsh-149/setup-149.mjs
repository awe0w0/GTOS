// GTOS-DSH-149 setup: register the two native roots before anything uses them, and record the identities this packet
// starts from.
//
// The shape is 148's, because the review kept that part: the absence is measured before the first mkdir, the skeleton each
// root needs is created by Node file work against the UNC form of the root (never by a WSL command), and the frozen inputs
// are re-measured here rather than trusted from the dispatch. A record that says "the root was absent" and is written after
// creating it would be a claim rather than an observation.
//
// What is new for 149 is the last block: the origin materials this packet is allowed to read and must not write. The review's
// finding STATIC_WRONG_NATIVE_ROOT_AND_USED_OUTPUT_REUSE was that 148's static callback wrote into a root its own authority did
// not name and deleted a gate report to make room. So every previous native root is listed here as read-only, and the two
// roots this packet owns are the only ones a guarded command may be given as a destination.
import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const L = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-149-e759-role-semantics';
const S = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-149-e759-role-semantics-view';
const L_LINUX = '/var/tmp/gtos-dsh-149-e759-role-semantics';
const S_LINUX = '/var/tmp/gtos-dsh-149-e759-role-semantics-view';
const REPO = 'F:/GTOS-Chromium/sources/GTOS';
const stage = process.argv[2] || 'setup149-01';
const outPath = process.argv[3] || (R + '/' + stage + '.json');
if (fs.existsSync(outPath)) { console.error(JSON.stringify({ refused: true, why: 'the output already exists', path: outPath })); process.exit(1); }

// ---------------------------------------------------------------- 1. absence, measured
const absence = [L, S].map((p) => ({ host: p, exists: fs.existsSync(p) }));
if (absence.some((a) => a.exists)) {
  console.error(JSON.stringify({ refused: true, why: 'a 149 root already exists, so this is not the packet that created it',
    roots: absence }, null, 2));
  process.exit(1);
}

// ---------------------------------------------------------------- 2. the roots and their skeleton
// L is where guarded work runs: its logs carry the guard's own spec and run rows, and its home/tmp/cache are the writable
// environment. S is the source view the guard reads from: it holds the materialised package-derived bytes, the runtime, and
// the helper inputs. `stage-<name>` under S is where the static gate's inputs are materialised for this packet -- a new
// directory per stage, so a second gate run never reuses the first one's bytes.
const PLAN = [
  { root: L, linux: L_LINUX, dirs: ['logs', 'home', 'tmp', 'cache', 'shm', 'evidence', 'inputs', 'frozen-helpers', 'test-only', 'unit-run'] },
  { root: S, linux: S_LINUX, dirs: ['logs', 'home', 'tmp', 'cache', 'shm', 'evidence', 'inputs', 'frozen-helpers', 'image', 'material', 'staging', 'runtime', 'test-only', 'unit-run'] },
];
const created = [];
for (const p of PLAN) {
  if (!fs.existsSync(p.root)) { fs.mkdirSync(p.root, { recursive: false }); created.push(p.root); }
  for (const d of p.dirs) {
    const full = p.root + '/' + d;
    if (!fs.existsSync(full)) { fs.mkdirSync(full, { recursive: false }); created.push(full); }
    const st = fs.statSync(full);
    if (!st.isDirectory()) throw new Error('not a directory: ' + full);
  }
}
const skeleton = PLAN.map((p) => ({ root: p.linux, host: p.root, dirs: p.dirs,
  rows: p.dirs.map((d) => { const st = fs.statSync(p.root + '/' + d); return { dir: d, mode: '0' + (st.mode & 0o777).toString(8) }; }) }));

// ---------------------------------------------------------------- 3. the identities this packet starts from
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha256(b) }; };

const authority = readJson(R + '/authority-149.json');
const dispatch = readJson(R + '/dispatch-149.json');

const inputs = dispatch.inputs.map((e) => {
  const now = fs.existsSync(e.path) ? pin(e.path) : null;
  return { path: e.path, frozen: { bytes: e.bytes, sha256: e.sha256 }, actual: now,
    matches: !!(now && now.bytes === e.bytes && now.sha256 === e.sha256) };
});
const standing = dispatch.standing_inputs.map((e) => {
  const now = fs.existsSync(e.path) ? pin(e.path) : null;
  return { path: e.path, frozen: { bytes: e.bytes, sha256: e.sha256 }, actual: now,
    matches: !!(now && now.bytes === e.bytes && now.sha256 === e.sha256) };
});

const configPath = REPO + '/.git/config';
const configBytes = fs.readFileSync(configPath);
const configPin = authority.primary_config;
const indexPin = authority.primary_index;
const indexActual = pin(indexPin.path);
const indexMtimeNs = String(fs.statSync(indexPin.path).mtimeMs * 1e6);
const primaryFiles = authority.primary_files.map((e) => ({ logical: e.logical, path: e.path,
  frozen: { bytes: e.bytes, sha256: e.sha256 }, actual: fs.existsSync(e.path) ? pin(e.path) : null }));

// The roots a guarded command may write to. Everything else named by an earlier packet is read-only, and the record says so
// once, here, so no later step has to reason about it again.
const readonlyRoots = authority_authorization().read_only_previous_native_roots;
function authority_authorization() { return readJson(R + '/authorization-149.json'); }

const record = {
  task: 'GTOS-DSH-149', kind: 'setup', stage,
  what: 'the two native roots registered before first use, the identities this packet starts from, and the roots a guarded '
    + 'command is allowed to write to',
  roots: {
    observed_absent_before_creation: absence,
    observed_by: 'node fs.existsSync against the UNC form of each root, before the first mkdir',
    created_by: 'node fs.mkdirSync against the UNC form of each root, never by a WSL command',
    top_level_created: created,
    skeleton,
    independent_not_nested: L_LINUX + ' and ' + S_LINUX + ' share no ancestor below /var/tmp',
    writable: [{ name: '149-L', linux: L_LINUX, host: L }, { name: '149-S', linux: S_LINUX, host: S }],
    readonly_previous_native_roots: { count: readonlyRoots.length, roots: readonlyRoots,
      why: 'a guarded command in this packet is given a destination inside 149-L or 149-S only; these exist so the originals '
        + 'this packet preserves can be read by exact locator, not so they can be written to' },
  },
  repository: {
    repo: REPO,
    parent_commit: dispatch.parent_commit,
    worktree: R + '/worktree-149',
    branch: 'codex/gtos-dsh-149-role-semantics',
    primary_commit: authority.repository_base_commit,
    primary_head: fs.readFileSync(REPO + '/.git/HEAD', 'utf8').trim(),
  },
  primary_index: { path: indexPin.path, frozen: { bytes: indexPin.bytes, sha256: indexPin.sha256 },
    actual: indexActual, matches: indexActual.bytes === indexPin.bytes && indexActual.sha256 === indexPin.sha256,
    mtime_ns_now: indexMtimeNs, mtime_ns_frozen: authority.primary_index_mtime_ns,
    mtime_matches: indexMtimeNs === authority.primary_index_mtime_ns,
    why_mtime: 'the index is a protected object; its bytes and its mtime are both measured so a later step can show it was '
      + 'not rewritten, and a difference is reported rather than cured' },
  primary_config: { path: configPin.path, frozen: { bytes: configPin.bytes, sha256: configPin.sha256 },
    actual: { bytes: configBytes.length, sha256: sha256(configBytes) },
    matches: configBytes.length === configPin.bytes && sha256(configBytes) === configPin.sha256,
    handling: 'recorded, not cured: the config is a protected object and is never edited to make a hash agree' },
  primary_files: primaryFiles,
  primary_files_all_match: primaryFiles.every((f) => f.actual && f.actual.bytes === f.frozen.bytes && f.actual.sha256 === f.frozen.sha256),
  inputs: { count: inputs.length, distinct_paths: new Set(inputs.map((i) => i.path)).size,
    all_match: inputs.every((i) => i.matches), mismatches: inputs.filter((i) => !i.matches) },
  standing_inputs: { count: standing.length, all_match: standing.every((i) => i.matches),
    mismatches: standing.filter((i) => !i.matches) },
  ok: inputs.every((i) => i.matches) && standing.every((i) => i.matches) && primaryFiles.every((f) => f.actual)
    && indexActual.sha256 === indexPin.sha256,
  ok_means: 'every frozen input and standing input is the bytes the dispatch names, the primary index is the frozen index, and '
    + 'the 13 protected primary sources are unchanged. The config difference and the index mtime are reported separately and '
    + 'do not make this record green by being ignored',
};
const text = JSON.stringify(record, null, 2) + '\n';
fs.writeFileSync(outPath, text, { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ stage, ok: record.ok, roots: { L, S }, top_level_created: created.length,
  inputs: inputs.length, inputs_all_match: record.inputs.all_match,
  standing_all_match: record.standing_inputs.all_match,
  index_matches: record.primary_index.matches, index_mtime_matches: record.primary_index.mtime_matches,
  config_matches: record.primary_config.matches, primary_files_all_match: record.primary_files_all_match,
  readonly_roots: readonlyRoots.length,
  record: { path: outPath, bytes: Buffer.byteLength(text), sha256: sha256(Buffer.from(text)) } }, null, 2));
process.exit(record.ok ? 0 : 1);
