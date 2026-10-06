# Native Chromium: platform assessment and next proof

Assessment and status date: 2026-10-06. Kernel/desktop/Chinese checkpoint
`29f60022a381cbb11257f03fb3a61ef1f3273fac` and bounded ELF32/CPL3 checkpoint
`b36d0c22f1b5b3c3bc54aef82733ca584b8c568d` have passed their exact-head CI.
A separately authorized larger workspace has completed a Linux x86-64 V8
reference build and workload. Native Chromium and webpage rendering inside GTOS
remain future acceptance results.

## Build resources: split kernel and reference environments

The current cloud executor reports an x86-64 host, nine logical CPUs, approximately
9.7 GiB RAM, and only 28 GiB free on a 32 GiB workspace filesystem. Official
Chromium Linux instructions require at least 8 GB RAM (strongly recommend more
than 16 GB) and at least 100 GB free disk. The full documented checkout/build is
therefore storage-blocked here. Removing the few existing project files cannot meet that floor. Kernel/API
development and small guest conformance programs therefore remain here. The
separately authorized larger environment now handles the upstream checkout and
reference builds without making its Linux services part of the GTOS guest.
[Official Linux build instructions](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/linux/build_instructions.md)

## Target architecture: do not confuse host and guest

The x86-64 **build host** requirement does not itself make every Chromium target
64-bit-only. Current compiler configuration still contains i386 target paths,
and V8 still contains an IA-32 backend. This is evidence of implementation paths,
not a verified supported Chromium-on-GTOS configuration. The exact upstream
revision and target require a conventional reference build before committing to
an IA-32 browser deliverable.
[Compiler CPU ABI](https://chromium.googlesource.com/chromium/src/+/HEAD/build/config/compiler_cpu_abi.gn),
[V8 build configuration](https://chromium.googlesource.com/v8/v8/+/refs/heads/main/BUILD.gn)

An x86-64 GTOS userland is strongly preferable for the eventual modern browser:
address-space pressure is lower and V8's heap sandbox specifically needs 64-bit
virtual addressing. Its large virtual reservation does not mean an equally
large physical-RAM requirement. This V8 boundary is separate from OS process
sandboxing. Existing i386 desktop acceptance will remain maintained during any
future architecture transition.
[V8 sandbox design](https://v8.dev/blog/sandbox)

The immediate i386 native-process milestone below is a reusable OS-foundation
proof, not a promise that the final Chromium target will remain i386. Its ABI is
experimental. It cannot provide NX enforcement under the current non-PAE tables,
and initially rejects user floating-point/SIMD until context ownership exists.
Those limitations must be resolved for the selected real browser target.

## What a platform backend does and does not supply

Ozone is a useful display/input boundary. Software Skia canvases can establish a
first userland rendering path without implementing accelerated GPU drivers.
It does not provide processes, libc, virtual memory, networking or a sandbox.
A GTOS backend must eventually supply real window surfaces and focus/resize/key/
pointer/text-composition events. The current bounded kernel pinyin box is not a
complete browser IME.
[Ozone overview](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/ozone_overview.md),
[Canvas/surface API](https://chromium.googlesource.com/chromium/src/+/HEAD/ui/ozone/public/surface_factory_ozone.h)

Chromium uses coordinated Clang/libc++ and Rust toolchains. A C/C++-only runtime
plan omits actual dependencies. Native threads, thread-local storage, blocking
primitives, descriptor/handle lifetime, IPC, shared memory and executable-page
management need guest implementations, not APIs that return success without
performing their contract.
[Toolchain policy](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/toolchain_support.md),
[Rust configuration](https://chromium.googlesource.com/chromium/src/+/HEAD/build/config/rust.gni),
[Mojo initialization](https://chromium.googlesource.com/chromium/src/+/HEAD/mojo/core/embedder/README.md)

The legacy network demonstration sources are insufficient for browsing. Guest
sockets, DNS, TCP, reliable time, certificate trust and entropy are prerequisites.
TLS can be implemented in userland with the browser's cryptographic libraries;
it does not require inventing kernel TLS. A host may provide an ordinary virtual
NIC or controlled HTTP fixture, but must not fetch/render pages for GTOS.
[Socket implementation](https://chromium.googlesource.com/chromium/src/+/HEAD/net/socket/socket_posix.cc),
[TLS client](https://chromium.googlesource.com/chromium/src/+/HEAD/net/socket/ssl_client_socket_impl.cc)

## Completed first implementation gate: real native user processes

1. Separate private process page directories borrow only immutable supervisor
   kernel mappings. Never reopen the AP workers' sealed directory. Reject a user
   arena if an existing identity-mapped kernel PDE occupies any of it
2. Allocate a fixed retained pool of guarded kernel-stack aliases before the
   shared template is sealed, so every process CR3 maps every dispatch stack
3. Add CPL3 code/data descriptors, a TSS with denied port access, segment-safe
   interrupt frames, and BSP CR3/kernel-stack dispatch that preserves ring-0 tasks
4. Validate syscall ranges page by page before copying. The existing trusted
   kernel debug-print syscall must never become a raw-pointer userspace service
5. Terminate user-origin faults without stopping another process or desktop;
   kernel faults remain fatal. Reap user frames only after switching away and
   removing all runnable references. Keep floating-point/SIMD state unavailable
   until a proper save/restore contract exists
6. Parse bounded ELF32 load plans with overflow, overlap, unsupported-feature and
   resource checks. ELF flags are loader policy, not a claim of hardware NX
7. Prove two real CPL3 programs at identical virtual addresses have distinct data;
   timers preempt them; invalid pointers fail safely; a faulting program dies;
   its peer, GUI input/game and AP workers continue. Repeat create/exit/reap and
   run all existing i386 desktop/language/legacy regressions

The seven checks above are now exercised by the published bounded native
milestone, including a separately compiled external Clang ELF with verified
byte identity. See [native processes](native-processes.md) for exact contracts,
tests and cross-emulator exception expectations. This milestone is not POSIX, a
stable application ABI or browser execution.
The optional i386 legacy FP module now supplies bounded BSP ownership with
[explicit multi-engine verification limits](native-fp-verification.md), while
the normal desktop profile stays integer-only. Subsequent work includes the
separate x64 foundation and its FP/thread state, complete native loading/runtime,
dynamic VM, user threads, VFS/files, IPC, networking and userland graphics. Each needs an executable conformance gate.

## Measured Linux reference and next target

The separately built sandbox-enabled V8 `d8` reference executes JavaScript,
16 MiB typed arrays, floating point, Chinese Intl/ICU, Wasm, garbage collection
and microtasks on Linux x86-64. Its 54,831,144-byte ELF64 ET_DYN contains
PT_INTERP, PT_DYNAMIC and PT_TLS, with SHA-256
`29f81ab63e01632d3554c158011abafc701ba94f5fcf87f58cf467ab066de91f`.
This records a control artifact, not an executable accepted by GTOS's static
ELF32 loader or a completed Chromium browser build.

The traced workload observes anonymous mapping/protection/unmapping/advice,
thread creation and synchronization, randomness, file opens and architecture
TLS setup. Its largest successful PROT_NONE/MAP_NORESERVE call reserves
1,481,763,713,024 bytes of virtual space, including guards/alignment. This is
not a RAM requirement or a reason to eagerly allocate page tables for that
entire range. The trace is workload evidence, not an exhaustive browser API
specification, and Linux call names need not be copied verbatim into GTOS.

The deliberate architecture track now has a separate [x86-64 boot foundation](../arch/x86_64/README.md)
with real long-mode/protection acceptance on QEMU 8.2 and 10, followed by
a bounded boot-map-derived physical frame service and
[kernel-only sparse VM](../arch/x86_64/SPARSE_VM.md). Reserve/commit/protect/
decommit/release now have actual guest conformance, including large unbacked
regions and exact rollback. Private x64 user processes, additional region
lifecycle operations, per-thread FP/TLS and wait/wake lifetime remain open.
The existing i386 desktop target and its tests remain maintained. This follows
both the measured address-space requirement and V8's explicit 64-bit sandbox
requirement. It does not promise unchanged Linux binary compatibility.
[V8 sandbox design](https://v8.dev/blog/sandbox)

## Later browser acceptance

After prerequisites and sufficient build resources: pin a maintained revision,
prove an unchanged reference build, bring up selected base/Mojo and V8 tests,
then a real guest Content shell on packaged HTML/CSS/JavaScript and controlled
HTTP/HTTPS fixtures. A JavaScript engine alone is not webpage rendering. The
final browser needs navigation, input, persistent profiles, renderer recovery
and enforced security boundaries. Single-process or unsandboxed bring-up cannot
establish safe public-internet browsing.
[Process architecture](https://www.chromium.org/developers/design-documents/multi-process-architecture/),
[Linux sandbox example](https://chromium.googlesource.com/chromium/src/+/HEAD/sandbox/linux/README.md)

Fuchsia WebEngine is an official non-Linux precedent using Chromium's Content
layer, but explicitly does not provide the complete Chrome browser interface.
It demonstrates substantial platform integration, not a framebuffer-only port.
Separate Chromium fork ownership/publication still needs clarification; current
publication authorization covers reviewed GTOS work on `dev`.
[Fuchsia WebEngine](https://chromium.googlesource.com/chromium/src/+/HEAD/fuchsia_web/README.md)

Rolling HEAD/main links above record the inspected interfaces; they are not a
pinned reproducible browser build specification or a full implementation list.
