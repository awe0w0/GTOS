# Native process address spaces

`ProcessAddressSpace` is the bounded first native address-space implementation.
It uses real i386 non-PAE CR3 directories, private 4 KiB user page tables, and
private zero-initialized data frames. The production module is
`include/memory/process_address_space.h` and `src/memory/process_address_space.cpp`.
This document describes the memory contract; the task runtime owns CPL3 entry,
exception recovery, scheduling, syscall dispatch, and deferred reaping.

## Boundaries and guarantees

- User virtual addresses are inside `[0x40000000, 0xC0000000)`, with at most 256
  mapped user pages per process (1 MiB of data). The exclusive upper bound is
  never itself a valid pointer, including for a zero-length request
- Each process owns one directory, every page table it adds, and every user data
  frame it allocates. Two processes may use the same user virtual address and
  receive different physical data frames. The cap covers data pages; a sparse
  worst-case mapping also needs 256 table frames and one directory frame
- Kernel PDEs are borrowed, supervisor-only, from an already enabled and
  permanently sealed `KernelPaging`. The entire user arena must contain no
  present kernel PDE, even if the particular requested user PTE would be a hole
- Sharing does not reopen or modify `KernelPaging`; no borrowed frame is freed.
  If a machine's identity-mapped RAM or an explicit device/alias occupies any
  PDE in the user arena, preparation fails closed. This first layout therefore
  does not provide an arbitrary-size physical direct map
- All API operations that inspect or change mappings are BSP-only, with an
  interrupt guard spanning validation and mutation/copy. The hardware checks
  require CR0.PG and CR0.WP, CR4.PAE clear, CPUID, MSR/APIC support, and
  IA32_APIC_BASE.BSP set. This is not an SMP lock or TLB-shootdown protocol
- APs must never load a process directory, mutate the allocator, or hold pointers
  to process-owned memory. Existing AP workers retain the immutable kernel CR3
- This is not NX or W^X protection. Non-PAE i386 has no NX bit, so user data and
  stacks remain executable. Read-only code is possible, but a writable supervisor
  identity alias to every allocated RAM frame still exists

## Lifetime and API

Keep the process object, kernel mapper, and physical allocator alive while any
saved task context can use the process. The object must be in writable memory
retained by the kernel template. Copying the object is prohibited. Its implicit
trivial destructor deliberately performs no cleanup.

1. Enable `KernelPaging` and call `sealForSharedProcessors()`. Complete all common
   kernel stack aliases and other kernel mappings before sealing
2. On the BSP with the kernel CR3 active, call
   `Prepare(kernelPaging, physicalAllocator)`. Pass the allocator that owns the
   template directory and tables. Preparation enforces allocator object identity
   through `KernelPaging::usesAllocator` before changing state or allocating; a
   distinct allocator is refused even if it reports identical allocated frames.
   It also verifies the template's supervisor-only 4 KiB flags, mapped
   paging-structure aliases, and disjoint user arena before allocating a private
   directory
3. Use `MapNewPage(pageAddress, writable)` for new zeroed user pages. There is no
   interface for importing arbitrary physical frames or creating user aliases
4. Populate writable pages with `CopyToUser`. To load code, map writable, copy the
   image, and then `ProtectPage(pageAddress, false)`
5. Call `Seal()` before publishing `DirectoryAddress()` to a task. Sealing is
   idempotent under the kernel CR3, and freezes map/unmap/protect. Data copies and
   validation remain available for syscall/runtime use
6. After the runtime removes all runnable references, timer/interrupt return
   contexts, and other users, switch to the kernel CR3 and call `Destroy()` from
   deferred reap. Destruction validates all ownership and tables before clearing
   and freeing every owned data, table, and directory frame

`MapNewPage`, `ProtectPage`, `UnmapPage`, `Seal`, and `Destroy` require the kernel
CR3. An operation on the currently loaded process directory fails with
`ProcessMemoryActive`; an unrelated CR3 fails with `ProcessMemoryUnsafeContext`.
`UnmapPage` releases a data frame and its now-empty private table immediately,
but only before sealing. An unmap frees capacity for another page. `Prepare` and
new-page mapping are transactional: any failure leaves existing mappings intact
and returns every newly allocated frame. No data page or table is published
before allocation, alias verification, and zeroing have succeeded.

`Destroy()` accepts sealed spaces after the runtime has completed the lifecycle
steps above. A CR3 check cannot discover references in scheduler queues or saved
interrupt frames. The runtime must enforce that contract; merely finding an
inactive CR3 does not prove that reclamation is safe. Refusing teardown on an
active or inconsistent space intentionally retains frames rather than freeing
live or borrowed memory. No automatic destructor can safely infer this lifetime.
A successfully destroyed object can be prepared again.

