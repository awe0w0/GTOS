// GTOS-DSH-146: write the required-set record.
//
// The derivation itself lives in `d-required-146.mjs`; this is the thin runner that gives it a stage name and a file, so the
// record that the plan pins is a record a tool wrote rather than an object printed to a terminal.
//
// What makes this record different from 145's is where its rows come from. 145's required set was built from the spec's own
// member list -- so the package required exactly what the package carried, and the comparison agreed with itself by
// construction. This one reads the contracts that were frozen before any 146 package existed: the frozen link order, the
// source/profile contract, the dependency chain, the static image contract, the link-input consumption record, the host
// contract and its five build records, and the guest matrix. `not_derived_from` names the things it did not read, and it is
// a claim that can be checked: the module has no path to a 146 spec in it.

import crypto from 'node:crypto';
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const MODULE = R + '/worktree-146/tools/gtos-dsh-146/d-required-146.mjs';
const ORDER = R144 + '/stage-output/lk144-01.spec.json';
const STAGE = process.argv[2] || 'd-required-146-01';
// The record name, not the executor's stage grammar: the publisher this packet publishes with never reads `stage`, and the
// records in this packet are named `<letter>-<topic>-146-<nn>`. The name still has to end in a two-digit ordinal so a second
// run cannot quietly overwrite the first.
if (!/^[a-z][a-z0-9-]*-[0-9]{2}$/.test(STAGE)) throw new Error('bad record name: ' + STAGE);

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const M = await import(pathToFileURL(MODULE).href);
const derived = M.derive({ R144, R146: R, orderSpecPath: ORDER });

const rows = derived.requirements;
// The rows are requirements, not files: one file can satisfy two of them (the host seam is also a compiled build input), and
// this record says so rather than collapsing them silently. The package will carry the file once.
const byPath = new Map();
for (const row of rows) {
  const key = String(row.path).toLowerCase();
  if (!byPath.has(key)) byPath.set(key, { path: row.path, roles: [], requirements: 0 });
  const e = byPath.get(key);
  if (!e.roles.includes(row.role)) e.roles.push(row.role);
  e.requirements += 1;
}

const out = {
  task: 'GTOS-DSH-146',
  stage: STAGE,
  what: 'the business requirements, derived from the contracts that were frozen before any 146 package existed',
  derivation: { module: MODULE, bytes: fs.statSync(MODULE).size, sha256: sha(fs.readFileSync(MODULE)) },
  order_spec: { path: ORDER, bytes: fs.statSync(ORDER).size, sha256: sha(fs.readFileSync(ORDER)) },
  not_derived_from: derived.not_derived_from,
  contracts_read: derived.contracts_read,
  derivations: derived.derivations,
  counts: { ...derived.counts, distinct_source_paths: byPath.size },
  entries: [...byPath.values()].map((e) => ({ source: e.path, roles: e.roles, requirements: e.requirements })),
  requirements: rows,
  missing: derived.missing,
  ok: derived.ok,
  ok_means: 'every derivation succeeded, and every requirement it produced is a file that exists on disk at the size it was '
    + 'derived at. A missing row is kept in `missing` and in `requirements`; it is not dropped, because a requirement that '
    + 'cannot be met is still a requirement.',
};

const target = R + '/' + STAGE + '.json';
if (fs.existsSync(target)) throw new Error('refused, exists: ' + target);
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ stage: STAGE, ok: out.ok, requirements: rows.length, distinct: byPath.size,
  missing: derived.missing.length, by_role: derived.counts.by_role,
  derivations: derived.derivations, bytes: fs.statSync(target).size, sha256: sha(fs.readFileSync(target)) }, null, 2));
process.exit(out.ok ? 0 : 1);
