# Bounded x64 physical-frame ownership, slice 1

This module extends the separate BIOS/GRUB x64 test target. It does not alter the
ordinary i386 build, desktop, runtime disks, or any code under `src/`. It is a
BSP-only, IF-off kernel service exercised with QEMU TCG, not userspace isolation,
SMP, a virtual-memory reservation layer, or a V8/Chromium port.

The original seven foundation protection probes run first and retain their exact
fault sequence. Pool initialization and its tests follow the foundation marker.
The old marker alone does **not** establish a frame-pool pass. Final success is
`X64 FRAME POOL PASS BSP-only managed=...` and debug-exit status 33.

## Physical ownership policy

`frame_pool_init` invokes `boot_memory_select` on the actual private resident
Multiboot2 copy. There is no production initializer accepting a caller-selected
free list. The private bytes and retained exclusions remain immutable during
validation and selection; the BSP is the only executing caller and IF is clear.
The handoff view is internal trusted state, not a hostile-caller ABI.

The entire copy is validated before any selection is published. The memory map
must have one version-zero tag, whole nonempty/nonwrapping entries, and no overlap
between **any** entry types. One type-1 interval must cover the complete kernel.
Reserved header/entry words remain ignored per the specification. Unknown tags
are skipped without following their addresses; unknown RAM types are unavailable.
An EFI boot-services-not-terminated tag is rejected. This target remains BIOS.

Module endpoints must be ordered, and a bounded NUL-terminated command string
must fit the tag. The framebuffer common prefix, type-specific fields, pitch,
width, height, address and checked length are validated before deriving its range.
Duplicate framebuffers, overlapping module/framebuffer payloads, or boot payloads
overlapping kernel/original information are refused as ambiguous. The indexed
framebuffer palette uses the uint16 count in GNU's `multiboot2.h` wire layout.

Only whole available pages in `[1 MiB, 64 MiB)` qualify. Available edges round
inward; exclusions round outward. The complete page-rounded kernel covers all
static metadata, boot tables, private information, descriptors, stacks and guards.
Original information pages, validated modules, framebuffer storage and up to 32
explicit retained ranges are excluded too. ACPI, NVS and every non-type-1 page
remain unavailable. This address window is below the minimum physical-address
width of the supported long-mode processors, not a claim about all machine RAM.

The default selects the first at most 2,048 eligible frames (8 MiB) in ascending
physical order and requires at least 512. Explicit test-only requests can lower
those bounds. Firmware usable bytes, eligible full frames, managed frames and
currently free managed frames are distinct quantities. Unselected RAM is unmanaged.
No firmware total is treated as an allocator free count.

## Mappings and context

Initialization preflights **all** selected PTEs for exact zero before writing any.
Existing present mappings and software-owned nonpresent values are conflicts.
Only selected frames get permanent identity aliases, each supervisor RW/NX with
no user/global/large-page bit. The existing root/PDPT/PD/32 PTs are borrowed from
the kernel image: 35 boot-owned paging frames, never pool allocations.

The real architecture backend checks BSP, CPL0 selector, normal kernel stack,
IF clear, CR3 identity, CR0.WP/PG/EM/TS, EFER.LMA/LME/NXE, CR4 exactly PAE, and the
whole expected borrowed hierarchy. AP/interrupt/exception callers and PCID/PGE/
LA57 contexts are outside the contract. There is a non-reentrant pool guard.

After leaf installation CR3 is reloaded, then every alias is audited allowing
only architectural A/D changes. A failed post-install audit clears all newly
installed aliases and reloads CR3 before resetting initialization state. No frame
is exposed from an unsuccessful initialization. Guest tests compare every low
leaf with the original baseline, allowing only the selected alias delta.

Aliases remain resident even while frames are FREE. They are privileged kernel
implementation access, not proof of sandbox protection. Later service R/NONE
permissions cannot constrain a trusted kernel using these aliases. No executable
allocation exists. Static pool metadata is itself within the excluded kernel.

## Ownership and lifetime

Allocation accepts only STAGED_DATA or STAGED_TABLE and a nonzero owner identity.
It returns a physical address plus non-reused generation, after bytewise zeroing
through a checked alias. Generation exhaustion is explicit and never wraps.
Promotions are STAGED_DATA→DATA or STAGED_TABLE→PT/PD/PDPT. Data and every dynamic
table role consume the same finite pool, so tables cannot disappear from accounting.

Cancellation frees only staging. The owner must first unlink any external live
mapping before retiring a DATA/PT/PD/PDPT frame. RETIRING cannot be allocated or
returned by the checked alias API. Reclaim reloads CR3 **before** any RETIRING
record becomes FREE. At this slice no service mapping exists; future VM callers
must implement unlinking and transaction publication. The pool cannot inspect an
arbitrary future caller's mappings or recover leaked raw privileged pointers.

