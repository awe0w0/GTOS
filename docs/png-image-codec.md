# Native PNG raster codec

The independent PNG component decodes real static PNG data with pinned Wuffs
0.3.5, then invokes the existing GTOS Skia integer conversion to produce
premultiplied RGBA. The complete application has run in GTOS i386 through the
existing ABI1 loader. It provides a raster decoder; browser execution and a
window/surface submission API remain outstanding.

## Storage and failure contract

The C API is declared in `apps/png_image_codec/png_decode.h` and `png_pixel.h`.
`gtos_png_context_bytes()` gives the decoder size: the qualified i386 build uses
44,460 bytes, while the x86-64 host build uses 44,632 bytes. Context requires
eight-byte alignment and must reside in caller storage outside the user stack.
Requirements needs its natural alignment. Callers own accessible memory for
the call duration; the component does not validate arbitrary kernel addresses.

`gtos_png_inspect()` checks the complete chunk envelope and configures Wuffs.
It returns dimensions and exact work requirements without decompressing IDAT.
Successful inspection therefore does not establish compressed-data validity.
`gtos_png_decode_bgra()` produces straight-alpha BGRA. `gtos_png_decode_rgba()`
then converts that scratch result to premultiplied RGBA using the unchanged
Skia nearest `(channel * alpha) / 255` integer implementation.

All supplied regions, including full declared capacities and requirements,
must be disjoint. Pointer-wrap and overlap checks precede buffer access. Stride
must hold four bytes per pixel; capacity must hold
`(height - 1) * stride + width * 4`. Final RGBA and requirements remain unchanged
on every error. Context, work and active BGRA scratch may change; padding and
spare pixel capacity remain untouched. The reusable component allocates no
memory and introduces no syscall, mutable global state, host CRT or FP/SIMD.

## PNG behavior

All static PNG input formats supported by the pinned decoder remain enabled,
including packed samples, transparency, 16-bit channels, filters 0 through 4
and Adam7. The wrapper checks signature, bounded lengths/types, every critical
and ancillary CRC, IHDR legality, PLTE/tRNS rules, consecutive IDAT, and one
empty IEND at exact EOF. Actual indexed decoding validates reconstructed
palette indices before expanding backwards into BGRA.

Success additionally requires Wuffs to consume through the last nonempty IDAT,
report end-of-data on the next frame configuration, consume exact input EOF,
and report one frame. Extra compressed bytes fail. Empty trailing consecutive
IDAT chunks remain valid. Grayscale/RGB tRNS transparent pixels retain their
source RGB in the straight-alpha result; the wrapper restores the validated
key after complete decoding because pinned Wuffs clears those hidden colors.
Wuffs still decides exact 16-bit transparency matching before conversion.

Unknown critical extensions and APNG return unsupported after generic envelope
validation. This result does not certify their payload semantics. Ancillary
metadata is CRC-checked without interpretation; color management and animation
are outside this raster API. Fixture coverage limits are documented separately
in `apps/png_image_codec/FIXTURE_EVIDENCE.md`.

## Explicit release adaptation

The dependency cache retains byte-exact upstream source and licenses. The
builder invokes `tools/adapt-wuffs-png.py` into each fresh output's `derived/`
directory and records source, generator and diff hashes. It explicitly records
`function_bodies_modified: true` and `generic_upcasting_supported: false`.
`--wuffs-profile upstream` still selects the original control.

The release profile uses one 256-entry reflected IEEE CRC table with a real
bytewise rolling checksum instead of the original 16-row table. It removes
unused generic vtable registration from five concrete initializers while
preserving struct layouts, zeroing, magic checks and actual initialization.
Pixel preparation exposes the wrapper's existing SRC blend and two actual
destinations, BGRA and indexed BGRA, while retaining all thirteen upstream
source cases and their conversion helper bodies. These private choices do not
remove any PNG input format or expose a substitute Wuffs generic interface.
The smaller CRC implementation's throughput has not been benchmarked.

