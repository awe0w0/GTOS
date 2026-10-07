# GTOS native coarse monotonic clock and genuine V8 timer leaf

Acceptance packet, 2026-10-08. The kernel clock and the actual pinned V8 timer
leaf pass static qualification and the completed real CPL3 matrix. Full V8,
Chromium, video and HTML5 remain unaccepted.

## Interface and source

The kernel-owned ABI is CLOCK_READ 0x4712: EAX status only (0 or a negative
error), EBX request VA, ECX exactly 20 bytes. The request is
{version=1, clock_id=MONOTONIC(1), flags=0, result_va, result_bytes=48}.
The 48-byte result is
{version, clock_id, unit, source, capabilities, resolution_us,
uint64 microseconds, uint64 delivered_ticks, pit_input_hz, pit_divisor}.
Its exact metadata is 1/1/1/1/7/10000 and 1193182/11931. Checked-copy error order
is size, complete request snapshot, version, flags/result size, clock ID,
complete output prevalidation, guarded kernel snapshot, checked copy. Unsupported
REALTIME/THREAD_CPU and malformed buffers preserve output, as do sticky uint64
exhaustion (-75). This ABI accepts no caller seed and allocates no frame/region/heap.

The clock estimates delivered BSP PIT interrupts only. With the unchanged PIT
programming, period_us = 9999 + 373182/1193182 and
microseconds(N) = floor(N * 11931000000 / 1193182). The bounded fractional update
avoids a long-time product/division; the typed snapshot preserves the full 64-bit
count and value. Its conservative resolution is 10000us. It has no sub-IRQ
interpolation, missed-IRQ reconstruction, oscillator calibration, Unix epoch,
thread CPU time or cross-AP synchronization claim. Legacy uint32 ticks retain
existing wrap behavior. Yield/query/admission/Reap do not increment the new clock.

## Actual upstream consumer

V8 pin be042d4462bee463c9b785701b8c6ee4576ee0a3 supplies the actual time.cc,
unmodified time.h and unmodified elapsed-timer.h. LLVM libc pin
ebe33e01982dbbf879661e3b6b78450f3020a53f and libc++ pin
97b436da4c33663581d394f4ee0a5977fc38c2f4 supply official hdrgen declarations,
unmodified C++ headers and generated official configuration. The nested
GoogleTest declaration pin is 4fe3307fb2d9f86d19777c7eb0e4809e9694dde7.
Clang24 compiles i686-unknown-none-elf with -nostdinc/-nostdinc++, real SDK and
its hash-bound resource headers. Actual predefines reject Linux/Unix/POSIX
identity. The GN full-GTOS backend guard remains fail closed.

The precise new time.cc patch is da1e7eca11c5f4e337d8701702c75a5e2c218389e2d9e7e9bbbf4eae22df1c6f.
Original time.cc SHA is f6273ee921bb880a03571b78c26d87d4077b51fff8c1f9bc4f47f7570868aebd;
derived SHA is 7d71a37164ca6d48acada8e7d047f5f61e8ded9fb252b6f82c3a167960d9e3a7.
It adds only the explicit GTOS Now bridge, false IsHighResolution and false
ThreadTicks::IsSupported, plus GTOS-only guards for unused CPU/Mutex/platform
includes. Original non-GTOS include ordering is retained. The frozen upstream
checkout is not edited. Time::Now/NowFromSystemTime receive no uptime-as-epoch
implementation; unsupported services have no successful stub.

The bridge validates every fixed metadata word and accepts raw us only through
INT64_MAX-2. The original upstream Now then adds one, excluding Null and Max.
Nonzero syscall status, invalid metadata or a signed-domain violation terminates
with Exit 0x49000001. It returns no cached/clamped/null time. The actual consumer
calls TimeTicks::Now/IsHighResolution, ThreadTicks::IsSupported, and genuine
ElapsedTimer Start/IsStarted/Elapsed/Restart/HasExpired/Stop. Explicit same-TimeTicks
elapsed establishes zero without assuming that sequential reads straddle no IRQ.
The real Now payload used by each live duration check supplies its exact value;
positive completion records elapsed_us = last.microseconds-first.microseconds.
Pure metadata/range mutations use the identical backend validator and are
reported as validator checks, not malformed snapshots from the real kernel.

## Static qualification completed

