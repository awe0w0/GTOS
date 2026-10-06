# Native Skia integer alpha and pixel conversion

This independent app qualifies two real Skia integer helpers and a bounded
BGRA straight-alpha to RGBA premultiplied-alpha row converter in GTOS ABI1.
It does not instantiate full Skia, allocate or submit a display surface, or
establish Chromium, V8, JIT, networking or an ELF64 user ABI.

The helpers `SkMul16ShiftRound` and `SkMulDiv255Round` are verbatim from Skia
revision `00987348988a7355d6437a917fa153dde9ce6c82`. The original headers,
BSD license, hashes and extraction location are retained beside the component.
`source-manifest.json` is the immutable extraction receipt; its build/guest
fields describe preparation time only. Each build and guest run emits its own
current result. Only unsigned scalar type names and an active fatal assertion
handler adapt the extracted code. There are no empty OS or allocation APIs.

## Contract

`gtos_skia::PremultiplyBgraRows` reads caller-owned accessible BGRA buffers and
writes caller-owned accessible RGBA buffers. Both lengths and strides are bytes;
the width and height are pixels. Each RGB output channel is the corresponding
input channel multiplied by alpha with nearest-integer rounding over 255. Alpha
is preserved. Row padding is unchanged. All geometry, length, stride, address
overflow and overlap checks complete before the first output write.

The whole row spans, including padding between rows, must not overlap. Passing
a numeric address does not prove that memory is mapped or belongs to a process:
this is a trusted component-call contract, not syscall pointer validation.
Callers must provide readable/writable memory for the validated spans. The
component allocates no storage and retains no state; a program supplies a
non-returning `gtos_skia_assert_failure` handler for violated upstream assertions.

GTOS's compositor currently stores opaque `0x00RRGGBB` pixels, and its
`Framebuffer::Blend` alpha is 0..15. This RGBA output must not be passed directly
to either interface or blended twice. A future surface backend must agree on
format, stride, ownership, damage and completion before integration. An 800x600
32-bit surface alone needs 1,920,000 bytes, beyond the current process's roughly
1 MiB mapped-page budget. This module changes neither that budget nor any kernel
graphics, FP, loader or ABI policy.

## Reproduce

Use a Linux development host with Python 3, Clang/LLD/LLVM tools, GCC C++, GNU
binutils, GRUB, xorriso and QEMU. The existing kernel font generator needs Pillow.
All source is local; this module's builder performs no downloads or system
installation. Select fresh output directories on a drive with space:

```sh
./tests/skia_alpha_probe_test.sh
python3 tools/build-skia-alpha-probe.py /path/to/new-build \
  --clang /path/to/clang --host-cxx /path/to/g++
make -j2 PYTHON=python3 GTOS-native-test.iso
python3 tests/skia_alpha_probe_qemu.py /path/to/new-build /path/to/new-evidence
```

The builder emits a reusable native archive and a static ELF32/i386 test app
with `i686-unknown-none-elf`, no host OS identity, no C++ runtime, no FP/SIMD,
disjoint RX/RW loads and no undefined symbols in the final app. It checks the
unchanged boot demonstration's 64 KiB external-file limit, separately from the
generic ELF validator's trusted-file limit. The app uses only ABI1 version,
bounded debug write, yield and exit. The runner reuses the existing ELF32 host
validator from the Wuffs qualification module and makes a private ISO by copying
a staged kernel and replacing only its external test ELF. It records the staged
kernel's actual hash; a kernel revision is not inferred from the app source.

For rootless toolchains, use their existing environment. `GTOS_QEMU_DATA_DIR`
selects the runtime's `usr/share/qemu`; `--runtime-root` can put QEMU socket files
on a filesystem supporting Unix sockets. Evidence is copied to the selected
output directory afterward. The ordinary platform probe remains unchanged.

## Acceptance

Host O0/O2 runs use ASan/UBSan and independently check all 65,536 byte channel /
alpha pairs against rational division. Host and guest also check 2,000
deterministic padded rectangles, alpha endpoints, unchanged source and output
guards, plus rejected null/geometry/stride/length/overlap/address-overflow cases
with no output writes. Guest acceptance requires normal exit and reclamation,
AP work, desktop startup, app installation/launch and actual game keyboard input
with 64 MiB/4 CPUs, 32 MiB/1 CPU and 128 MiB/8 CPUs. The desktop screenshot proves
coexistence and input, not presentation of this component's pixel output.

Initial qualification on 2026-10-06 passed all these checks using official Clang
24 `62397f8b-57`, GCC 13 host sanitizers and an unchanged GTOS dev19b staged
kernel. The native app was 8,944 bytes, SHA-256
`fbeb5e84f2297276380709f8406e72e8e3c7b9b8242a9d864408214bd04fef3b`.
The initial kernel hash was
`33b0b136e9ac4a5302aa081e056f1f352aa475dd02135ff97809c34160b86ca4`.
Subsequent module build and guest manifests contain their actual artifact hashes.
