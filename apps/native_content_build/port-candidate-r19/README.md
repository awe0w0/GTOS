# The r19 GTOS port candidate

A build root for this checkout cannot describe a GTOS target without control files that the pinned checkouts either do not
carry or carry in the wrong form. This directory holds those files as **patches against the pinned origins**, one per file, plus
the composer that turns them into a view. Nothing here is a source fork: the eleven hundred-odd files of the two checkouts stay
where they are, read-only, and the view is a tree of links with eight real files in it.

`candidate.py --rebuild` produced the patches and `candidate.json` from the origins; `candidate.py --compose <dir>` reproduces
the view from the committed patches alone. Both run under the original guard with the checkouts read-only and one writable root.

## What the eight files do

Six of them are the state the r18 view already reached, and this round only made them reproducible: the r18 view carried `.gn`,
the root `BUILD.gn`, `v8/BUILD.gn`, `components/crash/core/common/BUILD.gn` and `content/test/BUILD.gn` as real files whose patches
existed only for four of them and whose manifest recorded `.gn` and the root `BUILD.gn` as *not* edited, so a fresh composition
silently used the shared port's versions and diverged. `candidate.json` now records all six with a patch, and the generator refuses
to finish unless each patch reproduces the r18 view byte for byte.

| file | what the candidate changes |
| --- | --- |
| `.gn` | `script_executable = "/usr/bin/python3.8"`. The port's own value points at `//third_party/cpython3/host/bin/python3`, a 9p path this host cannot execute, so every `exec_script` in GN failed with 127. |
| `BUILD.gn` | the bring-up gate: `gtos_frontier_bringup = false` and an assertion naming `//tools/gtos:platform_identity_probe`. |
| `v8/BUILD.gn` | the V8 backend refusal, with `gtos_v8_bringup` as the switch that measures the graph instead of refusing. |
| `components/crash/core/common/BUILD.gn` | the crash-key stub default for GTOS. |
| `content/test/BUILD.gn` | the fixture list defect: a `data` entry that cannot be a list element. |
| `third_party/libxml/BUILD.gn` | GTOS has no configure output. r18 wrote `assert(false, "…no configure output…")`, which states a conclusion; this round replaces it with a check on the two files libxml's own build reads, so the refusal is a missing file that a real cross configure satisfies. |

The last two are new, and they are the reason a real GTOS target can now be built at all:

| file | what the candidate changes |
| --- | --- |
| `build/toolchain/gtos/BUILD.gn` | `-nostdinc` for C and `-nostdinc++` for C++ in the shared command line. Without them every translation unit searched the host's `/usr/include` first and died on `bits/libc-header-start.h`; the target was freestanding in its linker and hosted in its preprocessor. |
| `build/config/gtos/BUILD.gn` | a `config("sdk")` that names the four header directories in the order the qualified V8 build used — the GTOS SDK's C++ config, libc++, the SDK's C headers, the clang resource directory — and the flags that build compiled with: `-msse2 -mfpmath=sse -mno-sse3 -mno-ssse3 -mno-sse4 -mno-avx -femulated-tls -fstack-usage`. The paths are `declare_args`, so a rebuilt SDK needs arguments, not an edit. |

`config("platform")` pulls `:sdk` in, so every GTOS compilation in the graph gets them.

## How to use it

```
python3 -B apps/native_content_build/port-candidate-r19/candidate.py --compose /var/tmp/<new>/view-01
gn gen <out> --root=/var/tmp/<new>/view-01 --root-target=//tools/gtos/png_image_codec:png_pixels_probe \
    --args='target_os="gtos" target_cpu="x86" gtos_frontier_bringup=true gtos_clang_bin="…" \
            gtos_sdk_dir="…/apps/native_v8_runtime" gtos_libcxx_include="…/third_party/libc++/src/include" \
            gtos_clang_include="…/lib/clang/24/include" gtos_png_source_dir="…" …'
ninja -C <out> png_pixels_probe
```

The composer refuses a destination that exists, a destination inside a shared source root, a pinned origin that moved since the
manifest was written, a missing patch, and a patch whose result is not the recorded byte sequence. A refusal removes the
half-composed view: a partial root looks usable and is not.

## What this does not claim

Composing a view and linking an image are not a port. The candidate makes the freestanding target describable and buildable; it
does not implement libc, the VM, threads, IPC, Ozone or networking, and the eight patches leave every one of those refusals in
place — which is why a plain `gn gen` still stops at the V8 backend refusal, and the Content graph stopped at libxml's missing
configure output after this round. The measured results of each attempt are in the round's report, not here.
