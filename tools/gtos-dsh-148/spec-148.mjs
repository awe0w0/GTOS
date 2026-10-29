// GTOS-DSH-148: the required set, and the spec the payload is planned from.
//
// THE REQUIRED SET IS BUILT FIRST, AND FROM THE CLOSURE. The engine refuses a required set that is not a closure: it must
// say that it was derived independently of the spec and independently of the payload, that it has a member count, and that
// no required member went unread. Those three claims are only worth anything if they are true of the order in which the
// files were written, so this file writes the required set before it writes the spec, and it does not read the spec while
// building the set. The 689 members come from the 147 closure's own required set -- the record the closure derivation
// produced, not the payload plan -- and each one is then re-resolved against the 148 package: a member the package cannot
// produce is a member that was required and not read, which is the number the closure block has to report honestly.
//
// THE SPEC COMES SECOND. It names every member of the payload with its role, its size and its hash; it pins the seven
// mandatory consumer roles by the records their consumers wrote, together with the subject each judged, the consumer version
// that judged it, the entry that was called and the obligations it decided; and it pins the evidence records the seal
// re-reads. It names no payload and no archive: the plan is a copy-plan over this spec, and the payload carries the plan.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const PKG = R + '/package-148';
const H = R + '/h-roles-148';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
const nextRecord = (series) => {
  for (let i = 1; i < 100; i += 1) {
    const p = R + '/' + series + '-' + String(i).padStart(2, '0') + '.json';
    if (!exists(p)) return p;
  }
  throw new Error('no free record name in the series ' + series);
};
const writeNew = (p, text) => {
  if (exists(p)) throw new Error('CreateNew refused: ' + p);
  fs.writeFileSync(p, text, { flag: 'wx' });
  const b = fs.readFileSync(p);
  return { path: p, bytes: b.length, sha256: sha(b) };
};

