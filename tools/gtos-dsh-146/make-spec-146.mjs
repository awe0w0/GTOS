// GTOS-DSH-146: build the package spec.
//
// The spec is the ordered list of what the payload carries. What makes this one different from 145's is where the list comes
// from: 145's members were assembled from the spec's own member list, and its required set was then built from that list --
// so the package required exactly what it carried, by construction. Here the requirement set is `d-required-146-02.json`,
// derived from the contracts that were frozen before any 146 package existed, and the member list is built FROM it. Every
// requirement is a member. If a requirement cannot be copied the spec refuses rather than shipping a package that is short
// one row.
//
// On top of the requirements the payload carries the packet's own records: the identity it was dispatched under, the seven
// role records, the four suite records, the numeric judgement and the mislocation disclosure. Those are not derived from a
// contract because they did not exist when the contracts were frozen -- they are what this packet found out, and the seal is
// the statement that they were produced by the code the payload also carries.
//
// Nothing here decides whether the payload is correct. It decides what the payload is.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const RUN = process.argv[2] || '01';
const SPEC_STAGE = 'spec-146-' + RUN;
const REQUIRED_SET = R + '/d-required-146-02.json';
const ROLE_DIR = R + '/stage-output/roles-146-02';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => fs.existsSync(p);

if (!exists(REQUIRED_SET)) throw new Error('the required set is absent: ' + REQUIRED_SET);
const required = readJson(REQUIRED_SET);
if (required.ok !== true) throw new Error('the required set says it is not ok, so there is nothing to package against');
if (required.missing.length !== 0) throw new Error('the required set has ' + required.missing.length + ' missing rows');

// ---------------------------------------------------------------- the requirement members
//
// One member per distinct source file. The required set has more rows than files because one file can satisfy two rows -- the
// host seam is also a compiled build input -- and that is recorded here rather than collapsed silently.
const members = [];
const used = new Map();      // package path (lower) -> source path
const aliases = {};          // source path -> package path
const disambiguated = [];

const basenameOf = (p) => path.posix.basename(String(p).replace(/\\/g, '/'));
// A requirement names the path the producing packet wrote, and those paths are in two domains: Windows drive paths, and Linux
// paths under the distribution. Both have to be readable from here, so a Linux path is translated to the share that carries
// it. The translation is one-way and is used only to OPEN the file; the member records the original path, because that is
// what the contract names and what a reader of the package will look for.
const WSL = '//wsl$/Ubuntu-20.04';
const toHost = (p) => {
  const s = String(p);
  if (/^[A-Za-z]:[\\/]/.test(s) || s.startsWith('//')) return s;
  if (s.startsWith('/mnt/')) return s.slice(7, 8).toUpperCase() + ':/' + s.slice(9);
  return WSL + s;
};
const dirFor = {
  producer_object: 'producers',
  source_view: 'sources',
  depfile: 'depfiles',
  dep_source: 'dep-sources',
  image: 'image',
  image_map: 'image',
  host_unit_elf: 'host/elf',
  host_result: 'host/results',
  host_inner: 'host/inner',
  host_raw: 'host/raw',
  host_build_input: 'host/build-inputs',
  host_seam: 'host/seam',
  guest_trial_record: 'guest/records',
  guest_channel: 'guest/channels',
  support_module: 'engine',
};

// A role the derivation does not know about is a refusal, not a default directory. A silent fallback is how a requirement
// ends up somewhere nobody looks for it.
for (const entry of required.entries) {
  const role = entry.roles[0];
  const dir = dirFor[role];
  if (!dir) throw new Error('the required set names a role this spec has no place for: ' + role);
  const base = basenameOf(entry.source);
  // A dependency source is named by the tail of its path from the project root, not by its basename: two views can hold two
  // different `arp.cpp` files, and a package that carried both under one name would lose one of them. When the path has no
  // recognisable root the basename is used and the collision rule below disambiguates it.
  let leaf = base;
  if (role === 'dep_source' || role === 'source_view') {
    const m = /\/(src|include|tests|apps|lib)\/.*$/.exec(String(entry.source).replace(/\\/g, '/'));
    if (m) leaf = m[0].slice(1);
  }
  let candidate = dir + '/' + leaf;
  if (used.has(candidate.toLowerCase()) && used.get(candidate.toLowerCase()) !== entry.source) {
    // Two different files with the same basename -- the numeric judge and its own test suite are the real example. The
    // disambiguator is a digest of the source path, so it is stable across runs and does not depend on iteration order.
    const tag = sha(Buffer.from(entry.source.toLowerCase())).slice(0, 8);
    const ext = path.posix.extname(leaf);
    candidate = dir + '/' + leaf.slice(0, leaf.length - ext.length) + '-' + tag + ext;
    disambiguated.push({ source: entry.source, first: used.get(candidate.toLowerCase()) || null, path: candidate });
  }
  if (used.has(candidate.toLowerCase())) throw new Error('two requirements map to one package path: ' + candidate);
  used.set(candidate.toLowerCase(), entry.source);
  if (!exists(toHost(entry.source))) throw new Error('a required file is absent: ' + entry.source);
  const bytes = fs.readFileSync(toHost(entry.source));
  members.push({ path: candidate, role, from: entry.source, bytes: bytes.length, sha256: sha(bytes),
    satisfies: entry.requirements > 1 ? entry.roles : undefined });
  aliases[entry.source] = candidate;
}

const requirementMembers = members.length;