Every public operation checks context and bounded metadata/alias invariants.
Every free/retire/alias checks physical membership, generation, owner and role.
Outputs are unchanged on failed allocation/query/reclaim calls. Private pool
initialization state is reset on failed selection/alias publication; boot-error
text may be returned. Allocation failures can zero an unowned frame only if a
trusted backend fails outside its contract; no live owner's bytes are touched.

The stable accounting equation is managed = FREE + STAGED_DATA + STAGED_TABLE +
DATA + PT + PD + PDPT + RETIRING. Tests independently count records and require
staged/retiring counts zero at stable cleanup. No heap, AP allocator, atomic SMP
protocol, userspace handles or high virtual reservations are implemented.

## Acceptance commands and what they prove

Run from the repository root:

```
make -f arch/x86_64/Makefile test-host
python3 arch/x86_64/tests/frame_qemu.py --output /tmp/gtos-frame-new-evidence
python3 arch/x86_64/tests/boot_qemu.py --output /tmp/gtos-boot-new-evidence
```

The runtime tools can be selected with `--runtime`; direct ISO builds accept
`GRUB_MKRESCUE`. Every evidence directory must be new. Both runners use only their
new ISO, no host disks or existing guest images, and `-nic none`.

Host suites compile the same parser/selector/pool code under ASan/UBSan at O0/O2.
They cover maximal buffers, deterministic malformed mutations and independent
selection oracles; ownership, context, alias rollback, exact accounting, byte
zeroing, all roles, failure injection, generation retirement and recovery. A
source gate checks the real flush/control-flow contract; emulator TLB behavior
alone cannot prove an intentionally missing flush always fails.

The frame matrix runs O0/O2 at 4/16/32/64/256/512 MiB with max/qemu64/Nehalem, pc/q35
and 1/2/4 vCPUs (APs remain parked). The 4 MiB configurations select the actual
smaller firmware-derived capacity, while still requiring the default minimum.
Seven-frame reduced pools exercise bounded
resource exhaustion, not all-machine OOM. Actual GRUB module payloads of 5,003
and 8,221 bytes exercise payload exclusions. Guest parser fixtures are identified
as synthetic and never become accepted free-frame sources. Actual alias collision,
rollback and recovery fixtures select from the original real boot map.

Guest tests read and write every managed frame, prove zero-before-use and zero on
physical reuse, verify distinct contents, consume all real pool frames in shared
data/PT/PD/PDPT roles, reject double/foreign/reserved/stale/wrong-owner frees, and
require exact cleanup. They test failure at every position of a seven-allocation
staging sequence and explicit corruption of zeroing, counters, NX and U/S. These
are pool/staging tests, not a transactional sparse-VM implementation claim.

The separate original 33-case foundation gate must still pass. Final evidence
records source before/after manifests, exact commands, compiler/emulator versions,
ELF/ISO/log SHA256, exit results and complete guest logs. Physical hardware,
UEFI, AP use, and userspace isolation remain unqualified.

## Fixed real x64 backend

`frame_platform.c` owns the production `x64_frame_platform`, the boot-root
accessor, and the count of actual CR3 reloads. Its hardware context, low PT
read/write, identity alias and flush operations are the original backend moved
out of `frame_guest.c`. BSP, IF-clear, the normal boot stack, exact boot CR3,
CR4=PAE, WP/NXE and every static low-tree restriction remain unchanged. VM_TEST=0
still rejects every nonzero upper root entry. No alternate stack/root, user mode,
FP or SMP capability is added.

The optional VM_TEST build has one one-shot `x64_frame_vm_init(pool, space)`
operation. It accepts only a ready, idle, audited, entirely free pool using the
exact production backend and an unready, idle VM, with no prior service owner or
root. It initializes those resident kernel objects using unchanged `vm_init`
and binds them to the fixed boot PML4. There is no root parameter, callback
registration, reset or replacement. The temporary root required by the core is
restored to zero on any initialization error; the private binding is published
only after core initialization succeeds. Success therefore returns a ready VM,
with no externally exposed pre-ready binding. Core alternate-root rejection and
ownership semantics remain unchanged. Context directly checks the bound pool,
fixed root, exact VM and pool ready==1 states, service owner and original non-recursive
owned-hierarchy audit. A published binding always requires all those identities,
even when its dynamic tree is empty; clearing ready or owner never downgrades
it to an unbound platform.

The guest harness retains priming, snapshots, negative injections and unchanged
acceptance markers. It checks null/state/backend/context rejections, corrupt and
nonempty pool rollback after temporary-root publication, existing core
alternate-root rejection, successful initialized fixed-root binding, duplicate
initialization rejection, cleared VM ready/owner independently and together, cleared pool readiness, and
restoration after deliberately corrupted pool/VM-root/service-root/owner fields. Source gates retain all original rejected mutations and add
focused gates for initialization/binding publication, rollback, immutable root
selection and backend consumption. These are extraction and regression tests,
not user-runtime evidence.
