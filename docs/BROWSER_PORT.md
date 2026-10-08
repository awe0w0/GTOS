# Native Chromium port: assessed prerequisites

The requested outcome is Chromium executing inside GTOS and rendering real web
pages through GTOS services. A host browser, remote browser session, screenshot
proxy, or set of unimplemented API stubs does not satisfy that outcome.

The kernel/desktop/language gates below completed at `29f6002`. Bounded native
ELF32 execution and process isolation subsequently completed at `b36d0c2`, with
exact-head CI passing. See the [official-upstream and resource assessment](browser-assessment.md).
The small cloud workspace continues kernel development. A separately authorized
larger workspace now builds browser-side reference components; its successful
Linux x86-64 V8 control is not a GTOS execution result. Chromium webpage rendering
in GTOS remains an open acceptance gate.

## Finish the current phase first

- Reviewed English/Simplified Chinese desktop, UTF-8 editing and bounded pinyin
- Durable language/theme preference and application-store recovery verification
- Real bounded AP work queues with separately reported worker readiness
- Clean-checkout builds, module tests, QEMU integration and green CI on `dev`
- Reproducible boot image/source checkpoint with licenses and documented limits

These gates define this phase. They do not imply a production-grade kernel or
that all browser prerequisites below are already present.

## Assessment and architecture decision

The initial assessment established the following continuing requirements:

1. Inspect current official Chromium architecture, supported target/toolchain
   configurations, dependency requirements and maintained source revision
2. Check actual build CPU/RAM/disk capacity and a suitable persistent environment
3. Determine whether the existing 32-bit protected-mode architecture is viable
   for the selected maintained target, or whether a 64-bit GTOS transition is
   required. Do not assume support or silently choose an obsolete browser
4. Produce a concrete platform/API gap analysis and staged executable tests
5. Clarify ownership/publication before creating a separate Chromium fork;
   current project publication is limited to reviewed GTOS `dev` work

## Verified native foundation, and remaining browser gaps

Published checkpoint `b36d0c2` now supplies bounded static ELF32 loading, four
BSP-scheduled CPL3 process slots, private page directories, checked user copies,
validated console/tick/yield/exit calls, guarded kernel stacks, and recoverable
user faults. External ELF probes prove distinct same-address data, preemption,
peer survival and exact deferred reclamation while the desktop and AP workers
continue. The separately compiled application-side ABI probe also executes and
exits successfully. See [native processes](native-processes.md),
[process memory](process-memory.md), and [ELF validation](elf32-loader.md).

Those results are genuine userspace progress, but do not yet provide a native
browser runtime or a POSIX-compatible application environment. The default
profile is integer-only, non-PAE, fixed-layout and deliberately resource-bounded.
It has no hardware NX and is not a completed internet-browsing sandbox. Optional
BSP legacy x87/MMX/SSE ownership is now implemented with strict Bochs state
acceptance and separately qualified QEMU desktop/AP diagnostics; it remains
disabled by default. See [FP ownership and limits](native-fp.md).

Remaining work includes:

- Deliberate final target architecture, toolchain, C/C++ runtime/libc and Rust
  support. ELF64, dynamic loading, relocations, TLS and a stable general ABI are
  not provided by the bounded ELF32 loader
- Broader process lifecycle, wait/handles/resource ownership and native user
  threads; the current scheduler is BSP-only and AP workers execute only bounded
  trusted integer operations, not general user threads
- Browser-target FP/extended-state ownership, user-thread TLS, synchronization,
  clocks, blocking/wakeup,
  event and signal semantics suitable for the chosen browser platform
- Dynamic virtual-memory reservation/commit/unmap/protection and shared memory,
  including safe executable-page/JIT policy beyond immutable first-process images
- A general filesystem, descriptors, directories, random-access I/O, executable
  and asset lookup, persistent profiles and crash-safe file operations. The
  private bytecode application store is not that filesystem
- Network interfaces, sockets, DNS, reliable TCP, entropy, time, certificate trust
  and TLS. The old demonstration network sources are not browser-ready services
- Transferable handles and process IPC with backpressure, cancellation, peer-death
  handling and service boundaries appropriate to the selected upstream design
- A userland window/surface/input API, text/clipboard/composition services and
  robust font coverage. Current GUI and bounded pinyin input are kernel-side
- Enforced browser sandbox policy and security boundaries. Passing native probes
  or an unsandboxed bring-up build does not establish safe public-web browsing

## Independent GTOS platform identity

The requested Chromium adaptation must have its own GTOS target and platform
identity. It may reuse suitable Linux/POSIX implementation pieces through real,
equivalent GTOS services, but must not identify GTOS as Linux merely to select
code paths. Missing operations must remain explicit build/capability failures,
not stubs returning success. Keep the unchanged Linux reference target available
for comparison, and distinguish its results from every GTOS guest test.

