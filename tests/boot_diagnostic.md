# boot_diagnostic host unit — notes

The unit is built from the production helper body and a freestanding i386 entry, then linked into a static ELF with
no libc. It is never the kernel image and never calls `kernelMain` or any device.

## What the test establishes

- an absent option leaves the normal flow unchanged
- the two exact tokens select the two checkpoints
- similar prefix and suffix tokens are distinguished: a longer option name or a bare `bootdiag-stop` is not our
  token, while a wrong value behind the prefix is a refusal with a stated reason
- empty, unknown, duplicate, unterminated and oversize inputs all refuse
- the 256 byte span contract: `capacity` is the caller's readable guarantee, it is never silently clamped, and an
  oversize span refuses so a valid early token cannot mask a later duplicate
- 64 interleaved parses are deterministic and retain no state
- the three markers are distinguishable by content, not by length, and the reject marker claims no checkpoint
- the rejection reasons are named and distinct

## Two defects corrected from the earlier packet

1. The old `ExpectStop` helper asserted `reject == RejectNone` as part of accepting a stop, so a refusal could not be
   expressed through it, and the same input was then also expected to refuse. Acceptance and refusal are now separate
   predicates. The production return values were not changed to fit the test.
2. The old test asserted the three markers differ in *length*. Length is not a business invariant; the test now
   compares marker content and checks that each is recognisable on its own.

A third defect was in the test itself: a NUL-terminated line is readable *including* its terminator, so the span size
carries the NUL. The earlier helper omitted it, which made every line-final token look unterminated.

## Build profile

The test compiles with the frozen production flags from the packet profile and is linked with the fixed individual
sequence. The ELF gate runs before any execution: ET_EXEC, i386, entry inside an executable PT_LOAD, no INTERP and no
DYNAMIC. Only that exact test ELF path is granted EXEC.