// The scene contract.
//
// What this file is
// -----------------
// A scene is not "some boot with a stop flag". It is a claim: *this* machine shape, running *this* stop, must produce
// *these* observations, and must not produce *those*. Before 147 the obligations were read off the log that happened to be
// in hand -- the judge asked "which numbers appeared?" and built its checklist from the answer, so a log that dropped a
// whole family simply had fewer obligations to satisfy, and a log that dropped the family entirely could still pass on the
// marker at the end. The obligations have to exist before the log does.
//
// So this module declares, for every permitted scene, the full set of observations that scene owes, and nothing here is
// derived from a captured channel. The declarations come from two places, both of which are inside the packet:
//
//   * `src/kernel.cpp` -- the code that prints each line, which fixes the label text, the family, and the order the lines
//     are printed in;
//   * the runtime's own public constants (`include/process/native_runtime.h`,
//     `include/hardwarecommunication/cpu_work_queue.h`) -- which fix the numbers a relation is allowed to assume.
//
// Where a value is a pure constant of the program (a selector, a page count) the obligation names the value. Where a value
// depends on the machine or on an earlier observation the obligation is a *relation*, checked by a function that recomputes
// it from the other numbers in the same channel. Nothing is checked against a captured log, and no obligation is optional:
// a missing label fails the obligation that names it. That is the difference between "the family that appeared was
// correct" and "the family the scene owes was produced".
//
// The two channel sides
// ---------------------
// Every scene is run with both E9 and serial enabled, and both must carry the family. A run that produced its evidence on
// one side only is not the same run: the two sides are written by the same `printf` and a divergence means the log was
// assembled rather than emitted. `judgeScene` therefore takes both texts and requires both.

export const CONTRACT_VERSION = '147.1';

// ---------------------------------------------------------------- the constants a relation may assume
//
// These are read out of the two headers named above. They are stated here so that a relation can recompute a value the
// kernel printed instead of accepting it, and so that a change to either header shows up as a failed relation rather than
// as a quietly different scene.
export const CONSTANTS = {
  // include/process/native_runtime.h
  kernel_stack_arena: 0xd0000000,
  kernel_stack_pages: 4,
  maximum_processes: 4,
  user_stack_bottom: 0xbfffd000,
  user_stack_top: 0xbffff000,
  data_address: 0x40002000,
  code_address: 0x40000000,
  // include/hardwarecommunication/cpu_work_queue.h
  work_queue_capacity: 16,
  // src/kernel.cpp, the worker submit loop
  worker_passes: 2,
  worker_jobs_per_worker: 16, // the ring accepts Capacity of the Capacity+2 offered
  // src/kernel.cpp, the task-state descriptor the GDT constructor builds
  tss_selector: 0x30,
  tss_ring0_selector: 0x18,
  tss_iomap_base: 0x68,
  tss_descriptor_type: 0x8b,
};

// The per-slot stride the runtime maps: one guard page, the owned pages, one guard page.
export const STACK_STRIDE = (CONSTANTS.kernel_stack_pages + 1) * 4096;
// Slot `i`'s stack top, which is what `Activate` loads into the TSS.
export const stackTop = (i) => CONSTANTS.kernel_stack_arena + (i + 1) * STACK_STRIDE;
// The lowest frame of slot `i`'s own stack (the guard page sits below it).
export const stackLow = (i) => stackTop(i) - CONSTANTS.kernel_stack_pages * 4096;

// ---------------------------------------------------------------- the fixed integer job
//
// The worker scene submits a fixed request per job and the AP accumulates. Both halves of that are program constants, so a
// judge can compute the answer without asking the guest what it expected -- which is the whole point, because the guest
// prints `EXPECTED` too and comparing the guest's value with the guest's expectation is comparing a number with itself.
export const workerSeed = (pass, worker, job) => 0x1000 * (pass + 1) + worker * 0x100 + job;
export const workerCount = (job) => 8 + (job % 4);
export const workerValue = (pass, worker, job) => {
  let value = workerSeed(pass, worker, job);
  for (let i = 1; i <= workerCount(job); i += 1) value += i;
  return value >>> 0;
};

// ---------------------------------------------------------------- obligation constructors
const num = (label, value) => ({ kind: 'number', label, value });
const numFrom = (label, fn, why) => ({ kind: 'number', label, derived: fn, why });
const line = (text) => ({ kind: 'line', text });
const family = (prefix, min) => ({ kind: 'family', prefix, min });
const rel = (id, fn, why) => ({ kind: 'relation', id, fn, why });

// The boot prefix every scene owes. `depth` says how far the machine got before the stop took it.
const bootPrefix = (depth) => {
  const out = [
    line('BOOT B01 HANDOFF'),
    line('GTOS 0.3 PROTECTED DESKTOP BOOT'),
    line('BOOT B02 MEMORY'),
    line('BOOT SERIAL COM1 115200 8N1'),
  ];
  if (depth >= 2) {
    out.push(num('MEMORY FREE FRAMES', null, 'present'));
    out.push(line('PHYSICAL SELFTEST PASS'));
    out.push(line('HEAP SELFTEST PASS'));
  }
  if (depth >= 3) {
    out.push(line('BOOT B03 GDT IDT SCHEDULER'));
    out.push(family('CPU VENDOR', 1));
    out.push(num('CPU DETECTED', null, 'present'));
    out.push(num('CPU ONLINE', 1));
    out.push(line('SCHEDULER SELFTEST PASS'));
  }
  return out;
};

// The refusal scenes: two or three cores are detected, and the stop refuses before it prepares anything.
const refusal = (name, vcpus, stop, marker, diagPrefix, extraForbidden) => ({
  name, vcpus, stop, flags: ['serial'], boot: 3,
  terminal: marker,
  obligations: [
    ...bootPrefix(3),
    num('CPU DETECTED', vcpus),
    num(`${diagPrefix} DETECTED`, vcpus),
    line(`${diagPrefix} REFUSED MULTI CPU`),
    rel('refusal-before-work', (c) => {
      const refuse = c.lineIndex(`${diagPrefix} REFUSED MULTI CPU`);
      const prepared = c.lineIndex(`${diagPrefix} STACKS PREPARED`);
      const activated = c.lineIndex(`${diagPrefix} ACTIVATED`);
      return { ok: refuse >= 0 && prepared < 0 && activated < 0,
        why: `the refusal must be reached before any stack is prepared (refuse=${refuse}, prepared=${prepared}, activated=${activated})` };
    }, 'a refusal that prepared stacks is not the refusal this scene declares'),
  ],
  forbidden: [
    `${diagPrefix} STACKS PREPARED`, `${diagPrefix} ACTIVATED`, 'PANIC', 'BOOTDIAG REJECT',
    'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ...(extraForbidden || []),
  ],
});

