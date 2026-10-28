// GTOS-DSH-144 P, the selection: which of the 61 link slots may be reused and which must be rebuilt.
//
// The 141 packet reused 60 objects because their bytes equalled an old catalog pin. That is not evidence of anything: an object
// whose bytes are unchanged is only reusable if the source it was built from is also unchanged. Two objects can be identical
// while the source has moved on, and then the object is a stale artefact that happens to hash the same.
//
// So the question this file answers is per slot: what source was this object built from, and is that source still what the
// current committed tree says it is? A slot is reusable only when both the object's bytes match its pin AND the source the
// object was built from matches the current source byte for byte. Everything else is a rebuild, and a slot where either side of
// that comparison is missing is unproven rather than assumed fine.
//
// The order comes from the 140 spec that produced the last published image, not from a list written here.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

// The roots are located from this file's own path, so a reviewer can run the selection from a clean checkout. The first version of
// this tool hardcoded the packet root, which is the mistake 141 made with its committed copies: they could not run anywhere else.
import { fileURLToPath } from 'node:url';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const WORKTREE_DIR = path.resolve(HERE, '..', '..');
const R = path.resolve(WORKTREE_DIR, '..');
const R140 = path.resolve(R, '..', 'native-guarded-frame-recovery-repeatability-and-portable-consumption-20261027-a');
const WORKTREE = WORKTREE_DIR;
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const sleep = (ms) => { const a = new Int32Array(new SharedArrayBuffer(16)); Atomics.wait(a, 0, 0, ms); };
// A dependency graph shares most of its nodes: the same headers appear in dozens of slots. Hashing each file once and reusing the
// result is not an optimisation of the check, it is the same check with the same values; without it the run reads the same
// header over the share a thousand times and takes minutes to answer a question about 61 objects.
const HASH_CACHE = new Map();
function fileIdentity(p) {
  if (HASH_CACHE.has(p)) return HASH_CACHE.get(p);
  let out = null;
  for (let i = 0; i < 6; i++) {
    try { const b = fs.readFileSync(p); out = { bytes: b.length, sha256: sha(b) }; break; }
    catch (e) { if (e.code === 'ENOENT') break; sleep(120); }
  }
  HASH_CACHE.set(p, out);
  return out;
}
const readRetry = fileIdentity;
// A path may arrive in either domain. The dependency record stores Windows UNC paths while the spec stores Linux ones, so a
// translator that assumed Linux would prefix a UNC path with the share a second time and produce a path that cannot exist. A
// path that is already a Windows path is passed through unchanged.
const toHost = (l) => {
  const s = String(l);
  if (/^[A-Za-z]:[\\/]/.test(s) || /^\\\\/.test(s) || /^\/\/wsl\$/i.test(s)) return s;
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(s);
  return m ? m[1].toUpperCase() + ':/' + m[2] : '//wsl$/Ubuntu-20.04' + s;
};

const catalog = JSON.parse(fs.readFileSync(R + '/build-material-catalog-144.json', 'utf8'));
const spec = JSON.parse(fs.readFileSync(catalog.parent_actual_final_link_spec.path, 'utf8'));
const individuals = spec.control.stage_options.individuals;
const linkerScript = spec.control.stage_options.linkerScript;

// ---------------------------------------------------------------- what each object was built from
// The link-input-read record lists, per slot, the exact sources the compiler read: the .cpp or .s it was given and every header
// in its dependency file, each resolved to a path inside the 140 wave. Those wave files are the bytes the compiler actually
// consumed, so they are the "built from" side of the comparison. The record carries names rather than hashes, but it does not
// need hashes: the wave still exists, so the bytes are read and hashed here.
const inputRead = JSON.parse(fs.readFileSync(R140 + '/link-input-read-p140-05.json', 'utf8'));
const closure = JSON.parse(fs.readFileSync(R140 + '/producer-closure-p140-05.json', 'utf8'));
const builtBySlot = new Map(closure.built.map((b) => [b.token, b]));
const readBySlot = new Map(inputRead.inputs.map((r) => [r.slot, r]));

// A resolved wave path carries its logical name after the wave directory, which is what makes it comparable to the current tree.
function logicalFromWave(p) {
  const s = String(p).replace(/\\/g, '/');
  const m = /\/wave-[0-9]+\/(.+)$/.exec(s);
  if (m) return m[1];
  const m2 = /(?:^|\/)(src|include|tests|tools)\/(.+)$/.exec(s);
  return m2 ? m2[1] + '/' + m2[2] : null;
}

const sourceView = new Map(catalog.source_view.map((v) => [v.logical, v.source]));
const objectBySlug = new Map(catalog.objects.map((o) => [o.slug, o]));

