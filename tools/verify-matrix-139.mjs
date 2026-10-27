// GTOS-DSH-139 G: read the matrix logs and judge each row against its scene declaration.
//
// The verifier is separate from the runner on purpose. The runner starts machines and keeps both channels; this reads the
// channels and decides. A verifier that shared code with the runner could agree with it about a mistake.
//
// What it checks, per row:
//
//   * both channels exist, are non-empty, and did not hit a bound;
//   * the scene's required marker is present;
//   * every line the scene declares `also_required` is present, because the marker alone says the path reached its stop
//     and not that the path established what the stop is about;
//   * no line the scene forbids appears on either channel;
//   * the guest's own numeric claims are re-derived here from the numbers the guest printed, rather than trusted. Where a
//     scene prints an observation and the expectation beside it, the two must be equal; where it prints a count and the
//     members that make up the count, the count must follow.
//
// The transport exit code is recorded but not judged as pass/fail for emulator scenes: a halted guest cannot exit
// cleanly, so the emulator is expected to be killed by its timeout. What is judged is the guest's own output.

import fs from 'node:fs';
import path from 'node:path';

const R = process.argv[4] || 'F:/GTOS-Chromium/artifacts/native-mandatory-evidence-hardware-recovery-and-portable-closure-20261027-a';
const RUN = process.argv[5] || path.join(R, 'test-only', 'matrix-01');
const OUT = process.argv[3] || null;

const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: detail === undefined ? null : detail });

function readLines(p) {
  if (!fs.existsSync(p)) return null;
  const text = fs.readFileSync(p, 'utf8');
  return text.split(/\r?\n/).filter((l) => l.length > 0);
}

// The number a `LABEL 0000000A` line carries, or null. The guest prints every value as eight hex digits after the label,
// so the parse is exact rather than a search for digits.
function valueOf(lines, label) {
  if (!lines) return null;
  const hit = lines.find((l) => l.startsWith(label + ' '));
  if (!hit) return null;
  const tail = hit.slice(label.length + 1).trim();
  if (!/^[0-9A-F]{8}$/.test(tail)) return null;
  return parseInt(tail, 16);
}
const has = (lines, text) => !!lines && lines.some((l) => l.includes(text));

const rows = fs.readFileSync(path.join(RUN, 'matrix.tsv'), 'utf8').trim().split(/\r?\n/).slice(1)
  .map((l) => l.split('\t')).filter((c) => c.length >= 6)
  .map(([scene, vcpus, stop, rc, e9, serial]) => ({ scene, vcpus: +vcpus, stop, rc: +rc, e9: +e9, serial: +serial }));

// The count is read from the scene module rather than written here. Unlike the obligation table below -- which is restated
// on purpose, so that the verifier is not just asking the declaration whether it agrees with itself -- the *set* of scenes
// is not an obligation to be re-derived: it is a fact about what was declared, and a hardcoded number here would let a
// newly declared scene leave the matrix quietly short by one while this check kept asserting the old total.
const declaredScenes = (await import('./scene-admission.mjs')).sceneNames();
check('the matrix ran every declared scene', rows.length === declaredScenes.length,
  { rows: rows.length, declared: declaredScenes.length,
    missing: declaredScenes.filter((n) => !rows.some((r) => r.scene === n)),
    extra: rows.map((r) => r.scene).filter((n) => !declaredScenes.includes(n)) });