const SCENE_LIST = [
  // ================================================================ runtime-init
  {
    name: 'runtime-init-smp1', vcpus: 1, stop: 'runtime-init', flags: ['serial'], boot: 3,
    terminal: 'BOOTDIAG RUNTIME INITIALIZED',
    obligations: [
      ...bootPrefix(3),
      num('CPU DETECTED', 1),
      num('RUNTIME DIAG DETECTED', 1),
      line('RUNTIME DIAG STACKS PREPARED'),
      num('RUNTIME DIAG CR0', null, 'present'),
      num('RUNTIME DIAG CR3', null, 'present'),
      num('RUNTIME DIAG DIRECTORY', null, 'present'),
      line('RUNTIME DIAG SEALED'),
      line('RUNTIME DIAG ACTIVATED'),
      // the task register, read from the machine
      num('RUNTIME DIAG TR READ', 1),
      num('RUNTIME DIAG TR SELECTOR', CONSTANTS.tss_selector),
      num('RUNTIME DIAG TR TI', 0),
      num('RUNTIME DIAG TR RPL', 0),
      numFrom('RUNTIME DIAG TR INDEX', () => CONSTANTS.tss_selector >> 3, 'the selector index is the selector shifted by three'),
      num('RUNTIME DIAG TSS DESC IN GDT', 1),
      num('RUNTIME DIAG TSS DESC TYPE', CONSTANTS.tss_descriptor_type),
      num('RUNTIME DIAG TSS SS0', CONSTANTS.tss_ring0_selector),
      num('RUNTIME DIAG TSS IOMAP', CONSTANTS.tss_iomap_base),
      num('RUNTIME DIAG EXPECT SELECTOR', CONSTANTS.tss_selector),
      num('RUNTIME DIAG EXPECT SS0', CONSTANTS.tss_ring0_selector),
      num('RUNTIME DIAG EXPECT IOMAP', CONSTANTS.tss_iomap_base),
      num('RUNTIME DIAG EXPECT LOADED', 1),
      num('RUNTIME DIAG TSS OBLIGATIONS FAILED', 0),
      line('RUNTIME DIAG TSS OBSERVED'),
      // the stack geometry the arena was built with, read from the runtime's own constants
      num('RUNTIME DIAG STACK ARENA', CONSTANTS.kernel_stack_arena),
      num('RUNTIME DIAG STACK PAGES', CONSTANTS.kernel_stack_pages),
      num('RUNTIME DIAG STACK STRIDE', STACK_STRIDE),
      num('RUNTIME DIAG STACK SLOTS', CONSTANTS.maximum_processes),
      num('RUNTIME DIAG ESP0 OWNER', 0),
      numFrom('RUNTIME DIAG ESP0 OWNER TOP', () => stackTop(0), 'the activated slot is slot 0, so its top is arena + stride'),
      numFrom('RUNTIME DIAG ESP0 OWNER LOW', () => stackLow(0), 'the owned span starts one page above the guard page'),
      num('RUNTIME DIAG TSS ESP0', null, 'present'),
      line('RUNTIME DIAG ESP0 IN OWNER SPAN'),
      line('RUNTIME DIAG ESP0 OWNER IS ACTIVATED SLOT'),
      num('RUNTIME DIAG CREATED', 0),
      num('RUNTIME DIAG EXITED', 0),
      num('RUNTIME DIAG FAULTED', 0),
      num('RUNTIME DIAG REAPED', 0),
      num('RUNTIME DIAG FP ENABLED', 0),
      line('RUNTIME DIAG EMPTY'),
      line('RUNTIME DIAG INTERRUPTS CLEAR'),
      // ---- relations the numbers above are not enough for
      rel('esp0-is-the-activated-stack-top', (c) => {
        const esp0 = c.num('RUNTIME DIAG TSS ESP0');
        const owner = c.num('RUNTIME DIAG ESP0 OWNER');
        const top = c.num('RUNTIME DIAG ESP0 OWNER TOP');
        const low = c.num('RUNTIME DIAG ESP0 OWNER LOW');
        if (esp0 === null || owner === null || top === null || low === null) return { ok: false, why: 'the witness is incomplete' };
        const derivedOwner = [0, 1, 2, 3].find((i) => stackTop(i) === esp0);
        return {
          ok: derivedOwner === 0 && owner === 0 && top === stackTop(0) && low === stackLow(0) && esp0 === top,
          why: `esp0=0x${esp0.toString(16)} must be the top of slot 0 (0x${stackTop(0).toString(16)}), owner=${owner}, `
            + `top=0x${top.toString(16)}, low=0x${low.toString(16)}, derived owner=${derivedOwner}`,
        };
      }, 'a nonzero aligned ESP0 is not a stack; it must be the top of the stack the activated slot owns'),
      rel('esp0-span-is-the-declared-page-count', (c) => {
        const top = c.num('RUNTIME DIAG ESP0 OWNER TOP');
        const low = c.num('RUNTIME DIAG ESP0 OWNER LOW');
        const pages = c.num('RUNTIME DIAG STACK PAGES');
        if (top === null || low === null || pages === null) return { ok: false, why: 'the witness is incomplete' };
        return { ok: top - low === pages * 4096 && pages === CONSTANTS.kernel_stack_pages,
          why: `top-low=0x${(top - low).toString(16)} must be pages*4096=0x${(pages * 4096).toString(16)}` };
      }, 'the owned span must be the frame count the runtime declares, not an arbitrary range'),
      rel('esp0-is-inside-the-arena', (c) => {
        const esp0 = c.num('RUNTIME DIAG TSS ESP0');
        const arena = c.num('RUNTIME DIAG STACK ARENA');
        const slots = c.num('RUNTIME DIAG STACK SLOTS');
        const stride = c.num('RUNTIME DIAG STACK STRIDE');
        if (esp0 === null || arena === null || slots === null || stride === null) return { ok: false, why: 'the witness is incomplete' };
        return { ok: esp0 > arena && esp0 <= arena + slots * stride,
          why: `esp0=0x${esp0.toString(16)} must lie inside [0x${arena.toString(16)}, 0x${(arena + slots * stride).toString(16)}]` };
      }, 'a stack pointer outside the arena the runtime prepared is not a stack the runtime owns'),
      rel('cr3-names-the-directory', (c) => {
        const cr3 = c.num('RUNTIME DIAG CR3');
        const dir = c.num('RUNTIME DIAG DIRECTORY');
        return { ok: cr3 !== null && dir !== null && cr3 === dir, why: `cr3=0x${cr3} directory=0x${dir}` };
      }, 'the loaded page directory and the directory the runtime reports must be the same object'),
      rel('cr0-has-paging-and-protection', (c) => {
        const cr0 = c.num('RUNTIME DIAG CR0');
        return { ok: cr0 !== null && ((cr0 & 0x80010000) >>> 0) === 0x80010000, why: `cr0=0x${cr0 === null ? 'null' : cr0.toString(16)}` };
      }, 'the runtime cannot be initialised with paging or write protection off'),
      rel('descriptor-matches-the-runtime-intent', (c) => {
        const pairs = [
          ['RUNTIME DIAG TSS DESC BASE', 'RUNTIME DIAG EXPECT TSS BASE'],
          ['RUNTIME DIAG TSS DESC LIMIT', 'RUNTIME DIAG EXPECT TSS LIMIT'],
          ['RUNTIME DIAG TSS SS0', 'RUNTIME DIAG EXPECT SS0'],
          ['RUNTIME DIAG TSS IOMAP', 'RUNTIME DIAG EXPECT IOMAP'],
          ['RUNTIME DIAG TR SELECTOR', 'RUNTIME DIAG EXPECT SELECTOR'],
        ];
        const bad = pairs.filter(([a, b]) => c.num(a) === null || c.num(b) === null || c.num(a) !== c.num(b));
        return { ok: bad.length === 0, why: bad.map(([a, b]) => `${a}=${c.num(a)} != ${b}=${c.num(b)}`).join('; ') || 'all five agree' };
      }, 'the loaded descriptor must be the descriptor the runtime meant to load'),
      rel('descriptor-is-inside-the-gdt', (c) => {
        const base = c.num('RUNTIME DIAG SGDT BASE');
        const limit = c.num('RUNTIME DIAG SGDT LIMIT');
        const offset = c.num('RUNTIME DIAG TSS DESC OFFSET');
        return { ok: base !== null && limit !== null && offset !== null && offset + 8 <= limit + 1,
          why: `offset=0x${offset === null ? 'null' : offset.toString(16)} limit=0x${limit === null ? 'null' : limit.toString(16)}` };
      }, 'a task descriptor outside the GDT limit is not loaded from the table the register names'),
    ],
    forbidden: [
      'RUNTIME DIAG REFUSED MULTI CPU', 'BOOTDIAG RUNTIME CHECKPOINT NOT REACHED', 'PANIC', 'BOOTDIAG REJECT',
      'RUNTIME DIAG NOT EMPTY', 'RUNTIME DIAG TSS MISMATCH', 'RUNTIME DIAG ESP0 OUTSIDE OWNER SPAN',
      'RUNTIME DIAG ESP0 OWNER IS NOT ACTIVATED SLOT', 'RUNTIME DIAG INTERRUPTS ENABLED',
      'OBJECT DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG', 'WORKER DIAG', 'PAGING DIAG', 'AP DIAG',
      'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ],
  },
  refusal('runtime-init-smp2', 2, 'runtime-init', 'BOOTDIAG RUNTIME REFUSED MULTI CPU', 'RUNTIME DIAG', ['BOOTDIAG RUNTIME INITIALIZED']),
  refusal('runtime-init-smp3', 3, 'runtime-init', 'BOOTDIAG RUNTIME REFUSED MULTI CPU', 'RUNTIME DIAG', ['BOOTDIAG RUNTIME INITIALIZED']),

  // ================================================================ runtime-objects
  {
    name: 'runtime-objects-smp1', vcpus: 1, stop: 'runtime-objects', flags: ['serial'], boot: 3,
    terminal: 'BOOTDIAG RUNTIME OBJECTS RECLAIMED',
    obligations: [
      ...bootPrefix(3),
      num('CPU DETECTED', 1),
      num('OBJECT DIAG DETECTED', 1),
      line('OBJECT DIAG STACKS PREPARED'),
      line('OBJECT DIAG ACTIVATED'),
      num('OBJECT DIAG CR3', null, 'present'),
      num('OBJECT DIAG BASELINE ALLOCATED', null, 'present'),
      num('OBJECT DIAG BASELINE FREE', null, 'present'),
      line('OBJECT DIAG CREATED A'),
      line('OBJECT DIAG CREATED B'),
      num('OBJECT DIAG ID A', null, 'present'),
      num('OBJECT DIAG ID B', null, 'present'),
      line('OBJECT DIAG DATA A MATCHES'),
      line('OBJECT DIAG DATA B MATCHES'),
      line('OBJECT DIAG DATA DIFFERS'),
      num('OBJECT DIAG DATA A DIGEST', null, 'present'),
      num('OBJECT DIAG DATA B DIGEST', null, 'present'),
      num('OBJECT DIAG DIR A', null, 'present'),
      num('OBJECT DIAG DIR B', null, 'present'),
      line('OBJECT DIAG ADDRESS SPACES DIFFER'),
      num('OBJECT DIAG CREATED COUNT', 2),
      num('OBJECT DIAG EXITED COUNT', 0),
      num('OBJECT DIAG REAPED COUNT', 0),
      line('OBJECT DIAG INTERRUPTS CLEAR'),
      line('OBJECT DIAG NOT DISPATCHED'),
      line('OBJECT DIAG EXIT REQUESTED'),
      num('OBJECT DIAG REAPED', 2),
      line('OBJECT DIAG RECLAIMED'),
      num('OBJECT DIAG OLD A REAPED', 1),
      num('OBJECT DIAG OLD A LIVE', 0),
      num('OBJECT DIAG OLD B REAPED', 1),
      line('OBJECT DIAG OLD IDS TOMBSTONED'),
      line('OBJECT DIAG OLD ID UNREADABLE'),
      line('OBJECT DIAG OLD ID UNEXITABLE'),
      num('OBJECT DIAG AFTER ALLOCATED', null, 'present'),
      num('OBJECT DIAG AFTER FREE', null, 'present'),
      line('OBJECT DIAG RESOURCES RESTORED'),
      num('OBJECT DIAG FINAL CREATED', 2),
      num('OBJECT DIAG FINAL EXITED', 2),
      num('OBJECT DIAG FINAL REAPED', 2),
      rel('two-objects-two-identities', (c) => {
        const idA = c.num('OBJECT DIAG ID A');
        const idB = c.num('OBJECT DIAG ID B');
        const dirA = c.num('OBJECT DIAG DIR A');
        const dirB = c.num('OBJECT DIAG DIR B');
        return { ok: idA !== null && idB !== null && idA !== idB && dirA !== null && dirB !== null && dirA !== 0 && dirB !== 0 && dirA !== dirB,
          why: `idA=${idA} idB=${idB} dirA=0x${dirA === null ? 'null' : dirA.toString(16)} dirB=0x${dirB === null ? 'null' : dirB.toString(16)}` };
      }, 'two objects that share an id or an address space are one object'),
      rel('two-objects-two-data-blocks', (c) => {
        const a = c.num('OBJECT DIAG DATA A DIGEST');
        const b = c.num('OBJECT DIAG DATA B DIGEST');
        return { ok: a !== null && b !== null && a !== 0 && b !== 0 && a !== b,
          why: `digestA=${a} digestB=${b}` };
      }, 'private memory means two objects holding different bytes, shown as different digests'),
      rel('reap-returns-the-frame-set', (c) => {
        const baseAlloc = c.num('OBJECT DIAG BASELINE ALLOCATED');
        const baseFree = c.num('OBJECT DIAG BASELINE FREE');
        const afterAlloc = c.num('OBJECT DIAG AFTER ALLOCATED');
        const afterFree = c.num('OBJECT DIAG AFTER FREE');
        return { ok: baseAlloc !== null && baseFree !== null && afterAlloc === baseAlloc && afterFree === baseFree,
          why: `baseline(${baseAlloc},${baseFree}) after(${afterAlloc},${afterFree})` };
      }, 'reclamation that does not return the frame set it took is a leak with a marker on it'),
    ],
    forbidden: [
      'OBJECT DIAG REFUSED MULTI CPU', 'OBJECT DIAG CREATE A FAILED', 'OBJECT DIAG CREATE B FAILED',
      'OBJECT DIAG DATA A WRONG', 'OBJECT DIAG DATA B WRONG', 'OBJECT DIAG DATA SHARED',
      'OBJECT DIAG ADDRESS SPACES SHARED', 'BOOTDIAG RUNTIME OBJECTS CHECKPOINT NOT REACHED',
      'PANIC', 'BOOTDIAG REJECT', 'RUNTIME DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG', 'WORKER DIAG', 'AP DIAG',
      'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ],
  },
  refusal('runtime-objects-smp2', 2, 'runtime-objects', 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU', 'OBJECT DIAG', ['OBJECT DIAG CREATED A', 'OBJECT DIAG REAPED']),
  refusal('runtime-objects-smp3', 3, 'runtime-objects', 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU', 'OBJECT DIAG', ['OBJECT DIAG CREATED A', 'OBJECT DIAG REAPED']),

  // ================================================================ runtime-recovery (the capacity refusal)
  {
    name: 'runtime-recovery-smp1', vcpus: 1, stop: 'runtime-recovery', flags: ['serial'], boot: 3,
    terminal: 'BOOTDIAG RUNTIME RECOVERED CAPACITY',
    obligations: [
      ...bootPrefix(3),
      num('CPU DETECTED', 1),
      num('RECOVERY DIAG DETECTED', 1),
      line('RECOVERY DIAG STACKS PREPARED'),
      line('RECOVERY DIAG ACTIVATED'),
      num('RECOVERY DIAG CR3', null, 'present'),
      num('RECOVERY DIAG BASELINE ALLOCATED', null, 'present'),
      num('RECOVERY DIAG BASELINE FREE', null, 'present'),
      num('RECOVERY DIAG BASELINE DIGEST', null, 'present'),
      num('RECOVERY DIAG CAPACITY', CONSTANTS.maximum_processes),
      line('RECOVERY DIAG FILLED TO CAPACITY'),
      num('RECOVERY DIAG FULL CREATED', CONSTANTS.maximum_processes),
      num('RECOVERY DIAG FULL REAPED', 0),
      line('RECOVERY DIAG OVERFLOW REFUSED'),
      num('RECOVERY DIAG OVERFLOW ID', 0),
      num('RECOVERY DIAG REFUSED CREATED', CONSTANTS.maximum_processes),
      line('RECOVERY DIAG REFUSAL CLEAN'),
      line('RECOVERY DIAG EXIT REQUESTED'),
      num('RECOVERY DIAG REAPED', 1),
      line('RECOVERY DIAG VICTIM TOMBSTONED'),
      line('RECOVERY DIAG RECREATED'),
      numFrom('RECOVERY DIAG NEW ID', (c) => c.num('RECOVERY DIAG CAPACITY') + 1, 'the ids issued so far are 1..capacity, so the next is capacity+1'),
      line('RECOVERY DIAG NEW ID FRESH'),
      line('RECOVERY DIAG NEW DATA MATCHES'),
      line('RECOVERY DIAG NEW LIVE'),
      numFrom('RECOVERY DIAG SURVIVORS', (c) => c.num('RECOVERY DIAG CAPACITY') - 1, 'one of the capacity objects was retired'),
      line('RECOVERY DIAG SURVIVORS LIVE'),
      line('RECOVERY DIAG SURVIVORS DISTINCT'),
      numFrom('RECOVERY DIAG FINAL CREATED', (c) => c.num('RECOVERY DIAG CAPACITY') + 1, 'capacity creates plus the one recovery create'),
      num('RECOVERY DIAG FINAL EXITED', 1),
      num('RECOVERY DIAG FINAL REAPED', 1),
      line('RECOVERY DIAG ACCOUNTED'),
      line('RECOVERY DIAG INTERRUPTS CLEAR'),
      num('RECOVERY DIAG RETIRED', CONSTANTS.maximum_processes),
      line('RECOVERY DIAG RETIRED ALL'),
      line('RECOVERY DIAG BASELINE RESTORED'),
      line('RECOVERY DIAG TABLE EMPTY'),
      // ---- the live neighbours, by identity and by data
      rel('survivors-keep-their-data', (c) => {
        const filled = c.records('RECOVERY DIAG FILLED');
        const survivors = c.records('RECOVERY DIAG SURVIVOR');
        if (filled.length !== CONSTANTS.maximum_processes) return { ok: false, why: `expected ${CONSTANTS.maximum_processes} fill records, got ${filled.length}` };
        if (survivors.length !== CONSTANTS.maximum_processes - 1) return { ok: false, why: `expected ${CONSTANTS.maximum_processes - 1} survivor records, got ${survivors.length}` };
        const byId = new Map(filled.map((r) => [r.ID, r]));
        const bad = [];
        for (const s of survivors) {
          const before = byId.get(s.ID);
          if (!before) { bad.push(`survivor id ${s.ID} was never filled`); continue; }
          if (before.DATA !== s.DATA) bad.push(`id ${s.ID} data moved 0x${before.DATA.toString(16)} -> 0x${s.DATA.toString(16)}`);
          if (before.DIR !== s.DIR) bad.push(`id ${s.ID} directory moved 0x${before.DIR.toString(16)} -> 0x${s.DIR.toString(16)}`);
          if (before.SLOT !== s.SLOT) bad.push(`id ${s.ID} slot moved ${before.SLOT} -> ${s.SLOT}`);
        }
        return { ok: bad.length === 0, why: bad.join('; ') || `${survivors.length} survivors unchanged by id, slot, directory and data` };
      }, 'a neighbour that survived but changed its bytes or its address space was disturbed, not preserved'),
      rel('survivors-hold-distinct-blocks-and-spaces', (c) => {
        const survivors = c.records('RECOVERY DIAG SURVIVOR');
        const digests = survivors.map((r) => r.DATA);
        const dirs = survivors.map((r) => r.DIR);
        const ids = survivors.map((r) => r.ID);
        const uniq = (a) => new Set(a).size === a.length;
        return { ok: uniq(digests) && uniq(dirs) && uniq(ids) && digests.every((d) => d !== 0),
          why: `ids=${ids.join(',')} digests=${digests.map((d) => '0x' + d.toString(16)).join(',')} dirs=${dirs.map((d) => '0x' + d.toString(16)).join(',')}` };
      }, 'four live objects with four data blocks in four address spaces is the privacy claim, and it must hold after the recovery'),
      rel('baseline-is-restored-exactly', (c) => {
        const pairs = [
          ['RECOVERY DIAG BASELINE ALLOCATED', 'RECOVERY DIAG RESTORED ALLOCATED'],
          ['RECOVERY DIAG BASELINE FREE', 'RECOVERY DIAG RESTORED FREE'],
          ['RECOVERY DIAG BASELINE DIGEST', 'RECOVERY DIAG RESTORED DIGEST'],
        ];
        const bad = pairs.filter(([a, b]) => c.num(a) === null || c.num(b) === null || c.num(a) !== c.num(b));
        return { ok: bad.length === 0, why: bad.map(([a, b]) => `${a}=${c.num(a)} != ${b}=${c.num(b)}`).join('; ') || 'allocated, free and digest all returned' };
      }, 'the frame set must come back to the state the machine was in, compared by set and not by total'),
      rel('the-refusal-kept-the-frame-set', (c) => {
        const baseAlloc = c.num('RECOVERY DIAG BASELINE ALLOCATED');
        const refAlloc = c.num('RECOVERY DIAG REFUSED ALLOCATED');
        const baseFree = c.num('RECOVERY DIAG BASELINE FREE');
        const refFree = c.num('RECOVERY DIAG REFUSED FREE');
        return { ok: baseAlloc !== null && refAlloc !== null && refAlloc > baseAlloc && baseFree !== null && refFree !== null && refFree < baseFree,
          why: `baseline(${baseAlloc},${baseFree}) after refusal(${refAlloc},${refFree}): the four live objects must still hold their frames` };
      }, 'a refusal that released the frames of the objects already live would be a scene that forgot its own state'),
    ],
    forbidden: [
      'RECOVERY DIAG REFUSED MULTI CPU', 'RECOVERY DIAG OVERFLOW ACCEPTED', 'RECOVERY DIAG REFUSAL DIRTIED',
      'RECOVERY DIAG VICTIM LIVE', 'RECOVERY DIAG RECREATE FAILED', 'RECOVERY DIAG NEW ID REUSED',
      'RECOVERY DIAG NEW DATA WRONG', 'RECOVERY DIAG NEW NOT LIVE', 'RECOVERY DIAG SURVIVORS LOST',
      'RECOVERY DIAG SURVIVORS SHARED', 'RECOVERY DIAG SURVIVOR UNREADABLE', 'RECOVERY DIAG ACCOUNTING WRONG',
      'RECOVERY DIAG INTERRUPTS ENABLED', 'RECOVERY DIAG BASELINE DRIFTED', 'RECOVERY DIAG FILL FAILED',
      'BOOTDIAG RUNTIME RECOVERY CHECKPOINT NOT REACHED', 'PANIC', 'BOOTDIAG REJECT',
      'FRAMEREC DIAG', 'OBJECT DIAG', 'RUNTIME DIAG', 'WORKER DIAG', 'AP DIAG',
      'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ],
  },
  refusal('runtime-recovery-smp2', 2, 'runtime-recovery', 'BOOTDIAG RUNTIME RECOVERY REFUSED MULTI CPU', 'RECOVERY DIAG', ['RECOVERY DIAG FILLED TO CAPACITY']),
  refusal('runtime-recovery-smp3', 3, 'runtime-recovery', 'BOOTDIAG RUNTIME RECOVERY REFUSED MULTI CPU', 'RECOVERY DIAG', ['RECOVERY DIAG FILLED TO CAPACITY']),

  // ================================================================ runtime-frame-recovery (the allocator refusal)
  {
    name: 'runtime-frame-recovery-smp1', vcpus: 1, stop: 'runtime-frame-recovery', flags: ['serial'], boot: 3,
    terminal: 'BOOTDIAG RUNTIME RECOVERED FRAMES',
    obligations: [
      ...bootPrefix(3),
      num('CPU DETECTED', 1),
      num('FRAMEREC DIAG DETECTED', 1),
      line('FRAMEREC DIAG STACKS PREPARED'),
      line('FRAMEREC DIAG ACTIVATED'),
      num('FRAMEREC DIAG CR3', null, 'present'),
      num('FRAMEREC DIAG BASELINE ALLOCATED', null, 'present'),
      num('FRAMEREC DIAG BASELINE FREE', null, 'present'),
      num('FRAMEREC DIAG BASELINE DIGEST', null, 'present'),
      line('FRAMEREC DIAG WALK AGREES'),
      line('FRAMEREC DIAG SIBLING CREATED'),
      num('FRAMEREC DIAG SIBLING ID', null, 'present'),
      line('FRAMEREC DIAG SIBLING LIVE'),
      num('FRAMEREC DIAG SIBLING SLOT', null, 'present'),
      num('FRAMEREC DIAG SIBLING DATA AT CREATE', null, 'present'),
      line('FRAMEREC DIAG VICTIM CREATED'),
      num('FRAMEREC DIAG VICTIM ID', null, 'present'),
      num('FRAMEREC DIAG VICTIM SLOT', null, 'present'),
      num('FRAMEREC DIAG VICTIM DIRECTORY', null, 'present'),
      line('FRAMEREC DIAG VICTIM OCCUPIES SLOT'),
      line('FRAMEREC DIAG VICTIM DATA MATCHES'),
      num('FRAMEREC DIAG OBJECT COST', null, 'present'),
      num('FRAMEREC DIAG CLAIMED FRAMES', null, 'present'),
      num('FRAMEREC DIAG CLAIM BLOCKS', null, 'present'),
      num('FRAMEREC DIAG PRESSURE FREE', null, 'present'),
      line('FRAMEREC DIAG PRESSURE HELD'),
      num('FRAMEREC DIAG BEFORE ALLOCATED', null, 'present'),
      num('FRAMEREC DIAG BEFORE FREE', null, 'present'),
      num('FRAMEREC DIAG BEFORE DIGEST', null, 'present'),
      num('FRAMEREC DIAG SPARE SLOTS', null, 'present'),
      line('FRAMEREC DIAG SPARE SLOT'),
      line('FRAMEREC DIAG STARVED REFUSED'),
      num('FRAMEREC DIAG STARVED ID', 0),
      num('FRAMEREC DIAG AFTER ALLOCATED', null, 'present'),
      num('FRAMEREC DIAG AFTER FREE', null, 'present'),
      num('FRAMEREC DIAG AFTER DIGEST', null, 'present'),
      num('FRAMEREC DIAG AFTER FAILURES', null, 'present'),
      line('FRAMEREC DIAG REFUSAL IDENTLESS'),
      line('FRAMEREC DIAG ALLOCATOR REFUSED'),
      line('FRAMEREC DIAG FRAME SET UNMOVED'),
      line('FRAMEREC DIAG NO OBJECT MADE'),
      line('FRAMEREC DIAG SIBLING INTACT'),
      num('FRAMEREC DIAG RETURNED FRAMES', null, 'present'),
      line('FRAMEREC DIAG PRESSURE REVERSED'),
      line('FRAMEREC DIAG RECOVERED'),
      num('FRAMEREC DIAG RECOVERED ID', null, 'present'),
      line('FRAMEREC DIAG RECOVERED ID FRESH'),
      line('FRAMEREC DIAG RECOVERED DATA MATCHES'),
      line('FRAMEREC DIAG RECOVERED LIVE'),
      num('FRAMEREC DIAG SIBLING DIRECTORY', null, 'present'),
      num('FRAMEREC DIAG RECOVERED DIRECTORY', null, 'present'),
      num('FRAMEREC DIAG SIBLING DATA', null, 'present'),
      num('FRAMEREC DIAG RECOVERED DATA', null, 'present'),
      line('FRAMEREC DIAG NEIGHBOUR DATA DISTINCT'),
      line('FRAMEREC DIAG VICTIM EXIT REQUESTED'),
      num('FRAMEREC DIAG VICTIM REAPED', 1),
      line('FRAMEREC DIAG VICTIM TOMBSTONED'),
      line('FRAMEREC DIAG VICTIM SLOT VACATED'),
      line('FRAMEREC DIAG OLD ID READ REFUSED'),
      line('FRAMEREC DIAG OLD ID EXIT REFUSED'),
      line('FRAMEREC DIAG OLD ID MEMORY REFUSED'),
      line('FRAMEREC DIAG UNKNOWN ID REFUSED'),
      line('FRAMEREC DIAG NEW VICTIM CREATED'),
      num('FRAMEREC DIAG NEW VICTIM ID', null, 'present'),
      num('FRAMEREC DIAG NEW VICTIM SLOT', null, 'present'),
      num('FRAMEREC DIAG NEW VICTIM DIRECTORY', null, 'present'),
      line('FRAMEREC DIAG SLOT REUSED'),
      line('FRAMEREC DIAG NEW GENERATION'),
      line('FRAMEREC DIAG NEW DIRECTORY'),
      line('FRAMEREC DIAG NEW VICTIM LIVE'),
      line('FRAMEREC DIAG NEW VICTIM DATA MATCHES'),
      line('FRAMEREC DIAG SIBLING HELD'),
      line('FRAMEREC DIAG RECOVERED HELD'),
      // `FINAL REAPED` is printed twice in this scene and the two numbers are different on purpose: the first is what the
      // reap call returned (the three objects that were live), the second is the runtime's lifetime total (the four objects
      // that ever existed, the fourth being the one the earlier reap took). Both are checked, by the relation below.
      num('FRAMEREC DIAG FINAL CREATED', null, 'present'),
      num('FRAMEREC DIAG FINAL EXITED', null, 'present'),
      num('FRAMEREC DIAG FINAL ALLOCATED', null, 'present'),
      num('FRAMEREC DIAG FINAL FREE', null, 'present'),
      num('FRAMEREC DIAG FINAL DIGEST', null, 'present'),
      num('FRAMEREC DIAG OCCUPIED SLOTS', 0),
      line('FRAMEREC DIAG TABLE EMPTY'),
      line('FRAMEREC DIAG BASELINE RESTORED'),
      line('FRAMEREC DIAG ACCOUNTED'),
      line('FRAMEREC DIAG INTERRUPTS CLEAR'),
      rel('the-final-reap-closes-the-accounting', (c) => {
        const reaps = c.nums('FRAMEREC DIAG FINAL REAPED');
        const created = c.num('FRAMEREC DIAG FINAL CREATED');
        const exited = c.num('FRAMEREC DIAG FINAL EXITED');
        if (reaps.length !== 2) return { ok: false, why: `FINAL REAPED must be printed twice (the call and the total), got ${reaps.length}` };
        if (created === null || exited === null) return { ok: false, why: 'the lifetime counters are absent' };
        // Three objects were live when the last reap ran; four ever existed. Both numbers are derived from the scene's own
        // structure -- sibling, recovered, new victim are live, and the victim was the fourth create -- so the two lines are
        // not two readings of one counter that could agree with each other and with nothing else.
        return { ok: reaps[0] === 3 && reaps[1] === created && created === 4 && exited === 4,
          why: `reap returned ${reaps[0]} (three live objects), lifetime reaped ${reaps[1]}, created ${created}, exited ${exited}` };
      }, 'the last reap must take exactly the three objects that were live, and the lifetime totals must close'),
      rel('the-refusal-moved-no-frame', (c) => {
        const pairs = [
          ['FRAMEREC DIAG BEFORE ALLOCATED', 'FRAMEREC DIAG AFTER ALLOCATED'],
          ['FRAMEREC DIAG BEFORE FREE', 'FRAMEREC DIAG AFTER FREE'],
          ['FRAMEREC DIAG BEFORE DIGEST', 'FRAMEREC DIAG AFTER DIGEST'],
        ];
        const bad = pairs.filter(([a, b]) => c.num(a) === null || c.num(b) === null || c.num(a) !== c.num(b));
        return { ok: bad.length === 0, why: bad.map(([a, b]) => `${a}=${c.num(a)} != ${b}=${c.num(b)}`).join('; ') || 'the frame set either side of the failing call is the same set' };
      }, 'the point of the scene is that the call which failed changed nothing'),
      rel('the-pressure-was-exactly-withdrawn', (c) => {
        const bad = [];
        for (const [a, b] of [['FRAMEREC DIAG AFTER VICTIM FREE', 'FRAMEREC DIAG WITHDRAWN FREE'],
          ['FRAMEREC DIAG AFTER VICTIM DIGEST', 'FRAMEREC DIAG WITHDRAWN DIGEST']]) {
          if (c.num(a) === null || c.num(b) === null) bad.push(`${a}/${b} absent`);
          else if (c.num(a) !== c.num(b)) bad.push(`${a}=${c.num(a)} != ${b}=${c.num(b)}`);
        }
        const beforeAlloc = c.num('FRAMEREC DIAG BEFORE ALLOCATED');
        const afterAlloc = c.num('FRAMEREC DIAG AFTER ALLOCATED');
        if (beforeAlloc === null || afterAlloc === null || beforeAlloc !== afterAlloc) {
          bad.push(`the failing call moved the allocation: before=${beforeAlloc} after=${afterAlloc}`);
        }
        return { ok: bad.length === 0, why: bad.join('; ') || 'free count and frame-set digest return to the post-victim reading' };
      }, 'the pressure must be reversible: withdrawing it must land on the reading taken before it was applied'),
      rel('the-sibling-kept-its-bytes', (c) => {
        const atCreate = c.num('FRAMEREC DIAG SIBLING DATA AT CREATE');
        const now = c.num('FRAMEREC DIAG SIBLING DATA');
        return { ok: atCreate !== null && now !== null && atCreate !== 0 && atCreate === now,
          why: `at create 0x${atCreate === null ? 'null' : atCreate.toString(16)}, after recovery 0x${now === null ? 'null' : now.toString(16)}` };
      }, 'the live neighbour is the witness for the whole scene; its bytes must be the bytes it was created with'),
      rel('the-neighbours-hold-different-bytes', (c) => {
        const s = c.num('FRAMEREC DIAG SIBLING DATA');
        const r = c.num('FRAMEREC DIAG RECOVERED DATA');
        return { ok: s !== null && r !== null && s !== 0 && r !== 0 && s !== r, why: `sibling=0x${s === null ? 'null' : s.toString(16)} recovered=0x${r === null ? 'null' : r.toString(16)}` };
      }, 'two live objects holding the same block is a shared mapping, whatever the marker says'),
      rel('the-recovered-object-has-its-own-space', (c) => {
        const sib = c.num('FRAMEREC DIAG SIBLING DIRECTORY');
        const rec = c.num('FRAMEREC DIAG RECOVERED DIRECTORY');
        const vic = c.num('FRAMEREC DIAG VICTIM DIRECTORY');
        const nv = c.num('FRAMEREC DIAG NEW VICTIM DIRECTORY');
        return { ok: sib !== null && rec !== null && sib !== rec && vic !== null && nv !== null && vic === nv && vic !== sib,
          why: `sibling=0x${sib === null ? 'null' : sib.toString(16)} recovered=0x${rec === null ? 'null' : rec.toString(16)} `
            + `victim=0x${vic === null ? 'null' : vic.toString(16)} newVictim=0x${nv === null ? 'null' : nv.toString(16)}` };
      }, 'the retired slot is reused, so the new object inherits that directory and must not inherit a live one'),
      rel('the-cost-is-the-cost-the-scene-derived', (c) => {
        const cost = c.num('FRAMEREC DIAG OBJECT COST');
        const afterSibling = c.num('FRAMEREC DIAG AFTER SIBLING ALLOCATED');
        const baseline = c.num('FRAMEREC DIAG BASELINE ALLOCATED');
        return { ok: cost !== null && afterSibling !== null && baseline !== null && cost === afterSibling - baseline && cost >= 2,
          why: `cost=${cost} afterSibling=${afterSibling} baseline=${baseline}` };
      }, 'the pressure is derived from what one object actually cost, so the derivation has to hold'),
      rel('final-frame-set-is-the-baseline', (c) => {
        const bad = [];
        for (const [a, b] of [['FRAMEREC DIAG BASELINE ALLOCATED', 'FRAMEREC DIAG FINAL ALLOCATED'],
          ['FRAMEREC DIAG BASELINE FREE', 'FRAMEREC DIAG FINAL FREE'],
          ['FRAMEREC DIAG BASELINE DIGEST', 'FRAMEREC DIAG FINAL DIGEST']]) {
          if (c.num(a) === null || c.num(b) === null || c.num(a) !== c.num(b)) bad.push(`${a}=${c.num(a)} vs ${b}=${c.num(b)}`);
        }
        return { ok: bad.length === 0, why: bad.join('; ') || 'every object reaped back to the prepared baseline' };
      }, 'a scene that ends somewhere other than where it started has not shown recovery, it has shown a different machine'),
      rel('the-spare-slot-existed-throughout', (c) => {
        const spare = c.num('FRAMEREC DIAG SPARE SLOTS');
        return { ok: spare !== null && spare >= 1, why: `spare slots=${spare}: the refusal must come from the allocator, not the table` };
      }, 'this scene is the allocator refusal; a refusal with a full table would be the other scene'),
    ],
    forbidden: [
      'FRAMEREC DIAG REFUSED MULTI CPU', 'FRAMEREC DIAG STACKS FAILED', 'FRAMEREC DIAG ENABLE FAILED',
      'FRAMEREC DIAG SEAL FAILED', 'FRAMEREC DIAG ACTIVATE FAILED', 'FRAMEREC DIAG COST UNUSABLE',
      'FRAMEREC DIAG PRESSURE SHORT', 'FRAMEREC DIAG TABLE FULL', 'FRAMEREC DIAG TABLE OCCUPIED',
      'FRAMEREC DIAG STARVED ACCEPTED', 'FRAMEREC DIAG REFUSAL LEFT AN ID', 'FRAMEREC DIAG ALLOCATOR NOT ASKED',
      'FRAMEREC DIAG FRAME SET MOVED', 'FRAMEREC DIAG OBJECT MADE', 'FRAMEREC DIAG SIBLING DAMAGED',
      'FRAMEREC DIAG SIBLING UNREADABLE', 'FRAMEREC DIAG PRESSURE RESIDUE', 'FRAMEREC DIAG RECOVERY FAILED',
      'FRAMEREC DIAG RECOVERED ID REUSED', 'FRAMEREC DIAG RECOVERED DATA WRONG', 'FRAMEREC DIAG RECOVERED NOT LIVE',
      'FRAMEREC DIAG NEIGHBOUR DATA SHARED', 'FRAMEREC DIAG VICTIM STILL LIVE', 'FRAMEREC DIAG OLD ID READ SERVED',
      'FRAMEREC DIAG OLD ID EXIT SERVED', 'FRAMEREC DIAG OLD ID MEMORY SERVED', 'FRAMEREC DIAG UNKNOWN ID SERVED',
      'FRAMEREC DIAG BASELINE DRIFTED', 'FRAMEREC DIAG ACCOUNTING WRONG', 'FRAMEREC DIAG INTERRUPTS ENABLED',
      'FRAMEREC DIAG WALK DISAGREES', 'BOOTDIAG RUNTIME FRAME RECOVERY CHECKPOINT NOT REACHED',
      'PANIC', 'BOOTDIAG REJECT', 'RECOVERY DIAG', 'OBJECT DIAG', 'RUNTIME DIAG', 'WORKER DIAG', 'AP DIAG',
      'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ],
  },
  refusal('runtime-frame-recovery-smp2', 2, 'runtime-frame-recovery', 'BOOTDIAG RUNTIME FRAME RECOVERY REFUSED MULTI CPU', 'FRAMEREC DIAG', ['FRAMEREC DIAG CLAIMED FRAMES', 'FRAMEREC DIAG STARVED REFUSED']),
  refusal('runtime-frame-recovery-smp3', 3, 'runtime-frame-recovery', 'BOOTDIAG RUNTIME FRAME RECOVERY REFUSED MULTI CPU', 'FRAMEREC DIAG', ['FRAMEREC DIAG CLAIMED FRAMES', 'FRAMEREC DIAG STARVED REFUSED']),

  // ================================================================ workers
  {
    name: 'workers-smp1', vcpus: 1, stop: 'workers', flags: ['serial'], boot: 3,
    terminal: 'BOOTDIAG WORKERS NONE AVAILABLE',
    obligations: [
      ...bootPrefix(3),
      num('CPU DETECTED', 1),
      num('WORKER DIAG DETECTED', 1),
      num('WORKER DIAG BSP APIC', 0),
      line('WORKER DIAG SINGLE CPU'),
      num('WORKER DIAG CONFIGURED', 0),
      num('WORKER DIAG SUBMITTED', 0),
      num('WORKER DIAG COLLECTED', 0),
    ],
    forbidden: [
      'WORKER DIAG TICKET', 'WORKER DIAG VALUE', 'WORKER DIAG POOL READY', 'WORKER DIAG DRAINED',
      'BOOTDIAG WORKERS CHECKPOINT NOT REACHED', 'PANIC', 'BOOTDIAG REJECT',
      'RUNTIME DIAG', 'OBJECT DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG', 'AP DIAG',
      'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ],
  },
];

// The two worker scenes differ only in how many APs answered, and everything else about them is computed from that
// number. Writing them once means the per-ticket relation cannot be right in one and wrong in the other.
const workerScene = (name, vcpus) => {
  const workers = vcpus - 1;
  const rows = workers * CONSTANTS.worker_passes * CONSTANTS.worker_jobs_per_worker;
  return {
    name, vcpus, stop: 'workers', flags: ['serial'], boot: 3,
    terminal: 'BOOTDIAG WORKERS DRAINED PARKED',
    obligations: [
      ...bootPrefix(3),
      num('CPU DETECTED', vcpus),
      num('WORKER DIAG DETECTED', vcpus),
      num('WORKER DIAG BSP APIC', 0),
      line('WORKER DIAG POOL READY'),
      num('WORKER DIAG CONFIGURED', workers),
      num('WORKER DIAG READY', workers),
      num('WORKER DIAG FAILED', 0),
      num('WORKER DIAG SESSION', null, 'present'),
      num('WORKER DIAG READY APICS', workers),
      family('WORKER DIAG TICKET', rows),
      numFrom('WORKER DIAG SUBMITTED', () => rows, 'one accepted job per ticket'),
      numFrom('WORKER DIAG COLLECTED', () => rows, 'every submitted job is collected'),
      num('WORKER DIAG MISMATCH', 0),
      numFrom('WORKER DIAG AP EXECUTED', () => rows, 'the BSP submits and the APs execute'),
      num('WORKER DIAG BSP EXECUTED', 0),
      numFrom('WORKER DIAG REUSED SLOTS', () => rows - workers * CONSTANTS.work_queue_capacity,
        'each worker reuses every ring slot exactly once per extra pass'),
      numFrom('WORKER DIAG DISTINCT TICKETS', () => rows, 'every ticket is distinct'),
      line('WORKER DIAG ADMISSION CLOSED'),
      line('WORKER DIAG POST CLOSE REFUSED'),
      line('WORKER DIAG DRAINED'),
      num('WORKER DIAG QUEUED', 0),
      num('WORKER DIAG BUSY', 0),
      num('WORKER DIAG UNCOLLECTED', 0),
      line('WORKER DIAG PARK REQUESTED'),
      numFrom('WORKER DIAG PARK REQUESTED WORKERS', () => workers, 'one park request per ready worker'),
      numFrom('WORKER DIAG PARK ACKED WORKERS', () => workers, 'each AP writes its own acknowledgement'),
      numFrom('WORKER DIAG PARK EXPECTED', () => workers, 'the pool counts the workers it configured'),
      line('WORKER DIAG PARK ACKED'),
      numFrom('WORKER DIAG SUBMITTED TOTAL', () => rows, 'the pool totals the jobs it accepted'),
      numFrom('WORKER DIAG WORKERS EXECUTED', () => workers, 'every configured worker must have executed at least one job'),
      num('WORKER DIAG ROUTED WRONG', 0),
      // ---- the fixed integer job, recomputed
      rel('every-ticket-carries-the-value-the-rule-produces', (c) => {
        const tickets = c.tickets();
        if (tickets.length !== rows) return { ok: false, why: `expected ${rows} ticket rows, got ${tickets.length}` };
        const bad = [];
        for (const t of tickets) {
          const expected = workerValue(t.PASS, t.WORKER, t.JOB);
          if (t.SEED !== workerSeed(t.PASS, t.WORKER, t.JOB)) {
            bad.push(`t${t.TICKET} seed 0x${t.SEED.toString(16)} != 0x${workerSeed(t.PASS, t.WORKER, t.JOB).toString(16)}`);
          }
          if (t.COUNT !== workerCount(t.JOB)) bad.push(`t${t.TICKET} count ${t.COUNT} != ${workerCount(t.JOB)}`);
          if (t.VALUE !== expected) bad.push(`t${t.TICKET} value 0x${t.VALUE.toString(16)} != 0x${expected.toString(16)}`);
          if (t.EXPECTED !== expected) bad.push(`t${t.TICKET} printed expected 0x${t.EXPECTED.toString(16)} != 0x${expected.toString(16)}`);
        }
        return { ok: bad.length === 0, why: bad.slice(0, 6).join('; ') || `${tickets.length} tickets carry seed+sum(1..count)` };
      }, 'the value the AP produced must be the value the fixed input implies, computed here and not read from the guest'),
      rel('every-ticket-was-routed-to-its-own-worker', (c) => {
        const tickets = c.tickets();
        const bad = [];
        for (const t of tickets) {
          if (t.APIC !== t.WORKER + 1) bad.push(`t${t.TICKET} worker ${t.WORKER} ran on apic ${t.APIC}`);
          if (t.SLOT !== t.JOB) bad.push(`t${t.TICKET} job ${t.JOB} took slot ${t.SLOT}`);
        }
        const passes = new Set(tickets.map((t) => t.PASS));
        const workersSeen = new Set(tickets.map((t) => t.WORKER));
        return { ok: bad.length === 0 && passes.size === CONSTANTS.worker_passes && workersSeen.size === workers,
          why: bad.slice(0, 6).join('; ') || `passes=${[...passes].join(',')} workers=${[...workersSeen].join(',')}` };
      }, 'a job that ran on another worker, or in another slot than it was queued for, is not the job the scene submitted'),
      rel('every-worker-contributed', (c) => {
        const tickets = c.tickets();
        const perWorker = new Map();
        for (const t of tickets) perWorker.set(t.WORKER, (perWorker.get(t.WORKER) || 0) + 1);
        const bad = [];
        for (let w = 0; w < workers; w += 1) {
          const n = perWorker.get(w) || 0;
          if (n !== CONSTANTS.worker_passes * CONSTANTS.worker_jobs_per_worker) bad.push(`worker ${w} executed ${n}`);
        }
        return { ok: bad.length === 0, why: bad.join('; ') || `${workers} workers each executed ${CONSTANTS.worker_passes * CONSTANTS.worker_jobs_per_worker} jobs` };
      }, 'a two-AP run that passed on one AP doing all the work has not run two workers'),
      rel('the-tickets-are-distinct-and-the-slots-reused', (c) => {
        const tickets = c.tickets();
        // Ticket ids are per-worker: each AP has its own ring and its own id sequence, so `t1` on worker 0 and `t1` on
        // worker 1 are two different tickets. The identity that must be unique is the pair. The pool's own
        // `DISTINCT TICKETS` counter counts collected tickets rather than measuring distinctness, which is exactly why the
        // judge does not use it for this claim.
        const pairs = tickets.map((t) => t.WORKER + ':' + t.TICKET);
        const distinct = new Set(pairs).size;
        const perWorkerSlots = new Map();
        for (const t of tickets) {
          const key = t.WORKER + ':' + t.SLOT;
          perWorkerSlots.set(key, (perWorkerSlots.get(key) || 0) + 1);
        }
        const reused = [...perWorkerSlots.values()].filter((n) => n > 1).length;
        return { ok: distinct === rows && reused === workers * CONSTANTS.work_queue_capacity,
          why: `distinct (worker,ticket) pairs=${distinct} of ${rows}, reused slots=${reused} of ${workers * CONSTANTS.work_queue_capacity}` };
      }, 'the ring passing its capacity is what makes the occupancy real; if no slot was reused the scene did not test it'),
    ],
    forbidden: [
      'WORKER DIAG SINGLE CPU', 'WORKER DIAG NO READY WORKER', 'WORKER DIAG POOL LIMITED',
      'WORKER DIAG CLOSE FAILED', 'WORKER DIAG POST CLOSE ACCEPTED', 'WORKER DIAG NOT DRAINED',
      'WORKER DIAG PARK REQUEST FAILED', 'WORKER DIAG PARK NOT ACKED', 'BOOTDIAG WORKERS CHECKPOINT NOT REACHED',
      'PANIC', 'BOOTDIAG REJECT', 'RUNTIME DIAG', 'OBJECT DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG',
      'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
    ],
  };
};
SCENE_LIST.push(workerScene('workers-smp2', 2));
SCENE_LIST.push(workerScene('workers-smp3', 3));

// ================================================================ the three scenes that are not a runtime family
SCENE_LIST.push({
  name: 'tables-smp1', vcpus: 1, stop: 'tables', flags: ['serial'], boot: 3,
  terminal: 'BOOTDIAG TABLES CHECKPOINT',
  obligations: [
    ...bootPrefix(3),
    num('CPU DETECTED', 1),
    num('CPU ONLINE', 1),
  ],
  forbidden: [
    'PANIC', 'BOOTDIAG REJECT', 'RUNTIME DIAG', 'OBJECT DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG',
    'WORKER DIAG', 'PAGING DIAG', 'AP DIAG', 'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
  ],
});
SCENE_LIST.push({
  name: 'paging-smp1', vcpus: 1, stop: 'paging', flags: ['serial'], boot: 3,
  terminal: 'BOOTDIAG PAGING ENABLED OWNED MAP',
  obligations: [
    ...bootPrefix(3),
    num('CPU DETECTED', 1),
    num('PAGING DIAG CR0', null, 'present'),
    num('PAGING DIAG CR3', null, 'present'),
    num('PAGING DIAG DIR', null, 'present'),
    num('PAGING DIAG PAGES', null, 'present'),
    num('PAGING DIAG TABLES', null, 'present'),
    num('PAGING DIAG CANARY A', null, 'present'),
    num('PAGING DIAG CANARY B', null, 'present'),
    num('PAGING DIAG VIRTUAL A', null, 'present'),
    num('PAGING DIAG VIRTUAL B', null, 'present'),
    num('PAGING DIAG PHYS A', null, 'present'),
    num('PAGING DIAG PHYS B', null, 'present'),
    line('PAGING DIAG ENABLED'),
    line('PAGING DIAG PG WP SET'),
    line('PAGING DIAG CR3 MATCH'),
    line('PAGING DIAG OWNED MAP PASS'),
    rel('the-two-virtual-addresses-translate-to-two-frames', (c) => {
      const pa = c.num('PAGING DIAG PHYS A');
      const pb = c.num('PAGING DIAG PHYS B');
      return { ok: pa !== null && pb !== null && pa !== 0 && pb !== 0 && pa !== pb,
        why: `physA=0x${pa === null ? 'null' : pa.toString(16)} physB=0x${pb === null ? 'null' : pb.toString(16)}` };
    }, 'one physical frame behind two virtual addresses is an alias, not a mapping test'),
    rel('the-canaries-survive-the-mapping', (c) => {
      const bad = [];
      for (const [a, b] of [['PAGING DIAG CANARY A', 'PAGING DIAG VIRTUAL A'], ['PAGING DIAG CANARY B', 'PAGING DIAG VIRTUAL B']]) {
        if (c.num(a) === null || c.num(b) === null || c.num(a) !== c.num(b)) bad.push(`${a}=${c.num(a)} vs ${b}=${c.num(b)}`);
      }
      return { ok: bad.length === 0, why: bad.join('; ') || 'both canaries read back through the mapping' };
    }, 'a mapping that does not preserve the bytes it maps is not the mapping that was installed'),
    rel('paging-names-the-directory', (c) => {
      const cr3 = c.num('PAGING DIAG CR3');
      const dir = c.num('PAGING DIAG DIR');
      const cr0 = c.num('PAGING DIAG CR0');
      return { ok: cr3 !== null && dir !== null && cr3 === dir && cr0 !== null && ((cr0 & 0x80010000) >>> 0) === 0x80010000,
        why: `cr3=0x${cr3 === null ? 'null' : cr3.toString(16)} dir=0x${dir === null ? 'null' : dir.toString(16)} cr0=0x${cr0 === null ? 'null' : cr0.toString(16)}` };
    }, 'the directory the register names must be the directory the runtime built, with paging and WP on'),
    rel('the-walk-covered-something', (c) => {
      const pages = c.num('PAGING DIAG PAGES');
      const tables = c.num('PAGING DIAG TABLES');
      return { ok: pages !== null && pages > 0 && tables !== null && tables > 0, why: `pages=${pages} tables=${tables}` };
    }, 'a walk that mapped nothing cannot have established an owned map'),
  ],
  forbidden: [
    'PAGING DIAG NOT ENABLED', 'PAGING DIAG PG WP MISSING', 'PAGING DIAG CR3 MISMATCH',
    'PAGING DIAG OWNED MAP FAIL', 'PANIC', 'BOOTDIAG REJECT', 'RUNTIME DIAG', 'OBJECT DIAG',
    'RECOVERY DIAG', 'FRAMEREC DIAG', 'WORKER DIAG', 'AP DIAG',
    'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
  ],
});
SCENE_LIST.push({
  name: 'e99-priority', vcpus: 1, stop: 'memory', flags: ['serial', 'bootlog-fail'], boot: 2,
  terminal: 'PANIC E99 PHASE B02 SAFE DIAGNOSTIC FAILURE',
  // This scene is a refusal with a reason, and the reason is the whole content of it. The legacy bootlog-fail token must
  // keep its precedence: a log that reached the tables checkpoint instead has not tested the precedence, it has tested
  // something else and lost the token on the way.
  obligations: [
    line('BOOT B01 HANDOFF'),
    line('GTOS 0.3 PROTECTED DESKTOP BOOT'),
    line('BOOT B02 MEMORY'),
    line('BOOT SERIAL COM1 115200 8N1'),
    line('PANIC E99 PHASE B02 SAFE DIAGNOSTIC FAILURE'),
  ],
  forbidden: [
    'BOOT B03', 'BOOTDIAG TABLES', 'BOOTDIAG PAGING', 'BOOTDIAG RUNTIME', 'BOOTDIAG WORKERS',
    'BOOTDIAG AP', 'BOOTDIAG REJECT', 'RUNTIME DIAG', 'OBJECT DIAG', 'PAGING DIAG',
    'APP STORE', 'SETTINGS', 'DESKTOP MODE',
  ],
});
SCENE_LIST.push({
  name: 'ap-fresh-smp2', vcpus: 2, stop: 'ap', flags: ['serial'], boot: 3,
  terminal: 'BOOTDIAG AP CHECKPOINT',
  obligations: [
    ...bootPrefix(3),
    num('CPU DETECTED', 2),
    num('AP DIAG DETECTED', 2),
    num('AP DIAG BSP APIC', 0),
    num('AP DIAG RECORD', 1),
    num('AP DIAG APIC', null, 'present'),
    num('AP DIAG OBSERVED', 1),
    num('AP DIAG STATE', null, 'present'),
    num('AP DIAG STACK', null, 'present'),
    num('AP DIAG CHECKSUM', null, 'present'),
    num('AP DIAG ATTEMPTED', 1),
    num('AP DIAG ACK', 1),
    num('AP DIAG PARKED', 1),
    num('AP DIAG FAILED', 0),
    num('AP DIAG FRESH ACK', 1),
    num('AP DIAG STALE ACK', 0),
    line('AP DIAG START PASS'),
    rel('the-ap-is-not-the-bsp', (c) => {
      const bsp = c.num('AP DIAG BSP APIC');
      const apic = c.num('AP DIAG APIC');
      return { ok: bsp !== null && apic !== null && apic !== bsp && apic !== 0, why: `bsp=${bsp} apic=${apic}` };
    }, 'an AP record that names the BSP is the BSP writing about itself'),
    rel('the-ack-is-the-fresh-one', (c) => {
      const ack = c.num('AP DIAG ACK');
      const fresh = c.num('AP DIAG FRESH ACK');
      const stale = c.num('AP DIAG STALE ACK');
      return { ok: ack !== null && fresh !== null && stale !== null && ack === 1 && fresh === 1 && stale === 0,
        why: `ack=${ack} fresh=${fresh} stale=${stale}` };
    }, 'the point of the scene is that a stale acknowledgement is refused; a run that accepted one has not shown it'),
    rel('the-ap-has-a-stack-and-a-checksum', (c) => {
      const stack = c.num('AP DIAG STACK');
      const checksum = c.num('AP DIAG CHECKSUM');
      return { ok: stack !== null && stack !== 0 && checksum !== null && checksum !== 0, why: `stack=0x${stack === null ? 'null' : stack.toString(16)} checksum=0x${checksum === null ? 'null' : checksum.toString(16)}` };
    }, 'a start pass with no stack and no checksum is a claim that the AP ran, not evidence that it did'),
  ],
  forbidden: [
    'AP DIAG SINGLE CPU', 'AP DIAG START LIMITED', 'PANIC', 'BOOTDIAG REJECT',
    'RUNTIME DIAG', 'OBJECT DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG', 'WORKER DIAG', 'PAGING DIAG',
    'BOOT B04', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
  ],
});
SCENE_LIST.push({
  name: 'kernel-invalid', vcpus: 1, stop: null, flags: ['serial', 'bootlog-invalid'], boot: 2,
  terminal: 'BOOTDIAG REJECT invalid-option',
  // The rejecting parser. It owes two rejections in a fixed order: the value it did not recognise, then the option it
  // could not parse. It is the only scene whose terminal line is a rejection, and its reason text is not interchangeable
  // with the E99 scene's.
  obligations: [
    line('BOOT B01 HANDOFF'),
    line('GTOS 0.3 PROTECTED DESKTOP BOOT'),
    line('BOOT B02 MEMORY'),
    line('BOOT SERIAL COM1 115200 8N1'),
    line('BOOTDIAG REJECT reason=unknown-value'),
    line('BOOTDIAG REJECT invalid-option'),
    rel('the-two-rejections-in-order', (c) => {
      const a = c.lineIndex('BOOTDIAG REJECT reason=unknown-value');
      const b = c.lineIndex('BOOTDIAG REJECT invalid-option');
      return { ok: a >= 0 && b > a, why: `unknown-value at ${a}, invalid-option at ${b}` };
    }, 'the parser rejects the value before the option; the other order is a different parser path'),
  ],
  forbidden: [
    'PANIC', 'BOOT B03', 'BOOTDIAG TABLES', 'BOOTDIAG RUNTIME', 'BOOTDIAG WORKERS', 'BOOTDIAG AP',
    'RUNTIME DIAG', 'OBJECT DIAG', 'PAGING DIAG', 'APP STORE', 'SETTINGS', 'DESKTOP MODE',
  ],
});

export const SCENES = SCENE_LIST;

// ---------------------------------------------------------------- the scene set, fixed here
//
// The taskbook fixes twenty scenes and names six of them as cold repeats. The set is declared once, in this file, so that a
// matrix built from a shorter list fails rather than shrinking its own obligations.
//
// Eight scenes run twice rather than six, and the two extra are the packet's own two startup-state scenes: `ap-fresh-smp2`
// asserts that a second AP's acknowledgement is fresh and not a stale value left by the first, and `kernel-invalid` asserts a
// refusal that happens before any device is touched. Both are claims about state that only exists at startup, so a single cold
// start cannot distinguish "the value was produced by this boot" from "the value was already there". The six the taskbook names
// are a floor, not a ceiling; the two added here are recorded separately so the difference is visible rather than folded in.
export const COLD_REPEAT_SCENES = [
  'runtime-init-smp1', 'runtime-objects-smp1', 'runtime-recovery-smp1',
  'runtime-frame-recovery-smp1', 'workers-smp2', 'workers-smp3',
];
// Repeats beyond the six the taskbook names, each with the reason it needs a second cold start.
export const ADDITIONAL_REPEAT_SCENES = {
  'ap-fresh-smp2': 'a fresh AP acknowledgement is only distinguishable from a stale one across two boots',
  'kernel-invalid': 'a refusal before any device is touched has to be seen twice to show it is not a leftover state',
};
export const REPEATED_SCENES = [...COLD_REPEAT_SCENES, ...Object.keys(ADDITIONAL_REPEAT_SCENES)];
export const trialsFor = (name) => (REPEATED_SCENES.includes(name) ? 2 : 1);
export const EXPECTED_SCENE_COUNT = 20;
export const EXPECTED_TRIAL_COUNT = SCENE_LIST.reduce((n, s) => n + trialsFor(s.name), 0);

const BY_NAME = new Map(SCENE_LIST.map((s) => [s.name, s]));

// A scene name that is not in the table is not a scene. This is the gate the pre-147 judge did not have: it looked the
// name up, found nothing, and returned whatever obligations the caller had built from the log.
export class UnknownScene extends Error {
  constructor(name) { super('unknown scene: ' + name); this.name = 'UnknownScene'; this.scene = name; }
}

export const scene = (name) => {
  const found = BY_NAME.get(name);
  if (!found) throw new UnknownScene(name);
  return found;
};
export const sceneNames = () => SCENE_LIST.map((s) => s.name);
export const hasScene = (name) => BY_NAME.has(name);

// ---------------------------------------------------------------- channel reading
//
// The parsers below read a channel into the shapes the obligations use: labelled numbers, the ordered list of lines, and the
// repeating records (`FILLED ID/SLOT/DIR/DATA`, the worker ticket rows). A label whose value is not a fixed eight-digit hex
// word is not silently coerced: the line stays text and the number is absent, so an obligation that names it fails rather
// than reading a truncated value.
//
// A label may be printed more than once in one run -- the recovery scene prints its baseline three times, the frame scene
// prints two different final-reap counts -- so a label maps to the list of its values in the order they were printed.
// `num` is the first, `nums` is all of them, and the judge requires every occurrence of a single-valued label to carry the
// declared value. That is what makes a log with two different `CPU DETECTED` lines a failure rather than a coin toss.
const HEX8 = /^[0-9A-F]{8}$/;

export const parseChannel = (text) => {
  const lines = String(text).split(/\r?\n/).map((l) => l.trimEnd()).filter((l) => l.length > 0);
  const numbers = new Map();
  const index = new Map();
  const labelOrder = [];
  for (let i = 0; i < lines.length; i += 1) {
    const line = lines[i];
    if (!index.has(line)) index.set(line, i);
    const m = line.match(/^(.*\S)\s+([0-9A-Fa-f]{1,8})$/);
    if (!m) continue;
    const label = m[1];
    const raw = m[2].toUpperCase();
    if (!HEX8.test(raw)) continue;
    const value = parseInt(raw, 16);
    const list = numbers.get(label);
    if (list) list.push(value); else numbers.set(label, [value]);
    labelOrder.push(label);
  }
  return { text: String(text), lines, index, numbers, labelOrder };
};

const numsOf = (channel, label) => channel.numbers.get(label) || [];
const numOf = (channel, label) => {
  const list = numsOf(channel, label);
  return list.length ? list[0] : null;
};

// The repeating records. The recovery scene prints one block per object, twice: once when the object is created and once
// after the refusal, the recovery and the reap. The two blocks are keyed differently in the channel -- the fill block is
// headed by a sequence number and carries its own `ID`, while the survivor block is headed by the `ID` itself -- so each is
// read by its own parser rather than by one generic rule that would have to guess which line is the head.
const readFields = (lines, headPrefix, fieldNames) => {
  const out = [];
  let current = null;
  for (const line of lines) {
    if (!line.startsWith(headPrefix + ' ')) continue;
    const rest = line.slice(headPrefix.length + 1);
    const m = rest.match(/^(.*\S)\s+([0-9A-Fa-f]{8})$/);
    if (!m) continue;
    const name = m[1].trim();
    const value = parseInt(m[2], 16);
    if (name === 'ID') {
      if (current) out.push(current);
      current = { ID: value };
      continue;
    }
    if (!current) continue;
    if (!fieldNames.includes(name)) continue;
    current[name] = value;
  }
  if (current) out.push(current);
  return out.filter((r) => fieldNames.every((f) => typeof r[f] === 'number'));
};

const filledRecords = (channel) => readFields(channel.lines, 'RECOVERY DIAG FILLED', ['ID', 'SLOT', 'DIR', 'DATA']);
const survivorRecords = (channel) => readFields(channel.lines, 'RECOVERY DIAG SURVIVOR', ['SLOT', 'DIR', 'DATA']);

// The worker rows are six consecutive labelled lines per ticket. They are read as a window rather than as six separate
// families, because the obligation is a relation between the six values of one job.
const TICKET_FIELDS = ['PASS', 'WORKER', 'JOB', 'SEED', 'COUNT', 'TICKET', 'SLOT', 'APIC', 'VALUE', 'EXPECTED'];
const ticketParser = (channel) => {
  const out = [];
  let current = null;
  for (const line of channel.lines) {
    const m = line.match(/^WORKER DIAG (PASS|WORKER|JOB|SEED|COUNT|TICKET|SLOT|APIC|VALUE|EXPECTED) ([0-9A-F]{8})$/);
    if (!m) continue;
    if (m[1] === 'PASS') { if (current) out.push(current); current = {}; }
    if (!current) continue;
    current[m[1]] = parseInt(m[2], 16);
  }
  if (current) out.push(current);
  return out.filter((r) => TICKET_FIELDS.every((f) => typeof r[f] === 'number'));
};

export const channelView = (parsed) => ({
  lines: parsed.lines,
  text: parsed.text,
  num: (label) => numOf(parsed, label),
  nums: (label) => numsOf(parsed, label),
  lineIndex: (text) => (parsed.index.has(text) ? parsed.index.get(text) : -1),
  has: (text) => parsed.index.has(text),
  records: (head) => {
    if (head === 'RECOVERY DIAG FILLED') return filledRecords(parsed);
    if (head === 'RECOVERY DIAG SURVIVOR') return survivorRecords(parsed);
    throw new Error('no record parser for ' + head);
  },
  tickets: () => ticketParser(parsed),
});

export const CONTRACT_SUMMARY = {
  version: CONTRACT_VERSION,
  scenes: SCENE_LIST.length,
  trials: EXPECTED_TRIAL_COUNT,
  cold_repeats: COLD_REPEAT_SCENES.length,
  obligations: SCENE_LIST.reduce((n, s) => n + s.obligations.length, 0),
  forbidden: SCENE_LIST.reduce((n, s) => n + s.forbidden.length, 0),
};
