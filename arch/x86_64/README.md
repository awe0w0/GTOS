# GTOS x86-64 BSP boot foundation

This is a **separate, explicitly invoked experimental kernel**, not a port of the
i386 desktop, an x64 userspace runtime, or Chromium. It does not link any `src/`
files and changes neither the default Makefile nor existing emulator disks.

The executable now also includes the separately documented [bounded physical-frame
ownership slice](FRAME_POOL.md), after these seven foundation probes. The memory
layout and no-allocator statements below describe the foundation baseline before
that slice initializes; its permanent pool aliases are the documented extension.

## Build and test

Run from the repository root:

```sh
make -f arch/x86_64/Makefile
make -f arch/x86_64/Makefile test
```

Requirements: GNU GCC/binutils capable of freestanding x86-64, Python 3, BIOS
GRUB `grub-mkrescue` plus xorriso, and QEMU x86_64. No hosted libc, cross libc,
third-party source download, or x64 C++ runtime is linked. The ordinary build
writes only `obj/x86_64/`. The test script can discover the existing sibling
`gtos-runtime` rootless tools; building directly can use:

```sh
make -f arch/x86_64/Makefile GRUB_MKRESCUE=../gtos-runtime/bin/grub-mkrescue
python3 arch/x86_64/tests/boot_qemu.py --output /tmp/gtos-x64-new-evidence
```

Evidence output must be a new directory. The runner attaches only each newly
built ISO, no disk images or host block devices; networking is disabled. It
records commands, exit status, source/ISO/guest-log SHA256, compiler/QEMU versions,
ELF headers, host test output and all guest logs. Success requires actual guest
completion, not a timeout or printed boot banner. QEMU's `isa-debug-exit` reports
33 for successful completion and 35 for a deliberate fail-closed halt. On a
machine without that test device the kernel halts indefinitely. Do not add
`-no-shutdown`: it prevents the debug-exit device from terminating QEMU.

`BUILD`, `OPT`, and `TEST_INJECT` are build variables. A content-checked compiler
configuration dependency prevents a changed optimization/injection setting from
silently reusing incompatible object files. Injection values are **test-only
negative images** and never count as a normal foundation pass.

## Entry and trust boundary

- GRUB BIOS Multiboot2 i386 entry into a fixed ELF64 ET_EXEC at 1 MiB
- Require the Multiboot2 magic, an 8-byte-aligned handoff in `[4 KiB, 64 MiB)`,
  total size between 16 bytes and 64 KiB, no arithmetic wrap and no overlap with
  the page-rounded kernel image
- Require protected mode with paging off, CR4 zero, EFER zero, TR selector zero, CPUID, MSR, PAE,
  APIC, long mode and NX. Check IA32_APIC_BASE.BSP. Other entry configurations
  fail closed; they are not silently normalized into an untested configuration.
  TR=0 is an intentional clean-loader restriction, not a Multiboot2 requirement;
  this also rejects otherwise valid loaders that leave a usable 32-bit TSS loaded
- The loader must supply a physically readable, stable handoff buffer and valid
  loaded image/flat segment state. Bounds checking is not authentication of an
  adversarial firmware or loader. The initial pointer cannot be proven readable
  using the memory map which that pointer itself contains
- Set up a private GDT and a 32-bit fail-stop IDT before feature/MSR operations
- Mask ordinary interrupts throughout. Mask the legacy PC NMI source with CMOS
  port 0x70 during the mode transition, then unmask after the x64 IDT/TSS are ready
- Set PAE, CR3, EFER.LME/NXE and CR0.PG/WP in that order, then far-jump to an
  L=1/D=0 segment and immediately install the x64 emergency IDT
- The bootstrap runs integer-only. CR0.EM/TS are set and the C compiler uses
  `-mgeneral-regs-only`; this target makes no FP/SIMD ownership claim

The ELF linker includes page padding *inside* all PT_LOAD segments. Otherwise a
loader may legally place boot information in apparent holes inside the rounded
kernel protection range. This was found and corrected with an actual GRUB boot.

The legacy NMI mask is a **PC BIOS platform assumption**, not a universal hardware
quiescence guarantee. No recovery is promised for firmware/SMI or unmaskable events
that circumvent it during the narrow paging/IDT transition. MCE is not enabled.
This is tested on QEMU TCG, not qualified on physical hardware or UEFI.

## Address space and lifetime

Four levels, 4 KiB leaves, no huge pages, no user-accessible mappings:

| Region | Permission |
| --- | --- |
| Kernel text and entry code | supervisor R/X |
| Read-only constants | supervisor R/NX |
| Data, BSS, page tables, stacks, copied boot information | supervisor RW/NX |
| GDT and IDT after initialization | supervisor R/NX |
| Null page and five lower stack guards | absent |
| Original boot information | temporary supervisor R/NX; then absent |
| Everything else | absent |

Only PML4[0], PDPT[0], and the first 32 PDEs exist. Most of their 16,384 leaf
entries remain absent. The 64 MiB bound is a bootstrap addressability limit,
**not** an identity mapping of 64 MiB RAM. The kernel must remain below 4 MiB.
Every entry and leaf is audited in the guest before protection probes, allowing
only hardware-maintained accessed/dirty bits in addition to the intended values.
There are no aliases and no writable/executable leaves.

