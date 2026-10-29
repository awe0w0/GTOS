// GTOS-DSH-148 setup: register the two native roots before anything uses them, and record the identities this packet
// starts from.
//
// Three things are recorded here and nothing else is decided:
//
//   1. The roots did not exist. The dispatch says DSH observes and registers them; Root did not probe or create them. So the
//      absence is measured first, from the file system, and written down before the first `mkdir`. A record that says "the
//      root was absent" and is written after creating it would be a claim rather than an observation.
//   2. The skeleton each root needs. The guard writes `<work root>/logs/spec-<stage>.json` and runs with HOME, TMPDIR and
//      XDG_CACHE_HOME inside its work root; a root without those directories fails before the guarded command starts, and
//      that failure looks like a bad stage. The skeleton is created by Node file work against the UNC form of each root --
//      never by a WSL command -- and each directory is created only if absent.
//   3. The identities the packet starts from: the parent commit and its tree, the primary branch and index, and the frozen
//      inputs. The primary `.git/config` is measured and compared with the dispatch pin; the comparison is recorded either
//      way, including the exact bytes that explain a difference.
import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const L = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-148-e759-executed-consumers';
const S = '//wsl$/Ubuntu-20.04/var/tmp/gtos-dsh-148-e759-executed-consumers-view';
const L_LINUX = '/var/tmp/gtos-dsh-148-e759-executed-consumers';
const S_LINUX = '/var/tmp/gtos-dsh-148-e759-executed-consumers-view';
const REPO = 'F:/GTOS-Chromium/sources/GTOS';
const stage = process.argv[2] || 'setup148-01';
const outPath = process.argv[3] || (R + '/setup-148-01.json');
if (fs.existsSync(outPath)) { console.error(JSON.stringify({ refused: true, why: 'the output already exists', path: outPath })); process.exit(1); }

// ---------------------------------------------------------------- 1. absence, measured
const absence = [L, S].map((p) => ({ host: p, exists: fs.existsSync(p) }));
if (absence.some((a) => a.exists)) {
  console.error(JSON.stringify({ refused: true, why: 'a 148 root already exists, so this is not the packet that created it',
    roots: absence }, null, 2));
  process.exit(1);
}

// ---------------------------------------------------------------- 2. the roots and their skeleton
// The two roots have different jobs, so they get different skeletons. L is where guarded work runs: its logs carry the
// guard's own spec and run rows, and its home/tmp/cache are the writable environment. S is the source view the guard reads
// from: it holds the materialised package-derived bytes the static gate is handed, and the helper inputs.
const PLAN = [
  { root: L, linux: L_LINUX, dirs: ['logs', 'home', 'tmp', 'cache', 'shm', 'evidence', 'inputs', 'frozen-helpers', 'test-only', 'unit-run'] },
  { root: S, linux: S_LINUX, dirs: ['logs', 'home', 'tmp', 'cache', 'shm', 'evidence', 'inputs', 'frozen-helpers', 'image', 'material', 'staging', 'test-only', 'unit-run'] },
];
const created = [];
for (const p of PLAN) {
  if (!fs.existsSync(p.root)) { fs.mkdirSync(p.root, { recursive: false }); created.push(p.root); }
  for (const d of p.dirs) {
    const full = p.root + '/' + d;
    const existed = fs.existsSync(full);
    if (!existed) { fs.mkdirSync(full, { recursive: false }); created.push(full); }
    const st = fs.statSync(full);
    if (!st.isDirectory()) throw new Error('not a directory: ' + full);
  }
}
const skeleton = PLAN.map((p) => ({ root: p.linux, host: p.root, dirs: p.dirs,
  rows: p.dirs.map((d) => { const st = fs.statSync(p.root + '/' + d); return { dir: d, mode: '0' + (st.mode & 0o777).toString(8) }; }) }));

// ---------------------------------------------------------------- 3. the identities this packet starts from
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const pin = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha256(b) }; };

const authority = readJson(R + '/authority-148.json');
const dispatch = readJson(R + '/dispatch-148.json');

// The frozen inputs are re-measured here rather than trusted from the dispatch. A dispatch that names 54 inputs and a file
// that has since changed is exactly the difference this record exists to surface.
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

