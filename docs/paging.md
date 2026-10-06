# BSP kernel paging foundation

GTOS now has a real i386, non-PAE page directory with 4 KiB page tables. This is
page-level **kernel write protection**, not a process isolation implementation.
The BSP sets CR3 and CR0.PG/CR0.WP. All present PDEs/PTEs are supervisor-only;
page zero remains absent, and complete kernel text/rodata pages are read-only.

There are no user address spaces, ring-3 tasks, ELF loader, demand paging,
copy-on-write, swapping, NX protection, or guarded task stacks in this milestone.
Non-PAE i386 paging cannot express NX. Executable data and kernel-wide writable
RAM remain possible. A kernel page fault is fatal; it does not recover a crashed
application. The application VM is still not a hardware-isolated process.

## Boot integration

Keep `PhysicalMemoryManager` and `KernelPaging` alive for the kernel lifetime.
Call CPU discovery and AP startup first, with interrupts disabled and paging off.
APs remain parked with paging disabled, retaining their private stacks, emergency
IDT/shared records and low trampoline/GDT page. This implementation does not make
APs scheduler-online or synchronize page tables with running APs.

The linker exports page-aligned `kernel_start`, exclusive `kernel_end`, and
`kernel_readonly_start`/exclusive `kernel_readonly_end`. Put `.text` and `.rodata`
inside the read-only interval; align the start of `.data` to the following page.
The full kernel interval includes the loader stack and BSS.

Install the GDT/IDT before enabling paging. Finish firmware discovery and parsing
boot strings/symbol/VBE structures before paging: only the boot structures listed
below are retained. Initialize the chosen graphics device first so its exact
framebuffer interval is known. Enable paging before `interrupts.Activate()`.

```cpp
#include <memory/paging.h>
extern "C" uint8_t kernel_start, kernel_end;
extern "C" uint8_t kernel_readonly_start, kernel_readonly_end;
static gtos::memory::KernelPaging paging;

// Explicit VGA aperture. Add a validated framebuffer/LAPIC interval only when
// actually needed; use page-rounded physical address and byte length.
gtos::memory::PagingDeviceRange devices[] = {{0xA0000, 0x20000}};
gtos::memory::PagingConfig config = {
    (uint32_t)&kernel_start, (uint32_t)&kernel_end,
    (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
    (const gtos::memory::MultibootInfo*)multiboot,
    devices, sizeof(devices) / sizeof(devices[0])
};
if (!paging.prepareIdentity(frames, config) || !paging.enable()) {
    // Report KernelPaging::ErrorName(paging.getLastError()), then halt.
}
```

`prepareIdentity` creates only the following identity mappings:

- Every allocator-eligible physical RAM page, whether currently free or allocated,
  so the existing heap, future allocations and parked AP stacks remain accessible
- The complete kernel image, with the explicit text/rodata interval read-only
- The physical allocator's own bitmap/state storage, including when it was placed
  in a reserved boot workspace outside the linked kernel
- The Multiboot information structure, memory-map bytes, module descriptors and
  module payloads still consumed by the desktop installer
- Low bootstrap pages explicitly claimed through the physical manager
- Each explicitly supplied device interval, using supervisor writable PTEs with
  PWT/PCD set for uncached accesses

Unclaimed low memory, physical-map holes and unrequested devices stay absent.
No automatic `0..highest RAM` blanket mapping is created. Memory-map reservations
therefore continue to matter even when a reserved hole falls between RAM ranges.
Boot command lines, bootloader names, firmware tables, palettes and symbol tables
are not guaranteed to remain mapped. Copy/consume these before activation.

Device intervals must be nonempty, page-aligned, fit below 4 GiB and number at
most 64. A collision with RAM, kernel, retained boot metadata or another device
interval rejects the complete preparation. Do not supply overlapping VGA and
text-framebuffer ranges. A framebuffer's pitch-times-height interval must be
validated and page-rounded by its caller; the mapper never invents a device
address. The explicit LAPIC address, when needed, comes from validated CPU
startup results rather than assuming all machines use `0xFEE00000`.

Preparation requires an initialized physical allocator and hardware paging off.
It never changes CR0 or CR3. Any preparation failure frees every directory/table
frame it allocated, clears the partial page tables and preserves earlier physical
allocations. `abandon()` similarly reclaims tables before activation. Once active or explicitly sealed for shared processors,
it refuses teardown; paging frames are permanently allocated for kernel life.
They remain counted as allocated frames, not available RAM.

`enable()` refuses IF=1, paging already active, CR4.PAE=1, an unmapped/wrong-permission
current stack/GDT/IDT, and missing writable identity mappings for its own state,
allocator state, directory or tables. It then loads CR3 and sets CR0.PG and CR0.WP. It neither
installs exception handlers nor makes unknown pointers safe. This requires an
IA-32 CPU supporting CR4 and INVLPG. PSE/PGE may remain enabled: our PDE.PS and
PTE.G bits are always clear. A previous PG-disable transition clears global
translations, and loading CR3 clears nonglobal translations. See Intel's
[system programming manual, TLB invalidation rules](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf).
The supplied kernel
bounds and boot structures remain trusted boot-time inputs.

