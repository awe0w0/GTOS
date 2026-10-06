# Safe low-memory pages for AP startup

`PhysicalMemoryManager::claimLowBootstrapPage(uint32_t& address)` provides a
separate, boot-only page pool for an x86 SIPI trampoline. It returns a boolean
and clears the output address on failure. The returned physical page is 4 KiB
aligned, at least 64 KiB, and below 640 KiB. `isBootstrapPage(address)` reports
its permanent ownership.

This API does not make arbitrary reserved low memory allocatable. In particular,
`reserveRegion(0x7000, 4096)` is not evidence that address 0x7000 is safe to
write. The normal allocator continues to exclude the entire first MiB.

## Eligibility and ownership

1. The low pool starts from complete type-1 pages in the actual Multiboot memory
   map. The `mem_upper` fallback cannot invent low-memory eligibility.
2. Overlapping non-available firmware ranges take precedence over RAM entries.
3. The first 64 KiB is excluded for the IVT, BDA and conservative firmware and
   real-mode scratch space.
4. The upper limit is the minimum of the BIOS conventional-memory size, the
   Multiboot lower-memory size when supplied, and the EBDA base when present.
   The page partially containing that limit is excluded. VGA/ROM memory is
   always excluded. Invalid firmware bounds disable the low pool safely.
5. Every ordinary boot reservation also applies: kernel, allocator metadata,
   Multiboot structures, strings, module contents, symbol data, framebuffer,
   and explicit extra reservations.
6. A claim permanently removes the page from the low pool. There is no free
   operation because a delayed processor may still enter the trampoline.
   Reinitialization and conflicting reservations are rejected after a claim.

The bitmap implementation is in `memory/bootstrap.h` and `memory/bootstrap.cpp`.
The physical manager serializes these operations with its single-CPU interrupt
guard. AP startup must obtain pages on the BSP; this is not an SMP allocator.

`getStatistics().bootstrapFrames` counts permanently claimed bootstrap pages.
`bootstrapFreeFrames` counts remaining boot-only candidates. Neither changes the
ordinary `freeFrames`/`allocatedFrames` counts. Bootstrap pages remain part of
ordinary `reservedFrames`, so they must not be counted twice as general RAM.

## Integration

```cpp
uint32_t trampoline;
if (!physicalMemory.claimLowBootstrapPage(trampoline)) {
    // Skip AP startup and retain the working BSP-only kernel.
}
// Copy the trampoline to this page; supply its address in the startup IPI.
// Allocate independent AP stacks from ordinary frames before sending any IPI.
// Preserve all trampoline, mailbox and stack ownership for late-arriving APs.
```

The allocator proves the destination belongs to the trampoline; it does not
verify the trampoline's real-mode addressing, GDT, startup sequencing, AP stack
layout, memory ordering, or timeout recovery. Those require CPU-side validation.

## Tests

```sh
./tests/memory_bootstrap_test.sh
```

The main `tests/memory_test.sh` also invokes this suite. The low-page extension
does not change the boot heap self-test's stack requirement: approximately 9 KiB,
so run that diagnostic on the large boot stack rather than a 4 KiB task stack. Both `-O0` and `-O2`
builds use `-Wall -Wextra -Werror` and test the production pool and physical
allocator. The test policy covers conventional memory/EBDA limits, malformed
bounds, partial pages, duplicate claims, exhaustion, reservation conflicts,
module exclusions, separate general-memory accounting, and failure cleanup.

Under `GTOS_MEMORY_TEST`, BIOS-word reads use a conventional 640 KiB/0x9FC00
fixture because the host process has no BIOS mapping. Policy tests separately
exercise arbitrary supplied BIOS bounds. Kernel builds read BIOS data at
0x413 and 0x40E directly; QEMU boot is required to verify that hardware path.
Do not define `GTOS_MEMORY_TEST` in production.
