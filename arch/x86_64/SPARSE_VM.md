# Sparse anonymous VM, implementation slice 3

This module is a bounded, integer-only, BSP kernel service. It adds real sparse
four-level mappings to the experimental arena
`[0xffff900000000000, 0xffff980000000000)` (8 TiB, root indices 288–303).
It does not expose a user ABI, executable mappings, JIT allocator, process
isolation, SMP synchronization, demand paging, or Chromium/V8 execution.

Build the existing foundation/frame targets normally. Enable the VM guest suite
with `VM_TEST=1`, or run `make -f arch/x86_64/Makefile test-vm` for fresh isolated
BIOS ISO acceptance. VM_TEST defaults to zero, preserving the original boot and
frame guest gates and their exact marker/fault sequences. All architecture files
remain outside the i386 source-discovery tree.

## Implemented contract

`sparse_vm.h` exposes init, reserve, commit, protect, discard, decommit, reset,
trim, split, punch, release, query, stats and audit. These are explicit owned
anonymous-memory operations, not aliases for Linux syscall names. Unknown
permissions or placement modes fail explicitly. Only supervisor NONE, R/NX and
RW/NX exist. All output arguments must point to valid private writable kernel
storage, separate from pool/VM state and service mappings. This trusted C
interface does not validate user pointers and is not a syscall boundary.

- Reserve creates one record, independent of byte length, and allocates no data
  or paging frames. Exact placement never replaces an existing reservation.
  Automatic placement searches aligned holes. Zero/unaligned/overflow/outside
  arena requests reject without touching outputs
- Commit R/RW allocates and genuinely zeroes missing data frames through the
  validated pool. Existing bytes remain unchanged, including resident NONE
  backing. Every page in the requested interval takes the requested protection
- Protect NONE clears leaf entries while keeping all data and logical table
  ownership. Protect R/RW allocates nothing and rejects the whole call if any
  requested page lacks backing. An absent leaf is always zero
- Discard is deterministic: each resident R/RW page is eagerly zeroed through
  its privileged alias, preserving its frame and permission; each resident NONE
  page is reclaimed along with genuinely unused table paths. Already-unbacked
  pages remain absent. The output separately reports `zeroed_pages` and
  `released_pages`; their sum is affected resident pages, not the requested page
  count. Zeroing accessible backing does not reclaim its physical capacity.
  Mixed R/RW/NONE/unbacked ranges obey each case independently after whole-call
  validation. The old handle remains live. No lazy-fault or POSIX behavior is
  implied; READ pages remain READ even though the trusted kernel zeros them
- Decommit frees all of the interval's backing, regardless of permission, and
  truly unused paging paths while retaining authority. Recommit of removed
  backing starts zero
- Reset requires the caller's live handle and a contained nonempty page-aligned
  subrange. It destroys backing only in that subrange, retains the entire outer
  reservation, and returns a fresh whole-reservation handle. Old API authority
  is revoked even outside the reset interval. Outside bytes and protections are
  unchanged, and their backing ownership is migrated to the fresh token
- Trim retains exactly the specified nonempty page-aligned subinterval, reclaims
  backing outside it, and returns a fresh handle with the new base and length.
  Prefix and suffix holes become reusable. Keeping the whole interval still
  rotates authority. There is no implicit rounding
- Split requires an interior nonzero page-aligned offset and returns exactly two
  fresh handles in increasing address order. They partition the original range
  exactly; all backing, contents and permissions stay unchanged
- Punch removes a contained nonempty page-aligned interval and its backing.
  `vm_regions.count` is zero for full removal, one for either edge, and two for
  an interior hole. Retained regions are in increasing address order and all
  receive fresh authority. Removed VA is reusable by a different owner. Count
  zero invalidates the original handle and needs no output slot
- Release decommits all backing and invalidates the handle. Shared table paths
  remain live for neighboring owners, including neighbors protected NONE
- Query is trusted kernel inspection, including frame IDs. It is not a safe
  future userspace interface because physical identity is intentionally visible

