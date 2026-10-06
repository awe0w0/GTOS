# Memory management

GTOS now separates page ownership from small-object allocation. This is a
single-CPU, identity-mapped 32-bit implementation; it does not enable paging,
virtual address spaces, demand allocation, swapping, or SMP synchronization.

## Kernel integration

The linker must export `kernel_start` and exclusive `kernel_end` around the
**entire** loaded image, including BSS, the loader stack and static allocator
metadata. Preserve Multiboot EAX/EBX before invoking C++ constructors.

```cpp
#include <memory/physical.h>
#include <memory/selftest.h>
#include <memorymanagement.h>

extern "C" uint8_t kernel_start;
extern "C" uint8_t kernel_end;
static gtos::memory::PhysicalMemoryManager physicalMemory;

// Call before activating interrupts, using the bootloader's original arguments.
if (!gtos::memory::RunHeapSelfTest()
    || !physicalMemory.initialize(multiboot_structure, magicnumber,
                                  (uint32_t)&kernel_start,
                                  (uint32_t)&kernel_end)
    || !gtos::memory::RunPhysicalMemorySelfTest(physicalMemory)) {
    // Report a fatal boot diagnostic and halt; do not fabricate a heap address.
}
uint32_t heapAddress;
const uint32_t heapPages = 1024; // 4 MiB
if (!physicalMemory.allocateContiguous(heapPages, heapAddress)) {
    // Not enough contiguous usable RAM. Halt or explicitly choose a smaller heap.
}
gtos::MemoryManager heap(heapAddress, heapPages * physicalMemory.PageSize);
```

Keep the `MemoryManager` alive for the lifetime of its allocations. Keep its
backing frames owned by `physicalMemory`; do not free or reuse them while the
heap exists. The physical manager contains 256 KiB of bitmap storage and should
be static, rather than placed on a small kernel stack. Add
`obj/memory/physical.o` and `obj/memory/selftest.o` to the kernel object list.
Compile allocation call sites with `-fcheck-new`; global freestanding
`new`/`new[]` return null on allocation failure rather than throwing.

## Physical page allocator

`initialize(info, magic, kernelStart, kernelEnd, optionalRanges, rangeCount)`:

- Checks Multiboot v1 magic and structure address arithmetic
- Parses each variable-sized memory-map record using its declared length
- Rejects short/truncated/overflowing maps instead of falling back silently
- Makes only complete, available (type 1) 4 KiB pages eligible
- Processes reserved entries after available entries, so reserved entries win
  over overlapping or out-of-order available ranges
- Clips physical addresses at 4 GiB; PAE/high-memory support is not implied
- Excludes the first MiB, loaded kernel, allocator metadata, Multiboot structure,
  memory-map descriptors, modules and names, command line, bootloader name,
  symbol tables and loaded ELF sections, drive/APM/VBE data, and framebuffer
- Supports additional explicit early DMA/MMIO reservations
- Uses `1 MiB + mem_upper * 1024` only when the map flag is absent and the basic
  memory-size flag is present; statistics reveal when this fallback was used
- Leaves no usable pages after malformed initialization, and refuses to
  reinitialize while owned pages are still live

Boot structures are trusted, mapped bootloader inputs: arithmetic checks cannot
prove an arbitrary physical pointer actually refers to readable RAM without
paging/fault recovery. Boot strings must terminate within 64 KiB. Legacy BIOS
configuration pointers conservatively reserve 64 KiB. Boot reservations are
permanent in this implementation, even after their contents are no longer used.

```cpp
uint32_t page, dmaBuffer;
physicalMemory.allocate(page);
physicalMemory.allocateContiguous(8, dmaBuffer, 16, 0x00FFFFFF);
// 8 pages, 64 KiB aligned, wholly below 16 MiB (inclusive address ceiling).
physicalMemory.free(page);
physicalMemory.freeContiguous(dmaBuffer, 8);
```

