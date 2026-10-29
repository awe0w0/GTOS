// GTOS-DSH-149 T: the prerequisite role, executed through the original ten-role consumer.
//
// THE FINDING THIS FILE ANSWERS. The 148 review's first item was PREREQUISITE_ROLE_REPLACED_BY_REQUIRED_SET_READER: the role
// named `prerequisite_full` called `spec.buildRequiredSet`, which rebuilds a list of physical members and hashes them. That is
// the D question -- "is the required set a closure of the package" -- and it is a real question, but it is not T. T is the
// ten prerequisite roles of the build that produced the image, each read from the artifact it is a claim about, and then
// bound into one set. A role that clears a file inventory has not decided whether the source contract, the producer chain, the
// dependency chain, the link input consumption, the typed terminal, the default link, the static image, the related host
// tests, the tooling subject and the runtime environment are all about the same build.
//
// WHAT IS CALLED. `admitPrerequisiteSet` from the 147 consumer: it walks the ten frozen roles, calls `consumeRole` for each
// (which opens the artifacts the role is a claim about and re-derives every pin from bytes it reads itself), and then calls
// `bindSet`, which refuses a set whose members describe different contracts, images or tool generations. This module does not
// re-implement any of that. It assembles the ten members and the context the consumer needs, calls it, and reports what it
// decided. `buildRequiredSet` stays available in `spec-149.mjs` as the D sub-step; it is simply no longer the T answer.
//
// WHY THE ALIAS MAP IS RESOLVED BY PIN AND NOT BY PATH. A role record names the absolute paths its producer wrote, and those
// paths are not where the package keeps the bytes: the package keeps them under `material/`, and the producer's root is a
// different tree that a later reader may not have and must not depend on. The wrong fix is to rewrite the record's paths -- a
// record whose paths were rewritten is no longer the record the producer wrote, and the copy would then be checked against a
// description of itself. The other wrong fix is to guess the member from the basename, which maps `arp.o` to whichever
// `arp.o` is found first and proves nothing.
//
// So the mapping is resolved from the pin the record itself carries: for every (path, bytes, sha256) the record names, the
// member of the package with exactly those bytes is the member that path means. That is an exact match on content, not a
// guess from a name, and it is one-directional -- the record's pin is the search key, so a drifted package member cannot be
// used to justify itself. The consumer then re-reads the mapped member and compares it against the same pin again, so a wrong
// mapping fails rather than passes. `unresolved` lists every pin the package does not carry, and a non-empty list is a
// refusal: a role whose artifact is not in the package has not been decided on the package's bytes.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
const mod = async (p) => import(pathToFileURL(p).href);
const norm = (p) => String(p === null || p === undefined ? '' : p).replace(/\\/g, '/');

// The ten roles, in the frozen order the consumer declares. Named here only so this file can report a per-role row; the
// consumer holds the authoritative list and refuses a set that omits one, so this list cannot be used to shrink the set.
export const TEN = ['source_profile_contract', 'producer_chain', 'dependency_chain', 'link_input_consumption',
  'typed_terminal_validation', 'default_link', 'static_image', 'related_host_tests', 'tooling_subject', 'runtime_environment'];

export const recordNameFor = (role) => 'control/role-' + role + '-147.json';

// Every (path, bytes, sha256) a record names, anywhere in it. The walk is generic on purpose: a role that starts naming a
// new artifact does not silently escape the alias map, because the map is built from the pins the records carry rather than
// from a hand-kept list of fields.
export const pinsOf = (record) => {
  const out = [];
  const seen = new Set();
  const walk = (v, where) => {
    if (v === null || typeof v !== 'object') return;
    if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) walk(v[i], where + '[' + i + ']'); return; }
    if (typeof v.path === 'string' && v.path && typeof v.sha256 === 'string' && /^[0-9a-f]{64}$/.test(v.sha256)) {
      const key = norm(v.path) + '|' + v.sha256;
      if (!seen.has(key)) { seen.add(key); out.push({ where, path: v.path, bytes: typeof v.bytes === 'number' ? v.bytes : null, sha256: v.sha256 }); }
    }
    for (const k of Object.keys(v)) walk(v[k], where + '.' + k);
  };
  walk(record, '');
  return out;
};