// ---------------------------------------------------------------- the packet's own records
const add = (packagePath, role, from, what) => {
  if (!exists(toHost(from))) throw new Error('a record the spec carries is absent: ' + from + ' (' + what + ')');
  if (used.has(packagePath.toLowerCase())) throw new Error('a record collides with a requirement: ' + packagePath);
  const bytes = fs.readFileSync(toHost(from));
  used.set(packagePath.toLowerCase(), from);
  members.push({ path: packagePath, role, from, bytes: bytes.length, sha256: sha(bytes) });
};

const CONTEXT = [
  ['authority-146.json', 'the authority this packet executes under'],
  ['publication-context-146.json', 'the publication context the engine pins'],
  ['mandatory-consumer-roles-146.json', 'the frozen role contract the seal compares against'],
  ['dispatch-146.json', 'the dispatch record'],
  ['acceptance-objectives-146.json', 'the acceptance objectives'],
  ['execution-identities-146.json', 'the executor identities'],
  ['current-default-materials-146.json', 'the default materials index'],
  ['candidate-source-and-test-materials-146.json', 'the candidate source and test materials'],
  ['build-material-catalog-146.json', 'the build material catalog'],
  ['reference-index-146.json', 'the reference index'],
  ['assembly-source-contract-146.json', 'the assembly source contract'],
  ['authorization-146.json', 'the authorization record'],
  ['candidate-commit-identities-146.json', 'the candidate commit identities'],
  ['history-and-forward-materials-145.json', 'the forward material index'],
];
for (const [name, what] of CONTEXT) add('context/' + name, 'context', R + '/' + name, what);

add('records/d-required-146-02.json', 'record', REQUIRED_SET, 'the required set this package was built from');
add('records/roles-146-02.json', 'record', R + '/roles-146-02.json', 'the seven-role summary');

const ROLE_NAMES = readJson(R + '/mandatory-consumer-roles-146.json').roles;
const roleRecords = [];
for (const role of ROLE_NAMES) {
  const p = ROLE_DIR + '/' + role + '.json';
  add('records/roles/' + role + '.json', 'record', p, 'the record the ' + role + ' role wrote');
  roleRecords.push({ role, path: p, bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)), ok_field: 'ok' });
}

const EVIDENCE = [
  ['d-aliases-146-01.json', 'the alias reader and its thirteen cases'],
  ['f-cardinality-146-01.json', 'the publisher cardinality and role gate, thirty-one cases'],
  ['r-numeric-146-01.json', 'the numeric judge over every real trial, with its mutations and negatives'],
  ['io-semantic-146-01.json', 'the IO classifier, its tiers and its negatives'],
  ['q-mislocated-verify-146.json', 'the mislocation this packet found in its own publisher, and the fix'],
];
const evidence = [];
for (const [name, what] of EVIDENCE) {
  const p = R + '/' + name;
  add('records/' + name, 'record', p, what);
  evidence.push({ what, path: p, bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)), ok_field: 'ok' });
}
{
  const p = ROLE_DIR + '/g-numeric-full.json';
  if (exists(p)) {
    add('records/g-numeric-full.json', 'record', p, 'every matrix trial judged numerically, standalone');
    evidence.push({ what: 'every matrix trial judged numerically, standalone', path: p,
      bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)), ok_field: 'ok' });
  }
}
evidence.push({ what: 'the seven-role summary', path: R + '/roles-146-02.json',
  bytes: fs.statSync(R + '/roles-146-02.json').size, sha256: sha(fs.readFileSync(R + '/roles-146-02.json')), ok_field: 'ok' });

// ---------------------------------------------------------------- the spec
const roles = {};
for (const m of members) roles[m.role] = (roles[m.role] || 0) + 1;
const sorted = members.slice().sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
for (let i = 1; i < sorted.length; i++) {
  if (sorted[i].path === sorted[i - 1].path) throw new Error('two members share one path: ' + sorted[i].path);
}
const lines = sorted.map((m, i) => i + '\0' + m.path + '\0' + m.bytes + '\0' + m.sha256).join('\n');
const ordinalDigest = sha(Buffer.from(lines, 'utf8'));

const spec = {
  task: 'GTOS-DSH-146',
  stage: SPEC_STAGE,
  what: 'the ordered member list for this packet payload: every requirement the contracts derive, plus the records this '
    + 'packet produced, each with the bytes and hash it had when the spec was written',
  members: sorted,
  roles,
  total_bytes: sorted.reduce((n, m) => n + m.bytes, 0),
  member_count: sorted.length,
  member_list_ordinal_digest: ordinalDigest,
  aliases,
  disambiguated,
  required_set: { path: REQUIRED_SET, bytes: fs.statSync(REQUIRED_SET).size,
    sha256: sha(fs.readFileSync(REQUIRED_SET)), ok_field: 'ok' },
  mandatory_roles: { roles: roleRecords },
  evidence,
  counts: {
    requirement_rows: required.requirements.length,
    requirement_files: requirementMembers,
    files_satisfying_more_than_one_requirement: required.entries.filter((e) => e.requirements > 1).length,
    context_records: CONTEXT.length,
    role_records: roleRecords.length,
    evidence_records: evidence.length,
  },
  ok: true,
  ok_means: 'every requirement row in the required set is a member of this payload, every record the packet produced is a '
    + 'member, and every member is a file that exists at the bytes and hash recorded here.',
};

const outPath = R + '/' + SPEC_STAGE + '.json';
if (exists(outPath)) { fs.unlinkSync(outPath); }
fs.writeFileSync(outPath, JSON.stringify(spec, null, 2) + '\n');
console.log(JSON.stringify({ spec: outPath, bytes: fs.statSync(outPath).size,
  sha256: sha(fs.readFileSync(outPath)), member_count: spec.member_count, total_bytes: spec.total_bytes,
  roles, ordinal_digest: ordinalDigest, counts: spec.counts,
  disambiguated: disambiguated.length }, null, 2));
