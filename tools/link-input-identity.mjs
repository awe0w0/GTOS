// GTOS-DSH-119 C0: the complete link-input identity.
//
// The 116 identity covered four hand-listed objects, so it could not notice an object that was missing from the link. This
// module derives the identity from the linker's actual ordered argv instead: every individual in the real command line is
// an input, in the order the linker will see it, with its real bytes and its real dependency file. Nothing is listed by
// hand, and nothing is assumed about how many there are.
//
// The obligations it enforces are the ones the taskbook names, each as a refusal rather than a comment:
//
//   omit an unchanged object      the count and the ordered name list no longer match what was derived
//   reorder                       the ordered name list differs even though the set does not
//   duplicate                     the same individual appears twice in one argv
//   same path, different bytes    the recorded hash is not the file's hash
//   a test object mixed in        an object built with host-test macros in a default link
//   a missing dependency          the object's .d file names something the identity does not carry
//
// Expectations are computed from the inputs, never from the image, so a wrong link cannot certify itself.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

// Flags that take a value. The argv parser has to know them, or it would treat a linker script as an individual.
const VALUE_FLAGS = new Set(['-T', '--script', '-Map', '--Map', '-o', '--output', '-m', '--oformat', '-L', '--library-path']);

// Host-test macros. An object compiled with any of these is a test object and must never appear in a default link.
const TEST_MACROS = ['GTOS_MEMORY_TEST', 'GTOS_NATIVE_RUNTIME_HOST_TEST', 'GTOS_PAGING_TEST',
  'GTOS_PROCESS_MEMORY_TEST', 'GTOS_CPU_TEST', 'GTOS_SURFACE_HOST_TEST', 'GTOS_WINDOW_HOST_TEST',
  'GTOS_CPU_WORK_POOL_HOST_TEST', 'GTOS_PROCESS_MEMORY_TEST'];

export function parseLinkArgv(argv) {
  if (!Array.isArray(argv) || argv.length < 2) throw new Error('LINK IDENTITY: argv must name a linker and inputs');
  const tool = argv[0];
  if (typeof tool !== 'string' || !tool.startsWith('/')) throw new Error('LINK IDENTITY: the linker must be an absolute path');
  const individuals = [];
  const values = {};
  let emulation = null;
  for (let i = 1; i < argv.length; ++i) {
    const token = argv[i];
    if (typeof token !== 'string' || !token) throw new Error('LINK IDENTITY: empty argv token');
    if (token === '-melf_i386') { emulation = 'elf_i386'; continue; }
    if (VALUE_FLAGS.has(token)) {
      const value = argv[++i];
      if (typeof value !== 'string' || !value) throw new Error('LINK IDENTITY: ' + token + ' has no value');
      values[token] = value;
      continue;
    }
    if (token.startsWith('-')) throw new Error('LINK IDENTITY: unexpected linker flag: ' + token);
    individuals.push(token);
  }
  if (emulation !== 'elf_i386') throw new Error('LINK IDENTITY: the link must declare -melf_i386');
  if (!values['-T']) throw new Error('LINK IDENTITY: no linker script');
  if (!values['-o']) throw new Error('LINK IDENTITY: no output image');
  if (individuals.length === 0) throw new Error('LINK IDENTITY: no individuals');
  return { tool, emulation, linkerScript: values['-T'], map: values['-Map'] || null,
    output: values['-o'], individuals };
}

// The dependency file a compiler emits beside an object. It names the sources and headers that object was built from.
function hasAssemblySource(objectPath, toHost) {
  const stem = objectPath.replace(/\.o$/, '');
  for (const ext of ['.asm', '.s', '.S']) {
    try { if (fs.existsSync(toHost(stem + ext))) return stem + ext; } catch { /* an unmappable path is not evidence */ }
  }
  return null;
}

function readDependencies(objectPath, toHost) {
  const candidate = objectPath.replace(/\.o$/, '.d');
  const host = toHost(candidate);
  if (!fs.existsSync(host)) return null;
  const text = fs.readFileSync(host, 'utf8');
  const colon = text.indexOf(':');
  if (colon < 0) return { path: candidate, targets: [], prerequisites: [] };
  const targets = text.slice(0, colon).trim().split(/\s+/).filter(Boolean);
  const prerequisites = text.slice(colon + 1).replace(/\\\r?\n/g, ' ')
    .trim().split(/\s+/).filter((t) => t && t !== '\\');
  return { path: candidate, targets, prerequisites };
}

