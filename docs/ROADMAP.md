# GTOS engineering roadmap

GTOS is an educational 32-bit x86 operating system. This roadmap separates verified
capabilities from development targets. Work is reviewed and committed to `dev`;
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
records the full-build storage blocker, architecture tradeoffs, and immediate
CPL3 process/isolation proof. Chromium itself has not yet been built or run.

## Current phase checkpoint: kernel workers and localized desktop

The foundation, kernel paging, modern desktop and bounded language implementation
are tested together. APs now perform bounded queued integer work with verified
results, rather than merely being discovered or parked. Per-worker private IDTs,
shared sealed page tables and wake IPIs are implemented. One failed worker can be
contained; this is not isolation for arbitrary native applications.

This checkpoint does **not** close phases 2–5. General AP scheduling, ring-3
processes, a filesystem, terminal/file manager, session restoration, broader
hardware support and browser-facing services remain open. The queued browser
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

Remaining immediate native-runtime work includes FP/SIMD ownership, richer VM and
syscalls, threads/TLS, files/IPC, and userland surfaces. The current non-PAE scheme
has no NX protection; fixed layout and four bounded process slots are explicit
limits rather than a general-purpose browser platform.
