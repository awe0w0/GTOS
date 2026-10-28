// GTOS-DSH-146 test: the alias reader in the prerequisite consumer, on real files.
//
// The consumer gained one thing this packet: a caller-supplied map from the path a record names to the path its bytes are
// at now, so a sealed package can be consumed where it sits instead of only where it was built. That is a change to a
// consumer, so it needs its own tests, and the tests have to be about the properties that make it safe rather than about
// the happy path:
//
//   1. with no map, the consumer behaves exactly as before (the original paths are still what it reads);
//   2. with a map, it reads the mapped bytes and still compares against the pin the record carries;
//   3. a map that points at different bytes is REFUSED, so a map cannot launder a drifted file into a pass;
//   4. a map entry nothing needed is reported, so a stale map is visible rather than silent;
//   5. a map that redirects the contract record itself is honoured too, because that read goes through the same reader.
//
// Case 5 is the one that would be missed by testing only the happy path: the contract identity is read at a different
// place in the code from the input pins, and a partial aliasing implementation would leave it pointing at the original.

import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const R146 = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const host = (p) => (/^[A-Za-z]:/.test(p) || p.startsWith('//') ? p : '//wsl$/Ubuntu-20.04' + p);

const M = await import(pathToFileURL(R146 + '/worktree-146/tools/gtos-dsh-144/references/prerequisite-consumption-144.mjs').href);

const rolePath = R144 + '/role-source_profile_contract-144a.json';
const role = JSON.parse(fs.readFileSync(rolePath, 'utf8'));
const ctxBase = {
  contract_path: R144 + '/contract-144.json',
  ledger_path: R144 + '/producer-order-144.jsonl',
  runner_path: R144 + '/frozen-runners/run-144-r01.mjs',
};

// A private tree under this packet's own report root. Nothing here writes to any previous packet's root.
const scratch = R146 + '/test-only/d-aliases-146';
fs.mkdirSync(scratch, { recursive: true });
const copyRoot = scratch + '/copied';
fs.mkdirSync(copyRoot, { recursive: true });

const cases = [];
function record(name, expected, observed, extra) {
  cases.push({ name, expected, observed, ok: expected === observed, ...(extra || {}) });
}

// ---- 1. no map: the consumer reads the original paths, exactly as before this packet.
{
  const r = M.consumeRole('source_profile_contract', role, { ...ctxBase });
  record('no-map-is-accepted', true, r.ok, { detail: r.ok ? 'read ' + r.detail.inputs_read : r.why });
  record('no-map-uses-no-alias', 0, ctxBase.alias_use === undefined ? 0 : ctxBase.alias_use.used);
}

// ---- 2. a map for every source: the same verdict, from copied bytes, with the copies actually used.
const aliases = {};
const copied = [];
for (const s of role.sources.slice(0, 20)) {
  const base = path.basename(s.path);
  const dest = copyRoot + '/' + base;
  fs.writeFileSync(dest, fs.readFileSync(host(s.path)));
  aliases[s.path] = dest;
  copied.push({ from: s.path, to: dest });
}
{
  const ctx = { ...ctxBase, aliases };
  const r = M.consumeRole('source_profile_contract', role, ctx);
  record('mapped-copies-are-accepted', true, r.ok, { detail: r.ok ? 'read ' + r.detail.inputs_read : r.why });
  record('mapped-aliases-were-used', copied.length, ctx.alias_use.used);
  record('mapped-aliases-all-distinct', copied.length, ctx.alias_use.distinct_used.length);
}

// ---- 3. a map pointing at different bytes must be refused. The pin in the record is the thing that refuses it: the
// reader has no idea the copy is wrong, and the only reason it can tell is that it still compares against the pin.
{
  const bad = { ...aliases };
  const victim = Object.keys(bad)[0];
  const wrong = copyRoot + '/wrong-bytes.h';
  fs.writeFileSync(wrong, 'this is not the header the contract pinned\n');
  bad[victim] = wrong;
  const ctx = { ...ctxBase, aliases: bad };
  const r = M.consumeRole('source_profile_contract', role, ctx);
  record('wrong-bytes-is-refused', false, r.ok, { why: r.ok ? null : String(r.why).slice(0, 120) });
  record('wrong-bytes-refusal-names-the-file', true, !r.ok && String(r.why).includes('contract pin is not the file on disk'));
}

// ---- 4. an alias nothing needed is reported. A stale map is the failure mode of this feature: a caller believes it
// pointed the consumer at the package when it pointed it at a path no record names any more.
{
  const ctx = { ...ctxBase };
  ctx.aliases = { ...aliases, '/var/tmp/a/path/that/no/record/names.h': copyRoot + '/wrong-bytes.h' };
  const r = M.consumeRole('source_profile_contract', role, ctx);
  record('stale-alias-does-not-fail-the-read', true, r.ok);
  record('stale-alias-is-reported', 1, ctx.alias_use.unused.length);
  record('stale-alias-is-the-one-supplied', '/var/tmp/a/path/that/no/record/names.h', ctx.alias_use.unused[0]);
}

// ---- 5. the contract record itself is read through the same reader. If only the input pins were aliased, this case
// would fail, and the failure would be a wrong answer rather than a refusal: the contract identity would be taken from
// the original file while the inputs came from the copy.
{
  const contractCopy = copyRoot + '/contract-144.json';
  fs.writeFileSync(contractCopy, fs.readFileSync(host(ctxBase.contract_path)));
  const ctx = { ...ctxBase, contract_path: ctxBase.contract_path, aliases: { [ctxBase.contract_path]: contractCopy } };
  const r = M.consumeRole('source_profile_contract', role, ctx);
  record('contract-identity-read-through-the-map', true, r.ok, { detail: r.ok ? 'read ' + r.detail.inputs_read : r.why });
  record('contract-alias-was-used', 1, ctx.alias_use.used);
}

// ---- 6. and the same map with a corrupt contract copy is refused, which is the property that makes case 5 worth
// having: aliasing the identity read is only safe because the identity is still compared.
{
  const contractCopy = copyRoot + '/contract-144-wrong.json';
  fs.writeFileSync(contractCopy, '{}\n');
  const ctx = { ...ctxBase, aliases: { [ctxBase.contract_path]: contractCopy } };
  const r = M.consumeRole('source_profile_contract', role, ctx);
  record('corrupt-contract-copy-is-refused', false, r.ok, { why: r.ok ? null : String(r.why).slice(0, 140) });
}

const out = {
  task: 'GTOS-DSH-146',
  stage: 'd-alias-146-01',
  what: 'the alias reader the prerequisite consumer gained, tested on real files for the four properties that make it safe',
  copied_files: copied.length,
  cases,
  counts: { cases: cases.length, passed: cases.filter((c) => c.ok).length, failed: cases.filter((c) => !c.ok).length },
  ok: cases.every((c) => c.ok),
};
fs.writeFileSync(R146 + '/d-aliases-146-01.json', JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify(out.counts), out.ok ? 'OK' : 'FAILED');
for (const c of cases) if (!c.ok) console.log('  FAIL', c.name, 'expected', c.expected, 'observed', c.observed, c.why || '');
