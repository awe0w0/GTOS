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
