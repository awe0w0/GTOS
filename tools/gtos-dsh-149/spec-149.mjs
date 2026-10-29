// GTOS-DSH-149: the required set, and the spec the payload is planned from.
//
// THE REQUIRED SET IS BUILT FIRST, AND FROM THE CLOSURE. The engine refuses a required set that is not a closure: it must
// say that it was derived independently of the spec and independently of the payload, that it has a member count, and that
// no required member went unread. Those three claims are only worth anything if they are true of the order in which the
// files were written, so this file writes the required set before it writes the spec, and it does not read the spec while
// building the set. The 689 members come from the 147 closure's own required set -- the record the closure derivation
// produced, not the payload plan -- and each one is then re-resolved against the package: a member the package cannot
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

const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const PKG = R + '/package-149';
const H = R + '/roles-149';

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
// THE FINDING THIS REPLACES. Until now this file answered "what does the packet require" by reading the 147 packet's own
// required-set record and copying its 689 members, then resolving each one by looking for a file of the same basename in the
// bucket the member's role suggested, and taking the hash of whatever it found. Three things were wrong with that, and each
// of them is the kind of wrongness that survives a green seal:
//
//   1. IT INHERITED THE ANSWER. The 689 members were 147's derivation, not this packet's. A member 149 needs and 147 did not
//      name could not appear; a member 149 no longer needs could not leave.
//   2. IT RESOLVED BY BASENAME. `arp.o` is a name. The 140 packet's arp.o is 3808 bytes and the 147 packet's is 2824, and a
//      resolver that picks a file by its name cannot tell them apart. So the member's declared bytes were ignored, and the
//      hash in the record was the hash of whatever the basename found.
//   3. IT READ THE CANDIDATE AND THEN WROTE ITS HASH. A member row whose sha256 is computed from the file it resolved to is
//      not a requirement, it is a description: it can never disagree with the package, because the package is what it was
//      measured against.
//
// What replaces it: the requirement is read from the contracts that state it, and each member is carried with the EXACT pin
// the contract states -- path, bytes, sha256 -- copied out of the contract, never computed here. Resolution then compares the
// pin with the package: the member must have those bytes and that hash, and a member whose bytes differ is a member the
// package does not hold. Nothing is looked up by basename.
//
// The sources are the ten prerequisite role records and the build's own records, because those are the records that say what
// the build required:
//
//   role-source_profile_contract  155 sources + the order spec
//   role-producer_chain           61 producer objects
//   role-dependency_chain         61 objects + 56 depfiles
//   role-link_input_consumption   the read set of the link
//   role-typed_terminal_validation the link's helper report
//   role-default_link             the image and its map
//   role-static_image             the image and its map
//   role-related_host_tests       the three unit ELFs
//   role-tooling_subject          the seven tool subjects
//   role-runtime_environment      the manifest and its verification record
//   p-build-147a                  the 61 link slots (12 rebuilt, 49 reused) with their objects
//   h-contract-147e               the three unit ELFs and their cases
//   g-matrix-gm147-01-147         the 28 trials: the record, the e9 channel, the serial channel
//
// A pin that names a thing outside the package on purpose -- the host Node binary and the WSL launcher, which are the
// transport and not the subject -- is classified `external_by_design` with the reason, and counted separately. It is never
// silently dropped and it is never counted as resolved.
const REQUIRED_SOURCES = [
  { role: 'source_view', record: 'role-source_profile_contract-147.json', at: 'sources[]', what: 'the source files the build compiled' },
  { role: 'profile', record: 'role-source_profile_contract-147.json', at: 'order_from', what: 'the order spec the producer contract was frozen against' },
  { role: 'producer_object', record: 'role-producer_chain-147.json', at: 'built[].object', what: 'the producer objects the link consumed' },
  { role: 'producer_object', record: 'role-dependency_chain-147.json', at: 'inputs[].object', what: 'the producer objects the dependency chain names' },
  { role: 'depfile', record: 'role-dependency_chain-147.json', at: 'inputs[].depfile', what: 'the depfiles those objects were built with' },
  { role: 'link_input', record: 'role-link_input_consumption-147.json', at: 'record.read_set.pins', what: 'the artifacts the link read' },
  { role: 'link_helper_report', record: 'role-typed_terminal_validation-147.json', at: 'helper_report', what: 'the link helper report the terminal validation is a claim about' },
  { role: 'image', record: 'role-static_image-147.json', at: 'elf', what: 'the default image' },
  { role: 'image_map', record: 'role-static_image-147.json', at: 'map', what: 'the default image map' },
  { role: 'image', record: 'role-default_link-147.json', at: 'elf', what: 'the image the default link produced' },
  { role: 'image_map', record: 'role-default_link-147.json', at: 'map', what: 'the map the default link produced' },
  { role: 'host_unit_elf', record: 'role-related_host_tests-147.json', at: 'units[].elf', what: 'the host unit ELFs' },
  { role: 'host_unit_elf', record: 'h-contract-147e.json', at: 'units[].elf', what: 'the host unit ELFs the host contract names' },
  { role: 'tool_subject', record: 'role-tooling_subject-147.json', at: 'subjects[]', what: 'the seven tool subjects' },
  { role: 'runtime_manifest', record: 'role-runtime_environment-147.json', at: 'manifest', what: 'the runtime materialisation manifest' },
  { role: 'runtime_verification', record: 'role-runtime_environment-147.json', at: 'verification_record', what: 'the record the runtime manifest is verified by' },
  { role: 'guest_record', record: 'g-matrix-gm147-01-147.json', at: 'trials[].record', what: 'the per-trial record of each guest trial' },
  { role: 'guest_channel_e9', record: 'g-matrix-gm147-01-147.json', at: 'trials[].e9', what: 'the e9 channel each trial printed' },
  { role: 'guest_channel_serial', record: 'g-matrix-gm147-01-147.json', at: 'trials[].serial', what: 'the serial channel each trial printed' },
  // The runtime supply. The manifest is the contract for it -- 206 files, each with a pin -- and the manifest is itself pinned
  // by role-runtime_environment above, so requiring its files is requiring what a pinned contract says.
  { role: 'runtime_file', from: 'material/role-artifacts/runtime-manifest-147.json', record: 'runtime-manifest-147.json',
    at: 'files[]', what: 'one file of the runtime supply the emulator was given' },
  // The profile: the two files that turned sources into objects, and the linker script and assembly sources the link used.
  // These state their pins as `pinned_bytes`/`pinned_sha256`, because the selection record carries a current measurement
  // beside each pin; the pin is what is read here.
  { role: 'profile', record: 'p-selection-147-a.json', at: 'profile_sources[]', what: 'the profile that turned sources into objects' },
  { role: 'linker_script', record: 'p-selection-147-a.json', at: 'linker_script', what: 'the linker script the image was linked with' },
  { role: 'assembly_source', record: 'p-selection-147-a.json', at: 'assembly_sources[]', what: 'an assembly source the producers compiled' },
];
// The per-stage requirements. Each is a family of records -- one per trial or per host case -- and each record states the pins
// for the two artifacts that belong to it. They are separate from the table above because the record they are read from is not
// one document but twenty-eight, or fourteen, and a reader has to be able to see which record said what.
const PER_STAGE_SOURCES = [
  { role: 'guest_inner_report', from: 'material/guest-records/<stage>.json', stages_from: 'guest', at: 'inner_report',
    what: 'the guard report of one guest trial, as that trial recorded it' },
  { role: 'guest_raw', from: 'material/guest-records/<stage>.json', stages_from: 'guest', at: 'raw',
    what: 'the raw child output of one guest trial, as that trial recorded it' },
  { role: 'host_inner_report', from: 'chain/<stage>/<stage>.report.json', stages_from: 'host', at: 'inner_report',
    what: 'the guard report of one host case, as that case recorded it' },
  { role: 'host_raw', from: 'chain/<stage>/<stage>.report.json', stages_from: 'host', at: 'raw',
    what: 'the raw child output of one host case, as that case recorded it' },
];
// The pins that name the transport rather than the subject. They are not package members and the packet does not pretend
// they are; the list is written out so the classification is a decision with a reason and not a filter that swallows whatever
// failed to resolve.
const EXTERNAL_BY_DESIGN = [
  { match: /(^|\/)node\.exe$/i, why: 'the host Node binary the runner executes under. It is the transport, not the subject of '
    + 'the build, and it is pinned so a reader can see which Node ran; it is not a member of the package' },
  { match: /(^|\/)wsl\.exe$/i, why: 'the host WSL launcher. Same reason: it is how the guard was started, not what was built' },
];