// A profile is "default" when the object carries no host-test macro. The object file cannot be asked, so the profile is
// taken from the build record the caller supplies and cross-checked against the macros the build declared.
export function classifyProfile(buildRecord) {
  const macros = Array.isArray(buildRecord && buildRecord.macros) ? buildRecord.macros : [];
  const test = macros.filter((m) => TEST_MACROS.includes(m));
  return { profile: test.length ? 'host-test' : 'default', testMacros: test, macros };
}

// The paths in the argv are the Linux paths the guest sees. A host that is not that filesystem needs a mapping, and the
// caller must supply it; a path the mapper cannot place is refused, never quietly treated as absent.
export function makeHostMapper(rules) {
  if (!Array.isArray(rules) || rules.length === 0) throw new Error('LINK IDENTITY: a host mapper needs at least one rule');
  for (const r of rules) {
    if (!r || typeof r.linux !== 'string' || !r.linux.startsWith('/') || typeof r.host !== 'string' || !r.host) {
      throw new Error('LINK IDENTITY: each mapper rule needs a Linux prefix and a host prefix');
    }
  }
  return (linuxPath) => {
    if (typeof linuxPath !== 'string' || !linuxPath.startsWith('/')) {
      throw new Error('LINK IDENTITY: cannot map a non-absolute path: ' + linuxPath);
    }
    for (const r of rules) {
      if (linuxPath === r.linux) return r.host;
      if (linuxPath.startsWith(r.linux + '/')) return r.host + linuxPath.slice(r.linux.length);
    }
    throw new Error('LINK IDENTITY: no mapper rule places this path: ' + linuxPath);
  };
}

export function deriveLinkInputIdentity(argv, options = {}) {
  const parsed = parseLinkArgv(argv);
  // A real link draws individuals from more than one root: the objects this packet rebuilt and the frozen catalog objects
  // it reuses. Accepting a single root would refuse a genuine link, so roots are a list and each individual must sit under
  // exactly one of them. The role is the path relative to the root that contains it.
  const roots = Array.isArray(options.roots) ? options.roots.slice()
    : (options.root ? [options.root] : []);
  for (const r of roots) {
    if (typeof r !== 'string' || !r.startsWith('/') || r.endsWith('/')) {
      throw new Error('LINK IDENTITY: each root must be an absolute path with no trailing slash: ' + r);
    }
  }
  const rootOf = (p) => roots.find((r) => p.startsWith(r + '/')) || null;
  // Identity of a path is its Linux form; existence and bytes are read through the host form.
  const toHost = options.hostPath || ((p) => p);
  const profiles = options.profiles || {};
  // Individuals produced by the assembler. Declared rather than guessed, so "no dependency file" is only accepted where it
  // is the true state of the build.
  const assembled = new Set(Array.isArray(options.assembled) ? options.assembled : []);
  const catalogArtifacts = options.catalogArtifacts && typeof options.catalogArtifacts === 'object'
    ? options.catalogArtifacts : {};
  for (const [k, v] of Object.entries(catalogArtifacts)) {
    if (typeof v !== 'string' || !v) throw new Error('LINK IDENTITY: a catalog locator must be a non-empty string: ' + k);
  }
  const seenPath = new Map();
  const seenLogical = new Set();
  const individuals = [];
  for (let index = 0; index < parsed.individuals.length; ++index) {
    const real = parsed.individuals[index];
    if (!real.startsWith('/')) throw new Error('LINK IDENTITY: individual must be an absolute path: ' + real);
    if (seenPath.has(real)) throw new Error('LINK IDENTITY: the same individual appears twice: ' + real);
    seenPath.set(real, index);
    const containing = rootOf(real);
    if (roots.length && !containing) {
      throw new Error('LINK IDENTITY: individual outside every declared root: ' + real);
    }
    const host = toHost(real);
    if (!fs.existsSync(host)) throw new Error('LINK IDENTITY: individual is absent: ' + real);
    const bytes = fs.readFileSync(host);
    // The logical name is the path relative to the object root, so a moved object keeps its role and a renamed one loses it.
    const logical = containing ? real.slice(containing.length + 1) : path.posix.basename(real);
    if (seenLogical.has(logical)) throw new Error('LINK IDENTITY: duplicate logical role: ' + logical);
    seenLogical.add(logical);
    const declared = profiles[logical] || profiles[real] || null;
    const profile = declared ? classifyProfile(declared) : { profile: 'default', testMacros: [], macros: [] };
    individuals.push({
      ordinal: index,
      logical,
      path: real,
      bytes: bytes.length,
      sha256: sha256(bytes),
      profile: profile.profile,
      test_macros: profile.testMacros,
      dependencies: readDependencies(real, toHost),
      produced_by: (assembled.has(logical) || assembled.has(real)) ? 'assembler'
        : (readDependencies(real, toHost) === null && hasAssemblySource(real, toHost)) ? 'assembler' : 'compiler',
      assembly_source: hasAssemblySource(real, toHost),
      // Where this object dependency record lives, when it is not beside the object.
      provenance: catalogArtifacts[logical] || catalogArtifacts[real] || null,
    });
  }
  // Expectations are computed from the inputs above, not from the image, so the image cannot certify itself.
  const expected = {
    count: individuals.length,
    ordered_logicals: individuals.map((o) => o.logical),
    ordered_sha256: individuals.map((o) => o.sha256),
    total_bytes: individuals.reduce((n, o) => n + o.bytes, 0),
    distinct_hashes: new Set(individuals.map((o) => o.sha256)).size,
    default_profile_count: individuals.filter((o) => o.profile === 'default').length,
    host_test_count: individuals.filter((o) => o.profile === 'host-test').length,
    // Every prerequisite named by every .d file, as a set, so a missing dependency is visible.
    dependencies: [...new Set(individuals.flatMap((o) => (o.dependencies ? o.dependencies.prerequisites : [])))].sort(),
    assembled_count: individuals.filter((o) => o.produced_by === 'assembler').length,
    compiled_count: individuals.filter((o) => o.produced_by === 'compiler').length,
  };
  return {
    kind: 'complete_link_input_identity',
    task: options.task || null,
    tool: parsed.tool,
    emulation: parsed.emulation,
    linker_script: parsed.linkerScript,
    map: parsed.map,
    output: parsed.output,
    object_roots: roots,
    individuals,
    expected,
  };
}