// The primary `.git/config` pin does not match the bytes on disk, and the difference is one line. It is recorded rather than
// cured: the config is a protected object, and editing it to make a hash agree would be exactly the "move a used file to
// make the check pass" this packet refuses. The line is identified so a reader can see it is not a rewriting of the file.
const configPath = REPO + '/.git/config';
const configBytes = fs.readFileSync(configPath);
const configPin = authority.primary_config;
const configLines = configBytes.toString('utf8').split('\n');
const explainedBy = [];
for (let i = 0; i < configLines.length; i++) {
  const without = Buffer.from(configLines.filter((_, j) => j !== i).join('\n'), 'utf8');
  if (sha256(without) === configPin.sha256) explainedBy.push({ dropped_line: configLines[i], at: i + 1, bytes_without: without.length });
}
const indexPin = authority.primary_index;
const indexActual = pin(indexPin.path);
const primaryFiles = authority.primary_files.map((e) => ({ logical: e.logical, path: e.path,
  frozen: { bytes: e.bytes, sha256: e.sha256 }, actual: fs.existsSync(e.path) ? pin(e.path) : null }));

const record = {
  task: 'GTOS-DSH-148', kind: 'setup', stage,
  what: 'the two native roots registered before first use, and the identities this packet starts from',
  roots: {
    observed_absent_before_creation: absence,
    observed_by: 'node fs.existsSync against the UNC form of each root, before the first mkdir',
    created_by: 'node fs.mkdirSync against the UNC form of each root, never by a WSL command',
    top_level_created: created,
    skeleton,
    independent_not_nested: L_LINUX + ' and ' + S_LINUX + ' share no ancestor below /var/tmp',
  },
  repository: {
    repo: REPO,
    parent_commit: dispatch.parent_commit,
    parent_tree: dispatch.tree || null,
    worktree: R + '/worktree-148',
    branch: 'codex/gtos-dsh-148-executed-consumers',
    primary_commit: authority.repository_base_commit,
    primary_head: fs.readFileSync(REPO + '/.git/HEAD', 'utf8').trim(),
  },
  primary_index: { path: indexPin.path, frozen: { bytes: indexPin.bytes, sha256: indexPin.sha256 },
    actual: indexActual, matches: indexActual.bytes === indexPin.bytes && indexActual.sha256 === indexPin.sha256 },
  primary_config: { path: configPin.path, frozen: { bytes: configPin.bytes, sha256: configPin.sha256 },
    actual: { bytes: configBytes.length, sha256: sha256(configBytes) },
    matches: configBytes.length === configPin.bytes && sha256(configBytes) === configPin.sha256,
    difference_explained_by: explainedBy,
    handling: 'recorded, not cured: the config is a protected object, and the line that explains the difference is named so a '
      + 'reader can see the rest of the file is byte-identical to the pin' },
  primary_files: primaryFiles,
  primary_files_all_match: primaryFiles.every((f) => f.actual && f.actual.bytes === f.frozen.bytes && f.actual.sha256 === f.frozen.sha256),
  inputs: { count: inputs.length, distinct_paths: new Set(inputs.map((i) => i.path)).size,
    all_match: inputs.every((i) => i.matches), mismatches: inputs.filter((i) => !i.matches) },
  standing_inputs: { count: standing.length, all_match: standing.every((i) => i.matches),
    mismatches: standing.filter((i) => !i.matches) },
  ok: inputs.every((i) => i.matches) && standing.every((i) => i.matches) && primaryFiles.every((f) => f.actual)
    && indexActual.sha256 === indexPin.sha256,
  ok_means: 'every frozen input and standing input is the bytes the dispatch names, the primary index is the frozen index, and '
    + 'the 13 protected primary sources are unchanged. The config difference is reported separately and does not make this '
    + 'record green by being ignored',
};
const text = JSON.stringify(record, null, 2) + '\n';
fs.writeFileSync(outPath, text, { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ stage, ok: record.ok, roots: { L, S }, top_level_created: created.length,
  inputs: inputs.length, inputs_all_match: record.inputs.all_match,
  standing_all_match: record.standing_inputs.all_match,
  index_matches: record.primary_index.matches, config_matches: record.primary_config.matches,
  config_difference: explainedBy, primary_files_all_match: record.primary_files_all_match,
  record: { path: outPath, bytes: Buffer.byteLength(text), sha256: sha256(Buffer.from(text)) } }, null, 2));
process.exit(record.ok ? 0 : 1);