The linker maps ordinary ELF headers into the RX segment and places entry
after them. Page-offset congruence, RX/RW separation and existing address/page
limits are retained. Standard stripping removes symbol metadata and rewrites
the retained section-table metadata. It
changes section-table fields in the mapped ELF header, while program-header
geometry, executable bytes, rodata, initialized data and BSS remain equivalent.
There is no compressed executable or guest decompressor.

## Reproduce

Use explicit cache/output paths and an existing qualified toolchain. Every
output directory must be new. On DESKTOP-NNIL5M6 the persistent paths are on F:

```sh
source /mnt/f/GTOS-Chromium/scripts/env.sh
cd /mnt/f/GTOS-Chromium/sources/GTOS-png-image-codec-c237
python3 tools/build-png-image-codec.py /mnt/f/GTOS-Chromium/artifacts/png-NEW \
  --dependency-cache /mnt/f/GTOS-Chromium/cache/native-wuffs-dependencies \
  --clang /mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang \
  --host-cc /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/gcc \
  --host-cxx /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++ \
  --wuffs-profile gtos-release
python3 tools/verify-wuffs-png-release.py /mnt/f/GTOS-Chromium/artifacts/png-proof-NEW \
  --dependency-cache /mnt/f/GTOS-Chromium/cache/native-wuffs-dependencies \
  --derived /mnt/f/GTOS-Chromium/artifacts/png-NEW/derived \
  --cc /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/gcc
env GTOS_QEMU_DATA_DIR=/mnt/f/GTOS-Chromium/toolchains/gtos-runtime/root/usr/share/qemu \
  /usr/bin/python3 tests/png_pixels_qemu.py \
  /mnt/f/GTOS-Chromium/artifacts/png-NEW /mnt/f/GTOS-Chromium/artifacts/png-guest-NEW \
  --kernel-stage /path/to/qualified/iso-stage \
  --kernel-sha256 ACTUAL_QUALIFIED_KERNEL_SHA256 \
  --host-cc /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/gcc \
  --host-cxx /mnt/f/GTOS-Chromium/toolchains/ubuntu-noble-gcc13/bin/g++
```

The guest driver verifies the build revision, unchanged tracked source,
manifest inputs, selected stripped ELF hash, 64 KiB admission, copied kernel
hash and exact ISO-extracted ELF bytes. It creates only its own test disks.
The existing system Python provides Pillow; depot_tools Python does not.
No system installation or settings change was needed for qualification.

## Standalone qualification, 2026-10-07

Qualification used dev `c237c70af9de32d097f5b0812c5d34a023f69d29` and its unchanged
previously tested O2 kernel, SHA256
`4da166ebec7c8d1d269051bea3f11f6116e541dd4cc41368ec99d112c567d881`.
Both gtos-release O0 and O2 host ASan/UBSan runs passed all 46 fixtures, 3,866 truncation
prefixes, 2,000 mutations including 500 source-oracle positive mutations,
30 alias cases, 164 boundary cases and 8,146 calls per run. Actual upstream and
release implementations also passed 40,965 independent CRC cases and 4,290
pixel preparation cases with matching differential output and layouts.

The full original guest fixture table, all 46 cases and both independent pixel
oracles are preserved. The stripped ELF is **61,672 bytes**, SHA256
`bbdadb066c66d12db9bb1d8f253aecebd7f57e65746936ea853714c38d3786e7`.
Its unstripped symbol counterpart is 74,256 bytes, SHA256
`0566a01808f661d57938deb6cf19e29d4f3cfedacbec0c07ea968f6f71869bef`.
The ELF is i386 EXEC, closes its real dependency graph, and has no interpreter,
TLS/dynamic segment, relocations, RWX or FP/SIMD. RX uses 15 pages and RW uses
18; the two existing stack pages bring the total to **35 of 256 pages**.

