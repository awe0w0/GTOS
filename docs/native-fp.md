# Native legacy FP ownership (staged, disabled by default)

This bounded i386 module owns x87/MMX and XMM0–XMM7/MXCSR for four native
process slots on the hardware-verified BSP. It is an optional runtime
prerequisite, not Chromium, libc/libm, x86-64, AVX/XSAVE, kernel SIMD, native
threads, CPU migration, or a browser sandbox. The eventual browser architecture
remains a separate x86-64 decision. AP workers and all kernel C/C++ remain
integer-only.

`NativeRuntime::Activate(tasks,gdt,paging,frames,policy)` accepts fixed-width
`NativeFpDisabled` (the compatibility default), `NativeFpSse2`, or
`NativeFpSse3`. Ordinary desktop boot still chooses Disabled. The dedicated
`GTOS-native-fp-test.iso` explicitly requests SSE2 with `native-fp-test` and
loads two separately built FP peers while desktop/input/AP work proceeds.
This opt-in is an acceptance fixture, not a default support claim.

## Capability and control ownership

Enabled activation requires CPUID leaf1 FPU, MMX, FXSR, SSE, SSE2; the SSE3
profile additionally requires SSE3. These are minimum profiles, not instruction
whitelists. Inherited CR4.OSXSAVE rejects activation before FP instructions or
FP control mutations. A raw CPU AVX feature bit is not OS extended-state support.
No XSETBV, XSAVE, YMM, AVX or extended-state enablement is performed.

Only CR0.MP/EM/TS/NE and CR4.OSFXSR/OSXMMEXCPT are changed. Unrelated bits
are preserved and read back. Enabled dispatch never restores the stale
activation-time whole CR0. TS stays set during ordinary kernel execution;
it is cleared only in bounded transitions and while CPL3 executes. #NM is
not lazy restore: an enabled user #NM is a fatal ownership invariant failure.
On the enabled BSP, a kernel FP instruction faults fatally. AP control
registers are unchanged; AP integer-only execution is enforced by the compiler
and linked-code audit, not a new AP FP-ownership/runtime-guard mechanism.

Disabled activation bypasses FP feature checks, all FP instructions and the
new FP CR0/CR4 changes. Existing integer-only dispatch and #NM/#UD denial
expectations remain intact. No syscall number or native ABI1 convention changes.

## State, residency and lifetime

Each slot has a 16-byte-aligned 544-byte payload: a 512-byte FXSAVE image, a
28-byte protected-mode x87 environment and four alignment bytes. Generation
and initialized metadata are separate. The record stride is 560 bytes, with
compile-time size/offset/alignment assertions. Four fixed records and both
module scratch/neutral images are checked once for resident writable supervisor
mappings in the sealed shared kernel template. The active runtime, its module,
GDT, allocator and template remain alive for the kernel lifetime; there is no
deactivation or destruction API. There is no allocation in a trap
and no user-provided restore pointer or raw-state setter.

Initial images are integer-zeroed, then FCW=0x037F, empty tags, TOP/status=0,
MXCSR=0x1F80 and zero pointers/opcode are set. All x87/MMX payload bytes and all
XMM payload bytes are zero, including empty physical registers. The processor's
MXCSR mask is obtained from a separate zeroed probe and falls back to 0xFFBF
when reported as zero. A clean FNINIT/FNSTENV supplies reserved full-environment
encodings only; inherited hardware payloads never become a new process image.
Activation restores/captures the canonical state and compares its defined fields
before admitting a process, then scrubs its scratch buffer.

Admission copies the canonical image before `AddTask`. Failed admission scrubs
an initialized record. Exit and fault handling first pass through the entry
boundary, leaving no hardware owner. Reap checks ownership, invalidates the
record with non-elidable stores, then frees the inactive private address space.
IDs are monotonic generations and refuse wrap. No stale generation can become
the hardware owner of a reused slot.

## Every trap boundary

The common assembly stub saves GPRs/selectors, loads kernel selectors and clears
DF, then invokes the integer-only entry preparation before ordinary C++ handling.
This applies to no-switch ABI/ticks/write calls and non-scheduling input IRQs,
not just scheduler changes. CPL3 entry verifies current slot, live generation,
actual private CR3, retained supervisor frame range and controls before touching
state. It publishes SavingUser, executes FXSAVE then FNSTENV, and copies full
environment pointer/opcode fields into the FX image. It then restores the clean
neutral environment/image, sets TS and publishes KernelNeutral with no owner.
No waiting FWAIT/FINIT/FSTENV appears in these helpers.

