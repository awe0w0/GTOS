# Native IA32 C/C++ heap

This module supplies a bounded native C/C++ heap above the accepted GTOS data-page
VM ABI. The genuine pinned V8 `src/base/platform/memory.h` consumer and official
LLVM libc/libc++ declarations execute against this heap in real CPL3. It retains
the existing integer-only profile and capacity limits. The full V8 backend guard
remains closed; this acceptance does not establish an Isolate, JavaScript engine,
JIT, native Chromium browser, video playback or HTML5 runtime.

## Arena and lifetime

`apps/native_heap/heap.cc` owns one 65536-byte arena per process, for one native
user thread. A positive allocation lazily reserves the arena through the real
VM ABI and commits all 16 pages READ_WRITE. Small allocations still consume those
16 resident data pages. The allocator uses 32-byte, 16-byte-aligned block headers,
a default payload alignment of 16, first-fit selection, splitting and adjacent
free-block coalescing. The maximum default payload in an otherwise empty arena
is 65504 bytes. Larger requests fail; the arena does not grow or acquire a second
arena.

The last free releases the entire VM region, including its resident pages and
any page table no longer needed. There is no tail trim or idle resident cache.
A later allocation gets a new globally monotonic VM handle and newly zeroed
kernel pages; the virtual base may be reused. First-commit failure and a newly
reserved arena unable to satisfy alignment also release the empty reservation.

`gtos_native_heap_query` returns 1 with real owner VM_QUERY metadata for a live
arena, and 0 without modifying output for an empty arena or a rejected kernel
output buffer. The result must describe version 1, the current handle/base,
65536 bytes and 16 resident pages. Test oracles use these public results and
allocation pointers; they do not inspect allocator block headers.

The fixed profile remains: 64KiB boot-file limit, ELF32 static ET_EXEC admission,
254 static image pages plus two user stack pages, 256 total resident user pages,
8192-byte stack with 8176 usable bytes, and BSP native scheduling. Arena pages
count toward that same resident budget. Non-PAE IA32 has no hardware NX; data
allocation is not a JIT or hardware W^X qualification.

## C interface

The allocation declarations come from the genuine generated LLVM libc SDK.
The implementation provides `malloc`, `calloc`, `realloc`, `free` and
`posix_memalign`, with an actual per-process errno cell through
`__llvm_libc_errno`. This profile does not advertise TLS or thread-safe allocation.

- Zero-size malloc/calloc return NULL without changing errno; free(NULL) does
  nothing. realloc(NULL,n) follows malloc; realloc(p,0) frees p and returns NULL.
- Positive allocation failure returns NULL and ENOMEM. Multiplication and size
  overflow are checked before allocation. calloc clears all requested bytes,
  including reused dirty storage.
- Successful realloc preserves the requested prefix and allocation alignment.
  Failed growth preserves the original requested bytes, neighbor allocations
  and the free-block layout. It does not merge an insufficient adjacent free
  block before knowing growth can succeed.
- posix_memalign requires a power-of-two alignment at least sizeof(void*).
  EINVAL/ENOMEM failures preserve the output pointer and errno; a valid zero-size
  request writes NULL and succeeds. NULL, obvious pointer misalignment and
  out-of-user-range output addresses are rejected before a store.

The POSIX output is a caller-provided **writable pointer object**. This user
library cannot supply the kernel wire ABI's checked-copy protection for arbitrary
read-only or unmapped objects. A read-only output can cause a real user page
fault after an internal allocation succeeds; the fault/reap case proves all
process-owned heap frames are still reclaimed. It is not an output-protection
promise for invalid C/POSIX callers.

free validates the complete bounded block chain and requires an exact current
live payload before mutation. An invalid/interior/already-free pointer not
matching a current live allocation terminates with 0x48000002. Once the same VA
has been reused for a new live allocation, pointer bits alone cannot identify
an earlier lifetime. Arbitrary stale pointers or every double-free are not
claimed detectable; callers retain the C/C++ lifetime obligations.

## Genuine C++ replacements

`apps/native_heap/operators.cc` uses the genuine pinned `<new>` declarations.
It defines eight allocation replacements and twelve deallocation replacements,
with scalar and array forms of every row:

| Allocation forms | Count |
|---|---:|
| ordinary new | 2 |
| nothrow new | 2 |
| aligned ordinary new | 2 |
| aligned nothrow new | 2 |

| Deallocation forms | Count |
|---|---:|
| ordinary delete | 2 |
| nothrow cleanup delete | 2 |
| sized delete | 2 |
| aligned delete | 2 |
| aligned nothrow cleanup delete | 2 |
| sized aligned delete | 2 |