The calls return `bool`; failed allocations set the output address to zero.
Alignment must be a nonzero power of two, expressed in pages. `freeContiguous`
validates the complete range before changing any bits. Freeing is page-granular;
it does not track allocation-group boundaries. Unaligned, reserved, out-of-range
and already-free pages are rejected. `reserveRegion(address, length)` reserves
every overlapping page, rejects arithmetic overflow and fails atomically if
any page is currently allocated. Reservation cannot accidentally release RAM.

`getStatistics()` reports eligible, free, allocated, reserved and addressable
frame counts. Addressable frames include holes below the highest available
range, and reserved frames include those holes. Multiply counts by 4096 using
64-bit arithmetic when displaying bytes. `getLastError()` reports initialization
errors. `isFree`/`isAllocated` accept aligned physical page addresses.

## Heap allocator

`MemoryManager(start, bytes, activate = true)` aligns its arena inward to 16
bytes. Payloads, chunk headers and split boundaries remain 16-byte aligned.

- Correctly subtracts allocated payload when splitting; exact-fit blocks work
- Rounds requests upward and rejects zero-length or overflowing requests
- Validates bounded contiguous chunk links, sizes, magic, allocation flags and
  reciprocal previous links before touching allocator state
- Finds actual allocation starts before freeing; it never trusts a fabricated
  header preceding a foreign/interior pointer
- Treats null free as a no-op and rejects double frees and invalid pointers
- Coalesces adjacent free blocks in both directions
- Supports ordinary, array, placement and sized global new/delete operators
- Preserves/restores the original interrupt-enable state during allocator work

`tryFree(pointer)` provides a checked `bool` result; the original `free(pointer)`
API remains available. `validate()` checks the arena; `getStatistics()` reports
arena bytes, used/free payload bytes, largest free block, block counts, failed
allocations and invalid frees. Used bytes include alignment padding. Arena
bytes also include metadata; `usedBytes + freeBytes` therefore need not equal
`totalBytes`. Corruption causes operations to fail closed; this is not recovery,
use-after-free protection, or a substitute for memory isolation.

The list allocator uses a linear scan and full validation; it prioritizes
correctness and observability over throughput. Interrupt masking serializes
single-CPU interrupt/task access, not other CPUs. Interrupt handlers should
still avoid potentially long allocations. Activating a second heap changes
where global new/delete route, so independent diagnostic heaps use
`activate=false`; callers must not switch global heaps with live allocations.

## Tests

From the repository root:

```sh
./tests/memory_test.sh
```

This compiles and runs the actual production allocator sources at `-O0` and
`-O2`, under a freestanding static i386 Linux harness. It requires `g++` with
32-bit code generation and either native i386 execution or `qemu-i386` on PATH.
No 32-bit C/C++ runtime libraries or multilib headers are needed. Only privileged
interrupt masking is disabled in this harness (`GTOS_MEMORY_TEST`); the kernel
build must never define that macro.

The test script uses `-Wall -Wextra -Werror`. Tests cover alignment, exact fits,
exhaustion, split/coalescing recovery, foreign/interior/null/double frees,
corrupt metadata, global operators, boot magic, map record extensions,
malformed and overlapping maps, page boundaries, 4 GiB clipping, fallback
memory sizing, modules, descriptor reservations, framebuffer/symbol regions,
DMA alignment, failed frees and live reinitialization rejection. A second property
test compares 128 deterministic randomized maps against an independent page
reference model, followed by 16,384 page allocation/free steps.

`RunHeapSelfTest()` runs a 1,024-operation deterministic allocation/free and
payload-integrity workload on private scratch storage. It leaves the active heap
unchanged. `RunPhysicalMemorySelfTest(manager)` allocates and releases temporary
frames and checks ownership/protection while restoring the original live/free
frame counts. Kernel boot tests must additionally exercise real interrupt masking
and actual Multiboot data from GRUB; the host harness does not verify either.

Reference: [GNU Multiboot v1 boot information specification](https://www.gnu.org/software/grub/manual/multiboot/html_node/Boot-information-format.html).
