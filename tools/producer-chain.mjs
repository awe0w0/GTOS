// GTOS-DSH-123 P0: a consumer whose expectations come from a frozen contract, not from the evidence.
//
// The review of 122 was right about the shape of the defect. The old test read `receipt.buildSpecs` and then declared those
// same specs to be the "external" expectation, and took the ELF hash from the ELF it was about to approve. That proves the
// evidence agrees with itself. It cannot prove the build satisfied a requirement that existed before the build.
//
// This module reads a contract that was frozen to disk before the first compile (`contract-cpp-123.json`), and then reads
// the producer records that the build actually left behind:
//
//   compile spec  -> which source, which macros, which flags, which object
//   wave source   -> the bytes of that source at build time, hashed against the contract
//   link spec     -> the real ordered ld argv and the individuals it consumed
//   static gate   -> the ELF those individuals produced
//   run spec      -> cmd0 and the case argument the child was given
//   inner report  -> the child's own exit status and the raw log it wrote
//   raw log       -> the bytes that must name the requested case
//
// Every expectation is an argument that came from the contract file. Nothing is read back out of the receipt and then
// compared to itself.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

export function hostMapper(rules) {
  if (!Array.isArray(rules) || !rules.length) throw new Error('EVIDENCE: a host mapper needs rules');
  return (p) => {
    if (typeof p !== 'string' || !p) throw new Error('EVIDENCE: cannot map an empty path');
    if (!p.startsWith('/')) {
      if (!/^([A-Za-z]:[\\/]|\\\\)/.test(p)) throw new Error('EVIDENCE: neither a Linux nor a host path: ' + p);
      return p;
    }
    for (const r of rules) {
      if (p === r.linux) return r.host;
      if (p.startsWith(r.linux + '/')) return r.host + p.slice(r.linux.length);
    }
    throw new Error('EVIDENCE: no mapper rule places this path: ' + p);
  };
}

const readFile = (host, p, what) => {
  let b = null;
  for (let i = 0; i < 12 && b === null; i++) { try { b = fs.readFileSync(host(p)); } catch { /* retry */ } }
  if (b === null) throw new Error('EVIDENCE: absent ' + (what || 'file') + ': ' + p);
  return b;
};
const readJson = (host, p, what) => JSON.parse(readFile(host, p, what).toString('utf8'));

// ---------------------------------------------------------------- the frozen contract

// Reading the contract is deliberately separate from reading the evidence. A caller cannot pass an inline contract, because
// then the "expectation" could be written after looking at the build.
export function loadContract(host, contractPath) {
  const bytes = readFile(host, contractPath, 'contract');
  const c = JSON.parse(bytes.toString('utf8'));
  if (c.kind !== 'independent_cpp_contract') throw new Error('CONTRACT: not a frozen Cpp contract');
  if (c.frozen_before_build !== true) throw new Error('CONTRACT: the contract does not claim to predate the build');
  if (!c.expected || !Array.isArray(c.expected.subjects)) throw new Error('CONTRACT: no subject set');
  return { contract: c, path: contractPath, bytes: bytes.length, sha256: sha256(bytes) };
}

// ---------------------------------------------------------------- the producer chain

