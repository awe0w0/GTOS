# Bounded multicore kernel workers

The work pool executes real bounded integer jobs on application processors (APs)
using a shared, sealed page directory and hardware wake IPIs. It is separate from
the BSP task scheduler. On a four-vCPU machine, three AP workers can execute jobs
while the BSP continues its PIT interrupts and desktop loop. **General scheduler
online CPUs remains one.** Worker-ready, busy, faulted and completed-job counts
must be displayed separately.

## Scope

Each AP has a private stack, private IDT, local-APIC interrupt context and one
16-slot single-producer/single-consumer queue. The BSP submits and collects jobs,
serializing these operations with local IRQ masking. The AP consumes its queue.
Only two built-in integer operations are accepted, with 1–65,536 iterations:
integer hash and modular sum. Requests contain values, not pointers or callbacks.

AP jobs do not use the global heap/physical allocator, device drivers, app VM,
BSP scheduler, floating point or SIMD. There is no general AP task dispatch,
preemption, migration, hotplug or cross-CPU allocator safety in this milestone.
The jobs are bounded trusted kernel work, not arbitrary application execution.
See [the queue contract](cpu-work-queue.md) for formulas and ticket semantics.

## Boot integration and fallback

The existing `CpuStartup::Start(...)` remains compatible and parks APs. Its
optional two-phase form allows shared mappings to be prepared after all startup
resources have been allocated:

```cpp
CpuStartup startup;
startup.Prepare(cpu.GetInfo(), frames, firmwareBound);
CpuWorkPool workers;
workers.Prepare(startup, frames);
// Build a PagingConfig including the actual LAPIC page and required devices.
paging.prepareIdentity(frames, config);
workers.Start(paging);
paging.enable();
```

Check every return value and surface its error. Prepare and Start require the
BSP, interrupts disabled and paging still off. `Prepare` claims/allocates AP
startup resources, then work-pool contexts. `prepareIdentity` maps those pages,
the claimed trampoline/GDT, kernel code/rodata, stacks and explicitly authorized
LAPIC MMIO. `workers.Start` validates the complete read-only executable range and
writable shared ranges, rejects incompatible CR4.PAE, and seals the directory
before sharing it with any AP. With zero worker candidates it does not impose an
unnecessary shared-directory seal.

`CpuStartup::StartPrepared` accepts an optional trusted, non-returning kernel
continuation. The default still parks APs. A continuation is a boot mechanism,
not a public work-item callback. The AP must pass its identity/stack/checksum
self-test before handoff. Startup reports distinguish initialized/handed-off APs
from parked APs; handoff alone does not establish worker readiness.

Each worker then verifies its own APIC configuration, memory types and mapping
activation, installs its private IDT, enables its AP-local interrupt reception,
and publishes readiness only after loading the expected CR3 with CR0.PG/WP set.
The BSP can enable the same directory afterward. If worker preparation fails
before handoff, the caller can retain the existing parked-AP path. If handoff
fails, keep all startup/worker pages allocated; healthy workers may remain usable
and failed workers are explicitly reported. Never retry INIT/SIPI or free these
pages while an AP could still arrive or access them.

Mapping permissions and translations are immutable once shared. The paging
object rejects map/unmap/protect/abandon after sealing. TLB shootdown and parallel
address-space mutation are not implemented. Ordinary frame/heap allocators are
still BSP-only, even though their existing RAM mappings are shared.

## Cache and firmware compatibility

INIT can leave AP caches disabled. The pool captures BSP memory-type state before
paging and compares every AP's supported MTRR/PAT configuration against it,
including feature support, physical-address width, capabilities, default type,
variable ranges, supported fixed ranges and PAT entries. The paging layer's PAT
slot 0 must be write-back and slot 3 must be strong uncacheable. Required shared
RAM, executable code, startup records, GDT/stacks and page-directory/table frames
are checked for effective write-back type across every touched page, including
MTRR fixed/default/overlap behavior. Equal-but-write-combining memory types are
not accepted for the lock-free queues. Missing optional
features are supported only when both CPUs match. Unsupported capabilities,
more than 32 variable MTRRs, or inconsistent values fail closed.

After a match, the AP normalizes CR0 to CD=1/NW=0, executes WBINVD and clears CD/NW
before enabling shared paging. No MTRR, PAT or APIC-base MSRs are rewritten.
Each AP independently checks enabled xAPIC mode, its physical APIC base and
hardware APIC identity. x2APIC remains unsupported by this transport.

This check exposes a real current limitation: QEMU's `-cpu max` boot in the tested
SeaBIOS setup leaves AP MTRRs zero while the BSP has programmed ranges. Such APs
are deliberately rejected as workers, rather than enabling caches under
inconsistent memory types. The default `qemu32` model passes; sparse APIC-ID
coverage uses that model. Supporting mismatched firmware requires a separately
reviewed, synchronized memory-type initialization protocol. These tests do not
claim arbitrary physical-machine support.

