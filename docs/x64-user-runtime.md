# Planned x64 user-runtime gates

Status: engineering plan, not an implemented x64 userspace ABI. The existing
i386 native processes and ABI1 remain supported separately. The x64 boot,
physical-frame and sparse-VM services are real but currently supervisor-only,
BSP-only and integer-only. They do not execute V8 or Chromium.

The next coherent target is two isolated static ELF64 processes with private
CR3 roots, checked calls, bounded anonymous user memory, real timer preemption,
contained user faults and deferred reclamation. Entering CPL3 alone does not
complete this target. Maintain the i386 desktop, Chinese input, application
storage, game and native-process regressions throughout this work.

## Implementation order

1. Extract the existing real frame platform from guest tests without broadening
   its accepted CR3, stack, BSP, interrupt or control-register context. Retain
   the exact root binding, low-tree validation and actual CR3 flush. Keep this
   mechanical refactor separate from new ownership semantics
2. Introduce one authoritative pool manager and registered, nonwrapping space
   identities. Preserve the old single-VM behavior through an audited adapter
   before adding multiple owners. Account for every live/staged/retiring frame
   exactly once; add an explicit PML4 role and pinned borrowed-table lifetime.
   Do not obtain multiple spaces by simply deleting exclusive-pool checks
3. Construct private process roots and owned user mappings while running on the
   trusted kernel root and registered service stack. Separate construction,
   active/inactive, dying and dead states. Reclamation must be owner-scoped and
   prove the old root/stack and every prepared return reference are abandoned
4. Add user descriptors, per-thread guarded kernel stacks, TSS.RSP0 ownership,
   a sealed runtime IDT, integer entry/return assembly and checked return frames.
   Start with a DPL3 interrupt gate and IRETQ; fast-entry instructions remain
   disabled. Ordinary runtime traps must not suspend work on a shared IST stack
5. Validate/load bounded static ELF64 images and expose an explicitly versioned
   experimental GTOS wire contract. Add actual timer-driven preemption before
   claiming isolation from an uncooperative infinite loop
6. After this complete slice, implement shared-space threads, TLS/FP ownership,
   monotonic wait/wake and teardown semantics, then the larger runtime and
   browser-facing services. Each requires its own guest conformance gate

## Proposed layout, not a stable ABI

Use a private PML4 and a private boundary PDPT per process. Borrow only the
unchanged supervisor-only low 1 GiB subtree. User space can then occupy
`[0x40000000, 0x0000800000000000)` without elevating permissions in a shared
ancestor. Private ancestors permit user access; the borrowed subtree retains
its supervisor restriction. Kernel code, descriptors, entry/service/emergency
stacks and pool aliases must remain identical supervisor mappings in every root.
The existing high-half kernel sparse arena is not blindly copied into userspace.

This design adds one boundary table instead of forcing every program above
512 GiB. It must be proven with attacks against all low supervisor leaves,
every pool alias, the 1 GiB boundary and corrupted shared-table metadata. The
end-exclusive upper bound is a numeric sentinel, not a canonical pointer or
valid RIP/RSP. All complete intervals, rounding and additions require checked
arithmetic; the final user page needs explicit positive and negative tests.

With PCID, global pages and SMP absent, actual CR3 transitions can form the
inactive-root invalidation proof. A function named "flush" is not that proof.
Future PCID/SMP work needs a new shootdown and active-CPU protocol. These paging
and privilege rules are grounded in the [Intel system-programming manual](https://cdrdv2-public.intel.com/929359/253668-093-sdm-vol-3a.pdf).

## Loader and browser-toolchain handoff

The first executable profile should be little-endian ELF64, EM_X86_64,
ET_EXEC, page-disjoint RX/R/NX/RW/NX loads, a bounded GTOS vendor note and an
explicit entry record. Reject dynamic loading, interpreter, TLS, runtime
relocations, unresolved dependencies, executable stack, overlapping pages and
W+X. Parse all file/virtual ranges before allocating; zero BSS and padding;
publish the process only after the complete image is ready. The
[ELF program-header specification](https://gabi.xinuos.com/elf/07-pheader.html)
defines the container, not a Linux runtime contract.

Coordinate a freestanding integer-only probe with the independent GTOS
`none-elf` x64 toolchain. Link an initial fixture near 1 GiB and also test an
explicit greater-than-4-GiB variant to catch pointer truncation. Inspect actual
ELF headers, relocations, code and stack alignment, then run the exact bytes in
the guest. A relocatable platform-identity object or successful post-link check
does not establish executable support.

No syscall numbers or shared structure layouts are assigned by this plan.
The reviewed implementation and probe must share fixed-width, size/versioned
wire definitions and reject unknown required features. ABI1 remains i386-only.
Return only implemented capabilities and actual resource limits. Preserve
`IS_GTOS` independently from Linux and fail clearly for unsupported operations.

## Required containment and lifetime proof

- Copy user input only after validating its complete logical permissions,
  owner, backing and actual page-table path; use bounded kernel snapshots.
  Validate output before side effects and reject operations that revoke their
  own output storage. Do not expose physical identities or kernel pointers
- Sanitize selectors, flags, RIP/RSP and the owned return descriptor before
  returning. Keep kernel/return-window corruption fatal; contain only the
  explicitly supported user-fault cases. No generic fault-skipping recovery
- Demonstrate same-VA private data, real user R/NONE/NX faults, supervisor-alias
  denial, cross-page checked copies, stale/foreign handles, zero reuse, all
  allocation failures and complete cleanup after switching away from victims
- Keep untrusted user output unambiguously framed apart from trusted kernel
  acceptance records; user-printed PASS text must never satisfy the harness
- Preserve all GPR/flag/stack contracts through real timer interrupts. One
  looping or faulting process must leave its peer progressing. APs remain parked;
  multiple configured virtual CPUs do not constitute SMP scheduling proof
- Qualify the timer backend explicitly. An xAPIC timer requires actual base/mode,
  register, routing and effective UC mapping checks, including PAT/MTRR policy.
  A PIT/PIC backend needs its own x64 routing and stack proof. Uncalibrated ticks
  must not be reported as nanoseconds or milliseconds
- Keep FP disabled until an independently reviewed x64 profile owns x87/MMX,
  MXCSR and all XMM0–15. Do not transplant the i386 pointer-repair format or
  enable OSXSAVE/AVX without owning the corresponding state. TLS, wait/wake,
  address reuse, cancellation and join each have additional lifetime obligations

The current 2,048-frame pool manages only 8 MiB and cannot host a browser.
Capacity expansion, general storage/files, IPC, network/TLS, libc/C++ runtime,
JIT policy, x64 graphics/input and browser sandboxing remain explicit work.
Supervisor page isolation is not a claim of speculative-side-channel safety or
physical-hardware qualification. Preserve full host/source/guest gates and
exact artifact identities for each implementation stage.
