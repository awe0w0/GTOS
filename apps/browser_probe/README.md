# Browser platform ABI fixture

This small integer-only ELF32 application is the first application-side probe
for a future native Chromium port. It exercises actual GTOS syscall services;
it contains no browser API implementation. Building it does not prove guest
execution, process isolation, V8 compatibility, or webpage rendering.

## Build and inspect

```sh
./tools/build-browser-probe.sh /absolute/new/output
./tests/browser_probe_test.sh
python3 tools/browser_artifact.py /absolute/new/output/browser-probe.elf
```

GNU GCC/binutils with freestanding i386 support and Python 3 are sufficient.
No host libc or multilib C runtime is linked. The script records readelf output,
disassembly, metadata and SHA256. Do not execute the fixture as a Linux program.

## Provisional ABI contract

The ABI was supplied by the parallel kernel work on 2026-10-06 and was not yet
published in `dev` at implementation time. The build baseline is
`29f60022a381cbb11257f03fb3a61ef1f3273fac`; it does not run this application.
The kernel's published ELF loader and syscall documentation take precedence.
This directory does not define an independent ELF acceptance policy.

The fixture has little-endian ELF32 ET_EXEC/EM_386 metadata, entry
`0x40000000`, one read/execute text+rodata page, and a separate read/write
data+BSS page at `0x40002000`. GNU_STACK requests read/write, not execution.
There is no interpreter, dynamic linking, TLS, or relocation table. The guest
must provide a separate stack; `_start` aligns ESP to 16 bytes before cdecl
entry. Compiler options prohibit floating point, MMX and SSE instructions.

`int 0x80` uses EAX for the syscall number and result. EBX/ECX hold arguments;
the application expects other general registers to be preserved:

| EAX | Operation | Arguments |
| --- | --- | --- |
| 0x4700 | ABI version | Returns 1 |
| 0x4701 | Bounded write | EBX buffer, ECX length up to 256 |
| 0x4702 | PIT tick count | Returns ticks |
| 0x4703 | Yield | No arguments |
| 0x4704 | Exit | EBX exit code; must not return |

The probe checks ABI version, initialized data, zero-filled BSS, bounded write,
bad-address error -14, oversized-write error -7, unsupported-syscall error -38,
state retained over yields, and wrap-safe tick ordering. Tick ordering does not
prove time advanced. It exits 0 after emitting
`BROWSER PLATFORM PROBE PASS ABI1`; failures return 10/11/12/20. If exit returns,
`ud2` forces a guest fault instead of continuing into unrelated code.

## Guest acceptance still required

Integrate the compiled fixture through the real kernel ELF loader after its
contract is published. Require the PASS marker and exit 0, then verify the
desktop and an independent process remain alive. Test 32/64/128 MiB guest memory
and the supported CPU configurations. Preserve the loader rejection tests in
the kernel module rather than copying their policy here.

This baseline ABI has no mmap/protect/unmap, page reservations, native threads,
TLS, shared memory, IPC, file descriptors, sockets, DNS, entropy, TLS trust,
window surfaces, FP/SIMD state, or browser sandbox services. Those must be real
services and guest-tested before a V8/Chromium payload can replace this fixture.
