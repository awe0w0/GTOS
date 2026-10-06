# Sparse anonymous VM, implementation slice 2

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

`sparse_vm.h` exposes init, reserve, commit, protect, decommit, release, query,
stats and audit. Discard, reset, trim, split and punch are deliberately absent;
no existing API silently approximates those operations. Unknown permissions or
placement modes fail explicitly. Only supervisor NONE, R/NX and RW/NX exist.

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
- Decommit frees the interval's backing and truly unused paging paths while
  retaining the reservation. Recommit of removed backing starts zero
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
prefix counts verify the temporary extra data/table usage. The unimplemented
suffix-trim portion of the reference trace is not claimed.

## Ownership and bounds

A `vm_handle` contains an explicit space lifetime identity, slot, and generation.
Wrong spaces and stale/released/invalid slot tokens produce different errors.
Each successful reserve increments that slot's generation; a slot at UINT64_MAX
is permanently retired after release. The global space sequence also refuses
wrap. Release does not reset or decrement either identity. Numeric pointers can
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

Reserve scans 64 slots. Protect/decommit/release scan the bounded sparse records
and allocated hierarchy; they never walk absent pages in a TiB reservation.
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
diagnostics and can increase on failure. Flush epochs track actual successful
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