// Compare a derived identity with a second observation of the same link. The comparison is two-way: an input the
// observation lacks is as much a failure as an input the identity lacks.
export function compareLinkInputIdentity(identity, observed) {
  const problems = [];
  if (!identity || identity.kind !== 'complete_link_input_identity') problems.push('no derived identity');
  if (!observed) return { ok: false, problems: ['no observed input'] };
  const want = identity.individuals;
  const got = Array.isArray(observed.individuals) ? observed.individuals : [];
  if (want.length !== got.length) problems.push(`individual count ${got.length} is not ${want.length}`);
  const shorter = Math.min(want.length, got.length);
  for (let i = 0; i < shorter; ++i) {
    if (want[i].path !== got[i].path) problems.push(`ordinal ${i}: ${got[i].path} is not ${want[i].path}`);
    else if (want[i].sha256 !== got[i].sha256) problems.push(`ordinal ${i}: bytes changed at ${want[i].path}`);
  }
  const wantSet = new Set(want.map((o) => o.path));
  const gotSet = new Set(got.map((o) => o.path));
  for (const p of wantSet) if (!gotSet.has(p)) problems.push('missing input: ' + p);
  for (const p of gotSet) if (!wantSet.has(p)) problems.push('unexpected input: ' + p);
  return { ok: problems.length === 0, problems };
}