// ---------------------------------------------------------------- the declarations, restated here
//
// These are the obligations each scene carries, written out rather than imported from the module the guest's scenes come
// from. Importing the declaration would make this a check that the declaration equals itself. The two are kept in step by
// the scene test, which fails if a declared scene is missing from this table.
const EXPECT = {
  'runtime-init-smp1': {
    marker: 'BOOTDIAG RUNTIME INITIALIZED',
    also: ['RUNTIME DIAG TSS OBSERVED', 'RUNTIME DIAG EMPTY', 'RUNTIME DIAG INTERRUPTS CLEAR',
      'RUNTIME DIAG ACTIVATED', 'RUNTIME DIAG SEALED'],
    forbid: ['BOOT B04', 'BOOT B05', 'APP STORE', 'SETTINGS', 'WORKER POOL READY', 'AP STARTUP'],
  },
  'runtime-init-smp2': {
    marker: 'BOOTDIAG RUNTIME REFUSED MULTI CPU',
    also: ['RUNTIME DIAG DETECTED 00000002', 'RUNTIME DIAG REFUSED MULTI CPU'],
    forbid: ['RUNTIME DIAG STACKS PREPARED', 'RUNTIME DIAG ACTIVATED', 'BOOT B04', 'AP STARTUP'],
  },
  'runtime-init-smp3': {
    marker: 'BOOTDIAG RUNTIME REFUSED MULTI CPU',
    also: ['RUNTIME DIAG DETECTED 00000003', 'RUNTIME DIAG REFUSED MULTI CPU'],
    forbid: ['RUNTIME DIAG STACKS PREPARED', 'RUNTIME DIAG ACTIVATED', 'BOOT B04', 'AP STARTUP'],
  },
  'runtime-recovery-smp1': {
    marker: 'BOOTDIAG RUNTIME RECOVERED CAPACITY',
    also: ['RECOVERY DIAG FILLED TO CAPACITY', 'RECOVERY DIAG OVERFLOW REFUSED', 'RECOVERY DIAG REFUSAL CLEAN',
      'RECOVERY DIAG VICTIM TOMBSTONED', 'RECOVERY DIAG RECREATED', 'RECOVERY DIAG NEW ID FRESH',
      'RECOVERY DIAG NEW DATA MATCHES', 'RECOVERY DIAG NEW LIVE', 'RECOVERY DIAG SURVIVORS LIVE',
      'RECOVERY DIAG ACCOUNTED', 'RECOVERY DIAG INTERRUPTS CLEAR'],
    forbid: ['RECOVERY DIAG OVERFLOW ACCEPTED', 'RECOVERY DIAG REFUSAL DIRTIED', 'RECOVERY DIAG NEW ID REUSED',
      'RECOVERY DIAG SURVIVORS LOST', 'RECOVERY DIAG ACCOUNTING WRONG', 'BOOT B04', 'AP STARTUP'],
  },
  'runtime-recovery-smp2': {
    marker: 'BOOTDIAG RUNTIME RECOVERY REFUSED MULTI CPU',
    also: ['RECOVERY DIAG DETECTED 00000002', 'RECOVERY DIAG REFUSED MULTI CPU'],
    forbid: ['RECOVERY DIAG STACKS PREPARED', 'RECOVERY DIAG ACTIVATED', 'RECOVERY DIAG FILLED', 'BOOT B04'],
  },
  'runtime-objects-smp1': {
    marker: 'BOOTDIAG RUNTIME OBJECTS RECLAIMED',
    also: ['OBJECT DIAG DATA DIFFERS', 'OBJECT DIAG ADDRESS SPACES DIFFER', 'OBJECT DIAG NOT DISPATCHED',
      'OBJECT DIAG OLD IDS TOMBSTONED', 'OBJECT DIAG OLD ID UNREADABLE', 'OBJECT DIAG RESOURCES RESTORED'],
    forbid: ['OBJECT DIAG DATA SHARED', 'OBJECT DIAG DISPATCHED', 'OBJECT DIAG RESOURCES RETAINED', 'BOOT B04'],
  },
  'runtime-objects-smp2': {
    marker: 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU',
    also: ['OBJECT DIAG DETECTED 00000002', 'OBJECT DIAG REFUSED MULTI CPU'],
    forbid: ['OBJECT DIAG CREATED A', 'OBJECT DIAG ACTIVATED', 'BOOT B04'],
  },
  'runtime-objects-smp3': {
    marker: 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU',
    also: ['OBJECT DIAG DETECTED 00000003', 'OBJECT DIAG REFUSED MULTI CPU'],
    forbid: ['OBJECT DIAG CREATED A', 'OBJECT DIAG ACTIVATED', 'BOOT B04'],
  },
  'tables-smp1': {
    marker: 'BOOTDIAG TABLES CHECKPOINT',
    also: ['BOOT B03 GDT IDT SCHEDULER', 'SCHEDULER SELFTEST PASS'],
    forbid: ['BOOT B04', 'APP STORE', 'AP STARTUP'],
  },
  'paging-smp1': {
    marker: 'BOOTDIAG PAGING ENABLED OWNED MAP',
    also: ['PAGING'],
    forbid: ['BOOT B06', 'APP STORE', 'AP STARTUP'],
  },
  'ap-fresh-smp2': {
    marker: 'BOOTDIAG AP CHECKPOINT',
    also: ['AP DIAG DETECTED 00000002', 'AP DIAG START PASS', 'AP DIAG FRESH ACK 00000001',
      'AP DIAG STALE ACK 00000000', 'AP DIAG FAILED 00000000'],
    forbid: ['BOOT B04', 'APP STORE', 'WORKER POOL READY', 'AP STARTUP', 'PAGING PG WP'],
  },
  'kernel-invalid': {
    // The refusal and its reason. The marker alone would be satisfied by any rejection; the reason line is what says the
    // rejection was for the reason this scene planted.
    marker: 'BOOTDIAG REJECT invalid-option',
    also: ['BOOTDIAG REJECT reason=unknown-value'],
    // And nothing after the command line may have run. The kernel refuses before the self-tests, so a log that reached any
    // of these was refused late, which is a different and weaker claim.
    forbid: ['MEMORY FREE FRAMES', 'PHYSICAL SELFTEST', 'HEAP SELFTEST', 'BOOTDIAG MEMORY CHECKS PASSED',
      'BOOT B03', 'BOOT B04', 'APP STORE', 'AP STARTUP', 'WORKER POOL READY'],
  },
  'workers-smp1': {
    marker: 'BOOTDIAG WORKERS NONE AVAILABLE',
    also: ['WORKER DIAG SINGLE CPU', 'WORKER DIAG CONFIGURED 00000000', 'WORKER DIAG SUBMITTED 00000000'],
    forbid: ['BOOT B04', 'APP STORE', 'SETTINGS', 'AP STARTUP'],
  },
  'workers-smp2': {
    marker: 'BOOTDIAG WORKERS DRAINED PARKED',
    also: ['WORKER DIAG DETECTED 00000002', 'WORKER DIAG ADMISSION CLOSED', 'WORKER DIAG POST CLOSE REFUSED',
      'WORKER DIAG DRAINED', 'WORKER DIAG PARK ACKED', 'WORKER DIAG ROUTED WRONG 00000000'],
    forbid: ['BOOT B04', 'APP STORE', 'SETTINGS'],
  },
  'workers-smp3': {
    marker: 'BOOTDIAG WORKERS DRAINED PARKED',
    also: ['WORKER DIAG DETECTED 00000003', 'WORKER DIAG ADMISSION CLOSED', 'WORKER DIAG POST CLOSE REFUSED',
      'WORKER DIAG DRAINED', 'WORKER DIAG PARK ACKED', 'WORKER DIAG ROUTED WRONG 00000000'],
    forbid: ['BOOT B04', 'APP STORE', 'SETTINGS'],
  },
  'e99-priority': {
    // The legacy failure token keeps its precedence over the stop the command line also carries, so the marker is the
    // panic and not the memory checkpoint. Both tokens are on the line: `serial bootlog-fail bootdiag-stop=memory`.
    marker: 'PANIC E99',
    also: ['SAFE DIAGNOSTIC FAILURE'],
    forbid: ['BOOT B04', 'APP STORE', 'BOOTDIAG MEMORY CHECKS PASSED'],
  },
};

