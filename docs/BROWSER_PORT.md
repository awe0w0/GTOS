# Native Chromium port: queued next project

The requested outcome is Chromium executing inside GTOS and rendering real web
pages through GTOS services. A host browser, remote browser session, screenshot
proxy, or set of unimplemented API stubs does not satisfy that outcome.

No Chromium source download or build is part of the current foundation phase.
This work starts after the current gates below are complete.

## Finish the current phase first

- Reviewed English/Simplified Chinese desktop, UTF-8 editing and bounded pinyin
- Durable language/theme preference and application-store recovery verification
- Real bounded AP work queues with separately reported worker readiness
- Clean-checkout builds, module tests, QEMU integration and green CI on `dev`
- Reproducible boot image/source checkpoint with licenses and documented limits

These gates define this phase. They do not imply a production-grade kernel or
that all browser prerequisites below are already present.

## First browser milestone: verify feasibility and resources

Before choosing an upstream revision or implementing a platform port:

1. Inspect current official Chromium architecture, supported target/toolchain
   configurations, dependency requirements and maintained source revision
2. Check actual build CPU/RAM/disk capacity and a suitable persistent environment
3. Determine whether the existing 32-bit protected-mode architecture is viable
   for the selected maintained target, or whether a 64-bit GTOS transition is
   required. Do not assume support or silently choose an obsolete browser
4. Produce a concrete platform/API gap analysis and staged executable tests
5. Clarify ownership/publication before creating a separate Chromium fork;
   current project publication is limited to reviewed GTOS `dev` work

## Known GTOS gaps from the current implementation

The current system has a bounded bytecode app host and trusted kernel workers.
It does not yet provide native browser processes or a POSIX-compatible userspace.

- Native executable loading, an ABI and a practical libc/C++ runtime/toolchain
- Hardware-enforced user/kernel and process isolation, safe fault termination,
  user stacks, syscall argument validation and per-process resource ownership
- General threads, preemption, synchronization, TLS, timers and signal/event APIs
- Virtual-memory mapping/protection, shared memory and appropriate allocation
  behavior beyond the present supervisor identity-map foundation
- A general filesystem, file descriptors, directories, random-access I/O,
  persistent browser profiles and crash-safe storage behavior
- Network interfaces, sockets, DNS, reliable TCP, entropy, clock accuracy,
  certificate trust and TLS. The old demo network sources are not sufficient
- Process creation and IPC, shared buffers, event dispatch and browser service
  boundaries appropriate to the selected upstream architecture
- A browser-facing window/surface/input API, text/clipboard services, robust font
  coverage and a tested rendering path. The current compositor is kernel-side
- Platform sandboxing/security boundaries; a successful test build alone is not
  evidence that browsing untrusted internet content is safe

## Staged proof, after the assessment

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
