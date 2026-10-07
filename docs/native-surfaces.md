# Native PNG desktop surfaces (i386)

This separately versioned extension of native ABI 1 passes decoded pixels to a
bounded desktop-owned image snapshot. The resource catalog and qualified
Wuffs/Skia PNG decoder keep their existing contracts. This is the next native
image module; Linux Chromium/V8 reference success does not establish that a
GTOS native Chromium browser has run.

## Wire and ownership contract

`include/process/surface_abi.h` defines four additive `int 0x80` calls. EAX is
the call/result, EBX addresses the request, ECX is its exact wire size. Other
registers and segments preserve the native ABI policy. No kernel address is
returned, and all native descriptors are snapshotted through checked-copy.

- BEGIN (`0x4707`, 20 bytes) takes version, width, height, stride, format. It
  accepts RGBA8 premultiplied, dimensions 1..32 and exact `width * 4` stride,
  returning a positive signed-i386 handle. Dimensions are bounded before any
  multiplication; the maximum frame contains 4096 bytes. One unfinished
  transaction exists at a time. It belongs to the admitted native owner.
- WRITE (`0x4708`, 20 bytes) takes version, handle, offset, source, length.
  Chunks contain 4..256 bytes in whole pixels, written at the exact next
  sequential offset. The kernel copies the complete user chunk into its
  bounded bounce buffer before the service checks every R/G/B <= A. An invalid
  final pixel rejects the whole chunk without changing any earlier draft byte.
- PRESENT (`0x4709`, 8 bytes) takes version and handle, requires a complete
  frame, and returns zero. It copies the entire fixed buffer into a
  desktop-owned snapshot, publishes its generation, then scrubs the draft.
  The generation is the retired handle. Small frames clear the unused tail.
- ABORT (`0x470a`, 8 bytes) takes version and handle and returns zero. It
  scrubs only the caller's unfinished draft. Process exit/fault/reap use
  `ReclaimOwner` with the same ownership rule. Foreign owners, stale handles,
  malformed wire data and failed writes leave all service state unchanged.

Handles advance for every admitted BEGIN, including aborted transactions.
They stop at `INT_MAX`, never wrap, and never become valid again for a new
owner. Once a frame is published, producer exit or an unrelated draft's abort
cannot remove or modify it. `CopyLatest` copies a fixed snapshot to the caller;
when there is no publication or the generation is already known it returns
false and leaves the output untouched. GUI closing hides its owned image;
future producer publication and GUI lifetime are separate consumer concerns.

## Serialization and allocation

The service is admitted only on the existing BSP native path. Every public
state operation uses the existing `InterruptGuard`, preserving prior IF for
nested scopes. This is single-CPU serialization, not an SMP lock; AP native
admission must not be enabled with this design. The desktop copies under the
same guard and renders its own fixed snapshot after that call returns.

Draft and published pixels occupy fixed supervisor storage. These operations
allocate nothing, change no page mapping, retain no user address, and call no
GUI code. BEGIN availability is configured at boot after a framebuffer desktop
is actually constructed. Legacy/no-framebuffer boot rejects image admission.
The host substitution disables privileged IF instructions solely to exercise
pure state transitions. Tests also compile the actual production i386 object
and independently inspect all eight guarded methods for CLI/STI and reject
heap allocation imports.

## Standalone host qualification

`tests/native_surface_host.cpp` exercises the actual service implementation,
using an independent coordinate pixel oracle and widened endpoint arithmetic.
It covers all 1024 legal dimensions; zero, oversized and UINT_MAX dimensions,
stride/length/offset/wire/version errors; owner and stale-handle isolation;
empty/partial/complete publication; entire-chunk premultiplication validation;
nonzero draft scrubbing on abort and owner exit; immutable publication after
producer exit; large-to-small tail clearing; and untouched CopyLatest output.
A private friend exists only under `GTOS_SURFACE_HOST_TEST` to inspect scrubbing
and position the real counter at its signed-i386 exhaustion boundary. No native
ABI or production public API is added for tests.

Run with the prepared host toolchain and a fresh output directory, preserving
all previous evidence and caches:

```sh
source /mnt/f/GTOS-Chromium/scripts/env.sh
export CXX=/mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++
bash tests/native_surface_host_test.sh \
  /mnt/f/GTOS-Chromium/artifacts/native-surface-host-reproduction
```

The runner performs GCC13 O0/O2 x64 ASan+UBSan checks and O0/O2 actual Linux
ELF32 execution of the same service and cases. The prepared compiler lacks
multilib C++/sanitizer libraries, so ELF32 uses a freestanding entry and Linux
`int 0x80` report/exit, with minimal memory shims for aggregate copies. This
still executes real 32-bit code; it does not emulate the bank, and no i386
sanitizer result is claimed. Source and binary hashes, complete build/run logs,
ELF class/machine inspection, production disassembly and `status.json` are
written into the new artifact directory.

Host qualification does not establish checked user-copy, interrupt/concurrent
scheduler admission, actual desktop rendering or close behavior. Those require
the independent real QEMU native consumer acceptance and exact guest evidence.

The completed standalone matrix on the 4dd39a1-derived worktree reports
8,427,639 checks per run, 1,058,058 offset/length combinations and 1024 legal
dimensions in all four configurations. Final evidence is
`/mnt/f/GTOS-Chromium/artifacts/native-surface-host-20261008T0102Z/status.json`.
All eight production guard methods and the no-heap-import check also pass.

