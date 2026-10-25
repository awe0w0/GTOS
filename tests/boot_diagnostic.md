# Boot diagnostic checkpoints

The private diagnostic stops exist so a single cold boot can be observed at one chosen point, without any service
running. They are selected by whole space-delimited tokens on the kernel command line. Nothing here is a public
interface: a build that carries no such token behaves exactly as it did before.

## The recognised options

| Option | Halts |
|---|---|
| `bootdiag-stop=memory` | after the real physical self-test and the heap self-test with validation both succeed |
| `bootdiag-stop=tables` | after the GDT and IDT are constructed and loaded |
| `bootdiag-stop=ap` | after the AP handshake diagnostic, before any worker or service |
| `bootdiag-stop=paging` | after real bootstrap paging is enabled and the owned mapping probe passes |

The legacy `bootlog-fail` token keeps its E99 precedence. The kernel checks it first, so a command line carrying both
never reaches a checkpoint.

## Refusals

Anything that carries the option prefix but is not one of those four values is refused before any self-test:

| Reason | Meaning |
|---|---|
| `empty-value` | the prefix with nothing after the `=` |
| `unknown-value` | a value that is not one of the four, case-sensitively and exactly |
| `duplicate` | a second diagnostic option, even one repeating the same value |
| `unterminated` | the option reaches the end of the readable span with no space and no NUL |
| `oversize-span` | the caller declared more readable bytes than the kernel's own 256 byte bound |

A refusal prints `BOOTDIAG REJECT reason=<name>` and then `BOOTDIAG REJECT invalid-option`, and halts. It never falls
through into late startup.

## The span contract

`BootDiagnostic::Parse(span, capacity)` treats `capacity` as the number of readable bytes the caller guarantees, not as
an upper bound the helper may quietly reduce.

- a null span, or `capacity == 0`, means "no command line" and yields `StopNone`
- `capacity` above 256 is a refusal, because a caller that declares more than the kernel's own bound has handed over a
  line this helper may not interpret. There is no implicit clamp: a clamp would let a valid early token hide a later
  duplicate or illegal option that sat beyond the shortened window
- the scan stops at the first NUL, or at the end of the span when no NUL appears

An ordinary token that runs to the end of the span is harmless, because it is not our option. Only a token carrying
the option prefix has to be properly terminated.

## Phases

A stop belongs to a phase, and the order is the real boot order, so a later checkpoint strictly dominates an earlier
one:

    command line  ->  memory  ->  tables  ->  ap  ->  paging

`StopReject` belongs to the command-line phase.

## The AP checkpoint

`bootdiag-stop=ap` builds its own `CpuStartup` instance, so the default instance later in the boot is untouched and
the default flow is unchanged.

**Single CPU.** `Prepare` reports one detected processor. No AP is attempted and no acknowledgement is invented: the
log prints `AP DIAG SINGLE CPU` with attempted, acknowledged and parked all zero, and then
`BOOTDIAG AP SINGLE CPU NO AP STARTED`. This is the honest degrade, not a failure dressed as success.

**Two CPUs.** `Prepare` validates the firmware inventory, the APIC base against the executing BSP's own MSR, and the
local APIC identity. `StartPrepared` is then called with a null continuation, which means every AP runs the real entry,
publishes its own identity and stack pointer, passes its integer and control-register self-test, and parks with
interrupts disabled. It never enters the work pool, a scheduler, an allocator or a driver.

`Prepare` resets each record's observation to `0xFFFFFFFF` before any AP is started, so a published value equal to the
record's own APIC id can only have come from this run. The log reports, per AP, its APIC id, the observed id, its
state, its observed stack pointer and its self-test checksum, and then the aggregate counts plus
`AP DIAG FRESH ACK` and `AP DIAG STALE ACK`.

The checkpoint marker requires all of: a started handshake, at least one fresh non-BSP acknowledgement, at least one
verified parked AP, no stale or foreign observation, and no failed AP. Anything less prints
`BOOTDIAG AP CHECKPOINT NOT REACHED` instead, so a marker can never be produced by a partial result.

## The paging checkpoint

`bootdiag-stop=paging` is single-CPU only and runs before any AP is started, so no cross-core TLB question arises. It
calls the same `KernelPaging` implementation the default B06 path uses, through the same `BuildPagingConfig` helper, so
the diagnostic cannot diverge from production.

It allocates two owned frames and maps each at its own virtual page above the identity-mapped RAM, so the probe cannot
collide with the boot identity range and a fresh page table must be allocated to hold it. It then enables paging and
reads back the real state:

- `CR0` must show both PG and WP
- `CR3` must equal the directory address the paging object reports
- each virtual page must read back the pattern written through it
- each physical identity address must read back the same pattern
- the two frames must be distinct
- both mappings must be marked managed
- address zero must remain unmapped

The marker `BOOTDIAG PAGING ENABLED OWNED MAP` requires the enabled state, the CR0 bits, the CR3 match and every probe
to hold. A successful call to `enable()` is not enough.

## What a checkpoint does not prove

A reached checkpoint proves the kernel got that far with interrupts disabled and no service running. It does not prove
a boot. In particular, paging being enabled and its tables being loaded are separate facts from interrupts being
enabled: the only global `sti` in this kernel sits after B07, which no checkpoint reaches.

## Host unit

`tests/boot_diagnostic_test.cpp` links the real helper body and runs it over literal spans, including the exact
tokens, the similar prefix and suffix cases, the duplicate and unterminated cases, the 256 byte boundary, marker
distinctness by content, phase ordering, and the value names. It asserts nothing about the kernel's own call sites;
those are observed on the guest.
