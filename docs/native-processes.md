# Native process isolation, experimental i386 ABI v1

This module adds real BSP-scheduled CPL3 execution to the existing 32-bit kernel.
It does not implement a general browser platform, Linux ABI, POSIX process model,
dynamic executable service, FPU state ownership, or an internet-safe browser sandbox.
The trusted desktop and existing ring-0 tasks remain round-robin participants.
APs continue to use only the sealed kernel directory and their existing bounded
worker queues; no native task runs on an AP.

## Integration and ownership

`gtos::process::NativeRuntime` is a bounded runtime with four process slots. Its
GDT, scheduler, physical allocator, kernel paging object and runtime object must
outlive every process. Boot ordering is deliberate:

1. Construct the GDT, scheduler, interrupt manager and `SyscallHandler(…,0x80)`
2. Build the supervisor template with `KernelPaging::prepareIdentity`
3. Call `NativeRuntime::PrepareStacks(paging,frames)` **before enabling/sharing**
4. Enable paging; seal the supervisor template before AP startup or native launch
5. With IF clear on the hardware-verified BSP and the kernel CR3 active, call
   `Activate(tasks,gdt,paging,frames)`
6. From the boot/desktop context, call `Create(image,id)` and later inspect
   `Status(id,…)`; enable timer interrupts normally
7. Call `Reap()` from the boot/desktop loop after observing terminated processes

The kernel template and physical allocator identities are checked before stack
allocation and again at activation; a different allocator is rejected before
any frame mutation.

Failure of this optional initialization must leave the desktop running without
native tasks. Never bypass a memory-layout rejection by changing existing kernel
PDEs. `PrepareStacks` cannot be called after sealing; there is no unseal path.

Each of the four slots retains sixteen KiB of supervisor kernel stack backing at
aliases beginning in the `0xD0000000` arena. An absent four-KiB guard precedes each
stack and follows the last. The aliases are installed in the kernel template and
therefore visible, supervisor-only, in **every** process directory. The sixteen
backing frames and the template-owned alias page table are retained for the
kernel lifetime. Slots are cleared on reuse. They are not reported as reclaimed
process frames. Preparation failure rolls back allocated stack frames/aliases;
an empty page table remains owned by `KernelPaging` until pre-enable `abandon()`.
The guard detects overruns but this milestone has no separate emergency
kernel-stack/double-fault recovery stack; kernel-stack exhaustion is fatal.

Private directories borrow immutable supervisor mappings and own only their
user pages/tables. Native scheduling always updates TSS.ESP0 and CR3 with IF clear.
C++ may still be finishing on the outgoing stack after CR3 changes: this is safe
because all stack slots, ring-0 task stacks and the boot stack are mapped by the
shared supervisor template. **Never replace this with per-process-only kernel
stack mappings without implementing an assembly switch-stack transition.**

Exit/fault handling marks the task terminated and selects another saved frame;
it does not free memory. `Reap()` requires the boot task and kernel CR3, removes
scheduler references, then destroys inactive private mappings once. The guarded
kernel stack slot remains retained. `RequestExit(id,code)` provides bounded
kernel-side cancellation from the boot context. Slot IDs are monotonically
assigned and fail closed on wrap. A status record survives reap until that slot
is reused; it is not a permanent event history.

## Loader fixture contract

`NativeImage` is a kernel-provided raw isolation fixture:

- `code` / `codeBytes`: 1–4096 initialized bytes, at `0x40000000`
- `entryOffset`: strictly inside those initialized code bytes
- `data` / `dataBytes`: 0–4096 initialized bytes, at `0x40002000`
- New pages start zeroed; code becomes read-only before the directory is sealed
- The user stack is `0xBFFFD000..0xBFFFF000` (exclusive end), with absent neighbors
- Entry ESP is `UserStackTop-16`, 16-byte aligned, with no call/return frame or
  arguments; assembly `_start` should call any ordinary cdecl C/C++ entry
- Initial GPRs are zero. CS=0x23; SS/DS/ES/FS/GS=0x2B; EFLAGS=0x202

Creation validates and loads all memory transactionally before admitting a task.
No caller-supplied physical-frame mapping is accepted. This fixed payload format
is not an external-file or ELF loader. `ReadMemory` is a kernel-only checked
inspection helper, not a user syscall. User input does not choose task stacks,
CR3 values, selectors, ports, kernel pointers or privileged state.

