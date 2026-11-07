# GTOS Content platform continuation

These patches extend the committed `port-candidate-r20` in a new private view.
They preserve the original files and qualification records. `apply.py` verifies
the exact input digests, materializes symlink parents using the existing
composer, and refuses shared source roots, changed inputs or a second application.

The changes add GTOS to the platform-independent menu/user-data targets, select
ELF32 for GTOS NASM, and give GRIT an explicit GTOS identity. GRIT must receive
`-t gtos`; otherwise resource generation can inherit the build host's platform.
GTOS is neither Linux nor POSIX in the resource condition evaluator.

Dawn's common library now has an explicit GTOS platform identity. Its handle
ownership uses the existing native POSIX file subset's `dup`/`close` contract;
environment access uses the existing libc environment functions. Optional
executable/module paths remain unavailable because GTOS has no such query ABI.
Dynamic library open/symbol lookup explicitly fail with a diagnostic: this
port does not claim a shared-library loader or a working Dawn GPU backend.

`args.gn` selects existing Aura/Views code and disables unsupported dynamic CDMs,
GLib, CUPS and Linux PipeWire capture. Host toolchains keep their platform D-Bus
default; GTOS does not become Linux. It does not implement the missing GTOS Ozone
backend or remove the port's refusal to enable it. These are configuration changes,
not a claim that windows or Chromium currently run on GTOS.

## Replay

Execute through the workspace's existing guard. Use a fresh owned view/output;
the source trees and SDK are read-only inputs.

1. Run `port-candidate-r20/candidate.py --compose <fresh-view>`.
2. Run `gtos-platform/apply.py <fresh-view>`.
3. Pass the contents of `args.gn` and the real SDK/configuration paths to GN:

   ```text
   gn gen <fresh-output> --root=<fresh-view>
     --root-target=//content/shell:content_shell_app
     --root-pattern=//content/shell:content_shell_app
     --args=<profile plus explicit material paths>
   ```

   Material arguments: `gtos_clang_bin`, `clang_base_path`, `gtos_clang_include`,
   `gtos_sdk_dir`, `gtos_libcxx_include`, `gtos_compiler_builtins`, `gtos_png_source_dir`,
   `gtos_png_skia_source_dir`, `gtos_png_gif_source_dir`,
   `gtos_png_wuffs_include_dir`, `gtos_libxml_config_dir`, and
   `gtos_posix_file_include` (the native_libxml_file/include directory).

   `gtos_compiler_builtins` must name the already qualified native IA32 integer
   archive, not a Linux runtime or a Rust metadata archive. The caller verifies
   SHA256 `f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528`
   before dispatch. This restores the explicit archive contract previously
   qualified in `native_rust_target/gn`, replacing a nonexistent guessed Clang
   runtime path. Other architectures and requested GTOS profiling are refused.

Both target switches matter. `--root-target` selects the entry, but by itself
still defines other targets in loaded BUILD files. `--root-pattern` retains the
entry's complete transitive dependencies while excluding unrelated targets.
Use the same switches for subsequent `gn desc`/`check` operations. This does not
load or validate the port's root BUILD gate, and does not remove a Content dep.

## Tests and evidence

```text
python3 -B tests/gtos_content_platform_test.py
  --view <patched-view> --gn <exact-gn> --out <fresh-test-directory>
```

The test runs the actual patched GRIT evaluator and NASM template under GN.
It checks GTOS identity/cached expression reuse, unknown-variable rejection,
ELF32 selection, unsupported GTOS architecture refusal, existing Linux/Windows
formats, and the private patch application's refusal paths. A real GN regression
also proves that root-pattern cannot hide a missing transitive dependency.
The small GN test toolchain only supplies surrounding action infrastructure;
this is configuration validation, not assembler execution or guest acceptance.

The Dawn tests compile the actual patched DynamicLib, SystemHandle and
SystemUtils implementations plus upstream logging. Build with
`tests/gtos_dawn_platform_build.py --view ... --clang ... --gcc13-root ... --out ...`.
Its manifest records the two binaries and matching loader. Run them only in a
second guard stage after checking those identities, with the build directory
read-only and a separate writable run root. The host tests exercise the real
GTOS branches but use host libc calls, so they do not qualify GTOS syscalls.
`tests/gtos_dawn_target_build.py --view ... --clang ... --out ...` separately
compiles those four files with the actual GTOS SDK and `-nostdinc`, checking
ELF32/i386 objects and producing depfiles at both optimization levels.

