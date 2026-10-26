// GTOS-DSH-123 P0: the producer chain, against this packet's real build and run.
//
// Three things are different from the 122 test, and each one answers a specific criticism.
//
// 1. The expectations come from `contract-cpp-123.json`, which was frozen to disk after staging and before the first
//    compile. The test does not read the build and then call it the expectation.
// 2. The "foreign record" negatives use real locators: the link spec, a compile spec, and a real git fixture commit. They
//    are read from disk. They are not `{kind:'git'}` literals written in this file.
// 3. The case negative keeps the stage valid and changes only the requested case, so it is the case obligation that
//    refuses and not an earlier gate.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as pc from '../tools/producer-chain.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-admitted-default-link-and-tested-consumer-integration-20261026-a';
const S = process.argv[5] || '/var/tmp/gtos-dsh-123-e759-consumer-integration-view';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const host = pc.hostMapper([
  { linux: '/var/tmp', host: '//wsl$/Ubuntu-20.04/var/tmp' },
  { linux: '/mnt/f', host: 'F:' },
]);

// ---------------------------------------------------------------- the frozen contract
const contractRecord = pc.loadContract(host, R + '/contract-cpp-123.json');
check('the contract was read from disk and predates the build',
  contractRecord.contract.frozen_before_build === true, contractRecord.sha256);
check('the contract states ten subjects, three test sources and thirteen headers',
  contractRecord.contract.expected.subjects.length === 10
  && contractRecord.contract.expected.test_sources.length === 3
  && contractRecord.contract.expected.headers.length === 13,
  { subjects: contractRecord.contract.expected.subjects.length,
    tests: contractRecord.contract.expected.test_sources.length,
    headers: contractRecord.contract.expected.headers.length });
check('the contract names seven seam macros and five profile flags',
  contractRecord.contract.expected.macros.length === 7
  && contractRecord.contract.expected.cxxflags.length === 5,
  contractRecord.contract.expected.macros);

// ---------------------------------------------------------------- the real chain
let chain = null;
try {
  chain = pc.readProducerChain({ reportRoot: R, sourceRoot: S, tag: 'p1a', runStage: 'case01-01', host });
  // The mapper is a function and cannot be cloned; the record carries it for the source check only.
  contractRecord.host = host;
  check('the chain opened the real producer records', chain.read_set.length >= 15, chain.read_set.length);
  check('twelve compile specs were read', chain.compile_specs.length === 12, chain.compile_specs.length);
  check('the link cmd0 is a real linker', /ld/.test(chain.link.cmd0), chain.link.cmd0);
  check('the link consumed sixteen individuals', chain.link.individuals.length === 16, chain.link.individuals.length);
  check('the assembly was recorded as assembly, not as a compile',
    chain.asm_specs.length === 1 && /as$/.test(chain.asm_specs[0].cmd0), chain.asm_specs);
  check('the terminal is an independent record, not the attempt',
    chain.terminal.path !== chain.attempt.path && fs.existsSync(host(chain.terminal.path)),
    { terminal: chain.terminal.path, attempt: chain.attempt.path });
  check('the executed case came from the run spec argv', chain.run.executed_case === 1, chain.run.executed_case);
} catch (error) {
  check('the chain opened the real producer records', false, String(error.message));
}