The user arena is `0x40000000..0xC0000000`; any template PDE collision rejects
process initialization. The fixed layout consequently does not support all
large-RAM/MMIO configurations. Existing hardware support claims are unchanged.

## Validated external ELF32 creation

`CreateElf(const uint8_t* image,uint32_t bytes,uint32_t& id)` loads the narrow
static format accepted by `ValidateElf32` (`include/process/elf32.h`). The caller
supplies stable trusted kernel bytes, for example a retained GRUB module. The
runtime validates every source page as supervisor mapped and non-MMIO, rejects
null/wrapping/unmapped buffers, and caps the entire file at 2 MiB before parsing.
This is a native ELF loader, not dynamic linking or a general filesystem launcher.

The parser checks ELF32 little-endian System V, EM_386, ET_EXEC, bounded headers,
segments, ranges and alignment; rejects dynamic/interpreter/TLS/relocation and
writable-executable segments; and requires an entry in file-backed executable
code. The runtime additionally budgets the two stack pages within the 256-page
process limit and rejects segment pages intersecting either stack guard or the
stack itself. No parsed entry/segment can overwrite the supervisor template.

Each segment's page-rounded memory is freshly zeroed, initialized file bytes are
copied, and read-only final permissions are applied. BSS and page padding stay
zero. Only after the whole image is ready does the shared admission path add the
stack, seal mappings, install a safe trap frame and register a runnable task.
Failed validation allocates nothing; any later allocation/copy/protection/admission
failure destroys the inactive owned address space. Input bytes are copied
synchronously and no file pointer is retained. Program entry follows the same
assembly `_start`/16-byte-aligned ESP convention as raw fixtures.

Use `docs/elf32-loader.md` for the pure validator contract. Loading from a named
GRUB module or another external source still requires caller-side lifecycle and
source selection; this module does not reinterpret the bytecode app store.

The integrated default desktop now receives Catch as GRUB module 0,
`native-fault.elf` as module 1, `native-peer.elf` as module 2, and the separately
developed `browser-probe.elf` as module 3. The first two ELF files
are independently built from `apps/native`, with an assembly startup wrapper
that calls the C++ entry with correct cdecl alignment. The first program's
intentional kernel-write #PF is expected; its peer continues with different
private data at the same VA, then exits normally. `NATIVE RUNTIME PASS` requires
verified CPL3/CR3/preemption and the exact original free-frame baseline after
deferred reap. This does not install native applications through the app store.

## Trap and architectural state

The original kernel selectors stay at CS=0x10 and DS=0x18, preserving AP startup.
The GDT appends user code/data and one available 32-bit TSS descriptor at 0x30.
The TSS has SS0=0x18, updated ESP0, and I/O bitmap base=104 with descriptor limit
103. Since the bitmap lies outside the TSS limit, all user I/O ports are denied;
IOPL is always zero. The GDT limit covers descriptors only, not TSS storage.

Every IRQ, exception and int80 saves DS/ES/FS/GS as initialized 32-bit words and
loads the kernel data selector before C++. Entry clears DF. Return restores the
saved segments, including legal null user DS/ES/FS/GS. The common packed frame is:

| Offset | Contents |
| --- | --- |
| 0,4,8,12 | GS,FS,ES,DS |
| 16,20,24,28 | EAX,EBX,ECX,EDX |
| 32,36,40 | ESI,EDI,EBP |
| 44,48 | per-frame vector,error |
| 52,56,60 | EIP,CS,EFLAGS |
| 64,68 | user ESP,SS, **only for a privilege transition** |

Compile-time assertions and source tests check these offsets. Ring-0 frames end
at EFLAGS; the original kernel-task bootstrap uses the following two words as a
synthetic cdecl return address and task argument. No handler reads the absent
privilege tail of a ring-0 interrupt. Assembly aligns the C++ call stack.

Only PIT dispatch advances `Ticks()` or run-tick counters. Syscall yield/exit and
fault dispatch use `Reschedule()` without inventing clock time. The boot/desktop
slot always remains runnable. PIC EOI handling is restricted to hardware IRQs;
int80 is not acknowledged as a PIC interrupt.