2026-11-06, original-guard results:

- `codex-c-unit-u06`: all 10 GN/GRIT/refusal tests passed.
- `codex-c-dawn-build-b04` / `codex-c-dawn-run-r01`: O0/O2 each ran 190 checks,
  zero failures, including repeated handle cleanup and unsupported operations.
- `codex-c-dawn-target-t01`: eight real GTOS ELF32/i386 objects plus depfiles.
- `codex-c-gn-g14`: the formal Content graph generated 21,680 targets from
  2,571 files. This still does not provide a Content object, link or GTOS entry.
- `codex-c-gn-g15`: the same formal graph now names the qualified integer archive
  and ext4 compiler supply; `codex-c-unit-u07` passed 14 tests, including the actual
  runtime config's positive, missing-archive, wrong-architecture and profiling cases.

Full raw stage records remain in the existing packet's `evidence/codex-c-*`
records, including failed attempts and the preceding GN-only generation.

The actual GTOS C/C++ toolchain now uses `-MD`, so SDK headers passed through
`-isystem` participate in Ninja invalidation. Run Ninja with `-d keepdepfile` to
retain the textual dependency evidence. Rust-only tool variables are scoped to
the Rust condition; the C/C++ toolchain also generates when Rust is disabled.

`tests/gtos_toolchain_dependencies_test.py --view ... --gn ... --ninja ...
--clang-bin ... --out ...` builds two real ELF32 objects through that toolchain.
On 2026-11-07, guarded `codex-c-dependency-test-d02` passed all six checks:
complete dependency records, no unnecessary rebuild, SDK change invalidation,
compiler rejection, deterministic recovery, and missing-header rejection.
`codex-c-unit-u08` passed the 14 existing regressions; `codex-c-gn-g16` retained
the complete 21,680-target Content graph with Rust enabled. No Content object
or runtime acceptance is implied by these component tests.

DevTools' esbuild invocation preserves symlink paths. Without this, a source
importer resolves back into the shared checkout and the plugin searches there
for `locales.js` instead of the private GN generation directory. The real
Ninja failure is retained as `codex-c-ninja-n04` (47th action).
Guarded `codex-c-bundle-test-b01` runs the actual upstream script as a failing
calibration and the patched script for six checks: generated import resolution,
dependency paths, a conflicting canonical-source neighbour, ordinary files,
missing input, and malformed input. Successful bundles are imported by Node and
their actual exported value is checked; no replacement bundler model is used.

For private symlink views, Node also needs
`NODE_OPTIONS="--preserve-symlinks --preserve-symlinks-main"` so its own module
and tool resolution stays in that view. The host supply copies pinned Node,
TypeScript, esbuild, Rust tools and clang-format into private ext4 paths, checks
their SHA256, and grants execution to those exact files. Source checkouts remain
read-only. These tools are build-host tools, not GTOS runtime dependencies.

The view restores `.gn`'s pinned CPython 3.11.9 interpreter. The older candidate
selected system Python 3.8, which fails while importing the actual upstream
`modulemap_config.py` (`list[str]` annotations); formal Ninja reached 708 actions
before this failure in `codex-c-ninja-n05`. Supply the pinned interpreter at
`third_party/cpython3/host/bin/python3` in ext4 before generation, keeping its
matching checked-out standard library read-only. Its SHA256 is
`df4f8d0ae4bace71af48c8630b2cb48789395925c08b56fa8037be9b15f215a7`.

Guarded `codex-c-python-test-y02` runs
`tests/gtos_python_toolchain_test.py --view ... --gn ... --ninja ... --out ...`.
It reproduces the real module's failure with Python 3.8, imports it with the
pinned interpreter, and verifies a GN/Ninja action's own reported executable,
version and output. Unchanged actions remain idle; deliberate Python failures
propagate through Ninja. `codex-c-unit-u10` passed 15 regressions, including real
root `.gn` patch application. The application's containment check accepts the
private view itself as a parent while continuing to refuse shared roots.

