# Bounded native ELF32 validation

`include/process/elf32.h` and `src/process/elf32.cpp` provide the pure validation
and load-plan stage for small native GTOS user executables. This is a prerequisite
for a native process loader, not a Linux ABI, a general dynamic linker, or a
Chromium port. A successful validation does not execute anything or establish
that the code obeys the native syscall ABI.

## API and limits

All names are in `gtos::process`:

```cpp
Elf32LoadPlan plan;
Elf32Error error;
if (!ValidateElf32(image, imageSize, plan, error)) {
    // Elf32ErrorName(error) describes the rejected feature or invalid field.
}
// The three-argument overload omits the diagnostic output.
```

The plan holds `entry`, `segmentCount`, `pageCount`, and fixed
`segments[Elf32MaximumSegments]`. Each segment has `fileOffset`, `fileSize`,
`memorySize`, `virtualAddress`, and ELF `flags` (`Elf32Read = 4`,
`Elf32Write = 2`, `Elf32Execute = 1`). Segment order is program-header order;
ascending virtual-address order is not required. Unused entries are zero.
On every failure **the entire output plan is zero**, including failures after
one or more valid load segments. The diagnostic is per call, with no mutable
global error state.

The explicit compile-time limits are:

- 16 nonempty `PT_LOAD` segments
- 256 distinct image pages of 4096 bytes, including partial first/last pages
- 64 program headers and 256 optional section headers
- User virtual addresses in `[0x40000000, 0xC0000000)`

The validator does not allocate, map, copy executable data, dereference user
virtual addresses, or read file data beyond supplied bounds. Work depends on the
bounded header counts, not file size or the amount of BSS. Overlap checking is
bounded quadratic work over at most 16 loads. File ranges use subtraction-based
bounds checks before pointer arithmetic; virtual extents are checked against the
exclusive user limit before addition or page rounding. Fields are decoded
bytewise, so the input buffer need not be aligned.

The caller must supply an actual readable, stable kernel-owned buffer of
`imageSize` bytes. A pointer and claimed length cannot establish memory ownership
or accessibility. Input may not alias the output plan or diagnostic storage.
Keep the buffer unchanged and alive between validation and copying; do not
validate storage and later reload potentially changed bytes without revalidation.
The plan is also trusted kernel storage, not a structure accepted from userspace.

## Accepted format

- ELF32, little endian, current identification and ELF versions (both 1)
- System V OSABI 0, ABI version 0, `ET_EXEC`, `EM_386`, processor flags 0
- Canonical 52-byte ELF header and 32-byte program-header records
- At least one load segment; nonempty program-header table entirely in the file,
  beginning after the ELF header
- Every load has `0 <= filesz <= memsz`, with nonzero `memsz`, a bounded source
  range, and a bounded user virtual range
- Load flags contain only R/W/X, must include R, and must not include W and X
  together
- `p_align` is 0, 1, or a power of two; virtual and file offsets are congruent
  modulo `p_align` when it exceeds 1 and always congruent modulo 4096
- Page-rounded load intervals are disjoint, even if two byte ranges in the same
  page would otherwise not overlap
- Entry lies strictly inside the file-backed portion of an executable load
  segment; BSS, end boundaries, readable data, and unmapped addresses cannot be
  entries

Physical-address fields are ignored; they never authorize physical mappings.
Loads may refer to the same file bytes, because each gets independent owned
frames. Zero-file-size BSS-only loads are accepted away from the entry, including
an empty file range at EOF. Zero-memory-size loads are rejected rather than
silently dropped. Identification padding is ignored.

## Optional metadata and rejected features

Program-header policy is intentionally conservative:

- `PT_NULL`: all other fields ignored, as the entry is unused
- `PT_NOTE`: opaque informational bytes, file extent validated, never mapped or
  interpreted by the loader; its address, size-in-memory, alignment and flags do
  not grant permissions
- `PT_GNU_STACK`: accepted only with exactly R|W, zero offset/address/file size/
  memory size, and 0/1/power-of-two alignment; an X request is explicitly rejected
- `PT_DYNAMIC`, `PT_INTERP`, and `PT_TLS`: specific unsupported-feature errors
- Every other type, including `PT_PHDR`, GNU RELRO, GNU properties, OS/processor
  extensions, and obsolete shared-library headers: explicitly rejected

Missing or repeated harmless headers do not add mappings. Repeated supported
stack policy headers must each satisfy the same non-executable-stack policy.
The caller chooses the stack size and location; this metadata cannot create it.