// The index of package members by content. It is built by hashing the tree, not by reading the plan: the question "which
// member holds these bytes" is a question about the package, and a plan row that disagreed with the tree would answer it
// wrongly. Two members with the same bytes are both valid answers and both are recorded.
//
// THE PACKET'S OWN RECORD DIRECTORY IS NOT PART OF THE INDEX, AND THAT IS A DEPENDENCY RATHER THAN A PREFERENCE.
//
// `judgements/` holds the records this packet wrote ABOUT the payload, and those records are members of it. A role record is
// written after the tree exists, so if the index covered that directory then each role's answer would depend on the bytes of
// records written after it -- the answer would not be a function of the payload it judges. Concretely: the seven roles are
// executed against a tree, their records are collected into the tree, and the post-seal receipt re-executes the roles against
// the sealed payload. Under a self-including index the second execution would read a different tree and could never reproduce
// the bytes that were sealed, so the receipt could never pass. A record that cannot be reproduced cannot be checked, which is
// the whole reason the receipt exists.
//
// So the index covers the payload's material -- the trees the contracts pin -- and skips `judgements/`.
//
// THE SKIP IS A STATEMENT, NOT A MEASUREMENT, AND THAT DISTINCTION IS LOAD-BEARING. A first version of this file reported the
// skipped subtree's member and byte counts, which meant stat-ing every file in it. Those stat calls land in the caller's
// trace, so the role's record then depended on how many records happened to be in `judgements/` when it ran -- the same
// irreproducibility the skip exists to remove, reintroduced one layer down. So the record says what was skipped and why, and
// does not count it. Nothing is hidden by that: the members of `judgements/` are enumerated by the packet's own `collect`
// record, which is a member of the tree.
//
// This cannot hide an unresolved pin. A pin is resolved by finding its content in the index; a pin naming a member of the
// skipped directory would simply not be found and the run would refuse with that pin listed as unresolved. The skip narrows
// what a pin may resolve TO; it does not narrow what the roles DECIDE.
export const RECORD_ROOT = 'judgements';
export const memberIndex = (pkg) => {
  const bySha = new Map();
  let files = 0;
  let bytes = 0;
  const excluded = { subtree: RECORD_ROOT, counted: false,
    why: 'the records this packet wrote about the payload are members of it, so indexing them would make the answer to each '
      + 'role depend on records written after it' };
  const walk = (d, rel) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const full = d + '/' + e.name;
      const r = rel ? rel + '/' + e.name : e.name;
      if (r === RECORD_ROOT) continue;
      if (e.isDirectory()) { walk(full, r); continue; }
      const b = fs.readFileSync(full);
      files += 1;
      bytes += b.length;
      const h = sha(b);
      if (!bySha.has(h)) bySha.set(h, []);
      bySha.get(h).push(r);
    }
  };
  walk(pkg, '');
  for (const [k, v] of bySha) bySha.set(k, v.slice().sort());
  return { bySha, files, bytes, excluded };
};