Rust target invocations use `gtos_rust_target_json` for the actual JSON path,
while keeping `i686-unknown-gtos` as the sysroot/output ABI name. Use the committed
`apps/native_rust_target/i686-unknown-gtos.json`, whose IA32/SSE2 ABI matches this
Content toolchain. The older `native_content_build/rust` soft-float experiment
is a separate profile. The cfg action and compiler both receive the JSON with
`-Zunstable-options`; both track it as an input. GTOS Rust disables default host
link libraries. The Rust compiler wrapper uses the same pinned CPython as GN.

`codex-c-rust-test-z02` passed five real-compiler checks: the bare custom triple
fails, the JSON reports GTOS IA32/SSE2 without Unix/Linux identity, the Linux
host target retains its behavior, and malformed/missing specifications fail.
`codex-c-ninja-n10` then built the actual formal graph's core rule successfully:
`local_rustc_sysroot/lib/rustlib/i686-unknown-gtos/lib/libcore_core.rlib`,
65,478,856 B, SHA256
`f0546af512792600c4267b0dc3f8c27aa72bf4d8b9d26fd9169e30615985217a`.
Its metadata, response file, flags and dependency file are retained alongside it.
`codex-c-dependency-test-d03` retained all six C/C++ SDK dependency regressions
with Rust disabled. This core build does not qualify full Rust std, OS services,
Content object/link generation, or browser execution.

The GTOS Rust `System` allocator calls the native C heap ABI directly. The
ordinary alignment fast path uses malloc/calloc/realloc; over-aligned layouts
use posix_memalign and the pinned standard library's own realloc fallback.
Failed allocations return null, and failed reallocations preserve the old
allocation. This adapter does not increase the heap quota or supply a new heap.
The platform application adds this module only after materializing private
parents; a pre-existing file or dangling link is refused before patching.

Guarded `codex-c-allocator-build-a01` builds
`tests/gtos_rust_allocator_build.py` with pinned Rust/Clang, using the actual
upstream MIN_ALIGN and realloc_fallback. `codex-c-allocator-run-r01` then runs
both frozen host images from a separate writable root: seven cases at each of
O0/O2 pass, including alignment, zeroing, allocation failure/recovery, realloc
failure ownership, and over-aligned grow/shrink. Only foreign C imports are
redirected for host fault injection; the allocator body is the product module.
The same build separately compiles two actual GTOS ELF32/i386 target objects.
Host libc tests and target compilation do not qualify the GTOS heap runtime.
`codex-c-unit-u24` passes 17 platform regressions, including private new-file
materialization and pre-existing-file/dangling-link refusal.
`codex-c-gn-g24` generates the unchanged full Content graph, and its actual
`codex-c-ninja-n25` std compilation accepts the allocator branch. It then stops
at eleven remaining error/TLS/synchronization/random backend errors; no complete
std library or Content object is claimed.

The following Rust backends now select GTOS explicitly, without Unix identity:

- Error handling reads the native `__llvm_libc_errno()` slot and maps the
  committed SDK/file-service errors; unknown values retain their code.
- TLS keys use the native compiler-rt emutls control ABI and C++ thread-exit
  callbacks. Key allocation/registration failure aborts, concurrent lazy-key
  losers are freed, and callbacks can reinitialize a key. Link the native
  thread-aware C heap/emutls/exit provider; the adapter does not supply that
  provider or expand its four-context BSP profile.
- Random bytes require CPUID-advertised RDRAND with at most ten attempts per
  word, including short tails. Unavailable/exhausted hardware fails; a partial
  prefix is unusable. There is no timestamp or deterministic fallback.
- The upstream Mutex/Condvar/Once/RwLock algorithms use a GTOS cooperative
  waiting backend. It polls atomics and calls native Yield rather than claiming
  a kernel futex queue. A shared notification generation permits spurious
  wakes, including notifications without a value change. Timeouts validate the
  native coarse monotonic-clock tuple and include its 10 ms resolution to
  prevent early timeout. This costs runnable CPU and is not an SMP scalability
  claim; invalid clocks/yields fail instead of claiming completion.