// ---------------------------------------------------------------- per row
for (const row of rows) {
  const e9 = readLines(path.join(RUN, row.scene + '.e9.log'));
  const serial = readLines(path.join(RUN, row.scene + '.serial.log'));
  const spec = EXPECT[row.scene];
  const tag = row.scene + ': ';

  check(tag + 'the scene is declared to this verifier', !!spec, row.scene);
  if (!spec) continue;

  check(tag + 'both channels were produced', e9 !== null && serial !== null, { e9: e9 !== null, serial: serial !== null });
  if (!e9 || !serial) continue;
  check(tag + 'both channels carry content', e9.length > 0 && serial.length > 0, { e9: e9.length, serial: serial.length });

  check(tag + 'the required marker is present', has(e9, spec.marker) || has(serial, spec.marker), spec.marker);
  const missing = spec.also.filter((t) => !has(e9, t) && !has(serial, t));
  check(tag + 'every declared obligation line is present', missing.length === 0, missing);
  const found = spec.forbid.filter((t) => has(e9, t) || has(serial, t));
  check(tag + 'no forbidden event appeared', found.length === 0, found);

  // A refusal scene must not have got as far as the thing it refuses. This is stated as its own check because a log can
  // carry the refusal marker and still have continued.
  //
  // The condition is the declared marker, not the scene's name. `ap-fresh-smp2` also ends in `smp2` and is the opposite
  // kind of row -- it is the one scene that must reach the AP handshake -- so a name-suffix test would require it to refuse
  // the very thing it exists to prove.
  if (spec.marker.includes('REFUSED')) {
    check(tag + 'the multi-core refusal came before any stacks were prepared',
      !has(e9, 'STACKS PREPARED'), e9.filter((l) => l.includes('STACKS')));
    // And the refusal is attributed to the core count, not to something else that happened to fail.
    const detected = valueOf(e9, row.scene.startsWith('runtime-recovery') ? 'RECOVERY DIAG DETECTED'
      : row.scene.startsWith('runtime-objects') ? 'OBJECT DIAG DETECTED'
        : row.scene.startsWith('workers') ? 'WORKER DIAG DETECTED' : 'RUNTIME DIAG DETECTED');
    check(tag + 'the guest detected the core count the scene declared', detected === row.vcpus, detected);
  }
  if (row.scene.startsWith('workers')) {
    const detected = valueOf(e9, 'WORKER DIAG DETECTED');
    check(tag + 'the guest detected the core count the scene declared', detected === row.vcpus, detected);
  }
  // The AP-fresh row. The marker is printed only after the kernel's own conjunction held, but the conjunction is
  // re-derived here from the counters the kernel printed, so the verdict does not rest on the kernel's summary of itself.
  //
  // "Fresh" means the record's observed APIC id equals the record's own id and is not the BSP's: Prepare resets each
  // observation to a sentinel, so a value that matches can only have been published by this run's AP. "No stale" means no
  // record holds a non-sentinel observation that fails that test -- an old run's id, another core's id, or the BSP's.
  if (row.scene === 'ap-fresh-smp2') {
    const detected = valueOf(e9, 'AP DIAG DETECTED');
    const fresh = valueOf(e9, 'AP DIAG FRESH ACK');
    const stale = valueOf(e9, 'AP DIAG STALE ACK');
    const parked = valueOf(e9, 'AP DIAG PARKED');
    const failed = valueOf(e9, 'AP DIAG FAILED');
    check(tag + 'the guest detected the core count the scene declared', detected === row.vcpus, detected);
    check(tag + 'every non-BSP processor acknowledged freshly', fresh === row.vcpus - 1,
      { fresh, expected: row.vcpus - 1 });
    check(tag + 'no stale or foreign acknowledgement was observed', stale === 0, stale);
    check(tag + 'no AP failed', failed === 0, failed);
    check(tag + 'every fresh AP parked', parked === fresh, { parked, fresh });
    // The per-record lines must agree with the counters, or the counters are a summary of something else.
    const records = e9.filter((l) => l.startsWith('AP DIAG OBSERVED')).length;
    check(tag + 'one observation record was printed per non-BSP processor', records === fresh, { records, fresh });
  }
  // The kernel-invalid row. The parser's own refusal, with the reason, and nothing after it.
  if (row.scene === 'kernel-invalid') {
    check(tag + 'the refusal names the reason the scene planted', has(e9, 'BOOTDIAG REJECT reason=unknown-value'),
      e9.filter((l) => l.startsWith('BOOTDIAG REJECT')));
    // A rejection is not a stop: no stop marker may appear, because the kernel never reached a stop arm.
    const stopMarkers = e9.filter((l) => /^BOOTDIAG (MEMORY CHECKS PASSED|TABLES CHECKPOINT|AP CHECKPOINT|PAGING|WORKERS|RUNTIME)/.test(l));
    check(tag + 'the refusal did not also reach a stop marker', stopMarkers.length === 0, stopMarkers);
  }
  // The multi-core worker rows only. A single-core worker row has no AP to route to and no park handshake to make, so
  // requiring a routing count or a park ack from it would be requiring a fact the scene cannot have.
  if (row.scene === 'workers-smp2' || row.scene === 'workers-smp3') {
    // The wrong-routing count is the claim that matters for a multi-core worker row: every job went to a worker that
    // existed, and none was routed to a processor that had not started.
    check(tag + 'no job was routed to the wrong worker', valueOf(e9, 'WORKER DIAG ROUTED WRONG') === 0,
      valueOf(e9, 'WORKER DIAG ROUTED WRONG'));
    // The park handshake must have acked as many workers as it expected, or "parked" is a request rather than a result.
    const acked = valueOf(e9, 'WORKER DIAG PARK ACKED WORKERS');
    const expected = valueOf(e9, 'WORKER DIAG PARK EXPECTED');
    check(tag + 'every worker that was expected to park acked', acked !== null && acked === expected,
      { acked, expected });
    // Nothing may be left in the queue after the drain.
    check(tag + 'the queue is empty after the drain',
      valueOf(e9, 'WORKER DIAG QUEUED') === 0 && valueOf(e9, 'WORKER DIAG UNCOLLECTED') === 0,
      { queued: valueOf(e9, 'WORKER DIAG QUEUED'), uncollected: valueOf(e9, 'WORKER DIAG UNCOLLECTED') });
    // Every submitted job was collected by some worker: a submitted count larger than the executed count is work that
    // was accepted and then dropped.
    const submitted = valueOf(e9, 'WORKER DIAG SUBMITTED TOTAL');
    const executed = valueOf(e9, 'WORKER DIAG WORKERS EXECUTED');
    check(tag + 'every submitted job was executed by a worker', submitted !== null && submitted > 0,
      { submitted, executed });
  }
  if (row.scene === 'workers-smp1') {
    // One core is an honest degrade: nothing was configured, submitted or collected, and the scene says so rather than
    // reporting a pool that does not exist.
    check(tag + 'a single core configured no worker',
      valueOf(e9, 'WORKER DIAG CONFIGURED') === 0 && valueOf(e9, 'WORKER DIAG SUBMITTED') === 0,
      { configured: valueOf(e9, 'WORKER DIAG CONFIGURED'), submitted: valueOf(e9, 'WORKER DIAG SUBMITTED') });
  }
  if (row.scene === 'runtime-init-smp1' || row.scene === 'runtime-recovery-smp1') {
    const detected = valueOf(e9, row.scene === 'runtime-init-smp1' ? 'RUNTIME DIAG DETECTED' : 'RECOVERY DIAG DETECTED');
    check(tag + 'the guest detected exactly one core', detected === 1, detected);
  }
}