// Collect the pins a record states, at one named place. The pin is copied verbatim: `path`, `bytes`, `sha256` come out of the
// record exactly as the record wrote them.
const pinsAt = (doc, at) => {
  const out = [];
  const walk = (node, trail) => {
    if (!node || typeof node !== 'object') return;
    if (Array.isArray(node)) { node.forEach((x, i) => walk(x, trail + '[' + i + ']')); return; }
    // Two spellings are in use across the contracts: `bytes`/`sha256`, and `pinned_bytes`/`pinned_sha256` for a pin that is
    // carried beside a *current* measurement of the same thing. Both are a pin and both are read the same way; the second is
    // not a measurement of the package, it is the contract's own statement of what it required, and the `current_*` fields
    // next to it are the contract's check of itself, which is not this record's business.
    const sha256 = typeof node.sha256 === 'string' && /^[0-9a-f]{64}$/.test(node.sha256) ? node.sha256
      : (typeof node.pinned_sha256 === 'string' && /^[0-9a-f]{64}$/.test(node.pinned_sha256) ? node.pinned_sha256 : null);
    const bytes = typeof node.bytes === 'number' ? node.bytes
      : (typeof node.pinned_bytes === 'number' ? node.pinned_bytes : undefined);
    const isPin = sha256 !== null
      && (typeof node.path === 'string' || typeof node.name === 'string' || typeof node.linux_path === 'string'
        || typeof node.host_path === 'string' || typeof node.linux === 'string' || typeof node.logical === 'string');
    if (isPin) {
      out.push({ at: trail, path: node.path || node.name || node.linux_path || node.host_path || node.linux
        || node.logical, bytes, sha256, logical: node.logical || null });
    }
    for (const k of Object.keys(node)) walk(node[k], trail ? trail + '.' + k : k);
  };
  // `at` is a dotted path, with `[]` meaning "every element". A trailing `[]` on a key means the value is an array to walk.
  const parts = at.split('.');
  let nodes = [doc];
  for (const part of parts) {
    const every = part.endsWith('[]');
    const key = every ? part.slice(0, -2) : part;
    const next = [];
    for (const n of nodes) {
      if (!n || typeof n !== 'object') continue;
      const v = n[key];
      if (v === undefined) continue;
      if (every && Array.isArray(v)) next.push(...v);
      else if (every) next.push(v);
      else next.push(v);
    }
    nodes = next;
  }
  for (const n of nodes) walk(n, at);
  return out;
};

