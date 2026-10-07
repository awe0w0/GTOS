# Native IA32 data-page VM, ABI v1

This additive interface lets a real CPL3 process reserve virtual data regions
and change their resident pages at runtime. It retains the original sealed ELF,
stack and guard mappings, supervisor template, BSP scheduling and 256-page total
resident budget. It does not provide hardware NX, user threads or a complete V8
runtime. The pinned upstream V8 PageAllocator consumer is accepted through real
CPL3 data-page calls; this memory leaf does not establish browser execution.

## Wire

`include/process/vm_abi.h` defines independent version 1. Calls use native
`int 0x80`: EAX=operation, EBX=request pointer, ECX=exact descriptor byte count.
EAX returns zero or a signed negative error; other general/segment registers are
preserved. Virtual bases can exceed `0x80000000`, so they are returned through a
fully checked user output structure rather than EAX.

| Operation | Call | Request fields after version | Bytes |
|---|---|---|---|
| Reserve | 0x470b | length, alignment, hint, result | 20 |
| SetPermissions | 0x470c | handle, offset, length, protection | 20 |
| Decommit | 0x470d | handle, offset, length, protection=0 | 20 |
| Release | 0x470e | handle, length=0 | 12 |
| Trim | 0x470f | handle, retained length | 12 |
| Discard | 0x4710 | handle, offset, length, protection=0 | 20 |
| Query | 0x4711 | handle, result | 12 |

Reserve output is `{version,handle,base,length,page_size}`. Query output is
`{version,handle,base,length,resident_pages}`; both are exactly 20 bytes. Requests
are snapshotted after wire-size validation, and version is checked before any
operation. Reserve/Query validate the complete writable output range before
semantic processing. Input/output overlap is permitted: later logic uses the
kernel snapshot. No user PID, CR3, frame address or retained pointer is accepted.

## Ownership and page semantics

Regions occupy `[0x80000000,0xBFFFC000)`, excluding the static user stack and its
guards. Reserve detects existing static pages and other reservations. A nonzero
hint is exact; callers wanting an advisory hint may retry with zero. Length is a
positive 4096-byte multiple; alignment is a power of two at least 4096. There are
32 region slots per process. Handles are positive global monotonic IDs and never
reused after release, fault, cancellation or reap. Reserving 16MiB consumes no
physical frame. Static ELF and stack pages still count toward the 256 total.

Permissions are NONE=0, READ=1 and READ_WRITE=3. R/RW commits holes with freshly
zeroed pages; NONE removes access but retains committed frames and contents.
Decommit releases resident frames while retaining the virtual reservation;
subsequent SetPermissions(R/RW) allocates zero pages. Discard requires every page
in the requested range to be resident and eagerly clears its contents, retaining
permissions and frames. Recommit after discard must not be confused with access
restoration after decommit. Trim retains a positive whole-page prefix no larger
than the old size and releases the tail. Release retires the complete region.

Runtime mutations require the sealed address space's own CR3 on the verified
BSP, with interrupts masked. Loader MapNewPage/ProtectPage/UnmapPage retain their
original unsealed/kernel-CR3 requirement. Page provenance prevents a runtime call
from mutating ELF/stack/borrowed mappings. All allocations and alias checks finish
before publishing a multi-page permission update. Object-owned transaction
scratch avoids large kernel stack arrays. Allocation failures reclaim new data
and table frames without changing existing bytes, PTEs or resident accounting.

Present status and frame ownership are tracked separately. Layout validation
checks NONE frames even when their PTE is absent. Every changed leaf mapping is
invalidated in the current CR3 with `invlpg`; data and last-table frames are not
freed before invalidation. No AP enters a user directory, so this interface does
not imply SMP TLB shootdown. Exit/fault/cancellation still defer physical teardown
until NativeRuntime removes scheduler references and reaps under kernel CR3.

EXEC/RWX requests fail. Non-PAE IA32 has no NX bit, so R/RW data remains executable;
software refusal is not hardware W^X. Full V8/browser backend guards stay closed
until the remaining runtime services are implemented and accepted.

## Current evidence

