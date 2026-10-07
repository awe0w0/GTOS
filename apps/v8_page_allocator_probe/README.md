# Native V8 PageAllocator leaf

This CPL3 consumer links the unchanged pinned V8 `src/base/page-allocator.cc`
(revision `be042d4462bee463c9b785701b8c6ee4576ee0a3`) to the real GTOS
owned data-page syscalls. `os_memory.cc` implements the `v8::base::OS` methods
that this actual upstream object consumes; it does not replace PageAllocator.

The SDK uses official pinned libc++ headers and LLVM libc hdrgen output, with
an explicit freestanding libc++ configuration. The original V8 GTOS foundation
patch retains the whole-V8 GN failure. Its only additional derived declaration
is `Semaphore::NativeHandle=uint32_t` under `V8_OS_GTOS`, with a saved diff.
No semaphore/thread method is implemented or linked. V8 DEPS pins the consumed
GoogleTest `gtest_prod.h` declaration independently; no GoogleTest runtime is
linked. The builder verifies consumed repository bytes against pinned Git blobs.
The target is `i686-unknown-none-elf`, with no Linux target/sysroot identity.
Official hdrgen requires Python >=3.9; the qualified environment uses the
workspace's cached Python 3.11.9 and PyYAML.

The normal consumer checks exact descriptors and output overlap, full region
quota, a 16MiB reservation without resident frames, real commit/R/RW/NONE,
zeroing after decommit/discard, recommit, tail release, full release, stale
handles, exact-hint fallback, whole-page byte patterns and syscall register retention.
The actual upstream AllocationHint overload performs real virtual dispatch.
It leaves one 16-page reservation with three RW resident pages, then exits.
A deterministic scalar xorshift32 provides reproducible optional placement
hints. It supplies no entropy or security-qualified ASLR.

`vm_probe_record` is a separate 24-byte RW ELF load at `0x40020000`:
`version, mode, stage, base, handle, foreign_handle`. Mode 0 uses stage 2 after
all checks; each remaining page contains `(offset ^ salt)` for salts
`0x99, 0x5a, 0xa6`. The native guest harness reads this actual record and all
12288 bytes before reap, then checks complete frame recovery. Optional modes
1..5 arm real RO-write, NONE-write, decommit, release and trimmed-tail faults;
mode 6 leaves three resident pages and yields for kernel RequestExit.
The variant mode and exact symbol address are recorded in each manifest.

Executable permissions, shared memory and sealing are unsupported and fail
honestly. The non-PAE kernel has no NX; this does not establish W^X or JIT.
This leaf uses automatic objects and fixed metadata. Unexpected C++ heap or
pure-virtual operations terminate; they are never successful allocation stubs.
The entire retained executable is audited for floating point/SIMD, then
`qualify_stack.py` verifies exact owner-object instructions, actual readonly
vtable dispatch, all CFG/call paths, return addresses, alignment, assembly and
all executable bytes against the 8176-byte usable native stack. It does not
establish guest behavior; the independent native VM guest harness must pass.

No Linux V8/Chromium reference workload is rebuilt or run, and no frozen
ELF64/M1 path is enabled. This qualifies a real upstream memory leaf only;
GTOS Chromium and complete V8 execution remain unqualified.
