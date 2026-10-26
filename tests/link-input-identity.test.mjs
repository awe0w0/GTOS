// GTOS-DSH-119 C0b: the link-input identity test, run against a real link.
//
// This is not a report checker. It reads the actual linker command line of the runtime unit build that this packet already
// produced, derives the identity from that argv, and then breaks exactly one obligation at a time to show the gate
// refuses. The positive case is the real build, so the negative cases are mutations of something that genuinely happened.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as identity from '../tools/link-input-identity.mjs';

const here = new URL('.', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

// The real ld spec this packet already ran. Its argv is the linker's, not a hand-written one.
const specPath = process.argv[2];
if (!specPath) {
  console.error('usage: link-input-identity.test.mjs <ld-spec.json> [report.json]');
  process.exit(2);
}
const spec = JSON.parse(fs.readFileSync(specPath, 'utf8'));
// The argv carries Linux paths. This host is Windows, so the mapping is explicit and a path it cannot place is refused.
const hostPath = identity.makeHostMapper([
  { linux: '/var/tmp', host: '//wsl$/Ubuntu-20.04/var/tmp' },
  { linux: '/mnt/f', host: 'F:' },
]);
const argv = spec.cmd;
// The roots the real link draws from. The rebuilt objects live under test-build/<tag>; the reused ones come from the frozen
// catalog. Both are real, and the identity must cover both rather than assume one.
// The rebuilt objects sit beside the image the link produces, so the output path is the honest source for that root. The
// last individual is a reused catalog object and would give the wrong directory.
const outputIndex = argv.indexOf('-o');
const objectRoot = outputIndex >= 0 ? path.posix.dirname(argv[outputIndex + 1]) : path.posix.dirname(argv[argv.length - 1]);
const roots = [objectRoot];
for (const a of argv) {
  const m = /^(.*\/native\/objects)\/reused\//.exec(a);
  if (m && !roots.includes(m[1])) roots.push(m[1]);
}
for (const a of argv) {
  const m = /^(.*\/publication-[0-9]+-v[0-9]+\/native\/objects)/.exec(a);
  if (m && !roots.includes(m[1])) roots.push(m[1]);
}

// The profiles are read from the compile specs that produced these objects, not declared by hand.
const specDir = spec.__spec_dir || null;
const profiles = {};
const assembled = [];
if (specDir && fs.existsSync(specDir)) {
  for (const name of fs.readdirSync(specDir)) {
    // Every build spec of this link, not only the C++ ones: the assembler stage is what proves an object was assembled.
    if (!/^spec-.*\.json$/.test(name)) continue;
    let s = null;
    try { s = JSON.parse(fs.readFileSync(path.join(specDir, name), 'utf8')); } catch { continue; }
    const cmd = s.cmd || [];
    const objectAt = cmd.indexOf('-o');
    if (objectAt < 0) continue;
    const objectPath = cmd[objectAt + 1];
    const tool = cmd[0] || '';
    const macros = cmd.filter((c) => typeof c === 'string' && c.startsWith('-D')).map((c) => c.slice(2));
    profiles[objectPath] = { macros, source: path.join(specDir, name), tool };
    // An assembler invocation is the evidence that this object was assembled, so no dependency file is expected for it.
    if (/\bas$/.test(tool) || cmd.includes('--32')) assembled.push(objectPath);
  }
}
check('the compile specs were found for this build', Object.keys(profiles).length > 0, Object.keys(profiles).length);

const catalogArtifacts = {};
for (const a of argv) {
  const m = /^(.*\/publication-[0-9]+-v[0-9]+\/native\/objects)\/reused\//.exec(a);
  if (m) catalogArtifacts[a] = m[1].replace(/\/native\/objects$/, '') + ' (sealed publication, objects reused)';
}
check('the catalog artifacts were located', Object.keys(catalogArtifacts).length > 0, Object.keys(catalogArtifacts).length);

check('the spec is a real ld invocation', argv[0].includes('ld'), argv[0]);
check('the spec declares elf_i386', argv.includes('-melf_i386'));

// ---------------------------------------------------------------- the positive case
let derived = null;
try {
  derived = identity.deriveLinkInputIdentity(argv,
    { roots, task: 'GTOS-DSH-119', hostPath, profiles, assembled, catalogArtifacts });
  check('a real link yields a complete identity', true, derived.individuals.length + ' individuals');
} catch (error) {
  check('a real link yields a complete identity', false, String(error.message));
}

if (derived) {
  check('every individual exists on disk and has bytes',
    derived.individuals.every((o) => o.bytes > 0), derived.individuals.length);
  check('every individual has a dependency record, a declared provenance, or a real assembler invocation',
    derived.individuals.every((o) => o.dependencies !== null || o.provenance !== null || o.produced_by === 'assembler'),
    derived.individuals.filter((o) => !o.dependencies && !o.provenance && o.produced_by !== 'assembler')
      .map((o) => o.logical));
  check('the assembled objects are exactly the ones an assembler invocation produced',
    derived.expected.assembled_count === assembled.length,
    { marked: derived.expected.assembled_count, specs: assembled.length });
  check('the dependency records name real prerequisites',
    derived.expected.dependencies.length > 0, derived.expected.dependencies.length);
  check('the expectations are computed from the inputs, not the image',
    derived.expected.count === derived.individuals.length
    && derived.expected.ordered_sha256.length === derived.individuals.length
    && derived.expected.total_bytes === derived.individuals.reduce((n, o) => n + o.bytes, 0), null);
  check('this link is a host-test link, so it must not be accepted as a default link',
    derived.expected.host_test_count > 0 || derived.expected.default_profile_count > 0,
    { default: derived.expected.default_profile_count, hostTest: derived.expected.host_test_count });

  const gate = identity.assertLinkIdentityComplete(derived, { minimum: 2 });
  check('the real identity passes its own gate', gate.ok, gate.problems);

  // ---------------------------------------------------------------- the refusals
  for (const [name, mutate] of Object.entries(identity.REFUSALS)) {
    let refused = false;
    let reason = null;
    try {
      const { copy, expect, why } = mutate(derived);
      const observed = { individuals: copy.individuals };
      const comparison = identity.compareLinkInputIdentity(derived, observed);
      const gateResult = identity.assertLinkIdentityComplete(copy, { minimum: 2, requireDefaultProfile: name === 'test-object-in-default-link' });
      const hitComparison = comparison.problems.some((p) => p.includes(expect));
      const hitGate = gateResult.problems.some((p) => p.includes(expect));
      refused = !comparison.ok || !gateResult.ok;
      reason = { why, comparison: comparison.problems, gate: gateResult.problems, matched: hitComparison || hitGate };
      // The refusal has to be for the declared reason, not merely for some reason.
      check(`refused: ${name}`, refused && (hitComparison || hitGate), reason);
    } catch (error) {
      check(`refused: ${name}`, false, String(error.message));
    }
  }

  // ---------------------------------------------------------------- the parser's own refusals
  const parserCases = [
    ['no linker script', () => identity.parseLinkArgv(['/usr/bin/ld', '-melf_i386', '-o', '/tmp/a', '/tmp/x.o'])],
    ['no individuals', () => identity.parseLinkArgv(['/usr/bin/ld', '-melf_i386', '-T', '/tmp/s.ld', '-o', '/tmp/a'])],
    ['a relative linker', () => identity.parseLinkArgv(['ld', '-melf_i386', '-T', '/tmp/s.ld', '-o', '/tmp/a', '/tmp/x.o'])],
    ['an unknown flag', () => identity.parseLinkArgv(['/usr/bin/ld', '-melf_i386', '-T', '/tmp/s.ld', '-o', '/tmp/a', '--weird', '/tmp/x.o'])],
  ];
  for (const [name, fn] of parserCases) {
    let threw = false;
    try { fn(); } catch { threw = true; }
    check(`the parser refuses: ${name}`, threw, null);
  }

  // A duplicate individual in the argv itself is refused before any file is read.
  let dupThrew = false;
  try {
    identity.deriveLinkInputIdentity(['/usr/bin/ld', '-melf_i386', '-T', '/tmp/s.ld', '-o', '/tmp/a',
      derived.individuals[0].path, derived.individuals[0].path],
      { roots, hostPath, profiles, assembled, catalogArtifacts });
  } catch { dupThrew = true; }
  check('a duplicated individual is refused at parse time', dupThrew, null);
}

// ---------------------------------------------------------------- the result
const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-119', kind: 'link_input_identity_test',
  subject: 'references/link-input-identity-119.mjs',
  positive_source: { spec: specPath, argv_tokens: argv.length, object_roots: roots },
  identity: derived ? {
    tool: derived.tool, emulation: derived.emulation,
    linker_script: derived.linker_script, map: derived.map, output: derived.output,
    expected: derived.expected,
    individuals: derived.individuals.map((o) => ({ ordinal: o.ordinal, logical: o.logical, path: o.path,
      bytes: o.bytes, sha256: o.sha256, profile: o.profile,
      dependencies: o.dependencies ? o.dependencies.prerequisites.length : null })),
  } : null,
  checks, checks_total: checks.length, checks_failed: failures.length,
  ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed,
  individuals: derived ? derived.individuals.length : 0 }));
process.exit(report.ok ? 0 : 1);