User returns preserve arithmetic flags and DF, force reserved bit1 and IF on,
and clear IOPL, NT, VM, AC, TF and other privileged/reserved flags. Initial frames
are wholly kernel-constructed. User code cannot install arbitrary privileged
selectors. A bad user EIP/ESP or user-selected inaccessible data address faults
in CPL3 and is contained by the native fault path.

There is no FPU/SIMD context save or restore. Native dispatch sets CR0.TS|MP;
rejected x87/MMX/SSE instructions cause a user #NM (or #UD when the CPU/OS does
not enable the extension). The kernel baseline CR0 is restored for kernel-task
and boot dispatch. Kernel handlers and programs in this prototype must use
integer instructions only. The runtime does not clear TS in a #NM handler and
does not let one task observe another task's FP registers. FP/TLS/extended-state
support requires a separate tested ownership implementation.

## Syscall ABI

Before publishing the runtime, activation explicitly closes unsupported fast
entry paths on the BSP. CPUID MSR/SEP support gates clearing and read-back of
IA32_SYSENTER_CS/ESP/EIP; ambiguous early Intel family 6 SEP signatures (models <3, or model 3 before stepping 3)
are rejected without touching those MSRs. Extended CPUID SYSCALL support gates
clearing EFER.SCE while preserving all other EFER bits. A user SYSENTER then
faults rather than jumping to an inherited target; SYSCALL/SYSRET are disabled.
This does not rely on reset values or GRUB having cleared a previous target.


`int 0x80` is the only DPL3 software gate. EAX is the call number, EBX argument1,
ECX argument2, EAX the result. Other GPRs and segment selectors are preserved;
EFLAGS follows the policy above. Arguments/results are 32 bits. Errors are
negative signed 32-bit integers. This ABI is explicitly distinct from Linux.
The public constants are in `include/process/abi.h`.

| Call | Number | Arguments/result |
| --- | --- | --- |
| ABI version | 0x4700 | returns 1 |
| Bounded debug text | 0x4701 | EBX=user address, ECX=length; returns consumed bytes |
| PIT ticks | 0x4702 | returns scheduler tick count, wrapping at 32 bits |
| Yield | 0x4703 | immediately dispatches another context, eventually returns 0 |
| Exit | 0x4704 | EBX=exit code, never returns |

Debug output accepts at most 256 bytes. It validates **every** user page and
copies the whole range to a trusted bounce buffer before any console output.
A cross-page hole never prints a partial prefix. Kernel/MMIO addresses and
wraparound fail. Zero length still requires an address inside the user arena;
it does not require a mapped page. NUL bytes are consumed but suppressed by the
existing string console interface. Calls are bounded, nonblocking and execute
with interrupts masked. No user pointer is passed to unbounded `printf`.

Errors: `-38` unsupported call, `-14` invalid user buffer, `-7` excessive length,
`-22` no valid native calling context. The legacy kernel syscall4 self-test is
preserved only on a saved CPL0 frame. Calling4 from CPL3 returns -38 regardless
of the pointer, so raising the gate DPL does not expose the old raw EBX path.

## Fault containment and limits

Recovery requires both saved CS.RPL=3 and a current occupied native task with
its actual private CR3. Supported synchronous faults are #DE, #DB, #BP, #OF, #BR,
#UD, #NM, #TS, #NP, #SS, #GP, #PF, #MF, #AC and #XM. The runtime records ID,
vector, error, CR2 for #PF, CS/CR3/EFLAGS and an exit code with its high bit set,
then selects a surviving task. No user-memory pointers are retained in a fault
record. A #PF with a reserved-bit violation or without the user-access error bit is also
fatal, because it indicates broken paging/implicit descriptor access. Kernel
faults, NMI, double fault, machine check and reserved/unknown
exceptions remain fatal diagnostics. All 32 exception vectors have explicit
stubs; error-code vectors match the x86 architectural frame shapes.

Paging is legacy non-PAE and has **no NX**. Read-only code prevents writes but
user data and stack can still execute. There is no W^X/JIT security claim, no
resources/files/network/display handles, no shared memory, no native process
migration, no threads and no browser sandbox claim.

## Verification

