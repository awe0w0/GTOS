# AP startup and parked-core validation

This module adds **real application-processor execution** after CPU discovery.
It is deliberately separate from parallel task scheduling. On a four-vCPU QEMU
machine, the BSP starts APs 1, 2 and 3, each AP executes a private-stack self-test,
and all three park. The scheduler still reports **one online CPU**.

## Boot integration

```cpp
CpuManager cpu;
cpu.Detect(accessiblePhysicalBytes);
CpuStartup startup;
bool allStarted = startup.Start(cpu.GetInfo(), frames, accessiblePhysicalBytes);
CpuStartupReport report = startup.GetReport();
// report.parkedAps is distinct from report.schedulerOnlineProcessors (always 1).
```

Call `Start` once, on the BSP, before interrupts are enabled and before paging is
enabled. The BSP must have flat 4-GiB code/data segments. The physical allocator
must already be initialized from a trustworthy Multiboot memory map. The AP
module does not call a global heap allocator. It does not send APs into the BSP
scheduler, shared exception handlers or device drivers.

When adding BSP paging afterward, keep ordinary allocated AP stack/shared-record
frames reserved and identity mapped for BSP diagnostics. APs remain paging-off
and parked. The low trampoline remains permanently owned because it contains the
APs' GDT. The module does not require ongoing BSP LAPIC MMIO access after startup;
its validated MMIO base is exposed for a future explicit device mapping.

## Hardware and ownership checks

- The MADT or MP processor inventory is revalidated, including checksum, bounds,
  enabled IDs, duplicates, the BSP's presence and agreement with the detected CPU
  count. A MADT local-APIC-address override must be unique, well-formed and fit in
  32 bits
- CPUID must advertise MSRs and a local APIC. IA32_APIC_BASE must identify this CPU
  as BSP and an enabled xAPIC; x2APIC mode is currently rejected without changing
  modes. The MSR address must agree with firmware, lie in the high MMIO region
  outside the supplied accessible-RAM span, and expose a plausible integrated
  APIC version and the expected BSP APIC ID
- `claimLowBootstrapPage()` supplies an E820/BIOS/EBDA/boot-data-validated page in
  the eligible conventional-memory region. A fixed guessed physical page is
  never used. The page remains permanently claimed
- Shared startup records, an emergency AP IDT and independent 16-KiB AP stacks
  come from the physical allocator. Stack allocation excludes the final physical
  page so a 32-bit stack-top pointer cannot wrap at 4 GiB. They remain allocated for the entire boot,
  including after an AP timeout or a delivery error. A late AP cannot execute on
  a stack that the BSP has freed or repurposed
- Every APIC ID has an immutable stack/record lookup entry. Starting APs one by
  one never reuses a mutable single-AP mailbox. Destination IDs 255 and above are
  reported unsupported in this xAPIC transport

The AP emergency IDT halts the affected AP on an exception/NMI. It does not touch
BSP interrupt globals or attempt to return through a damaged frame. An exception
before successful acknowledgment is reported as a bounded startup failure by the
BSP. The APs do not enable maskable interrupts.

## Startup sequence and evidence

The BSP sends directed INIT assert/deassert and SIPIs using finite APIC delivery
polls. PIT channel 2 supplies actual elapsed delays independently of PIT channel
0 and the BSP scheduler. The existing speaker/gate bits are restored, but startup
requires channel 2 not to be in active use. The module leaves the BSP's original
local-APIC spurious-vector configuration restored after the startup pass.

The SIPI trampoline transitions each AP from real mode to protected mode, chooses
its stack by hardware APIC ID, loads its emergency IDT and calls the isolated AP
entry routine. Each AP verifies its actual APIC identity, stack location,
interrupt-disabled state and protected-mode/paging-off state, and computes an
integer checksum. The BSP independently verifies the checksum. Only then does a
successful AP report `CpuStartupParked` and halt.

Shared observations use atomic loads/stores. An acquire read of the published
state establishes whether identity/stack or final checksum observations are
ready. Every query independently checks terminal identity, stack and checksum
against BSP-owned expectations, including late acknowledgments after a timeout.
Delivery failure transitions use compare-and-exchange so a simultaneously
successful acknowledgment cannot be overwritten. Deadline failures retain all
memory and can coexist safely with an AP that acknowledges late. Reports expose
current observed counts; a partial-failure result is not silently turned into a
promise that later startup succeeded.

A second startup call is rejected. There is no retry, AP shutdown, CPU hotplug,
parallel task dispatch, migration, cross-CPU wakeup or FPU/SIMD context switching
in this module. Those require per-CPU scheduler/interrupt infrastructure,
synchronization throughout shared kernel services and a separately verified
scheduler-online transition. AP parked/acknowledged counts must never be labeled
as scheduler-online counts.

## Reproducible tests

```sh
./tests/cpu_startup_test.sh
./tests/cpu_startup_smoke.sh
```

The first script uses freestanding Linux i386 binaries at `-O0` and `-O2`. It
revalidates MADT/MP inventory, malformed overrides, missing BSP, inconsistent
counts, capacity/pointer bounds, late-acknowledgment verification and startup
admission guards. It uses `qemu-i386`
when present, or `GTOS_QEMU_I386` when supplied.

The second script builds a separate optimized Multiboot kernel with the real
startup module and runs:

- 1 vCPU / 32 MiB: no APs are spuriously started
- 2 vCPUs / 64 MiB: one AP acknowledges and parks
- 4 vCPUs / 64 MiB: three APs acknowledge and park
- 8 vCPUs / 128 MiB: seven APs acknowledge and park
- Six vCPUs with two sockets and three cores each: sparse APIC IDs 0, 1, 2,
  4, 5 and 6 confirm that stack/record lookup does not assume consecutive IDs
- A deliberately nonexistent APIC ID: finite timeout, precise failed state, and
  stack/trampoline ownership retained
- A no-APIC-capability input: startup rejects before any IPI

Every case checks scheduler-online remains one. The timeout and capability tests
inject controlled test inventories/features; they do not represent successful
AP execution. Hardware smoke tests require `qemu-system-i386`, optionally set via
`GTOS_QEMU_SYSTEM_I386`. `GTOS_QEMU_DATA_DIR` selects the QEMU firmware directory,
and `GTOS_AP_TEST_OUTPUT` retains debug logs. QEMU's ISA debug-exit device provides
a checked process status in addition to the serial-style debug log.

This validates the xAPIC BIOS/Multiboot path in QEMU TCG. It does not establish
support for UEFI-only firmware, x2APIC mode, CPUs above 32-bit addressability,
arbitrary real hardware, AP scheduling, production power management or secure
process isolation.

## Protocol reference

The initialization sequencing follows the processor-startup guidance in the
[Intel SDM multiprocessor-management documentation](https://cdrdv2-public.intel.com/835748/252046-sdm-change-document.pdf).
Firmware enabled-CPU and local-APIC structures follow the
[ACPI specification](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html).