After dispatch selects a frame/CR3, return preparation validates the selected
slot/frame/generation and trusted image, including the hardware MXCSR mask and
coherent environment fields. It publishes RestoringUser. The final assembly
helper clears TS, performs FNINIT → FLDENV → FXRSTOR, then publishes UserLive
using only integer stores. Only the integer selector/register epilogue and IRET
follow. Pending unmasked user x87 state is therefore delivered on user waiting
execution, never by a kernel waiting instruction. User #MF/#XM uses the existing
verified CPL3 fault path; survivors remain runnable. A fault in a transition,
owner mismatch, CPL0 exception or invalid trusted image is fatal.

The full environment supplements AMD's conditional diagnostic pointer handling
in FXSAVE/FXRSTOR. FLDENV runs after clean FNINIT so it cannot encounter a prior
pending exception; FXRSTOR does not itself deliver the newly restored pending
x87 exception. A nested transition fault is not recoverable. TS is a misuse
guard, not a claim of general speculative/extended-state isolation.

## Integer-only build enforcement

All production kernel C++ and standalone kernel builds consume
`tools/kernel-cxxflags`: `-mgeneral-regs-only -mno-sse -mno-mmx -msoft-float
-fno-tree-vectorize`. User probe flags are separate. The linked audit uses
executable input-section ranges and exact sized helper symbols, not arbitrary
bytes merged from rodata into .text. Only four production helper symbols may
contain state instructions: native_fp_probe_asm, native_fp_save_asm,
native_fp_neutral_asm and native_fp_restore_asm. AP trampoline code is decoded
in its explicit 16/32-bit modes. The test gate injects forbidden state/register
instructions and checks rejection, including neighboring/lookalike symbols.

## Verification and emulator-specific limits

`tests/native_fp_test.sh` tests pure image layout, capabilities, masks,
control-bit preservation, canonical construction, pointer stitching and
ownership generations at O0/O2. Existing scheduler source tests remain
link-isolated from privileged FP assembly. `tests/kernel_integer_test.sh`
checks full linked O0/O2 builds and negative instruction-audit cases.

`tests/native_fp_smoke.sh` is the strict real-GRUB enabled ownership matrix.
It retains independent FNSTENV diagnostic-pointer comparisons alongside
FXSAVE register comparisons, because FXSAVE alone cannot prove pointer
fidelity. It checks fresh/reused state, independently seeded peers, real timer
preemption, non-switching syscalls, actual keyboard/mouse IRQ origin, yield,
null selectors/DF, MMX→EMMS→x87, pending #MF, SIMD #XM, bad user state operands,
repeated reap/cancellation and exact allocator baselines, feature rejection,
AVX #UD, unrelated control preservation, and a separate expected kernel panic.
The dedicated desktop script is `tests/native_fp_desktop_qemu.py`.

Strict architectural acceptance passes on official Debian Bochs 3.0+dfsg-1:
22 real-GRUB boots cover both O0/O2, 32/64/128 MiB, one/four CPU topologies,
Intel/AMD models, baseline SSE2 without SSE3, missing feature rejection,
inherited OSXSAVE rejection and the separate fatal kernel-#NM fixture. Each
positive guest preserves all defined state including diagnostic pointers,
delivers real #MF/#XM, rejects reserved MXCSR, exercises real CPL3 input IRQs,
and reclaims the exact private-frame baseline. No waiver is used. The official
package provenance is verified through Debian's signed Release, package index
and individual archive hashes; packages are unpacked task-locally.

The same strict tests intentionally **fail on the bundled QEMU 10.0.13 TCG
implementation**. Both Intel- and AMD-model boots
lose FNSTENV FIP/FCS/FDP/FDS after restore. Upstream QEMU v10's `do_fldenv`
does not load those fields, `do_xrstor_fpu` also omits them, and `do_xsave_fpu`
explicitly writes zero diagnostic pointers. Thus changing correct kernel
ordering cannot make that emulator implement the missing architectural state.
The same bundled emulator also returns from DIVPS(0/0) with invalid unmasked
(MXCSR changes 0x1F00→0x1F01) instead of delivering #XM, and accepts reserved
MXCSR bit31 through FXRSTOR instead of #GP. Those strict cases also remain
required; optional diagnostic-only waivers are separately named and only accept
the exact independently observed emulator omissions.
The strict failing evidence must remain visible; a narrower/emulator-limited
run cannot be called full pointer-fidelity acceptance. Physical GTOS hardware
coverage has not been established.