`tests/native_process_test.sh` runs actual i386 source tests at O0 and O2 for
trap offsets, safe kernel bootstrap segments/flags and cdecl alignment,
timer-only accounting, yield/exit dispatch, sleep deadlines and deferred removal.
The same fault-classification helper used by the live handler is tested across
all 256 vector values, kernel/user origins and all low-five-bit #PF error patterns,
including reserved-bit and implicit supervisor-access failures.
The original `tests/cpu_test.sh` remains link-isolated from the native runtime
and passes unchanged at both optimization levels.

`tests/native_process_smoke.sh` assembles a separate raw user fixture and boots
through real GRUB at O0/O2, with 32 MiB/1 CPU, 64 MiB/4 CPU, 128 MiB/1 CPU, plus
64 MiB/4 CPU with CR4.OSFXSR enabled and with SYSCALL CPUID support enabled.
The latter uses QEMU's x86_64 emulator/`qemu64` CPU because its i386-only engine
masks SYSCALL capability; the guest remains the same i386 kernel. It proves:

- Kernel-observed CPL3 and distinct active private CR3s
- Identical VAs with different sentinels, intact after many switches
- Pure CPU-bound user loops, a ring-0 loop and the boot context all preempted
- Kernel-page write, CLI, HLT, port I/O, UD2, divide-by-zero, text write,
  user-stack guard access, x87 use and DPL0 software-int faults contained
- Valid cross-page writes; null/kernel/MMIO/wrapping/hole/oversize rejection
- Unknown and legacy syscall4 rejection without partial console output
- Null DS/ES/FS/GS, preserved registers and DF across yield; null DS/ES and DF
  across real timer interrupts
- x87/MMX/SSE/FXSAVE/FWAIT rejection, with SSE checked both before and after
  enabling OSFXSR; NT/TF trap flags, legal readable-code data selectors,
  kernel-SS/far-return attempts, user ESP=0 and port 0xffff boundary containment
- Seeded inherited SYSENTER CS/ESP/EIP and EFER.SCE are cleared before CPL3;
  user SYSENTER/SYSCALL/SYSRET are contained with their specified exceptions
- Repeated normal exit/fault/create/reap with exact private-frame restoration
- Four-slot exhaustion failing without extra allocations and final kernel progress
- A separately GNUas/ld-built ET_EXEC file supplied as a real GRUB module,
  executed twice in distinct CPL3 address spaces with initialized data and BSS
- Final ELF code permissions enforced by a real user text-write #PF
- Malformed/truncated ELF, invalid source pointers, stack/guard collisions and
  image-plus-stack page-budget exhaustion rejected without leaking resources
- Real guest physical allocator exhaustion, then every prefix of enough frames
  for ELF creation, with exact rollback at each failed stage and successful retry

The standalone harness's multiple-CPU settings do not start AP jobs; full kernel
integration must separately retain the established AP workload and interactive
GUI regressions. A log-only boot tick is not evidence of actual desktop input,
rendering, settings persistence or successful Chromium execution.

Set `GTOS_NATIVE_TEST_OUTPUT` to retain per-boot logs. Tools honor the existing
`GTOS_QEMU_SYSTEM_I386`, `GTOS_QEMU_SYSTEM_X86_64`, `GTOS_QEMU_DATA_DIR`, `GTOS_GRUB_MKRESCUE` and
`GTOS_QEMU_I386` overrides/rootless local runtime conventions.


### Cross-workspace application-side probe

The app-side fixture introduced in commit `48cd1939663f7befe3b3af2a01618b8757ad42fe`
is now built by the ISO recipe and loaded through `CreateElf`, not emulated in
kernel C++. It checks ABI1, initialized data/BSS, valid and rejected writes,
unknown calls, yields and retained state. The boot verifier separately requires
its actual CPL3/CR3, normal exit0, and a distinct directory from the fault/peer
programs. All three processes are deferred-reaped and the exact free-frame
baseline must return. This small probe is not Chromium or V8 execution.

The dedicated `GTOS-native-test.iso` replaces only the ordinary peer with a
five-second build, leaving the production ISO's shorter test unchanged.
`tests/native_desktop_qemu.py` sends keyboard/mouse input while userspace is live,
installs/launches/controls the game before peer exit, then requires native cleanup
and AP work completion. QEMU interrupt traces must show hardware keyboard and
mouse IRQs actually originating at CPL3. The full trace is retained compressed.
