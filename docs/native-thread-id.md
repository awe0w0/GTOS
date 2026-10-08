# Native IA32 compiler-emulated TLS and genuine V8 ThreadId

This module implements bounded trivial TLS storage for a single native user Task
in its private process address space. The actual pinned V8
src/execution/thread-id.cc and thread-id.h consume that storage without edits.
The source/compiler, resolver, complete O0/Oz consumer qualification and real
GTOS CPL3 guest gates pass. This accepts a trivial compiler-emulated TLS and
ThreadId leaf. Full V8, Chromium, video, HTML5 and JIT remain unaccepted.

## Compiler and source contract

Clang24 targets i686-unknown-none-elf with -femulated-tls and the existing
integer-only profile. The compiler emits ordinary writable 16-byte controls:
uint32 size, alignment, cached_address and initial_value at offsets 0/4/8/12.
The original private V8 control remains STB_LOCAL. These are compiler-emulated
controls, with no native FS/GS or ELF PT_TLS/SHF_TLS admission.

V8 pin be042d4462bee463c9b785701b8c6ee4576ee0a3 supplies the unchanged ThreadId
implementation and genuine header closure. Official LLVM libc and libc++ pins
ebe33e01982dbbf879661e3b6b78450f3020a53f and
97b436da4c33663581d394f4ee0a5977fc38c2f4 supply declarations and actual C++ headers.
GoogleTest declaration pin is 4fe3307fb2d9f86d19777c7eb0e4809e9694dde7.
The existing GTOS foundation patch supplies target identity; the additional
GTOS-only semaphore NativeHandle=uint32_t branch is a declaration for those
headers, with no semaphore or thread operation implementation. The complete
GTOS V8 GN runtime guard remains fail closed. No Linux target SDK is used.

TryGetCurrent reads TLS even before an ID has been assigned. It therefore
allocates storage while returning Invalid (-1). Current assigns 1 in each
independent program through the real original atomic increment; repeated
Current/TryGetCurrent calls retain 1. Equal IDs or equal virtual addresses in
different private address spaces are legitimate. Isolation requires private
CR3 and full owner-pattern byte checks, rather than ID uniqueness.

## Resolver and lifetime

apps/v8_thread_id_probe/emutls.cc defines __emutls_get_address with an immutable
exact-slot inventory generated from the actual compiler objects. The linker
sorts compiler-control sections and keeps the original local binding. Inventory
count is 1..8; each payload is 1..65504 bytes with power-of-two alignment up to
65536. Constant templates must fit completely in the bound readonly range.
There is no runtime descriptor registration.

Unknown addresses are compared for exact membership before requested-control
bytes are read. Every control's size, alignment, template and cache word must
match the immutable slot and resolver-owned cache. Live payloads must be
aligned, within the native arena, mutually disjoint and fully initialized.
The real native posix_memalign allocates storage. Complete volatile zero/copy
stores and a compiler memory barrier precede publication to both caches.
Allocation failure, malformed metadata, cache inconsistency and reentry
terminate through real GTOS Exit, preserving the prior objects.

Exit codes are 0x4A000000 OR reason: registry 1, unknown control 2, metadata 3,
cache 4, recursion 5, allocation failure 6 and post-finalization resolution 7.
The unchanged original V8 CHECK path retains its int3;ud2 trap bytes.

gtos_emutls_finalize validates all controls, frees every owned payload and
clears both caches. It is idempotent. Subsequent resolution terminates; a query
can still report finalized storage. This performs trivial storage cleanup.
Dynamic initialization, TLS destructors, platform keys, shared-PAS threads and
heap synchronization are outside this profile. Abrupt Exit/fault/cancellation
does not call this finalizer; the kernel owns Stop and deferred Reap.

gtos_emutls_query does not allocate. Null output returns 0. Non-null output must
be a valid writable caller-owned object outside controls, inventory and payload
storage. Diagnostics are not an independent oracle. Arbitrary private-BSS
corruption or a caller freeing resolver-owned TLS payloads is outside the
one-owner storage contract.

## Consumer and independent observations

The healthy compiler inventory contains the V8 int (4/4), zero array (257/64),
constant-initialized array (64/16) and aligned array (64/4096). A fifth compiler
object (65504/16) remains uninitialized until the real failure getter. Its valid
request cannot fit after healthy objects occupy the single 64KiB native arena.
Malformed modes change only that owned, still-uninitialized control's alignment
or size, then execute the actual compiler getter. They are deliberate metadata
mutations; the compiler is not claimed to emit malformed controls.

