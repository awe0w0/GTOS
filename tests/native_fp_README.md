# Native legacy-FP guest proof

`native_fp_smoke.sh` builds separate integer-only O0/O2 kernels, puts them in
real GRUB ISOs, and runs actual i386 CPL3 users. It never changes the normal
GTOS desktop's default Disabled policy. The complete strict matrix is:

```sh
GTOS_FP_TEST_OUTPUT=/tmp/native-fp-strict tests/native_fp_smoke.sh
```

QEMU/GRUB discovery follows the existing sibling `gtos-runtime` convention.
`GTOS_QEMU_SYSTEM_X86_64`, `GTOS_QEMU_DATA_DIR`, and `GTOS_GRUB_MKRESCUE`
can select installed official tools. The x86_64 emulator still runs an i386
kernel; its `max` CPU advertises AVX so withholding OSXSAVE can be tested.
`GTOS_FP_OPTIMIZATIONS` and `GTOS_FP_CASE_FILTER` select focused runs.

For strict independent-engine coverage with official unpacked Bochs packages:

```sh
GTOS_FP_EMULATOR=bochs \
GTOS_BOCHS_ROOT=../gtos-bochs/root \
GTOS_FP_TEST_OUTPUT=/tmp/native-fp-bochs tests/native_fp_smoke.sh
```

The Bochs runner uses a process-local loopback-only RFB input connection to
send actual PS/2 events, and refuses all emulator-waiver flags. Its default
300-million configured IPS per emulated CPU and virtual clock allow the deliberately long
no-syscall loops to finish within the guest's virtual-time deadlines; they are
not claims about host throughput. It records the actual Bochs CPU model and
feature exclusions independently of the corresponding QEMU case labels.

Each output directory preserves the exact ELF/ISO, ELF hash, QEMU version,
launch arguments, actual CPUID fields, guest log, and injected-input count.
The linked-code audit permits only the four production transition helpers and
the explicitly sized assembly CPL3 payload. A second ELF contains the single
intentional CPL0 FNINIT; its audit must fail for that instruction, and its
separate negative boot must reach fatal CPL0 #NM without an ownership-pass
marker.

## Assertions

- First-instruction FXSAVE plus independent FNSTENV observes canonical controls,
  tags, pointers/opcode, all eight zero x87/MMX payloads, and all eight zero XMM
  registers, including after dirty x87/MMX owners are reaped or cancelled
- Two differently seeded processes validate every XMM lane and x87/MMX payload,
  distinct FCW/TOP/tag shapes, MXCSR, and full environment pointer history
- Both peers must accumulate at least three actual run ticks and dispatches
  before either issues a syscall; boot and an integer-only ring-0 task progress
- ABI/ticks/accepted and rejected writes preserve the complete seeded state;
  yields preserve legal null DS/ES/FS/GS plus DF; EMMS permits subsequent x87 use
- All MMX values survive a timer-measured integer-only loop and a separate yield
- Injected real keyboard and mouse IRQs must each interrupt CPL3 and run with
  kernel TS guarded; user comparisons detect resulting state corruption
- Pending unmasked x87 state survives a no-switch syscall, yield and timer
  preemption, then faults only at the user's FWAIT with #MF16
- Both direct and post-switch unmasked DIVPS must generate user #XM19
- Misaligned FXSAVE/FXRSTOR, cross-page/hole/supervisor images, reserved MXCSR
  in both FXRSTOR and LDMXCSR, AVX with OSXSAVE clear, #PF, and #UD are isolated
- Admission at capacity, never-dispatched cancellation, active lifetime churn,
  and final reap have exact allocator/task-count/FP-record counter baselines
- Actual masked virtual CPUs reject missing FXSR/SSE2/SSE3 before publication,
  control mutation or allocation; SSE2-only admission remains accepted
- Inherited OSXSAVE fails closed; unrelated AM/PGE control bits stay preserved

The matrix includes 32 MiB/1 CPU, 64 MiB/4 CPU, 128 MiB/1 CPU, and Intel/AMD
virtual models. Native FP users remain BSP-only in this fixture. A four-CPU
configuration proves compatibility with that SMP topology; its parked APs do
not establish AP scheduling or AP FP ownership. Concurrent active AP-work-pool
coverage belongs to the separate desktop acceptance fixture. These are
emulator observations, not physical CPU evidence.

## Known QEMU 10 TCG gaps: diagnostic mode is not acceptance

Strict tests intentionally fail on the observed QEMU 10.0.13 TCG omissions:

1. A synthetic user FLDENV/FNSTENV roundtrip does not reload FIP/FCS/FDP/FDS;
   post-preemption environment comparisons fail despite preserved payloads and
   controls. That user-side observation alone does not exclude an intervening
   trap. The separate desktop diagnostic additionally qualifies the omission
   with direct IF-clear kernel instruction pairs before admitting any user,
   excluding a trap-boundary bug from that qualification
2. DIVPS sets invalid-operation status in MXCSR with its mask clear but does
   not deliver #XM, both directly and after syscall/yield
3. FXRSTOR accepts reserved MXCSR bit 31 instead of raising #GP

For investigating the *other* assertions, three separate explicit switches
recognize only those exact independently observed failure signatures:

```sh
GTOS_FP_ALLOW_EMULATOR_POINTER_LIMITATION=1 \
GTOS_FP_ALLOW_EMULATOR_XM_LIMITATION=1 \
GTOS_FP_ALLOW_EMULATOR_MXCSR_LIMITATION=1 \
GTOS_FP_TEST_OUTPUT=/tmp/native-fp-diagnostic tests/native_fp_smoke.sh
```

Such successful runs emit `NATIVE FP DIAGNOSTIC PASS (EMULATOR GAPS REMAIN)`.
They do not satisfy the strict ownership gate, demonstrate pointer fidelity,
prove #XM containment, or prove the unsupported reserved-image #GP behavior.
No production workaround or production instruction ordering is changed for
these emulator omissions. Use an independent sufficiently accurate engine or
physical hardware to close those proof gaps.

## Desktop and firmware companion fixtures

- `make GTOS-native-fp-test.iso` keeps all user state checks strict
- `native_fp_desktop_bochs.py --root ../gtos-bochs/root --cpus 1 --output NEW_DIR`
  verifies strict FP users with legacy GUI, screenshot-derived game movement,
  actual CPL3 input IRQs, browser ABI1 and exact reap. UP does not prove AP work
- Stock Bochs BIOS with four CPUs correctly parks APs because inherited
  CR0.CD/NW fail the existing memory-type capture guard. No cache policy is
  changed. `native_fp_firmware_probe.sh` diagnoses that boundary read-only
- `native_fp_desktop_diagnostic.sh` builds a separate qualified QEMU diagnostic
  ELF/ISO at O0/O2 for modern GUI plus active AP work. Its IF-clear CPL0 qualifier
  and explicit test-kernel instruction-audit allowance are absent from normal
  kernels. It bypasses only the proven pointer omission and cannot emit a strict
  FP ownership PASS. Consult `docs/native-fp.md` for the exact evidence split