Zero-size new is normalized to at least one byte. The actual consumer verifies
simultaneously live zero-sized allocations are distinct, genuine aligned
nontrivial object construction/destruction, and a real new(std::nothrow)
expression whose oversized aligned object returns NULL without constructing it.

Ordinary new OOM terminates through the native EXIT ABI with 0x48000010; explicit
nothrow allocation returns NULL. This is the native no-exception profile, not a
hosted exception/new_handler implementation. No unwinder, new_handler, RTTI,
thread, locale or stdio runtime is advertised by this module.

## Actual pinned V8 memory paths

`apps/v8_heap_probe/memory_calls.cc` compiles the unmodified pinned
`src/base/platform/memory.h` at V8 revision
`be042d4462bee463c9b785701b8c6ee4576ee0a3`. Runtime-argument helpers invoke seven
actual upstream paths: Malloc, Calloc, Realloc, Free, AlignedAlloc, AlignedFree
and AllocateAtLeast<unsigned char>. They add no successful platform substitute.
The real C entry points and posix_memalign provide the backend.

The release configuration has NDEBUG/V8_LOGGING_LEVEL=0. Invalid-alignment
observations concern downstream POSIX rejection after compiled-out DCHECKs;
they do not broaden V8's valid-alignment precondition. The upstream Realloc
size-zero CHECK retains its immediate-crash branch.

## Real guest matrix and independent checks

`tests/native_heap_smoke.cpp` uses the production runtime, syscall handler,
ProcessAddressSpace and physical allocator. Every mode admits an independently
built native ELF, executes at observed CS 0x23 in a private CR3 and calls the
actual int80 VM wire. A CPU-bound native peer uses no syscall/yield, while a heap
peer, ring0 task and boot context must keep progressing through every stop/reap.

The 40-byte HeapProbeRecord at 0x40020000 contains ten unsigned words:
version, mode, stage, base, handle, primary, primaryBytes, neighbor,
neighborBytes, checks. Retained ordinary modes publish two 4096-byte blocks with
whole-byte patterns offset XOR 0x37/0xA9. The kernel checks every published byte,
alignment and disjoint span independently. The fixture's final arena base is
explicitly first-fit 0x80000000, disjoint from its static ELF/stack PDEs, so the
independent heap cost is 16 data frames plus one page table. It does not assume
that 17-frame cost for every possible VM placement.

| Mode | Raw C probe | Actual V8/C++ consumer |
|---|---|---|
| 0 | positive boundaries, hold two blocks, normal Exit | seven V8 paths and twenty replacements, hold blocks, normal Exit |
| 1 | retain heap then write stack guard; PF6 at 0xBFFFCFFC | same |
| 2 | retain heap and yield for external RequestExit(73) | same |
| 3 | last free, no dynamic frame, normal Exit | same |
| 4 | last free then fresh malloc; all 4096 bytes zero | same |
| 5 | interior free; designed BAD_POINTER Exit 0x48000002 | real ordinary new[] OOM Exit 0x48000010 |
| 6 | already-free neighbor not reallocated; BAD_POINTER Exit | absent |
| 7 | read-only posix output at 0x40000000; actual PF7 | absent |

The raw positive mode also checks overflow, zero sizes, output/errno preservation,
full-arena exhaustion, dirty-free-calloc reuse with an intact anchor, failed
realloc with intact old bytes/neighbors, an insufficient adjacent free block
remaining reusable, alignment preservation, coalescing and repeated allocation.
It consumes the true per-process resident quota through VM, then observes forty
failed first-commit malloc attempts. While the quota reservation is still held,
an additional frame-free VM reserve must succeed: this detects reservation-slot
leaks that a physical-frame baseline alone would miss. Releasing the quota must
restore healthy allocation. No allocator implementation is mirrored in a test.

The final matrix is kernel/raw O0 and O2, each at 32MiB/1 CPU, 64MiB/4 CPUs and
128MiB/1 CPU. Each of six guests executes raw modes 0..7 then genuine consumer
modes 0..5: **84 actual CPL3 cases**. All six QEMU runs exit 33. There are fourteen
individual victim Reap baseline checks plus a final two-peer Reap baseline check
per guest: **90 exact physical-baseline checks**. Last-free modes are stopped
before the record is reread and before Reap; the arena is actually unmapped and
owns zero dynamic frames. Other stopped processes retain the full arena until
explicit Reap, including fault, ordinary new OOM and RequestExit cases.

## Compilers, source proof and reproduction

The compiler boundary is recorded accurately:

| Code | Compiler and optimization |
|---|---|
| kernel | GCC13 O0/O2 |
| raw C main and byte support | GCC13 O0/O2 |
| same production heap.cc in raw ELFs | Clang24 O0/O2, real libc++ SDK |
| actual V8/C++ consumer, heap.cc and operators.cc | Clang24 Oz, i686-unknown-none-elf |