// The alias map, plus what could not be resolved. `ambiguous` is not a failure -- two members with identical bytes are the
// same bytes, so the pin is satisfied either way -- but it is reported, because a reader should be able to see that the
// mapping was not unique and why that did not matter.
export const aliasesFor = ({ pkg, pins, bySha }) => {
  const aliases = {};
  const resolved = [];
  const unresolved = [];
  const ambiguous = [];
  const add = (from, to) => {
    const k = norm(from);
    if (aliases[k] === undefined) aliases[k] = to;
    else if (aliases[k] !== to) ambiguous.push({ from: k, a: aliases[k], b: to });
  };
  for (const pin of pins) {
    const hits = bySha.get(pin.sha256) || [];
    if (hits.length === 0) { unresolved.push({ path: pin.path, sha256: pin.sha256, where: pin.where }); continue; }
    const to = pkg + '/' + hits[0];
    add(pin.path, to);
    // The same file spelled in the other path domain. Both spellings point at one member, so which one a record used cannot
    // change the outcome, and a record written in either domain resolves.
    const win = /^([A-Za-z]):\/(.*)$/.exec(norm(pin.path));
    if (win) add('/mnt/' + win[1].toLowerCase() + '/' + win[2], to);
    const mnt = /^\/mnt\/([a-z])\/(.*)$/.exec(norm(pin.path));
    if (mnt) add(mnt[1].toUpperCase() + ':/' + mnt[2], to);
    resolved.push({ path: pin.path, member: hits[0], candidates: hits.length });
    if (hits.length > 1) ambiguous.push({ from: norm(pin.path), candidates: hits });
  }
  return { aliases, resolved, unresolved, ambiguous };
};

// The pins a role record names are not the end of the story: an artifact the record points at can name further artifacts of
// its own. The runtime manifest is the case that matters -- the role pins the manifest, and the manifest is a list of two
// hundred and six files, none of which the role record names. A map built only from the records would leave every one of
// those reads pointing at the producer's tree, and the runtime role would be decided on files the package does not have.
//
// WHICH DOCUMENTS EXPAND. Only the ones the consumer actually iterates. That is a fact about the consumer, not a preference:
// `consumeRole` opens the manifest and walks `files[]`, and for every other artifact it compares a pin and reads no further.
// So a document expands only when it is a file list -- a top-level `files` array of {path, bytes, sha256} rows. Expanding
// everything that parses as JSON was tried first and is wrong in a way worth recording: the 147 contract names the host's
// node.exe, wsl.exe and a staging script, none of which are in any package and none of which the consumer ever opens, so an
// unrestricted closure reported eleven artifacts "missing" from a package that was complete for the question being asked.
// A closure that demands bytes nobody reads does not make the answer stronger; it makes it unanswerable, and the temptation
// is then to weaken the check until it passes. The scope is stated here instead.
//
// The closure terminates because a member expands once: the frontier is keyed by the member a pin resolved to, not by the
// path it was spelled with, so a record naming the same file in two domains expands it once.
export const isFileList = (doc) => {
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) return false;
  if (!Array.isArray(doc.files) || doc.files.length === 0) return false;
  return doc.files.every((f) => f && typeof f === 'object' && typeof f.path === 'string'
    && typeof f.sha256 === 'string' && /^[0-9a-f]{64}$/.test(f.sha256));
};

export const pinClosure = ({ pkg, seeds, bySha, limit = 200000 }) => {
  const pins = [];
  const expanded = new Set();
  const notExpanded = [];
  const layers = [];
  let frontier = seeds.map((p) => ({ ...p }));
  for (let depth = 0; depth < 12 && frontier.length; depth++) {
    const next = [];
    for (const pin of frontier) {
      pins.push(pin);
      const hits = bySha.get(pin.sha256) || [];
      if (hits.length === 0) continue;
      const member = hits[0];
      if (expanded.has(member)) continue;
      expanded.add(member);
      let body = null;
      try { body = fs.readFileSync(pkg + '/' + member); } catch { continue; }
      if (!/\.json$/.test(member)) { notExpanded.push({ member, why: 'not a JSON document' }); continue; }
      let parsed = null;
      try { parsed = JSON.parse(body.toString('utf8').replace(/^\uFEFF/, '')); } catch { notExpanded.push({ member, why: 'not JSON' }); continue; }
      if (!isFileList(parsed)) { notExpanded.push({ member, why: 'not a file list, so the consumer reads no pins from it' }); continue; }
      for (const p of pinsOf(parsed)) next.push({ ...p, where: member + p.where, depth: depth + 1 });
      if (pins.length + next.length > limit) break;
    }
    layers.push({ depth, pins: frontier.length, next: next.length });
    frontier = next;
  }
  return { pins, expanded: expanded.size, notExpanded, layers };
};

