# Legacy native FP staging verification, 2026-10-06

## Candidate and scope

The isolated working tree began at `0aef0a67a7870257392c40d85eb842adf208d753`
and incorporates the test-only native compatibility repair published as
`b36d0c22f1b5b3c3bc54aef82733ca584b8c568d`. This record describes the candidate and its reproduced integration gates; the
remote commit and CI are verified separately during publication. Default
activation remains Disabled.

The implemented core is BSP-only eager legacy x87/MMX/SSE ownership. All common
CPL3 trap boundaries save/neutralize, final assembly alone restores/publishes,
and fixed per-slot records are initialized before admission and scrubbed before
reclamation. It is not AVX/XSAVE, AP-native migration, physical-hardware
certification, a 64-bit browser platform, or Chromium execution.

## Strict architectural evidence

Official Debian Bochs `3.0+dfsg-1` passed all 22 strict real-GRUB cases, O0/O2:

- 32 MiB/1 CPU, 64 MiB/4 CPU and 128 MiB/1 CPU on the AVX-capable model,
  with OSXSAVE withheld and actual AVX #UD verified
- Intel/AMD SSE3 model runs and the SSE2-only floor
- Actual missing FXSR/SSE2/SSE3 rejection and inherited OSXSAVE rejection
- Dedicated fatal CPL0 #NM, whose intentional FP opcode is rejected by the
  ordinary executable-code audit

Positive guests verify every x87/MMX/XMM payload, control/status/TOP/tag state,
full x87 environment pointer/opcode history, canonical first-entry/reuse state,
real timer-only preemption, yields, non-switching syscalls, true keyboard/mouse
CPL3 IRQs, legal null selectors/DF, pending #MF, real #XM, invalid state operands,
reserved MXCSR, repeated exit/fault/cancellation/reap and exact frame baselines.
Production IRQ counters equal separately observed handler saved-CPL3 counts.

Exact guest ELF SHA256 values across the authoritative matrix:

- O0: `1c5524e7b12cd2603109b8aa3612ddc9abd2ada2e0f1eb6f8bb4045e68993269`
- O2: `9e7c7fe0631ab95e6be5d0e423c9851c76bfcc79ca89c2e7148c10641e0019a9`

After test-runner retry cleanup and the separate diagnostic audit option were
added, both strict 32 MiB cases were rebuilt/rerun with final tooling. Both
passed and reproduced these exact ELF hashes. The authoritative 22 guest logs
and their hashes were independently reviewed. A separate native-host AMD
instruction microprobe supported restore ordering and pending-state behavior,
but is not counted as GTOS/i386 physical-hardware acceptance.

## Desktop and concurrency evidence split

Strict Bochs UP/legacy-desktop acceptance passed with two live, differently
seeded FP ELF programs, Catch install/launch, screenshot-derived rightward
paddle movement, actual user-origin keyboard/mouse IRQs, original browser ABI1
fixture, isolated intentional #PF, survivor exit, exact three-process reap,
restart and close. There is no AP-work claim in that UP run.

The strict four-CPU Bochs desktop deliberately does **not** pass its active-AP
assertion. Stock firmware inherits CR0.CD/NW; the existing memory-type guard
correctly parks secondary processors. The classic RFB/display configuration
uses the 8-bit legacy UI fallback. This is preserved as a coverage limit.

A separately compiled QEMU 10.0.13 diagnostic passed at O0/O2. It qualifies the
specific emulator pointer omission through direct IF-clear kernel instruction
pairs before admission, restores canonical neutral state, and omits only
FIP/FCS/FDP/FDS comparisons in its separately built user binaries. It proves
modern 800×600×32 desktop/game operation, all three APs repeatedly completing
verified work while both FP users remain live, retained payload/control/opcode
checks, real user input IRQs, browser ABI1 and exact cleanup.

Observed rightward paddle positions and traced CPL3 IRQ counts:

- O0: x=423.5 → 463.5; keyboard=25, mouse=24
- O2: x=423.5 → 543.5; keyboard=29, mouse=27

Its output explicitly says diagnostic, pointer gaps remain, and not strict
ownership acceptance. Production audit rejects all 13 extra qualifier FP
instructions unless the separate test-kernel symbol allowance is supplied.
The normal production link allows only its original four transition helpers.

QEMU's strict pointer, unmasked SIMD #XM and reserved-MXCSR FXRSTOR tests remain
failing evidence of missing TCG semantics. The strict Bochs matrix closes those
state/fault semantics gaps. This is a precisely documented two-engine proof
split, not a claimed single-engine strict modern-desktop/SMP result.

## Read-only firmware diagnosis

A separate GRUB fixture invokes production `CaptureMemoryTypes` before paging,
with verified BSP identity, IF clear and capability-gated MSR reads. It has no
CR destination, WRMSR, cache flush, CLTS, LMSW or XSETBV instructions.