Formal immutable four variants reside in
artifacts/v8-clock-final-four-20261007T215727Z/mode-{0,1,2,3}/.
All source_base fields are 390b0789762290236de11a5279a20bc135026fa1. Each manifest
and whole-stack/status.json bind the actual ELF, compiled sources, 1123 input
paths and 1021 pinned source inputs. ELF32 static ET_EXEC, no interpreter,
undefined-symbol closure, actual GTOS ELF32 validator and scalar machine-code
classification pass. Complete executable bytes, linked object correspondence,
readonly spans, relocations and exact canonical alignment padding pass.

Every variant retains 27 actual functions, ten consumer call sites, four actual
nested upstream calls and three genuine inline timer operations. The full call
chain is 437 bytes with 7739 bytes headroom. The only four-byte callee return
cleanups are the three actual eight-byte class-return signatures TimeTicks::Now,
ElapsedTimer::Elapsed and ElapsedTimer::Restart. All other cleanup values are
zero or genuinely nonreturning; unknown cleanup/indirect calls fail closed.
Each ELF has two static LOAD pages plus the unchanged two user stack pages.
The fixed profile remains 65536 boot-file bytes, 256 total admitted user pages,
8192 mapped/8176 initially usable stack bytes; no capacity expansion is claimed.

The frozen qualifier SHA is
3c8e6a8d8a4cc368039362b25f2426870a6c3cbcfab21f69f6f19d1b3bd7e843.
Artifact-only 22 negative controls are in
artifacts/native-clock-qualifier-negative-20261007T220642Z/status.json.
They reject unknown ret4, each genuine sret cleanup0/8, mixed cleanup paths,
missing/wrong/duplicated real consumer and nested targets, wrong code owner,
executable/readonly coverage gaps and source/dependency/ELF/manifest binding
failures. Actual frozen AST guards, the real executable coverage function and
original CLI are exercised; no genuine rebuild or guest is rerun. All four
formal manifests, proofs and source bytes remain unchanged.

## Completed real guest matrix

native_clock_record is 192 bytes at 0x40020000, kind=1. Its first/last entries
are actual syscall result payloads; legacy tick fields are compatibility bits,
not a user wrap extender. Two real RW VM pages retain XOR37/XORA9 byte patterns.
Stage1 publishes complete positive samples before the terminal mode action;
mode2 subsequently only yields so RequestExit cannot truncate a record update.
Mode0 exits0 with stage2; mode1 writes UserStackBottom-4 and must fault with
CR2=0xBFFFCFFC/error6; mode2 waits for external RequestExit73; mode3 lets real
PIT IRQ progression cross the signed TimeTicks limit and must exit49000001.
Mode3 retains its pre-boundary genuine elapsed measurement while its final raw
last is deliberately outside the signed domain. It is not raw uint64 exhaustion.

The final matrix passed 14 boots, 40 victim cases and 54 exact baseline checks:
normal O0/O2 kernels and raw probes on 32M/smp1 and 64M/smp4 (four boots, 24 cases);
three independent low32 rollover scenarios on O0/O2 32M/smp1 (six boots, 12 cases);
genuine signed-domain boundary on O0/O2 (two boots, two cases); raw sticky uint64
exhaustion on O0/O2 (two boots, two cases). O0/O2 describe the GCC13 kernel
and raw probes; all four genuine V8 ELFs retain Clang24 -Oz. Coherent fixture-only seeds are calculated with independent
host bigint divmod. Bootstrap headroom is 256 real IRQs, enlarged after the
preserved -16 locating failure occurred before victim startup; the clock is not
reseeded backwards while running and no production seed API is introduced.

The completed acceptance binds captured actual QEMU invocation/ISO/kernel/
module hashes, exact ordered RAW/V8 cases, CS23/private CR3, every successful
snapshot against the real IRQ observer and rational floor formula, error/output
sentinels, old tick bit patterns, peer preemption/boot progress, exact fault/Exit/
RequestExit and retained-page patterns, victim Stop/Reap baseline and final peer
Reap baseline. Formal static manifests remain immutable guest_pass=false; real guest success
is separately recorded and tied to those hashes.

Full V8/Isolate/Platform precision, JIT, Chromium, native browser, video, media,
HTML5 and epoch/thread clock acceptance remain false.