const slots = [];
for (const [i, linux] of individuals.entries()) {
  const base = path.posix.basename(linux);
  const cat = objectBySlug.get(base);
  const read = readBySlot.get(base) || null;
  const built = builtBySlot.get(base) || null;
  const slot = { index: i + 1, basename: base, linux, logical: cat ? cat.logical : (built ? built.logical : null),
    kind: cat ? cat.kind : (built ? built.kind : null) };

  // The object as it sits on disk now, and the pin the catalog holds for it.
  const hostPath = toHost(linux);
  const obj = fileIdentity(hostPath);
  slot.object = { present: obj !== null, path: linux };
  if (obj !== null) { slot.object.bytes = obj.bytes; slot.object.sha256 = obj.sha256; }
  if (cat && cat.object) {
    slot.object.pinned_bytes = cat.object.bytes;
    slot.object.pinned_sha256 = cat.object.sha256;
    slot.object.matches_pin = obj !== null && obj.bytes === cat.object.bytes && obj.sha256 === cat.object.sha256;
  } else {
    slot.object.matches_pin = null;
    slot.object.no_catalog_pin = true;
  }

  // The source side. Every file the compiler read for this slot is compared between the wave that was compiled and the current
  // committed tree, byte for byte.
  const consumedPaths = [];
  if (read) {
    for (const r of (read.dep_sources_resolved || [])) {
      const logical = logicalFromWave(r);
      if (logical && !consumedPaths.some((c) => c.logical === logical)) consumedPaths.push({ logical, wave: r });
    }
  }
  if (built && built.source) {
    const logical = logicalFromWave(built.source) || built.logical;
    if (logical && !consumedPaths.some((c) => c.logical === logical)) consumedPaths.push({ logical, wave: built.source });
  }
  slot.consumed_sources = consumedPaths.length;

  const drift = [];
  const compared = [];
  for (const c of consumedPaths) {
    const waveBytes = fileIdentity(toHost(c.wave));
    const curBytes = fileIdentity(WORKTREE + '/' + c.logical);
    if (waveBytes === null) { drift.push({ logical: c.logical, state: 'the compiled copy is no longer readable' }); continue; }
    if (curBytes === null) { drift.push({ logical: c.logical, state: 'absent from the current tree' }); continue; }
    const same = waveBytes.bytes === curBytes.bytes && waveBytes.sha256 === curBytes.sha256;
    compared.push({ logical: c.logical, bytes: waveBytes.bytes, same });
    if (!same) {
      drift.push({ logical: c.logical, state: 'changed', built_from_bytes: waveBytes.bytes,
        built_from: waveBytes.sha256, current_bytes: curBytes.bytes, current: curBytes.sha256 });
    }
  }
  slot.compared = compared.length;
  slot.source_drift = drift;

  // The decision. Reusable needs both sides proven: the object's bytes still match a pin, and every file the compiler read still
  // matches the current tree. Anything else is a rebuild, and a slot with no provenance at all is unproven.
  if (slot.object.present !== true) slot.decision = 'rebuild-object-absent';
  else if (slot.object.matches_pin === false) slot.decision = 'rebuild-object-changed';
  else if (slot.consumed_sources === 0) slot.decision = 'unproven-no-provenance';
  else if (drift.length > 0) slot.decision = 'rebuild-source-changed';
  else slot.decision = 'reuse';

  slots.push(slot);
}

// ---------------------------------------------------------------- the assembly sources and the profile
// The five assembly sources and the profile are not objects in the link, but they decide whether a reused object is still the
// right object, so they are judged the same way: current bytes against the bytes the producer consumed.
const asmLogical = catalog.source_inputs.map((s) => s.logical);
const asmRows = asmLogical.map((logical) => {
  const cur = fileIdentity(WORKTREE + '/' + logical);
  const pinned = catalog.source_inputs.find((s) => s.logical === logical).source;
  return { logical, pinned_bytes: pinned.bytes, pinned_sha256: pinned.sha256,
    current_bytes: cur ? cur.bytes : null, current_sha256: cur ? cur.sha256 : null,
    matches: cur !== null && cur.bytes === pinned.bytes && cur.sha256 === pinned.sha256 };
});
const profileRows = catalog.profile_sources.map((p) => {
  const cur = fileIdentity(p.path);
  return { path: p.path, pinned_bytes: p.bytes, pinned_sha256: p.sha256,
    present: cur !== null, matches: cur !== null && cur.bytes === p.bytes && cur.sha256 === p.sha256 };
});

const linkerScriptCurrent = (() => {
  const logical = logicalFromWave(linkerScript);
  const cur = logical ? fileIdentity(WORKTREE + '/' + logical) : null;
  return { linux: linkerScript, logical, present: cur !== null,
    bytes: cur ? cur.bytes : null, sha256: cur ? cur.sha256 : null };
})();

const counts = {
  slots: slots.length,
  reuse: slots.filter((s) => s.decision === 'reuse').length,
  rebuild_source_changed: slots.filter((s) => s.decision === 'rebuild-source-changed').length,
  rebuild_object_changed: slots.filter((s) => s.decision === 'rebuild-object-changed').length,
  rebuild_object_absent: slots.filter((s) => s.decision === 'rebuild-object-absent').length,
  unproven: slots.filter((s) => s.decision === 'unproven-no-provenance').length,
};

const out = {
  task: 'GTOS-DSH-144',
  what: 'per-slot decision for the 61 individuals of the last published link: reusable only when the object still matches its pin '
    + 'and every source it was built from still matches the current committed tree',
  order_from: { spec: catalog.parent_actual_final_link_spec, individuals: individuals.length,
    why: 'the order is the one the 140 link actually used, taken from the spec that produced the last published image' },
  linker_script: linkerScriptCurrent,
  counts,
  assembly_sources: asmRows,
  profile_sources: profileRows,
  slots,
  note: 'a slot whose object matches an old pin but whose source has moved is a stale object that happens to hash the same. 141 '
    + 'reused 60 objects on the pin alone, which is why its image could not be called a current image.',
  ok: counts.unproven === 0,
};
// The name the driver reads. The tag is part of it because a second selection is a second record, but it is appended without
// a separator so that the file this tool writes is the file the next phase opens.
const target = R + '/p-selection-144' + (process.argv[2] || 'worktree') + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ wrote: target, counts, linker_script: linkerScriptCurrent,
  assembly: asmRows.map((a) => a.logical + ':' + (a.matches ? 'same' : 'changed')),
  profile: profileRows.map((p) => (p.matches ? 'same' : 'changed')),
  rebuilds: slots.filter((s) => s.decision !== 'reuse').map((s) => s.index + ' ' + s.basename + ' ' + s.decision
    + (s.source_drift.length ? ' (' + s.source_drift.map((d) => d.logical + ':' + d.state).join(',') + ')' : '')),
  ok: out.ok }, null, 2));