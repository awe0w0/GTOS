# Genuine V8 ThreadId and compiler-emulated TLS leaf

This bounded single-user-Task consumer compiles the entire original pinned V8
`src/execution/thread-id.cc` and `thread-id.h`. It uses their actual Try, Current,
Invalid, FromInteger, validity, comparison and integer methods. The original
`lazy-instance.h` and `platform.h` includes remain. An explicitly derived GTOS
`Semaphore::NativeHandle` declaration permits parsing; it implements no semaphore
operation and leaves the full V8 GN runtime guard closed.

Clang24 i686-none `-femulated-tls` emits the real private V8 4-byte TLS object and
four public trivial TLS arrays. The compiler controls are 16 bytes with four
32-bit fields: size, alignment, cache address and initial template pointer.
The linker keeps a sorted exact 80-byte control range; V8's control remains LOCAL.
The generated read-only inventory uses actual pointer relocations to the two
complete 64-byte compiler templates. Zero257/align64 and zero65504/align16 have no
large templates. The nonzero64 array aligns16; the other initialized64 aligns4096.
No constructors, destructors, TLS initialization guards or native FS/GS access
are provided. The checked resolver allocates from the accepted real 64KiB heap.

The record is 512 bytes at 0x40030000. Stage1 follows the genuine first Try(-1):
its cache pointer is nonzero and the allocated 4-byte value is zero; only this
TLS object is live. After 32 delivered ticks, stage0 marks transition to Current1
and four live objects. Stage2 publishes their metadata and full byte patterns;
normal terminal modes then wait32 ticks and use the actual compiler getters
again. A cancel may occur during this stage2 hold. A waiting peer loops on yield,
so its later stability is established by independent kernel readback, rather
than a claim that each loop invokes TLS. Stage3 arms terminal paths or records
completed normal finalization. The fixture reads stopped state for final checks;
the record is not an atomic 512-byte transaction.

Array role1/2/3 byte i is `(i ^ nonce ^ (0x53 * role)) & 255`. The V8 int remains1.
The original formal record nonce is A9; a qualified private CreateElf copy may
patch only its four bytes at offset28 to37 for the independent-PAS peer. This
models separate processes, not threads sharing an address space or global V8 IDs.

Modes0..6 respectively finalize twice normally; Exit0 retaining storage; write
stack guard; wait for RequestExit73; request actual compiler zero65504 TLS and
fail OOM; corrupt that unvisited control alignment to6; corrupt its size toU32MAX.
Modes4..6 write A5A5A5A5 before the real getter and assign its return if it returns.
Expected resolver failures are4A000006 (OOM) or4A000003 (metadata). Normal cleanup
preserves the old metadata/address observation, clears runtime/control caches,
releases the heap and records final_heap_handle0. Abrupt paths require private-PAS
kernel Reap; they do not invoke C++ TLS destructors.

The builder separately checks official pinned SDK/header provenance, genuine
GTOS predefines, static ELF32/64KiB/256pages, scalar instructions, protected input
bytes and relocations, and a complete linked stack bound below8176. Its independent
engine distinguishes duplicate LOCAL names by object/section/VA and binds each
stack-usage report to its actual owner. Hidden-sret ret4 is permitted only for
explicit real 4-byte ThreadId signatures. O0's actual libc++ atomic memory-order
switch keeps all bounded relocated targets; its exact unsigned guard and saved
index reload are checked. Unknown indirect calls or dispatch forms fail closed.
Every retained function and executable byte, including the genuine CHECK trap
and unreachable return bytes, remains covered. Static manifests have guest_pass
false. Real guest acceptance is a separate immutable evidence set.

Native FS/GS TLS, shared-PAS threads, mutexes/semaphores, platform-key TLS, Isolate,
full V8, JIT, Chromium browsing, video and HTML5 acceptance remain false.
