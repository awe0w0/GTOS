# Dedicated GTOS app storage v1

GTOS now performs real ATA PIO reads/writes for application installation,
listing, launching and removal. It intentionally uses a small, bounded custom
store on a dedicated data image. It is **not** a full disk manager, partition
editor, FAT/NTFS/ext filesystem, secure erasure tool or general file browser.
Do not attach a valuable physical disk to this experimental kernel.

## Safe setup

Create a new blank, explicitly GTOS-formatted image on the host:

```sh
python3 tools/disk.py create gtos-data.img --size-mib 8
```

Creation uses exclusive-create mode: existing files, symlinks and device paths
are never reformatted. The OS has no format command and will mount/write only
if sector zero contains the exact geometry, format version and valid checksum
for this dedicated store. A missing, ordinary blank or unrecognized disk stays
read-only to the application store. Attach it to the primary IDE master in
QEMU, for example `-drive file=gtos-data.img,format=raw,if=ide,index=0`.

The shell receives `apps/catch.gtapp` as a GRUB Multiboot module. Installing it
copies the validated module into the data disk. Running an installed app reads
and revalidates the disk copy; removing it commits a new app directory. A reboot
without an installer module can still run installed apps. Consult the project
README for desktop controls and build/run targets.

Inspect or manage an image while the VM is stopped:

```sh
python3 tools/disk.py list gtos-data.img
python3 tools/disk.py check gtos-data.img
python3 tools/disk.py install gtos-data.img apps/catch.gtapp --offline
python3 tools/disk.py uninstall gtos-data.img catch --offline
```

The `--offline` flag acknowledges that no VM is using the image. The host tool
also rejects symlinks/non-regular files and uses an advisory lock, but that lock
cannot guarantee another program honors it. Never mutate a live QEMU image.

## Limits and on-disk format

All integers are little-endian. Sectors are exactly 512 bytes. The store uses
only sectors 0 through 258 (132,608 bytes) regardless of image size. There are
eight logical installed-app entries, sixteen copy-on-write payload slots, and a
maximum package size of 8,192 bytes. Larger disk capacity is reported by ATA
but is not available as general user storage in this version.

- Sector 0: immutable superblock; magic `GTSTOR1\0`, version 1, sector size 512,
  total sectors 259, slot count 16, slot sectors 16, max apps 8, data start 3
- Sectors 1 and 2: alternate directory snapshots; magic `GTDIR01\0`, generation
  counter and entry count, followed by eight fixed 60-byte entries
- Each entry: 24-byte app ID, 24-byte title, slot number, package length and
  full-package CRC32; strings are NUL-terminated and zero-padded
- Sectors 3 onward: 16 slots of 16 sectors each; unused payload bytes are zero
- Each metadata sector ends with CRC32 over its first 508 bytes; all reserved
  bytes must be zero

The superblock's seven u32 fields begin at offset 8. Directory generation and
count are at offsets 8 and 12, entries begin at offset 24, and the final four
reserved bytes begin at offset 504. The directory checksum is at offset 508.
The package's independent header/checksum format is documented in `apps.md`.

## Commit and recovery behavior

Install validates the entire package before writing. It chooses a payload slot
referenced by neither valid directory snapshot, writes and flushes its contents,
and verifies them by reading them back. It then writes the inactive directory
with the incremented generation, flushes it and verifies exact readback. Only
then does the mounted in-memory directory switch. Replacing the same app ID is
also copy-on-write and does not consume a second logical entry.

Uninstall commits a directory without that app. It does not immediately erase
old payloads: they may remain in the prior snapshot and are reclaimed by later
installs only when neither snapshot references them. Removal is not secure
erasure. If the latest metadata is damaged, mounting falls back to the previous
valid snapshot, which may undo the last install or removal. If both snapshots
are invalid, mounting fails without writes. A package with corrupt contents is
listed from valid metadata but cannot be loaded/executed.

The scheme limits ordinary interrupted-write damage; it is not a guarantee
against failing hardware, dishonest flush behavior, arbitrary corruption of
both snapshots, malicious disk modifications or CRC32 collisions. CRC32 detects
accidental corruption; it is not a digital signature or authentication scheme.
After a failed metadata commit, the store marks itself unmounted because the
outcome is uncertain and requires a remount/reboot before more mutations.

## ATA support

The driver supports polling ATA PIO, LBA28 and 512-byte logical sectors only.
It detects master/slave devices, reports model and capacity, checks command
errors, waits for data readiness, drains complete sectors, bounds every polling
loop, and flushes the device write cache for commits. It does not implement
AHCI/NVMe, LBA48, DMA, removable media, partition discovery or automatic repair.

## Tests

```sh
python3 tests/storage_tool_test.py
tests/storage_test.sh
```

The C++ script compiles the actual kernel store for i386 and runs it directly
or through `qemu-i386` when available; on an x86_64 host that cannot execute
i386 and has no emulator, it additionally builds and runs the same sources
natively. A fault-injecting in-memory block device exercises the storage code. They cover invalid packages, capacity, replacement/uninstall/
reinstall, repeated slot reuse, persistent remounts, payload corruption, metadata
fallback, invalid geometry, absent disks and 34 interrupted/torn-install cases.
Host-image tests independently cover CLI safety and Python-format behavior.
Real ATA/QEMU end-to-end checks are reported separately; these unit tests alone
do not prove correctness on physical hardware.