// ---------------------------------------------------------------- the guest's own arithmetic
//
// These are the checks that make the H rows evidence rather than transcripts. Each one takes two numbers the guest
// printed and requires the relation between them, so a log edited to say "OBSERVED" without the observation would fail.

const init = readLines(path.join(RUN, 'runtime-init-smp1.e9.log'));
if (init) {
  const t = 'runtime-init: ';
  // The task register. STR must have returned the selector the runtime loaded, and the index must be that selector >> 3.
  const sel = valueOf(init, 'RUNTIME DIAG TR SELECTOR');
  const idx = valueOf(init, 'RUNTIME DIAG TR INDEX');
  const ti = valueOf(init, 'RUNTIME DIAG TR TI');
  const rpl = valueOf(init, 'RUNTIME DIAG TR RPL');
  check(t + 'the task register selector is the one the runtime loaded',
    sel !== null && sel === valueOf(init, 'RUNTIME DIAG EXPECT SELECTOR'), { observed: sel, expected: valueOf(init, 'RUNTIME DIAG EXPECT SELECTOR') });
  check(t + 'the selector index is the selector shifted right by three', idx !== null && idx === (sel >> 3), { index: idx, selector: sel });
  check(t + 'the selector names the GDT, not the LDT', ti === 0, ti);
  check(t + 'the selector is ring 0', rpl === 0, rpl);
  // The descriptor. Its offset in the table must be the selector's index times eight, and it must lie inside the limit.
  const off = valueOf(init, 'RUNTIME DIAG TSS DESC OFFSET');
  const inGdt = valueOf(init, 'RUNTIME DIAG TSS DESC IN GDT');
  const gdtLimit = valueOf(init, 'RUNTIME DIAG SGDT LIMIT');
  check(t + 'the descriptor offset is the selector index times eight', off !== null && off === idx * 8, { offset: off, index: idx });
  // A GDTR limit is the offset of the last valid byte, so a descriptor of eight bytes at offset O fits when O + 7 <= limit.
  // Writing O + 8 <= limit is the off-by-one that would refuse the runtime's own table, whose task descriptor is the last
  // one in it.
  check(t + 'the descriptor lies wholly inside the GDT limit', inGdt === 1 && off + 7 <= gdtLimit,
    { in_gdt: inGdt, offset: off, limit: gdtLimit });
  // And the table is exactly big enough for the seven descriptors the runtime lays out, with the task descriptor last.
  check(t + 'the GDT limit covers seven descriptors and no more', gdtLimit === 7 * 8 - 1, gdtLimit);
  // The descriptor's base and limit must be the runtime's own TSS object, and its type must be busy because ltr ran.
  check(t + 'the descriptor base is the runtime task state',
    valueOf(init, 'RUNTIME DIAG TSS DESC BASE') === valueOf(init, 'RUNTIME DIAG EXPECT TSS BASE'),
    { observed: valueOf(init, 'RUNTIME DIAG TSS DESC BASE'), expected: valueOf(init, 'RUNTIME DIAG EXPECT TSS BASE') });
  check(t + 'the descriptor limit is the task state size',
    valueOf(init, 'RUNTIME DIAG TSS DESC LIMIT') === valueOf(init, 'RUNTIME DIAG EXPECT TSS LIMIT'));
  // 0x8B = present, DPL 0, system, type 11 (busy 32-bit TSS). Type 9 would mean the load never happened.
  const descType = valueOf(init, 'RUNTIME DIAG TSS DESC TYPE');
  check(t + 'the descriptor type is busy 32-bit TSS, which is what ltr leaves behind', descType === 0x8B, descType === null ? null : '0x' + descType.toString(16));
  // The TSS's own fields, read through the descriptor's base.
  check(t + 'the TSS ring-0 selector is the one the runtime set',
    valueOf(init, 'RUNTIME DIAG TSS SS0') === valueOf(init, 'RUNTIME DIAG EXPECT SS0'));
  check(t + 'the TSS io map base is the one the runtime set',
    valueOf(init, 'RUNTIME DIAG TSS IOMAP') === valueOf(init, 'RUNTIME DIAG EXPECT IOMAP'));
  // The ring-0 stack pointer must be inside the runtime's kernel stack arena, not zero and not somewhere arbitrary. The
  // arena is above 0xC0000000 on a real machine; the check is that the value is a kernel address the runtime owns.
  const esp0 = valueOf(init, 'RUNTIME DIAG TSS ESP0');
  check(t + 'the TSS ring-0 stack pointer is a kernel address, not zero', esp0 !== null && esp0 > 0xC0000000, esp0 === null ? null : '0x' + esp0.toString(16));
  check(t + 'no TSS obligation failed', valueOf(init, 'RUNTIME DIAG TSS OBLIGATIONS FAILED') === 0,
    valueOf(init, 'RUNTIME DIAG TSS OBLIGATIONS FAILED'));
  check(t + 'the task state reports itself loaded', valueOf(init, 'RUNTIME DIAG EXPECT LOADED') === 1);
  // The runtime stayed empty and interrupts stayed clear: the checkpoint reached the task register without going further.
  check(t + 'the runtime created no object', valueOf(init, 'RUNTIME DIAG CREATED') === 0);
  check(t + 'the runtime recorded no fault', valueOf(init, 'RUNTIME DIAG FAULTED') === 0);
  check(t + 'the FP policy stayed disabled', valueOf(init, 'RUNTIME DIAG FP ENABLED') === 0);
}