`tests/gtos_rust_module_build.py --module error|tls|random|futex --view ...
--clang-bin ... --gcc13-root ... --core-root ... --out ...` builds the actual
modules' O0/O2 host tests. TLS uses real host emutls/C++ exit providers; allocator
and registration failure injection redirects only the C imports. Random tests
inject instruction outcomes, and waiting tests inject the platform clock/yield.
The Mutex and Condvar tests include the pinned upstream implementations, not
replacement lock algorithms. All images are published read-only and executed
in a second original-guard stage with a separate writable root.

Guarded results on 2026-11-07:

- `codex-c-error-build-e03` / `codex-c-error-run-r10`: three cases at O0/O2.
- `codex-c-tls-build-t03` / `codex-c-tls-run-r06`: six cases at O0/O2.
- `codex-c-random-build-q03` / `codex-c-random-run-r09`: five cases at O0/O2.
- `codex-c-futex-build-f03` / `codex-c-futex-run-r11`: seven cases at O0/O2,
  including contended upstream mutex updates, condvar relocking, clock refusal,
  no-notification value changes, same-value notifications, and timeout bounds.

The last three builds also compile two real GTOS ELF32/i386 objects each.
Earlier `codex-c-futex-run-r07` caught an early timeout caused by timestamp
rounding; its failure remains recorded. These host/interface tests and target
objects do not establish execution of these Rust backends in a GTOS guest,
complete Rust std services, Content C1/C2/C3, or browser functionality.

The five added modules are registered in the generated std target's GN sources
as well as its gnrt template/configuration. The compiler wrapper's source-input
check stays enabled: `codex-c-ninja-n27` compiled the crate but rejected those
missing declarations. No successful GN build is claimed for that stage.
Platform application now dry-runs every patch before modifying any input,
resolving leaf links only for that read-only check and materializing private
copies for the real writes. `codex-c-unit-u27` passed 19 tests, including bad
patch refusal without side effects and linked-original byte preservation.

`codex-c-gn-g27` retains 21,680 formal Content targets from 2,571 files.
`codex-c-ninja-n29` successfully builds the actual GN std target, including the
wrapper's source-input checks. Its `libstd_std.rlib` is 12,154,424 B, SHA256
`2fb6c619a712b43f5723266ac3a17364e7154d381aab8cf95fa7e21d4fdcd469`:
one Rust metadata member and 18 ELF32/i386 relocatable objects. The actual
depfile includes all five GTOS modules. This is a restricted experimental std
build: filesystem, general time, network and Rust thread-spawn services still
retain upstream unsupported behavior. It does not assert full std runtime
support, Content C1/C2/C3, or guest execution of these Rust adapters. gnrt
regeneration itself has not been run; the existing generated GN rule and its
template/configuration are updated together.

`codex-c-ninja-n30` reached 1,698 actions before the actual Node version check
imported its helper from the canonical checkout, selecting the shared-drive
Node instead of the private executable. `check_version.py` now inserts its
logical checkout directory before importing that helper. The version check
itself remains unchanged. `codex-c-node-version-test-nv01` executes six real
action checks: upstream canonical-import failure, patched leaf-link entry,
ordinary entry, true version mismatch, missing private tool, and recovery.
All pass with the pinned Python/Node; this does not constitute Content C1.

`tests/gtos_c_rust_float_build.py` uses the actual private GN C toolchain plus
the canonical SSE2 Rust target and GN-built core. The current C profile already
enables SSE2; the older shared toolchain's no-SSE flags are not its current
arguments. `codex-c-float-build-f04` / `codex-c-float-run-r02` run 22 checks
at O0/O2 in the current SSE2 profile and an explicit x87 compatibility control:
float/double arguments and returns in both directions, repr(C) structures,
function pointers, signed zero, subnormal identities and infinities. A fifth
image changes one expected result and returns one failure through the same
entry, proving the comparison can reject. The emulator requires the read-only
ELF input's executable mode bit; the earlier f03/r01 supply failed without it,
and f04's five images have identical bytes. Linux write/exit are used only for
reporting these compiler-ABI results. This is neither a GTOS guest run nor FP
context, kernel, libc or browser acceptance.

The private incremental source workspace is versioned by `workspace.py`.
`snapshot` archives materialized source overlays and records the pinned host
tools outside the workspace. Builds verify that record before and after
GN/Ninja while the workspace is read-only. To change sources, compose a fresh
desired view, snapshot it with `--workspace` naming the active workspace, then
use `apply --previous ... --following ...` between builds. Unchanged files keep
their timestamps, while changed files get private copies. Old views, snapshots
and version receipts remain intact; `.codex-workspace/current.json` is the
explicit mutable pointer to the active source version. Source removal and
host-tool changes require a fresh workspace. This avoids rebuilding unchanged
frontend actions simply because their source-root path changed.

