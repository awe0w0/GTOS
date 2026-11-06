# GTOS Content platform continuation

These patches extend the committed `port-candidate-r20` in a new private view.
They preserve the original files and qualification records. `apply.py` verifies
the exact input digests, materializes symlink parents using the existing
composer, and refuses shared source roots, changed inputs or a second application.

The changes add GTOS to the platform-independent menu/user-data targets, select
ELF32 for GTOS NASM, and give GRIT an explicit GTOS identity. GRIT must receive
`-t gtos`; otherwise resource generation can inherit the build host's platform.
GTOS is neither Linux nor POSIX in the resource condition evaluator.

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

   Material arguments: `gtos_clang_bin`, `gtos_clang_include`, `gtos_sdk_dir`,
   `gtos_libcxx_include`, `gtos_compiler_rt_source_dir`, `gtos_png_source_dir`,
   `gtos_png_skia_source_dir`, `gtos_png_gif_source_dir`,
   `gtos_png_wuffs_include_dir`, and `gtos_libxml_config_dir`.

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

2026-11-06: `codex-c-unit-u05` passed all 10 tests under the original guard.
The formal Content run advanced from the `ui/menus` platform assertion through
the existing Aura/Views dependency path. The broad graph reached an unrelated
Chrome supervised-user assertion; the focused graph excluded that branch and
reached Linux host code-generator configuration. No Content CXX object, link or
GTOS Content entry has yet been produced. Full raw stage records remain in the
existing packet's `evidence/codex-c-*` records, including failed attempts.