At both 1 and 4 configured CPUs:

- Stock Bochs BIOS: CR0=`60000011`, CR4=0; capture=false, valid=0
- Unmodified SeaBIOS: CR0=`00000011`, CR4=0; capture=true, valid=1
- Both: APIC_BASE=`FEE00900`; MTRRCAP=`0508`; DEF_TYPE=`0C06`;
  PAT=`0007040600070406`; 40 physical-address bits
- CPUID.1: EAX=`000206A7`, ECX=`179AE3BF`, EDX=`BFEBFBFF`

The stock case triggers the first production condition:
`!(cr0 & 1U) || (cr0 & 0xE0000000U)` through CD and NW, before later validation.
SeaBIOS passes that validation with eight variable MTRRs. This diagnoses a
firmware-state support boundary; it does not prove that clearing cache bits
alone is safe or that SeaBIOS establishes complete AP/desktop compatibility.
No cache policy or guard was changed.

Firmware-probe ELF SHA256:
`ee649540b9b8627cb4461e946b1a9090cecc6d9e56b7157a4e5346927078b60b`.

## Existing regressions and review

The complete deterministic suite passed with mandatory integer-only kernel
flags and linked executable-code audit at O0/O2, including the unchanged
link-isolated scheduler tests and new pure FP policy/state tests. Negative
audit tests reject FP/SIMD, misleading helper names, neighboring instructions,
AP bootstrap contamination and unused FP interfaces. Failed audited builds
cannot leave a timestamp-current executable target.

Existing disabled native, AP startup/work-pool and paging smoke coverage passed:
10 native + 7 AP-startup + 18 work-pool + 12 paging boots. The modern desktop
and concurrent native/desktop/AP suites also passed under Disabled, retaining
window/input/game/persistence and trace-proven CPL3 input assertions. The final
current-tooling deterministic suite and disabled native/GUI gates were repeated.

Independent final source review found no actionable isolation, lifecycle or
architecture defect and approved disabled-default staged integration. Physical
GTOS hardware and single-engine strict modern-desktop/active-AP concurrency
remain unproven. All evidence is local testing, not permission to broaden
support claims or enable FP by default.

## Reproduction and retained evidence

- `tests/native_fp_test.sh`
- `tests/kernel_integer_test.sh`
- `GTOS_FP_EMULATOR=bochs GTOS_BOCHS_ROOT=../gtos-bochs/root tests/native_fp_smoke.sh`
- `make GTOS-native-fp-test.iso`, then `tests/native_fp_desktop_bochs.py`
- `GTOS_FP_DESKTOP_OUTPUT=NEW_DIRECTORY tests/native_fp_desktop_diagnostic.sh`
- `tests/native_fp_firmware_probe.sh`
- `tests/run.sh`, existing `native_process_smoke.sh`, `desktop_qemu.py`,
  `native_desktop_qemu.py`, AP and paging smoke scripts

The staging checkout retains per-run logs, actual CPUID, compiler flags/tool
versions, source/ELF/ISO hashes, maps/disassembly, complete configurations,
compressed interrupt traces, screenshots and official-package provenance under
`obj/fp-final-evidence/`. Generated evidence is ignored by git; this document
records its scope and reproducible gates rather than publishing large binaries.

## Integrated candidate reproduction

After applying the frozen 60-file patch to the normal b36 checkout, every patch
file was verified byte-identical to staging. The full deterministic suite,
all ten default-profile native guest cases, strict Bochs O0/O2 32 MiB cases,
concurrent native/desktop/AP input, Chinese preference/IME/reboot flows and
legacy VGA/game/storage acceptance were rerun successfully. The two strict ELF
hashes match the authoritative matrix above. The firmware-probe shell script
was made executable; its source bytes are unchanged.

The unchanged supplementary diagnostic also passed on the official Ubuntu
QEMU `1:8.2.2+ds-0ubuntu1` package at O0/O2. Both users remained live during Catch
interaction and verified repeated AP jobs; hardware CPL3 IRQ counts were 26/30
(keyboard/mouse) at O0 and 21/21 at O2. The ordinary workflow now includes this
qualified diagnostic. Strict Bochs semantics remain a separate recorded gate;
ordinary CI does not claim to reproduce them using QEMU.

Final integration review also identified and repaired two QMP harness failure
paths: blocking line reads that could bypass the deadline, and an uninitialized
input counter after early guest exit. Ten deterministic pipe/fake-emulator tests
now cover stalled/partial greeting, EOF, malformed/oversized input, buffered
events, early successful exit and stale-log rejection. These change no guest
acceptance assertions or production code; the final aggregate suite includes
them. The strict QEMU runner now fails within its global read deadline.