if (chain) {
  const verdict = pc.acceptProducerChain(chain, contractRecord);
  check('the real producer chain is accepted', verdict.accepted, verdict.problems);

  // ---------------------------------------------------------------- real foreign locators
  // Each of these is a record that a real stage in this packet really wrote. The consumer is handed its actual shape.
  const realForeign = [
    ['the real link spec of this build', () => JSON.parse(fs.readFileSync(host(chain.link.spec), 'utf8'))],
    ['the real compile spec of this build', () => JSON.parse(fs.readFileSync(host(chain.compile_specs[0].spec), 'utf8'))],
    ['the real assembly spec of this build', () => JSON.parse(fs.readFileSync(host(chain.asm_specs[0].spec), 'utf8'))],
    ['the real run spec of this build', () => JSON.parse(fs.readFileSync(host(chain.run.spec), 'utf8'))],
  ];
  for (const [name, load] of realForeign) {
    let record = null;
    try { record = load(); } catch (error) { check(`refused a real foreign record: ${name}`, false, String(error.message)); continue; }
    // The outer expectation is unchanged: this is meant to be a Cpp producer chain.
    const v = pc.acceptProducerChain(record, contractRecord);
    check(`refused a real foreign record: ${name}`,
      !v.accepted && v.first_refusal.obligation === 'kind', v.first_refusal);
  }

  // ---------------------------------------------------------------- obligations broken one at a time
  const mutations = [
    ['the run stage is another stage', (c) => { c.run_stage = 'otherstage-01'; }, 'terminal'],
    ['the attempt is incomplete', (c) => { c.attempt.complete = false; }, 'terminal'],
    ['the terminal did not report success', (c) => { c.terminal.ok = false; }, 'terminal'],
    // The last individual is a catalog object this build did not compile, so removing it would not test anything. The
    // mutation removes a real compiled object of this build from the link.
    ['a compiled object was not linked', (c) => {
      const victim = c.compile_specs.find((s) => s.object && s.object.endsWith('/native_runtime.o'));
      c.link.individuals = c.link.individuals.filter((o) => o !== victim.object);
    }, 'link'],
    ['the link cmd0 is not a linker', (c) => { c.link.cmd0 = '/usr/bin/cat'; }, 'link'],
    ['the link wrote a different file', (c) => { c.link.output = '/tmp/elsewhere.elf'; }, 'link'],
    ['there is no static gate', (c) => { c.gate = null; }, 'gate'],
    ['the gate did not pass', (c) => { c.gate = { ...c.gate, passed: false }; }, 'gate'],
    ['the gate names a different ELF', (c) => { c.gate = { ...c.gate, sha256: 'a'.repeat(64) }; }, 'gate'],
    // The last compile spec is a test source, not one of the contract subjects, so popping it would leave the subject set
    // intact. The mutation drops a real subject instead.
    ['a subject is missing from the build', (c) => {
      const victim = c.compile_specs.findIndex((s) => s.source && s.source.endsWith('/src/gdt.cpp'));
      c.compile_specs.splice(victim, 1);
    }, 'subjects'],
    ['one subject was built with the wrong macros', (c) => { c.compile_specs[3].defines = ['GTOS_SOMETHING_ELSE']; }, 'profile'],
    ['one subject lost the profile flags', (c) => { c.compile_specs[3].flags = []; }, 'flags'],
    ['the assembly did not run', (c) => { c.asm_specs = []; }, 'assembly'],
    ['the child ran a different ELF', (c) => { c.run.cmd0 = '/tmp/other.elf'; }, 'run'],
    ['the child timed out', (c) => { c.inner.timed_out = true; }, 'run'],
    ['the child exited non-zero', (c) => { c.inner.returncode = 2; }, 'run'],
    ['the child argv names no case', (c) => { c.run.executed_case = null; }, 'case'],
    ['the raw log does not name the case', (c) => { c.raw = { ...c.raw, text: 'nothing here' }; }, 'witness'],
    ['the child reported FAIL but exited zero', (c) => { c.raw = { ...c.raw, text: 'CASE 1\nFAIL x\n' }; }, 'result'],
    ['the child exited zero without PASS', (c) => { c.raw = { ...c.raw, text: 'CASE 1\nother\n' }; }, 'result'],
  ];
  for (const [name, mutate, obligation] of mutations) {
    const copy = JSON.parse(JSON.stringify(chain));
    mutate(copy);
    const v = pc.acceptProducerChain(copy, contractRecord);
    check(`refused: ${name}`, !v.accepted && v.first_refusal.obligation === obligation,
      { first: v.first_refusal });
  }

  // ---------------------------------------------------------------- the case obligation on its own
  // Same stage, same build, same ELF, same gate: only the requested case changes. If this were refused by an earlier gate
  // the test would not be testing the case rule.
  {
    // The record carries the mapper function, which structuredClone cannot carry, so the copy is built by hand and the
    // mapper is reattached. Everything the verdict reads is plain data.
    const caseContract = { path: contractRecord.path, bytes: contractRecord.bytes,
      sha256: contractRecord.sha256, host,
      contract: JSON.parse(JSON.stringify(contractRecord.contract)) };
    caseContract.contract.expected.requested_case = 6;
    const v = pc.acceptProducerChain(chain, caseContract);
    check('only the requested case changed, and the case rule is what refused',
      !v.accepted && v.first_refusal.obligation === 'case', v.first_refusal);
    check('nothing earlier than the case refused',
      v.problems.every((p) => p.obligation === 'case' || p.obligation === 'witness'),
      v.problems.map((p) => p.obligation));
  }

  // ---------------------------------------------------------------- the gate throws
  let threw = null;
  try { pc.gateProducerChain({ kind: 'git' }, contractRecord); } catch (e) { threw = e; }
  check('the downstream gate throws rather than returning false',
    threw !== null && /EVIDENCE REFUSED/.test(String(threw.message)), threw ? threw.message : null);
  let passed = false;
  try { pc.gateProducerChain(chain, contractRecord); passed = true; } catch { passed = false; }
  check('the gate lets the real chain through', passed, null);

  // ---------------------------------------------------------------- a source that changed after the build
  // The contract pins the source bytes. If the wave source is replaced after the build, the chain must refuse even though
  // the object and the ELF are unchanged. This is the "reuse an old object with a new source" case.
  {
    const testOnly = R + '/test-only/source-drift';
    fs.mkdirSync(testOnly, { recursive: true });
    const victim = chain.compile_specs.find((c) => c.source).source;
    const original = fs.readFileSync(host(victim));
    const copy = JSON.parse(JSON.stringify(chain));
    // Point the compile spec at a TEST_ONLY copy whose bytes differ, and leave everything else alone.
    const drifted = testOnly + '/drifted.cpp';
    fs.writeFileSync(drifted, Buffer.concat([original, Buffer.from('// drift\n')]));
    copy.compile_specs = copy.compile_specs.map((c) => c.source === victim ? { ...c, source: drifted } : c);
    const v = pc.acceptProducerChain(copy, contractRecord);
    check('a source that is not the pinned byte is refused',
      !v.accepted && (v.first_refusal.obligation === 'source' || v.first_refusal.obligation === 'subjects'),
      v.first_refusal);
  }
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-123', kind: 'producer_chain_test',
  subject: 'references/producer-chain-123.mjs',
  contract: { path: contractRecord.path, sha256: contractRecord.sha256 },
  chain: chain ? { tag: chain.tag, run_stage: chain.run_stage, read_set: chain.read_set,
    subjects: chain.compile_specs.length, individuals: chain.link.individuals.length } : null,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
