# Genuine GTOS coarse TimeTicks leaf

This consumer compiles the actual pinned V8 time.cc with the separately recorded
GTOS source delta and includes unmodified time.h and elapsed-timer.h. The C ABI
returns a complete kernel-owned uint64 microsecond/tick snapshot. The backend
validates every fixed metadata field and accepts raw microseconds only through
INT64_MAX-2; the original upstream +1 excludes null and Max. Failed reads, bad
metadata and signed-range exhaustion terminate with Exit 0x49000001.

The pinned TimeTicks and ElapsedTimer calls cover Now, IsHighResolution,
ThreadTicks::IsSupported, Start, IsStarted, Elapsed, Restart, HasExpired and Stop.
An explicit same-time Elapsed overload establishes zero elapsed without assuming
that two syscall reads cannot straddle an IRQ. Live duration checks compare the
actual syscall payload used by each genuine method, and require real progress.
The stored elapsed_us is the genuine elapsed duration between the record first
and last payloads at positive completion. Stage1 is published after complete
sample stores. Mode2 then only yields, so RequestExit cannot interrupt a sample
record update. Mode3 subsequently advances Now until signed-domain rejection;
its final raw last payload is invalid for TimeTicks, while elapsed_us preserves
the positive duration measured before the boundary loop.
Pure mutations call the identical backend validator: they demonstrate its
metadata/range rejection, and do not pretend to be malformed kernel snapshots.
The conversion never extends low32 wrap or maps uptime to epoch.

Record native_clock_record is 192 bytes at 0x40020000, kind=1. Modes: 0 normal
positive Exit with stage2, 1 guard-write PF6 at 0xBFFFCFFC with stage1, 2 external
RequestExit while yielding with stage1, 3 actual PIT progression across a coherent
signed-domain boundary seed into adapter fatal with stage1. Two real RW VM pages
are retained in every mode for exact Stop/Reap accounting, with byte patterns
(offset XOR 0x37)/(offset XOR 0xA9). Clock reads allocate no pages themselves.
Raw wire/GPR/sentinel cases are independently covered by the raw fixture; those
unexecuted counters remain zero in this record.

Competing explanations checked before implementation: signed EAX cannot carry
unsigned time; reads cannot reconstruct missed low32 wraps; +10000/IRQ drifts
from the programmed divisor; +9999/IRQ loses carry; valid uint64 uptime may fail
the smaller V8 domain; Null/Max must never represent a successful Now; a same-IRQ
zero duration is valid; genuine IRQs may occur between reads/Yields. Metadata
resolution remains coarse and IsHighResolution is false. Thread CPU and epoch
wall time remain unavailable. Full V8, Platform precision, Chromium, JIT, browser,
video and HTML5 qualification remain false.