The experimental first/middle/last test uses the full `0x158fffff000` reservation
without trim, with a 64-page run, one page at offset `0xac00000000`, and one page
at `0x157fffff000`. The exact reference occupancy is 66 data and nine dynamic
table frames. A separate additional commit at the actual untrimmed outermost
page (`length - 4096`) proves zero/write/read and one-past rejection, then
reclaims that extra backing to restore the reference baseline. Independent
prefix counts verify the temporary extra data/table usage. This prior untrimmed
outer-end proof remains recognizable separately from the lifecycle reference.
The lifecycle sequence trims the giant suffix `0xfffff000` to retain exactly
`0x15800000000`, replays the traced 256 KiB commit at offset `0x1000000000`
(64 GiB), and resets an owned 4 GiB interior. Offset `0xac00000000` is deliberately
distant sparse coverage, not the Linux trace's commit offset. Reference ASLR
addresses are not ABI. A separate exact alignment sequence reserves `0x800f000`
bytes at a base with low 16 bits `0x5000`, trims `0xb000` bytes from the prefix
and `0x4000` from the suffix, and retains `0x8000000` bytes aligned to 64 KiB.

## Ownership and bounds

A `vm_handle` contains an explicit space lifetime identity, slot, and generation.
Wrong spaces and stale/released/invalid slot tokens produce different errors.
Each successful reserve increments that slot's generation. Reset, trim, split
and nonempty punch outputs likewise consume fresh slot generations. They prefer
the input slot if its generation is below UINT64_MAX, then distinct reusable
free slots. A terminal input can migrate to spare slots; its terminal slot
remains retired. With no sufficient reusable slots, the entire call fails with
VM_LIMIT before mutation. Unary replacements can succeed when all 64 slots are
live if the input slot is nonterminal; a binary result needs one additional
reusable slot. The global space sequence also refuses wrap. Release and full
punch do not reset or decrement either identity. Numeric pointers can
still alias future reservations; handles prevent stale API authority, not stale
raw kernel pointers.

One platform-owned pool and actual CR3 root are bound to a VM for its boot
lifetime. `frame_pool.service_root` must be assigned by trusted platform setup,
not supplied from an untrusted caller. Init checks the exact root identity,
empty upper hierarchy, free pool and absent prior binding. Production setup
binds `pml4`, checks it again in every architecture context callback, and tests
alternate-root rejection. Creating duplicate pool descriptions of the same
physical memory or inventing a second backend for the same root is outside the
trusted backend contract. Do not allocate from the pool independently once bound.

There are 64 fixed region records and one sparse record per managed physical
frame (at most 2048). A record is either DATA or a PT/PD/PDPT identified by its
level-specific VA prefix. There is never a record per reserved virtual page.
One contiguous commit is limited to 256 pages (1 MiB); at most six new path
frames are required. Static staging holds at most 262 records. Statistics report
VM metadata separately from pool metadata, firmware usable RAM, managed/free
frames, permanent aliases and borrowed bootstrap tables.

Reserve scans 64 slots. Protect/discard/decommit/reset/trim/split/punch/release
scan the bounded sparse records and allocated hierarchy; they never walk absent
pages in a TiB reservation. Discard touches 4096 bytes per accessible resident
frame, bounded by the pool capacity. Metadata replacement stages at most two
output records on the kernel stack; no heap or physical frame allocation is
needed. This fixed temporary storage is separate from `metadata_bytes`, which
reports the retained vm_space object.
Commit's page loop is guarded by the 256-page limit. The implementation favors
simple auditable scans over throughput: audits and some lookups are quadratic
in the finite record cap, not in reservation length.

## Transaction and paging discipline

The service requires the owning BSP, IF clear, normal kernel stack, expected
CR3, CR0.WP, EFER.NXE, and CR4 exactly PAE. No PCID, global mappings, huge pages,
AP execution, interrupts, NMI or exception callers are supported. Both pool and
VM entry have reentrancy guards. Unexpected exceptions remain fatal.

Commit checks the complete token/range, resource counts and remaining frame-ID
epoch budget before allocation. Each actual allocation is fallible and staged;
new tables/data are zeroed by the real pool. Any allocation failure cancels all
staging, leaves outputs/live records/PTEs/contents unchanged, and restores all
free/data/table role counts. Once promotion starts, the operation is no-fail:
no more fallible allocation or recoverable error is allowed. Invariant failure
is fatal. Child table contents and lower-level links precede parent publication.
A real CR3 reload completes every translation-changing success.