`codex-c-workspace-test-wt03` passes eight tests against those real functions,
including drift/reuse/escape rejection, linked-origin preservation, immutable
old bytes, unchanged timestamps and stale-version refusal.
`codex-c-source-snapshot-w01` records 44 materialized sources and nine pinned
tools; `codex-c-gn-g28` verifies it and generates the formal 21,680-target graph.
These are source/build checks and do not establish Content C1/C2/C3.

The formal `codex-c-ninja-n32` build reached 1,794 actions, then exposed a GTOS
compiler-rule bug: `{{output}}` expands to both `.o` and the additional `.dwo`,
so it cannot be used as a single `-o`/`-MF` argument. C/C++ rules now use the
same `target_out_dir/label_name/source_name_part.o` layout as the upstream GCC
toolchain and bind that primary output explicitly. This also matches the
existing split-DWARF config. The pinned GN did not emit `source_out_dir` in
compiler edges for the intermediate command template; that rejected attempt
is retained. The formal C1 output locator consequently becomes
`obj/content/shell/content_shell_app/shell_main_delegate.o`, still the same
production translation unit and dependency graph.

`codex-c-dependency-test-ds04` passes nine real GN/Clang/Ninja checks, including
system-header changes and compiler rejection, both C/C++ object/debug pairs,
unchanged builds, and recompilation after a `.dwo` is removed. It extracts the
actual `split_dwarf` config, rather than substituting a test-only output rule.

`codex-c-ninja-n34` passes the corrected compiler command and reaches actual
Perfetto source diagnostics. Its next blockers include an unknown GTOS OS,
missing file/integer-format headers, and the legacy diagnostic-only guard on
the libc++ external thread declarations. The native Content header layer now
preserves the existing provider's declaration layout and ABI namespace under
an explicit GTOS/IA32 guard, while the old qualification headers stay intact.
It copies three LLVM integer-format headers from `native_c_clock/sdk` with
their provenance, and supplies scalar `uid_t`/`mode_t` aliases. Those aliases
do not implement Unix users or permission enforcement.

The existing `gtos_posix_file_include` argument moves from Dawn's local config
to the common GTOS SDK config, so consumers share the implemented native file
subset. Perfetto gets an independent GTOS identity with all other OS flags
false; its remaining platform services still need implementation. No Linux
identity or signal/filesystem functionality is inferred from that flag.

`codex-c-sdk-test-cs02` passes nine actual compiler/header contracts: native
C/C++ and layout checks, wrong architecture, conflicting OS, a deliberately
wrong ABI layout, and Perfetto native/host/unknown-platform controls.
`codex-c-unit-ucs02` passes the 19 platform/application regressions. These
validate declarations, compiler behavior and private patch application, not
new threading runtime behavior, linking, a Perfetto runtime or a guest run.

The formal `codex-c-ninja-n35` / `n37` builds now compile real GTOS Perfetto
sources including base64, crash keys, CPU info, event fd and dynamic string
writer. Their primary objects, split debug outputs and SDK depfiles exist.
The native directory adapter has separate O0/O2 unit execution evidence (see
`apps/native_libxml_file/README.md`); runtime linking and guest I/O for that
new adapter remain pending. C1 is still the formal shell delegate TU, not one
of these dependency objects.

GTOS has no POSIX signal-handler service. The native base target selects
`ctrl_c_handler_gtos.cc`, which diagnoses and fails a request through the
existing Perfetto crash mechanism. It cannot silently report an installation.
Windows/POSIX keep their original source; the pinned caller search finds the
requests in Perfetto's command-line and Unix RPC daemon tools, with no match
in the checked Base/Content trees. Signal-handler support remains unavailable.

`codex-c-perfetto-control-build-p01` / `run-r01` exercise that actual source:
two no-request controls return zero; four O0/O2 null/non-null requests print
the diagnostic and terminate with SIGILL. The logging sink and Linux reporting
are test dependencies. The original host source compiles and accidental host
selection of the native source is refused. This verifies the explicit failure
policy, not a working GTOS signal service or a guest execution.