Section headers may be absent (`shoff = shnum = shstrndx = 0`, `shentsize` 0 or
40). A present table needs canonical 40-byte records, a fully bounded extent,
and an all-zero null section at index zero. A nonzero section-name-table index
must identify an in-range STRTAB section. Extended numbering is unsupported.
Every non-NOBITS section must have a file-bounded extent. NOBITS descriptors do
not cause reads or allocations; loaded memory comes only from `PT_LOAD`.

Present sections explicitly reject REL, RELA, RELR and Android packed relocation
types, DYNAMIC and DYNSYM types, and the TLS flag. This includes relocation
records retained merely for debugging; callers should strip such records when
producing executables for this minimal format. Other sections, symbol/string
tables and debug metadata are not consumed or mapped separately, and section
names and symbol links are not interpreted. Their metadata is not a second
source of virtual mappings or permissions. Constructor arrays, if present, are
ordinary data; executable startup code would have to implement their behavior.
A stripped image must still meet all program-header checks.

## Transactional loader integration

1. Read the complete image into stable trusted kernel memory and validate it
   before preparing a new process address space. Retain the validated plan and
   identical image bytes until all copies finish.
2. Account for runtime pages, stack pages, and reserved stack/guard locations.
   The parser's 256-page budget counts the **image only**, while
   `ProcessAddressSpace::MaximumPages` covers **all mapped process pages**.
   Consequently a valid 256-page image may not fit a process that also needs a
   stack. Check that budget and stack/image disjointness before mapping.
3. Prepare a fresh, unpublished address space. For each planned interval, map
   every page from `floor(virtualAddress / 4096)` through the final occupied page
   using newly owned **zeroed** frames, temporarily writable. Never map caller-
   supplied physical addresses or ELF file buffers as executable page aliases.
4. Copy exactly `fileSize` bytes from `image + fileOffset` to `virtualAddress`
   through `CopyToUser`. Leave BSS (`memorySize - fileSize`) and both page-padding
   areas zero. A zero-file-size segment needs no copy. Do not assume the allocator
   zeroes memory unless its API actually guarantees this.
5. Apply final page write permissions from `Elf32Write` using `ProtectPage`.
   Whole-page disjointness makes this unambiguous. Allocate/initialize the
   separately budgeted user stack; keep any chosen guard page unmapped.
6. On **any** preparation, allocation, copy, protection, stack setup, or sealing
   failure, destroy the entire new address space in the documented safe context.
   Do not expose a partial process, reuse an incomplete plan, or leave mappings
   behind. Preserve the original failure separately if teardown changes the
   address-space diagnostic. If safe teardown itself fails, do not publish or
   free the ownership object prematurely; quarantine it and report the failure.
7. Seal only the fully initialized address space, then publish the runnable task
   and validated entry using the native runtime's CPL3 transition and ownership
   rules. Never destroy an active or still-runnable address space.

R/W/X checks are **loader policy**, not an NX claim. Non-PAE i386 paging can make
pages read-only but cannot mark data or a stack non-executable. Rejecting W+X
segments and executable-stack requests therefore does **not** provide hardware
W^X or prevent execution from writable pages. The native runtime must separately
provide its available user/kernel isolation, safe syscall copies, and fault
handling. Static linking here does not imply libc, POSIX, networking, graphics,
or browser compatibility.

## Reproducible verification

```sh
./tests/elf32_test.sh
```

The script creates a temporary fixture locally with GNU `as --32` and `ld
-melf_i386`; no downloaded executable is used. The fixture contains RX text,
RW initialized data, multi-page BSS and a non-executable GNU stack header. Tests
validate this real image and every truncation of it, as well as unaligned input.

The same C++ parser and assertions run as host and i386 freestanding binaries at
both `-O0` and `-O2`, without libc or libstdc++. Native i386 execution or the
repository's established `qemu-i386` fallback is used. A required host ASan/UBSan
run additionally poisons inaccessible tails for truncated buffer tests; it stops
on sanitizer errors. `ELF32_SANITIZERS=0` is an explicit, visibly reported opt-out
for toolchains lacking sanitizer runtimes, not equivalent verification.

Coverage includes all error classes, malformed/truncated tables, overflowed file
and virtual fields, unsupported formats and features, page overlaps, partial
pages, all budget boundaries, BSS-only segments, entry boundaries and permissions,
output clearing, and 21,000 deterministic mutation/random-byte fuzz cases. Every
accepted fuzz result is independently checked for plan bounds, page counts,
entry membership, permissions and page disjointness, and repeat validation must
return identical results. This suite exercises validation only; it does not
substitute for CPL3 execution or process teardown acceptance tests.
