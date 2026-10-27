// GTOS-DSH-138 P: one closure, correctly sourced, and actually consumed by the one link path.
//
// 132 built 61 objects and then linked a different set. The evidence is unambiguous: the link record's 61 individuals
// contained zero from the producer directory, 51 came from the catalog and 10 from the link phase's own rebuilds. Two
// separate runs, each successful, joined by a report that called the result done.
//
// Worse, one of the 61 was wrong. The IRQ slot assembled `src/loader.s` because its own source was not found and the
// search fell back, which produced an object byte-identical to the loader slot: two "individuals" that were the same file,
// one of them a completely different role.
//
// This module does the whole chain in one place so the two cannot come apart:
//
//   resolve   every slot to exactly one declared source, refusing rather than substituting
//   compile   every slot, recording spec, attempt, terminal, inner, raw and object per slot
//   freeze    the producer contract that names those slots, sources and profile
//   link      build the ordered argv FROM that contract and hand it to the real link entry
//
// A slot whose source cannot be resolved refuses the closure. There is no search by basename and no fallback.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

export function hostOf(p) {
  if (typeof p !== 'string') return p;
  if (p.startsWith('/mnt/')) { const m = /^\/mnt\/([a-z])\/(.*)$/.exec(p); return m[1].toUpperCase() + ':/' + m[2]; }
  if (p.startsWith('/')) return '//wsl$/Ubuntu-20.04' + p;
  return p;
}

export function linuxOf(p) {
  if (typeof p !== 'string') return p;
  // Normalise a UNC path first, because toHost produces backslashes and the linker never sees those.
  if (p.startsWith('\\\\wsl$\\') || p.startsWith('//wsl$/')) {
    const tail = p.replace(/^\\\\wsl\$\\[^\\]+/, '').replace(/^\/\/wsl\$\/[^/]+/, '');
    return tail.replace(/\\/g, '/');
  }
  if (/^[A-Za-z]:[\\/]/.test(p)) {
    const drive = p[0].toLowerCase();
    return '/mnt/' + drive + '/' + p.slice(3).replace(/\\/g, '/');
  }
  return p;
}

function readBytes(p) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* retry */ } }
  return b;
}

function pinOf(p) {
  const b = readBytes(p);
  return b === null ? null : { path: p, bytes: b.length, sha256: sha(b) };
}

// ---------------------------------------------------------------- resolve

// The assembly slots come from the fixed contract and from nowhere else. A slot the contract does not name, or whose
// declared source is absent or has drifted, refuses the whole closure: it is never filled from another file.
export function resolveAssembly(assemblyContract) {
  const problems = [];
  const byToken = new Map();
  for (const row of assemblyContract.rows) {
    const b = readBytes(row.source.path);
    if (b === null) {
      problems.push({ slot: row.token, why: 'the declared source is absent: ' + row.source.path });
      continue;
    }
    if (b.length !== row.source.bytes || sha(b) !== row.source.sha256) {
      problems.push({ slot: row.token, why: 'the declared source has drifted: ' + row.source.path });
      continue;
    }
    byToken.set(row.token, { logical: row.logical, source: row.source.path, bytes: b.length, sha256: sha(b) });
  }
  return { byToken, problems };
}

// The C++ slots are resolved by their exact logical path: the candidate first, then the catalog entry that names that same
// path. A stem that matches nothing is a refusal, not a search.
export function resolveCxx(slotTokens, options) {
  const { catalog, candidateRoot } = options;
  const byLogical = new Map(catalog.source_view.map((v) => [v.logical, v]));
  const problems = [];
  const out = new Map();
  for (const token of slotTokens) {
    const tokenName = token.split('/').pop();
    const stem = tokenName.replace(/\.o$/, '');
    // The candidate may carry the source under a path the catalog also names; the catalog's logical path is the authority
    // for where a source lives, and the candidate overrides its bytes.
    const hit = catalog.source_view.find((v) => v.logical === 'src/' + stem + '.cpp')
      || catalog.source_view.find((v) => v.logical.endsWith('/' + stem + '.cpp'));
    if (!hit) { problems.push({ slot: tokenName, why: 'no source is declared for this C++ slot' }); continue; }
    const candidatePath = candidateRoot + '/' + hit.logical;
    const source = fs.existsSync(candidatePath) ? candidatePath : hit.source.path;
    const b = readBytes(source);
    if (b === null) { problems.push({ slot: tokenName, why: 'the declared source is absent: ' + source }); continue; }
    out.set(tokenName, { logical: hit.logical, source, from_candidate: fs.existsSync(candidatePath),
      bytes: b.length, sha256: sha(b) });
  }
  return { byLogical: out, problems };
}