// ---------------------------------------------------------------- the required set
//
// Every member is a physical thing the packet requires. The role comes from the record that required it, not from the file
// extension, because a member is required by a role and not by being a `.o`.
export const buildRequiredSet = ({ root = PKG } = {}) => {
  const origin = readJson(root + '/control/required-set-rs147-01-147.json');
  const closure147 = readJson(root + '/control/d-closure-dc147-02-147.json');
  const members = origin.required_physical_set.members;
  const resolved = [];
  const notResolved = [];
  const byRole = {};
  for (const m of members) {
    const role = m.role;
    byRole[role] = (byRole[role] || 0) + 1;
    // The name is the 147 closure's own logical name. It is resolved against the 148 package by looking for the same
    // basename in the bucket the role implies, and a member that cannot be found is recorded as unread rather than dropped.
    const candidates = [];
    if (role === 'image' || role === 'image_map') candidates.push('material/image/' + path.posix.basename(m.name));
    else if (role === 'producer_object' || role === 'depfile') candidates.push('material/producers/' + path.posix.basename(m.name));
    else if (role === 'profile') candidates.push('material/profile/' + path.posix.basename(m.name));
    else if (role === 'runtime_file') candidates.push('material/' + m.name);
    else if (role === 'host_result') candidates.push('material/host/results/' + path.posix.basename(m.name));
    else if (role === 'host_inner_report') candidates.push('material/host/logs/' + path.posix.basename(m.name));
    else if (role === 'host_raw') candidates.push('material/host/logs/' + path.posix.basename(m.name));
    else if (role === 'host_unit_elf') candidates.push('material/host/elf/' + path.posix.basename(m.name));
    else if (role === 'guest_channel_e9' || role === 'guest_channel_serial' || role === 'guest_inner_report'
      || role === 'guest_raw') candidates.push('material/guest/' + path.posix.basename(m.name));
    else if (role === 'source_view') candidates.push('material/sources/' + m.name.replace(/^sources\//, ''));
    else candidates.push('material/' + m.name);
    const found = candidates.map((c) => root + '/' + c).find((p) => exists(p));
    if (!found) { notResolved.push({ role, name: m.name, why: m.why }); continue; }
    const b = fs.readFileSync(found);
    resolved.push({ role, name: m.name, in_package: found.slice(root.length + 1), bytes: b.length, sha256: sha(b) });
  }
  return {
    task: 'GTOS-DSH-148', stage: 'rs148-01', kind: 'required_set',
    what: 'every physical member the packet requires, each resolved against the package, with the role that required it',
    origin: { path: origin.task + ' required set', members: members.length,
      note: 'the member list comes from the closure derivation the 147 packet ran, which is a record about what is required '
        + 'and not a record about what a payload happens to contain' },
    closure_derivation: { path: 'control/d-closure-dc147-02-147.json', members: closure147.members.length,
      ok: closure147.ok === true },
    required_physical_set: { by_role: byRole, total: members.length, members: resolved },
    closure: {
      independent_of_spec: true,
      independent_of_spec_means: 'this record was written before any 148 spec existed and does not read one',
      independent_of_payload: true,
      independent_of_payload_means: 'this record names no payload, no archive and no plan',
      members: resolved.length,
      required_not_read: notResolved.length,
      not_read: notResolved,
    },
    ok: notResolved.length === 0,
    ok_means: 'every member the closure requires was found in the package and its bytes were read; a required member that '
      + 'could not be resolved would be counted in required_not_read and would refuse the seal',
  };
};

// ---------------------------------------------------------------- the members of the payload
const roleOf = (rel) => {
  const top = rel.split('/')[0];
  if (top === 'chain') return 'chain';
  if (top === 'control') return 'control';
  if (top === 'engine') return 'engine';
  if (top === 'material') return 'material';
  return 'other';
};

const buildSpec = (requiredSetPath) => {
  const index = readJson(H + '/index.json');
  const summary = readJson(H + '/summary.json');
  if (summary.ok !== true) throw new Error('the role harness is not green, so there is nothing to qualify: ' + JSON.stringify(summary.blocked || []));
  if (!Array.isArray(index.roles) || index.roles.length !== 7) {
    throw new Error('the role index does not name seven roles');
  }
  // The required set is pinned as an entry -- path, bytes, hash and the field that says it holds -- because that is the
  // shape the seal re-reads. It is not inlined: a spec that inlined the record would pin its content but not the file the
  // closure wrote, and the point of pinning is that the file is the thing.
  const rsBytes = fs.readFileSync(requiredSetPath);
  const requiredSetEntry = { path: requiredSetPath, bytes: rsBytes.length, sha256: sha(rsBytes), ok_field: 'ok' };
  const members = [];
  const walk = (dir, rel) => {
    for (const n of fs.readdirSync(dir).sort()) {
      const full = dir + '/' + n;
      const r = rel ? rel + '/' + n : n;
      const st = fs.statSync(full);
      if (st.isDirectory()) { walk(full, r); continue; }
      const b = fs.readFileSync(full);
      members.push({ path: r, role: roleOf(r), from: full, bytes: b.length, sha256: sha(b) });
    }
  };
  walk(PKG, '');
  // The role records and the judgements travel with the payload: a reader who has only the payload has to be able to see
  // what qualified it and what was decided about it.
  for (const role of index.roles) {
    const b = fs.readFileSync(role.path);
    members.push({ path: 'roles/' + path.basename(role.path), role: 'role_record', from: role.path, bytes: b.length,
      sha256: sha(b) });
  }
  // The records the package tree already carries are the same records this loop would add, because the package was built by
  // copying them. A payload cannot carry two members at one path, so the package's copy is replaced by the report root's
  // copy -- and only when the two are the same bytes, which is what makes "the same record" a fact rather than an assumption.
  const byPath = new Map(members.map((m) => [m.path, m]));
  const replaced = [];
  for (const name of fs.readdirSync(R).sort()) {
    // Every record of every judge's series, so a re-run of a judge is carried without this list being edited. The failed
    // records travel too: a payload that carried only the successes could not show what was found and fixed.
    if (!/^(f-negatives-148|f-qualification-148|c-negatives-148-\d+|c-chain-148-\d+|stage-148-\d+|required-set-148-\d+|setup-148-\d+|h-roles-summary-148)\.json$/.test(name)) continue;
    const b = fs.readFileSync(R + '/' + name);
    const entry = { path: 'judgements/' + name, role: 'judgement', from: R + '/' + name, bytes: b.length, sha256: sha(b) };
    const existing = byPath.get(entry.path);
    if (existing) {
      if (existing.sha256 !== entry.sha256) {
        throw new Error('the package and the report root disagree about ' + entry.path + ': ' + existing.sha256
          + ' against ' + entry.sha256 + '. One of them is stale, and a payload that carried either would be a payload that '
          + 'contradicted itself.');
      }
      replaced.push(entry.path);
    }
    byPath.set(entry.path, entry);
  }
  members.length = 0;
  members.push(...[...byPath.values()].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0)));
  // The evidence the seal re-reads. Each is the newest record of its series that says it held, chosen by reading the
  // records rather than by naming a number, because a spec that hard-coded `stage-148-04` would keep pinning a superseded
  // judgement after a later run had found something the earlier one did not look for.
  const newestHeld = (series, kind = null) => {
    const hits = fs.readdirSync(R).filter((n) => new RegExp('^' + series + '-\\d+\\.json$').test(n)).sort();
    const held = hits.filter((n) => {
      try {
        const j = readJson(R + '/' + n);
        return j.ok === true && (kind === null || j.kind === kind);
      } catch { return false; }
    });
    if (!held.length) throw new Error('no record of the series ' + series + (kind ? ' and kind ' + kind : '') + ' says it held');
    return held[held.length - 1];
  };
  const evidenceNames = [
    'f-negatives-148.json', 'f-qualification-148.json', newestHeld('c-chain-148'),
    newestHeld('c-negatives-148'), newestHeld('required-set-148'),
    newestHeld('stage-148', 'package_tree'), newestHeld('stage-148', 'package_tree_verified'),
  ];
  const evidence = [...new Set(evidenceNames)].map((n) => {
    const b = fs.readFileSync(R + '/' + n);
    return { path: R + '/' + n, bytes: b.length, sha256: sha(b), ok_field: 'ok' };
  });
  const spec = {
    task: 'GTOS-DSH-148',
    what: 'the members of the payload with their roles, the seven mandatory consumer roles with the records their consumers '
      + 'wrote, the required set, and the evidence the seal re-reads',
    members,
    mandatory_roles: {
      roles: index.roles.map((r) => ({
        role: r.role, path: r.path, bytes: r.bytes, sha256: r.sha256, ok_field: r.ok_field || 'ok',
        subject: r.subject, consumer_version: r.consumer_version, entry: r.entry,
        obligations: r.obligations_count,
      })),
    },
    required_set: requiredSetEntry,
    evidence,
  };
  return spec;
};

