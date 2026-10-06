# GTOS engineering roadmap

GTOS currently provides a tested educational 32-bit x86 operating system, with
a separately tested x86-64 boot/protection foundation for later browser work.
This roadmap separates verified capabilities from development targets. Work is reviewed and committed to `dev`;
`main` is not changed automatically. No prototype is described as production-ready.

## Acceptance rules for every module

1. Preserve bootability through GRUB Multiboot v1 and inspect compiler diagnostics.
2. Add deterministic tests for valid and invalid inputs, state transitions and limits.
3. Boot the integrated system under QEMU; verify actual input and visible output.
4. Check interruption, repeated operations, cancellation and recovery where relevant.
5. State unsupported hardware, missing isolation and known failure modes precisely.
6. Self-review the diff, run independent review where practical, then commit to dev.
7. Verify the remote commit after pushing; only report CI that actually ran.

## Phase 1: safe and observable foundation (accepted checkpoint)

- Correct early boot ABI/GDT/IRQ handling and keyboard/mouse event routing
- Bounded physical frame allocator from firmware memory map; validated heap
- CPUID/firmware CPU discovery, honest detected/online status and BSP task lifecycle
- Timeout/error/bounds-checked ATA PIO; explicit dedicated app-store volumes
- Checksummed app protocol, bounded bytecode runtime, install/list/remove/reinstall
- Graphical desktop, app list and externally packaged playable game
- Repeatable build, host regression tests, QEMU acceptance automation and logs

Gate: 32/64/128 MiB and one/four-vCPU guests boot; fault-free allocator/scheduler
self-tests; install and removal persist across reboot; keyboard/mouse launch and
play a packaged game; invalid package and unformatted disk are refused safely.

## Phase 2: kernel isolation and real multicore execution

- Page tables, explicit virtual address ownership and guarded kernel stacks
- Exception diagnostics, executable/data permissions and fault containment
- Reentrant interrupt entry, LAPIC/IOAPIC discovery and interrupt routing
- Safe AP trampoline/INIT-SIPI startup, per-CPU GDT/IDT/stacks and online handshakes
- Locking primitives, per-CPU queues, migration/affinity, sleep/wakeup invariants
- Stress tests across one/two/four/eight vCPUs and low-memory/error injection

Gate: independently counted AP work, timer-driven scheduling on each online CPU,
no shared interrupt-frame state, tested timeouts/offline fallback, and a crashed
application cannot corrupt the desktop or another process. Detection alone does
not satisfy this phase. The phase-1 scheduler deliberately runs only on the BSP.

## Phase 3: storage and application services

- Block-device abstraction, read cache/writeback policy and proper flush semantics
- Partition discovery and a documented filesystem with crash-recovery tests
- Files/directories, file descriptors, per-process resource limits and syscall ABI
- ELF userspace loading, versioned manifests, dependency/capability model
- Transactional upgrades, application data separation and recovery/uninstall flows

Gate: interrupted writes do not destroy previously committed files; malformed
media/apps fail closed; external applications run without recompiling the kernel.

## Phase 4: modern desktop

- Linear framebuffer modes with scalable typography, composition and double buffer
- Window manager with focus, moving/resizing, minimize/maximize and keyboard access
- Launcher/search, application/task list, terminal, file manager and settings
- Notifications, modal flows, shortcuts and accessible focus/contrast
- Persistent user settings, session restoration and application lifecycle control
- Input latency/frame-time profiling and screenshot-based interaction regression

Gate: repeated/interrupted multi-window workflows behave predictably, apps remain
responsive while other tasks run, and a visual/interaction review covers all flows.

## Phase 5: hardware breadth and reliability

- ACPI power/reset, RTC/timekeeping, richer PCI inventory and supported device matrix
- AHCI/virtio block, networking lifecycle and safe packet parsing
- SMP/allocator/I/O fault injection, soak tests and reproducible release images
- Installation/recovery documentation and reproducible release verification

## Current deliberate limits

The original phase1 checkpoint did not provide native process isolation. The
subsequent bounded ELF32/CPL3 milestone adds checked private user address spaces
and user-fault containment, but not a full filesystem, POSIX runtime, stable
general application ABI or production compositor. The tiny VM restricts application operations but
is not equivalent to hardware-enforced process isolation. ATA PIO support does not
imply AHCI/NVMe support. Firmware-reported processors are not automatically online.

## Language milestone (requested 2026-10-06, accepted bounded implementation)

- Versioned English/Simplified Chinese string catalogs and persisted preference
- Strict UTF-8 decoding and codepoint-aware editing, clipping and search
- Licensed Chinese glyph coverage for every localized label and bundled game prompt
- Bounded pinyin composition/candidates with explicit supported vocabulary
- Reboot persistence, malformed-input and actual Chinese-pixel/keyboard acceptance

This stage does not claim a complete system IME, unrestricted CJK typography or
translation of arbitrary third-party applications before those are implemented.