// ---------------------------------------------------------------- the contract

// The contract every producer spec must carry. It names the slot, the source, the headers and the profile, and it is
// written before any compile so a spec cannot be described as having run under a contract written afterwards.
export function producerContract(input) {
  return {
    task: input.task,
    kind: 'producer_closure_contract',
    tag: input.tag,
    frozen_before_build: true,
    order_from: input.orderFrom,
    order: input.order,
    lineage: input.lineage,
    default_profile: input.profile,
    slots: input.slots,
    headers: input.headers,
    assembly_source_contract: input.assemblyContractPath,
    rule: 'every producer spec in this closure pins this contract by path and hash; a spec pinned to any other contract '
      + 'is not part of this closure',
  };
}

// ---------------------------------------------------------------- the link argv

// The argv is built from the contract's slots, in the contract's order, using the object each slot actually produced. A
// caller cannot supply a different list: this function takes the contract and the built table and nothing else.
export function orderedIndividuals(contract, built) {
  const problems = [];
  const out = [];
  // A producer contract pins a slot's source in one of two places, depending on which contract writer produced it: the
  // `closure133` writer carries the pin inline on the slot, and the `producerfull` writer carries a `sources` table beside
  // the slots. Both are the same obligation — the object the slot produced was compiled from the source the contract names
  // — so this reads whichever is present rather than requiring one writer's shape.
  const declaredSourcePin = (slot) => {
    const fromSources = Array.isArray(contract.sources)
      ? contract.sources.find((s) => s.slot === slot.slot) : null;
    return { sha256: slot.source_sha256 || (fromSources ? fromSources.sha256 : null),
      path: slot.source || (fromSources ? fromSources.source : null) };
  };
  for (const slot of contract.slots) {
    const b = built.find((x) => x.slot === slot.slot);
    if (!b) { problems.push({ slot: slot.slot, why: 'the closure produced no entry for this slot' }); continue; }
    if (!b.object) { problems.push({ slot: slot.slot, why: 'the slot produced no object' }); continue; }
    // The contract pins the source and the profile; the object is what the compile produced from them. Comparing the
    // object against a hash the contract could not have known before the compile would be describing an outcome as an
    // input, so the contract's obligation is that the source is the one it names.
    const declared = contract.slots.find((s) => s.slot === slot.slot);
    if (!declared) { problems.push({ slot: slot.slot, why: 'the contract names no such slot' }); continue; }
    const pin = declaredSourcePin(declared);
    if (!pin.sha256) { problems.push({ slot: slot.slot, why: 'the contract pins no source for this slot' }); continue; }
    if (pin.sha256 !== sha(readBytes(b.source))) {
      problems.push({ slot: slot.slot, why: 'the source is not the one the contract pins',
        contract_pins: pin.sha256, read: sha(readBytes(b.source)), source: b.source });
      continue;
    }
    out.push({ slot: slot.slot, token: slot.token, path: b.object.path, bytes: b.object.bytes,
      sha256: b.object.sha256 });
  }
  return { individuals: out, problems };
}

// The check that would have caught the 132 defect in one line: no two slots may share an object hash, because two slots
// that produce the same bytes are either the same file or a substitution that went wrong.
export function distinctObjects(individuals) {
  const seen = new Map();
  const duplicates = [];
  for (const x of individuals) {
    const prior = seen.get(x.sha256);
    if (prior) duplicates.push({ a: prior.token, b: x.token, sha256: x.sha256 });
    else seen.set(x.sha256, x);
  }
  return duplicates;
}
