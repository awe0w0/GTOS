# CPU discovery and BSP task management

## Implemented

GTOS discovers the processor vendor, brand, architectural feature bits and the
boot processor's CPUID package topology. It enumerates **firmware-enabled logical
processors** using a checksummed ACPI MADT, with a checksummed Intel MP table as a
fallback. ACPI processor-local-APIC and x2APIC records are supported; disabled and
hotplug-only processors do not count, and duplicate APIC IDs count once.

`CpuManager::DetectedLogicalProcessors()` is the firmware inventory count.
`CpuManager::OnlineProcessors()` is **1**, because this milestone executes the
scheduler only on the bootstrap processor (BSP). Enumeration does not initialize
application processors (APs). The GUI and diagnostics must keep these separate.
When firmware enumeration is unavailable or invalid, the conservative inventory
is the one known-running BSP; CPUID capacity is never substituted for a system
processor count. Firmware tables with more than 256 distinct enabled IDs are
currently rejected, rather than silently reporting a truncated count.

CPUID leaf `0x1F`, falling back to `0x0B`, supplies topology levels when valid.
`logicalPerPackage`, `coresPerPackage` and `threadsPerCore` describe the BSP's
reported package capacity, not necessarily populated system-wide totals. On
Intel processors, leaf 4 is a fallback for package core capacity. The physical
core inventory is derived by grouping firmware APIC IDs only when a valid SMT ID
shift is available; otherwise `detectedPhysicalCores == 0` means unknown.
Legacy/default virtual CPU models may expose logical CPUs without exposing enough
topology to identify physical cores. A virtual CPU is not proof of a dedicated
host physical core.

### Safe firmware access

`Detect(accessiblePhysicalBytes)` is for the identity-mapped, pre-paging boot
phase. The bound must cover the physical firmware tables and must be accessible
through the active segment/mapping. Every physical table dereference is bounded;
lengths, checksums and entry boundaries are validated before enumeration. ACPI
pointers above 4 GiB are skipped by this 32-bit implementation. The caller should
not floor `mem_upper` to whole MiB before choosing the bound: ACPI tables can live
near the end of RAM, above the reported usable RAM portion. BIOS-discovered ACPI
RSDP and MP floating pointers are searched in their specified low-memory regions.

`DiscoverFromMadt(pointer, bytes)` and `DiscoverFromMpTable(pointer, bytes)` are
bounded, allocation-free parsers. They can also consume tables supplied by a
future bootloader/memory mapper. A failed parse leaves the previous inventory
unchanged. The firmware source and its physical address are exposed separately.
The parser does not enable APIC hardware or touch the interrupt routing model.

## Scheduler behavior

`TaskManager` implements a uniprocessor round-robin scheduler. The pre-existing
kernel main loop is retained as an always-runnable boot slot. For two runnable
tasks, dispatch order is task A, task B, boot context, task A, and so on. Thus,
adding a background task cannot permanently replace the desktop main loop. With
zero tasks or all tasks asleep/terminated, scheduling returns the saved boot
context safely.

The scheduler runs once per PIT interrupt. `Ticks()` records delivered timer IRQs;
these are not milliseconds and do not count interrupts missed while IF is clear.
At a configured 100 Hz, 100 ticks is approximately one second. Every task exposes
scheduled `runTicks`, `dispatches`, `yields` and `sleeps`; these are scheduler
accounting, not performance-counter or CPU-busy-time measurements.

Task states are:

- `TaskNew`: constructed but not admitted
- `TaskReady`: eligible for dispatch
- `TaskRunning`: selected on the BSP
- `TaskSleeping`: ineligible until its deadline or explicit wake
- `TaskTerminated`: permanently ineligible; remove after it is no longer current

### API and lifetime

- `AddTask(task)` accepts valid new tasks once, with an owner and executable code
  selector. The capacity is 256; null, invalid, duplicate or already-owned tasks
  are rejected
- `SleepCurrent(ticks)` synchronously sleeps a task. Zero requests a yield;
  nonzero sleeps accept at most `0x7fffffff` ticks. Deadline comparisons remain
  valid across 32-bit timer rollover
- `YieldCurrent()` lets the timer switch away and waits until the caller is
  dispatched again. Both synchronous operations require IF enabled; they return
  false when that precondition is not satisfied