The original handoff is copied once into a fixed 64 KiB kernel-owned buffer.
Its mappings are removed and CR3 reloaded **before** parsing the private copy.
The parser follows no pointer from a tag. It checks bounded tag sizes, 8-byte
padding, unique terminal tag at the exact end, a single memory map, version zero,
entry stride at least 24 and divisible by 8, whole entries, nonempty nonwrapping
ranges, no overlaps, and a usable range covering the whole kernel. Unknown tags
are skipped, and reserved words are ignored as the specification requires.
Unknown memory types remain unavailable. The byte total is firmware-reported
available RAM, not an allocator free-memory count; kernel reservations have not
been subtracted. The private copy remains resident only
for this boot; no allocator or ownership transfer exists yet.

The low identity layout is deliberate bring-up scope. A future process model
must choose a user/kernel split or higher-half layout, remove unwanted low aliases,
and design per-process tables and TLB/ownership rules before treating arbitrary
userspace virtual ranges as available. This foundation does not yet reserve the
large virtual address regions required by a modern V8 sandbox.

## Descriptors, exceptions and stacks

- Private 64-bit GDT and 104-byte TSS; no DPL3 descriptors or syscall entry
- All 256 IDT gates are present interrupt gates with DPL0; exceptions 0–31 have
  vector-specific stubs with correctly normalized error-code frames
- General exceptions use a 16 KiB IST stack. Double fault, NMI and machine check
  have separate 8 KiB IST stacks. The regular boot stack is 16 KiB. All five stacks
  have an absent 4 KiB lower guard
- The common entry saves all 15 general registers, clears DF, aligns the C call
  stack, and uses IRETQ with unconditional SS:RSP restoration. Red-zone use is off
- Unexpected exceptions halt. The test-only recovery path is armed for exactly
  one vector/error/RIP and, for #PF, exact CR2. It checks the saved CS/SS, IF clear,
  IST frame bounds, and a normal-stack saved RSP or the exact boundary RSP for the overflow probe. It disarms
  before logging and changes RIP only to the explicit probe continuation
- Nested-fault recovery is not a general service. Separate emergency IST stacks
  permit a fatal diagnostic when the normal exception delivery path fails

## Acceptance

A normal image must complete all seven hardware probes:

1. Write to executable text: #PF with P|W (3), exact target/RIP
2. Write to rodata: #PF with P|W (3)
3. Fetch from the RW/NX data page: #PF with P|I/D (17)
4. Read address zero: nonpresent supervisor-read #PF (0)
5. Move actual RSP to the stack boundary and push into the guard: #PF (2), correct
   original RSP, frame on the healthy IST stack, successful stack restoration
6. Read the revoked boot-info address: nonpresent #PF (0)
7. Execute UD2: #UD (6), normalized zero error code and exact continuation

The QEMU suite covers optimized and unoptimized kernels, 16/32/64/256/512 MiB RAM,
`max`, `qemu64`, and `Nehalem` CPUs, pc/q35 machines and 1/2/4 virtual CPUs. More
vCPUs do **not** mean SMP support: APs are never started by this target.

Negative boots remove NX/LM/PAE/MSR/APIC, corrupt magic/alignment/tag size/end tag,
exercise low/high/overlapping/cross-ceiling handoff ranges and invalid total sizes,
reject nonzero CR4/EFER/TR, trigger an unarmed #UD, and make the #PF gate
non-present to force a real #DF. Armed vector/error/CR2/RIP/RSP mismatches and a
wrong continuation must also fail instead of counting as a recovered probe.
The #DF image must reach the fatal handler on its dedicated IST and must never
print foundation success. Host tests compile the actual parser with ASan/UBSan
and cover a maximum-size 64 KiB buffer with 2,729 memory-map entries, boundaries,
and 20,000 deterministic mutations. LeakSanitizer is disabled
because this executor is ptrace-managed; the parser does not allocate memory.

## Limits and next prerequisites

No desktop, AP startup, timer/IRQ service, userspace, allocator, scheduler, devices,
filesystem, network, POSIX/Linux ABI, TLS, ELF64 dynamic loading, Chromium or V8
execution is included. Existing i386 behavior is not replaced. The next work
needs separately reviewed physical-memory ownership, a deliberate kernel virtual
layout, exception/IRQ and SMP policy, x64 process ABI, then loader/runtime services.
Passing this boot target alone justifies none of those claims.

## Architecture references

- [GNU Multiboot2 specification 2.0](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html),
  sections 3.1, 3.3 and 3.6: header, i386 handoff, tag/memory-map format
- [Intel SDM Volume 3A, order 253668-060US](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf),
  sections 9.8.5 (transition), 4.5/4.6 and 5.13 (paging/protection), 6.14 (64-bit
  interrupt frames and IST), and 7.7 (64-bit TSS). These architectural semantics
  were checked against the official manual; the exact manual revision is recorded
  rather than presenting this older PDF as the latest publication