The complete machine-code stack bound is **2,752 bytes** from `_start`, leaving
**5,424 bytes** against the loader's 8,176 usable bytes after its 16-byte initial
reservation. Analysis includes incoming return addresses, call-site arguments,
hidden struct-return cleanup, alignment and all bounded indirect targets.
Instruction bytes and continuous function coverage match the exact guest ELF;
all fifteen indirect calls and nineteen switch tables are resolved. There are
no unresolved stack effects or reachable recursion. The critical path is the
RGBA/BGRA wrapper through PNG, zlib, DEFLATE and Huffman initialization. The
indexed 1 KiB palette is included in the 1,308-byte BGRA frame.

Real GTOS guest acceptance passed at 64 MiB/4 CPUs, 32 MiB/1 CPU with persisted
Chinese locale, and 96 MiB/4 CPUs. Each completed the 46-case native fixture and
pixel checks plus every positive truncation prefix, exited zero, and reached
native/scheduler/desktop completion. Actual keyboard/mouse disk reload and
Catch right-arrow input worked; the paddle moved from x=423.5 to x=563.5.
Every disk byte remained unchanged, and available AP workers completed their
real periodic jobs. PNG output is checked in the native app; the desktop
screenshots establish coexistence, rather than PNG presentation through a new
surface API. The first QEMU attempt failed before boot due to shell firmware
selection; raw evidence remains, and only guest checks were retried.

Exact local evidence is in `artifacts/png-release-final-20261007T102400Z`,
`artifacts/png-release-differential-20261007T102500Z`,
`artifacts/png-stack-release-20261007T102219Z`, and
`artifacts/png-native-guest-firmware-corrected-20261007T102800Z` under the F
workspace. Earlier original-source size failures and the optional compact
fixture experiment are preserved; the compact fixture variant was not selected.

## Local GN qualification

The local Chromium `//tools/gtos/png_image_codec:png_pixels_probe` graph builds
three isolated targets and twenty objects, including the existing real scalar
compiler runtime. It passed native instruction/identity/dependency audits.
All ten object files shared with the standalone application, including their
relocation records, are byte-identical. Host controls are reused for these
proven identical sources and profile; GN itself did not rerun the host tests.

Different link input ordering produces a different exact ELF, also 61,672 bytes:
SHA256 `9da3e7f4017711e88b741bac9536df4f01d024390885b8bd4e700b4b12d1e094`.
All allocated definitions match, with differences confined to the entry and
text layout. This actual GN binary separately passed full machine-code stack
analysis with the same 2,752-byte bound, then all three GTOS guest configurations
and their desktop/input/AP/disk checks. The standalone binary was not substituted.

Evidence: `logs/gtos-png-gn-release-status.json`,
`artifacts/gtos-png-image-codec-gn-release-20261007T102654Z`,
`artifacts/png-stack-gn-release-20261007T103153Z`, and
`artifacts/png-gn-native-guest-20261007T103400Z` in the F workspace.
The GN definition stays local; no independent Chromium fork was created or
published and no full Chromium/V8 graph was rebuilt for this module.

## Remaining browser dependencies

The 64 KiB file cap, 256-page budget, 8 KiB stack, ABI1, kernel and process/VM/FP
implementation are unchanged. A single 800x600 RGBA image needs 1,920,000 bytes,
already above the present 1 MiB mapping budget before context and full-image
filtered work storage. Chromium still needs real process/VM/JIT/thread/TLS,
synchronization, runtime, files/resources, surfaces/input, IPC and networking
contracts. No GTOS browser startup, webpage browsing or Linux-ABI substitution
is claimed. The Linux x86-64 host requirement does not require a 64-bit GTOS
target. Local Chromium GN integration remains a separate component build.

Primary references: [PNG3](https://www.w3.org/TR/png-3/),
[pinned Wuffs source](https://skia.googlesource.com/external/github.com/google/wuffs-mirror-release-c.git/+/7411f488fe2e2c205c3d3b3d28638b7356522930/release/c/wuffs-v0.3.c),
and [Chromium Linux host requirements](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md).
