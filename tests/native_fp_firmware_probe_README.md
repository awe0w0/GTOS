# Pre-paging firmware memory-type observation

Run from the repository with:

```sh
sh tests/native_fp_firmware_probe.sh
```

This standalone GRUB Multiboot fixture links the actual, unmodified
`src/hardwarecommunication/cpu_memory_types.cpp`. It samples the BSP before
paging, reports the exact initial `CaptureMemoryTypes` CR0 predicate, and calls
that production function. It is a diagnostic, not an AP-worker acceptance test.

## What it qualifies

- CPUID availability and leaf limits precede feature observations
- IA32_APIC_BASE is read only with MSR and APIC support; its BSP bit verifies
  that the observer is the bootstrap processor
- MTRRCAP and MTRR_DEF_TYPE are read only with MSR and MTRR support
- PAT is read only with MSR and PAT support
- CPUID extended leaves report the physical-address width used by production
- The fixture records CR0, CR4, IF, capture return/valid, and unchanged controls
- The loader clears IF and DF; it never changes CR0, CR4, or memory-type MSRs

The production AP-preparation function is removed by linker garbage collection.
The linked ELF is audited with the mandatory kernel integer-only policy and a
separate check rejecting CR-destination, WRMSR, WBINVD, INVD, CLTS, LMSW, and
XSETBV instructions. Debug output uses port E9. The fixture does not start APs,
normalize cache mode, rewrite MTRRs/PAT, or test shared mappings.

## Environments and evidence

Defaults use the existing unpacked official Bochs 3.0 executable, its
BIOS-bochs-latest, and the separately existing SeaBIOS image under
`../gtos-runtime/root/usr/share/seabios/bios.bin`. Both firmware files remain
unmodified. The model is `corei7_sandy_bridge_2600k`, memory is 64 MiB, and the
matrix has one and four configured CPUs. The runner uses the existing test
suite's process-local loopback-only RFB shim and drains display output; it does
not inject keyboard or mouse input.

Each run retains guest and Bochs logs, exact configuration, tool command,
firmware/emulator/ISO hashes, and a completion result. The build retains the ELF,
link map, disassembly, integer-only and read-only audit results, source snapshot,
before/after source manifests, compiler versions, and GRUB log. A completion
result means the observation reached its end, not that production capture or
AP admission succeeded. The host terminates the halted emulator after the
completion marker, so process return code -15 is expected on successful runs.

Environment overrides:

- `GTOS_FP_FIRMWARE_OUTPUT`: evidence directory
- `GTOS_BOCHS_ROOT`: unpacked Bochs package root
- `GTOS_GRUB_MKRESCUE`: GRUB rescue-image builder
- `GTOS_FP_FIRMWARE_CPUS`: CPU-count list (default `1 4`)
- `GTOS_FP_FIRMWARE_VARIANTS`: firmware list (default `stock seabios`)
- `GTOS_FP_FIRMWARE_IPS`: total Bochs IPS (default `300000000`)
- `GTOS_FP_FIRMWARE_TIMEOUT`: per-run observation deadline in seconds (default 180)

The focused Python runner also accepts an explicit BIOS path, CPU model, CPU
count, IPS, and timeout for reproducing an individual case. IPS is fixed across
the default matrix; this short observation has no native-FP workload requiring
per-CPU timer scaling.

## Interpretation

The first production rejection is exactly:

```cpp
!(cr0 & 1U) || (cr0 & 0xE0000000U)
```

In other words, protected mode must be enabled and paging, cache-disable, and
not-write-through must all be clear. If this predicate is true, capture sets
`valid=0` and returns without reaching its CPUID/MTRR/PAT validation. Qualifying
and printing those registers independently does not imply that later validation
would pass, nor that changing CR0 would be a safe fix. Full AP compatibility and
shared-memory safety require their own existing production acceptance tests.