// The package, indexed by content. Resolution is a lookup of the pin's own sha256 in this index, and then a check that the
// candidate's byte count is the pin's byte count. Nothing here reads a file name: a pin resolves because the package holds
// bytes with that hash, and for no other reason. Where several members carry the same bytes, the count is recorded on the
// member row, so an ambiguity is visible rather than hidden.
const contentIndex = (root) => {
  const bySha = new Map();
  const walk = (dir, rel) => {
    for (const n of fs.readdirSync(dir).sort()) {
      const full = dir + '/' + n;
      const r = rel ? rel + '/' + n : n;
      if (fs.statSync(full).isDirectory()) { walk(full, r); continue; }
      const b = fs.readFileSync(full);
      const h = sha(b);
      if (!bySha.has(h)) bySha.set(h, []);
      bySha.get(h).push({ member: r, bytes: b.length });
    }
  };
  walk(root, '');
  return bySha;
};

// The files a payload spec, plan, archive or seal is written to. A derivation that opened one of these was not independent of
// the payload it claims to have been derived without, and the two closure flags are decided from the list of what it opened.
const isAPayloadSpec = (p) => /(^|\/)spec-149-\d+\.json$/.test(p) || /(^|\/)publication-context-\d*\.json$/.test(p);
const isAPayloadPlanOrSeal = (p) => /-payload\.tar$/.test(p) || /-seal\.json$/.test(p)
  || /publication-state-\d+\.jsonl$/.test(p) || /(^|\/)plan\.json$/.test(p);