Perfetto's thread identity and ThreadChecker now use the existing native
libc++ `__libcpp_thread_get_current_id` provider on GTOS. Its ABI returns a
positive, process-scoped native task ID; zero is rejected because it is the
checker's detached sentinel. Other OS branches still use their original
providers. This adds neither a pthread facade nor thread creation capability.

`codex-c-perfetto-thread-build-t04` / `run-r04` test the real checker and
inline identity adapter at O0/O2 with an injected provider: 145 checks per
normal image cover identity changes, copy/assignment, repeated detach/rebind
and the full unsigned ID width. Two wrong-expectation images return 1; two
zero-ID requests log and terminate with SIGILL. The host branch also compiles.
The first test supply lacked errno logging symbols; those fixture dependencies
were corrected. An optimized rejection-only fixture then had a zero-file-size
RW/BSS segment and faulted before reporting. The test linker layout now uses
an explicit, file-backed RW segment; all six images meet their real verdicts.
Old failures remain. These units use Linux reporting and an injected identity
boundary, so they do not qualify the native kernel/thread backend or C1/C2/C3.

The native Perfetto clock adapter reuses the qualified C clock selectors
(`CLOCK_MONOTONIC=1`, `CLOCK_REALTIME=2`), signed 64-bit seconds and 16-byte
timespec. Monotonic/raw/boot measurements use the delivered-PIT monotonic
provider; no suspend accounting exists. CPU-accounting measurements use the
separate native `0x4716` service for self thread/process delivered-PIT samples.
They validate the wire metadata and checked microsecond-to-nanosecond conversion.
Zero is valid before the first charged sample; service failures are rejected.
Conversion rejects service failures, negative fields, invalid nanoseconds and
signed nanosecond overflow. `TimeGm` calls the native LLVM calendar provider,
whose current implementation is UTC without a timezone database. Host timegm
and host clock branches stay intact.

`codex-c-perfetto-time-build-t01` / `run-r01` pass sixteen frozen ELF32 images:
O0/O2 normal clocks, INT64_MAX nanosecond boundary, duration helpers, dates from
1900 through 2100, leap dates, month/second normalization and errno preservation.
UTC conversion links the actual pinned LLVM `mktime.cpp` and `time_utils.cpp`,
not a replacement calendar model. Two wrong-expectation images return 1;
twelve service/field/overflow rejection images log and terminate with SIGILL.
Host header compilation and the 19 platform regressions also pass. The clock
boundary, logging/errno sinks and Linux reporting are unit dependencies; this
does not establish new native clock guest behavior, full calendar correctness,
runtime integration of all libc time functions or Content C1/C2/C3.

The earlier CPU-zero implementation incorrectly inferred that the kernel lacked
CPU accounting from the older C clock wrapper. The current kernel already has
that separate service. `codex-c-perfetto-time-build-t02` / `run-r02` correct this
with 36 real ELF32 unit images: O0/O2 normal thread/process values, repeat reads,
zero, largest safe nanosecond conversion, the pinned LLVM calendar cases, two
wrong expectations and 32 wall/CPU service/metadata/overflow refusals. Two
uninjected int80 production objects and the host header also compile.

`codex-c-perfetto-cpu-guest-build-g01` / `run-r01` link the actual uninjected CPU
backend into eight native modules and a current-source observer kernel. At O0
with 32 MiB and O2 with 64 MiB, three successive CPL3 processes compare adapter
results with raw CPU ABI brackets, consume real charged PIT samples and finish
with exact syscall totals and Reap restoration (baselines `0x1d7b` / `0x3d76`).
Each consumer makes twelve comparisons. The wrong consumer exits with a failed
record and QEMU 65; the two valid runs return QEMU 33. The observer programs the
canonical divisor 11931 and the judge rejects incomplete/duplicate records and
broken context/query/accounting fields. This qualifies this coarse self-only
CPU clock adapter, not full POSIX clocks, worker aggregation, SMP accounting or
Content C1/C2/C3.

