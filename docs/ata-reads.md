# Contiguous application reads

`BlockDevice::ReadSectors(first, count, destination)` preserves a contiguous read
through `FileStore` and `BlockHeaderCache`. Sector-only devices keep a checked
fallback. A request has at least one 512-byte sector; null pointers, disk-range
overflow and address wrap are rejected before device access. A device failure
may leave completed sectors in the destination and returns false immediately.

The polled LBA28 ATA driver issues `READ SECTORS` commands of at most 256 sectors.
The sector-count register uses zero for 256. Every sector still waits for DRQ,
transfers exactly 256 words, and the command's final status is checked. Existing
partial `Read28` requests still drain one full sector while preserving bytes
past the requested prefix. Write and live-boot protection policy are unchanged.

The metadata cache forwards multi-sector reads as one device request and caches
successfully read aligned block headers. Single-sector cache hits and write
invalidation retain their previous behavior. Failed batches populate no cache
entries. LittleFS zero-length callbacks remain no-ops.

`tests/ata_read_test.sh` compiles the actual ATA source with counted port I/O and
the actual cache, at O0/O2 with ASan/UBSan and as a freestanding i386 image. Only
the host build substitutes its IF observation; the i386 image runs the original
flags instruction. Coverage includes 1/2/8/16/255/256/257/513-sector requests,
unaligned buffers, the final LBA28 address, argument rejection before I/O,
mid-command and final-status failure, partial reads, fallback and cache rules.
A control calls the real single-sector API 513 times and compares every byte
with the batch path. Command reduction is a transport measurement; it does not
establish physical-disk throughput or Chromium startup latency.

The default entry builds and runs on an ordinary host. Restricted execution uses
`build <fresh-output>` to publish three immutable test images, then `run` in a
separate guard stage with an explicit i386 executor and a writable root outside
publication, manifest and record. The suite uses these two stages.