## Active next project: native Chromium prerequisites

After the active kernel/desktop/Chinese phase gates, assess a real native Chromium
port and required GTOS APIs. See [browser prerequisites and proof gates](BROWSER_PORT.md).
The current checkpoint is complete. The [assessment](browser-assessment.md)
records the cloud full-build storage limit, separately built Linux V8 control,
architecture decision, and completed bounded CPL3 process/isolation proof.
Chromium itself has not yet run in GTOS.

## Current phase checkpoint: kernel workers and localized desktop

The foundation, kernel paging, modern desktop and bounded language implementation
are tested together. APs now perform bounded queued integer work with verified
results, rather than merely being discovered or parked. Per-worker private IDTs,
shared sealed page tables and wake IPIs are implemented. One failed worker can be
contained; this is not isolation for arbitrary native applications.

This checkpoint does **not** close phases 2–5. Subsequent bounded ring-3 support
is described below. General AP scheduling, a filesystem, terminal/file manager,
session restoration, broader hardware support and browser-facing services remain open. The queued browser
assessment must turn those dependencies into executable milestones rather than
assuming a desktop screenshot means a browser platform is already available.

## Native-process prerequisite milestone

Private process page tables, bounded checked copies, TSS/segment-safe interrupts,
BSP native preemption and syscall dispatch, ELF32 load-plan validation, and
transactional native-image admission are implemented as a first i386 proof.
External boot-module programs exercise same-VA/private-data isolation, a contained
kernel-write fault, surviving peer execution, and exact deferred reclamation
while the existing desktop and AP workers continue. This milestone preserves the
existing i386 build; it does not settle the final browser target architecture.

Optional BSP legacy x87/MMX/SSE ownership has a separately documented
[multi-engine acceptance](native-fp-verification.md) and remains disabled by
default. Remaining native-runtime work includes browser-target FP/extended-state
ownership, richer VM and syscalls, threads/TLS, files/IPC, and userland surfaces. The current non-PAE scheme
has no NX protection; fixed layout and four bounded process slots are explicit
limits rather than a general-purpose browser platform.

## Separate x86-64 browser-foundation track

The measured sandbox-enabled Linux V8 control reserves roughly 1.35 TiB of
virtual space. This motivates a separate x86-64 target rather than promising a
maintained browser within the current i386 arena. Keep the working i386 build,
desktop/language/game and native-isolation suite throughout the transition.

- First gate: validated real long-mode boot, four-level mappings, NX/WP, own
  descriptor/exception stacks and strict actual protection-fault tests
- Next gate: sparse aligned reservation without proportional RAM/page-table
  allocation; zeroed commit, permission changes, decommit/release, rollback,
  stale-access faults and resource-lifetime conformance
- Then: real x64 native loading/entry, per-thread FP/TLS, scheduler wait/wake,
  join/handles and cross-process ownership before component-runtime integration

A successful Linux control, standalone x64 kernel boot, or JavaScript-engine
bring-up does not close the browser rendering/navigation/security gates.

The first x64 boot/protection gate is implemented in
[`arch/x86_64`](../arch/x86_64/README.md), built only by its separate Makefile.
Both QEMU 8.2 and 10 pass 33 strict cases, including actual NX/WP/stack-guard
faults, rejected malformed handoffs/control states and a real double fault on
its dedicated emergency stack. Its parser is sanitizer-tested. It remains
BSP-only with no timer service, userspace or desktop. A subsequent
[bounded physical frame service](../arch/x86_64/FRAME_POOL.md) now derives real
owned RAM from the validated boot map, excludes live boot allocations, and
proves zeroing, exhaustion/reuse and flush-before-free. It manages at most
2,048 frames (8 MiB) below64 MiB, with privileged RW/NX aliases. This is not
giant virtual reservation or an SMP/userspace allocator. The sparse-VM core below
and subsequent browser contracts build on this service.

## Chromium platform identity requirement

Chromium must gain an independent GTOS target/platform, even where audited
Linux/POSIX code can be reused through equivalent implemented services. Preserve
the Linux reference; do not disguise GTOS as Linux, invent syscall success, or
present a host control as guest execution. Stabilize actual x64 user ABI and
capabilities before binding browser-side code to kernel internals.

## Sparse VM core checkpoint

The [bounded x64 kernel service](../arch/x86_64/SPARSE_VM.md) now implements
aligned/exact reserve, zeroed commit, content-preserving R/RW/NONE protection,
decommit and release. Huge unbacked reservations use no proportional frame or
page-table allocation. Real guest tests cover distant and true outer-end pages,
14 exact hardware faults, allocation failure at every staged point, shared table
paths, stale/foreign handles, and complete reclamation. No untrusted-user or SMP
claim follows: the service owns one supervisor root/pool and never maps executable
pages. Lifecycle additions (discard/reset/trim/split/punch), private x64 processes,
thread/TLS/FP support and a versioned userspace ABI remain subsequent gates.