The GTOS `SystemInfo` backend now uses native process-info `0x4713` for the
current 4096-byte page size and one-CPU scheduler profile. Invalid/unavailable
results log and leave both measurements unset. The build identity is GTOS/i686;
kernel version/release, physical RAM and configured timezone remain unknown.
Per-process page limits are never presented as physical memory capacity.
`PERFETTO_MACHINE_NAME` retains upstream precedence, including an empty value.
Only GTOS replaces `android_utils.cc`; host platform code remains unchanged.

`codex-c-perfetto-system-info-build-i06` / `run-r03` run the real backend and
pinned LLVM `string.cpp` at O0/O2, with syscall/environment/allocation/logging
boundaries injected. They cover repeated valid queries, eight malformed/error
replies, unknown fields, environment precedence, native short/heap strings and
balanced release; both altered expectations return 1. Two production int80
objects and the original host source compile separately; `unit-usi01` passes
19 platform regressions. The first run exposed a test assumption: IA32 libc++
has ten-character inline string capacity, so an eleven-character name also
allocates. The corrected fixture requires eight allocations and eight releases.
`ninja-n50` builds `system_info_gtos.o` in the real formal Content graph. These
checks establish the adapter and build integration, not a new guest query or
Content C1/C2/C3 acceptance.

GTOS `PagedMemory` now owns a native VM handle instead of asking for a POSIX
mmap interface. Reserve retains two uncommitted guard pages; ordinary allocation
commits only the rounded interior. `kDontCommit` leaves the interior uncommitted
until `EnsureCommitted`, which extends it by whole pages without touching prior
contents. Failed initial commits release the reservation, including `kMayFail`.
Move construction/assignment transfer ownership, with self-move preserved.
Page-aligned resident discard uses native DISCARD, preserving access while
zeroing bytes; it makes no resident-charge reduction claim. Unsupported or
uncommitted discard ranges return false. Host backends remain unchanged.

`codex-c-perfetto-memory-build-m01` / `run-r01` execute the actual backend in
sixteen O0/O2 images with a simulated VM syscall endpoint. They check exact
wire layouts and guard exclusion, reserve/commit failure cleanup, zero filling,
rounding, lazy growth, prior-byte preservation, two owners, repeated allocation,
move/self-move, discard/error/range behavior and final release. Two deliberately
wrong expectations return 1; twelve malformed/failed-service/invalid-request
cases log and terminate with SIGILL. Default int80 objects compile separately
without the injected endpoint; the original host source and 19 platform units
also pass. The endpoint models VM outcomes, so this does not prove hardware
page protection, physical accounting, GTOS guest execution or Content C1/C2/C3.

The subsequent `codex-c-perfetto-memory-guest-build-g02` / `run-r03` supplies
that missing hardware evidence for this component. The actual backend, without
its test endpoint, runs at CPL3 in a current-source diagnostic kernel. O0/32MiB
and O2/64MiB each execute 114 consumer comparisons, then four separate modules
trigger exact lower-guard, upper-guard, uncommitted-read and released-write
page faults. Every exit/fault Reap restores the original physical-frame count
(0x1d77 and 0x3d70 respectively). The normal case makes 147 int80 calls; fault
cases make 3/3/2/4. A deliberately wrong consumer causes native kernel failure
and QEMU 65; the successful guests return 33. Each positive transcript also
passes 123 real-judge missing/duplicate/changed-field rejection controls.

The first carrier used the default integer-only FP policy: O0 completed but
O2 faulted with #UD before reservation. The carrier now explicitly activates
the existing `NativeFpSse2` policy, matching Content's SSE2 user profile. All
twelve user ELF byte identities are identical between those builds, so the
successful O2 result is tied to that carrier-policy correction, not weakened
checks or a changed user backend. Old failures remain. The observer kernel
uses actual GDT/runtime/VM/FP/page-fault/Reap sources; it substitutes no memory
policy. This qualifies the page component in that diagnostic profile, not the
ordinary desktop, all libc services, a browser or Content C1/C2/C3.

The formal `codex-c-ninja-n45` graph also compiles the real GTOS page backend,
signal policy and thread checker into their native objects with SDK depfiles
and split debug outputs. `n44` still stops on other platform services such as
file permissions/path removal, pipes, ioctl and system metadata. These native
dependency objects are not the formal shell delegate C1 object.

