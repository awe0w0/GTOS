// GTOS-DSH-146 R: the non-resource scenes judged on relations between the numbers the guest printed.
//
// 145's judge handled the resource scenes by rebuilding bitmap members, and for everything else it fell back to a pass
// marker, a forbidden-string check and a few exact lines. That is a shape check: a channel printing
// `RUNTIME DIAG CR3 00A28000` and one printing `RUNTIME DIAG CR3 00000000` were equally acceptable, because nothing
// compared the number to anything.
//
// Every obligation here is a relation. The strong ones are relations that cannot be satisfied by printing the right label:
//
//   * CR3 and the page directory are the same page-aligned address;
//   * the TR selector decomposes into the index, TI and RPL the guest also printed, and that index lies inside the GDT the
//     guest dumped with SGDT;
//   * the TSS descriptor's base and limit equal the values the guest independently expected, and the TSS does not overlap
//     the GDT;
//   * the allocator's two counters add up to the frame count a different subsystem printed at boot, before this ran;
//   * a refused allocation created no object even though it moved the allocator;
//   * each worker ticket's VALUE equals its own EXPECTED, and MISMATCH equals the number of tickets where it does not;
//   * the serial and E9 channels of one run carry the same numbers for every label they share.
//
// Two things this file deliberately does not do.
//
// It does not invent a scene's semantics. The multi-CPU scenes print `REFUSED MULTI CPU` and then stop; that is the honest
// partial this packet already records elsewhere, so the obligations for those scenes check the refusal (detected two,
// online one, no runtime numbers afterwards) rather than pretending the runtime ran.
//
// It does not dress a marker-only scene up as a numeric one. `tables-smp1`, `e99-priority` and `kernel-invalid` print no
// numbers beyond the boot line, so they are listed in `marker_only_scenes` and the record says so, instead of gaining
// three vacuous obligations to make a count look better.

import crypto from 'node:crypto';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const bit = (v, n) => ((v >>> n) & 1) === 1;
const PAGE = 0x1000;

// A channel is lines of `LABEL WORD...` where a word may be eight hex digits. Parsing is strict, and repeats are kept:
// several labels here are per-ticket counters printed many times, and taking only the last one would silently discard
// every ticket but the last.
export function parseNumbers(text) {
  const values = new Map();
  const repeats = new Map();
  const lines = [];
  for (const raw of String(text).split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) continue;
    lines.push(line);
    const m = /^(.*?)\s+([0-9A-Fa-f]{8})$/.exec(line);
    if (!m) continue;
    const label = m[1].trim();
    const value = parseInt(m[2], 16) >>> 0;
    if (!values.has(label)) values.set(label, value);
    if (!repeats.has(label)) repeats.set(label, []);
    repeats.get(label).push(value);
  }
  return {
    values, repeats, lines,
    get(label) { return values.has(label) ? values.get(label) : null; },
    all(label) { return repeats.get(label) || []; },
    has(label) { return values.has(label); },
    count(label) { return (repeats.get(label) || []).length; },
  };
}

// Per-ticket grouping for the worker scenes: `TICKET n` starts a group, and the following labelled lines belong to it.
export function parseTickets(nums) {
  const tickets = [];
  let current = null;
  for (const line of nums.lines) {
    const m = /^(.*?)\s+([0-9A-Fa-f]{8})$/.exec(line);
    if (!m) continue;
    const label = m[1].trim();
    const value = parseInt(m[2], 16) >>> 0;
    if (label === 'WORKER DIAG TICKET') {
      current = { ticket: value, fields: {} };
      tickets.push(current);
      continue;
    }
    if (current && !(label in current.fields)) current.fields[label] = value;
  }
  return tickets;
}

