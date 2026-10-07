# Native immutable resources v1

GTOS native i386 applications can read fixed public resources packed into the
kernel. Resource ID 1 is a 16x16 RGBA PNG. It is stable readonly content for the
kernel lifetime and contains no private application data. This interface is not
a general filesystem, host-file access service, or complete browser resource
layer. Native image presentation is outside this module.

The existing native ABI remains version 1. Resource calls are an additive,
independently versioned extension declared in `include/process/resource_abi.h`.
Both use `int 0x80`, EAX for the operation/result, and EBX/ECX for arguments.
Non-result GPRs and segment registers are preserved as before. Records contain
unsigned 32-bit little-endian fields and no native pointers returned by GTOS.

| Operation | Input | Result |
| --- | --- | --- |
| INFO `0x4705` | EBX=resource ID; ECX=user destination | 0 and exactly 16 bytes `{version,type,bytes,flags}`, or a negative error |
| READ `0x4706` | EBX=user request; ECX=20 | Copied byte count, zero at EOF, or a negative error |

READ's 20-byte record is `{version,id,offset,destination,length}`. INFO reports
resource API version 1, PNG type 1 and READONLY|PUBLIC flags. An older kernel
returns its existing unsupported-call error, so clients can query support with
INFO. No handles, registration, mutation, open, mapping or path operation exists.

Each READ request is capped at 256 bytes. After checking offset <= resource
length, the service computes available bytes by subtraction and transfers
`min(length, available)`. A request can return a short read at the end. Zero-byte
reads still validate destination inside the existing user arena
`[0x40000000,0xC0000000)`; an empty range need not be mapped. Null/kernel addresses
are rejected at EOF as well. Only the actual transferred range needs to be
writable.

The dispatcher checks the exact wire size before touching the request. It then
copies the entire readable descriptor into a kernel snapshot using existing
`CopyFromUser`. Validation precedence is size, readable request, version, ID,
length cap, offset, writable destination. INFO checks ID before destination.
Both result paths use existing `CopyToUser`, which validates the entire range
before any output changes. Successful output may overlap the snapshotted
request. On any error, user output remains unchanged.

| Error | Value |
| --- | --- |
| Unknown public resource ID | -2 |
| Requested length exceeds 256 | -7 |
| Invalid request/output address or permissions | -14 |
| Request size is not 20 | -22 |
| Offset is beyond EOF | -34 |
| Unsupported resource API version | -38 |

The catalog and PNG are compile-time const data in the existing kernel readonly
region. No process mappings, permissions, ELF loader, stack/page/file limits or
FP policy changed. The implementation exposes no kernel source address and
retains no user pointer.

`tools/generate-native-png-resource.py --check` verifies the exact 1108-byte PNG,
its source pixels, kernel literals, and independent straight-BGRA/premultiplied
RGBA oracles. PNG SHA256 is
`87fe7012f8bc06650f725dd7f584793abf1457e490a52d6ae1de3477a2d2aaf8`.
Hashes provide reproducibility; runtime trust comes from the fixed compiled
catalog. The kernel-linked bytes were independently extracted and matched inside
the existing readonly region.

The separate `apps/png_resource_probe` application embeds pixel oracles and a
CRC, but no PNG input. It reads INFO, retrieves the PNG through five READ calls,
checks all input via CRC, inspects requirements and invokes the unchanged
qualified Wuffs/Skia decoder. It compares all 1024 BGRA and 1024 premultiplied
RGBA bytes. It also checks descriptor/output page crossings, overlap snapshots,
invalid sizes/versions/IDs/addresses/offsets/lengths, atomic failed copies,
EOF/zero semantics, syscall registers/segments and immutable rereads.

Validated on DESKTOP-NNIL5M6 / Ubuntu-20.04 WSL, base f7e6a47:

- New service tests: GCC13 O0/O2 ASan+UBSan, 49,427,220 checks per run.
- Exact consumer: freestanding i386 ELF, 53,480 bytes; 35 load pages plus two
  stack pages, within existing 65,536-byte and 256-page limits. Actual ELF32
  validator and scalar instruction audit pass.
- Exact complete `_start` stack bound: 2,784 / 8,176 usable bytes, including
  arguments, return addresses, alignment, hidden returns and indexed palette;
  5,392 bytes headroom and no unresolved effects.
- Three actual resource guests: 64M/4CPU, 32M/1CPU with persisted zh-CN,
  96M/4CPU. Resource decode/pixels, exit 0, three-process reclamation,
  scheduler/AP workers, keyboard/mouse Reload and Catch input pass. Disk bytes
  remain unchanged. ISO-extracted consumer and kernel hashes are exact.
- Original ordinary ABI1 guest passes with the new kernel. Original PNG
  qualifier, all 46 fixtures and pixel oracles remain byte-identical. PNG core
  host qualification is reused only after exact source/archive/member checks;
  no new execution of that matrix is claimed.

Native ELF SHA256:
`5674146f2d85b608e845b6b43556e2d9f174d8e05e0a2836085ffbdd1b616891`.
Kernel SHA256:
`b48caefee0b39b6cf7002045e7783f07f7f7db072b00b175492487dbd13265a2`.
Guest console markers confirm resource consumption and decoded pixels;
screenshots establish desktop coexistence, not PNG presentation or Chromium
browsing. No frozen M1/ELF64 workload was run for this module.

Reproduction requires the existing qualified PNG build/cache and native Clang,
without downloading another Chromium checkout. From this checkout in the
selected WSL distribution, choose fresh output directories:

```sh
export GTOS_ROOT=/mnt/f/GTOS-Chromium
export CXX="$GTOS_ROOT/toolchains/ubuntu-noble-gcc13/bin/g++"
python3 tools/generate-native-png-resource.py --check
bash tests/native_resource_host_test.sh apps/png_resource_probe/resource.png "$GTOS_ROOT/artifacts/resource-host-reproduction"
make -j8 GTOS.iso CXX="$CXX" PYTHON=/usr/bin/python3
python3 tools/build-png-resource-probe.py "$GTOS_ROOT/artifacts/resource-consumer-reproduction" \
  --qualified-png "$GTOS_ROOT/artifacts/png-release-final-20261007T102400Z" \
  --dependency-cache "$GTOS_ROOT/cache/native-wuffs-dependencies" \
  --clang "$GTOS_ROOT/toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang" \
  --host-cc "$GTOS_ROOT/toolchains/ubuntu-noble-gcc13/bin/gcc" --host-cxx "$CXX"
/usr/bin/python3 tests/png_resource_qemu.py "$GTOS_ROOT/artifacts/resource-consumer-reproduction" \
  "$GTOS_ROOT/artifacts/resource-guest-reproduction" --kernel-sha256 "$(sha256sum GTOS.bin | cut -d' ' -f1)" \
  --host-cc "$GTOS_ROOT/toolchains/ubuntu-noble-gcc13/bin/gcc" --host-cxx "$CXX"
```

Source the workspace `scripts/env.sh` first for the already prepared dependencies
and explicit QEMU firmware path. The builder deliberately does not claim a full
stack or guest pass itself; the exact stack proof and guest results are separate
immutable evidence. CI reports are recorded after publication in the workspace
handoff rather than anticipated here.