// Read the build, link, gate and run records for one tag. `tag` is the build directory name; the compile and link stages
// are derived from it, and their specs are opened.
export function readProducerChain(options) {
  const { reportRoot, sourceRoot, tag, runStage, host } = options;
  const wave = sourceRoot + '/wave-01';
  const buildDir = sourceRoot + '/test-build/' + tag;
  const logDir = sourceRoot + '/logs';

  // ---- compiles
  const compilePrefix = 'spec-c' + tag + '-';
  const compileSpecs = [];
  for (const name of fs.readdirSync(host(logDir)).sort()) {
    if (!name.startsWith(compilePrefix) || !name.endsWith('.json')) continue;
    const s = readJson(host, logDir + '/' + name, 'compile spec');
    const cmd = s.cmd;
    if (!Array.isArray(cmd)) throw new Error('EVIDENCE: compile spec ' + name + ' has no command');
    const sourceAt = cmd.findIndex((a) => typeof a === 'string' && a.startsWith(wave + '/'));
    const objAt = cmd.indexOf('-o');
    compileSpecs.push({
      stage: s.stage,
      spec: logDir + '/' + name,
      source: sourceAt >= 0 ? cmd[sourceAt] : null,
      object: objAt >= 0 ? cmd[objAt + 1] : null,
      defines: cmd.filter((a) => typeof a === 'string' && a.startsWith('-D')).map((a) => a.slice(2)),
      flags: cmd.filter((a) => typeof a === 'string' && a.startsWith('-') && !a.startsWith('-D')
        && !['-o', '-c', '-MMD', '-MP'].includes(a)),
      cmd0: cmd[0],
    });
  }

  // ---- the link, which is the producer the ELF actually came from
  const linkName = 'spec-l' + tag + '-01.json';
  const linkSpec = readJson(host, logDir + '/' + linkName, 'link spec');
  const linkCmd = linkSpec.cmd;
  if (!Array.isArray(linkCmd)) throw new Error('EVIDENCE: the link spec has no command');
  const individuals = linkCmd.filter((a) => typeof a === 'string' && a.endsWith('.o'));
  const outAt = linkCmd.indexOf('-o');
  const scriptAt = linkCmd.indexOf('-T');
  const link = {
    stage: linkSpec.stage,
    spec: logDir + '/' + linkName,
    cmd0: linkCmd[0],
    individuals,
    output: outAt >= 0 ? linkCmd[outAt + 1] : null,
    linkerScript: scriptAt >= 0 ? linkCmd[scriptAt + 1] : null,
  };

  // ---- the assembly, if this build had one
  const asmSpecs = [];
  for (const name of fs.readdirSync(host(logDir)).sort()) {
    if (!name.startsWith('spec-a' + tag + '-') || !name.endsWith('.json')) continue;
    const s = readJson(host, logDir + '/' + name, 'assembly spec');
    const cmd = s.cmd || [];
    const srcAt = cmd.findIndex((a) => typeof a === 'string' && /\.(s|S|asm)$/.test(a));
    const objAt = cmd.indexOf('-o');
    asmSpecs.push({ stage: s.stage, spec: logDir + '/' + name, cmd0: cmd[0],
      source: srcAt >= 0 ? cmd[srcAt] : null, object: objAt >= 0 ? cmd[objAt + 1] : null });
  }

  // ---- the ELF the link produced
  const elfLinux = buildDir + '/runtime_test.elf';
  const elfBytes = readFile(host, elfLinux, 'ELF');

  // ---- the static gate
  const gatePath = reportRoot + '/static-gate-' + runStage + '.json';
  const gate = fs.existsSync(host(gatePath)) ? readJson(host, gatePath, 'static gate') : null;

  // ---- the run
  const runSpecPath = logDir + '/spec-' + runStage + '.json';
  const runSpec = readJson(host, runSpecPath, 'run spec');
  const runArgv = runSpec.cmd || [];
  const innerPath = logDir + '/inner-report-' + runStage + '.json';
  const inner = readJson(host, innerPath, 'inner report');
  const child = inner.child;
  if (!child) throw new Error('EVIDENCE: the inner report has no child');
  const rawBytes = readFile(host, child.output_path, 'raw log');
  if (child.output_sha256 && sha256(rawBytes) !== child.output_sha256) {
    throw new Error('EVIDENCE: the raw log does not match the hash the inner report recorded');
  }

  // ---- the terminal, which is a separate record from the attempt
  const terminalPath = reportRoot + '/result-' + runStage + '-123.json';
  const terminal = readJson(host, terminalPath, 'terminal result');

  const attemptPath = reportRoot + '/attempt-' + runStage + '-123.json';
  const attempt = readJson(host, attemptPath, 'attempt');

  return {
    kind: 'cpp', tag, run_stage: runStage,
    contract_independent: true,
    compile_specs: compileSpecs,
    asm_specs: asmSpecs,
    link,
    elf: { linux: elfLinux, bytes: elfBytes.length, sha256: sha256(elfBytes) },
    gate,
    run: { spec: runSpecPath, cmd0: runArgv[0], argv: runArgv,
      executed_case: runArgv.length > 1 ? Number(runArgv[1]) : null },
    inner: { path: innerPath, returncode: child.returncode, timed_out: child.timed_out === true,
      output_sha256: child.output_sha256 || null, group_measured: child.group_measured === true },
    raw: { path: child.output_path, bytes: rawBytes.length, sha256: sha256(rawBytes),
      text: rawBytes.toString('utf8') },
    terminal: { path: terminalPath, ok: terminal.ok === true, stage: terminal.stage || null,
      has_plan: !!terminal.plan, has_transport: !!terminal.transport },
    attempt: { path: attemptPath, complete: attempt.complete === true,
      result_ok: attempt.result_ok === true, attempt_stage: attempt.attempt || null },
    read_set: [terminalPath, attemptPath, runSpecPath, innerPath, child.output_path, elfLinux,
      logDir + '/' + linkName, ...compileSpecs.map((c) => c.spec), ...asmSpecs.map((a) => a.spec),
      ...(gate ? [gatePath] : [])],
  };
}