function judgeScene(scene, nums, other, boot) {
  const out = [];
  const add = (obligation, value, facts) => out.push({ obligation, value: value === true, ...(facts || {}) });
  const addMissing = (family, labels) => {
    if (labels.length) add(family + '.prints_the_numbers_it_is_judged_on', false, { missing: labels });
    else add(family + '.prints_the_numbers_it_is_judged_on', true, { missing: [] });
  };

  // ---- the boot numbers, which every scene that got past B02 prints.
  //
  // Two scenes in the matrix never get there: the emergency screen panics at B02, and the boot diagnostic rejects its own
  // option before the CPU lines exist. Asking those two whether their online count is at most their detected count is
  // asking about numbers that were never printed, and it produces a false finding -- the trial is a refusal that worked,
  // not a boot that failed. So the two are told apart by whether the boot numbers were printed at all, and a refusal is
  // judged on its own terms: it stopped, it said why, and it printed none of what the full path would have printed.
  const reachedBootNumbers = boot.detected !== null || boot.online !== null;
  if (reachedBootNumbers) {
    add('boot.cpu_online_is_at_most_detected',
      boot.online === null || boot.detected === null ? false : boot.online <= boot.detected,
      { detected: boot.detected, online: boot.online });
    add('boot.at_least_one_cpu_detected', boot.detected !== null && boot.detected >= 1, { detected: boot.detected });
    if (boot.freeFrames !== null) add('boot.free_frames_is_not_zero', boot.freeFrames > 0, { free_frames: boot.freeFrames });
  } else {
    const why = nums.lines.find((l) => /^PANIC |^BOOTDIAG REJECT /.test(l)) || null;
    add('refused-boot.printed_no_boot_numbers', true, { detected: boot.detected, online: boot.online });
    add('refused-boot.says_why_it_stopped', why !== null, { line: why });
    // The proof that it stopped is that the numbers the full path prints are absent. This is the same shape as the multi-CPU
    // refusal above, and for the same reason: what makes a refusal a refusal is that nothing followed it.
    const wouldHaveFollowed = ['CPU DETECTED', 'CPU ONLINE', 'MEMORY FREE FRAMES', 'BOOTDIAG TABLES CHECKPOINT']
      .filter((l) => nums.has(l));
    add('refused-boot.did_not_reach_a_checkpoint', wouldHaveFollowed.length === 0, { found: wouldHaveFollowed });
    const families = ['RUNTIME DIAG', 'OBJECT DIAG', 'RECOVERY DIAG', 'WORKER DIAG']
      .filter((p) => nums.lines.some((l) => l.startsWith(p + ' ')));
    add('refused-boot.printed_nothing_from_the_full_path', families.length === 0, { found: families });
  }

  // ---- family: the runtime refused to run because more than one CPU was detected.
  const refusalPrefix = ['RUNTIME DIAG', 'OBJECT DIAG', 'RECOVERY DIAG'].find((p) => nums.lines.includes(p + ' REFUSED MULTI CPU'));
  if (refusalPrefix) {
    const family = { 'RUNTIME DIAG': 'runtime', 'OBJECT DIAG': 'objects', 'RECOVERY DIAG': 'recovery' }[refusalPrefix];
    const refusal = family + '-refusal';
    addMissing(refusal, ['CPU DETECTED', 'CPU ONLINE', refusalPrefix + ' DETECTED'].filter((l) => !nums.has(l)));
    add(refusal + '.detected_agrees_with_the_cpu_count', nums.get(refusalPrefix + ' DETECTED') === nums.get('CPU DETECTED'),
      { diag: nums.get(refusalPrefix + ' DETECTED'), cpu: nums.get('CPU DETECTED') });
    add(refusal + '.detected_is_more_than_one', (nums.get('CPU DETECTED') || 0) > 1, { detected: nums.get('CPU DETECTED') });
    add(refusal + '.exactly_one_cpu_is_online', nums.get('CPU ONLINE') === 1, { online: nums.get('CPU ONLINE') });
    // The refusal has to be a refusal: none of the numbers the full path would have printed may appear afterwards.
    const wouldHaveFollowed = [refusalPrefix + ' CR3', refusalPrefix + ' TR READ', refusalPrefix + ' STACKS PREPARED',
      refusalPrefix + ' BASELINE ALLOCATED', refusalPrefix + ' ID A'].filter((l) => nums.has(l));
    add(refusal + '.printed_nothing_from_the_full_path', wouldHaveFollowed.length === 0, { found: wouldHaveFollowed });
    add(refusal + '.the_guest_says_it_refused', nums.lines.includes(refusalPrefix + ' REFUSED MULTI CPU'),
      { marker: refusalPrefix + ' REFUSED MULTI CPU' });
  }

  // ---- family: the task register and the TSS it selects.
  const trSel = nums.get('RUNTIME DIAG TR SELECTOR');
  if (trSel !== null) {
    const need = ['RUNTIME DIAG TR INDEX', 'RUNTIME DIAG TR TI', 'RUNTIME DIAG TR RPL', 'RUNTIME DIAG SGDT BASE',
      'RUNTIME DIAG SGDT LIMIT', 'RUNTIME DIAG TSS DESC OFFSET', 'RUNTIME DIAG TSS DESC TYPE', 'RUNTIME DIAG TSS DESC BASE',
      'RUNTIME DIAG TSS DESC LIMIT', 'RUNTIME DIAG TSS SS0', 'RUNTIME DIAG TSS ESP0', 'RUNTIME DIAG TSS IOMAP'].filter((l) => !nums.has(l));
    addMissing('runtime', need);

    const index = nums.get('RUNTIME DIAG TR INDEX');
    const ti = nums.get('RUNTIME DIAG TR TI');
    const rpl = nums.get('RUNTIME DIAG TR RPL');
    // The selector is not an opaque number: it is index, table indicator and privilege level, and the guest printed all
    // four. If any disagreed, the selector would not decompose -- which no marker notices.
    add('runtime.tr.selector_decomposes_into_its_parts',
      index !== null && ti !== null && rpl !== null && trSel === (((index << 3) | (ti << 2) | rpl) >>> 0),
      { selector: '0x' + trSel.toString(16), index, ti, rpl });
    add('runtime.tr.selector_is_ring0', (trSel & 3) === 0, { rpl: trSel & 3 });
    add('runtime.tr.selector_uses_the_gdt', ((trSel >>> 2) & 1) === 0, { ti: (trSel >>> 2) & 1 });
    add('runtime.tr.read_back_nonzero', (nums.get('RUNTIME DIAG TR READ') || 0) !== 0, { read: nums.get('RUNTIME DIAG TR READ') });
    const expectedSel = nums.get('RUNTIME DIAG EXPECT SELECTOR');
    if (expectedSel !== null) add('runtime.tr.selector_is_the_expected_one', trSel === expectedSel,
      { selector: '0x' + trSel.toString(16), expected: '0x' + expectedSel.toString(16) });

    const gdtBase = nums.get('RUNTIME DIAG SGDT BASE');
    const gdtLimit = nums.get('RUNTIME DIAG SGDT LIMIT');
    const descOffset = nums.get('RUNTIME DIAG TSS DESC OFFSET');
    if (gdtLimit !== null && index !== null) {
      add('runtime.tr.index_is_inside_the_gdt', (index * 8 + 7) <= gdtLimit, { index, limit: gdtLimit, needs: index * 8 + 7 });
    }
    if (descOffset !== null && index !== null) {
      add('runtime.tss.descriptor_offset_is_the_selector_index', descOffset === index * 8, { offset: descOffset, index });
    }
    if (descOffset !== null && gdtLimit !== null) {
      add('runtime.tss.descriptor_is_inside_the_gdt', (descOffset + 8) <= (gdtLimit + 1), { offset: descOffset, limit: gdtLimit });
    }
    const descType = nums.get('RUNTIME DIAG TSS DESC TYPE');
    if (descType !== null) {
      // 0x8B is present, DPL 0, type 1011: a busy 32-bit TSS. The 144 packet's whole subject was this byte.
      add('runtime.tss.descriptor_is_a_busy_32bit_tss', descType === 0x8B, { type: '0x' + descType.toString(16), want: '0x8b' });
      add('runtime.tss.descriptor_is_present_and_dpl0', bit(descType, 7) && ((descType >>> 5) & 3) === 0,
        { present: bit(descType, 7), dpl: (descType >>> 5) & 3 });
    }
    const descBase = nums.get('RUNTIME DIAG TSS DESC BASE');
    const descLimit = nums.get('RUNTIME DIAG TSS DESC LIMIT');
    const expBase = nums.get('RUNTIME DIAG EXPECT TSS BASE');
    const expLimit = nums.get('RUNTIME DIAG EXPECT TSS LIMIT');
    if (descBase !== null && expBase !== null) add('runtime.tss.descriptor_base_is_the_expected_tss', descBase === expBase,
      { descriptor: '0x' + descBase.toString(16), expected: '0x' + expBase.toString(16) });
    if (descLimit !== null && expLimit !== null) add('runtime.tss.descriptor_limit_is_the_expected_tss', descLimit === expLimit,
      { descriptor: '0x' + descLimit.toString(16), expected: '0x' + expLimit.toString(16) });
    if (descBase !== null && gdtBase !== null && descLimit !== null && gdtLimit !== null) {
      const gdtEnd = gdtBase + gdtLimit + 1;
      const tssEnd = descBase + descLimit + 1;
      add('runtime.tss.does_not_overlap_the_gdt', tssEnd <= gdtBase || descBase >= gdtEnd,
        { gdt: ['0x' + gdtBase.toString(16), '0x' + gdtEnd.toString(16)], tss: ['0x' + descBase.toString(16), '0x' + tssEnd.toString(16)] });
    }
    const ss0 = nums.get('RUNTIME DIAG TSS SS0');
    if (ss0 !== null) {
      add('runtime.tss.ss0_is_a_ring0_data_selector', (ss0 & 3) === 0 && (ss0 >>> 3) === 3,
        { ss0: '0x' + ss0.toString(16), rpl: ss0 & 3, index: ss0 >>> 3 });
      const expSs0 = nums.get('RUNTIME DIAG EXPECT SS0');
      if (expSs0 !== null) add('runtime.tss.ss0_is_the_expected_selector', ss0 === expSs0, { ss0: '0x' + ss0.toString(16), expected: expSs0 });
    }
    const esp0 = nums.get('RUNTIME DIAG TSS ESP0');
    if (esp0 !== null) add('runtime.tss.esp0_is_a_usable_stack', esp0 !== 0 && (esp0 & 0xF) === 0,
      { esp0: '0x' + esp0.toString(16), low: esp0 & 0xF });
    const iomap = nums.get('RUNTIME DIAG TSS IOMAP');
    if (iomap !== null && descLimit !== null) {
      add('runtime.tss.iomap_base_is_past_the_limit', iomap >= descLimit, { iomap, limit: descLimit });
      const expIomap = nums.get('RUNTIME DIAG EXPECT IOMAP');
      if (expIomap !== null) add('runtime.tss.iomap_is_the_expected_one', iomap === expIomap, { iomap, expected: expIomap });
    }
    if (nums.has('RUNTIME DIAG EXPECT LOADED')) {
      add('runtime.tss.the_guest_loaded_what_it_expected', nums.get('RUNTIME DIAG EXPECT LOADED') === 1,
        { loaded: nums.get('RUNTIME DIAG EXPECT LOADED') });
    }
    if (nums.has('RUNTIME DIAG TSS OBLIGATIONS FAILED')) {
      add('runtime.tss.the_guest_checked_itself_and_found_nothing', nums.get('RUNTIME DIAG TSS OBLIGATIONS FAILED') === 0,
        { failed: nums.get('RUNTIME DIAG TSS OBLIGATIONS FAILED') });
    }
  }

  // ---- family: CR0 and CR3, wherever they are printed.
  for (const p of ['RUNTIME DIAG', 'PAGING DIAG', 'OBJECT DIAG', 'RECOVERY DIAG', 'FRAMEREC DIAG']) {
    const cr0 = nums.get(p + ' CR0');
    if (cr0 !== null) {
      add('cr0.has_paging_and_protection', bit(cr0, 31) && bit(cr0, 0),
        { where: p, cr0: '0x' + cr0.toString(16), pg: bit(cr0, 31), pe: bit(cr0, 0) });
      // CD and NW are bits 30 and 29: caching and write-back must be on, or the kernel is running far slower than it says.
      add('cr0.has_caching_enabled', !bit(cr0, 30) && !bit(cr0, 29), { where: p, cd: bit(cr0, 30), nw: bit(cr0, 29) });
    }
    const cr3 = nums.get(p + ' CR3');
    if (cr3 !== null) {
      add('cr3.is_page_aligned', (cr3 & (PAGE - 1)) === 0, { where: p, cr3: '0x' + cr3.toString(16), low: cr3 & (PAGE - 1) });
      add('cr3.is_not_null', cr3 !== 0, { where: p, cr3: '0x' + cr3.toString(16) });
      const dir = nums.get(p + ' DIRECTORY') !== null ? nums.get(p + ' DIRECTORY') : nums.get(p + ' DIR');
      if (dir !== null) add('cr3.is_the_page_directory', cr3 === dir, { where: p, cr3: '0x' + cr3.toString(16), dir: '0x' + dir.toString(16) });
    }
  }

  // ---- family: the runtime object counters.
  if (nums.has('RUNTIME DIAG CREATED')) {
    const created = nums.get('RUNTIME DIAG CREATED');
    const exited = nums.get('RUNTIME DIAG EXITED');
    const faulted = nums.get('RUNTIME DIAG FAULTED');
    const reaped = nums.get('RUNTIME DIAG REAPED');
    add('runtime.counters.no_more_reaped_than_left', reaped <= (exited || 0) + (faulted || 0), { created, exited, faulted, reaped });
    if (created === 0) add('runtime.counters.empty_means_zero_everything', exited === 0 && reaped === 0 && (faulted || 0) === 0,
      { created, exited, faulted, reaped });
  }

  // ---- family: paging.
  if (nums.has('PAGING DIAG PAGES')) {
    const pages = nums.get('PAGING DIAG PAGES');
    const tables = nums.get('PAGING DIAG TABLES');
    addMissing('paging', ['PAGING DIAG CR0', 'PAGING DIAG CR3', 'PAGING DIAG DIR', 'PAGING DIAG PAGES', 'PAGING DIAG TABLES',
      'PAGING DIAG CANARY A', 'PAGING DIAG CANARY B', 'PAGING DIAG VIRTUAL A', 'PAGING DIAG VIRTUAL B',
      'PAGING DIAG PHYS A', 'PAGING DIAG PHYS B'].filter((l) => !nums.has(l)));
    add('paging.mapped_pages_exist', pages > 0, { pages });
    add('paging.page_tables_exist', tables > 0, { tables });
    add('paging.tables_are_fewer_than_pages', tables <= pages, { tables, pages });
    const ca = nums.get('PAGING DIAG CANARY A');
    const cb = nums.get('PAGING DIAG CANARY B');
    const va = nums.get('PAGING DIAG VIRTUAL A');
    const vb = nums.get('PAGING DIAG VIRTUAL B');
    if (ca !== null && cb !== null) add('paging.the_two_canaries_differ', ca !== cb, { a: '0x' + ca.toString(16), b: '0x' + cb.toString(16) });
    if (ca !== null && va !== null) add('paging.virtual_a_reads_canary_a', va === ca, { virtual: '0x' + va.toString(16), canary: '0x' + ca.toString(16) });
    if (cb !== null && vb !== null) add('paging.virtual_b_reads_canary_b', vb === cb, { virtual: '0x' + vb.toString(16), canary: '0x' + cb.toString(16) });
    const pa = nums.get('PAGING DIAG PHYS A');
    const pb = nums.get('PAGING DIAG PHYS B');
    if (pa !== null) add('paging.phys_a_is_page_aligned', (pa & (PAGE - 1)) === 0, { phys: '0x' + pa.toString(16), low: pa & (PAGE - 1) });
    if (pb !== null) add('paging.phys_b_is_page_aligned', (pb & (PAGE - 1)) === 0, { phys: '0x' + pb.toString(16), low: pb & (PAGE - 1) });
    if (pa !== null && pb !== null) add('paging.the_two_pages_are_different', pa !== pb, { a: '0x' + pa.toString(16), b: '0x' + pb.toString(16) });
  }

  // ---- family: the AP.
  if (nums.has('AP DIAG DETECTED')) {
    const detected = nums.get('AP DIAG DETECTED');
    add('ap.detected_agrees_with_the_cpu_count', detected === nums.get('CPU DETECTED'), { ap: detected, cpu: nums.get('CPU DETECTED') });
    const state = nums.get('AP DIAG STATE');
    if (state !== null) {
      add('ap.state_is_a_known_bitmask', (state & ~0x7) === 0, { state: '0x' + state.toString(16), unknown: state & ~0x7 });
      add('ap.state_is_fully_set', (state & 0x7) === 0x7, { state: '0x' + state.toString(16) });
    }
    const stack = nums.get('AP DIAG STACK');
    if (stack !== null) add('ap.stack_is_aligned', stack !== 0 && (stack & 0xF) === 0, { stack: '0x' + stack.toString(16), low: stack & 0xF });
    const sum = nums.get('AP DIAG CHECKSUM');
    if (sum !== null) add('ap.checksum_is_not_the_empty_sum', sum !== 0, { checksum: '0x' + sum.toString(16) });
    const attempted = nums.get('AP DIAG ATTEMPTED');
    const ack = nums.get('AP DIAG ACK');
    if (attempted !== null && ack !== null) add('ap.no_more_acks_than_attempts', ack <= attempted, { attempted, ack });
    const fresh = nums.get('AP DIAG FRESH ACK');
    const stale = nums.get('AP DIAG STALE ACK');
    if (fresh !== null && stale !== null) {
      add('ap.a_fresh_ticket_is_acknowledged', fresh === 1, { fresh });
      add('ap.a_stale_ticket_is_refused', stale === 0, { stale });
      add('ap.the_two_acknowledgements_differ', fresh !== stale, { fresh, stale });
    }
    const failed = nums.get('AP DIAG FAILED');
    if (failed !== null) add('ap.no_ap_failed', failed === 0, { failed });
    const parked = nums.get('AP DIAG PARKED');
    if (parked !== null && boot.online !== null && detected !== null) {
      // A relation between three numbers, so a `PARKED` marker cannot satisfy it.
      add('ap.parked_is_detected_minus_online', parked === Math.max(0, detected - boot.online), { detected, online: boot.online, parked });
    }
  }

  // ---- family: workers on a machine with one CPU, where there is nothing to do.
  //
  // The condition is DETECTED, not ONLINE. Every trial here prints `CPU ONLINE 00000001` -- the APs are started but the
  // guest's own counter of online CPUs stays at one -- so gating on ONLINE made this family fire on the two-CPU and
  // three-CPU trials as well, where the pool is configured and there is plenty of work. That was a wrong claim about the
  // guest, and it showed up as four real trials failing.
  if (nums.has('WORKER DIAG CONFIGURED') && boot.detected === 1) {
    const configured = nums.get('WORKER DIAG CONFIGURED');
    const submitted = nums.get('WORKER DIAG SUBMITTED');
    const collected = nums.get('WORKER DIAG COLLECTED');
    // One CPU means no worker pool and no work. A marker saying SINGLE CPU next to counters saying four would pass a
    // marker judge and fail this.
    add('workers.single_cpu_means_no_workers', configured === 0, { detected: boot.detected, configured });
    add('workers.single_cpu_means_no_work', submitted === 0 && collected === 0, { submitted, collected });
    add('workers.single_cpu_has_no_tickets', nums.count('WORKER DIAG TICKET') === 0, { tickets: nums.count('WORKER DIAG TICKET') });
  }

  // ---- family: the worker pool on more than one detected CPU.
  if (nums.has('WORKER DIAG SUBMITTED') && (boot.detected || 0) > 1) {
    const configured = nums.get('WORKER DIAG CONFIGURED');
    const submitted = nums.get('WORKER DIAG SUBMITTED');
    const collected = nums.get('WORKER DIAG COLLECTED');
    const mismatch = nums.get('WORKER DIAG MISMATCH');
    const apExecuted = nums.get('WORKER DIAG AP EXECUTED');
    const bspExecuted = nums.get('WORKER DIAG BSP EXECUTED');
    const reused = nums.get('WORKER DIAG REUSED SLOTS');
    const distinct = nums.get('WORKER DIAG DISTINCT TICKETS');
    const routedWrong = nums.get('WORKER DIAG ROUTED WRONG');
    const ready = nums.get('WORKER DIAG READY');
    const failed = nums.get('WORKER DIAG FAILED');
    const readyApics = nums.get('WORKER DIAG READY APICS');
    const session = nums.get('WORKER DIAG SESSION');

    addMissing('workers', ['WORKER DIAG DETECTED', 'WORKER DIAG CONFIGURED', 'WORKER DIAG READY', 'WORKER DIAG FAILED',
      'WORKER DIAG SUBMITTED', 'WORKER DIAG COLLECTED', 'WORKER DIAG MISMATCH', 'WORKER DIAG AP EXECUTED',
      'WORKER DIAG BSP EXECUTED', 'WORKER DIAG REUSED SLOTS', 'WORKER DIAG DISTINCT TICKETS', 'WORKER DIAG ROUTED WRONG',
      'WORKER DIAG PARK EXPECTED', 'WORKER DIAG PARK ACKED WORKERS'].filter((l) => !nums.has(l)));

    add('workers.the_pool_is_sized_from_the_detected_cpus', configured === (nums.get('WORKER DIAG DETECTED') || 0) - 1,
      { detected: nums.get('WORKER DIAG DETECTED'), configured });
    add('workers.every_configured_worker_became_ready', ready === configured && readyApics === configured, { ready, readyApics, configured });
    add('workers.no_worker_failed', failed === 0, { failed });
    add('workers.there_is_one_session', session === 1, { session });
    add('workers.every_submission_was_collected', submitted === collected, { submitted, collected });
    add('workers.every_ticket_is_distinct', distinct === submitted, { distinct, submitted });
    add('workers.nothing_was_routed_wrong', routedWrong === 0, { routedWrong });
    add('workers.the_bsp_did_none_of_the_work', bspExecuted === 0, { bspExecuted, submitted });
    add('workers.the_aps_did_all_of_the_work', apExecuted === submitted, { apExecuted, submitted });
    add('workers.every_worker_executed_something', nums.get('WORKER DIAG WORKERS EXECUTED') === configured,
      { executed: nums.get('WORKER DIAG WORKERS EXECUTED'), configured });
    if (reused !== null) {
      // Sixteen slots per worker is what the guest allocates; the ticket count beyond that is reuse, and the two numbers
      // have to agree.
      const slots = configured * 16;
      add('workers.reused_slots_is_the_overflow', reused === submitted - slots, { reused, submitted, slots });
    }
    const queued = nums.get('WORKER DIAG QUEUED');
    const busy = nums.get('WORKER DIAG BUSY');
    const uncollected = nums.get('WORKER DIAG UNCOLLECTED');
    if (queued !== null) add('workers.the_queue_drained', queued === 0 && busy === 0 && uncollected === 0, { queued, busy, uncollected });
    const parkExpected = nums.get('WORKER DIAG PARK EXPECTED');
    const parkAcked = nums.get('WORKER DIAG PARK ACKED WORKERS');
    if (parkExpected !== null && parkAcked !== null) {
      add('workers.every_park_request_was_acknowledged', parkAcked === parkExpected && parkExpected === configured,
        { parkExpected, parkAcked, configured });
    }
    add('workers.submitted_total_agrees', nums.get('WORKER DIAG SUBMITTED TOTAL') === submitted,
      { total: nums.get('WORKER DIAG SUBMITTED TOTAL'), submitted });

    // The per-ticket relations. Each ticket prints its own VALUE and EXPECTED, so the guest's own claim about the ticket is
    // compared to the ticket's own number -- and MISMATCH has to equal the count of tickets where they disagree.
    const tickets = parseTickets(nums);
    add('workers.the_tickets_are_the_submissions', tickets.length === submitted, { tickets: tickets.length, submitted });
    const withBoth = tickets.filter((t) => 'WORKER DIAG VALUE' in t.fields && 'WORKER DIAG EXPECTED' in t.fields);
    add('workers.every_ticket_printed_a_value_and_an_expectation', withBoth.length === tickets.length,
      { tickets: tickets.length, with_both: withBoth.length });
    const disagreeing = withBoth.filter((t) => t.fields['WORKER DIAG VALUE'] !== t.fields['WORKER DIAG EXPECTED']);
    add('workers.every_ticket_value_matches_its_own_expectation', disagreeing.length === 0,
      { disagreeing: disagreeing.length, of: withBoth.length,
        examples: disagreeing.slice(0, 4).map((t) => ({ ticket: t.ticket, value: t.fields['WORKER DIAG VALUE'], expected: t.fields['WORKER DIAG EXPECTED'] })) });
    if (mismatch !== null) add('workers.mismatch_equals_the_disagreeing_tickets', mismatch === disagreeing.length,
      { mismatch, disagreeing: disagreeing.length });
    const ticketNumbers = tickets.map((t) => t.ticket);
    // The ticket numbers are not one increasing sequence, and they are not a per-worker counter restarting at one either.
    // The two-CPU trial prints 1..32 once; the three-CPU trial prints 1..16, then 1..32, then 17..32, which is each of 1..32
    // exactly twice. Two earlier versions of this file were wrong about that: one asserted a single strictly-increasing
    // sequence, the next assumed a restart at one. Both were claims about the guest that the guest does not make, and both
    // failed on the real three-CPU channel.
    //
    // What the guest does maintain, and what is checked here, is that the numbers form a contiguous range and that every
    // number in it appears the same number of times. That is a real relation: one ticket dropped inside a worker's run, or
    // one delivered twice, breaks the uniformity even though every aggregate counter still adds up.
    const counts = new Map();
    for (const n of ticketNumbers) counts.set(n, (counts.get(n) || 0) + 1);
    const distinctNumbers = [...counts.keys()].sort((a, b) => a - b);
    const multiplicities = [...new Set(counts.values())];
    add('workers.the_ticket_numbers_are_contiguous',
      distinctNumbers.length > 0 && distinctNumbers.every((n, i) => n === i + 1),
      { distinct: distinctNumbers.length, first: distinctNumbers[0], last: distinctNumbers[distinctNumbers.length - 1] });
    add('workers.every_ticket_number_appears_the_same_number_of_times', multiplicities.length === 1,
      { multiplicities, per_number: multiplicities.length === 1 ? multiplicities[0] : null });
    add('workers.the_multiplicity_explains_every_submission',
      multiplicities.length === 1 && distinctNumbers.length * multiplicities[0] === submitted,
      { distinct: distinctNumbers.length, multiplicity: multiplicities.length === 1 ? multiplicities[0] : null, submitted });
    add('workers.the_multiplicity_is_the_worker_count',
      multiplicities.length === 1 && configured !== null && multiplicities[0] === configured,
      { multiplicity: multiplicities.length === 1 ? multiplicities[0] : null, configured });
    add('workers.the_ticket_numbers_cover_every_submission', ticketNumbers.length === submitted,
      { tickets: ticketNumbers.length, submitted });
    const slots = tickets.filter((t) => 'WORKER DIAG SLOT' in t.fields).map((t) => t.fields['WORKER DIAG SLOT']);
    add('workers.every_ticket_named_a_slot', slots.length === tickets.length, { tickets: tickets.length, with_slot: slots.length });
    if (slots.length && configured !== null) {
      add('workers.every_slot_is_inside_the_pool', Math.max(...slots) < configured * 16,
        { max_slot: Math.max(...slots), slots_per_worker: 16, configured, capacity: configured * 16 });
    }
    const apic = tickets.filter((t) => 'WORKER DIAG APIC' in t.fields).map((t) => t.fields['WORKER DIAG APIC']);
    add('workers.no_ticket_was_served_by_the_bsp', apic.every((a) => a !== 0), { bsp_served: apic.filter((a) => a === 0).length });
    const pass = tickets.filter((t) => 'WORKER DIAG PASS' in t.fields).map((t) => t.fields['WORKER DIAG PASS']);
    add('workers.a_pass_flag_is_a_flag', pass.every((p) => p === 0 || p === 1), { values: [...new Set(pass)] });
    // A ticket that says it passed must have the numbers to back it: VALUE equal to EXPECTED is what PASS is a summary of.
    const passedButWrong = tickets.filter((t) => t.fields['WORKER DIAG PASS'] === 1
      && t.fields['WORKER DIAG VALUE'] !== t.fields['WORKER DIAG EXPECTED']);
    add('workers.no_ticket_says_it_passed_with_a_wrong_value', passedButWrong.length === 0, { count: passedButWrong.length });
  }

  // ---- family: the runtime object table, in the scene that creates objects.
  if (nums.has('OBJECT DIAG ID A')) {
    const idA = nums.get('OBJECT DIAG ID A');
    const idB = nums.get('OBJECT DIAG ID B');
    addMissing('objects', ['OBJECT DIAG ID A', 'OBJECT DIAG ID B', 'OBJECT DIAG DIR A', 'OBJECT DIAG DIR B',
      'OBJECT DIAG CREATED COUNT', 'OBJECT DIAG FINAL CREATED', 'OBJECT DIAG FINAL EXITED', 'OBJECT DIAG FINAL REAPED',
      'OBJECT DIAG OLD A REAPED', 'OBJECT DIAG OLD A LIVE'].filter((l) => !nums.has(l)));
    add('objects.identifiers_are_distinct', idA !== idB, { a: idA, b: idB });
    add('objects.identifiers_are_not_zero', idA !== 0 && idB !== 0, { a: idA, b: idB });
    const dirA = nums.get('OBJECT DIAG DIR A');
    const dirB = nums.get('OBJECT DIAG DIR B');
    if (dirA !== null && dirB !== null) {
      add('objects.address_spaces_are_distinct', dirA !== dirB, { a: '0x' + dirA.toString(16), b: '0x' + dirB.toString(16) });
      add('objects.address_spaces_are_page_aligned', (dirA & (PAGE - 1)) === 0 && (dirB & (PAGE - 1)) === 0,
        { a_low: dirA & (PAGE - 1), b_low: dirB & (PAGE - 1) });
      add('objects.the_address_spaces_are_not_the_kernels', dirA !== nums.get('OBJECT DIAG CR3') && dirB !== nums.get('OBJECT DIAG CR3'),
        { cr3: nums.get('OBJECT DIAG CR3') === null ? null : '0x' + nums.get('OBJECT DIAG CR3').toString(16) });
    }
    const createdCount = nums.get('OBJECT DIAG CREATED COUNT');
    const finalCreated = nums.get('OBJECT DIAG FINAL CREATED');
    const finalExited = nums.get('OBJECT DIAG FINAL EXITED');
    const finalReaped = nums.get('OBJECT DIAG FINAL REAPED');
    if (createdCount !== null && finalCreated !== null) add('objects.counts_agree_on_how_many_were_created', createdCount === finalCreated,
      { counted: createdCount, final: finalCreated });
    if (finalCreated !== null && finalExited !== null && finalReaped !== null) {
      add('objects.every_object_was_exited_and_reaped', finalCreated === finalExited && finalExited === finalReaped,
        { created: finalCreated, exited: finalExited, reaped: finalReaped });
    }
    const oldReaped = nums.get('OBJECT DIAG OLD A REAPED');
    const oldLive = nums.get('OBJECT DIAG OLD A LIVE');
    if (oldReaped !== null && oldLive !== null) add('objects.a_reaped_object_is_not_live', oldReaped === 1 && oldLive === 0, { reaped: oldReaped, live: oldLive });
    const reapedTwo = nums.get('OBJECT DIAG REAPED');
    if (reapedTwo !== null && finalReaped !== null) add('objects.the_reap_count_is_the_object_count', reapedTwo === finalReaped,
      { reaped: reapedTwo, final: finalReaped });
  }

  // ---- family: the recovery scene, including the refused allocation.
  if (nums.has('RECOVERY DIAG CAPACITY')) {
    const capacity = nums.get('RECOVERY DIAG CAPACITY');
    const fullCreated = nums.get('RECOVERY DIAG FULL CREATED');
    const refusedCreated = nums.get('RECOVERY DIAG REFUSED CREATED');
    addMissing('recovery', ['RECOVERY DIAG CAPACITY', 'RECOVERY DIAG FULL CREATED', 'RECOVERY DIAG REFUSED CREATED',
      'RECOVERY DIAG OVERFLOW ID', 'RECOVERY DIAG NEW ID', 'RECOVERY DIAG SURVIVORS', 'RECOVERY DIAG FINAL CREATED',
      'RECOVERY DIAG BASELINE DIGEST', 'RECOVERY DIAG RESTORED DIGEST'].filter((l) => !nums.has(l)));
    if (capacity !== null && fullCreated !== null) add('recovery.the_table_filled_to_its_capacity', fullCreated === capacity,
      { capacity, created: fullCreated });
    if (fullCreated !== null && refusedCreated !== null) {
      // A refused allocation must create no object. The allocator itself moved on the refusal path, which is why the object
      // count is the number to compare and not the allocator's two counters.
      add('recovery.a_refused_allocation_created_no_object', refusedCreated === fullCreated, { before: fullCreated, after: refusedCreated });
    }
    const overflowId = nums.get('RECOVERY DIAG OVERFLOW ID');
    if (overflowId !== null) add('recovery.a_refused_allocation_returned_no_identifier', overflowId === 0, { id: overflowId });
    const newId = nums.get('RECOVERY DIAG NEW ID');
    const finalCreated = nums.get('RECOVERY DIAG FINAL CREATED');
    if (newId !== null && capacity !== null) add('recovery.the_new_identifier_is_past_the_full_set', newId > capacity, { new_id: newId, capacity });
    if (newId !== null && finalCreated !== null) add('recovery.the_recreated_object_took_the_next_identifier', newId === finalCreated,
      { new_id: newId, created: finalCreated });
    const survivors = nums.get('RECOVERY DIAG SURVIVORS');
    const retired = nums.get('RECOVERY DIAG RETIRED');
    if (survivors !== null && retired !== null && capacity !== null) {
      add('recovery.some_objects_survived', survivors > 0, { survivors });
      // The full set is four; one of them is reaped and recreated, so three of the original four survive. The earlier
      // version of this line added the survivors to the retired count and compared the sum to the capacity, which is not a
      // relation the guest maintains: RETIRED counts every object retired across the whole trial (four from the full set
      // plus the recreated one), so the sum was seven and the claim was simply wrong.
      add('recovery.one_of_the_full_set_was_retired', survivors === capacity - 1, { survivors, capacity });
      if (finalCreated !== null) {
        add('recovery.the_set_grew_by_exactly_one', finalCreated === capacity + 1, { created: finalCreated, capacity });
      }
      add('recovery.every_object_retired_at_the_end', retired === finalCreated || retired >= capacity,
        { retired, created: finalCreated, capacity });
    }
    const finalExited = nums.get('RECOVERY DIAG FINAL EXITED');
    const finalReaped = nums.get('RECOVERY DIAG FINAL REAPED');
    if (finalExited !== null && finalReaped !== null) add('recovery.no_more_reaped_than_exited', finalReaped <= finalExited,
      { exited: finalExited, reaped: finalReaped });
    const filled = nums.all('RECOVERY DIAG FILLED');
    if (filled.length > 1) {
      // The repeated label is a counter: four `FILLED` lines that count up. A counter that went backwards would mean the
      // table lost an entry mid-fill.
      add('recovery.the_fill_counter_counts_up', filled.every((v, i) => i === 0 || v === filled[i - 1] + 1), { filled });
    }
  }

  // ---- the allocator, in whichever scene dumps it.
  for (const p of ['OBJECT DIAG', 'RECOVERY DIAG']) {
    const baseAlloc = nums.get(p + ' BASELINE ALLOCATED');
    const baseFree = nums.get(p + ' BASELINE FREE');
    const afterAlloc = nums.get(p + ' AFTER ALLOCATED') !== null ? nums.get(p + ' AFTER ALLOCATED') : nums.get(p + ' RESTORED ALLOCATED');
    const afterFree = nums.get(p + ' AFTER FREE') !== null ? nums.get(p + ' AFTER FREE') : nums.get(p + ' RESTORED FREE');
    const fam = p === 'OBJECT DIAG' ? 'objects' : 'recovery';
    if (baseAlloc !== null && afterAlloc !== null && baseFree !== null && afterFree !== null) {
      add(fam + '.allocator_returned_to_its_baseline', baseAlloc === afterAlloc && baseFree === afterFree,
        { baseline: { allocated: baseAlloc, free: baseFree }, after: { allocated: afterAlloc, free: afterFree } });
    }
    if (baseAlloc !== null && baseFree !== null && boot.freeFrames !== null) {
      // Conservation, and a cross-subsystem identity: the allocator's two counters add up to the frame count the memory
      // subsystem printed at boot, before any of this ran.
      add(fam + '.allocator_total_is_the_boot_frame_count', baseAlloc + baseFree === boot.freeFrames,
        { allocated: baseAlloc, free: baseFree, sum: baseAlloc + baseFree, boot_free_frames: boot.freeFrames });
    }
    if (afterAlloc !== null && afterFree !== null && boot.freeFrames !== null) {
      add(fam + '.allocator_still_adds_up_afterwards', afterAlloc + afterFree === boot.freeFrames,
        { allocated: afterAlloc, free: afterFree, sum: afterAlloc + afterFree, boot_free_frames: boot.freeFrames });
    }
    const baseDigest = nums.get(p + ' BASELINE DIGEST');
    const restoredDigest = nums.get(p + ' RESTORED DIGEST');
    if (baseDigest !== null && restoredDigest !== null) {
      add(fam + '.the_bitmap_digest_is_the_same_after_restoring', baseDigest === restoredDigest,
        { baseline: '0x' + baseDigest.toString(16), restored: '0x' + restoredDigest.toString(16) });
      add(fam + '.the_bitmap_digest_is_not_the_empty_sum', baseDigest !== 0, { digest: '0x' + baseDigest.toString(16) });
    }
  }

  // ---- the two channels of one run have to agree on every number they share. This is a property of the run: the serial
  // port and the E9 port are two output paths from the same guest, and a number that differs between them was printed from
  // two different states.
  if (other && other.lines.length > 0 && nums.values.size > 0) {
    let shared = 0;
    const disagree = [];
    for (const [label, value] of nums.values) {
      if (!other.has(label)) continue;
      shared += 1;
      const mine = nums.all(label);
      const theirs = other.all(label);
      if (mine.length !== theirs.length || mine.some((v, i) => v !== theirs[i])) {
        disagree.push({ label, serial: mine.slice(0, 4), e9: theirs.slice(0, 4) });
      }
    }
    add('channels.agree_on_every_shared_number', disagree.length === 0, { shared, disagreeing: disagree.slice(0, 5), count: disagree.length });
    add('channels.share_a_substantial_number_of_numbers', shared >= 3, { shared });
  } else if (other && other.lines.length > 0) {
    // A refusal trial prints no numbers, so there is no shared number to compare -- but the two channels still both carried
    // the refusal, and they still have to agree on it. Skipping the comparison entirely would leave the one thing a refusal
    // trial does print unchecked.
    const sharedLines = nums.lines.filter((l) => other.lines.includes(l));
    add('channels.agree_on_the_lines_they_share',
      sharedLines.length > 0 && sharedLines.length >= Math.min(nums.lines.length, other.lines.length),
      { serial_lines: nums.lines.length, e9_lines: other.lines.length, shared: sharedLines.length });
  }

  // ---- a repeated label is either a counter that counts up, or the same value printed twice. A counter that went
  // backwards, or a label printed with two unrelated values, is a finding -- and this is where the per-ticket repeats are
  // deliberately excluded, because those are grouped by ticket above.
  const perTicket = new Set(['WORKER DIAG TICKET', 'WORKER DIAG SLOT', 'WORKER DIAG APIC', 'WORKER DIAG VALUE',
    'WORKER DIAG EXPECTED', 'WORKER DIAG PASS', 'RECOVERY DIAG FILLED']);
  const badRepeats = [];
  for (const [label, list] of nums.repeats) {
    if (list.length < 2 || perTicket.has(label)) continue;
    const allSame = list.every((v) => v === list[0]);
    const monotonic = list.every((v, i) => i === 0 || v >= list[i - 1]);
    if (!allSame && !monotonic) badRepeats.push({ label, values: list.slice(0, 6) });
  }
  add('channel.repeated_labels_are_counters_or_repeats', badRepeats.length === 0, { bad: badRepeats.slice(0, 5) });

  return out;
}

