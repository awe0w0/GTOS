# Native path and assembly-request checks

`tools/native-paths.mjs` is the shared path boundary used before creating guarded-build directories and deriving assembler requests. A canonical Linux path under the declared work/source roots maps to an Ubuntu-20.04 UNC path. Host directory creation refuses unmapped Linux paths and paths outside the declared report/work/source roots. Assembly argv comes from the registered stage, so a retry cannot silently inherit another stage's output.

Run the real module's tests with Node:

```text
node tests/native-paths.test.mjs control.json
```

The control has `module: {path, bytes, sha256}`, a new `fixture_root`, and a new `result_path`. Use absolute host paths and calculate the module's SHA256 from its actual bytes. All test filesystem writes stay in `fixture_root`; native Linux paths in the cases are data, and no WSL program or assembler is executed. The suite imports the pinned module itself, checks mapping/rejection/stage derivation, and exercises real directory creation and an occupied-file refusal.

The context comes from the build's pinned authority and includes `report_root`, canonical disjoint `/var/tmp/` work/source roots, `tools.as`, and `native_choices`. Each assembler choice has its category, source, cwd, output, and five-token argv. The returned argv is a copy. Callers must use the returned request and must still check actual native output parents and tool pins before launching.

These functions are not an OS sandbox. They enforce the declared logical boundary, but do not protect against a malicious authority, symlink replacement, or concurrent directory changes. A native metadata pass must confirm the actual files, directories and read-only source view. Passing these tests does not validate AP startup, interrupt behaviour, assembler output, SMP safety, or kernel execution.