export const buildRequiredSet = ({ root = PKG } = {}) => {
  // WHAT THIS DERIVATION OPENED.
  //
  // The engine refuses a required set whose closure does not state that it was derived independently of the spec and of the
  // payload. Those two statements used to live only in `derivation`, which the engine does not read, so the seal refused the
  // record -- and the refusal was right, because the record had never said it where the consumer looks.
  //
  // They are in the closure now, and they are MEASURED. `readFileSync` is wrapped for the duration of this function and every
  // path it opens is recorded; the two flags are decided from that list. A claim that this derivation read no spec is worth
  // nothing if nothing watched what it read, and the wrapper costs one array.
  const opened = [];
  const realRead = fs.readFileSync;
  fs.readFileSync = function watched(p, ...rest) {
    if (typeof p === 'string') opened.push(p.replace(/\\/g, '/'));
    return realRead.call(fs, p, ...rest);
  };
  const members = [];
  const unresolved = [];
  const external = [];
  const byRole = {};
  const bySource = [];
  const seen = new Set();
  const index = contentIndex(root);
  // A member is identified by the path the contract names for it, not by its content. Twenty-eight trials whose raw output
  // happens to be the same bytes are twenty-eight members and all twenty-eight are required: deduplicating by content would let
  // a package that dropped twenty-seven of them pass, which is a narrowing and not a resolution. The content check is separate
  // and is not weakened by this: a pin still resolves only if the package holds those bytes.
  const dedupeKey = (role, p) => role + '|' + String(p.path);

  // Every group is one record -- or one family of records -- and the place inside it where the requirement is stated. A
  // group is read, its pins are taken, and each pin is compared with the package. The loop below is the same for all of them,
  // so a family of twenty-eight records cannot be judged by a laxer rule than a single document.
  const groups = [];
  for (const s of REQUIRED_SOURCES) {
    const at = s.from || ('control/' + s.record);
    groups.push({ role: s.role, what: s.what, atPath: at, at: s.at, record: s.record, from: s.from || null });
  }
  // The per-stage families. The stage names come from the records that declare them -- the matrix's trials for the guest, the
  // host contract's cases for the host -- so the family is the set of stages the contracts name, not a glob over a directory.
  const stageNames = {
    guest: () => {
      const m = readJson(root + '/control/g-matrix-gm147-01-147.json');
      return m.trials.map((t) => t.stage);
    },
    host: () => {
      const h = readJson(root + '/control/h-contract-147e.json');
      const out = [];
      for (const u of h.units) for (const c of u.cases) out.push(c.stage);
      return out;
    },
  };
  const stageCache = {};
  for (const s of PER_STAGE_SOURCES) {
    const stages = stageCache[s.stages_from] || (stageCache[s.stages_from] = stageNames[s.stages_from]());
    for (const stage of stages) {
      groups.push({ role: s.role, what: s.what, atPath: s.from.replace(/<stage>/g, stage), at: s.at,
        record: s.from.replace(/<stage>/g, stage), stage, from: s.from.replace(/<stage>/g, stage) });
    }
  }

  for (const g of groups) {
    const recordPath = root + '/' + g.atPath;
    if (!exists(recordPath)) {
      unresolved.push({ role: g.role, record: g.atPath, why: 'the record that states this requirement is not in the package' });
      bySource.push({ record: g.atPath, at: g.at, role: g.role, pins: 0, resolved: 0, missing_record: true });
      continue;
    }
    const doc = readJson(recordPath);
    const pins = pinsAt(doc, g.at);
    let resolved = 0;
    let externals = 0;
    let duplicated = 0;
    for (const p of pins) {
      // The transport, named and reasoned about rather than resolved.
      const ext = EXTERNAL_BY_DESIGN.find((e) => e.match.test(String(p.path)));
      if (ext) {
        externals += 1;
        external.push({ role: g.role, record: g.atPath, at: p.at, path: p.path, bytes: p.bytes, sha256: p.sha256,
          why: ext.why });
        continue;
      }
      const key = dedupeKey(g.role, p);
      if (seen.has(key)) { duplicated += 1; continue; }
      seen.add(key);
      byRole[g.role] = (byRole[g.role] || 0) + 1;
      // Resolution compares the pin with the package: a member whose content hash is the pin's hash and whose byte count is
      // the pin's byte count. It never asks what a file is called, and it never computes a hash and calls it the requirement
      // -- the hash in the member row is the contract's, and the comparison is what this line does.
      const candidates = index.get(p.sha256) || [];
      const match = candidates.find((c) => c.bytes === p.bytes);
      if (match) {
        resolved += 1;
        members.push({ role: g.role, required_by: g.atPath + ' ' + g.at, stage: g.stage || null, path: p.path,
          bytes: p.bytes, sha256: p.sha256, in_package: match.member, pin_matches: true,
          members_carrying_these_bytes: candidates.length });
        continue;
      }
      unresolved.push({ role: g.role, record: g.atPath, at: p.at, path: p.path, bytes: p.bytes, sha256: p.sha256,
        why: candidates.length
          ? 'the package holds these bytes but not with the byte count the pin states'
          : 'no member of the package carries these bytes' });
    }
    bySource.push({ record: g.atPath, at: g.at, role: g.role, what: g.what, stage: g.stage || null, pins: pins.length,
      resolved, external_by_design: externals, already_named: duplicated });
    // A group that states no pin at all is not a requirement that was satisfied, it is a contract that no longer says what it
    // is read for. That is a failure of this derivation and it is counted as one, so a record quietly emptied of its pins cannot
    // make the required set smaller and still come out green.
    if (pins.length === 0) {
      unresolved.push({ role: g.role, record: g.atPath, at: g.at,
        why: 'the record states no pin at ' + g.at + ', so this requirement cannot be read from it' });
    }
  }
  // What the resolution actually achieved, stated as numbers rather than left to be inferred from `members`. A pin is a
  // requirement and a member is a file, and the two counts differ wherever a contract pins the same bytes twice -- the two role
  // records that pin the same unit ELF under two path spellings, or the twenty-eight guest trials whose raw output is one byte
  // string repeated. Reporting only the pin count would let a reader believe the package holds twenty-eight distinct files when
  // it holds one; reporting only the member count would let a package that dropped twenty-seven of the twenty-eight trial
  // records look complete. Both are stated.
  const distinctMembers = new Set(members.map((m) => m.in_package));
  const sharedBySha = {};
  for (const m of members) {
    const k = m.sha256;
    if (!sharedBySha[k]) sharedBySha[k] = { sha256: k, bytes: m.bytes, pins: 0, members: new Set() };
    sharedBySha[k].pins += 1;
    sharedBySha[k].members.add(m.in_package);
  }
  const contentShared = Object.values(sharedBySha).filter((v) => v.pins > 1)
    .map((v) => ({ sha256: v.sha256, bytes: v.bytes, pins: v.pins, package_members: [...v.members].sort() }));
  fs.readFileSync = realRead;
  return {
    task: 'GTOS-DSH-149', kind: 'required_set',
    what: 'every physical member the build required, each read from the contract that states the requirement and carried with '
      + 'the exact pin that contract states -- path, bytes and sha256 copied from the record, never computed from the package',
    derivation: {
      method: 'read the ten prerequisite role records and the build records, take the pins they state, and compare each pin '
        + 'with the package. No member is looked up by basename, and no requirement hash is taken from the file it resolves to',
      sources: bySource,
      // The same two decisions the closure carries, repeated here so a reader who looks at `derivation` is not told something
      // the closure measured differently. They are computed once, from `opened`, and both places read the same expression.
      independent_of_spec: !opened.some(isAPayloadSpec),
      independent_of_spec_means: 'this record is derived from the build contracts, not from a payload spec, and `closure.opened` '
        + 'is the list of what it actually opened',
      independent_of_payload: !opened.some(isAPayloadPlanOrSeal),
      independent_of_payload_means: 'it names no payload, no archive, no plan and no seal, and opened none of them',
      not_inherited_from_147: true,
      not_inherited_from_147_means: 'the 147 required-set record is not read. Its member list is 147\'s derivation; this one '
        + 'is derived from the contracts, which is why it carries 28 guest records and 9 origin-chain artifacts that the 147 '
        + 'list does not name',
    },
    resolution: {
      pins: members.length,
      distinct_package_members: distinctMembers.size,
      pins_sharing_content: contentShared.reduce((n, c) => n + (c.pins - 1), 0),
      content_shared_between_pins: contentShared,
      note: 'a pin resolves because the package holds those bytes, so where several pins state the same bytes they resolve to '
        + 'the same member. That is stated here rather than hidden: it is why the pin count and the member count differ, and a '
        + 'reader who needs to know whether the package holds twenty-eight trial records or one should read this block',
    },
    required_physical_set: { by_role: byRole, total: members.length, members },
    external_by_design: { total: external.length, members: external,
      note: 'pins that name the transport rather than the subject. They are not package members, they are counted here so the '
        + 'count is a stated number and not an omission' },
    closure: {
      members: members.length,
      required_not_read: unresolved.length,
      not_read: unresolved,
      // The two statements the engine reads. Both are decided from `opened`, not asserted.
      independent_of_spec: !opened.some(isAPayloadSpec),
      independent_of_payload: !opened.some(isAPayloadPlanOrSeal),
      independent_of_means: 'the derivation opened none of the files a payload spec, plan, archive or seal is written to. closure.opened is the list it did open, so the statement can be rechecked rather than believed',
      opened: [...new Set(opened)].sort(),
    },
    ok: unresolved.length === 0 && members.length > 0,
    ok_means: 'every pin the build contracts state either resolved to a package member with exactly those bytes, or names the '
      + 'transport by design and is counted as external. A pin that names bytes the package does not hold would be counted in '
      + 'required_not_read and would refuse the seal',
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
    if (!/^(f-negatives-149|f-qualification-149|c-negatives-149-\d+|c-chain-149-\d+|stage-149-\d+|required-set-149-\d+|setup-149-\d+|t-prerequisites-149-\d+)\.json$/.test(name)) continue;
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
  // records rather than by naming a number, because a spec that hard-coded `stage-149-04` would keep pinning a superseded
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
    'f-negatives-149.json', 'f-qualification-149.json', newestHeld('c-chain-149'),
    newestHeld('c-negatives-149'), newestHeld('required-set-149'),
    newestHeld('stage-149', 'package_tree'), newestHeld('stage-149', 'package_tree_verified'),
  ];
  const evidence = [...new Set(evidenceNames)].map((n) => {
    const b = fs.readFileSync(R + '/' + n);
    return { path: R + '/' + n, bytes: b.length, sha256: sha(b), ok_field: 'ok' };
  });
  const spec = {
    task: 'GTOS-DSH-149',
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
    const out = nextRecord('required-set-149');
    const written = writeNew(out, JSON.stringify(rec, null, 2) + '\n');
    console.log(JSON.stringify({ verb: mode, required_set: written, members: rec.required_physical_set.total,
      resolved: rec.closure.members, required_not_read: rec.closure.required_not_read, by_role: rec.required_physical_set.by_role,
      ok: rec.ok }, null, 2));
    process.exit(rec.ok ? 0 : 1);
  }
  if (mode === 'spec') {
    const requiredSetPath = process.argv[3];
    const spec = buildSpec(requiredSetPath);
    const out = nextRecord('spec-149');
    const written = writeNew(out, JSON.stringify(spec, null, 2) + '\n');
    const byRole = {};
    for (const m of spec.members) byRole[m.role] = (byRole[m.role] || 0) + 1;
    console.log(JSON.stringify({ verb: mode, spec: written, members: spec.members.length, by_role: byRole,
      mandatory_roles: spec.mandatory_roles.roles.length, evidence: spec.evidence.length,
      required_set: spec.required_set.sha256 }, null, 2));
    process.exit(0);
  }
  console.error('usage: spec-149.mjs required-set | spec <required-set-record>');
  process.exit(2);
}
