# PNG component self-review

The C ABI and RGBA bridge implement actual pinned Wuffs PNG decoding and the
existing Skia integer conversion. They expose caller-owned storage, exact work
requirements, complete-decoding results and explicit failure statuses. Source,
oracle, native instruction, full call-chain stack and real guest reviews are
complete for the artifacts recorded in `docs/png-image-codec.md`.

## Input and publication checks

All numeric pointer spans, alignments, complete capacities and pairwise aliases
are checked before dereference. Callers retain ownership of accessible memory;
these checks do not invent kernel validation of arbitrary addresses. Row spans
use widened arithmetic. Requirements and final RGBA publish only on success;
active scratch may change, while padding and spare pixel capacity are preserved.

The wrapper checks every envelope CRC and core PNG chunk constraint before
decoding, then verifies actual final IDAT position, next-frame end-of-data,
exact EOF and frame count. Indexed reconstruction validates every palette
index before expansion. Grayscale/RGB tRNS hidden RGB is restored only after
complete decoding for zero-alpha pixels of formats without intrinsic alpha.
Wuffs retains the exact transparency decision. Inspection remains a config/
requirements query and does not certify compressed data.

Ancillary metadata is framed and CRC-checked without interpretation. APNG and
unknown critical extensions return unsupported after generic envelope checks;
their payload semantics are not certified. The fixture coverage limits are
explicit in `FIXTURE_EVIDENCE.md` and do not redefine valid formats as corrupt.

## Release provenance and behavior

The upstream cache remains byte-exact SHA256
`82c6741dd751eb962a287882991836498526eb02dbe0c7f19adc01810b8bac96` with verified
licenses. The explicit generator produces a separate modified source SHA256
`efdc0257e2512777e92ca4b4c350b738ba09b1b826749cbbf03c39d52efa4ae3` and full diff.
The manifest truthfully records modified bodies and no generic upcasting.

The one-row CRC performs the real reflected IEEE rolling calculation. Only
unused generic registrations are removed from five actual initializers;
layout, magic, zeroing and concrete initialization remain. Pixel preparation
uses SRC and the two destinations already chosen internally by this API,
preserving thirteen source cases and their upstream helpers. There is no empty
decoder stub, ignored CRC, input-format reduction or oracle alteration.

The full original fixture table, all 46 cases and both oracles remain in the
native application. The optional compact fixture experiment was not selected.
The linker uses ordinary mapped ELF headers and standard stripping; executable
and data semantics remain intact. There is no executable compression.

## Evidence and scope

The release profile passes O0/O2 ASan/UBSan: 46 fixtures, 3,866 prefixes, 2,000
mutations including 500 independently expected positives, 30 alias cases,
164 boundaries and 8,146 calls per run. Actual upstream/release CRC and pixel
preparation controls pass 40,965 and 4,290 differential cases respectively.

The standalone and local GN images are each 61,672 bytes and use 35 pages
including the two stack pages. Both exact images pass machine-code stack
analysis: 2,752 bytes from entry, 5,424 bytes of conservative headroom. The
1 KiB indexed palette is included in the 1,308-byte BGRA function frame.
All fifteen indirect calls and nineteen switch tables are resolved; instruction
coverage and raw bytes match the guest images. No recursion or unknown stack
effects remain. Native dependency closure and scalar-code audits pass.

Both exact images separately pass real GTOS at 64 MiB/4 CPUs, 32 MiB/1 CPU
with Chinese locale, and 96 MiB/4 CPUs. Every case completes the PNG/pixel/prefix
checks and exits zero. Desktop keyboard/mouse, installed Catch input, available
AP jobs and unchanged disk bytes are verified. Screenshots establish desktop
coexistence, rather than a new PNG display surface.

Only new independent app, tool, test and documentation files are introduced.
Existing Skia and memory/runtime implementations are reused with actual
dependencies and active assertions. Kernel/process/VM/FP/ELF/ABI implementations,
the 64 KiB file limit, 256-page quota and 8 KiB stack are unchanged. Browser,
surface, network and general runtime integration remain outstanding.