## Mapping API and ownership

`mapOwnedPage(virtualPage, physicalPage, writable)` creates a non-user alias of an
already allocated normal physical frame. Both addresses must be page-aligned and
nonzero. Free, reserved, kernel and current paging-structure frames are rejected.
An existing virtual mapping is never overwritten. A missing table is allocated
through the physical manager; table-allocation failure leaves existing mappings
and ownership unchanged.

The physical frame is borrowed: the caller must keep it allocated until **every managed**
alias is unmapped. Permanent identity mappings remain present. Do not call the physical allocator's free functions on live
mapped data or paging structures. This API does not add pin counts to the
physical allocator. Arbitrary in-kernel writes can still corrupt these tables.

`unmapOwnedPage(virtualPage)` and `protectOwnedPage(virtualPage, writable)` operate
only on aliases created by `mapOwnedPage`. Boot identity maps, text protections,
devices and the null guard are pinned. Removing an alias does not free the data
frame or its now-empty table. Empty tables are retained until pre-enable
`abandon()` or, once enabled, for kernel life. Protection applies to that virtual
alias; ordinary RAM still has a writable identity alias.

Every active mapping/protection removal invalidates that virtual TLB entry using
INVLPG. Interrupt masking protects BSP updates only; there is no SMP shootdown,
lock or permission to mutate tables shared with executing APs.

`query(byteAddress, result)` reports the translated physical byte address and
writable/user/cache-disabled/managed attributes, clearing the result on failure.
`getStatistics()` reports mapped pages, table-frame count (excluding the one
page-directory frame), directory address, preparation, activation and `sealedForSharing` state.

## One-way shared-processor seal

`sealForSharedProcessors()` requires prepared tables and permanently freezes the
mapping API. Call it **before** publishing the directory address to an AP. Once
sealed, map/unmap/protect and even pre-enable `abandon()` fail with `PagingSealed`;
query/statistics and the first BSP `enable()` still work. Calling the seal again
is harmless. There is no unseal or partial-start rollback: an AP may still be
using these tables after a startup timeout, so their frames remain allocated.

This freezes mappings and permissions through the API; hardware accessed/dirty
bits can still change. It is **not** a TLB-shootdown implementation or AP allocator
synchronization. The caller must retain all borrowed mapped data,
AP stacks and shared records. Directly freeing a table or live shared data
through `PhysicalMemoryManager` is still prohibited. Sealing does not start an
AP, load its CR3, install its exception handlers or make it scheduler-online;
the CPU startup/handoff implementation owns those steps. An AP may share this
CR3 only after that separate handoff has established the required safe context.

## Verification

Run actual production-source host tests without libc or a multilib runtime:

```sh
./tests/paging_test.sh
```

Both `-O0` and `-O2` builds use `-Wall -Wextra -Werror`. The Linux i386 harness
obtains a real writable arena for physical table storage. Only privileged
instructions/interrupt masking are replaced (`GTOS_PAGING_TEST` together with
`GTOS_MEMORY_TEST`); neither macro is permitted in the kernel build. Coverage
includes null/holes/RO/MMIO attributes, borrowed-frame checks, paging-frame alias
rejection, alignment/overflow, top virtual page, collisions, pinned mappings,
prepare-state transitions, active-table lifetime, allocation exhaustion and
rollback after both early and partial page-table construction.

The separate GRUB/QEMU harness exercises real privileged instructions:

```sh
GTOS_PAGING_TEST_OUTPUT=/path/to/logs ./tests/paging_smoke.sh
```

It builds its own tiny kernel and GRUB ISO without modifying the desktop build.
Successful boots use 32 MiB/1 vCPU, 64 MiB/4 vCPUs and 128 MiB/8 vCPUs. Each
asserts CR0.PG/WP, CR3, retained GRUB module access, writable identity RAM/VGA,
and creating/accessing/removing a new virtual alias after activation. Additional
boots verify that CR4.PAE mode is rejected before modifying CR0/CR3/CR4 and
then enable the same prepared tables after the caller clears PAE. Another boot
checks successful activation with compatible PSE/PGE flags set. A sealed-map
boot verifies pre-enable teardown/mutation rejection, successful CR3 activation
and continued alias access without allowing changes. Fault boots
require actual #PF exceptions with exact CR2/error values for:

1. Null read: address zero, error 0
2. Kernel text write: instruction address, error 3
3. Kernel rodata write: constant address, error 3
4. Unrequested IOAPIC MMIO read: `0xFEC00000`, error 0
5. Read after alias removal: `0xC0000000`, error 0
6. Write after making a previously writable alias read-only: `0xC0000000`, error 3

The last two touch the alias before changing its PTE, testing real TLB
invalidation. Tests emit `PAGING SMOKE PASS` and QEMU's explicit success exit
status; a hang/triple fault or unexpected exception fails. These tests do not
start the extra processors; the independent CPU-startup tests verify AP startup
and park. The integrated desktop/QEMU suite must also pass with paging enabled
before claiming the complete kernel configuration works.
