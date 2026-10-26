// GTOS-DSH-125 F0a: every producer of the current chain, opened cell by cell.
//
// The 124 report said the compiles succeeded because twelve specs existed. This test reads all fourteen producers and
// requires, for each one, that the transport started, a project child started, the tool returned zero, the terminal was
// written before the attempt completed, and the output file on disk is the one that producer wrote.
//
// The negatives below break one cell at a time and require the first refusal to name that cell. Each starts from the real
// chain, so a negative that is refused by an earlier cell would be visible as the wrong obligation.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import * as pr from '../tools/producer-results.mjs';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-producer-business-wiring-runtime-recovery-20261026-a';
const S = process.argv[5] || '/var/tmp/gtos-dsh-125-e759-producer-recovery-view';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

const host = pr.hostMapper([
  { linux: '/var/tmp', host: '//wsl$/Ubuntu-20.04/var/tmp' },
  { linux: '/mnt/f', host: 'F:' },
]);

// ---------------------------------------------------------------- the producer table
const OBJ = S + '/test-build/p1a/';
const producers = [
  { stage: 'ap1a-01', output: OBJ + 'runtime_start.o' },
  { stage: 'cp1a-01', output: OBJ + 'native_runtime.o' },
  { stage: 'cp1a-02', output: OBJ + 'gdt.o' },
  { stage: 'cp1a-03', output: OBJ + 'native_fp.o' },
  { stage: 'cp1a-04', output: OBJ + 'multitasking.o' },
  { stage: 'cp1a-05', output: OBJ + 'native_realtime.o' },
  { stage: 'cp1a-06', output: OBJ + 'physical.o' },
  { stage: 'cp1a-07', output: OBJ + 'process_address_space.o' },
  { stage: 'cp1a-08', output: OBJ + 'paging.o' },
  { stage: 'cp1a-09', output: OBJ + 'native_surface.o' },
  { stage: 'cp1a-10', output: OBJ + 'native_windows.o' },
  { stage: 'cp1a-11', output: OBJ + 'runtime_test.o' },
  { stage: 'cp1a-12', output: OBJ + 'runtime_boundary.o' },
  { stage: 'lp1a-01', output: OBJ + 'runtime_test.elf' },
];

