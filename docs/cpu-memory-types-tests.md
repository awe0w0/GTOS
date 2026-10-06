# CPU memory-type compatibility checks

Run after these files are integrated into the normal repository layout:

```sh
python3 tests/cpu_memory_types_test.py
```

Run from the current external `gtos-work-pool` staging directory:

```sh
python3 tests/cpu_memory_types_test.py \
  --source src/cpu_memory_types.cpp \
  --include include \
  --include ../GTOS/include
```

Requirements: Python 3, a native C++ compiler able to compile freestanding i386
objects, and `nm`. The default compiler is `CXX` or `g++`; `--cxx` overrides it.
The host test does not execute privileged instructions. All generated sources,
objects, and executables live in a temporary directory and are cleaned up.

## What the runner verifies

1. Compiles the unchanged production source with `-O2 -m32 -std=c++11 -Wall
   -Wextra -Werror -ffreestanding`, plus kernel-compatible flags, and rejects any
   undefined symbols. No 32-bit libc is required for this object-only check.
2. Builds a native executable using the production capability-reading, snapshot,
   comparison, and cache-mode calculation logic. Only the privileged hardware
   access backend and final CR0/WBINVD instruction sequence are substituted.
   The runner fails if the expected assembly boundaries change, so such changes
   require explicit review rather than silently skipping the test.
3. Runs 970 assertions, including:
   - 60 individual low/high-half mismatches covering capability, default type,
     PAT, all 11 fixed registers, and all eight baseline variable-register pairs
   - MTRR absent, PAT absent, both absent, and MSR-only CPUs
   - Missing CPUID, missing leaf 1, and advertised MTRR/PAT without MSR support
   - Zero, 32, and unsupported 33 variable ranges; the final register at capacity
   - Unknown low/high MTRRCAP bits and optional fixed-register access guards
   - Physical-address-width mismatch, supported endpoints, invalid widths, and
     the documented 36-bit fallback when extended leaf 80000008h is absent
   - Invalid snapshot markers and an oversized/corrupted snapshot count
   - BSP cache/paging/protected-mode preconditions and AP PG/PE/IF preconditions
   - Cache-enabled and INIT-style AP entry, no-fill/normal cache-mode values,
     preservation of other CR0 bits, and no cache changes after rejection
   - Required PAT slot 0 = WB and slot 3 = strong UC, malformed PAT entries, and
     matching unsafe PAT configurations rejected before AP cache activation
   - All 88 fixed-MTRR byte fields and their start/end boundary crossings,
     fixed-range precedence, and the absence/disablement of fixed ranges
   - All 25 ordered pairs of supported variable memory types, plus UC dominance
     over otherwise conflicting triples in every enumeration order
   - WB islands in UC defaults, UC holes inside WB ranges, unaligned byte ranges,
     disabled variable ranges, MTRRs above 4 GiB, and an all-physical-address range
   - Reserved/type bits, unsupported WC types, noncontiguous masks, misaligned
     active bases, invalid snapshots, zero lengths, and 32-bit address overflow

## Shared-memory safety contract

`CaptureMemoryTypes` and AP preparation validate the architectural encoding and
the two PAT entries used by the shared page tables. Matching register values
alone are insufficient: the pool must call `MemoryTypeRangeIsWriteBack` for every
ordinary RAM range it will share or execute, before enabling the worker path.
This includes queue/context records, AP stacks and descriptors, kernel code and
globals, and page directory/table frames. The caller must independently establish
that these are RAM; the predicate does not discover physical memory or devices.

The predicate requires effective WB throughout every touched 4 KiB granule.
Enabled fixed ranges take precedence below 1 MiB. Variable ranges use the Intel
overlap rules: equal types agree, UC dominates, WB plus WT yields WT, and other
combinations fail closed. If no range matches, the default type applies. Disabled
MTRRs yield UC. With no MTRR feature, the supported legacy normal-RAM path assumes
WB. Shared ordinary-RAM entries must select PAT/PWT/PCD index 0; device entries
must select index 3 for strong UC when PAT is present.

## Privileged integration remains separate

These mocks verify fail-closed logic and access ordering, not the effects of
privileged instructions on a processor. The QEMU work-pool smoke test must also
verify AP CR0.CD/NW are clear after preparation, shared CR3 and PG/WP are correct,
and jobs plus fault isolation still pass. The helper never programs MTRRs or PAT;
firmware state must already match the immutable BSP snapshot.

Primary reference: [Intel SDM Volume 3A, December 2023](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf),
sections 12.5.3, 12.11.2.3, 12.11.8, and 12.12.
