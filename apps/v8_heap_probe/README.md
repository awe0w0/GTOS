# Actual pinned V8 native heap consumer

This leaf compiles the unchanged pinned `src/base/platform/memory.h` against
actual GTOS IA32 C allocation functions and genuine LLVM libc/libc++ declarations.
It adds no V8 platform implementation or successful runtime substitute. The
full V8 GN target remains fail closed. Browser, video, HTML5 and JIT acceptance
remain false.

The new builder targets `i686-unknown-none-elf` without Linux/Unix identity or a
Linux sysroot. It generates the official LLVM libc `malloc.h` in addition to the
previous SDK header set. All actually consumed headers and generator sources
are compared with their pinned Git bytes or classified as exact generated SDK,
GTOS source, or hash-bound Clang resource inputs. The original accepted VM builder,
consumer and qualification tools are retained unchanged.

The independent object `memory_calls.cc` invokes actual upstream Malloc, Calloc,
Realloc, Free, AlignedAlloc, AlignedFree and AllocateAtLeast<unsigned char> with
runtime arguments. The release profile uses NDEBUG/V8_LOGGING_LEVEL=0. Its
invalid-alignment observation is the downstream POSIX rejection after compiled
out DCHECKs; it does not extend V8's valid alignment precondition. The upstream
Realloc size-zero CHECK retains its real immediate-crash branch.

The same native_heap/heap.cc and operators.cc implement the allocation services.
C++ calls use the genuine <new> declarations, exercise all eight allocation and
twelve deallocation replacement functions, zero-size normalization, nothrow
failure, distinct simultaneous zero-sized allocations, and an actual align64
nontrivial object's constructor/destructor. A genuine new(std::nothrow) expression
for an align64 object larger than the arena also proves a null result suppresses
its constructor, without creating any large stack object. Ordinary OOM terminates in the
no-exception native profile; no exception/new_handler/thread runtime is claimed.
Only the previously qualified exact memcpy/memset support object is reused.

ProbeRecord is ten unsigned IA32 words (40 bytes) at 0x40020000:
version, mode, stage, base, handle, primary, primaryBytes, neighbor,
neighborBytes, checks. Modes 0/1/2/5 retain two 4096-byte allocations with whole
byte patterns offset XOR 0x37/0xA9. Mode 0 exits successfully at stage2; mode1
writes the original stack guard at 0xBFFFCFFC; mode2 yields for RequestExit; mode5
calls real ordinary operator new[] with a volatile maximum size and must exit
0x48000010. Mode3 releases the last allocation and exits with no dynamic region.
Mode4 releases, allocates fresh 4096-byte storage, verifies every requested byte
is zero, and retains that fresh allocation for ordinary exit/reap acceptance.

Full ELF32 admission, integer machine-code classification and complete retained
function/byte/call-stack qualification are separate from real CPL3 guest proof.
The fixed 64KiB boot-file limit, 254 static image pages plus two stack pages, and
8176 usable bytes of user stack are unchanged. Non-PAE IA32 still has no hardware
NX; this module provides bounded data allocation and no JIT qualification.

The full builder runs independent whole-ELF stack admission twice: the first
proof is bound to manifest.unqualified.json's preserved bytes, then
whole-stack/status.json binds the final immutable manifest, including every
actual helper call target. qualification_complete and stack_call_chain_qualified
cover only the explicitly named static admission scope. guest_pass remains false
until the separate real CPL3 fixture verifies these exact six ELF inputs.