let chain = null;
try {
  chain = pr.readChain({ reportRoot: R, sourceRoot: S, tag: 'p1a', producers, host,
    ledgerPath: R + '/producer-order-125.jsonl' });
  check('fourteen producers were opened', chain.rows.length === 14, chain.rows.length);
  check('the read set lists every record that was actually opened', chain.read_set.length >= 14 * 7,
    chain.read_set.length);
  check('the contract precedes every producer in the ledger', chain.ledger.contract_precedes_every_producer,
    { frozen_at: chain.ledger.contract_frozen_at_seq, first_spec_at: chain.ledger.first_spec_at_seq });
  check('the ledger froze the contract that is on disk', chain.ledger.contract_sha256 === chain.contract.sha256,
    { ledger: String(chain.ledger.contract_sha256).slice(0, 16), disk: chain.contract.sha256.slice(0, 16) });
  check('every producer names the contract it was prepared under',
    chain.rows.every((r) => r.spec.contract_pin && r.spec.contract_pin.sha256 === chain.contract.sha256),
    chain.rows.filter((r) => !r.spec.contract_pin).map((r) => r.stage));
  check('every producer has its own measured child process group',
    chain.rows.every((r) => r.inner.group_measured === true
      && Number.isInteger(r.inner.group_pgid) && r.inner.group_pgid > 0),
    chain.rows.map((r) => ({ stage: r.stage, pgid: r.inner.group_pgid })).slice(0, 3));
  check('every measured child actually took time, so a tool ran in each stage',
    chain.rows.every((r) => typeof r.inner.elapsed_seconds === 'number' && r.inner.elapsed_seconds > 0),
    chain.rows.map((r) => ({ stage: r.stage, seconds: r.inner.elapsed_seconds })).slice(0, 3));
  check('every producer tool returned zero', chain.rows.every((r) => r.inner.returncode === 0),
    chain.rows.filter((r) => r.inner.returncode !== 0).map((r) => [r.stage, r.inner.returncode]));
  check('every producer terminal was written before its attempt completed',
    chain.rows.every((r) => r.terminal.terminal_written_before_complete === true),
    chain.rows.filter((r) => r.terminal.terminal_written_before_complete !== true).map((r) => r.stage));
  check('every producer output exists with a real size',
    chain.rows.every((r) => r.output && r.output.bytes > 0),
    chain.rows.filter((r) => !r.output || r.output.bytes === 0).map((r) => r.stage));
  check('the raw logs were read, and an empty one is recorded as empty rather than missing',
    chain.rows.every((r) => typeof r.raw.bytes === 'number' && r.raw.sha256 === sha(fs.readFileSync(host(r.raw.path)))),
    chain.rows.filter((r) => r.raw.bytes === 0).length + ' of 14 raw logs are legitimately empty');
  check('the whole chain is accepted', chain.accepted, chain.verdicts.filter((v) => !v.accepted)
    .map((v, i) => ({ stage: chain.rows[i].stage, first: v.first_refusal })));

  // The tools are the real ones, by name, from the frozen run spec argv.
  const tools = Object.fromEntries(chain.rows.map((r) => [r.stage, r.run_spec.cmd0]));
  check('the compile cmd0 is the pinned g++-9',
    Object.entries(tools).filter(([s]) => s.startsWith('cp')).every(([, t]) => /g\+\+-9$/.test(t)),
    Object.entries(tools).filter(([s]) => s.startsWith('cp')).map(([s, t]) => t).slice(0, 2));
  check('the assembly cmd0 is the real assembler', /-as$/.test(tools['ap1a-01']), tools['ap1a-01']);
  check('the link cmd0 is ld.bfd', /ld\.bfd$/.test(tools['lp1a-01']), tools['lp1a-01']);

  // ---------------------------------------------------------------- one cell broken at a time
  const contractRecord = { path: chain.contract.path, bytes: chain.contract.bytes,
    sha256: chain.contract.sha256, contract: JSON.parse(fs.readFileSync(host(chain.contract.path), 'utf8')) };
  const negatives = [
    ['the transport never started', (r) => { r.terminal.transport_started = false; }, 'started'],
    ['the guard measured no process group', (r) => { r.inner.group_measured = false; }, 'child'],
    ['the process group has no positive id', (r) => { r.inner.group_pgid = 0; }, 'child'],
    ['the measured child took no time', (r) => { r.inner.elapsed_seconds = 0; }, 'child'],
    ['the producer timed out', (r) => { r.terminal.timed_out = true; }, 'timeout'],
    ['the producer reported a launch error', (r) => { r.terminal.launch_error = 'spawn failed'; }, 'launch'],
    ['the tool exited non-zero', (r) => { r.inner.returncode = 1; }, 'returncode'],
    ['the tool timed out', (r) => { r.inner.timed_out = true; }, 'timeout'],
    ['the attempt is for another stage', (r) => { r.attempt.attempt_stage = 'otherstage-01'; }, 'attempt'],
    ['the attempt is not complete', (r) => { r.attempt.complete = false; }, 'attempt'],
    ['the attempt did not report success', (r) => { r.attempt.result_ok = false; }, 'attempt'],
    ['the terminal did not report success', (r) => { r.terminal.ok = false; }, 'terminal'],
    ['the terminal was written after the attempt completed', (r) => {
      r.terminal.terminal_written_before_complete = false;
    }, 'terminal'],
    ['the spec was prepared under another contract', (r) => {
      r.spec.contract_pin = { ...r.spec.contract_pin, sha256: 'f'.repeat(64) };
    }, 'contract'],
    ['the spec carries no contract pin', (r) => { r.spec.contract_pin = null; }, 'contract'],
    ['the output is empty', (r) => { r.output = { ...r.output, bytes: 0 }; }, 'output'],
    ['the raw log does not match its recorded hash', (r) => {
      r.inner.output_sha256 = 'a'.repeat(64);
    }, 'raw'],
    ['the raw log is not the recorded size', (r) => { r.inner.output_bytes = r.raw.bytes + 1; }, 'raw'],
  ];
  for (const [name, mutate, obligation] of negatives) {
    const copy = JSON.parse(JSON.stringify(chain.rows[5]));
    mutate(copy);
    const v = pr.acceptProducer(copy, contractRecord);
    check(`refused at ${obligation}: ${name}`, !v.accepted && v.first_refusal.obligation === obligation,
      v.first_refusal);
  }

  // A producer that is genuinely a different stage cannot be accepted under this one.
  {
    const other = JSON.parse(JSON.stringify(chain.rows[5]));
    other.stage = 'cp1a-99';
    const v = pr.acceptProducer(other, contractRecord);
    check('a record from another stage is refused', !v.accepted && v.first_refusal.obligation === 'attempt',
      v.first_refusal);
  }

  // The gate throws rather than returning false.
  {
    let threw = null;
    try { pr.gateProducer({ stage: 'cp1a-01' }, contractRecord); } catch (e) { threw = e; }
    check('the gate throws', threw !== null && /PRODUCER REFUSED/.test(String(threw.message)),
      threw ? threw.message : null);
    let passed = false;
    try { pr.gateProducer(chain.rows[5], contractRecord); passed = true; } catch { passed = false; }
    check('the gate lets a real producer through', passed, null);
  }

  // ---------------------------------------------------------------- the ledger refuses a gap
  {
    const lines = fs.readFileSync(host(R + '/producer-order-125.jsonl'), 'utf8').split(/\r?\n/).filter(Boolean);
    const broken = [lines[0], lines[2], ...lines.slice(3)].join('\n') + '\n';
    const p = R + '/test-only/ledger-gap.jsonl';
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, broken);
    let caught = false;
    try { pr.readOrderLedger(host, p); } catch (e) { caught = /gap/.test(String(e.message)); }
    check('a ledger with a gap is refused', caught, caught);
  }
  // A ledger whose first line is not the contract is refused too: that is the ordering claim itself.
  {
    const lines = fs.readFileSync(host(R + '/producer-order-125.jsonl'), 'utf8').split(/\r?\n/).filter(Boolean);
    const rows = lines.map((l) => JSON.parse(l));
    // A valid ledger whose contract line simply comes last, renumbered so the sequence is still contiguous. This is the
    // shape of a build that ran before its contract was frozen.
    const moved = [...rows.filter((r) => r.event !== 'contract-frozen'),
      ...rows.filter((r) => r.event === 'contract-frozen')];
    moved.forEach((r, i) => { r.seq = i + 1; });
    const p = R + '/test-only/ledger-contract-last.jsonl';
    fs.writeFileSync(p, moved.map((r) => JSON.stringify(r)).join('\n') + '\n');
    const led = pr.readOrderLedger(host, p);
    check('a ledger whose contract does not come first is not an ordering proof',
      led.contract_precedes_every_producer === false,
      { frozen_at: led.contract_frozen_at_seq, first_spec_at: led.first_spec_at_seq });
  }
} catch (error) {
  check('the producer table was built', false, String(error.message));
}

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-125', kind: 'producer_results_test',
  subject: 'references/producer-results-125.mjs',
  chain: chain ? { tag: chain.tag, contract: chain.contract, ledger: {
    path: chain.ledger.path, bytes: chain.ledger.bytes, sha256: chain.ledger.sha256,
    rows: chain.ledger.rows.length,
    contract_frozen_at_seq: chain.ledger.contract_frozen_at_seq,
    first_spec_at_seq: chain.ledger.first_spec_at_seq },
    producers: chain.rows.map((r) => ({ stage: r.stage, cmd0: r.run_spec.cmd0,
      returncode: r.inner.returncode, output: r.output, raw_bytes: r.raw.bytes })),
    read_set: chain.read_set } : null,
  checks, checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
};
const out = process.argv[3];
if (out) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