The 512-byte diagnostic record is at 0x40030000. Stage 1 exposes the first
Invalid result and allocated zero int; transition stage 0 hides incomplete
updates; stage 2 exposes four healthy objects and owner-pattern bytes.
Stage 3 records the terminal action or completed normal cleanup. Kernel phase
snapshots must guard the entire record/control/payload/frame read and require
the exact live stage. Mode 3 may be cancelled during its stage-2 yielding hold;
it has no stage-3 or completed post-hold getter claim.

The actual consumer checks all original zero/template bytes before writing
owner patterns. The kernel independently reads the first int's zero bytes and
the complete later owner patterns. Original constant templates are also bound
to full readonly ELF bytes. These are distinct sources of evidence.

The peer uses a private CreateElf copy of mode 3 with only the nonce word at
record offset 28 changed from 0xA9 to 0x37. Formal ELF/ISO originals remain
byte-identical and executable LOAD bytes remain unchanged. A recorded runtime-copy
SHA is the predicted four-byte annotation binding, rather than a captured
whole runtime ELF hash. Actual ISO module copies have independently read file
hashes. The retained peer yields and supplies progress/bytes
for isolation checks; it does not establish shared address-space threading.

A first resolver access allocates the native heap's 16 resident pages, plus the
actual new user page table for this ELF geometry. Normal last-free releases
them before Stop; abrupt termination retains them until deferred Reap. Exact
static cost includes actual image pages, two stack pages, unique user tables
and the page directory. Reload must start with cold controls, original initial
bytes and a fresh globally monotonic region handle.

## Qualification and acceptance

Resolver self-review and independent source/direct O0/O2 linked-ELF review pass;
730 input bindings were rehashed by the latter. Thirty-two real i386 host model
cases cover normal/repeated cleanup, unknown pointers, malformed metadata/cache,
allocation failure, reentry and use after finalization. The host's artifact-only
64KiB allocator is a bounded transport model. It proves no actual GTOS heap/PAS,
16-frame allocation or deferred-Reap behavior. Production-object O0/O2 links
use the unchanged real native heap and retain exact controls/templates.

Fourteen immutable Clang24 formal inputs comprise modes 0..6 at O0 and Oz.
Each binds 1162 compiled input files and 1052 pinned SDK inputs. Independent
direct ELF and call-graph review rehashed 7659 unique input files. It checked
whole controls, complete templates, generated immutable specs, original LOCAL
binding, same-name local owners, hidden class-return ret4, original CHECK traps,
all allocated bytes/relocations and retained scalar code. The actual O0 atomic
switch's unsigned guard, five relocated targets and saved-index reload remain
covered. Unknown dispatch forms fail closed.

O0 normal has 73 retained functions and a 564-byte complete stack bound; other
O0 modes have 492-byte bounds. Oz normal has 43 functions and a 384-byte bound;
other Oz modes have 316-byte bounds. Stripped files are 21024 bytes (O0) and
16928 bytes (Oz), with actual static frame costs 11 and 10 respectively.
The unchanged boot-file limit is 65536 bytes, resident user budget 256 pages,
and user stack 8192 bytes with 8176 usable bytes.

Twenty-seven independent rejection controls pass: 23 exact-frozen-source AST
synthetic controls and four actual unmodified qualifier CLI runs against private
formal input copies. The latter reject an unqualified manifest, an incorrect
ELF binding, forged initial cache bytes and a ret4-to-ret8 machine-code mutation.
These are not 27 full compiler or guest runs; immutable formal originals remain
unchanged. The independent actual-log parser also rejects 76 in-memory mutations
of captured logs, with no guest rerun or original-file modification.

Real acceptance is the minimal two-boot matrix: GCC kernel O0 / Clang app O0 at
32M/1 CPU, and GCC kernel O2 / Clang app Oz at 64M/4 CPUs. It is not a Cartesian
matrix. Both actual QEMU runs exit 33. Each executes seven terminal modes plus
a cold normal reload: 16 victim cases, 18 TLS admissions, 20 process Reaps and
18 exact reclamation baselines in total. The two actual ISOs contain 16 original
qualified module copies. Sixteen saved annotation entries cover eighteen admissions,
including the two normal reloads. They describe the predicted CreateElf nonce
annotation, rather than capturing executed whole-ELF bytes.