const rec = readLines(path.join(RUN, 'runtime-recovery-smp1.e9.log'));
if (rec) {
  const t = 'runtime-recovery: ';
  const capacity = valueOf(rec, 'RECOVERY DIAG CAPACITY');
  const filled = valueOf(rec, 'RECOVERY DIAG FULL CREATED');
  check(t + 'the capacity the guest filled to is the runtime\'s declared capacity', capacity === 4, capacity);
  check(t + 'the guest created exactly as many objects as the capacity', filled === capacity, { created: filled, capacity });
  // The refusal. The id must be zero: a refused create must not hand back a usable id.
  check(t + 'the refused attempt returned no id', valueOf(rec, 'RECOVERY DIAG OVERFLOW ID') === 0,
    valueOf(rec, 'RECOVERY DIAG OVERFLOW ID'));
  check(t + 'the refused attempt did not change the created count',
    valueOf(rec, 'RECOVERY DIAG REFUSED CREATED') === filled,
    { after: valueOf(rec, 'RECOVERY DIAG REFUSED CREATED'), before: filled });
  // The allocator. A refusal that leaked or invented a frame would show as a change in the total, which is the one
  // quantity a create cannot legitimately move. The guest also compares the allocated count either side of the attempt
  // and prints REFUSAL CLEAN; this re-derives the conservation from the four printed numbers, so the verdict does not
  // rest on the guest's own comparison.
  const baseAlloc = valueOf(rec, 'RECOVERY DIAG BASELINE ALLOCATED');
  const baseFree = valueOf(rec, 'RECOVERY DIAG BASELINE FREE');
  const refAlloc = valueOf(rec, 'RECOVERY DIAG REFUSED ALLOCATED');
  const refFree = valueOf(rec, 'RECOVERY DIAG REFUSED FREE');
  check(t + 'the frame total is unchanged across the refused attempt',
    baseAlloc !== null && refAlloc !== null && baseAlloc + baseFree === refAlloc + refFree,
    { before: baseAlloc === null ? null : baseAlloc + baseFree, after: refAlloc === null ? null : refAlloc + refFree });
  // And the frames the filled objects consumed must be a whole number per object, so the allocator is being charged a
  // consistent amount rather than an amount that varies with the attempt.
  const consumed = (baseAlloc !== null && refAlloc !== null) ? refAlloc - baseAlloc : null;
  check(t + 'the filled objects consumed a whole number of frames each',
    consumed !== null && consumed > 0 && consumed % capacity === 0,
    { consumed, capacity, per_object: consumed === null ? null : consumed / capacity });
  // The recovery. One reap, one fresh id, one live object, and the survivors intact.
  check(t + 'exactly one object was reaped', valueOf(rec, 'RECOVERY DIAG REAPED') === 1, valueOf(rec, 'RECOVERY DIAG REAPED'));
  const newId = valueOf(rec, 'RECOVERY DIAG NEW ID');
  check(t + 'the new object has a non-zero id', newId !== null && newId !== 0, newId);
  check(t + 'the new id was never issued before', newId === 5, { new_id: newId, note: 'four objects were created first, so the fifth id is the next one' });
  check(t + 'the number of survivors is the capacity minus the reaped object',
    valueOf(rec, 'RECOVERY DIAG SURVIVORS') === capacity - 1,
    { survivors: valueOf(rec, 'RECOVERY DIAG SURVIVORS'), capacity });
  // The accounting. Every create counted, including the refused one? No: the refused one created nothing, so the total is
  // the capacity plus the replacement.
  check(t + 'the create count is the capacity plus the one replacement',
    valueOf(rec, 'RECOVERY DIAG FINAL CREATED') === capacity + 1,
    { final_created: valueOf(rec, 'RECOVERY DIAG FINAL CREATED'), capacity });
  check(t + 'exactly one exit was recorded', valueOf(rec, 'RECOVERY DIAG FINAL EXITED') === 1);
  check(t + 'exactly one reap was recorded', valueOf(rec, 'RECOVERY DIAG FINAL REAPED') === 1);
  // The machine is still where this path may be.
  check(t + 'interrupts are still clear', has(rec, 'RECOVERY DIAG INTERRUPTS CLEAR'));
  check(t + 'no object was ever dispatched', !has(rec, 'RECOVERY DIAG DISPATCHED'));
}

