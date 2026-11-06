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