The GTOS diagnostic sink has no interactive-terminal protocol. The progress
reporter therefore avoids ioctl/termios and reports no interactive progress;
the existing explicit FORCE_COLOR/NO_COLOR precedence stays intact. This does
not implement a terminal service or claim a measured terminal width.

`codex-c-perfetto-console-build-c01` / `run-r01` execute the actual exported
terminal/color decision functions at O0/O2 in both Chromium and standalone
configurations. The standalone fixture changes only the real generated
Chromium-build switch. Each normal image passes 25 checks over missing/empty
environment values, TERM variants, override precedence and repeated calls;
the wrong-expectation image returns 1. Host source compilation and 19 platform
regressions pass. Environment lookup is an injected unit dependency, and Linux
syscalls only report results. Full progress rendering, native FILE streams and
guest console behavior are outside this unit's claim.

Native AtomicFile now inspects the actual store with `stat` (there are no
symlinks) and refuses inspection failures other than ENOENT before creating a
temporary. It retains regular-file checks, exclusive creation, duplicate/flush/
close ordering, rename publication and destructor cleanup. Unix permission
preservation is excluded only for GTOS, whose file ABI has no permission bits.

`codex-c-perfetto-atomic-file-build-a01` / `run-r01` execute the real AtomicFile,
file helpers, POSIX adapter and pinned libc++ string/UUID formatting at O0/O2.
Nine scenarios repeat twice: absent/regular/directory destination, inspection
I/O/unavailable errors, create collision, sync failure, rename failure and
uncommitted destruction. Exact native request/close/cleanup ordering and final
allocation balance are checked. The syscall, allocation, UUID-generation and
error-formatting boundaries are injected; this is not a guest/durability or
entropy qualification. Both wrong-expectation images fail. Two default native
objects and the original host source compile; `unit-uaf01` passes 19 platform
regressions. Formal `ninja-n52` builds actual GTOS `atomic_file.o` and
`file_utils.o` with the real SDK, depfiles and split debug outputs. C1 remains
the shell delegate translation unit.

GTOS `EventFd` now owns a native manual-reset event, using CREATE/SET/RESET
and a dedicated CLOSE function. Its `fd()` is an EVENT_WAIT handle, never a
file descriptor. Notifications coalesce and clearing resets the signal, as
with the existing Windows profile. The runtime must attach NativeEvents before
activation; absence or invalid service responses fail. Move assignment closes
the replaced event, and moved-from objects cannot notify. This does not add a
POSIX poll or byte-pipe service.

`codex-c-perfetto-event-build-e01` / `run-r01` run thirty ELF32 images: O0/O2
normal lifecycle/moves, wrong expectations, unavailable/invalid CREATE replies,
SET/RESET/CLOSE errors and moved-from misuse. Separate cases require explicit
refusal of native byte pipes and stable file-identity comparison. The latter
cannot compare zeroed stat inode fields: its current caller is the traceconv
bundle tool. Both facilities remain unavailable; the refusal is not an
implementation or a runtime acceptance claim. Event wakeups use the actual
native event service instead of a pipe fallback. Host source and nineteen
platform regressions compile/pass, and `ninja-n55` builds the real native event,
pipe-policy and file-helper objects.

`codex-c-perfetto-event-guest-build-g03` / `run-r01` qualify the actual int80
backend against current-source NativeEvents in CPL3. Normal mode makes 72
comparisons and 131 syscalls, including 56 zero-timeout waits, 16 creations and
16 closes. Manual signal persistence, coalescing, reset and replacement moves
are checked. Two #UD cases retain one event each (explicit fault and moved-from
refusal); Reap releases exactly one and restores the frame baseline. O0/32 MiB
and O2/64 MiB pass (baselines `0x1d7e`/`0x3d76`), and a real wrong consumer is
rejected. The judge rejects 111 missing/duplicate/changed-field mutations per
valid capture. This does not qualify blocking waits or cross-thread races.
The carrier exposed a real GCC Werror indentation ambiguity in NativeEvents;
the return is now a separate line, preserving its semantics.

The shared guest builder's memory and CPU selections are also rebuilt and
rerun (`memory g04/r05`, `cpu g02/r02`), retaining all normal/fault/Reap and
negative-consumer results. `codex-a-core-c03` passes the four current-source
paging/process-memory/native-process/FP components. Formal Content C1/C2/C3
remain separate milestones.