// Which scenes this judge covers, and which of those print no numbers worth judging.
export const NUMERIC_SCENES = Object.freeze([
  'runtime-init-smp1', 'runtime-init-smp2', 'runtime-init-smp3',
  'runtime-objects-smp1', 'runtime-objects-smp2', 'runtime-objects-smp3',
  'runtime-recovery-smp1', 'runtime-recovery-smp2', 'runtime-recovery-smp3',
  'workers-smp1', 'workers-smp2', 'workers-smp3',
  'paging-smp1', 'ap-fresh-smp2',
]);
// These boot a kernel and stop: the guest prints a checkpoint or a refusal and no numbers beyond the boot line. They are
// judged by their markers in the 144 record, and this judge says so rather than padding them with vacuous obligations.
export const MARKER_ONLY_SCENES = Object.freeze(['tables-smp1', 'e99-priority', 'kernel-invalid']);

export function judgeNonResource(scene, serialText, e9Text) {
  const serial = parseNumbers(serialText);
  const e9 = parseNumbers(e9Text);
  const boot = { detected: serial.get('CPU DETECTED'), online: serial.get('CPU ONLINE'), freeFrames: serial.get('MEMORY FREE FRAMES') };
  const obligations = judgeScene(scene, serial, e9, boot);
  const failed = obligations.filter((o) => !o.value).map((o) => o.obligation);
  return {
    scene,
    obligations,
    obligation_count: obligations.length,
    failed,
    ok: failed.length === 0 && obligations.length > 0,
    numbers_read: serial.values.size,
    repeated_labels: [...serial.repeats].filter(([, v]) => v.length > 1).length,
    channels_compared: e9.lines.length > 0,
  };
}