The initial implementation is based on dev
`dfa91e88bd19deb1ebae25775244349ea8d140cc`. GCC13 O0/O2 real ELF32 host executions
pass 18,021 checks each, using actual ProcessAddressSpace and the actual physical
allocator; only hardware registers and invalidation observation are substituted.
The matrix covers 16MiB sparse reservations, three committed pages, NONE versus
decommit, trim/release/Destroy, static/guard/overflow/foreign/stale rejection,
256 resident and 32 region limits, plus every failure stage of transactions
requiring three, four and five frames, including two new PDE tables.
Evidence: `artifacts/dynamic-memory-host-20261008-02/status.json` in the workspace.

Production O0/O2 scalar compilation has a maximum individual function frame of
144 bytes and real `invlpg` instructions. Both complete production kernel audits
passed, including 61 objects and 66,521 decoded instructions in the independent
O0 build. The O2 kernel SHA256 is
`0082390cd69ebf2605a8ff17b4b503e1d3fdff862e516e589a8bfd88c86063a4`.

The unchanged V8 `src/base/page-allocator.cc` at
`be042d4462bee463c9b785701b8c6ee4576ee0a3` is compiled for
`i686-unknown-none-elf` with genuine pinned libc++/LLVM libc declarations and the
GTOS OS memory bridge. All seven Clang `-Oz` native ELF variants passed actual
ELF32 admission, complete scalar audit, pinned source/dependency closure and
complete linked machine-code stack proof. The positive ELF is 12,576 bytes and
uses seven image pages plus two stack pages. Its complete stack bound is 388
bytes, with 7,788 bytes of headroom; all 36 virtual call sites have exact targets.
Fault variants have 332--336-byte bounds. Merged strings require exact original
bytes and their real pointer relocations, and constructor/object identity is
proved through actual control-flow reaching definitions. Unknown instructions,
unowned executable bytes and unresolved virtual targets fail qualification.
Static evidence: `artifacts/v8-page-final-seven-20261007T193816Z/`.

The final six actual guest boots use production GCC13 kernel/raw probes at O0
and O2, each with 32MiB/1CPU, 64MiB/4CPU and 128MiB/1CPU. The same seven qualified
Clang `-Oz` upstream consumers run in every boot, alongside seven raw programs:
84 real CPL3 cases, all successful. This does not claim O0/O2 V8 consumer builds.
Each case verifies CS=0x23, private CR3, physical accounting and complete deferred
reap. RO writes fault with low error bits 7; raw NONE reads use 4, upstream NONE
writes use 6; decommit/release/tail reads use 4 at their exact expected CR2.
Normal exit and RequestExit retain and verify all 12,288 committed bytes before
reap. Peer, kernel-task and boot progress continue throughout. Evidence:
`artifacts/native-vm-final-20261008-01/status.json`.

Three fresh production PNG desktop boots also passed: 64MiB/4CPU, Chinese
32MiB/1CPU and 96MiB/4CPU. They compare all 256 source pixels and 9,216 displayed
framebuffer pixels, exercise Esc/title close, reopen and reclamation, and verify
peer/AP, desktop input, language persistence and complete private disk equality.
The PNG ELF is byte-identical to the earlier accepted module. Its complete stack
bound is 2,840 bytes. The 421-file source snapshot describes that frozen guest
window; final documentation/CI and the separately qualified V8 stack auditor
are recorded as later changes, without rewriting the original evidence.
Evidence: `artifacts/native-ia32-vm-png-guest-20261008-b/results.json`.

CI runs the raw production VM matrix through `tests/native_vm_smoke.sh` with a
fresh artifact directory. Supplying the seven qualified upstream ELF paths in
mode order additionally runs the actual V8 variants; building the pinned SDK is
a separate explicit qualification step. The full V8 GN backend remains closed.
A general C/C++ heap, native Chromium browsing, video playback, HTML5 browser
features, executable memory and hardware NX remain separate acceptance work.

Reserve does not change output mappings between prevalidation and copy while IF
is clear. Its defensive post-reserve copy-failure path releases the reservation
but keeps the already-issued global handle retired. It does not claim handle
sequence rollback after an internal mapping/alias invariant failure. Ordinary
invalid output descriptors are rejected before reservation/handle issuance.
