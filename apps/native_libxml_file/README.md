# The GTOS file layer libxml2 needs, and what it took to link against it

r20 ended with the honest note that `xmlIO.c` did not compile: `'fcntl.h' file not found`. That is not a missing include path. libxml2's I/O
layer is written against POSIX file calls, and GTOS has no `fcntl.h`, no `unistd.h` and no `sys/stat.h` at all. This directory is the
answer: a private SDK subset that declares exactly the calls GTOS implements, and implementations that are thin translations onto the
native file ABI (`include/process/file_abi.h`), the same service LLVM libc's `platform_file.cpp` already drives for the FILE layer.

## What is here

| path | what it is |
| --- | --- |
| `include/fcntl.h` | `open` and the `O_*` flags callers pass (i386 Linux values, translated explicitly) |
| `include/unistd.h` | `read`, `write`, `lseek`, `close`, `dup` and the seek/std constants |
| `include/sys/stat.h` | `struct stat` with the two fields the ABI can fill: `st_mode` (type) and `st_size` (bytes) |
| `posix_file.cpp` | those seven calls over `GTOS_SYS_FILE_*`, with the errno mapping the ABI documents |
| `link_closure/libc_closure.cpp` | the libc symbols a libxml2 link still lacked after the qualified runtimes, each with a documented limit |
| `link_closure/math_closure.cpp` | the libm subset `xpath.c` needs, with a host unit test that measures its error against the host libm |
| `probe/main.cpp` | a libxml2 consumer: `stat`, `open`, `read`, `dup`, `lseek`, `write`, `xmlReadMemory`, `xmlReadFile` |
| `probe/file_probe.cpp` | a consumer of the seven calls alone, so the subset can be linked without libxml2 |
| `probe/linker.ld` | a user-image script for a library too large for the FILE probe's one-page-globals layout |

Nothing here is a Linux runtime. The third argument of `open` is accepted and ignored because GTOS has no permission bits; there is no
`fstat`, `isatty`, `O_NONBLOCK` or fd inheritance beyond the ABI's owner-scoped handles; `time()` cannot be epoch time because the clock
ABI only offers coarse monotonic uptime and says so. Each absence is visible as a link error or a documented limit, not as a declaration
that quietly resolves to nothing.

## Measured, not asserted (2026-10-31, work root `/var/tmp/gtos-dsh-154-mainline/r21/libxml-link-09`)

* All **26** sources the pinned BUILD.gn compiles compile against the generated GTOS configuration with this subset on the include path.
* The parsing set — the 26 objects, this subset, the closure, the qualified native FILE library (41 objects) and the native printf objects —
  **links with zero undefined symbols**, producing `gtos-libxml-parse.elf` (763,880 bytes). The image exports `xmlReadFile`,
  `xmlReadMemory`, `xmlParseFile` and `xmlFileOpen`, and `xmlIO.c`'s file path is inside it.
* The file subset alone links closed as well: `gtos-file-subset.elf`, 0 undefined symbols, the seven calls resolved from `posix_file.cpp`.
* The libc closure is measured, not guessed: combining the objects with `ld -r` and subtracting what the qualified FILE and printf runtimes
  define left `strcat`, `strncpy`, `toupper`, `bsearch`, `time`, `fprintf`, `vfprintf`, `stdin`, `stdout`, `stderr` — supplied by
  `libc_closure.cpp` — and, through `xpath.c`, `fabs`, `floor`, `ceil`, `fmod`, `log10`, `pow`, `exit` — supplied by `math_closure.cpp`.
* `math_closure_test.c` compares those functions with the host libm over 62,207 values: maximum relative error 6.054e-14. The test states
  what it covers (sampled ranges, including the half-integer rounding boundary and powers of ten across 600 decades) and what it does not
  (a bound over all doubles).

## Not claimed

No GTOS guest has executed either image: no ISO was built, no QEMU was started, so nothing here is a boot, a run or a guest verdict. The
read/write cases in `probe/file_probe.cpp` (offset movement, dup offset sharing, write-then-read equality, `-1`+`errno` on a missing file,
`S_ISREG`) are written and linked but not executed; executing them is the next unit's measurement. The link says the symbols resolve and the
sections place, which is exactly what a link can say.

## Native directory adapter (2026-11-07)

The measurements and absence statements above describe the r21 link snapshot.
Later r22 evidence covers selected real CPL3 FILE/libxml/printf/time consumers;
the current time closure uses the validated realtime ABI, not uptime as epoch.
Neither that evidence nor the old seven-call tests qualifies the new directory
adapter described here.

`directory.cpp` and `include/dirent.h` project the existing native DIR_OPEN,
DIR_READ and DIR_REWIND operations into `opendir`, `readdir`, `rewinddir`,
`closedir` and `dirfd`. The opaque stream owns one native handle and one entry
buffer. EOF preserves errno and the prior entry; malformed replies fail with
EIO before replacing it. Failed opens release allocation, and a close releases
the buffer even when the native close reports an error, since that ABI already
invalidates the handle. Names remain bytes with a 255-byte maximum; inode
identity is unavailable and reported as zero. There is no seekdir/telldir.