// ---------------------------------------------------------------- mutations
//
// Each mutation changes ONE printed number in a real channel and names the obligation that has to notice. A mutation whose
// target still passes means the obligation is decorative, and this packet would rather find that out here.
export const MUTATIONS = Object.freeze([
  { name: 'cr0-paging-cleared', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG CR0', value: 0x00010011,
    targets: 'cr0.has_paging_and_protection' },
  { name: 'cr0-caching-disabled', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG CR0', value: 0xE0010011,
    targets: 'cr0.has_caching_enabled' },
  { name: 'cr3-unaligned', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG CR3', value: 0x00A28001,
    targets: 'cr3.is_page_aligned' },
  { name: 'cr3-not-the-directory', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG CR3', value: 0x00A29000,
    targets: 'cr3.is_the_page_directory' },
  { name: 'tr-selector-does-not-decompose', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TR SELECTOR', value: 0x00000038,
    targets: 'runtime.tr.selector_decomposes_into_its_parts' },
  { name: 'tr-index-past-the-gdt', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TR INDEX', value: 0x000000FF,
    targets: 'runtime.tr.index_is_inside_the_gdt' },
  { name: 'tss-type-is-not-busy-32', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TSS DESC TYPE', value: 0x00000089,
    targets: 'runtime.tss.descriptor_is_a_busy_32bit_tss' },
  { name: 'tss-base-differs-from-expected', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TSS DESC BASE', value: 0x00597E00,
    targets: 'runtime.tss.descriptor_base_is_the_expected_tss' },
  { name: 'tss-ss0-is-a-user-selector', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TSS SS0', value: 0x0000001B,
    targets: 'runtime.tss.ss0_is_a_ring0_data_selector' },
  { name: 'tss-esp0-is-unaligned', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TSS ESP0', value: 0xD0005004,
    targets: 'runtime.tss.esp0_is_a_usable_stack' },
  { name: 'tss-iomap-inside-the-limit', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TSS IOMAP', value: 0x00000010,
    targets: 'runtime.tss.iomap_base_is_past_the_limit' },
  { name: 'tss-guest-check-found-a-failure', scene: 'runtime-init-smp1', trial: 1, label: 'RUNTIME DIAG TSS OBLIGATIONS FAILED', value: 0x00000001,
    targets: 'runtime.tss.the_guest_checked_itself_and_found_nothing' },
  { name: 'paging-canaries-are-the-same', scene: 'paging-smp1', trial: 1, label: 'PAGING DIAG CANARY B', value: 0x5A5AA5A5,
    targets: 'paging.the_two_canaries_differ' },
  { name: 'paging-virtual-a-is-not-canary-a', scene: 'paging-smp1', trial: 1, label: 'PAGING DIAG VIRTUAL A', value: 0x00000000,
    targets: 'paging.virtual_a_reads_canary_a' },
  { name: 'paging-phys-a-is-unaligned', scene: 'paging-smp1', trial: 1, label: 'PAGING DIAG PHYS A', value: 0x00A49004,
    targets: 'paging.phys_a_is_page_aligned' },
  { name: 'paging-the-two-pages-are-one', scene: 'paging-smp1', trial: 1, label: 'PAGING DIAG PHYS B', value: 0x00A49000,
    targets: 'paging.the_two_pages_are_different' },
  { name: 'paging-tables-exceed-pages', scene: 'paging-smp1', trial: 1, label: 'PAGING DIAG TABLES', value: 0x0000FFFF,
    targets: 'paging.tables_are_fewer_than_pages' },
  { name: 'ap-state-loses-a-bit', scene: 'ap-fresh-smp2', trial: 1, label: 'AP DIAG STATE', value: 0x00000005,
    targets: 'ap.state_is_fully_set' },
  { name: 'ap-state-has-an-unknown-bit', scene: 'ap-fresh-smp2', trial: 1, label: 'AP DIAG STATE', value: 0x0000000F,
    targets: 'ap.state_is_a_known_bitmask' },
  { name: 'ap-stack-is-unaligned', scene: 'ap-fresh-smp2', trial: 1, label: 'AP DIAG STACK', value: 0x00A31FE4,
    targets: 'ap.stack_is_aligned' },
  { name: 'ap-stale-ticket-was-acknowledged', scene: 'ap-fresh-smp2', trial: 1, label: 'AP DIAG STALE ACK', value: 0x00000001,
    targets: 'ap.a_stale_ticket_is_refused' },
  { name: 'ap-checksum-is-empty', scene: 'ap-fresh-smp2', trial: 1, label: 'AP DIAG CHECKSUM', value: 0x00000000,
    targets: 'ap.checksum_is_not_the_empty_sum' },
  { name: 'ap-parked-count-is-wrong', scene: 'ap-fresh-smp2', trial: 1, label: 'AP DIAG PARKED', value: 0x00000002,
    targets: 'ap.parked_is_detected_minus_online' },
  { name: 'workers-count-work-on-one-cpu', scene: 'workers-smp1', trial: 1, label: 'WORKER DIAG SUBMITTED', value: 0x00000004,
    targets: 'workers.single_cpu_means_no_work' },
  { name: 'workers-pool-on-one-cpu', scene: 'workers-smp1', trial: 1, label: 'WORKER DIAG CONFIGURED', value: 0x00000001,
    targets: 'workers.single_cpu_means_no_workers' },
  { name: 'workers-a-submission-was-lost', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG COLLECTED', value: 0x0000001F,
    targets: 'workers.every_submission_was_collected' },
  { name: 'workers-a-ticket-mismatched', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG MISMATCH', value: 0x00000001,
    targets: 'workers.mismatch_equals_the_disagreeing_tickets' },
  { name: 'workers-the-bsp-did-the-work', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG BSP EXECUTED', value: 0x00000020,
    targets: 'workers.the_bsp_did_none_of_the_work' },
  { name: 'workers-the-aps-did-not-do-the-work', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG AP EXECUTED', value: 0x00000000,
    targets: 'workers.the_aps_did_all_of_the_work' },
  { name: 'workers-reuse-does-not-add-up', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG REUSED SLOTS', value: 0x00000011,
    targets: 'workers.reused_slots_is_the_overflow' },
  { name: 'workers-a-park-went-unacknowledged', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG PARK ACKED WORKERS', value: 0x00000000,
    targets: 'workers.every_park_request_was_acknowledged' },
  { name: 'workers-tickets-are-not-distinct', scene: 'workers-smp2', trial: 1, label: 'WORKER DIAG DISTINCT TICKETS', value: 0x0000001F,
    targets: 'workers.every_ticket_is_distinct' },
  { name: 'objects-share-an-identifier', scene: 'runtime-objects-smp1', trial: 1, label: 'OBJECT DIAG ID B', value: 0x00000001,
    targets: 'objects.identifiers_are_distinct' },
  { name: 'objects-share-an-address-space', scene: 'runtime-objects-smp1', trial: 1, label: 'OBJECT DIAG DIR B', value: 0x00A5A000,
    targets: 'objects.address_spaces_are_distinct' },
  { name: 'objects-allocator-does-not-add-up', scene: 'runtime-objects-smp1', trial: 1, label: 'OBJECT DIAG AFTER FREE', value: 0x00007587,
    targets: 'objects.allocator_still_adds_up_afterwards' },
  { name: 'objects-not-every-object-was-reaped', scene: 'runtime-objects-smp1', trial: 1, label: 'OBJECT DIAG FINAL REAPED', value: 0x00000001,
    targets: 'objects.every_object_was_exited_and_reaped' },
  { name: 'objects-a-reaped-object-is-still-live', scene: 'runtime-objects-smp1', trial: 1, label: 'OBJECT DIAG OLD A LIVE', value: 0x00000001,
    targets: 'objects.a_reaped_object_is_not_live' },
  { name: 'recovery-bitmap-digest-changed', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG RESTORED DIGEST', value: 0xB75BD3A9,
    targets: 'recovery.the_bitmap_digest_is_the_same_after_restoring' },
  { name: 'recovery-refusal-created-an-object', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG REFUSED CREATED', value: 0x00000005,
    targets: 'recovery.a_refused_allocation_created_no_object' },
  { name: 'recovery-refusal-returned-an-identifier', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG OVERFLOW ID', value: 0x00000006,
    targets: 'recovery.a_refused_allocation_returned_no_identifier' },
  { name: 'recovery-reused-an-identifier', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG NEW ID', value: 0x00000002,
    targets: 'recovery.the_new_identifier_is_past_the_full_set' },
  { name: 'recovery-allocator-did-not-add-up', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG RESTORED FREE', value: 0x00007587,
    targets: 'recovery.allocator_still_adds_up_afterwards' },
  { name: 'recovery-table-did-not-fill', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG FULL CREATED', value: 0x00000003,
    targets: 'recovery.the_table_filled_to_its_capacity' },
  { name: 'recovery-nobody-survived', scene: 'runtime-recovery-smp1', trial: 1, label: 'RECOVERY DIAG SURVIVORS', value: 0x00000000,
    targets: 'recovery.some_objects_survived' },
  { name: 'refusal-claims-two-cpus-online', scene: 'runtime-init-smp2', trial: 1, label: 'CPU ONLINE', value: 0x00000002,
    targets: 'runtime-refusal.exactly_one_cpu_is_online' },
  { name: 'refusal-detected-count-disagrees', scene: 'runtime-init-smp2', trial: 1, label: 'RUNTIME DIAG DETECTED', value: 0x00000003,
    targets: 'runtime-refusal.detected_agrees_with_the_cpu_count' },
  { name: 'refusal-detected-only-one', scene: 'runtime-objects-smp2', trial: 1, label: 'CPU DETECTED', value: 0x00000001,
    targets: 'objects-refusal.detected_is_more_than_one' },
]);

// Apply a mutation to a channel text: the first line whose label matches gets the new value. The value is coerced with
// `>>> 0` because a bitwise OR in JavaScript yields a signed 32-bit integer, and `0x80010011 | 0x60000000` would otherwise
// format as a negative number and silently stop parsing -- which is exactly how the first version of this test produced a
// mutation that changed nothing.
export function applyMutation(text, label, value) {
  const v = (value >>> 0);
  const hex = v.toString(16).toUpperCase().padStart(8, '0');
  const lines = String(text).split('\n');
  let done = false;
  const out = lines.map((line) => {
    if (done) return line;
    const m = /^(.*?)\s+([0-9A-Fa-f]{8})\s*$/.exec(line.trim());
    if (!m || m[1].trim() !== label) return line;
    done = true;
    return label + ' ' + hex;
  });
  return { text: out.join('\n'), applied: done, wrote: hex };
}

export { sha256 };