// ---------------------------------------------------------------- the verdict

export function acceptProducerChain(chain, contractRecord) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const expected = contractRecord.contract.expected;

  // A record of another kind is refused because it is not this shape. There is no label to add.
  if (!chain || chain.kind !== 'cpp') {
    return { accepted: false, problems: [{ obligation: 'kind', why: 'the evidence is not a Cpp producer chain' }],
      first_refusal: { obligation: 'kind', why: 'not a Cpp producer chain' } };
  }

  // ---- the subject set, compared with the frozen list, not with the receipt's own list
  const builtSources = chain.compile_specs.map((c) => c.source).filter(Boolean)
    .map((s) => s.replace(/^.*\/wave-01\//, '')).sort();
  const expectedSources = expected.subjects.map((s) => s.path.replace(/^.*\/wave-01\//, '')).sort();
  if (JSON.stringify(builtSources) !== JSON.stringify(expectedSources)) {
    const missing = expectedSources.filter((s) => !builtSources.includes(s));
    const extra = builtSources.filter((s) => !expectedSources.includes(s));
    refuse('subjects', 'the built set is not the frozen set'
      + (missing.length ? '; missing ' + missing.join(',') : '')
      + (extra.length ? '; unexpected ' + extra.join(',') : ''));
  }

  // ---- every source byte at build time must be the byte the contract pinned
  const bySource = new Map(chain.compile_specs.filter((c) => c.source).map((c) => [c.source, c]));
  for (const subj of expected.subjects) {
    const spec = bySource.get(subj.path);
    if (!spec) continue; // already reported as a missing subject
    let bytes = null;
    try { bytes = readFileSafe(spec.source, contractRecord.host); } catch { bytes = null; }
    if (bytes === null) { refuse('source', 'cannot read the built source ' + subj.path); break; }
    if (bytes.length !== subj.bytes || sha256(bytes) !== subj.sha256) {
      refuse('source', 'the source at build time is not the pinned byte: ' + subj.path);
      break;
    }
  }

  // ---- the profile: every macro, on every subject, not just the first
  for (const spec of chain.compile_specs) {
    if (!spec.source) continue;
    const got = spec.defines.slice().sort();
    const want = expected.macros.slice().sort();
    if (JSON.stringify(got) !== JSON.stringify(want)) {
      refuse('profile', spec.source.split('/').pop() + ' built with [' + got.join(',') + '] not [' + want.join(',') + ']');
      break;
    }
    // The full flag set, token by token, not just the defines.
    const gotFlags = spec.flags.filter((f) => expected.cxxflags.includes(f)).sort();
    const wantFlags = expected.cxxflags.slice().sort();
    if (JSON.stringify(gotFlags) !== JSON.stringify(wantFlags)) {
      refuse('flags', spec.source.split('/').pop() + ' did not carry the full profile flag set');
      break;
    }
  }

  // ---- the assembly is not a C++ compile and must not be counted as one
  if (expected.assembly_sources.length) {
    const asmSources = chain.asm_specs.map((a) => a.source ? a.source.split('/').pop() : null).filter(Boolean);
    if (!asmSources.length) refuse('assembly', 'the contract expects an assembled entry and none ran');
  }

  // ---- the link is the real producer of the ELF
  if (!chain.link.cmd0 || !/ld/.test(chain.link.cmd0)) {
    refuse('link', 'the link cmd0 is not a linker: ' + chain.link.cmd0);
  }
  if (chain.link.output !== chain.elf.linux) {
    refuse('link', 'the link wrote ' + chain.link.output + ', not the ELF that ran');
  }
  // Every compiled object must appear among the link individuals, or the ELF did not come from that build.
  const individualSet = new Set(chain.link.individuals);
  for (const spec of chain.compile_specs) {
    if (spec.object && !individualSet.has(spec.object)) {
      refuse('link', 'a compiled object was not linked: ' + spec.object.split('/').pop());
      break;
    }
  }
  for (const a of chain.asm_specs) {
    if (a.object && !individualSet.has(a.object)) {
      refuse('link', 'the assembled object was not linked: ' + a.object.split('/').pop());
      break;
    }
  }

  // ---- the static gate names this ELF
  if (!chain.gate) refuse('gate', 'there is no static gate record');
  else if (chain.gate.passed !== true) refuse('gate', 'the static gate did not pass');
  else if (chain.gate.sha256 && chain.gate.sha256 !== chain.elf.sha256) {
    refuse('gate', 'the gate record names a different ELF');
  }

  // ---- the run is the ELF the link produced
  if (chain.run.cmd0 !== chain.elf.linux) {
    refuse('run', 'the child ran ' + chain.run.cmd0 + ', not the linked ELF');
  }
  // ---- the case comes from the child argv and must be the requested one
  if (chain.run.executed_case === null) refuse('case', 'the child argv names no case');
  else if (chain.run.executed_case !== expected.requested_case) {
    refuse('case', 'the child ran case ' + chain.run.executed_case + ', not ' + expected.requested_case);
  }

  // ---- the terminal is an independent record and must be complete
  if (chain.attempt.attempt_stage !== chain.run_stage) refuse('terminal', 'the attempt record is for another stage');
  if (chain.attempt.complete !== true) refuse('terminal', 'the attempt is not complete');
  if (chain.terminal.stage && chain.terminal.stage !== chain.run_stage) {
    refuse('terminal', 'the terminal record is for ' + chain.terminal.stage);
  }
  if (chain.terminal.ok !== true) refuse('terminal', 'the terminal record did not report success');

  // ---- the child
  if (chain.inner.timed_out) refuse('run', 'the child timed out');
  else if (chain.inner.returncode !== 0) refuse('run', 'the child exited ' + chain.inner.returncode);

  // ---- the raw log names the case and reports a pass
  const witness = new RegExp('CASE ' + expected.requested_case + '(?![0-9])');
  if (!witness.test(chain.raw.text)) refuse('witness', 'the raw log does not name case ' + expected.requested_case);
  if (/\bFAIL\b/.test(chain.raw.text) && chain.inner.returncode === 0) {
    refuse('result', 'the child reported FAIL but exited zero');
  }
  if (!/RUNTIME UNIT PASS/.test(chain.raw.text) && chain.inner.returncode === 0) {
    refuse('result', 'the child exited zero without reporting PASS');
  }

  const accepted = problems.length === 0;
  return { accepted, problems, first_refusal: problems.length ? problems[0] : null,
    observed: { tag: chain.tag, run_stage: chain.run_stage, subjects: chain.compile_specs.length,
      individuals: chain.link.individuals.length, elf_sha256: chain.elf.sha256,
      executed_case: chain.run.executed_case, child_returncode: chain.inner.returncode } };
}

// A helper the source check needs; kept separate so the read failure is distinguishable from a hash mismatch.
function readFileSafe(linux, host) {
  let b = null;
  for (let i = 0; i < 12 && b === null; i++) { try { b = fs.readFileSync(host(linux)); } catch { /* retry */ } }
  return b;
}

export function gateProducerChain(chain, contractRecord) {
  const v = acceptProducerChain(chain, contractRecord);
  if (v.accepted) return v;
  const e = new Error('EVIDENCE REFUSED at ' + v.first_refusal.obligation + ': ' + v.first_refusal.why);
  e.verdict = v;
  throw e;
}