- `SleepTask(task, ticks)` is a management request. A currently executing task
  stops at the next timer IRQ; callers needing synchronous sleep use
  `SleepCurrent`. `WakeTask` makes a sleeping task ready early
- Returning from a task function runs a real bootstrap trampoline and calls
  `ExitCurrent()`. The task becomes terminated and cannot run again
- `TerminateTask` marks a task terminated; `RemoveTask` removes only a task that
  is not currently executing. Removal repairs the current index when earlier
  slots shift. Removed/terminated tasks cannot be accidentally re-added
- `SetAffinity(task, 1)` binds to scheduler CPU 0, the BSP. Zero, AP-only and mixed
  BSP/AP masks fail because AP scheduling is not available
- Task storage and stacks are caller-owned. Remove a non-running task before
  destroying it; keep every admitted task and its manager alive while runnable

The task bootstrap uses the interrupt frame's final `esp`/`ss` words as the
synthetic cdecl return address and task argument. Ring-0 `iret` does not pop those
words. The initial stack is aligned for the i386 ABI. `CPUState` must match the
assembly frame: seven registers, per-frame vector, error, EIP, CS, EFLAGS, then
optional ESP/SS. CPU error-code exceptions and no-error exceptions need different
stubs. Interrupt entry must clear DF and align the C++ handler's call stack.

Scheduler state mutations disable local interrupts, which is sufficient only on
the BSP. No SMP safety is implied. FPU/SIMD state, address spaces, ring-3 tasks,
priorities, guard pages and blocking I/O are not implemented by this scheduler.
Task code must not depend on unsaved FPU/SIMD context. Kernel tasks are trusted
ring-0 code and do not have process isolation.

## Verification

Run the freestanding i386 tests from the repository root:

```sh
./tests/cpu_test.sh
```

The script uses `qemu-i386` automatically when it is on PATH. On hosts without
native i386 execution, set `GTOS_QEMU_I386=/path/to/qemu-i386`. No 32-bit libc is
needed. The test runner compiles with `-Wall -Wextra -Werror`.

Coverage includes CPUID-only fallback, valid MADT/MP tables, checksum failures,
truncation, malformed record lengths, duplicate and disabled IDs, x2APIC IDs,
10,000 malformed probes, task admission and ownership, initialized ABI frames,
boot-slot fairness, sleeping/waking/termination/removal, tick rollover, offline
affinity rejection and the 256-task limit.

`TaskManager::RunSelfTests(&gdt)` uses a separate scheduler and synthetic frames;
it never switches the actual CPU or changes the live task list. The integrated
kernel also exercises real PIT context switching with a sleeper and yielder,
checks their counters, confirms both return through the task trampoline, and
then reaps them while the desktop boot loop continues. The deterministic tests
are not a substitute for this QEMU runtime test.

## Concrete next SMP milestone

1. Retain a validated APIC-ID inventory with per-CPU startup state. Reserve a
   low-memory real-mode trampoline page and independently allocated AP stacks
   through the physical allocator before heap use
2. Validate local APIC support and addresses, load a small protected-mode
   trampoline, send INIT/SIPI using bounded delivery waits, and require a unique
   per-AP acknowledgment. Report failed starts explicitly
3. Give every AP its own stack and CPU-local identity, then run an isolated
   per-core self-test before parking it. An AP that reached this stage is
   **started/parked**, not yet online for scheduling
4. Install AP-safe GDT/IDT state, local APIC timer/IPI handling and an AP idle loop.
   Remove remaining global interrupt assumptions; keep exception reporting safe
5. Introduce per-CPU run queues/current-task/idle state, spinlocks with interrupt
   discipline, memory-ordering rules, remote wakeups and a CPU-online transition
   only after these facilities are functioning
6. Save FPU/SIMD state and verify migration, task lifetime and allocator/device
   synchronization before permitting ordinary tasks on APs. Test actual work on
   every online CPU, offline-affinity failures, wakeups and shutdown behavior

Until these steps are complete, advertising all detected CPUs as online or
accepting AP affinity would misrepresent what the kernel can execute safely.

## References

- [UEFI ACPI 6.6: MADT and processor-local-APIC structures](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html)
- [Intel processor-topology enumeration reference](https://github.com/intel/SDM-Processor-Topology-Enumeration)
- [Linux x86 topology documentation](https://www.kernel.org/doc/html/latest/arch/x86/topology.html)