// The entry. `pkg` is the package tree and `engine` the directory holding the consumers. There is no plan argument: the
// mapping from a record's path to a member is decided by the bytes the package holds, so a plan that named a member wrongly
// could not change what the consumer reads.
export const judge = async ({ pkg, engine, ledger }) => {
  const S = await mod(engine + '/references/scene-admission-147.mjs');

  // The ten members. Each is {role, path, sha256}. The path is the package member; the record inside is what the consumer
  // reads, and its own hash is re-derived here so a record that changed after the tree was built is refused.
  const members = [];
  const absent = [];
  const records = [];
  for (const role of TEN) {
    const rel = recordNameFor(role);
    const p = pkg + '/' + rel;
    if (!exists(p)) { absent.push({ role, member: rel }); continue; }
    const b = fs.readFileSync(p);
    const record = JSON.parse(b.toString('utf8').replace(/^\uFEFF/, ''));
    members.push({ role, path: p, sha256: sha(b), bytes: b.length });
    records.push({ role, record });
  }
  if (absent.length) {
    return { ok: false, why: 'the package does not carry every one of the ten role records', absent };
  }

  // The alias map, from the pins the ten records carry, closed over the pins of the artifacts those pins resolve to.
  const index = memberIndex(pkg);
  const bySha = index.bySha;
  const seeds = [];
  const perRolePins = {};
  for (const { role, record } of records) {
    const pins = pinsOf(record);
    perRolePins[role] = pins.length;
    for (const p of pins) seeds.push({ ...p, where: role + p.where, depth: 0 });
  }
  const closure = pinClosure({ pkg, seeds, bySha });
  const alias = aliasesFor({ pkg, pins: closure.pins, bySha });
  // THE SECOND SOURCE OF ALIASES: the spellings the compiler actually used.
  //
  // The pins in the ten role records are spelled in the packet's own path domain -- a worktree path, or a staged view. The
  // depfiles the dependency_chain role is a claim about are spelled the way the compiler wrote them, and those spellings are
  // different: the same file appears as `.../wave-01/include/net/arp.h` in a depfile and as `.../worktree-147/include/net/
  // arp.h` in the source contract. A map built only from the pins leaves sixteen of those spellings unmapped, and the consumer
  // then reads them from disk -- which in this packet means reading the 147 tree the payload exists to replace. That was
  // measured: 1184 reads landed outside the package and 564 of them landed in the forbidden 147 tree.
  //
  // The spellings are not guessed. The 147 closure record states, for each of its 785 members, the exact path the producer used
  // (`src_linux`) beside the bytes that were there (`sha256`). So each spelling is taken from a record that already states it,
  // and it is aliased to the packaged member whose content hash is the hash that record states. A spelling whose bytes the
  // package does not hold is not aliased and is counted, so an alias can never be a guess that happens to point at something.
  const closureAliases = { added: 0, unresolved: [], source: 'control/d-closure-dc147-02-147.json' };
  const closurePath = pkg + '/control/d-closure-dc147-02-147.json';
  if (exists(closurePath)) {
    const dc = JSON.parse(fs.readFileSync(closurePath, 'utf8').replace(/^\uFEFF/, ''));
    const addAlias = (from, to) => {
      const k = norm(from);
      if (alias.aliases[k] === undefined) { alias.aliases[k] = to; closureAliases.added += 1; }
      else if (alias.aliases[k] !== to) closureAliases.unresolved.push({ from: k, why: 'two members claim this spelling' });
    };
    for (const m of dc.members || []) {
      if (!m || typeof m.src_linux !== 'string' || typeof m.sha256 !== 'string') continue;
      const member = (bySha.get(m.sha256) || [])[0];
      if (!member) { closureAliases.unresolved.push({ from: m.src_linux, sha256: m.sha256, why: 'the package holds no member with these bytes' }); continue; }
      const to = pkg + '/' + member;
      addAlias(m.src_linux, to);
      // The same file in the other path domain, for the same reason the pin aliases carry both.
      if (m.src_linux.startsWith('/mnt/')) {
        const t = /^\/mnt\/([a-z])\/(.*)$/.exec(m.src_linux);
        if (t) addAlias(t[1].toUpperCase() + ':/' + t[2], to);
      } else if (m.src_linux.startsWith('/')) {
        addAlias('//wsl$/Ubuntu-20.04' + m.src_linux, to);
      }
    }
  }
  if (alias.unresolved.length) {
    return {
      ok: false,
      why: 'the package does not carry every artifact the ten roles pin, directly or through an artifact they name, '
        + 'so the roles cannot be decided on the package',
      unresolved_count: alias.unresolved.length,
      unresolved: alias.unresolved.slice(0, 12),
      pins_named: closure.pins.length,
      closure_layers: closure.layers,
      documents_not_expanded: closure.notExpanded.length,
    };
  }

  // The two artifacts the consumer takes from the caller rather than from a record, resolved the same way -- by the pin the
  // record carries -- so the file supplied is the file the record is about and not whatever happens to be on this host.
  const contractPin = records[0].record.contract_sha256;
  const contractMembers = bySha.get(contractPin) || [];
  if (contractMembers.length === 0) {
    return { ok: false, why: 'the package carries no member with the contract hash the source_profile_contract role is filed for',
      contract_sha256: contractPin };
  }
  const contractPath = pkg + '/' + contractMembers[0];
  const runnerSubject = (records.find((r) => r.role === 'tooling_subject').record.subjects || [])
    .find((s) => s && s.role === 'runner');
  if (!runnerSubject || typeof runnerSubject.sha256 !== 'string') {
    return { ok: false, why: 'the tooling_subject record names no runner, so no runner can be supplied to the consumer' };
  }
  const runnerMembers = bySha.get(runnerSubject.sha256) || [];
  if (runnerMembers.length === 0) {
    return { ok: false, why: 'the package carries no member with the runner hash the tooling_subject role pins',
      runner_sha256: runnerSubject.sha256 };
  }
  const runnerPath = pkg + '/' + runnerMembers[0];

  const ledgerPath = ledger || (pkg + '/control/producer-order-147.jsonl');
  if (!exists(ledgerPath)) return { ok: false, why: 'the package carries no order ledger, so "frozen before build" cannot be checked', ledger: ledgerPath };

  const ctx = { ledger_path: ledgerPath, contract_path: contractPath, runner_path: runnerPath, aliases: alias.aliases };
  const verdict = S.admitPrerequisiteSet(members, ctx);

  const byRole = new Map((verdict.roles || []).map((r) => [r.role, r]));
  const rows = TEN.map((role) => {
    const r = byRole.get(role);
    const fact = r && r.fact ? r.fact : null;
    return {
      role,
      present: !!r,
      kind: r ? r.kind : null,
      bytes: r ? r.bytes : null,
      sha256: r ? r.sha256 : null,
      pins_named: perRolePins[role] || 0,
      fact_ok: !!(fact && fact.ok),
      why: fact && fact.ok === false ? fact.why : null,
      decided: fact && fact.detail ? Object.keys(fact.detail) : [],
    };
  });

  // Every role has to have produced a fact, and every fact has to be true. `complete` from the consumer already folds the
  // per-role facts and the binding, but the two are reported separately so a report can say which of them failed.
  const facts = rows.filter((r) => r.fact_ok).length;
  const binding = verdict.binding || null;
  return {
    ok: verdict.complete === true && facts === TEN.length && !!(binding && binding.bound === true),
    entry: 'admitPrerequisiteSet -> consumeRole x10 -> bindSet',
    consumer: engine + '/references/scene-admission-147.mjs',
    consumer_version: sha(fs.readFileSync(engine + '/references/scene-admission-147.mjs')),
    underlying: { consumeRole: engine + '/references/prerequisite-consumption-147.mjs',
      consumeRole_version: sha(fs.readFileSync(engine + '/references/prerequisite-consumption-147.mjs')) },
    roles_required: TEN.length,
    roles_that_produced_a_fact: facts,
    roles: rows,
    binding: binding ? { bound: binding.bound, contract: binding.contract, image: binding.image, subject: binding.subject,
      named: binding.named, problems: binding.problems } : null,
    complete: verdict.complete === true,
    problems: verdict.problems || [],
    supplied: { contract: { path: contractPath, sha256: contractPin, candidates: contractMembers.length },
      runner: { path: runnerPath, sha256: runnerSubject.sha256, candidates: runnerMembers.length },
      ledger: { path: ledgerPath } },
    aliases: { pins_named: closure.pins.length, distinct_paths: Object.keys(alias.aliases).length,
      resolved: alias.resolved.length, ambiguous: alias.ambiguous.length,
      unresolved: alias.unresolved.length, ambiguous_rows: alias.ambiguous.slice(0, 6),
      // The spellings taken from the closure record rather than from the pins, and the ones the package could not answer.
      closure_spellings: { added: closureAliases.added, unresolved: closureAliases.unresolved.length,
        source: closureAliases.source, unresolved_rows: closureAliases.unresolved.slice(0, 8) },
      closure_layers: closure.layers, members_expanded: closure.expanded,
      documents_not_expanded: closure.notExpanded,
      index: { members: index.files, bytes: index.bytes, distinct_contents: bySha.size,
        excluded_records: index.excluded },
      // The consumer builds a fresh reader per role and records its own use counts on the ctx it was handed, so the field
      // it leaves behind is the last role's. It is carried for completeness and is not the measurement this file reports:
      // the measurement is the closure above, which is counted here and covers every role.
      last_role_reader: ctx.alias_use ? { supplied: ctx.alias_use.supplied, used: ctx.alias_use.used,
        unused: ctx.alias_use.unused.length } : null },
    subject_of: binding && binding.image ? binding.image : null,
  };
};