`Prepared()`, `Sealed()`, and `DirectoryAddress()` are passive observations, not
reference acquisition, activation, or lifecycle synchronization.
`GetLastError()` and `ErrorName()` explain a failed operation. Successful
operations reset the error. Callers must not use an error value as a lock.

## Validated copies

`ValidateUserRange(address, length, writable)` accepts only pages created by this
object with matching owned frames and hardware permissions. It accepts a
zero-length address within the arena without requiring a mapped page. Null,
supervisor, out-of-arena, overflowing, missing, malformed, and write-to-read-only
ranges fail.

`CopyToUser(destination, source, length)` and
`CopyFromUser(destination, source, length)` validate the complete user range and
complete kernel buffer before touching any output byte. A bad second page cannot
cause a partial first-page copy. They walk the user range page by page and use
verified writable supervisor physical identity aliases to access each owned
frame, rather than dereferencing the untrusted user virtual address.

The kernel buffer must be a trusted buffer in the shared kernel mappings. A
nonidentity supervisor stack alias is supported. Every buffer page must be
present, supervisor-only, normally cached, and writable for `CopyFromUser`.
Physical overlap with this process's data frames, its paging structures, or the
shared kernel paging structures is rejected, including through another kernel
virtual alias. Length/address arithmetic is checked before any dereference.
A zero-length copy does not access or require a kernel buffer.

Trusted kernel buffers remain the caller's responsibility: this API does not
turn an arbitrary kernel address supplied by user code into an authorized output
buffer, prove C++ object bounds, or protect unrelated kernel objects from an
incorrect privileged caller. The kernel itself, allocator metadata, and the
sealed template remain trusted. Hardware accessed/dirty bits may change and are
accepted; unexpected flag bits, supervisor-only user entries, alias changes,
missing entries, and mismatched permissions are rejected. Full sealing,
management, and destruction also detect unexpected entries elsewhere in a
private table. An ownership ledger, rather than raw PTE contents, determines
which frames may be freed.

Copy/validation calls may run under either the kernel CR3 or this process's own
CR3, permitting the syscall handler to run on its retained kernel stack. To copy
another process while an unrelated private CR3 is active, first switch to the
kernel directory. Interrupt masking keeps mapping and ownership checks stable
through the copy on the BSP; no fault-recovery-by-probing or concurrent unmap is
supported.

## Limits

There is no demand paging, mmap-style region allocator, fork, copy-on-write,
shared user memory, pin counts, swapping, ASLR, execute-only permissions, NX,
unbounded heap growth, SMP process scheduling, or automatic frame recovery after
kernel corruption. Kernel paging must remain immutable for every process's
lifetime. The physical allocator's raw `free` API must never be applied to a live
process data/table/directory frame by another subsystem. The supervisor identity
map makes privileged access possible; CPL3 isolation depends on the runtime
actually installing the private CR3 and entering CPL3 with correct GDT/TSS,
interrupt, and segment state.

## Verification

Run the actual production sources with the freestanding i386 host harness:

```sh
./tests/process_memory_test.sh
```

The script builds and executes both `-O0` and `-O2` with
`-Wall -Wextra -Werror`. Linux `mmap` supplies real physical-frame storage; only
privileged register reads/interrupt masking and existing boot hardware probes
are substituted by `GTOS_PROCESS_MEMORY_TEST`, `GTOS_MEMORY_TEST`, and
`GTOS_PAGING_TEST`. None of these defines belong in a kernel build.

Coverage includes separate CR3 directories and identical-VA physical isolation;
page/range boundaries and overflow; foreign-allocator refusal without side
effects, including matching allocation bits; supervisor/read-only pointer rejection;
multi-page copy validation with unchanged output on failure; paging-structure
and data aliases; kernel-template hash preservation; malformed PDE/PTE flags and
address/permission tampering; permitted accessed/dirty changes; real allocator
exhaustion before directory/table/data allocation; unmapped physical aliases;
rollback and freed-frame counts; sealing, active/wrong-CR3/BSP checks; page-cap
reuse; and explicit destruction/repreparation.

These host tests do not execute hardware CR3 changes or CPL3. Guest integration
must separately verify actual user accesses, read-only and supervisor faults,
process exit/fault recovery, deferred reap, AP kernel-template stability, and
continued desktop operation before the full native task milestone is considered
complete.
