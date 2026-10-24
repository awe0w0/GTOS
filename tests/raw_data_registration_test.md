# GTOS-DSH-090 — raw data registration cell unit tests

**These are real unit tests of the shipped implementation.** `src/drivers/raw_data_registration.cpp` is compiled
into the test executable and every assertion below drives its actual state machine. Nothing re-implements the
cell, and nothing here is a model, a fixture, a text scan or a Python reimplementation.

## What the executable is

| Property | Value |
|---|---|
| target | `i386`, `ELF32`, `ET_EXEC`, statically linked |
| entry | `_start`, from `-Wl,-e,_start` |
| libraries | none: `-nostdlib -static -fno-pie -no-pie`, no CRT, no libc, no heap |
| syscalls | two: `write(1,…)` and `exit(code)`, both `int $0x80` |
| compiled TUs | `src/drivers/raw_data_registration.cpp`, `tests/raw_data_registration_test.cpp`, `tests/raw_data_registration_start.cpp` |
| output | `/var/tmp/gtos-dsh-090-4092d03-cell-tests/unit-bin/initial/raw-data-registration-tests.elf` |

The build defines exactly two macros, and both are test-only:

- `GTOS_MEMORY_TEST` — the pre-existing switch that compiles `InterruptGuard` down to nothing, so no `CLI` or
  `STI` executes. Without it this binary would fault on the first guarded call in a user process.
- `GTOS_RAW_DATA_REGISTRATION_TEST` — grants `RawDataRegistrationCellTestAccess` friendship so a test can create
  an independent cell, inject a boundary value, and observe `state`/`generation`/`active`.

The production profile defines neither. `default_object_compile` compiles the same `raw_data_registration.cpp`
with the frozen 29-token kernel profile and no test macro, and that object is what proves the module still
builds as kernel code.

## How to reproduce

```sh
# from the selected test view, with the two test macros
/usr/bin/x86_64-linux-gnu-g++-9 -O2 -m32 -std=c++11 -Iinclude -ffreestanding \
  -fno-use-cxa-atexit -nostdlib -static -fno-pie -no-pie -fno-builtin -fno-rtti \
  -fno-exceptions -fno-stack-protector -fno-asynchronous-unwind-tables -fcheck-new \
  -Wall -Wextra -Werror -DGTOS_MEMORY_TEST -DGTOS_RAW_DATA_REGISTRATION_TEST \
  -mgeneral-regs-only -mno-sse -mno-mmx -msoft-float -fno-tree-vectorize \
  -Wl,-e,_start \
  src/drivers/raw_data_registration.cpp \
  tests/raw_data_registration_test.cpp \
  tests/raw_data_registration_start.cpp \
  -o raw-data-registration-tests.elf

# the same source, as kernel code, with no test macro
/usr/bin/x86_64-linux-gnu-g++-9 -O2 -m32 -std=c++11 -Iinclude -ffreestanding \
  -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions \
  -fno-leading-underscore -fno-stack-protector -fno-pie \
  -fno-asynchronous-unwind-tables -fcheck-new -Wno-write-strings -MMD -MP \
  -Iinclude/storage/littlefs-sdk -mgeneral-regs-only -mno-sse -mno-mmx -msoft-float \
  -fno-tree-vectorize -c src/drivers/raw_data_registration.cpp -o raw_data_registration.o
```

Before the executable is allowed to run, its bytes are inspected: `ET_EXEC`, `EM_386`, `ELF32`/LSB, an entry
point inside an executable `PT_LOAD`, no `PT_INTERP`, no dynamic dependencies, no executable stack. Its bytes and
SHA-256 are pinned, and the run stage refuses if they changed. The suite is then executed **three times in three
fresh processes** and all three must exit 0 with an identical result.

## The cases

| ID | What it establishes |
|---|---|
| `C01` | a fresh cell binds nothing: `CELL_FREE`, null target, generation 0, active 0, and both `Acquire` and `TryDrain` refuse |
| `C02` | every null argument is refused, and a refusal never disturbs an output that already holds a valid record |
| `C03` | a bind issues a key that reports `Issued()`, names this cell and this target, and starts at generation 1; an idle retire drains at once |
| `C04` | an idle rebind is allowed, advances the generation, drains the old key, and the old key can no longer retire the new binding |
| `C05` | a rebind with a use outstanding is refused, and the caller's key and the cell are untouched |
| `C06` | with two uses outstanding, retirement refuses new `Acquire`s; the first release does **not** drain; the last release does |
| `C07` | the lease destructor is the release path, including the drain-on-last-use case |
| `C08` | releasing the same lease twice is refused and does not corrupt the use count |
| `C09` | a lease issued by another cell is refused, and the valid lease is not consumed |
| `C10` | a key from another cell, and keys with a wrong target, a zero generation or a null target, are all refused |
| `C11` | a drained key cannot retire a newer binding, and the newer binding stays usable |
| `C12` | `Acquire` into an already-held lease is refused without disturbing the existing use |
| `C13` | the use count saturates at `UINT32_MAX`, `Acquire` fails closed, and the count does not change |
| `C14` | the generation boundary: binding stops at the reserved `0xFFFFFFFF`, retirement there makes the cell permanently `CELL_EXHAUSTED`, and exhaustion never wraps back |
| `C15` | a repeated retire while retiring is idempotent, and an idle retirement drains immediately |
| `C16` | **8192 real lifecycles**: bind → acquire → release → retire → drain, checking state, target and counters every cycle, with the generation advancing exactly twice per cycle |
| `C17` | the shared boot cell is stable across calls and was never disturbed by the other tests |

Each case prints one `CASE <id> ok` line. A failure prints `FAIL <id>: <what>` and the process exits non-zero.
The suite ends with a `SUMMARY cases=… assertions=… failures=… cycles=…` line and a `RESULT PASS|FAIL` line.

## What these tests do **not** prove

- **Not interrupt safety.** `GTOS_MEMORY_TEST` removes the `CLI`/`STI` in `InterruptGuard`, so nothing is ever
  masked here. The guard is compiled out, not exercised.
- **Not SMP safety.** The cell is driven from one thread in one process.
- **Not a handler's lifetime.** The `RawDataHandler*` values are distinct addresses of static storage that are
  deliberately never dereferenced. The cell only compares and stores them, so they are faithful *identities* —
  and nothing about constructing, calling or destroying a real handler is tested.
- **Not the provider, the cell's owner wiring, or the target kernel.** The cell is not registered anywhere.
- **Not the target ABI or delivery path.** No IRQ, no AP, no hardware, no network.

`host_tests_run=true` is a fact about this host. It is not `target_kernel_execution`.

## Why `-Werror`

The suite is compiled with `-Wall -Wextra -Werror`. A test that builds with a warning is a test whose author has
not read the code path they are asserting on; refusing the build keeps the assertions honest.

## Platform limitation

This is an `i386` executable and needs a host that can run 32-bit x86 binaries. If the host cannot, the run stage
reports the failure and the commit is marked `NOT_RUN`. No emulator, no multilib install and no substitute test
is used to turn that into a pass.
