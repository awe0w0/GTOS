# Bounded per-AP integer work queue

`CpuWorkQueue` is a freestanding, fixed-storage SPSC queue in
`gtos::hardwarecommunication`. One BSP context submits requests and collects
results; one application processor executes them. It allocates no memory, uses
no libc or libatomic, and contains no pointer-bearing jobs, callbacks, device
I/O, floating point or SIMD. The queue does not start CPUs, send IPIs, mask
interrupts or manipulate paging. Those remain the caller's responsibility.

## Public contract

- Construct a queue once before publishing its address to the consumer. The queue
  cannot be copied or reset. Do not destroy or reconstruct its storage while an
  AP or ticket can still refer to it.
- The BSP must serialize all `Submit` and `Collect` calls, including calls from
  interrupt handlers. Disabling and restoring local interrupts is sufficient
  only if the BSP is their sole caller. Only one AP may call `ExecuteOne`, and
  its interrupt handlers must not reenter that function.
- `WorkRequest(kind, iterations, seed)` accepts `WorkIntegerHash` or
  `WorkModularSum`, with 1 through 65,536 iterations inclusive. The default
  request has zero iterations and is intentionally invalid.
- `Submit(request, ticket)` returns `WorkAccepted`, `WorkFull`, `WorkInvalid`
  or `WorkExhausted`. Every unsuccessful submission overwrites the output with
  an invalid ticket. The queue has exactly 16 slots. A completed slot remains
  occupied until its result is collected.
- `WorkTicket(slot, id)` identifies a job in its originating queue. A default
  ticket is invalid. `IsValid()` checks its shape, not whether that job still
  exists. Tickets do not identify their queue, so a caller managing several APs
  must route each ticket back to the same queue. A copied live ticket can collect
  its result, but all copies become stale as soon as one collection succeeds.
- `ExecuteOne(actualApicId)` returns `true` if it executed the next FIFO request,
  otherwise `false`. Pass the executing processor's actual hardware APIC ID;
  the queue has no way to discover or verify a supplied identity. All 32 bits
  are preserved in `WorkResult.executingApicId`.
- `Collect(ticket, result)` returns `WorkTicketInvalid`, `WorkPending` or
  `WorkComplete`. Only `WorkComplete` changes the output or releases the slot.
  Collection may be out of order. Previously collected, wrong-generation,
  zero-ID and out-of-range tickets are invalid. No error path dereferences an
  out-of-range slot.
- Ring admission is deliberately conservative. If the next producer slot is
  occupied, `Submit` returns `WorkFull` even if later slots have been collected.
  This avoids a secondary free-list and preserves FIFO execution.
- Ticket IDs start at one and never repeat in one queue lifetime. The last
  accepted ID is `UINT32_MAX`. All subsequent valid requests permanently return
  `WorkExhausted`; existing jobs can still execute and be collected. Invalid
  requests return `WorkInvalid` even after exhaustion.
- `GetStats()` returns independently atomic `submitted`, `completed`,
  `collected` and `rejected` counters. It is not a coherent multi-counter
  snapshot while operations are concurrent. The first three cannot overflow
  because accepted IDs are bounded. Rejected submissions saturate at
  `UINT32_MAX`. Invalid/pending collections do not increment `rejected`.

## Work definitions

All arithmetic is unsigned 32-bit arithmetic, reduced modulo 2^32. These are
bounded deterministic demonstration workloads, not cryptographic primitives.

For the integer hash, set `value = seed`. For each `i` from zero through
`iterations - 1`:

```
value = (value XOR (i + 0x9E3779B9)) * 16777619
value = value XOR (value >> 13)
```

For the modular sum, set `value = seed` and add each integer from one through
`iterations`. Its result is `seed + iterations * (iterations + 1) / 2`, reduced
modulo 2^32; the implementation performs the bounded loop rather than using
64-bit division. For example, seed `0xFFFFFFFF` and 65,536 iterations produces
`0x80007FFF`.

Known integer hash vectors for seed `0x12345678` are `0x2A057CF2` after one
iteration, `0xF2B9C5AA` after two and `0x279BE802` after sixteen.

## Publication and sleep safety

Every shared state or counter access uses a GCC `__atomic` operation on an
aligned 32-bit word. Compilation asserts that 32-bit atomics are always lock
free. Ordinary request/result fields are protected by state publication:

1. The BSP writes the request and ticket ID, then release-stores `SlotPending`.
2. The AP acquire-loads `SlotPending` before reading the request.
3. The AP writes the value and observed APIC ID, then release-stores
   `SlotComplete`.
4. The BSP acquire-loads `SlotComplete` before reading the result, then
   release-stores `SlotEmpty` after copying it to the caller.
5. The next submission checks `SlotEmpty` before reusing that slot.

The AP never changes ticket IDs. Only the BSP reads or changes them, under the
required Submit/Collect serialization. Producer and consumer indexes advance
only when their respective operations succeed; their modulo-16 arithmetic does
not affect generation IDs.

`HasPending()` acquire-checks the next consumer slot. It does not reserve work
and is not a substitute for interrupt-safe sleep. The AP worker must pair this
check with its own lost-wakeup-safe interrupt protocol, for example checking
with interrupts disabled and using adjacent `sti; hlt` when empty. The BSP
must publish an accepted request before sending its wakeup IPI. Queue storage
must remain mapped and coherent on both CPUs. Calls from other CPUs may use
`HasPending()` only as a momentary hint because the consumer index can advance
concurrently.

## Verification

Run `./tests/cpu_work_queue_test.sh`; set `GTOS_QEMU_I386` to an explicit
qemu-i386 path when it is not installed on PATH. Optional
`GTOS_CPU_WORK_QUEUE_INCLUDE` and `GTOS_CPU_WORK_QUEUE_SOURCE` overrides allow
testing a staged header/source outside the production tree. The script compiles and runs
the actual queue implementation at both `-O0` and `-O2`, in statically linked
freestanding i386 Linux binaries with no libc. It also compiles the public
header before and after existing CPU/scheduler headers at both optimization
levels.

Coverage includes empty behavior, all rejection paths, output preservation,
FIFO execution and APIC identity, capacity and conservative backpressure,
out-of-order collection, copied and stale tickets, reuse, unsigned overflow,
known hash vectors, both iteration boundaries and sequence exhaustion.
A white-box fixture reaches the otherwise impractical sequence/counter limits.
An independent model checks 100,000 randomized transitions. Finally, a
shared anonymous mapping and Linux `fork` exercise 16,384 concurrent producer /
consumer jobs, collecting batches in reverse order and verifying every result.

The Linux concurrent test checks real queue publication under separate host
processes. It is not a substitute for booting the kernel on multiple virtual
CPUs to test APIC delivery, AP paging/IDTs, execution identity and idle wakeups.