The existing calls and directory adapter share the same request/error/path
helpers. `GTOS_POSIX_FILE_TEST` replaces only their kernel-call boundary for
unit tests. Production builds retain the actual IA32 interrupt instruction.

`codex-c-directory-build-d03` / `codex-c-directory-run-r03` compile and run the
actual adapter at O0/O2: 124 checks per image, with a deliberately wrong
expectation rejected by the third image. Cases include allocation failure,
maximum paths/names, UTF-8 bytes, EOF/error preservation, malformed responses,
close-error cleanup, independent simultaneous streams, resource reuse and
existing file-call refusals. Four separate production ELF32 objects compile
without the injected test endpoint. Source identities and the three frozen
images are recorded in the build manifest; the run has its own writable root.
These are adapter units with injected kernel/allocation outcomes and Linux
reporting, not a GTOS guest test or complete POSIX qualification. The new
adapter still needs runtime linking and a real CPL3/ATA consumer.

The handle subset now also implements `fstat`, `fsync` and `ftruncate` using
HANDLE_INFO, SYNC and TRUNCATE in the existing native ABI. Both `stat` and
`fstat` validate response status/version/type before touching caller storage.
Only native file type and unsigned 32-bit size are projected; ownership,
timestamps and permission enforcement remain unavailable. Truncate rejects
negative sizes and values beyond the native 32-bit length field before I/O.

`codex-c-directory-build-d04` / `run-r04` execute 245 checks at each O0/O2
profile, retaining the directory/resource tests and adding exact wire requests,
zero/maximum sizes, caller-error zero-call checks, native/unknown error mapping,
malformed replies and unchanged failure outputs. The altered-expectation image
fails with 246 checks. Default int80 production objects compile separately.
This is still an injected-endpoint adapter unit, not new guest I/O evidence.

## Typed paths and owner-scoped handles (2026-11-07)

The private subset now implements `mkdir`, existence-only `access`, `unlink`
and `rmdir`. Deletion uses additive native operation `0x4760` with an exact
16-byte typed request. The existing untyped REMOVE operation is unchanged.
`NativeFiles` snapshots and validates the request, then checks type and removes
under its one BSP interrupt guard. This avoids a userspace stat/remove race.
There is no SMP filesystem concurrency claim. Wrong-kind requests preserve the
entry and return EISDIR/ENOTDIR; the underlying store still rejects nonempty
directories and propagates actual storage errors.

Native handles already belong to one process and cannot be inherited by a
new ELF process. `O_CLOEXEC` accepts that enforced property; `F_GETFD` validates
the handle, and `F_SETFD` accepts only CLOEXEC. `F_DUPFD_CLOEXEC` supports the
native `dup` operation with minimum zero; other minima and inheritable/nonblock
requests are refused. This is a bounded native-handle projection, not a POSIX
descriptor allocation or fork/exec implementation. Creation permissions remain
accepted but unenforced, and access checks only existence.

`codex-c-directory-build-d05` / `run-r05` run 399 checks at O0/O2 against the
actual adapters with an injected syscall boundary, plus a 400-check wrong
expectation that fails. Four default int80 objects compile separately.
`codex-c-posix-path-guest-build-p02` / `run-r01` then build the current native
runtime, NativeFiles, complete LittleFS and ATA driver with real CPL3 consumers.
Each O0/32 MiB and O2/64 MiB run repeats 54 checks across two process lifetimes:
create/write/sync/stat/truncate/dup-shared-offset/close, typed deletion, ten raw
validation refusals and legacy REMOVE. Each process makes 53 file endpoint
calls, leaves zero handles, and restores its frame baseline (`0x1d79`/`0x3d71`).
Separate unavailable runs distinguish an attached unmounted endpoint (five
service calls) from a detached endpoint (zero), although both return ENODEV.
The wrong consumer is rejected with QEMU 65; four valid configurations return
33. Scratch-disk prefix/suffix guards remain unchanged. This is new component
guest evidence, not a new legacy qualification lock or Content acceptance.

`rename` now snapshots two bounded paths into the existing native RENAME
request and propagates its real result; no replacement filesystem is added.
`directory-build-d06` / `run-r06` run 426 actual adapter checks per O0/O2,
plus a rejected 427-check calibration. `posix-path-guest-build-p03` / `run-r02`
add a real ATA rename, prove the old path is absent and the new path retains
the truncated bytes/size, then remove it. The two mounted profiles each repeat
57 checks with 56 endpoint calls, zero handles and exact Reap restoration.
The attached-unmounted, detached and wrong-consumer configurations also retain
their separate expected outcomes.