The actual guest verifies first Try allocation while Invalid, Current/repeated
identity, private CR3, every control/cache and full owner-pattern payloads. Normal
finalization twice clears all caches and makes old heap addresses unreadable
before Stop. Exit, guard fault, cancellation, real OOM and malformed metadata
retain the prior objects and correct terminal status until exact deferred Reap.
Reload observes cold controls, original initial bytes and a new globally
monotonic region handle. These results do not implement TLS destructors.

Guest logs contain 68 complete 512-byte record observations and 340 complete
16-byte compiler-control observations.
Healthy payload evidence instead combines the independent kernel's full guarded
ReadMemory/byte predicates with ordered successful assertions, plus the
consumer's initial-value checks. Full payload bytes are not externally dumped.
The full original readonly templates are independently decoded from actual ELF
bytes. Counts or diagnostic tags alone are not payload proof.

The preserved first guest attempt failed after its first normal victim had
already finalized and Reaped correctly. It recorded a legitimate victim
runTicks=0, then a combined survivor assertion failed before component counters
were dumped. This does not identify that attempt's sole failing component.
Source analysis established that explicit Yield dispatches do not charge
runTicks; only a PIT-preempted current task receives that counter. The corrected
oracle requires actual runTicks growth from the CPU-only peer, with no syscalls
or yields, and TLS-peer yield progress plus full stable bytes. All component
counters are dumped before combined assertions. The accepted second attempt
directly observes a live TLS peer with runTicks 0 to 0 while its yields grow,
and a CPU-only peer whose data and runTicks grow. PIT, ring0 and boot progress
also pass. Only the test oracle and diagnostic ordering changed; production
runtime and qualified consumer inputs remained frozen. The failed attempt and
earlier checker assumptions are retained.

## Retained evidence and reproduction

Evidence is retained outside the repository at F:\GTOS-Chromium:

- artifacts/native-thread-id-source-freeze-20261008-b/status.json binds the
  fifteen resolver/consumer/qualifier/builder source files.
- artifacts/native-emutls-root-source-review-20261008-a/status.json binds the
  independent resolver review and its bounded-host-model scope.
- artifacts/v8-thread-id-final-fourteen-20261007T232342Z/status.json indexes the
  fourteen immutable formal inputs; their static guest_pass fields remain false.
- artifacts/native-thread-id-root-formal-review-20261008-a/status.json binds
  the independent direct ELF/SDK/call-chain review.
- artifacts/native-thread-id-qualifier-negative-20261008-b/status.json binds
  the 23 AST and four real CLI rejection controls.
- artifacts/native-thread-id-final-20261008-01 retains the unsuccessful first
  guest attempt; artifacts/native-thread-id-final-20261008-02/status.json
  records the actual accepted two-boot matrix.
- artifacts/native-thread-id-aggregate-review-20261008-desktop-a/status.json
  binds the independent actual ISO, log, frame-cost, ordering and input audit.
- logs/native-thread-id-module-status.json binds the final current eighteen
  code files, this document and the acceptance receipts. Historical draft
  documentation bindings are explicitly superseded by final document review.

The builder is tools/build-v8-thread-id-probe.py; use its required pinned SDK,
actual Clang24 executable, integer-only target and qualified validator options.
Select --optimization O0 or Oz and --mode 0 through 6, with a fresh output
directory for every immutable input. The compiler, source bindings, generated
SDK, actual command argv and qualification results are retained in each formal
input's manifest and logs.

Source scripts/env.sh for the qualified GCC13/QEMU/GRUB runtime, put the cached
Python 3.11 runtime first on PATH, and invoke tests/native_thread_id_smoke.sh
with a fresh directory followed by all seven O0 stripped ELFs, then all seven
Oz stripped ELFs in mode order. The runner refuses reused evidence and checks
all fourteen formal source/input/ELF proofs before execution. Its commands.json
retains actual tool hashes, argv, environment, logs and return codes.

This module adds leaf app/fixture/build files and makes no production desktop
kernel change. The existing visible PNG desktop therefore keeps its accepted
production ISO and private apps.img. Headless acceptance ISOs are separate.
A later production desktop image update must refresh and reset that same
visible VM window while preserving its private disk.