## Native consumer and desktop acceptance, 2026-10-08

The module was self-reviewed and tested on the 4dd39a1-derived source. A separate
`apps/png_surface_probe` program retrieves the real PNG, checks the complete
BGRA/Skia oracle, and publishes **all 1024 final bytes from its decoded RGBA
buffer**. Bad wire sizes, unmapped/overflowing and crossing-page sources,
read-only reads, stale handles, partial PRESENT and final-pixel rejection run
through actual CPL3 syscalls. The original resource and codec probes are intact.

After PRESENT the producer poisons its buffers, starts a second partial frame
and exits. The existing kernel native acceptance checks admission/ABORT **before
Reap**, so the fallback cleanup cannot hide a missing Stop cleanup. After Reap
it compares every published byte, dimensions and generation, and confirms the
original physical-frame baseline. Fault/RequestExit use the same owner scrub;
existing native-process O0/O2 regressions pass all ten guests.

ModernDesktop owns fixed validated snapshots, renders outside the short BSP
guard, and composites premultiplied 8-bit alpha once against a checkerboard.
Esc and titlebar close hide the image; `5` explicitly reopens it. An unchanged
generation does not reopen or restore a minimized window. Closing intentionally
retains one bounded desktop snapshot for reopen, without a per-frame allocator.
New Chinese labels use the existing validated font atlas. Live/session and
boot-log behavior and the original five dock cells retain their contracts.

Actual guest configurations were 64 MiB/4 CPUs, 32 MiB/1 CPU with persisted
Chinese locale, and 96 MiB/4 CPUs. Each checked all 256 source pixels as 9216
scaled framebuffer pixels after producer exit/reap, close stability, explicit
reopen and mouse close. Catch input, keyboard/mouse application-disk reload,
AP work, original boot checks and unchanged entire guest-disk hashes passed.
Source, kernel, archive, ELF and extracted ISO identity were independently bound.

The static i386 consumer is **57,576 bytes**, uses **38 user pages**, and has a
complete exact-machine-code stack bound of **2840 bytes**, with **5336 bytes**
headroom in the unchanged 8176 usable bytes. Kernel O0/O2 integer audits pass.
The linked consumer preserves the prior real Wuffs/Skia object bytes; no new
upstream decoder matrix or Chromium/V8 build graph was launched.

Local evidence under `/mnt/f/GTOS-Chromium/artifacts`:

- `native-image-build-20261008T0130/manifest.json` and source hashes.
- `native-image-stack-20261008T0130-padding-proof2/status.json`.
- `native-image-guest-20261008T0140/results.json`, exact ISOs and screenshots.
- `native-surface-host-20261008T0102Z/status.json`.
- `native-image-desktop-host-20261008-a/desktop-verification.json`.
- `native-image-runtime-smoke-20261007T170930Z/status.json`.
- `native-image-kernel-20261008T0130` and `native-image-kernel-O0-20261008T0130`.

The unrelated strict FP regression compiled and passed linked/negative audits,
but stopped at its AVX CPU precondition: the prepared QEMU 4.2.1 `max` CPU did
not advertise AVX. No assertion was waived and no strict FP guest pass is
claimed. This PNG module uses the original integer-only Disabled policy.
The first consumer review error, a Windows command-path CR error, and the
strict alignment-audit failures remain in separate artifacts; none was accepted
as qualification. The final padding proof binds actual LLD section geometry,
object alignment, terminal instructions and targets rather than ignoring code.

## Reproduce

Use fresh output paths and the existing prepared runtime environment. The
consumer builder's ordinary mode continues to build the original resource app.

```sh
source /mnt/f/GTOS-Chromium/scripts/env.sh
python3 tools/build-png-resource-probe.py NEW_BUILD --surface \
  --qualified-png /mnt/f/GTOS-Chromium/artifacts/png-release-final-20261007T102400Z \
  --dependency-cache /mnt/f/GTOS-Chromium/cache/native-wuffs-dependencies \
  --clang /mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang \
  --host-cc /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/gcc \
  --host-cxx /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++
make -j8 GTOS.iso CXX=/mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++
python3 tools/qualify-native-image-stack.py --build-dir NEW_BUILD --output NEW_STACK \
  --source-sha256 QUALIFICATION_SOURCE_HASHES.json
/usr/bin/python3 tests/native_image_qemu.py NEW_BUILD NEW_GUEST \
  --runtime-root "$TMPDIR" --kernel-sha256 EXACT_NEW_KERNEL_SHA256 \
  --source-sha256 QUALIFICATION_SOURCE_HASHES.json \
  --host-cc /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/gcc \
  --host-cxx /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++
```

`QUALIFICATION_SOURCE_HASHES.json` must contain SHA256 values for the current
real consumer, service, GUI, catalog, runtime, kernel, test drivers and helper
sources required by the runner. The final local file is in the qualified build
directory; a changed checkout needs newly captured hashes and a new build.
The dedicated ISO replaces only module 3; the original ordinary ISO is retained.

This is a bounded native PNG/window seam for the 32-bit Chromium target. The
32x32 limit does not qualify browser-sized surfaces, and no native Chromium or
V8 executable was run. Larger native memory/runtime, real Chromium integration,
input/process/IO and networking remain future target work.