Architecture references:
- Intel SDM instruction definitions and system programming: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- AMD APM volume5: https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/26569.pdf
- QEMU v10 FP implementation: https://github.com/qemu/qemu/blob/v10.0.0/target/i386/tcg/fpu_helper.c

The strict Bochs desktop run also observes two simultaneous FP ELF users,
Catch installation/launch/input, kernel-verified CPL3 keyboard/mouse IRQs and
exact final native reap. Its separate AP-worker assertion does not pass: stock
Bochs firmware leaves CR0.CD/NW set (observed CR0=0xE0010011 after paging), so
the existing memory-type guard correctly refuses worker sharing and parks the
three APs. The classic RFB/display configuration yields the 8-bit legacy UI
fallback. An unmodified SeaBIOS alternative boots but exposes only one CPU in
its firmware tables in this setup. These are separately recorded firmware/UI
limitations; no cache-policy guard is weakened to pass an FP test.

A separately built `GTOS-native-fp-diagnostic.iso` supplies supplementary QEMU
modern-desktop/active-AP coverage. Before admitting any native process it runs
a diagnostic-only, IF-clear kernel instruction probe. Independent synthetic
FLDENV/FNSTENV and FXRSTOR/FNSTENV pairs must exhibit exactly the known zero
FIP/FCS/FDP/FDS omission while nondefault control/TOP and XMM payload fields
round-trip. It restores the activation-established canonical image and original
controls before returning. There is no intervening call, syscall or interrupt
window, so a trap-boundary bug cannot qualify the emulator gap. The helper is
linked only into the separate diagnostic ELF and needs an explicitly named
test-kernel audit allowance; the ordinary production audit rejects it.

Only after qualification do the diagnostic user binaries omit those pointer
comparisons. Their x87/XMM payloads, control state and opcode checks remain,
as do actual modern 800×600×32 rendering, live-users game movement, hardware
input IRQs, repeated AP work, browser ABI1 regression and exact native reap.
Successful output is labeled `DIAGNOSTIC PASS (POINTER GAPS REMAIN)` and never
strict FP ownership acceptance. This evidence complements strict Bochs state
semantics and UP/legacy desktop proof; it is not a single-engine strict
modern-desktop/SMP acceptance result. Default native activation remains Disabled.
Independent source and integration reviews plus frozen-candidate regression
gates accompany publication. Emulator vendor strings are not physical AMD
errata proof.

## Read-only firmware diagnosis

`tests/native_fp_firmware_probe.sh` boots a separate pre-paging fixture using
production `CaptureMemoryTypes`. It never writes CR/MSR/cache controls. With
stock Bochs BIOS, the verified BSP has CR0=0x60000011, CR4=0, IF=0 and
IA32_APIC_BASE=0xFEE00900. The first production gate rejects CD=NW=1 before
reading memory-type configuration. Capability-qualified reads show CPUID.1
EAX=0x206A7, ECX=0x179AE3BF, EDX=0xBFEBFBFF; MTRRCAP=0x0508,
MTRR_DEF_TYPE=0x0C06, PAT=0x0007040600070406 and 40 physical address bits.
With the same emulator, CPU and guest fixture but unmodified SeaBIOS, CR0 is
0x00000011 and production capture succeeds with valid=1 and eight variable
MTRRs. This identifies an inherited firmware-state/support boundary, not an FP
failure. Stock-firmware later checks are not reached; no claim is made that
clearing CD/NW alone would establish safe AP operation. Supporting such firmware
would require a separate reviewed cache-initialization design and proof.

## Continuous integration boundary

The ordinary workflow runs pure FP/ownership tests, mandatory linked-code audits,
all default-profile native/desktop/language regressions, and the explicitly
qualified QEMU desktop/AP diagnostic at O0/O2. The unchanged diagnostic also
passed locally under the official Ubuntu QEMU 8.2.2 package used to assess the
CI environment, as well as bundled QEMU 10.0.13. A qualifier mismatch fails the
step; it is not silently skipped or broadened.

The full strict Bochs architectural matrix is separately reproduced and recorded
in [the verification report](native-fp-verification.md). A green ordinary CI run
must not be presented as a strict QEMU pointer/#XM/invalid-MXCSR result or physical
hardware proof.