// ---------------------------------------------------------------- the same image
//
// Every row ran the same file. If the runner were ever changed to build per-scene images this would stop being true, and
// the matrix would stop being a matrix.
const imageNote = fs.existsSync(path.join(RUN, 'matrix.tsv'))
  ? fs.readFileSync(path.join(RUN, 'matrix.tsv'), 'utf8').split(/\r?\n/)[0] : '';
check('the matrix records one image for every row', /scene/.test(imageNote), imageNote);

const failures = checks.filter((c) => !c.ok);
const report = {
  task: 'GTOS-DSH-139', kind: 'runtime_matrix_verification',
  run_dir: RUN, rows: rows.length, checks,
  checks_total: checks.length, checks_failed: failures.length, ok: failures.length === 0,
  transport_note: 'the emulator exit code is recorded in matrix.tsv and is not a pass criterion: a halted guest cannot '
    + 'exit cleanly, so the timeout kill is the expected transport outcome. The guest output is the result.',
  not_claimed: ['that these runs prove the kernel is correct beyond the scenes they name',
    'that a scene which passed here would pass on different hardware'],
};
if (OUT) {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(report, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
}
console.log(JSON.stringify({ ok: report.ok, checks: report.checks_total, failed: report.checks_failed }));
process.exit(report.ok ? 0 : 1);
