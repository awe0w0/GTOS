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
