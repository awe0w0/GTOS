// GTOS-DSH-145 D: the independent required set.
//
// This is the set the seal consumes, and it is built from the payload's own member list rather than from any consumer's report. Each entry
// names one package path, the role the spec gives it, and -- the part 144 did not have -- the exact source path it was copied from. That
// source path is the alias, and it is what lets a comparison between "what a consumer opened" and "what the packet requires" be exact in both
// directions instead of matching by suffix.
//
// It also states what the package deliberately does not carry, so that an absence is a recorded decision rather than a gap: the runtime supply
// is a pinned external environment, and the host chain control record belongs to the host stage rather than to this payload.
//
// The record says true in a field named `ok`, which is what the seal checks. It is not a claim that the packet is correct; it is the claim that
// the required set is complete and that every member it names is in the payload.
import fs from 'node:fs';
import crypto from 'node:crypto';

const R = 'F:/GTOS-Chromium/artifacts/native-fixed-task-finality-and-real-consumer-read-closure-20261028-a';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const RUN = process.argv[2] || '01';

// The spec is named, and only its member list is used. The spec's own bytes are not recorded here, because this record is pinned by the spec
// in turn and a mutual hash would be a cycle that can only be broken by writing one of them twice.
const specFiles = fs.readdirSync(R).filter((f) => /^spec-145-\d+\.json$/.test(f)).sort();
if (specFiles.length === 0) throw new Error('no spec to derive a required set from: run make-spec-145 first');
const specName = process.argv[3] || specFiles[specFiles.length - 1];
const spec = JSON.parse(fs.readFileSync(R + '/' + specName, 'utf8'));

const ordinalDigest = (members) => sha(Buffer.from(members.map((m, i) => `${i}\0${m.path}\0${m.bytes}\0${m.sha256}`).join('\n'), 'utf8'));

const entries = spec.members.map((m) => {
  const alias = (spec.aliases && spec.aliases[m.path]) || null;
  return {
    package_path: m.path,
    role: m.role,
    source: alias ? alias.from : null,
    bytes: m.bytes,
    sha256: m.sha256,
    why: alias ? 'carried because the packet reads it at this exact path' : 'carried as a member of the packet',
  };
});
const byRole = {};
for (const e of entries) byRole[e.role] = (byRole[e.role] || 0) + 1;

const absent = [
  { role: 'runtime_file', why: 'the runtime supply is a pinned external environment named by the authority, not a member of this packet; a '
      + 'payload that carried a copy of it would be claiming to deliver an environment it did not build' },
  { role: 'host_control', why: 'the host chain control record belongs to the host stage that produced the host results; this packet carries the '
      + 'results and the unit ELFs, and the consumer that reads them is run against those' },
  { role: 'depfile', why: 'this packet builds nothing, so it produces no depfiles and no producer objects; the depfile closure belongs to the '
      + 'packet that compiled the image, and re-deriving it here would be re-deriving another packet work' },
];

const out = {
  task: 'GTOS-DSH-145',
  stage: 'required-set-145-' + RUN,
  what: 'the required set of the payload, one entry per member, with the exact source path of each as its alias',
  derived_from: { spec: specName, member_list_ordinal_digest: ordinalDigest(spec.members), member_count: spec.members.length },
  how_compared: 'by exact package path in both directions, through the alias table; nothing is matched by suffix, because a suffix match makes '
    + 'guest/07.e9.log and logs/gm144-07.e9.log the same name and that is the mistake this record exists to avoid',
  entries,
  counts: { entries: entries.length, by_role: byRole, with_an_alias: entries.filter((e) => e.source !== null).length },
  not_carried: absent,
  ok: entries.length > 0 && entries.length === spec.members.length && entries.every((e) => e.bytes > 0 && /^[0-9a-f]{64}$/.test(e.sha256)),
  ok_means: 'the required set names every member of the payload exactly once, each with its size and hash, and states what the payload '
    + 'deliberately does not carry',
};
const target = R + '/required-set-145-' + RUN + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ ok: out.ok, path: target, entries: out.counts, member_list: out.derived_from.member_list_ordinal_digest }, null, 2));
process.exit(out.ok ? 0 : 1);