// ---------------------------------------------------------------- the record
//
// Written to the first free name of the series, so a re-run adds a record rather than replacing one. The record is what the
// role index reads, so its shape is the role's answer and nothing else: what was called, what each of the ten roles decided,
// what the set bound to, and whether the reader had to reach outside the package to decide any of it.
const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const nextRecord = (stem) => {
  for (let i = 1; i < 1000; i++) {
    const p = R + '/' + stem + '-' + String(i).padStart(2, '0') + '.json';
    if (!exists(p)) return p;
  }
  throw new Error('no free record name for ' + stem);
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) {
  const pkg = process.argv[2] || (R + '/package-149');
  const engine = process.argv[3] || (R + '/worktree-149/tools/gtos-dsh-149/engine');
  const verdict = await judge({ pkg, engine });
  const record = {
    task: 'GTOS-DSH-149', kind: 'prerequisite_roles',
    what: 'the ten prerequisite roles of the build that produced the image, each decided by the original consumer against the '
      + 'artifact it is a claim about, and then bound into one set',
    entry: verdict.entry,
    package: pkg,
    ok: verdict.ok,
    ok_means: 'all ten roles were opened by the original ten-role consumer, each produced a true fact from the package bytes, '
      + 'and bindSet accepted the set as one claim about one contract, one image and one tool generation',
    verdict,
  };
  const p = nextRecord('t-prerequisites-149');
  fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ ok: verdict.ok, why: verdict.why || null, facts: verdict.roles_that_produced_a_fact,
    binding: verdict.binding, aliases: verdict.aliases && { pins: verdict.aliases.pins_named,
      distinct: verdict.aliases.distinct_paths, unresolved: verdict.aliases.unresolved },
    roles: (verdict.roles || []).map((r) => ({ role: r.role, ok: r.fact_ok, why: r.why })),
    problems: (verdict.problems || []).slice(0, 8), record: p }, null, 2));
  process.exit(verdict.ok ? 0 : 1);
}
