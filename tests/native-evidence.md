# Native evidence obligations

Run `node tests/native-evidence.test.mjs <control.json>`. The control supplies
`task`, a `module` pin (`path`, `bytes`, `sha256`), and a new `result_path`.
The test imports that exact module. Data marked `TEST_ONLY` is never executed.

Metadata and demangling have no object obligation. Compiling requires a nonempty
object and its paired dependency file. A relocatable link requires its object.
Deleting an expected output from a product record cannot relax that category's
obligation. All categories require matching argv, zero exit, successful observed
probes and isolation, a clean measured final process group, and a raw capture
whose path, byte count and digest match the child record. An empty real capture
is valid; an absent capture is not an empty capture.

The caller reads actual spec, inner report, raw bytes and product files, computes
the pins, and binds the expected command to its frozen command table before
calling `inspectNativeEvidence`. This pure function does not authenticate files
by itself. Registered attempt/result and budget checks remain separate gates.
ELF format, dependency contents, symbol ownership and kernel behavior are also
separate checks. A passing result proves no target code ran correctly.

Tests cover all four categories, missing and mismatched evidence, failed or
unobserved isolation, probes and exit status, cleanup, and 512 interleaved
evaluations for repeatability and absence of input mutation. They run no native
tool, kernel instruction, IRQ, GUI or network operation.