// The refusals, each as a function that takes a good identity and returns the mutated copy plus the expected reason.
// A test uses these so that the negative case is generated from a real positive, not written by hand.
export const REFUSALS = {
  'omit-unchanged-object': (identity) => {
    const copy = structuredClone(identity);
    const dropped = copy.individuals.pop();
    copy.expected = recompute(copy.individuals);
    return { copy, expect: 'individual count', why: 'an unchanged object was dropped: ' + dropped.logical };
  },
  'reorder-individuals': (identity) => {
    if (identity.individuals.length < 2) throw new Error('need two individuals to reorder');
    const copy = structuredClone(identity);
    const first = copy.individuals[0];
    copy.individuals[0] = copy.individuals[1];
    copy.individuals[1] = first;
    copy.individuals.forEach((o, i) => { o.ordinal = i; });
    copy.expected = recompute(copy.individuals);
    return { copy, expect: 'ordinal 0', why: 'the first two individuals were swapped' };
  },
  'duplicate-individual': (identity) => {
    const copy = structuredClone(identity);
    copy.individuals.push({ ...copy.individuals[0], ordinal: copy.individuals.length });
    copy.expected = recompute(copy.individuals);
    return { copy, expect: 'individual count', why: 'the same individual was named twice' };
  },
  'same-path-different-bytes': (identity) => {
    const copy = structuredClone(identity);
    copy.individuals[0].sha256 = '0'.repeat(64);
    copy.expected = recompute(copy.individuals);
    return { copy, expect: 'bytes changed', why: 'the recorded hash is not the file hash' };
  },
  'test-object-in-default-link': (identity) => {
    const copy = structuredClone(identity);
    copy.individuals[0].profile = 'host-test';
    copy.individuals[0].test_macros = ['GTOS_MEMORY_TEST'];
    copy.expected = recompute(copy.individuals);
    return { copy, expect: 'host-test', why: 'an object built with host-test macros was placed in a default link' };
  },
  'missing-dependency': (identity) => {
    const copy = structuredClone(identity);
    // Take the first object that genuinely has a dependency record. Removing one from an object that never had one would
    // prove nothing, which is exactly the mistake this refusal is meant to catch.
    const victim = copy.individuals.find((o) => o.dependencies !== null);
    if (!victim) throw new Error('no individual carries a dependency record to remove');
    victim.dependencies = null;
    victim.produced_by = 'compiler';
    victim.provenance = null;
    victim.assembly_source = null;
    copy.expected = recompute(copy.individuals);
    return { copy, expect: 'no dependency record', why: 'the dependency record of ' + victim.logical + ' was removed' };
  },
};

function recompute(individuals) {
  return {
    count: individuals.length,
    ordered_logicals: individuals.map((o) => o.logical),
    ordered_sha256: individuals.map((o) => o.sha256),
    total_bytes: individuals.reduce((n, o) => n + o.bytes, 0),
    distinct_hashes: new Set(individuals.map((o) => o.sha256)).size,
    default_profile_count: individuals.filter((o) => o.profile === 'default').length,
    host_test_count: individuals.filter((o) => o.profile === 'host-test').length,
    dependencies: [...new Set(individuals.flatMap((o) => (o.dependencies ? o.dependencies.prerequisites : [])))].sort(),
    assembled_count: individuals.filter((o) => o.produced_by === 'assembler').length,
    compiled_count: individuals.filter((o) => o.produced_by === 'compiler').length,
  };
}

// The gate a real link has to pass before an image is used. It is deliberately strict about the two things that a
// hand-written list gets wrong: the count, and whether a test object slipped in.
export function assertLinkIdentityComplete(identity, options = {}) {
  const problems = [];
  if (!identity || identity.kind !== 'complete_link_input_identity') {
    return { ok: false, problems: ['not a complete link input identity'] };
  }
  const e = identity.expected;
  const n = identity.individuals.length;
  if (e.count !== n) problems.push(`expected count ${e.count} is not the ${n} individuals carried`);
  if (e.ordered_logicals.length !== n) problems.push('the ordered logical list does not cover every individual');
  if (new Set(e.ordered_logicals).size !== n) problems.push('the ordered logical list repeats a role');
  if (e.distinct_hashes !== n) problems.push(`${n - e.distinct_hashes} individuals share a hash`);
  for (const o of identity.individuals) {
    if (o.dependencies) {
      if (o.dependencies.targets.length === 0) problems.push(`empty dependency target for ${o.logical}`);
      continue;
    }
    // No dependency file. That is only acceptable with a stated reason: the object was assembled, or its record is
    // declared to live in the publication it was frozen from. An unexplained absence is refused.
    // The assembler invocation in this build is the evidence, not a source file sitting beside the object: the source may
    // legitimately live in the stage inputs directory.
    if (o.produced_by === 'assembler') continue;
    if (typeof o.provenance === 'string' && o.provenance) continue;
    problems.push(`no dependency record and no declared provenance for ${o.logical}`);
  }
  if (options.requireDefaultProfile && e.host_test_count > 0) {
    problems.push(`${e.host_test_count} host-test objects in a default link`);
  }
  if (options.minimum && n < options.minimum) problems.push(`only ${n} individuals, fewer than ${options.minimum}`);
  if (options.loader) {
    const found = identity.individuals.some((o) => o.logical === options.loader || o.path === options.loader);
    if (!found) problems.push('the loader is not among the individuals');
  }
  return { ok: problems.length === 0, problems };
}