Identity-changing operations validate complete input authority/ranges, select
all reusable output slots, and stage complete output regions before changing
any data, translation, or live metadata. `vm_fail_metadata_after(n)` injects at
the Nth actual output-record staging step cumulatively across calls after
arming; setting the hook resets its counter and zero disables it. It fails
once at that index, then counts onward with saturation at UINT32_MAX, so no
counter wrap can re-arm an old failure. Reserve does not use this hook. Tests cover every one-/two-
output staging point, natural slot exhaustion, and terminal-generation
migration/exhaustion. A failed staging attempt consumes no live slot or
reservation generation and leaves the caller's outputs unchanged. No-op trim
and empty-backed reset still require replacement authority. Full punch stages
zero outputs, so a metadata injection cannot fail that release-like operation.

After staging, the operation has no recoverable failure path: removed backing
is unlinked/reclaimed, replacement slots are published, and every surviving
DATA record is moved from the old slot/generation to the correct fresh token.
Table records identify shared VA prefixes and remain owned by the space. Split
changes no translation and needs no TLB flush. A two-sided trim may reclaim its
prefix and suffix in two serialized flush-before-free batches. Under the
single-BSP/IF-clear contract no caller observes the intermediate state; there
is no fallible work after either batch. Caller outputs publish only after the
complete stable-state audit. Invariant corruption is fatal, not partial success.

Decommit clears leaves and unlinks empty tables bottom-up. Logical resident NONE
backing retains a path despite zero leaves. Only after the complete affected
hierarchy is unlinked does the core mark owned pool frames RETIRING. The pool
reclaim operation reloads CR3 before making any frame FREE. This includes
unlinked paging-structure frames, preventing reuse before paging cache/TLB
invalidation. Existing low bootstrap tables and the root are never pool-owned.

The frame context check retains exact low-tree validation. Dynamic upper entries
are accepted only through `vm_owned_hierarchy_valid`, which checks pool physical
identity, generation, owner, role, expected VA prefix and cached alias before
following a table. It verifies exact supervisor NX entry bits allowing only
architectural A/D changes. It calls no frame_pool APIs, avoiding recursive
context/audit entry. During staging and promotion it inspects only reachable
owned dynamic tables; final stable audits additionally prove every live record
is reachable, every table is needed, tokens/ranges are valid and no staging or
retirement remains.

Permanent pool identity aliases remain supervisor RW/NX, including free frames.
Service R/NONE protects the service address, not privileged access through these
aliases. The kernel is trusted; this service is not sandbox-isolation evidence.

## Failure observability and epochs

Rollback preserves externally visible regions, contents, handles, permissions,
owned live backing and physical free capacity. It does not rewind issuance of
internal frame epochs. A staged frame's physical bytes and generation may change
while it is FREE again; no live caller retains its canceled frame ID. Allocation
attempt counts, cumulative operation visits and staging high-water marks are
diagnostics and can increase on failure. Metadata staging attempts are likewise
diagnostic and are not live ownership. Flush epochs track actual successful
translation invalidation, not reservation operations. Statistics intentionally
expose these distinctions instead of comparing whole structs as rollback proof.

Generation availability is checked before staging so a transaction never hits
frame-ID exhaustion halfway through promotion. As with any finite nonwrapping
identity domain, the lifetime epoch budget is finite and consumed even by
canceled staging. That lifetime budget is separate from the managed/free RAM
capacity promised by rollback. No identity becomes valid by counter rewind.

## Evidence and limits

Host tests compile the same selector, frame pool and VM core at O0/O2 with
ASan/UBSan. Their byte-backed platform is explicitly a host fixture, not hardware
proof. It independently models intervals, backed bytes/permissions, distinct
table prefixes and callback-time invalidate-before-free ownership.

Guest tests use only frames selected from the validated real boot map. They
assert actual zero reads and nonzero writes at distant service VAs; exact page
faults on guard, RO, NONE, stale and NX accesses; shared table ownership; complete
reclamation and reuse; reservation quotas; finite real-pool exhaustion; and
every allocation index in fresh/shared/boundary commits. Narrow probe helpers
share the original handler's exact RIP, CR2, error, selectors, RSP and IST checks.
Unarmed or mismatched probes panic; no general page-fault recovery was added.

Source gates independently reject unsafe ownership/publication/unlink/flush
mutations. They supplement the guest checks because an emulator need not retain
a deliberately stale translation long enough for every missing-flush defect to
manifest dynamically. Frozen runner manifests include the rootless-launch helper,
commands, compiler/QEMU versions, ELF/ISO/guest hashes and before/after sources.
Physical hardware, user-mode isolation and SMP remain unqualified.
