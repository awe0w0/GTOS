# Native scalar Wuffs GIF qualification

This separate app runs the real Wuffs 0.3.5 first-frame GIF decoder inside GTOS's
existing i386 native process ABI1. It uses caller-owned decoder storage and BGRA
output and does not require allocation, libc, filesystem, threads, display or
network services. This is one genuine Chromium dependency; it is not Chromium
running in GTOS and does not establish a browser, V8, JIT or ELF64 user ABI.

The app uses only the published ABI1 version, bounded debug write, yield and exit
calls. The ordinary browser platform probe and kernel remain unchanged. Its
private test ISO replaces only the copied external fixture, never the original
probe source. The larger app uses the existing multi-page ELF32 loader and keeps
RX/RW segments disjoint with at most 224 image pages, below the native runtime's
254-image-page limit plus two stack pages.

The boot demonstration has a separate **64 KiB file limit** for each external
ELF in `src/kernel.cpp:StartNativeDemo`. This is stricter than the generic
`NativeRuntime::TrustedImage` 2 MiB trusted-file limit. Large zero-filled BSS does
not count as file bytes. This module's tested ELF is 38,484 bytes; the builder and
guest runner both check the boot-demo limit before starting. Passing the generic
validator alone does not prove admission through that boot entry point.

## Reproduce

Run from a Linux x86-64 development host with Python 3, Clang/LLD/LLVM tools, GCC,
GNU binutils, GRUB, xorriso and QEMU available. The existing kernel font generator
also needs Pillow. No global installation is performed
by these commands. Select output and cache directories on a drive with space:

```sh
python3 tools/build-wuffs-gif-probe.py /path/to/new-build \
  --dependency-cache /path/to/dependency-cache --clang /path/to/clang
make -j2 PYTHON=python3 GTOS-native-test.iso
python3 tests/wuffs_gif_probe_qemu.py /path/to/new-build /path/to/new-evidence
```

`tools/build-wuffs-gif-probe.sh` and `tests/wuffs_gif_probe_test.sh` wrap the
builder. The latter qualifies the host and native artifacts; guest acceptance is
the separate QEMU command. Both require fresh output directories. A reusable
dependency cache must match every pinned byte count and SHA-256. Downloads come
from the exact Chromium Wuffs release mirror revision and official LLVM sources
listed in `apps/wuffs_gif_probe/dependencies.json`; their licenses are preserved
alongside cached dependencies. No large upstream generated file is checked into
this repository.

For an unpacked QEMU runtime, set `GTOS_QEMU_DATA_DIR` to its existing
`usr/share/qemu` directory and use that runtime's normal library environment.
`--runtime-root` can place transient QEMU logs/disks in an existing temporary
directory; the complete evidence is copied to the requested output afterward.

The builder compiles Wuffs BASE, PIXCONV, GIF and LZW with CPU acceleration and
unused convenience interfaces disabled. The production object has no unresolved
symbols or mutable global state. Actual byte primitives implement memcpy,
memmove, memset and memcmp. Any upstream allocation convenience functions are
unreachable and removed; no fake allocator or empty API implementation is used.

## Checks and failure modes

The native app tests known opaque and transparent pixels, the first frame of an
animated GIF, malformed data, truncation, undersized output, hostile dimensions,
alignment, overlap-safe memmove and 2,000 deterministic mutations. Width and
height publish only on success; failure can leave partial output. Input, context
and output must not overlap. These are trusted decoder-call preconditions, not
GTOS syscall pointer validation. The context must be 8-byte aligned: a uint64_t
union alone provides only 4-byte alignment on i386, so the test uses `_Alignas(8)`
and checks a deliberately misaligned context.

Clang's precompiled i386 Linux `__ashldi3` assembly can use SSE2 even for integer
shifts. This app instead compiles the pinned, unmodified official compiler-rt C
implementation with the same no-MMX/no-SSE/no-SSE2 flags. Native disassembly is
checked for FP/SIMD register use and unresolved symbols. This does not claim that
all compiler runtime helpers are qualified.

Host tests retain ASan/UBSan and use a non-PIE executable to avoid a reproduced
WSL PIE/ASan address collision. Guest tests execute the same native application
with 64 MiB/4 CPUs, 32 MiB/1 CPU and 128 MiB/8 CPUs. They require decoder success,
exit zero, reaping three native processes, scheduler and AP work, desktop startup,
application installation, launch and keyboard input. The decoded pixels are
checked within the guest process; the game screenshot shows desktop coexistence,
not a rendered decoder image. Each run records hashes, commands, logs and pass or
failure in new output directories. The published ELF32 validator is separately
compiled with ASan/UBSan and checks the app before booting its private ISO.

## Recorded qualification

On 2026-10-06 the app built with official Clang 24
`llvmorg-24-init-7747-g62397f8b-57` and passed host GCC 9 ASan/UBSan. It then
passed all three configurations above using the unmodified dev kernel at
`19b484b571f8c2f89ed320153f48c80c69c8fdee`, with original tracked source hashes
unchanged. App ELF SHA-256:
`ec99ccf9ee496949221d0b58a1c93772234683d2da0410b982d1023ccea7fb87`.
Guest evidence contains decoder markers, exit zero, three native reaps, AP job
checks and desktop game input in each case. This records that tested revision
and toolchain; other revisions require their own acceptance runs.