Existing native ABI1 is an i386 contract. The experimental x64 kernel-memory
interfaces are not a published syscall or userspace ABI. Browser-side code must
wait for a versioned, guest-tested wire contract rather than guess entry numbers
or pass internal supervisor pointers across the future user boundary. Separate
Chromium-fork publication still requires an agreed destination; current remote
publication remains reviewed GTOS modules on `dev`.

## Next architecture and service gates

The selected next kernel target is a separate x86-64 bring-up, preserving the
existing i386 desktop and its regression suite. This follows the maintained V8
sandbox design and the measured reference workload's roughly 1.35 TiB virtual
reservation. Reservation size is not physical-memory consumption. This choice
does not assume that the Linux binary can execute unchanged in GTOS.

1. A bounded real long-mode boot with validated handoff, four-level tables,
   NX/write protection, exception stacks and actual protection-fault tests
2. Sparse VM ownership: reserve aligned virtual ranges without allocating a
   physical frame or page table per reserved page; commit zeroed pages, protect,
   decommit/release and owned lifecycle changes with rollback and exact accounting
3. Native x64 process entry and checked calls, then user threads with private
   stacks, FP/TLS ownership, blocking/wakeup and safe join/handle lifetime
4. Runtime/loader, filesystem, IPC, networking and userland graphics contracts
   tested by real independently built components before browser integration

The first gate now has a separately built, reviewed [x64 boot target](../arch/x86_64/README.md):
33 real-guest positive/negative cases pass under both QEMU 8.2 and 10. It proves
long mode and strict protection/exception behavior, not a port of the desktop,
x64 userspace or sparse reservation APIs. The target is deliberately BSP-only;
i386 AP workers and their tests continue on the existing target. Its subsequent
[physical frame-pool slice](../arch/x86_64/FRAME_POOL.md) now owns a bounded real
boot-map-derived RAM pool with exact exclusions, zeroing, generation/role checks
and reclamation tests. The next [sparse VM core](../arch/x86_64/SPARSE_VM.md)
implements kernel-only reserve/commit/protect/decommit/release with actual
page-table backing, hardware fault tests and transactional failure cleanup.
It proves the roughly 1.35 TiB reservation using bounded sparse metadata, not
that amount of RAM. This is still supervisor-only, NX-only and BSP-only, with
one bound pool/root and a 256-page commit limit. Discard/reset/trim/split/punch
now have distinct implemented contracts and real guest acceptance, including the
exact reference suffix trim and sparse 4 GiB reset. These remain trusted kernel
operations. [Private x64 userspace](x64-user-runtime.md), threads and the
browser-facing wire ABI remain open.

The real frame backend is now separated from guest test code with atomic,
fixed-root initialization and an invariant-checked published binding. This is
the behavior-preserving prerequisite for a registered ownership manager; it
does not yet permit multiple private roots or user-mode service calls.

## Staged browser proof

Each stage must run real guest code and have a reviewable acceptance result:

1. Native userspace ABI and isolation tests; a crashing test process leaves the
   desktop and another process alive
2. Filesystem, threading, virtual-memory and IPC conformance tests sufficient for
   the selected browser components
3. Network/DNS/TLS and local deterministic HTTP fixture tests inside the guest
4. Minimal supported Chromium executable/component brought up in GTOS
5. Real renderer/browser integration with guest input and display surfaces
6. Real webpage navigation tests, deterministic local fixtures first, then
   appropriately bounded public-page tests with security limitations stated

The assessment must decide ordering and achievable scope using current upstream
facts. This document records dependencies and proof requirements, not a promise
that changing platform conditionals is sufficient to produce a working port.

## Current native IA32 runtime diagnostic

The active browser target is native 32-bit Chromium on GTOS. The x64 gates above
remain separate architecture evidence. The [native runtime diagnostic subset](../apps/native_v8_runtime/README.md)
now preserves the qualified VM-backed heap, new/delete, emutls, C++ TLS destructor
registry, monotonic clock bridge and 17 libc++ single-task ABI helpers. Its private
ABI requires an explicit opt-in; headers for unavailable services do not supply
implementations.

Two independent QEMU/official Bochs cohorts each completed eight cold boots,
32 task cases and 40 exact Reap checks. The standalone builder must reproduce
all eight runtime objects byte-for-byte before reusing that qualification.
No production kernel capacity or image is changed by this module.

Condition variables, native shared-address-space threads and TLS keys, complete
libc++ out-of-line services, Abseil OS services and the actual GTOS V8 platform
remain open. Full V8 Isolate, Chromium navigation, video and HTML5 guest
acceptance remain false. Full engine memory and stack requirements must be
measured before changing production capacity.