// ---------------------------------------------------------------- main
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) {
  const mode = process.argv[2] || 'required-set';
  if (mode === 'required-set') {
    const rec = buildRequiredSet();
    const out = nextRecord('required-set-148');
    const written = writeNew(out, JSON.stringify(rec, null, 2) + '\n');
    console.log(JSON.stringify({ verb: mode, required_set: written, members: rec.required_physical_set.total,
      resolved: rec.closure.members, required_not_read: rec.closure.required_not_read, by_role: rec.required_physical_set.by_role,
      ok: rec.ok }, null, 2));
    process.exit(rec.ok ? 0 : 1);
  }
  if (mode === 'spec') {
    const requiredSetPath = process.argv[3];
    const spec = buildSpec(requiredSetPath);
    const out = nextRecord('spec-148');
    const written = writeNew(out, JSON.stringify(spec, null, 2) + '\n');
    const byRole = {};
    for (const m of spec.members) byRole[m.role] = (byRole[m.role] || 0) + 1;
    console.log(JSON.stringify({ verb: mode, spec: written, members: spec.members.length, by_role: byRole,
      mandatory_roles: spec.mandatory_roles.roles.length, evidence: spec.evidence.length,
      required_set: spec.required_set.sha256 }, null, 2));
    process.exit(0);
  }
  console.error('usage: spec-148.mjs required-set | spec <required-set-record>');
  process.exit(2);
}