## Submitting and collecting

```cpp
CpuWorkTicket ticket;
CpuWorkSubmitStatus accepted = workers.Submit(
    apicId, WorkRequest(WorkIntegerHash, 4096, seed), ticket);
WorkResult result;
CpuWorkCollectStatus state = workers.Collect(ticket, result);
```

- `CpuWorkAccepted` means the bounded job is queued and the wake IPI was delivered
- `CpuWorkAcceptedWakeFailed` also means the job is queued and the ticket is live.
  Do not submit a duplicate. Retry `Kick(apicId)` or check its existing ticket
- Invalid, full, exhausted, offline and wrong-context submissions leave an invalid
  ticket. The BSP is the only authorized producer/collector
- `CpuWorkPending` requires a later check; `CpuWorkComplete` provides the result
  and actual executing hardware APIC ID, then retires the ticket
- `CpuWorkFailed` means the target worker faulted while that accepted job was
  still pending. Its output is unavailable and its storage is retained. Restart
  or reclamation of faulted workers is not supported
- Tickets include their worker index so collection is routed to the originating
  queue. Successful collection makes copied/stale tickets invalid

The AP idle path disables interrupts, checks the queue, then executes STI/HLT as
one interrupt-shadow-safe sequence. A producer publishes work before sending its
IPI. IPIs arriving during a job are acknowledged; the next loop drains the queue.
This avoids the enqueue-between-check-and-sleep lost-wakeup race. The worker does
not need an AP timer for these bounded jobs.

## Fault handling and counters

AP wake and spurious interrupts have private handlers, correct i386 call-stack
alignment and per-frame exception information. They never use BSP interrupt
manager globals. Unexpected AP exceptions publish the failure/vector/error/EIP
(and CR2 for a page fault), then halt that AP with interrupts disabled. Further
submissions to it fail; other APs and BSP interrupts continue. The normal C++
fault handler needs a valid AP stack and mapped kernel handler code; this is not
a double-fault recovery or stack-guard implementation.

Reports distinguish configured, ready, busy and faulted workers. Readiness also
checks the observed CR3 and CR0.PG/WP/CD/NW values. Queue counters are independently
atomic snapshots, not a transactionally consistent performance sample.
Completed-job counts indicate finished built-in jobs, not time-sliced processes
or a measure of host physical-core utilization.

## Reproducible verification

The queue suite exercises FIFO/capacity/backpressure, invalid requests, stale
and out-of-order tickets, sequence exhaustion, 100,000 modeled transitions and
16,384 concurrent shared-memory submissions/completions at both O0 and O2.
The memory-type suite performs 970 mock assertions including 60 register-half
mismatches, plus strict compilation of the unchanged production helper.

```sh
./tests/cpu_work_queue_test.sh
python3 tests/cpu_memory_types_test.py
./tests/cpu_work_pool_smoke.sh
```

The pool smoke suite builds separate real Multiboot kernels at O0 and O2. It tests
1/2/4/8 vCPUs, sparse APIC IDs, optional MTRR/PAT absence, bad-checksum handoff
rejection and `-cpu max` compatibility. The latter accepts a fully working compatible
configuration or an explicitly diagnosed, fail-closed MTRR mismatch; it does not
require future QEMU firmware to retain today's mismatch. Successful worker
cases verify shared CR3, PG/WP, enabled caches, real IPI counters, exact job
results/APIC identity, queue-full behavior, ticket retirement, 64 repeated
idle/wakeup rounds and uninterrupted BSP PIT progress. Preparation and Start are
also retried after reversible admission failures.

A test-only AP hold and null-write injection verifies a real AP page fault,
failure of 16 pending tickets, offline admission rejection, and continued
computation on a healthy AP. Test hooks are absent from production builds.
Existing AP parked-path and BSP scheduler suites must continue to pass alongside
these tests. Integrated desktop acceptance separately validates the live desktop.

## References

- [Intel SDM Volume 3A](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf), cache control, multiprocessor memory-type consistency, APIC/IPI behavior and paging
- [ACPI processor inventory specification](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html)

## Known emulator interrupt limitation

An explicit `-cpu qemu32,apic=off -smp 4` desktop configuration reaches the
GUI but does not advance PIT-driven tasks or process input in the tested QEMU
10.0.13 setup. This was reproduced on both the integrated worker checkpoint
and the preceding Chinese desktop commit `24a5a881cbb3eb0e9069c573e7ea58e344d4e4fc`.
IRQ diagnostics showed generated PIT/keyboard interrupts while the BSP remained
halted with IF set. This is a pre-existing configuration limitation; its exact
firmware/emulator routing cause has not been established.

The no-APIC **uniprocessor** configuration does remain responsive. For multiple
virtual CPUs, retain the normal local APIC. Early startup rejection tests prove
that no AP is started when APIC support is absent; they do not imply that every
APIC-disabled SMP emulator configuration has usable legacy interrupt delivery.