GCC13 cannot parse the current official libc++ `<new>` closure: its pinned header
requires GCC15+, and Clang-specific feature/builtin parsing fails. That actual
compatibility failure is retained in
`artifacts/native-heap-gcc-sdk-compat-20261008-01`; no fake feature macro or
substitute standard header was added. GCC raw C declarations use official LLVM
libc headers. The wrapper's actual builtin stddef.h is admitted only by the
exact compiler -print-file-name=include path and hash; it supplies target types,
not a Linux libc/sysroot. Actual sizeof(void*) and sizeof(size_t) are four.

All raw executable instructions are audited as scalar, undefined symbols are
zero, real dependency files close every consumed header to GTOS source, pinned
SDK, generated official SDK or exact compiler resource input. Raw static/bounded
function frames, including compiler-marked bounded outgoing arguments, have a
conservative sum plus 32 bytes per function and 64 bytes for entry/alignment.
The final maxima are 4140 bytes at O0 and 2676 at O2, below 8176; bare dynamic
frames, recursion and indirect calls/jumps are not accepted in the raw paths.
The genuine consumer has complete whole-ELF function/byte/callsite proof: 79
functions, no unknowns, 292-byte maximum chains except mode4's 312 bytes.

Formal input qualification binds each immutable ELF, manifest and
whole-stack/status.json. qualification_complete denotes source/ELF32/scalar/
whole-stack static admission only; those original manifests retain guest_pass
false. The separate real guest status establishes execution of those exact
six inputs. The runner checks source-before/after hashes, copied ISO staging
ELFs and actual SDK dependency hashes. It accepts zero consumers for explicitly
separate raw-only diagnostics, or six ordered formal consumers for acceptance.

Reproduction requires the existing qualified GTOS tool environment, Python >=3.9
(the recorded run uses Python 3.11.8 from the existing depot_tools bootstrap), an explicit **absolute CXX wrapper
path**, and a fresh output directory. A bare command name such as g++ is not
sufficient for the runner's compiler-file hashing. No system installation is
required. In the accepted workspace:

```sh
source /mnt/f/GTOS-Chromium/scripts/env.sh
export PATH="/mnt/f/GTOS-Chromium/sources/depot_tools/bootstrap-2@3.11.8.chromium.35_bin/python3/bin:$PATH"
export CXX=/mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++
cd /mnt/f/GTOS-Chromium/sources/GTOS-native-image-4c7eb7b
inputs=()
for mode in 0 1 2 3 4 5; do
  inputs+=(/mnt/f/GTOS-Chromium/artifacts/v8-heap-final-six-20261007T205625Z/mode-$mode/v8-heap-probe.stripped.elf)
done
bash tests/native_heap_smoke.sh /path/to/fresh-artifacts \
  /mnt/f/GTOS-Chromium/artifacts/v8-heap-sdk-prepare-20261007T202829Z "${inputs[@]}"
```

The accepted runtime evidence is
`artifacts/native-heap-final-20261008-01/status.json`, with actual-input-preflight,
O0/O2 raw SDK closure/stack records, binary hashes and six original guest logs.
`artifacts/v8-heap-final-six-20261007T205625Z/status.json` and mode-N manifests /
whole-stack/status.json provide the immutable genuine-consumer input proof.
The earlier 48 raw-only cases remain separate localization evidence and are not
counted as actual upstream execution.

## Current desktop context

A read-only QMP check at 2026-10-07T21:11Z confirms owned PID1346 is running with
GTK, 64MiB and four configured CPUs. Its actual read-only ide1-cd0 medium is the
previous accepted PNG staging image:
`artifacts/native-ia32-vm-png-guest-20261008-b/GTOS-png-surface.iso`, SHA256
`1499a927f6b544dbe17928ac055dff5310cf226b4ef179d0995a5ac522dc3df0`.
The tray is closed and I/O status is OK. Actual QMP media agrees with saved state;
the original process argv contains an older preview path and is not used as the
current-medium authority.

The ordinary production GTOS.iso is 3432448 bytes, SHA256
`fa8a29c580d41b50b81f15f3e6221aea4643bf75f60fb81dc2d0636cb981568f`.
The visible staging ISO is 3481600 bytes. Their kernels have identical SHA256
`0082390cd69ebf2605a8ff17b4b503e1d3fdff862e516e589a8bfd88c86063a4`;
the PNG staging proof establishes that only boot module slot3 was replaced.
The existing debug log reports native PNG read/decode/present and surface reclaim
PASS. This is the existing PNG desktop demonstration, not heap/V8/browser
execution in the visible window. The heap acceptance used its separate recorded
real guest ISOs. No new desktop image, reset, media refresh or guest rerun was
performed for this check.