The exact real guest status is artifacts/native-clock-final-20261008-01/status.json
SHA369a65630df6fa4a988da1428cbb5ae811f7efaf3b7bb5176de1068e7f740ebb.
Readback in artifacts/native-clock-final-readback-20261008-a verifies all113
source/input hashes, fourteen actual QEMU exits33 and112 per-invocation module
annotation bindings. The guard fault error is exactly6, not only its low bits.
Each 192-byte stopped record and both complete retained page patterns are read
before deferred Reap. Forty victim Reaps and fourteen final two-peer Reaps each
restore their exact baseline;54 checks reclaim68 stopped processes in total.
CPU-bound zero-syscall peer, yielding peer, ring0/boot and independent real IRQ
progress are captured explicitly. Queries preserve non-result GPR/segment values
and allocate no frame.

Rollover scope is deliberate: RAW0 first-before/last-after crosses signed legacy
ticks, low32 ticks and low32 microseconds; same-boot V8 mode0 then consumes the
post-rollover64-bit value. The separate V8 mode3 boot observes a valid first
sample then an out-of-domain last sample and fatal Exit49000001. The raw uint64
exhaustion boot proves sticky -75 with repeated complete48-byte sentinels while
legacy ticks/real IRQ delivery continue. No running clock is reset or rewound.

Original ISO ELF inputs are byte-identical to their qualified static artifacts.
The fixture makes a private CreateElf input copy and annotates only the4-byte
scenario field at record offset20. The runner records its exact data offset,
changed bytes, predicted annotated copy SHA and unchanged executable PT_LOAD
hashes. That predicted SHA is not mislabeled a captured runtime ELF SHA.
There are twelve unique fixture ISO images;normal32M1/64M4 reuse each normal
ISO, giving fourteen actual QEMU invocations. Four-core configuration describes
the captured QEMU configuration; it is not a shared-address-space/AP-clock claim.

All localization failures remain retained: missing fixture include/constructor
order, insufficient16IRQ bootstrap headroom and the interrupted diagnostic line
parser. Source fixes only addressed the fixture; the production seven files and
formal four V8 ELFs stayed frozen. Final logging guards wrap output after actual
IF1 sample/preservation checks, while the independent IRQ observer still counts
only delivered hardware interrupts.

## Review and production desktop

Production O0/O2 full normal kernel builds and scalar audits passed;47 independent
exact bigint seeds passed actual freestanding i386 host checks. Root independently
reviewed the production/consumer source and current inputs of the four formal
ELFs (1428 unique paths). The protocol review binds final production7 and raw
fixture/probe7 bytes, and records the reviewer's earlier producer author role;
root's separate production review complements it. Qualifier negative controls
are22 actual AST/coverage/original-CLI checks, not22 additional guest executions.

The visible task-owned GTK VM was updated through actual QMP CD change and
system_reset with production clock O2 kernel
cf61aebd971e4608024229d88ea439d1f9333b20677d9915fd31530be394efad.
It preserves the previously accepted PNG ELF
120c521bda9bf7b14b042ebbcbe66c110a03905e7f33e3d34a411f0933c78c48,
the same PID1346/window and private8MiB apps.img path/contents. The desktop ISO
SHA is6fba6353b805147f1c32012302d1db5ea671209341d3b9caa72bc989147e8873.
Fresh boot logs prove desktop/PNG read-decode-present, surface reclaim, native
reap3/runtime pass and6 verified AP jobs. Independent ISO/RockRidge/ElTorito
review matches every staged payload, all copied bootloader files and all9216
scaled PNG screenshot pixels. This is a production desktop regression and a
separate accepted PNG path, not a native Chromium browser run.

Reproduce the final native guest matrix in the saved task environment after
building all four genuine modes with tools/build-v8-clock-probe.py and its
required qualified compiler/SDK/PNG inputs. The actual saved command is
artifacts/run-native-clock-final-20261008-01.sh. It sources scripts/env.sh,
uses the absolute GCC13 wrapper as CXX and invokes tests/native_clock_smoke.sh
with a fresh artifact directory and the four ordered formal stripped ELFs.
The builder and runner refuse reused output directories. Host bootstrap Python
must be >=3.9;this task uses its cached Python3.11 runtime. Keep caches and source
snapshots. Full GTOS GN remains fail closed until the later platform gates pass.

Independent final aggregation in artifacts/native-clock-aggregate-review-20261008-desktop-a/status.json directly verifies all fourteen original guest logs, twelve actual ISO kernel members, ninety-six original module members, ordered GRUB configurations, complete 48-byte snapshots and stopped 192-byte records, exact physical accounting and captured QEMU argv. Its 3866 input bindings were rehashed at completion. It does not rerun a guest or reinterpret static guest_pass=false records.
