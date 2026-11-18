# Chromium assets on NVMe

The `content-nvme` boot option selects the NVMe namespace for the existing
LittleFS Content volume. It also requires `content-shell content-assets`.
The disk must have 512-byte logical sectors and a sector count that fits the
FileStore interface. The volume layout and Chromium asset paths stay the same
as on ATA. Mount never formats an unrecognized disk.

The controller starts after its uncached MMIO mapping is active. A PAE boot
can use a firmware-assigned 64-bit BAR above 4 GiB. The mapper checks the
CPU's physical address width and the firmware memory map, then maps the BAR
into a free supervisor window above the native application address range.
Each process inherits the validated supervisor mapping. The disk, DMA buffers
and mounted endpoint remain alive until the browser processes have exited and
their resources have been reclaimed. With `content-profile-write` enabled,
profile updates use the NVMe write and flush commands.

This device is exclusive to the Content volume. Combining it with `live`,
`nvme-probe` or `nvme-files` is refused before controller initialization.
The ordinary GPT/NTFS Shell path still uses the full logical disk geometry
through ReadableDisk.

Read and write share the bounded queue and owned DMA slots. Writes copy the
caller bytes before posting commands. Completion IDs select the appropriate
slot even when completions arrive out of order. A failed or timed-out command
stops the controller; DMA is reclaimed only after reset acknowledgement.
Flush waits for its own completion. The NVM command encodings follow the
[NVM Express command specification](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-Revision-1.2-2025.08.01-Ratified.pdf).

The Content volume supplies 4 KiB LittleFS caches for both NVMe and ATA.
The minimum I/O size is still one sector. A file block stores its CTZ skip
links before its data, so an unaligned read takes the buffering path. A
block-sized buffer allows LittleFS to request the rest of that block at once.
The caller owns the 264 KiB of buffers for the lifetime of the mount.

`tests/nvme_io_test.sh` exercises the production transfer and FileStore code
against a device boundary spy, including queue/ID wrap, PRP page/list choices,
unaligned buffers, invalid ranges, controller errors, timeout and failed reset.
Its file case compares sector- and block-sized reads of the same data and
checks that blocking reduces actual commands. It also verifies synchronized
writes across open readers and reads the same volume again after remounting.

The browser comparison binds the Chromium ELF and resource disk by SHA-256.
Both guests use 8 GiB, eight vCPUs, the same display and QEMU accelerator.
Measure wall time from QEMU launch to a visible first document and verify
clean exit and reclamation. QEMU `snapshot=on` keeps each browser profile
write in the run's own overlay.

The workload in `tests/fixtures/content-file-speed.c` uses the same kernel
and volume mount as the browser. It verifies a seeded 32 MiB file, writes a
second 32 MiB file with an explicit sync and close, and reopens that file to
compare every byte. Measure read and synchronized write durations between
the workload's markers. Both transports use a private raw image with QEMU
host page caching disabled for this workload.

QEMU 8.2.2 handles NVMe Flush through `blk_aio_flush` and leaves the NVMe
backend's `query-blockstats.flush_operations` at zero. Use
`pci_nvme_flush_ns` to observe the controller command and check the guest's
sync result. The ATA backend accounts for its flushes in QMP.
[QEMU NVMe implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/nvme/ctrl.c#L3101-L3165)
